/*
 * Copyright (C) 2018 Purism SPC
 *               2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 * Somewhat based on mutter's src/backends/meta-monitor-manager.c
 */

#define G_LOG_DOMAIN "phosh-monitor-manager"

#include "monitor-manager.h"
#include "monitor/head-priv.h"
#include "monitor/monitor.h"

#include "wlr-gamma-control-unstable-v1-client-protocol.h"
#include "phosh-wayland.h"
#include "shell-priv.h"

#include "util.h"

#include "dbus/gsd-color-dbus.h"

#include <gdk/gdkwayland.h>

#define GSD_COLOR_BUS_NAME "org.gnome.SettingsDaemon.Color"
#define GSD_COLOR_OBJECT_PATH "/org/gnome/SettingsDaemon/Color"

/**
 * PhoshMonitorManager:
 *
 * The singleton that manages available monitors
 *
 * This keeps track of all monitors and handles the
 * org.gnome.Mutter.DisplayConfig DBus interface via
 * #PhoshDBusDisplayConfig. This includes individual monitor
 * configuration as well as blanking/power saving.
 */

/* Equivalent to the 'layout-mode' enum in org.gnome.Mutter.DisplayConfig */
typedef enum PhoshMonitorMAnagerLayoutMode {
  PHOSH_MONITOR_MANAGER_LAYOUT_MODE_LOGICAL = 1,
  PHOSH_MONITOR_MANAGER_LAYOUT_MODE_PHYSICAL = 2
} PhoshMonitorManagerLayoutMode;

enum {
  PROP_0,
  PROP_SENSOR_PROXY_MANAGER,
  PROP_N_MONITORS,
  PROP_NIGHT_LIGHT_TEMP,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
  SIGNAL_MONITOR_ADDED,
  SIGNAL_MONITOR_REMOVED,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };

static void phosh_monitor_manager_display_config_init (
  PhoshDBusDisplayConfigIface *iface);

typedef struct _PhoshMonitorManager
{
  PhoshDBusDisplayConfigSkeleton parent;

  PhoshSensorProxyManager *sensor_proxy_manager;
  GBinding                *sensor_proxy_binding;

  PhoshDBusColor          *gsd_color_proxy;
  guint32                  night_light_temp;

  GPtrArray *monitors;   /* Currently known monitors */
  GPtrArray *heads;      /* Currently known heads */

  int dbus_name_id;
  int serial;

  PhoshHead *pending_primary;
  uint32_t zwlr_output_serial;

  GCancellable            *cancel;
} PhoshMonitorManager;

G_DEFINE_TYPE_WITH_CODE (PhoshMonitorManager,
                         phosh_monitor_manager,
                         PHOSH_DBUS_TYPE_DISPLAY_CONFIG_SKELETON,
                         G_IMPLEMENT_INTERFACE (
                           PHOSH_DBUS_TYPE_DISPLAY_CONFIG,
                           phosh_monitor_manager_display_config_init));


static const double known_diagonals[] = {
  12.1,
  13.3,
  15.6,
};


static char *
diagonal_to_str (double d)
{
  unsigned int i;

  for (i = 0; i < G_N_ELEMENTS (known_diagonals); i++) {
    double delta;

    delta = fabs (known_diagonals[i] - d);
    if (delta < 0.1)
      return g_strdup_printf ("%0.1lf\"", known_diagonals[i]);
  }

  return g_strdup_printf ("%d\"", (int) (d + 0.5));
}


static char*
get_display_name (PhoshHead *head)
{
  const char *vendor_name = NULL;
  const char *product_name = NULL;
  g_autofree char *inches = NULL;

  if (phosh_head_is_builtin (head))
    return g_strdup (_("Built-in display"));

  if (head->phys.width > 0 && head->phys.height > 0) {
    double d = sqrt (head->phys.width * head->phys.width +
                     head->phys.height * head->phys.height);
    inches = diagonal_to_str (d / 25.4);
  }

  if (head->product && g_strcmp0 (head->product, ""))
    product_name = head->product;

  if (head->vendor && g_strcmp0 (head->vendor, ""))
    vendor_name = head->vendor;

  if (vendor_name != NULL) {
    if (inches != NULL) {
      return g_strdup_printf (C_("This is a monitor vendor name, followed by a "
                                 "size in inches, like 'Dell 15\"'",
                                 "%s %s"),
                              vendor_name, inches);
    }
    if (product_name != NULL) {
      return g_strdup_printf (C_("This is a monitor vendor name followed by "
                                 "product/model name where size in inches "
                                 "could not be calculated, e.g. Dell U2414H",
                                 "%s %s"),
                              vendor_name, product_name);
    }
  }

  if (head->name)
    return g_strdup (head->name);

  /* Translators: An unknown monitor type */
  return g_strdup (_("Unknown"));
}


static PhoshHead *
phosh_monitor_manager_get_head_from_monitor (PhoshMonitorManager *self, PhoshMonitor *monitor)
{
  for (int i = 0; i < self->heads->len; i++) {
    PhoshHead *head = g_ptr_array_index (self->heads, i);
    if (!g_strcmp0 (monitor->name, head->name)) {
      return head;
    }
  }

  return NULL;
}


/*
 * DBus Interface
 */

