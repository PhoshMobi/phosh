/*
 * Copyright (C) 2025-2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-brightness-manager"

#include "phosh-config.h"

#include "auto-brightness.h"
#include "auto-brightness-bucket.h"
#include "brightness-manager.h"
#include "shell-priv.h"
#include "util.h"

#define KEYBINDINGS_SCHEMA_ID "org.gnome.shell.keybindings"
#define KEYBINDING_KEY_BRIGHTNESS_UP "screen-brightness-up"
#define KEYBINDING_KEY_BRIGHTNESS_DOWN "screen-brightness-down"
#define KEYBINDING_KEY_BRIGHTNESS_UP_MONITOR "screen-brightness-up-monitor"
#define KEYBINDING_KEY_BRIGHTNESS_DOWN_MONITOR "screen-brightness-down-monitor"

#define POWER_SCHEMA_ID "org.gnome.settings-daemon.plugins.power"

/**
 * PhoshBrightnessManager:
 *
 * Manage backglight brightness. Handle auto-brightness and maintain a
 * `GtkAdjustment` that can be used for brightness sliders.
 *
 * For auto brightness the `PhoshBrightnessManager` gets the ambient
 * brightness from the `PhoshAmbient` manager and feeds these values
 * to a `PhoshAutoBrightness` tracker that calculates the resulting
 * backlight brightness. Based on other inputs like the currently
 * applied offset as set by the user the `PhoshBrightnessManager`
 * then sets the actual brightness on the backlight.
 */

enum {
  PROP_0,
  PROP_AUTO_BRIGHTNESS_ENABLED,
  PROP_ICON_NAME,
  LAST_PROP,
};
static GParamSpec *props[LAST_PROP];

#define MAX_KEYBOARD_LEVELS 20

struct _PhoshBrightnessManager {
  PhoshDBusBrightnessSkeleton parent;

  GStrv           action_names;
  GSettings      *settings;
  PhoshBacklight *backlight;
  GtkAdjustment  *adjustment;
  gulong          value_changed_id;
  gboolean        setting_brightness;

  GSettings      *settings_power;
  gboolean        dimmed;
  struct {
    gboolean enabled;
    PhoshAutoBrightness *tracker;
    double   base;
    double   offset;
  } auto_brightness;

  struct {
    double target;
    double step;
    uint   id;
  } transition;

  const char *icon_name;
  int         dbus_name_id;
  double      saved_brightness;
};

static void phosh_brightness_manager_brightness_init (PhoshDBusBrightnessIface *iface);

G_DEFINE_TYPE_WITH_CODE (PhoshBrightnessManager,
                         phosh_brightness_manager,
                         PHOSH_DBUS_TYPE_BRIGHTNESS_SKELETON,
                         G_IMPLEMENT_INTERFACE (PHOSH_DBUS_TYPE_BRIGHTNESS,
                                                phosh_brightness_manager_brightness_init))

static gboolean
on_transition_step (gpointer user_data)
{
  PhoshBrightnessManager *self = user_data;
  double current, next;

  current = phosh_backlight_get_relative (self->backlight);
  next = current + self->transition.step;

  if (!self->auto_brightness.enabled) {
    g_debug ("Brightness transition aborted");
    self->transition.target = current;

    goto end;
  }

  if ((self->transition.step > 0 && next >= self->transition.target) ||
      (self->transition.step < 0 && next <= self->transition.target)) {
    g_debug ("Brightness transition done at %f", self->transition.target);
    phosh_backlight_set_relative (self->backlight, self->transition.target);

    goto end;
  }

  g_debug ("Brightness transition step: current %.3f, next %.3f, step: %.3f, target: %.3f",
           current, next, self->transition.step, self->transition.target);
  phosh_backlight_set_relative (self->backlight, next);
  return G_SOURCE_CONTINUE;

 end:
  self->transition.id = 0;
  return G_SOURCE_REMOVE;
}

/* Human eye adapts faster to higher brightness values */
#define AUTO_UP_INTERVAL   150 /* ms */
#define AUTO_DOWN_INTERVAL 400 /* ms */

static void
transition_to_brightness (PhoshBrightnessManager *self, double target)
{
  double current;
  int interval;

  current = phosh_backlight_get_relative (self->backlight);

  self->transition.target = target;
  if (G_APPROX_VALUE (current, self->transition.target, FLT_EPSILON))
    return;

  self->transition.step = 1.0 / phosh_backlight_get_levels (self->backlight);
  /* Don't do too many steps, even for large changes */
  self->transition.step = MAX (0.025, self->transition.step);
  if (self->transition.target < current)
    self->transition.step *= -1.0;

  g_debug ("Starting auto brightness transition from %.2f to %.2f in steps of %f",
           current, target, self->transition.step);

  interval = self->transition.step > 1.0 ? AUTO_UP_INTERVAL : AUTO_DOWN_INTERVAL;
  g_clear_handle_id (&self->transition.id, g_source_remove);
  self->transition.id = g_timeout_add (interval, on_transition_step, self);
}


static double
calc_auto_brightness (PhoshBrightnessManager *self)
{
  double new_brightness = self->auto_brightness.base;

  /* Apply any offset the user has set */
  new_brightness += self->auto_brightness.offset;
  new_brightness = CLAMP (new_brightness, 0.0, 1.0);

  g_debug ("New auto brightness %.2f (base: %.2f, offset: %.2f)",
           new_brightness,
           self->auto_brightness.base,
           self->auto_brightness.offset);

  return new_brightness;
}


static void
on_auto_brightness_changed (PhoshBrightnessManager *self)
{
  double new_brightness;

  g_return_if_fail (PHOSH_IS_BRIGHTNESS_MANAGER (self));

  if (!self->backlight)
    return;

  if (!self->auto_brightness.enabled)
    return;

  new_brightness = phosh_auto_brightness_get_brightness (self->auto_brightness.tracker);
  /* TODO: clamp to 100% as we don't do brightness boosts yet */
  self->auto_brightness.base = CLAMP (new_brightness, 0.0, 1.0);
  new_brightness = calc_auto_brightness (self);

  transition_to_brightness (self, new_brightness);
}


static void
set_auto_brightness_tracker (PhoshBrightnessManager *self)
{
  if (self->auto_brightness.tracker)
    return;

  /* TODO: allow for different brightness trackers */
  self->auto_brightness.tracker = PHOSH_AUTO_BRIGHTNESS (phosh_auto_brightness_bucket_new ());
  g_signal_connect_swapped (self->auto_brightness.tracker,
                            "notify::brightness",
                            G_CALLBACK (on_auto_brightness_changed),
                            self);
}


static void
on_ambient_auto_brightness_changed (PhoshBrightnessManager *self,
                                    GParamSpec             *pspec,
                                    PhoshAmbient           *ambient)
{
  gboolean enabled = phosh_ambient_get_auto_brightness (ambient);
  double value;

  g_debug ("Ambient auto-brightness enabled: %d", enabled);

  if (self->auto_brightness.enabled == enabled)
    return;

  self->auto_brightness.enabled = enabled;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_AUTO_BRIGHTNESS_ENABLED]);

  self->icon_name = enabled ? "auto-brightness-symbolic" : "display-brightness-symbolic";
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ICON_NAME]);

  if (self->auto_brightness.enabled) {
    value = self->auto_brightness.offset + 0.5;
    set_auto_brightness_tracker (self);
    on_auto_brightness_changed (self);
  } else {
    value = phosh_backlight_get_relative (self->backlight);
  }

  gtk_adjustment_set_value (self->adjustment, value);
}


static void
on_ambient_light_level_changed (PhoshBrightnessManager *self,
                                GParamSpec             *pspec,
                                PhoshAmbient           *ambient)
{
  double level;

  if (!self->auto_brightness.enabled)
    return;

  level = phosh_ambient_get_light_level (ambient);
  g_debug ("Ambient light level: %.2f lux", level);

  phosh_auto_brightness_add_ambient_level (self->auto_brightness.tracker, level);
}