static gboolean
phosh_monitor_manager_handle_get_resources (PhoshDBusDisplayConfig *skeleton,
                                            GDBusMethodInvocation  *invocation)
{
  PhoshMonitorManager *self = PHOSH_MONITOR_MANAGER (skeleton);
  GVariantBuilder crtc_builder, output_builder, mode_builder;
  PhoshMonitor *primary_monitor;
  PhoshHead *primary_head;

  g_debug ("DBus %s", __func__);

  if (phosh_monitor_manager_get_num_monitors (self) == 0) {
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                           G_DBUS_ERROR_ACCESS_DENIED,
                                           "No monitors found");
    return TRUE;
  }

  primary_monitor = phosh_shell_get_primary_monitor (phosh_shell_get_default ());
  if (!primary_monitor) {
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                           G_DBUS_ERROR_ACCESS_DENIED,
                                           "No primary monitor found");
    return TRUE;
  }
  primary_head = phosh_monitor_manager_get_head_from_monitor (self, primary_monitor);
  if (!primary_head) {
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                           G_DBUS_ERROR_ACCESS_DENIED,
                                           "No primary monitor found");
    return TRUE;
  }

  g_variant_builder_init (&crtc_builder, G_VARIANT_TYPE ("a(uxiiiiiuaua{sv})"));
  g_variant_builder_init (&output_builder, G_VARIANT_TYPE ("a(uxiausauaua{sv})"));
  g_variant_builder_init (&mode_builder, G_VARIANT_TYPE ("a(uxuudu)"));

  /* CRTCs (logical monitor) */
  for (int i = 0; i < self->heads->len; i++) {
    PhoshHead *head = g_ptr_array_index (self->heads, i);
    GVariantBuilder transforms;

    if (!head->enabled) {
      g_debug ("Skipping disabled %s", head->name);
      continue;
    }

    g_variant_builder_init (&transforms, G_VARIANT_TYPE ("au"));
    for (int j = 0; j <= WL_OUTPUT_TRANSFORM_FLIPPED_270; j++)
      g_variant_builder_add (&transforms, "u", j);

    g_variant_builder_add (&crtc_builder, "(uxiiiiiuaua{sv})",
                           (guint32)i,           /* ID */
                           (guint64)i,           /* crtc->crtc_id, */
                           (gint32)head->x,
                           (gint32)head->y,
                           (gint32)head->mode->width,
                           (gint32)head->mode->height,
                           (gint32)0,           /* current_mode_index, */
                           (guint32)head->transform,
                           &transforms,
                           NULL                 /* properties */);
  }

  /* outputs (physical screen) */
  for (int i = 0; i < self->heads->len; i++) {
    PhoshHead *head = g_ptr_array_index (self->heads, i);
    GVariantBuilder crtcs, modes, clones, properties;
    gboolean is_primary;

    g_variant_builder_init (&crtcs, G_VARIANT_TYPE ("au"));
    g_variant_builder_add (&crtcs, "u", i /* possible_crtc_index */);
    g_variant_builder_init (&modes, G_VARIANT_TYPE ("au"));
    g_variant_builder_add (&modes, "u", 0 /* mode_index */);
    g_variant_builder_init (&clones, G_VARIANT_TYPE ("au"));
    g_variant_builder_add (&clones, "u", -1 /* possible_clone_index */);
    g_variant_builder_init (&properties, G_VARIANT_TYPE ("a{sv}"));
    g_variant_builder_add (&properties, "{sv}", "vendor",
                           g_variant_new_string (head->vendor ?: ""));
    g_variant_builder_add (&properties, "{sv}", "product",
                           g_variant_new_string (head->product ?: ""));
    g_variant_builder_add (&properties, "{sv}", "width-mm",
                           g_variant_new_int32 (head->phys.width));
    g_variant_builder_add (&properties, "{sv}", "height-mm",
                           g_variant_new_int32 (head->phys.height));
    is_primary = (head == primary_head);
    g_variant_builder_add (&properties, "{sv}", "primary",
                           g_variant_new_boolean (is_primary));

    g_variant_builder_add (&output_builder, "(uxiausauaua{sv})",
                           (guint32)i, /* ID */
                           (guint64)i, /* output->winsys_id, */
                           (int) i,    /* crtc_index, */
                           &crtcs,
                           head->name, /* output->name */
                           &modes,
                           &clones,
                           &properties);
  }

  /* Don't bother setting up modes, they're ignored */

  phosh_dbus_display_config_complete_get_resources (
    skeleton,
    invocation,
    self->serial,
    g_variant_builder_end (&crtc_builder),
    g_variant_builder_end (&output_builder),
    g_variant_builder_end (&mode_builder),
    65535,  /* max_screen_width */
    65535   /* max_screen_height */
    );

  return TRUE;
}


static gboolean
phosh_monitor_manager_handle_get_crtc_gamma (PhoshDBusDisplayConfig *skeleton,
                                             GDBusMethodInvocation  *invocation,
                                             guint                   serial,
                                             guint                   crtc_id)
{
  PhoshMonitorManager *self = PHOSH_MONITOR_MANAGER (skeleton);
  PhoshMonitor *monitor;
  guint32 n_bytes = 0;
  g_autoptr (GBytes) red_bytes = NULL, green_bytes = NULL, blue_bytes = NULL;
  GVariant *red_v, *green_v, *blue_v;

  g_debug ("DBus call %s for crtc %d, serial %d", __func__, crtc_id, serial);

  if (serial != self->serial) {
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                           G_DBUS_ERROR_ACCESS_DENIED,
                                           "The requested configuration is based on stale information");
    return TRUE;
  }

  if (crtc_id >= self->monitors->len) {
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                           G_DBUS_ERROR_INVALID_ARGS,
                                           "Invalid crtc id %d", crtc_id);
    return TRUE;
  }

  monitor = g_ptr_array_index (self->monitors, crtc_id);

  /* All known clients using libgnome-desktop's
   * gnome_rr_crtc_get_gamma only do so to get the size of the gamma
   * table. So don't bother getting the real table since this is not
   * supported by wlroots: https://github.com/swaywm/wlroots/pull/1059.
   * Return an empty table instead.
   */
  if (phosh_monitor_has_gamma (monitor))
    n_bytes = monitor->n_gamma_entries * 2;

  g_debug ("Gamma table entries: %d", monitor->n_gamma_entries);
  red_bytes = g_bytes_new_take (g_malloc0 (n_bytes), n_bytes);
  green_bytes = g_bytes_new_take (g_malloc0 (n_bytes), n_bytes);
  blue_bytes = g_bytes_new_take (g_malloc0 (n_bytes), n_bytes);

  red_v = g_variant_new_from_bytes (G_VARIANT_TYPE ("aq"), red_bytes, TRUE);
  green_v = g_variant_new_from_bytes (G_VARIANT_TYPE ("aq"), green_bytes, TRUE);
  blue_v = g_variant_new_from_bytes (G_VARIANT_TYPE ("aq"), blue_bytes, TRUE);

  phosh_dbus_display_config_complete_get_crtc_gamma (skeleton, invocation, red_v, green_v, blue_v);

  return TRUE;
}


static gboolean
phosh_monitor_manager_handle_set_crtc_gamma (PhoshDBusDisplayConfig *skeleton,
                                             GDBusMethodInvocation  *invocation,
                                             guint                   serial,
                                             guint                   crtc_id,
                                             GVariant               *red_v,
                                             GVariant               *green_v,
                                             GVariant               *blue_v)
{
  g_debug ("DBus call %s for crtc %d, serial %d", __func__, crtc_id, serial);
  g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                         G_DBUS_ERROR_ACCESS_DENIED,
                                         "The requested is not supported anymore");
  return TRUE;
}


#define MODE_FORMAT "(siiddada{sv})"
#define MODES_FORMAT "a" MODE_FORMAT
#define MONITOR_SPEC_FORMAT "(ssss)"
#define MONITOR_FORMAT "(" MONITOR_SPEC_FORMAT MODES_FORMAT "a{sv})"
#define MONITORS_FORMAT "a" MONITOR_FORMAT

#define LOGICAL_MONITOR_MONITORS_FORMAT "a" MONITOR_SPEC_FORMAT
#define LOGICAL_MONITOR_FORMAT "(iidub" LOGICAL_MONITOR_MONITORS_FORMAT "a{sv})"
#define LOGICAL_MONITORS_FORMAT "a" LOGICAL_MONITOR_FORMAT