static void
on_name_acquired (GDBusConnection *connection, const char *name, gpointer user_data)
{
  g_debug ("Acquired name %s", name);
}


static void
on_name_lost (GDBusConnection *connection, const char *name, gpointer user_data)
{
  g_debug ("Lost or failed to acquire name %s", name);
}


static void
on_bus_acquired (GDBusConnection *connection, const char *name, gpointer user_data)
{
  g_autoptr (GError) err = NULL;
  PhoshBrightnessManager *self = PHOSH_BRIGHTNESS_MANAGER (user_data);

  /* We need to use GNOME Shell's object path here to make g-s-d happy */
  if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                         connection,
                                         "/org/gnome/Shell/Brightness",
                                         &err)) {
    g_warning ("Failed export brightness interface: %s", err->message);
  }
}


static gboolean
phosh_brightness_manager_handle_set_auto_brightness_target (PhoshDBusBrightness   *object,
                                                            GDBusMethodInvocation *invocation,
                                                            gdouble                arg_target)
{
  g_debug ("Target brightness: %f", arg_target);

  /* Nothing to do here, we handle it internally */
  /* https://gitlab.gnome.org/GNOME/gnome-settings-daemon/-/merge_requests/442 */
  phosh_dbus_brightness_complete_set_auto_brightness_target (object, invocation);
  return TRUE;
}


static gboolean
phosh_brightness_manager_handle_set_dimming (PhoshDBusBrightness   *object,
                                             GDBusMethodInvocation *invocation,
                                             gboolean               arg_enable)
{
  PhoshBrightnessManager *self = PHOSH_BRIGHTNESS_MANAGER (object);
  double target;

  g_debug ("Dimming: %s", arg_enable ? "enabled" : "disabled");

  if (!self->backlight) {
    g_dbus_method_invocation_return_error (invocation,
                                           G_DBUS_ERROR,
                                           G_DBUS_ERROR_FILE_NOT_FOUND,
                                           "No backlight");
    return TRUE;
  }

  if (arg_enable) {
    double current = phosh_backlight_get_relative (self->backlight);

    target = g_settings_get_int (self->settings_power, "idle-brightness") * 0.01;

    /* If current brightness is lower than dim brightness don't do anything */
    if (target >= current) {
      self->saved_brightness = -1.0;
      goto done;
    }

    self->saved_brightness = current;
  } else {
    target = self->saved_brightness;
    self->saved_brightness = -1.0;
  }

  if (target >= 0.0)
    phosh_backlight_set_relative (self->backlight, target);

 done:
  phosh_dbus_brightness_complete_set_dimming (object, invocation);
  return TRUE;
}


static gboolean
phosh_brightness_manager_handle_get_has_brightness_control (PhoshDBusBrightness *object)
{
  PhoshBrightnessManager *self = PHOSH_BRIGHTNESS_MANAGER (object);

  return !!self->backlight;
}


static void
phosh_brightness_manager_brightness_init (PhoshDBusBrightnessIface *iface)
{
  iface->handle_set_auto_brightness_target = phosh_brightness_manager_handle_set_auto_brightness_target;
  iface->handle_set_dimming = phosh_brightness_manager_handle_set_dimming;
  iface->get_has_brightness_control = phosh_brightness_manager_handle_get_has_brightness_control;
}


static void
on_backlight_brightness_changed (PhoshBrightnessManager *self,
                                 GParamSpec             *pspec,
                                 PhoshBacklight         *backlight)
{
  double value;

  g_assert (self->backlight == backlight);

  /* With auto brightness the slider gives an offset to the auto brightness target */
  if (self->auto_brightness.enabled)
    return;

  if (self->setting_brightness)
    return;

  value = phosh_backlight_get_relative (self->backlight);

  g_signal_handler_block (self->adjustment, self->value_changed_id);
  gtk_adjustment_set_value (self->adjustment, value);
  g_signal_handler_unblock (self->adjustment, self->value_changed_id);
}