static void
build_mode (GVariantBuilder *modes_builder, PhoshHeadMode *mode, gboolean is_current)
{
  double scale = 1.0;
  GVariantBuilder supported_scales_builder, mode_properties_builder;
  const char *name = "default";
  int n;
  g_autofree float *scales = NULL;

  if (mode->name)
    name = mode->name;
  else if (!is_current) {
    g_warning ("Skipping unnamend mode %p", mode);
    return;
  }

  g_variant_builder_init (&supported_scales_builder, G_VARIANT_TYPE ("ad"));
  scales = phosh_util_calculate_supported_mode_scales (mode->width, mode->height, &n, TRUE);
  for (int l = 0; l < n; l++) {
    g_variant_builder_add (&supported_scales_builder, "d",
                           (double)scales[l]);
  }

  g_variant_builder_init (&mode_properties_builder,
                          G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&mode_properties_builder, "{sv}",
                         "is-current",
                         g_variant_new_boolean (is_current));
  g_variant_builder_add (&mode_properties_builder, "{sv}",
                         "is-preferred",
                         g_variant_new_boolean (mode->preferred));

  g_variant_builder_add (modes_builder, MODE_FORMAT,
                         name,
                         (gint32)mode->width,
                         (gint32)mode->height,
                         (double)mode->refresh / 1000.0,
                         (double)scale,  /* preferred_scale, */
                         &supported_scales_builder,
                         &mode_properties_builder);
}


static gboolean
phosh_monitor_manager_handle_get_current_state (PhoshDBusDisplayConfig *skeleton,
                                                GDBusMethodInvocation  *invocation)
{
  PhoshMonitorManager *self = PHOSH_MONITOR_MANAGER (skeleton);
  GVariantBuilder monitors_builder, logical_monitors_builder, properties_builder;
  PhoshMonitor *primary_monitor;
  PhoshHead *primary_head;

  g_debug ("DBus call %s", __func__);

  primary_monitor = phosh_shell_get_primary_monitor (phosh_shell_get_default ());
  if (!primary_monitor) {
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                           G_DBUS_ERROR_ACCESS_DENIED,
                                           "No primary monitor found");
    return TRUE;
  }
  primary_head = phosh_monitor_manager_get_head_from_monitor (self, primary_monitor);
  if (!primary_head) {
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                           G_DBUS_ERROR_ACCESS_DENIED,
                                           "No primary monitor found");
    return TRUE;
  }

  g_variant_builder_init (&monitors_builder,
                          G_VARIANT_TYPE (MONITORS_FORMAT));
  g_variant_builder_init (&logical_monitors_builder,
                          G_VARIANT_TYPE (LOGICAL_MONITORS_FORMAT));

  /* connected physical monitors */
  for (int i = 0; i < self->heads->len; i++) {
    PhoshHead *head = g_ptr_array_index (self->heads, i);
    GVariantBuilder modes_builder, monitor_properties_builder;
    char *display_name;
    gboolean is_builtin;

    g_variant_builder_init (&modes_builder, G_VARIANT_TYPE (MODES_FORMAT));

    /* Ensure we have at least one mode */
    if (head->modes->len == 0)
      build_mode (&modes_builder, head->mode, TRUE);

    for (int k = 0; k < head->modes->len; k++) {
      PhoshHeadMode *mode = g_ptr_array_index (head->modes, k);

      build_mode (&modes_builder, mode, head->mode == mode);
    }

    g_variant_builder_init (&monitor_properties_builder,
                            G_VARIANT_TYPE ("a{sv}"));

    is_builtin = phosh_head_is_builtin (head);
    g_variant_builder_add (&monitor_properties_builder, "{sv}",
                           "is-builtin",
                           g_variant_new_boolean (is_builtin));

    display_name = get_display_name (head);
    g_variant_builder_add (&monitor_properties_builder, "{sv}",
                           "display-name",
                           g_variant_new_take_string (display_name));

    g_variant_builder_add (&monitors_builder, MONITOR_FORMAT,
                           head->name,                       /* monitor_spec->connector */
                           head->vendor ?: "",               /* monitor_spec->vendor, */
                           head->product ?: "",              /* monitor_spec->product, */
                           head->serial ?: "",               /* monitor_spec->serial, */
                           &modes_builder,
                           &monitor_properties_builder);
  }

  /* Current logical monitor configuration */
  for (int i = 0; i < self->heads->len; i++) {
    PhoshHead *head = g_ptr_array_index (self->heads, i);
    GVariantBuilder logical_monitor_monitors_builder;
    gboolean is_primary;

    if (!head->enabled)
      continue;

    g_variant_builder_init (&logical_monitor_monitors_builder,
                            G_VARIANT_TYPE (LOGICAL_MONITOR_MONITORS_FORMAT));
    g_variant_builder_add (&logical_monitor_monitors_builder,
                           MONITOR_SPEC_FORMAT,
                           head->name,                       /* monitor_spec->connector, */
                           head->vendor ?: "",               /* monitor_spec->vendor, */
                           head->product ?: "",              /* monitor_spec->product, */
                           head->serial ?: "");              /* monitor_spec->serial, */

    is_primary = (head == primary_head);
    g_variant_builder_add (&logical_monitors_builder,
                           LOGICAL_MONITOR_FORMAT,
                           (gint32)head->x,             /* logical_monitor->rect.x */
                           (gint32)head->y,             /* logical_monitor->rect.y */
                           (double)head->scale,         /* (double) logical_monitor->scale */
                           (guint32)head->transform,    /* logical_monitor->transform */
                           is_primary,             /* logical_monitor->is_primary */
                           &logical_monitor_monitors_builder,
                           NULL);
  }

  g_variant_builder_init (&properties_builder, G_VARIANT_TYPE ("a{sv}"));

  g_variant_builder_add (&properties_builder, "{sv}",
                         "layout-mode",
                         g_variant_new_uint32 (PHOSH_MONITOR_MANAGER_LAYOUT_MODE_LOGICAL));

  g_variant_builder_add (&properties_builder, "{sv}",
                         "supports-changing-layout-mode",
                         g_variant_new_boolean (TRUE));

  phosh_dbus_display_config_complete_get_current_state (
    skeleton,
    invocation,
    self->serial,
    g_variant_builder_end (&monitors_builder),
    g_variant_builder_end (&logical_monitors_builder),
    g_variant_builder_end (&properties_builder));

  return TRUE;
}


#undef LOGICAL_MONITORS_FORMAT
#undef LOGICAL_MONITOR_FORMAT
#undef LOGICAL_MONITOR_MONITORS_FORMAT
#undef MODES_FORMAT
#undef MODE_FORMAT
#undef MONITORS_FORMAT
#undef MONITOR_FORMAT


#define MONITOR_CONFIG_FORMAT "(ssa{sv})"
#define MONITOR_CONFIGS_FORMAT "a" MONITOR_CONFIG_FORMAT

static PhoshHead *
phosh_monitor_manager_find_head (PhoshMonitorManager *self, const char *name)
{
  for (int i = 0; i < self->heads->len; i++) {
    PhoshHead *head = g_ptr_array_index (self->heads, i);
    if (!g_strcmp0 (head->name, name))
      return head;
  }
  return NULL;
}


static PhoshHead *
find_head_from_variant (PhoshMonitorManager *self,
                        GVariant            *monitor_config_variant,
                        char               **mode,
                        GError             **err)
{
  g_autofree char *connector = NULL;
  char *mode_id = NULL;
  g_autoptr (GVariant) properties_variant = NULL;
  GVariantIter iter;
  GVariant *value;
  PhoshHead *head;
  char *key;

  g_return_val_if_fail (*mode == NULL, NULL);

  g_variant_get (monitor_config_variant, "(ss@a{sv})",
                 &connector,
                 &mode_id,
                 &properties_variant);

  head = phosh_monitor_manager_find_head (self, connector);
  if (head == NULL) {
    g_set_error (err, G_IO_ERROR, G_IO_ERROR_FAILED,
                 "Could not find monitor for connector '%s'", connector);
    return NULL;
  }
  g_debug ("%s should have mode id %s", head->name, mode_id);

  g_variant_iter_init (&iter, properties_variant);
  while (g_variant_iter_loop (&iter, "{sv}", &key, &value)) {
    /* We don't bother about any properties here */
    g_debug ("Monitor '%s':  prop '%s'", connector, key);
  }

  *mode = mode_id;
  return head;
}


#define LOGICAL_MONITOR_CONFIG_FORMAT "(iidub" MONITOR_CONFIGS_FORMAT ")"

static PhoshHead *
config_head_config_from_logical_monitor_variant (PhoshMonitorManager *self,
                                                 GVariant            *logical_monitor_config_variant,
                                                 GError             **err)
{
  int x, y;
  unsigned int transform;
  double scale;
  gboolean is_primary;
  PhoshHead *head = NULL;
  g_autoptr (GVariantIter) monitor_configs_iter = NULL;

  g_variant_get (logical_monitor_config_variant, LOGICAL_MONITOR_CONFIG_FORMAT,
                 &x,
                 &y,
                 &scale,
                 &transform,
                 &is_primary,
                 &monitor_configs_iter);

  while (TRUE) {
    PhoshHeadMode *mode;
    g_autofree char *mode_name = NULL;
    g_autoptr (GVariant) monitor_config_variant =
      g_variant_iter_next_value (monitor_configs_iter);

    if (!monitor_config_variant)
      break;

    head = find_head_from_variant (self,
                                   monitor_config_variant,
                                   &mode_name,
                                   err);
    if (head == NULL)
      break;

    g_debug ("Head %s, mode %s in logical monitor config %p",
             head->name,
             mode_name,
             logical_monitor_config_variant);

    mode = phosh_head_find_mode_by_name (head, mode_name);
    if (mode)
      head->pending.mode = mode;
    else
      g_warning ("Mode %s not found on head %s", mode_name, head->name);
    head->pending.scale = scale;
    head->pending.x = x;
    head->pending.y = y;
    head->pending.transform = transform;
    head->pending.enabled = TRUE;
    head->pending.seen = TRUE;
  }

  return is_primary ? head : NULL;
}

#undef LOGICAL_MONITOR_CONFIG_FORMAT
#undef MONITOR_CONFIGS_FORMAT
#undef MONITOR_CONFIG_FORMAT


static void
phosh_monitor_manager_clear_pending (PhoshMonitorManager *self)
{
  for (int i = 0; i < self->heads->len; i++) {
    PhoshHead *head = g_ptr_array_index (self->heads, i);
    phosh_head_clear_pending (head);
  }

  self->pending_primary = NULL;
}


static gboolean
phosh_monitor_manager_handle_apply_monitors_config (PhoshDBusDisplayConfig *skeleton,
                                                    GDBusMethodInvocation  *invocation,
                                                    guint                   serial,
                                                    guint                   method,
                                                    GVariant               *logical_monitor_configs_variant,
                                                    GVariant               *properties_variant)
{
  PhoshMonitorManager *self = PHOSH_MONITOR_MANAGER (skeleton);
  GVariantIter logical_monitor_configs_iter;
  GError *err = NULL;
  PhoshHead *primary_head = NULL;
  GVariant *layout_mode_variant = NULL;
  PhoshMonitorManagerLayoutMode layout_mode = PHOSH_MONITOR_MANAGER_LAYOUT_MODE_LOGICAL;
  int n_monitors = 0;

  g_debug ("DBus call %s, method: %d", __func__, method);

  if (serial != self->serial) {
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                           G_DBUS_ERROR_ACCESS_DENIED,
                                           "The requested configuration is based on stale information");
    return TRUE;
  }

  if (phosh_shell_get_locked (phosh_shell_get_default ())) {
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                           G_DBUS_ERROR_ACCESS_DENIED,
                                           "Can't configure outputs when locked");
    return TRUE;
  }

  if (properties_variant) {
    layout_mode_variant = g_variant_lookup_value (properties_variant,
                                                  "layout-mode",
                                                  G_VARIANT_TYPE ("u"));
    if (layout_mode_variant) {
      g_variant_get (layout_mode_variant, "u", &layout_mode);
      g_debug ("Requested layout mode %d", layout_mode);
    }
  }

  if (layout_mode != PHOSH_MONITOR_MANAGER_LAYOUT_MODE_LOGICAL) {
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                           G_DBUS_ERROR_ACCESS_DENIED,
                                           "Invalid layout mode specified");
    return TRUE;
  }


  /* Make sure we refresh only heads from this config run */
  phosh_monitor_manager_clear_pending (self);

  g_variant_iter_init (&logical_monitor_configs_iter,
                       logical_monitor_configs_variant);

  while (TRUE) {
    PhoshHead *head;

    g_autoptr (GVariant) logical_monitor_config_variant =
      g_variant_iter_next_value (&logical_monitor_configs_iter);

    if (!logical_monitor_config_variant)
      break;

    head = config_head_config_from_logical_monitor_variant (self,
                                                            logical_monitor_config_variant,
                                                            &err);

    if (head == NULL && err) {
      phosh_monitor_manager_clear_pending (self);
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "%s", err->message);
      return TRUE;
    }

    if (head) {
      if (primary_head) {
        phosh_monitor_manager_clear_pending (self);
        g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                               G_DBUS_ERROR_ACCESS_DENIED,
                                               "Only one primary monitor possible");
        return TRUE;
      } else {
        primary_head = head;
      }
    }
    n_monitors++;
  }

  if (n_monitors == 0) {
    phosh_monitor_manager_clear_pending (self);
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                           G_DBUS_ERROR_ACCESS_DENIED,
                                           "Monitor config empty");
    return TRUE;
  }

  if (primary_head == NULL) {
    phosh_monitor_manager_clear_pending (self);
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                           G_DBUS_ERROR_ACCESS_DENIED,
                                           "Invalid primary monitor");
    return TRUE;
  }

  if (method == PHOSH_MONITOR_MANAGER_CONFIG_METHOD_PERSISTENT) {
    PhoshMonitor *primary_monitor;

    primary_monitor = phosh_monitor_manager_find_monitor (self, primary_head->name);
    /* If the primary monitor is in the list of enabled heads we can assign it right away */
    if (primary_monitor) {
      PhoshShell *shell = phosh_shell_get_default ();

      if (primary_monitor != phosh_shell_get_primary_monitor (shell)) {
        g_debug ("New primary monitor is %s", primary_monitor->name);
        phosh_shell_set_primary_monitor (shell, primary_monitor);
      }
    } else {
      /* Delay primary monitor reconfiguration in case the correspdonding head
         is currently disabled */
      self->pending_primary = primary_head;
    }

    for (int i = 0; i < self->heads->len; i++) {
      PhoshHead *head = g_ptr_array_index (self->heads, i);

      /* If head was not seen during this invocation, disable it */
      if (!head->pending.seen)
        head->pending.enabled = FALSE;

      head->pending.seen = FALSE;
    }

    /* Apply new config */
    phosh_monitor_manager_apply_monitor_config (self);
  }

  phosh_dbus_display_config_complete_apply_monitors_config (
    skeleton,
    invocation);

  return TRUE;
}