static void
on_value_changed (PhoshBrightnessManager *self, GtkAdjustment *adjustment)
{
  double value, new_brightness;

  g_assert (self->adjustment == adjustment);

  if (!self->backlight)
    return;

  value = gtk_adjustment_get_value (self->adjustment);

  /* With auto brightness the slider gives an offset to the auto brightness target */
  if (self->auto_brightness.enabled) {
    /* TODO: should we go through the brightness curve? */
    /* TODO: preserve as setting */
    /* Auto-brightness offset is [-0.5, +0.5] */
    double offset = CLAMP (value - 0.5, -0.5, 0.5);

    if (G_APPROX_VALUE (offset, self->auto_brightness.offset, FLT_EPSILON))
      return;
    self->auto_brightness.offset = offset;

    new_brightness = calc_auto_brightness (self);
    /* Cancel any ongoing transition, the user likely wants the new brightness right away */
    g_clear_handle_id (&self->transition.id, g_source_remove);
  } else {
    new_brightness = value;
  }

  self->setting_brightness = TRUE;
  phosh_backlight_set_relative (self->backlight, new_brightness);
  self->setting_brightness = FALSE;
}


static void
set_backlight (PhoshBrightnessManager *self, PhoshBacklight *backlight)
{
  if (self->backlight == backlight)
    return;

  if (self->backlight)
    g_signal_handlers_disconnect_by_data (self->backlight, self);

  g_set_object (&self->backlight, backlight);
  self->saved_brightness = -1.0;

  if (self->backlight) {
    g_debug ("Found %s for brightness control", phosh_backlight_get_name (self->backlight));

    g_signal_connect_swapped (self->backlight,
                              "notify::brightness",
                              G_CALLBACK (on_backlight_brightness_changed),
                              self);
    if (self->auto_brightness.enabled)
      on_auto_brightness_changed (self);
    else
      on_backlight_brightness_changed (self, NULL, self->backlight);
  }
}


static void
on_primary_monitor_changed (PhoshBrightnessManager *self, GParamSpec *psepc, PhoshShell *shell)
{
  PhoshMonitor *monitor = phosh_shell_get_primary_monitor (shell);
  PhoshBacklight *backlight = NULL;

  if (monitor && monitor->backlight)
    backlight = monitor->backlight;

  /* Fall back to built in display */
  if (!backlight)
    monitor = phosh_shell_get_builtin_monitor (shell);

  if (monitor)
    backlight = monitor->backlight;

  set_backlight (self, backlight);
  phosh_dbus_brightness_set_has_brightness_control (PHOSH_DBUS_BRIGHTNESS (self),
                                                    !!self->backlight);
}


static void
show_osd (PhoshBrightnessManager *self, double brightness)
{
  PhoshShell *shell = phosh_shell_get_default ();

  if (phosh_shell_get_state (shell) & PHOSH_STATE_SETTINGS)
    return;

  phosh_shell_show_osd (shell,
                        NULL,
                        self->icon_name,
                        NULL,
                        100.0 * brightness,
                        100.0);
}


static void
adjust_brightness (PhoshBrightnessManager *self, gboolean up)
{
  int levels;
  double brightness, step;

  if (!self->backlight)
    return;

  levels = phosh_backlight_get_levels (self->backlight);
  levels = MIN (MAX_KEYBOARD_LEVELS, levels);
  step = 1.0 / levels;
  brightness = phosh_backlight_get_relative (self->backlight);

  if (up)
    brightness += step;
  else
    brightness -= step;

  brightness = CLAMP (brightness, 0.0, 1.0);
  phosh_backlight_set_relative (self->backlight, brightness);

  show_osd (self, brightness);
}


static void
on_brightness_up (GSimpleAction *action, GVariant *param, gpointer data)
{
  PhoshBrightnessManager *self = PHOSH_BRIGHTNESS_MANAGER (data);

  adjust_brightness (self, TRUE);
}


static void
on_brightness_down (GSimpleAction *action, GVariant *param, gpointer data)
{
  PhoshBrightnessManager *self = PHOSH_BRIGHTNESS_MANAGER (data);

  adjust_brightness (self, FALSE);
}