static gboolean
phosh_monitor_manager_handle_set_output_ctm (PhoshDBusDisplayConfig *skeleton,
                                             GDBusMethodInvocation  *invocation,
                                             guint                   serial,
                                             guint                   output,
                                             GVariant               *ctm)
{
  g_debug ("Unimplemented DBus call %s", __func__);
  g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                         G_DBUS_ERROR_NOT_SUPPORTED,
                                         "Changing the color transformation"
                                         "matrix not supported");
  return TRUE;
}


static void
phosh_monitor_manager_display_config_init (PhoshDBusDisplayConfigIface *iface)
{
  iface->handle_get_resources = phosh_monitor_manager_handle_get_resources;
  iface->handle_get_crtc_gamma = phosh_monitor_manager_handle_get_crtc_gamma;
  iface->handle_set_crtc_gamma = phosh_monitor_manager_handle_set_crtc_gamma;
  iface->handle_get_current_state = phosh_monitor_manager_handle_get_current_state;
  iface->handle_apply_monitors_config = phosh_monitor_manager_handle_apply_monitors_config;
  iface->handle_set_output_ctm = phosh_monitor_manager_handle_set_output_ctm;
}


static void
power_save_mode_changed_cb (PhoshMonitorManager *self,
                            GParamSpec          *pspec,
                            gpointer             user_data)
{
  int mode;
  PhoshMonitorPowerSaveMode ps_mode;

  mode = phosh_dbus_display_config_get_power_save_mode (
    PHOSH_DBUS_DISPLAY_CONFIG (self));
  g_debug ("Power save mode %d requested", mode);

  switch (mode) {
  case 0:
    ps_mode = PHOSH_MONITOR_POWER_SAVE_MODE_ON;
    break;
  default:
    ps_mode = PHOSH_MONITOR_POWER_SAVE_MODE_OFF;
    break;
  }

  phosh_monitor_manager_set_power_save_mode (self, ps_mode);
}


static void
on_name_acquired (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
  g_debug ("Acquired name %s", name);
}


static void
on_name_lost (GDBusConnection *connection,
              const char      *name,
              gpointer         user_data)
{
  g_debug ("Lost or failed to acquire name %s", name);
}


static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  PhoshMonitorManager *self = PHOSH_MONITOR_MANAGER (user_data);

  /* We need to use Mutter's object path here to make gnome-settings happy */
  g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                    connection,
                                    "/org/gnome/Mutter/DisplayConfig",
                                    NULL);
}

/*
 * wl_output wayland protocol
 */

static PhoshMonitor *
find_monitor_by_wl_output (PhoshMonitorManager *self, struct wl_output *output)
{
  for (int i = 0; i < self->monitors->len; i++) {
    PhoshMonitor *monitor = g_ptr_array_index (self->monitors, i);
    if (monitor->wl_output == output)
      return monitor;
  }
  return NULL;
}


static void
phosh_monitor_manager_set_night_light_supported (PhoshMonitorManager *self)
{
  gboolean night_light_supported = FALSE;

  for (guint i = 0; i < self->monitors->len; i++) {
    PhoshMonitor *monitor = g_ptr_array_index (self->monitors, i);

    if (phosh_monitor_has_gamma (monitor)) {
      night_light_supported = TRUE;
      break;
    }
  }

  phosh_dbus_display_config_set_night_light_supported (PHOSH_DBUS_DISPLAY_CONFIG (self),
                                                       night_light_supported);
}


static void
on_monitor_n_gamma_entries_changed (PhoshMonitorManager *self)
{
  g_return_if_fail (PHOSH_IS_MONITOR_MANAGER (self));

  phosh_monitor_manager_set_night_light_supported (self);
}


static void
on_monitor_configured (PhoshMonitorManager *self, PhoshMonitor *monitor)
{
  g_return_if_fail (PHOSH_IS_MONITOR_MANAGER (self));
  g_return_if_fail (PHOSH_IS_MONITOR (monitor));

  g_signal_emit (self, signals[SIGNAL_MONITOR_ADDED], 0, monitor);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_N_MONITORS]);

  g_signal_handlers_disconnect_by_data (monitor, self);

  /* A monitor reconfiguration via DBus might have a pending primary monitor change */
  if (self->pending_primary) {
    PhoshMonitor *primary_monitor;
    PhoshShell *shell = phosh_shell_get_default ();

    primary_monitor = phosh_monitor_manager_find_monitor (self, self->pending_primary->name);
    if (primary_monitor == monitor) {
      g_warning ("New primary monitor %s", primary_monitor->name);
      phosh_shell_set_primary_monitor (shell, primary_monitor);
      self->pending_primary = NULL;
    }
  }

  g_signal_connect_swapped (monitor, "notify::n-gamma-entries",
                            G_CALLBACK (on_monitor_n_gamma_entries_changed),
                            self);

  phosh_monitor_manager_set_night_light_supported (self);

  /* Update night light */
  if (self->night_light_temp > 0 && phosh_monitor_has_gamma (monitor))
    phosh_monitor_set_color_temp (monitor, self->night_light_temp);
}


static void
on_monitor_removed (PhoshMonitorManager *self,
                    PhoshMonitor        *monitor,
                    gpointer            *data)
{
  g_return_if_fail (PHOSH_IS_MONITOR (monitor));
  g_return_if_fail (PHOSH_IS_MONITOR_MANAGER (self));

  g_debug ("Monitor %p (%s) removed", monitor, monitor->name);
  g_ptr_array_remove (self->monitors, monitor);
  phosh_monitor_manager_set_night_light_supported (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_N_MONITORS]);
}


static void
phosh_monitor_manager_add_monitor (PhoshMonitorManager *self, PhoshMonitor *monitor)
{
  g_ptr_array_add (self->monitors, monitor);
  /* Delay emission of 'monitor-added' until it's configured */
  g_signal_connect_swapped (monitor,
                            "configured",
                            G_CALLBACK (on_monitor_configured),
                            self);
}


static void
on_wl_outputs_changed (PhoshMonitorManager *self, GParamSpec *pspec, PhoshWayland *wl)
{
  GHashTable *wl_outputs = phosh_wayland_get_wl_outputs (wl);
  GHashTableIter iter;
  struct wl_output *wl_output;
  PhoshMonitor *monitor;

  /* Check for gone outputs */
  for (int i = 0; i < self->monitors->len; i++) {
    monitor = g_ptr_array_index (self->monitors, i);
    if (!phosh_wayland_has_wl_output (wl, monitor->wl_output)) {
      g_debug ("Monitor %p (%s) gone", monitor, monitor->name);
      /* Give up on wayland resources early otherwise we might not be able to
         bind them if the same output shows up again */
      g_object_run_dispose (G_OBJECT (monitor));
      g_signal_emit (self, signals[SIGNAL_MONITOR_REMOVED], 0, monitor);
      /* The monitor is removed from monitors in the class'es default
       * signal handler */
      return;
    }
  }

  /* Check for new outputs */
  g_hash_table_iter_init (&iter, wl_outputs);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer)&wl_output)) {
    if (!find_monitor_by_wl_output (self, wl_output)) {
      monitor = phosh_monitor_new_from_wl_output (wl_output);
      phosh_monitor_manager_add_monitor (self, monitor);
      g_debug ("Monitor %p added", monitor);
      return;
    }
  }
}


/*
 * wlr_output_manager wayland protocol
 */

static void
on_head_finished (PhoshMonitorManager *self,
                  PhoshHead           *head)
{
  g_return_if_fail (PHOSH_IS_MONITOR_MANAGER (self));

  if (g_ptr_array_remove (self->heads, head))
    g_debug ("Removing head %p", head);
  else
    g_warning ("Tried to remove inexistend head %p", head);

  phosh_dbus_display_config_emit_monitors_changed (PHOSH_DBUS_DISPLAY_CONFIG (self));
}


static void
zwlr_output_manager_v1_handle_head (void                          *data,
                                    struct zwlr_output_manager_v1 *manager,
                                    struct zwlr_output_head_v1    *wlr_head)
{
  PhoshMonitorManager *self = PHOSH_MONITOR_MANAGER (data);
  PhoshHead *head;

  g_return_if_fail (PHOSH_IS_MONITOR_MANAGER (self));

  head = phosh_head_new_from_wlr_head (wlr_head);
  g_debug ("New head %p", head);
  g_ptr_array_add (self->heads, head);
  g_signal_connect_swapped (head, "head-finished", G_CALLBACK (on_head_finished), self);

  phosh_dbus_display_config_emit_monitors_changed (PHOSH_DBUS_DISPLAY_CONFIG (self));
}


static void
zwlr_output_manager_v1_handle_done (void                          *data,
                                    struct zwlr_output_manager_v1 *manager,
                                    uint32_t                       serial)
{
  PhoshMonitorManager *self = PHOSH_MONITOR_MANAGER (data);

  g_return_if_fail (PHOSH_IS_MONITOR_MANAGER (self));
  g_debug ("Got zwlr_output_manager serial %u", serial);
  self->zwlr_output_serial = serial;
  self->serial++;

  phosh_dbus_display_config_emit_monitors_changed (PHOSH_DBUS_DISPLAY_CONFIG (self));
}


static const struct zwlr_output_manager_v1_listener zwlr_output_manager_v1_listener = {
  .head = zwlr_output_manager_v1_handle_head,
  .done = zwlr_output_manager_v1_handle_done,
};


static void
zwlr_output_configuration_v1_handle_succeeded (void                                *data,
                                               struct zwlr_output_configuration_v1 *config)
{
  g_debug ("New output configuration %p applied", config);
  zwlr_output_configuration_v1_destroy (config);
}


static void
zwlr_output_configuration_v1_handle_failed (void                                *data,
                                            struct zwlr_output_configuration_v1 *config)
{
  /* TODO: bubble up error */
  g_warning ("Failed to apply New output %p configuration", config);
  zwlr_output_configuration_v1_destroy (config);
}


static void
zwlr_output_configuration_v1_handle_cancelled (void                                *data,
                                               struct zwlr_output_configuration_v1 *config)
{
  zwlr_output_configuration_v1_destroy (config);
  g_warning ("Failed to apply New output configuration %p due to changes", config);
}


static const struct zwlr_output_configuration_v1_listener config_listener = {
  .succeeded = zwlr_output_configuration_v1_handle_succeeded,
  .failed = zwlr_output_configuration_v1_handle_failed,
  .cancelled = zwlr_output_configuration_v1_handle_cancelled,
};


static void
phosh_monitor_manager_dispose (GObject *object)
{
  PhoshMonitorManager *self = PHOSH_MONITOR_MANAGER (object);

  g_clear_handle_id (&self->dbus_name_id, g_bus_unown_name);

  if (g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (self)))
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));

  g_clear_object (&self->sensor_proxy_manager);
  g_clear_pointer (&self->sensor_proxy_binding, g_binding_unbind);

  g_clear_object (&self->gsd_color_proxy);
  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);

  G_OBJECT_CLASS (phosh_monitor_manager_parent_class)->dispose (object);
}


static void
phosh_monitor_manager_finalize (GObject *object)
{
  PhoshMonitorManager *self = PHOSH_MONITOR_MANAGER (object);

  g_ptr_array_free (self->monitors, TRUE);
  g_ptr_array_free (self->heads, TRUE);

  G_OBJECT_CLASS (phosh_monitor_manager_parent_class)->finalize (object);
}

/*
 * PhoshMonitorManager Class
 */

static void
phosh_monitor_manager_set_property (GObject      *object,
                                    guint         property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  PhoshMonitorManager *self = PHOSH_MONITOR_MANAGER (object);

  switch (property_id) {
  case PROP_SENSOR_PROXY_MANAGER:
    phosh_monitor_manager_set_sensor_proxy_manager (self, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_monitor_manager_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  PhoshMonitorManager *self = PHOSH_MONITOR_MANAGER (object);

  switch (property_id) {
  case PROP_SENSOR_PROXY_MANAGER:
    g_value_set_object (value, self->sensor_proxy_manager);
    break;
  case PROP_N_MONITORS:
    g_value_set_int (value, self->monitors->len);
    break;
  case PROP_NIGHT_LIGHT_TEMP:
    g_value_set_uint (value, self->night_light_temp);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
on_gsd_color_temperature_changed (PhoshMonitorManager*self)
{
  guint32 temp;

  temp = phosh_dbus_color_get_temperature (self->gsd_color_proxy);

  if (temp == self->night_light_temp)
    return;

  self->night_light_temp = temp;

  g_return_if_fail (self->night_light_temp > 0);
  g_debug ("Setting night light: %dK", self->night_light_temp);
  for (int i = 0; i < self->monitors->len; i++) {
    gboolean success;
    PhoshMonitor *monitor = g_ptr_array_index (self->monitors, i);

    success = phosh_monitor_set_color_temp (monitor, self->night_light_temp);
    if (!success)
      g_warning ("Failed to set gamma for %s", monitor->name);
  }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_NIGHT_LIGHT_TEMP]);
}


static void
on_gsd_color_proxy_new_for_bus_finish (GObject      *source_object,
                                       GAsyncResult *res,
                                       gpointer      user_data)

{
  PhoshMonitorManager *self = PHOSH_MONITOR_MANAGER (user_data);
  g_autoptr (GError) err = NULL;
  PhoshDBusColor *proxy;

  proxy = phosh_dbus_color_proxy_new_for_bus_finish (res, &err);

  if (!proxy) {
    phosh_async_error_warn (err, "Failed to get gsd color proxy");
    return;
  }

  self->gsd_color_proxy = PHOSH_DBUS_COLOR (proxy);
  g_signal_connect_object (self->gsd_color_proxy,
                           "notify::temperature",
                           G_CALLBACK (on_gsd_color_temperature_changed),
                           self,
                           G_CONNECT_SWAPPED);
  on_gsd_color_temperature_changed (self);

  g_debug ("GSD Color initialized");
}


static gboolean
on_idle (PhoshMonitorManager *self)
{
  self->dbus_name_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                       "org.gnome.Mutter.DisplayConfig",
                                       G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                       G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                       on_bus_acquired,
                                       on_name_acquired,
                                       on_name_lost,
                                       self,
                                       NULL);

  phosh_dbus_color_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                                      G_DBUS_PROXY_FLAGS_NONE,
                                      GSD_COLOR_BUS_NAME,
                                      GSD_COLOR_OBJECT_PATH,
                                      self->cancel,
                                      on_gsd_color_proxy_new_for_bus_finish,
                                      self);

  return FALSE;
}