static void
add_keybindings (PhoshBrightnessManager *self)
{
  g_autoptr (GArray) actions = g_array_new (FALSE, TRUE, sizeof (GActionEntry));
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();

  PHOSH_UTIL_BUILD_KEYBINDING (actions,
                               builder,
                               self->settings,
                               KEYBINDING_KEY_BRIGHTNESS_UP,
                               on_brightness_up);
  PHOSH_UTIL_BUILD_KEYBINDING (actions,
                               builder,
                               self->settings,
                               KEYBINDING_KEY_BRIGHTNESS_DOWN,
                               on_brightness_down);
  /* TODO: use current monitor */
  PHOSH_UTIL_BUILD_KEYBINDING (actions,
                               builder,
                               self->settings,
                               KEYBINDING_KEY_BRIGHTNESS_UP_MONITOR,
                               on_brightness_up);
  /* TODO: use current monitor */
  PHOSH_UTIL_BUILD_KEYBINDING (actions,
                               builder,
                               self->settings,
                               KEYBINDING_KEY_BRIGHTNESS_DOWN_MONITOR,
                               on_brightness_down);

  phosh_shell_add_global_keyboard_action_entries (phosh_shell_get_default (),
                                                  (GActionEntry *)actions->data,
                                                  actions->len,
                                                  self);
  self->action_names = g_strv_builder_end (builder);
}


static void
on_keybindings_changed (PhoshBrightnessManager *self)
{
  g_debug ("Updating keybindings in BrightnessManager");
  phosh_shell_remove_global_keyboard_action_entries (phosh_shell_get_default (),
                                                     self->action_names);
  g_clear_pointer (&self->action_names, g_strfreev);
  add_keybindings (self);
}


static void
phosh_brightness_manager_get_property (GObject    *object,
                                       guint       property_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  PhoshBrightnessManager *self = PHOSH_BRIGHTNESS_MANAGER (object);

  switch (property_id) {
  case PROP_AUTO_BRIGHTNESS_ENABLED:
    g_value_set_boolean (value, self->auto_brightness.enabled);
    break;
  case PROP_ICON_NAME:
    g_value_set_string (value, self->icon_name);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_brightness_manager_dispose (GObject *object)
{
  PhoshBrightnessManager *self = PHOSH_BRIGHTNESS_MANAGER (object);

  g_clear_handle_id (&self->transition.id, g_source_remove);
  g_clear_handle_id (&self->dbus_name_id, g_bus_unown_name);

  if (g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (self)))
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));

  set_backlight (self, NULL);
  g_clear_pointer (&self->action_names, g_strfreev);
  g_clear_object (&self->settings);
  g_clear_object (&self->settings_power);
  g_clear_signal_handler (&self->value_changed_id, self->adjustment);
  g_clear_object (&self->adjustment);

  g_clear_object (&self->auto_brightness.tracker);

  G_OBJECT_CLASS (phosh_brightness_manager_parent_class)->dispose (object);
}