static void
phosh_monitor_manager_constructed (GObject *object)
{
  PhoshMonitorManager *self = PHOSH_MONITOR_MANAGER (object);
  PhoshWayland *wl = phosh_wayland_get_default ();
  GHashTableIter iter;
  struct wl_output *wl_output;
  struct zwlr_output_manager_v1 *zwlr_output_manager_v1;
  guint id;

  G_OBJECT_CLASS (phosh_monitor_manager_parent_class)->constructed (object);

  g_signal_connect (self, "notify::power-save-mode",
                    G_CALLBACK (power_save_mode_changed_cb), NULL);

  g_signal_connect_swapped (phosh_wayland_get_default (),
                            "notify::wl-outputs",
                            G_CALLBACK (on_wl_outputs_changed),
                            self);
  /* Get initial output list */
  g_hash_table_iter_init (&iter, phosh_wayland_get_wl_outputs (wl));
  while (g_hash_table_iter_next (&iter, NULL, (gpointer)&wl_output)) {
    PhoshMonitor *monitor = phosh_monitor_new_from_wl_output (wl_output);
    phosh_monitor_manager_add_monitor (self, monitor);
  }

  zwlr_output_manager_v1 = phosh_wayland_get_zwlr_output_manager_v1 (wl);
  zwlr_output_manager_v1_add_listener (zwlr_output_manager_v1,
                                       &zwlr_output_manager_v1_listener,
                                       self);

  phosh_dbus_display_config_set_apply_monitors_config_allowed (
    PHOSH_DBUS_DISPLAY_CONFIG (self), TRUE);

  id = g_idle_add ((GSourceFunc) on_idle, self);
  g_source_set_name_by_id (id, "[PhoshMonitorManager] idle");
}