static void
phosh_brightness_manager_class_init (PhoshBrightnessManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = phosh_brightness_manager_dispose;
  object_class->get_property = phosh_brightness_manager_get_property;

  /**
   * PhoshBrightnessManager:auto-brightness-enabled:
   *
   * If `TRUE` the display brightness is currently being adjusted to
   * ambient light levels
   */
  props[PROP_AUTO_BRIGHTNESS_ENABLED] =
    g_param_spec_boolean ("auto-brightness-enabled", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshBrightnessManager:icon-name:
   *
   * An icon suitable for display in a brightness slider
   */
  props[PROP_ICON_NAME] =
    g_param_spec_string ("icon-name", "", "",
                         NULL,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}


static void
phosh_brightness_manager_init (PhoshBrightnessManager *self)
{
  PhoshShell *shell = phosh_shell_get_default ();
  GSettingsSchemaSource *source = g_settings_schema_source_get_default ();
  g_autoptr (GSettingsSchema) schema = NULL;
  PhoshAmbient *ambient = phosh_shell_get_ambient (phosh_shell_get_default ());

  self->saved_brightness = -1.0;
  self->icon_name = "display-brightness-symbolic";
  self->settings_power = g_settings_new (POWER_SCHEMA_ID);
  self->adjustment = g_object_ref_sink (gtk_adjustment_new (0, 0, 1.0, 0.01, 0.01, 0));
  self->value_changed_id = g_signal_connect_swapped (self->adjustment,
                                                     "value-changed",
                                                     G_CALLBACK (on_value_changed),
                                                     self);

  g_signal_connect_object (shell,
                           "notify::primary-monitor",
                           G_CALLBACK (on_primary_monitor_changed),
                           self,
                           G_CONNECT_SWAPPED);
  on_primary_monitor_changed (self, NULL, shell);

  self->dbus_name_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                       "org.gnome.Shell.Brightness",
                                       G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                       G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                       on_bus_acquired,
                                       on_name_acquired,
                                       on_name_lost,
                                       self,
                                       NULL);

  if (ambient) {
    g_object_connect (ambient,
                      "swapped-object-signal::notify::auto-brightness-enabled",
                      on_ambient_auto_brightness_changed,
                      self,
                      "swapped-object-signal::notify::light-level",
                      on_ambient_light_level_changed,
                      self,
                      NULL);
  }

  /* TODO: Drop once we can rely on GNOME 49 schema for the keybindings */
  schema = g_settings_schema_source_lookup (source, KEYBINDINGS_SCHEMA_ID, TRUE);
  if (!schema)
    return;

  if (!g_settings_schema_has_key (schema, KEYBINDING_KEY_BRIGHTNESS_UP))
    return;

  self->settings = g_settings_new (KEYBINDINGS_SCHEMA_ID);
  g_signal_connect_object (self->settings,
                           "changed",
                           G_CALLBACK (on_keybindings_changed),
                           self,
                           G_CONNECT_SWAPPED);
  add_keybindings (self);
}


PhoshBrightnessManager *
phosh_brightness_manager_new (void)
{
  return g_object_new (PHOSH_TYPE_BRIGHTNESS_MANAGER, NULL);
}


GtkAdjustment *
phosh_brightness_manager_get_adjustment (PhoshBrightnessManager *self)
{
  g_return_val_if_fail (PHOSH_IS_BRIGHTNESS_MANAGER (self), NULL);

  return self->adjustment;
}


gboolean
phosh_brightness_manager_get_auto_brightness_enabled (PhoshBrightnessManager *self)
{
  g_return_val_if_fail (PHOSH_IS_BRIGHTNESS_MANAGER (self), FALSE);

  return self->auto_brightness.enabled;
}

/**
 * phosh_brightness_manager_get_value:
 * @PhoshBrightnessManager: The brightness manager
 *
 * Get the value of the brightness adjustment. The interpretation of the value depends
 * on whether auto brightness is enabled or not.
 *
 * Returns: The current value of the adjustment [0.0, 1.0]
 */
double
phosh_brightness_manager_get_value (PhoshBrightnessManager *self)
{
  g_return_val_if_fail (PHOSH_IS_BRIGHTNESS_MANAGER (self), 0.5);

  return gtk_adjustment_get_value (self->adjustment);
}

/**
 * phosh_brightness_manager_set_value:
 * @PhoshBrightnessManager: The brightness manager
 * @value: The brightness adjustment value [0.0, 1.0].
 * @osd: Whether to show the osd when setting the value
 *
 * Set the value of the brightness adjustment. The interpretation of the value depends
 * on whether auto brightness is enabled or not.
 */
void
phosh_brightness_manager_set_value (PhoshBrightnessManager *self,
                                    double                  value,
                                    gboolean                osd)
{
  g_return_if_fail (PHOSH_IS_BRIGHTNESS_MANAGER (self));
  g_return_if_fail (0.0 <= value && value <= 1.0);

  gtk_adjustment_set_value (self->adjustment, value);
  if (osd)
    show_osd (self, value);
}