static void
phosh_monitor_manager_class_init (PhoshMonitorManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = phosh_monitor_manager_constructed;
  object_class->dispose = phosh_monitor_manager_dispose;
  object_class->finalize = phosh_monitor_manager_finalize;
  object_class->get_property = phosh_monitor_manager_get_property;
  object_class->set_property = phosh_monitor_manager_set_property;

  props[PROP_SENSOR_PROXY_MANAGER] =
    g_param_spec_object ("sensor-proxy-manager", "", "",
                         PHOSH_TYPE_SENSOR_PROXY_MANAGER,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshMonitorManager:n-monitors:
   *
   * The number of currently enabled monitors
   */
  props[PROP_N_MONITORS] =
    g_param_spec_int ("n-monitors", "", "",
                      0, G_MAXINT, 0,
                      G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshMonitorManager:night-light-temp:
   *
   * Night light color temperature
   */
  props[PROP_NIGHT_LIGHT_TEMP] =
    g_param_spec_uint ("night-light-temp", "", "",
                       0, G_MAXUINT, 0,
                       G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  /**
   * PhoshMonitorManager::monitor-added:
   * @manager: The #PhoshMonitorManager emitting the signal.
   * @monitor: The #PhoshMonitor being added.
   *
   * Emitted whenever a monitor was added.
   */
  signals[SIGNAL_MONITOR_ADDED] = g_signal_new (
    "monitor-added",
    G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
    NULL, G_TYPE_NONE, 1, PHOSH_TYPE_MONITOR);

  /**
   * PhoshMonitorManager::monitor-removed:
   * @manager: The #PhoshMonitorManager emitting the signal.
   * @monitor: The #PhoshMonitor being removed.
   *
   * Emitted whenever a monitor is about to be removed.
   */
  signals[SIGNAL_MONITOR_REMOVED] = g_signal_new_class_handler (
    "monitor-removed",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_CLEANUP,
    G_CALLBACK (on_monitor_removed),
    NULL, NULL,  NULL, G_TYPE_NONE, 1, PHOSH_TYPE_MONITOR);
}

static void
phosh_monitor_manager_init (PhoshMonitorManager *self)
{
  self->monitors = g_ptr_array_new_with_free_func ((GDestroyNotify) (g_object_unref));
  self->heads = g_ptr_array_new_with_free_func ((GDestroyNotify) (g_object_unref));
  self->serial = 1;

  self->cancel = g_cancellable_new ();
}


PhoshMonitorManager *
phosh_monitor_manager_new (PhoshSensorProxyManager *proxy)
{
  return g_object_new (PHOSH_TYPE_MONITOR_MANAGER,
                       "sensor-proxy-manager", proxy,
                       NULL);
}

/**
 * phosh_monitor_manager_get_monitor:
 * @self: The monitor manager
 * @num: The number of the monitor to get
 *
 * Get the nth monitor in the list of known monitors
 *
 * Returns:(nullable)(transfer none): The monitor
 */
PhoshMonitor *
phosh_monitor_manager_get_monitor (PhoshMonitorManager *self, guint num)
{
  if (num >= self->monitors->len)
    return NULL;

  return g_ptr_array_index (self->monitors, num);
}

/**
 * phosh_monitor_manager_find_monitor:
 * @self: The monitor manager
 * @name: The name of the monitor to find
 *
 * Find a monitor by its name
 *
 * Returns:(nullable)(transfer none): The monitor if found, otherwise %NULL
 */
PhoshMonitor *
phosh_monitor_manager_find_monitor (PhoshMonitorManager *self, const char *name)
{
  for (int i = 0; i < self->monitors->len; i++) {
    PhoshMonitor *monitor = g_ptr_array_index (self->monitors, i);
    if (!g_strcmp0 (monitor->name, name))
      return monitor;
  }
  return NULL;
}


guint
phosh_monitor_manager_get_num_monitors (PhoshMonitorManager *self)
{
  return self->monitors->len;
}

/**
 * phosh_monitor_manager_set_monitor_transform:
 * @self: A #PhoshMonitor
 * @monitor: The #PhoshMonitor to set the transform on
 * @transform: The #PhoshMonitorTransform to set
 *
 * Sets monitor's transform. This will become active after the next
 * call to #phosh_monitor_manager_apply_monitor_config().
 *
 * If necessary other heads will be moved to avoid gaps and
 * overlapping heads in the layout.
 */
void
phosh_monitor_manager_set_monitor_transform (PhoshMonitorManager  *self,
                                             PhoshMonitor         *monitor,
                                             PhoshMonitorTransform transform)
{
  PhoshHead *head;

  g_return_if_fail (PHOSH_IS_MONITOR_MANAGER (self));
  g_return_if_fail (PHOSH_IS_MONITOR (monitor));
  g_return_if_fail (phosh_monitor_is_configured (monitor));
  head = phosh_monitor_manager_get_head_from_monitor (self, monitor);
  g_return_if_fail (PHOSH_IS_HEAD (head));

  phosh_head_set_pending_transform (head, transform, self->heads);
}

/**
 * phosh_monitor_manager_set_monitor_scale:
 * @self: A #PhoshMonitor
 * @monitor: The #PhoshMonitor to set the scale for
 * @scale: The scale to set
 *
 * Sets monitor's scale. This will become active after the next
 * call to #phosh_monitor_manager_apply_monitor_config().
 */
void
phosh_monitor_manager_set_monitor_scale (PhoshMonitorManager  *self,
                                         PhoshMonitor         *monitor,
                                         double                scale)
{
  PhoshHead *head;

  g_return_if_fail (PHOSH_IS_MONITOR_MANAGER (self));
  g_return_if_fail (PHOSH_IS_MONITOR (monitor));
  g_return_if_fail (phosh_monitor_is_configured (monitor));
  head = phosh_monitor_manager_get_head_from_monitor (self, monitor);
  g_return_if_fail (PHOSH_IS_HEAD (head));

  phosh_head_set_pending_scale (head, scale, self->heads);
}

/**
 * phosh_monitor_manager_apply_monitor_config
 * @self: a #PhoshMonitorManager
 *
 * Applies a full output configuration
 */
void
phosh_monitor_manager_apply_monitor_config (PhoshMonitorManager *self)
{
  PhoshWayland *wl = phosh_wayland_get_default ();
  struct zwlr_output_configuration_v1 *config;
  struct zwlr_output_manager_v1 *output_manager =
    phosh_wayland_get_zwlr_output_manager_v1 (wl);

  g_return_if_fail (PHOSH_IS_MONITOR_MANAGER (self));

  config = zwlr_output_manager_v1_create_configuration (output_manager,
                                                        self->zwlr_output_serial);

  zwlr_output_configuration_v1_add_listener (config, &config_listener, self);
  for (int i = 0; i < self->heads->len; i++) {
    struct zwlr_output_configuration_head_v1 *config_head;
    PhoshHead *head = g_ptr_array_index (self->heads, i);
    struct zwlr_output_head_v1 *wlr_head = phosh_head_get_wlr_head (head);
    struct zwlr_output_mode_v1 *wlr_mode;

    g_debug ("Adding %sabled head %s to configuration",
             head->pending.enabled ? "en" : "dis",
             head->name);

    if (!head->pending.enabled) {
      zwlr_output_configuration_v1_disable_head (config, wlr_head);
      continue;
    }

    config_head = zwlr_output_configuration_v1_enable_head (config, wlr_head);

    /* A disabled head might not have a mode when set */
    if (head->pending.mode) {
      wlr_mode = head->pending.mode->wlr_mode;
    } else {
      wlr_mode = phosh_head_get_preferred_mode (head)->wlr_mode;
    }

    zwlr_output_configuration_head_v1_set_mode (config_head, wlr_mode);
    zwlr_output_configuration_head_v1_set_position (config_head,
                                                    head->pending.x, head->pending.y);
    zwlr_output_configuration_head_v1_set_transform (config_head, head->pending.transform);
    zwlr_output_configuration_head_v1_set_scale (config_head,
                                                 wl_fixed_from_double (head->pending.scale));
  }

  zwlr_output_configuration_v1_apply (config);
}

void
phosh_monitor_manager_set_sensor_proxy_manager (PhoshMonitorManager     *self,
                                                PhoshSensorProxyManager *manager)
{
  g_return_if_fail (PHOSH_IS_MONITOR_MANAGER (self));
  g_return_if_fail (PHOSH_IS_SENSOR_PROXY_MANAGER (manager) || manager == NULL);

  g_clear_object (&self->sensor_proxy_manager);
  g_clear_pointer (&self->sensor_proxy_binding, g_binding_unbind);

  if (manager == NULL)
    return;

  self->sensor_proxy_manager = g_object_ref (manager);
  self->sensor_proxy_binding = g_object_bind_property (manager, "has-accelerometer",
                                                       self, "panel-orientation-managed",
                                                       G_BINDING_SYNC_CREATE);
}

/**
 * phosh_monitor_manager_set_power_save_mode
 * @self: a #PhoshMonitorManager
 * @mode: The power save mode to set
 *
 * Applies a power save mode to all monitors
 */
void
phosh_monitor_manager_set_power_save_mode (PhoshMonitorManager       *self,
                                           PhoshMonitorPowerSaveMode  mode)
{
  g_return_if_fail (PHOSH_IS_MONITOR_MANAGER (self));

  for (int i = 0; i < self->monitors->len; i++) {
    PhoshMonitor *monitor = g_ptr_array_index (self->monitors, i);

    phosh_monitor_set_power_save_mode (monitor, mode);
  }
}

/**
 * phosh_monitor_manager_enable_fallback:
 * @self: a #PhoshMonitorManager
 *
 * When all heads are disabled look for a fallback to enable. This can be useful
 * when e.g. only external display is enabled and that gets unplugged.
 *
 * Returns: %TRUE if a new head was enabled, %FALSE otherwise
 */
gboolean
phosh_monitor_manager_enable_fallback (PhoshMonitorManager *self)
{
  PhoshHead *builtin_head = NULL;

  if (!self->heads->len)
    return FALSE;

  /* Make sure all display changes got processed otherwise we might try to re-enable
     a just gone head */
  phosh_wayland_roundtrip (phosh_wayland_get_default ());

  for (int i = 0; i < self->heads->len; i++) {
    PhoshHead *head = g_ptr_array_index (self->heads, i);

    if (phosh_head_get_enabled (head)) {
      g_warning ("%s still enabled, no fallback needed", head->name);
      return FALSE;
    }

    if (phosh_head_is_builtin (head) && !builtin_head) {
      builtin_head = head;
    }
  }

  if (!builtin_head)
    return FALSE;

  g_debug ("Enabling fallback head %s", builtin_head->name);
  phosh_head_set_pending_enabled (builtin_head, TRUE);
  phosh_monitor_manager_apply_monitor_config (self);

  return TRUE;
}

/**
 * phosh_monitor_manager_get_night_light_supported:
 * @self: The monitor manager
 *
 * Checks if any output supports night light
 *
 * Returns: %TRUE if night light is supported
 */
gboolean
phosh_monitor_manager_get_night_light_supported (PhoshMonitorManager *self)
{
  g_return_val_if_fail (PHOSH_IS_MONITOR_MANAGER (self), FALSE);

  return phosh_dbus_display_config_get_night_light_supported (PHOSH_DBUS_DISPLAY_CONFIG (self));
}

/**
 * phosh_monitor_manager_get_night_light_temp:
 * @self: The monitor manager
 *
 * Get the night light color temperature
 *
 * Returns: The night light color temperature in K
 */
guint32
phosh_monitor_manager_get_night_light_temp (PhoshMonitorManager *self)
{
  g_return_val_if_fail (PHOSH_IS_MONITOR_MANAGER (self), 0);

  return self->night_light_temp;
}
