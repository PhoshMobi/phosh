dbus_inc = include_directories('.')

# DBus client interfaces
generated_dbus_sources = []
generated_dbus_headers = []
libphosh_generated_dbus_headers = []
libphosh_generated_dbus_sources = []

dbus_prefix = 'PhoshDBus'
dbus_inc_dir = lib_inc_dir / 'dbus'

#
# Protocols where Phosh is the DBus client:
#
dbus_client_protos = [
  # Sorted by xml filename:
  [
    'iio-sensor-proxy-dbus',
    'net.hadess.SensorProxy.xml',
    'net.hadess',
    false,
    false,
  ],
  [
    'accounts-user-dbus',
    'org.freedesktop.Accounts.User.xml',
    'org.freedesktop',
    false,
    false,
  ],
  [
    'accounts-dbus',
    'org.freedesktop.Accounts.xml',
    'org.freedesktop',
    false,
    false,
  ],
  [
    'geoclue-manager-dbus',
    'org.freedesktop.GeoClue2.Manager.xml',
    'org.freedesktop',
    false,
    false,
  ],
  [
    'hostname1-dbus',
    'org.freedesktop.hostname1.xml',
    'org.freedesktop',
    false,
    false,
  ],
  [
    'portal-dbus',
    'org.freedesktop.impl.portal.xml',
    'org.freedesktop',
    false,
    false,
  ],
  [
    'login1-manager-dbus',
    'org.freedesktop.login1.Manager.xml',
    'org.freedesktop.login1',
    false,
    false,
  ],
  [
    'login1-session-dbus',
    'org.freedesktop.login1.Session.xml',
    'org.freedesktop.login1',
    false,
    false,
  ],
  ['calls-dbus', 'org.gnome.Calls.Call.xml', 'org.gnome', false, true],
  [
    'calls-emergency-dbus',
    'org.gnome.Calls.EmergencyCalls.xml',
    'org.gnome.Calls',
    false,
    false,
  ],
  [
    'gsd-color-dbus',
    'org.gnome.SettingsDaemon.Color.xml',
    'org.gnome.SettingsDaemon',
    false,
    false,
  ],
  [
    'gsd-rfkill-dbus',
    'org.gnome.SettingsDaemon.Rfkill.xml',
    'org.gnome.SettingsDaemon',
    false,
    false,
  ],
  [
    'gnome-session-dbus',
    'org.gnome.SessionManager.xml',
    'org.gnome',
    false,
    false,
  ],
  [
    'gnome-session-client-private-dbus',
    'org.gnome.SessionManager.ClientPrivate.xml',
    'org.gnome',
    false,
    false,
  ],
  [
    'gnome-session-presence-dbus',
    'org.gnome.SessionManager.Presence.xml',
    'org.gnome',
    false,
    false,
  ],
  [
    'gnome-shell-search-provider',
    'org.gnome.Shell.SearchProvider2.xml',
    'org.gnome.Shell',
    false,
    false,
  ],
  ['mpris-dbus', 'org.mpris.MediaPlayer2.xml', 'org.mpris', false, false],
  ['phosh-wwan-ofono-dbus', 'org.ofono.xml', 'org', false, false],
  ['phosh-osk0-dbus', 'sm.puri.OSK0.xml', 'sm.puri', false, false],
]

#
# Protocols where Phosh is the DBus server:
#
dbus_server_protos = [
  # Sorted by xml filename:
  [
    'phosh-dbus-debug-control',
    'mobi.phosh.Shell.DebugControl.xml',
    'mobi.phosh.Shell',
    false,
    false,
  ],
  [
    'geoclue-agent-dbus',
    'org.freedesktop.GeoClue2.Agent.xml',
    'org.freedesktop',
    false,
    false,
  ],
  [
    'notify-dbus',
    'org.freedesktop.Notifications.xml',
    'org.freedesktop',
    false,
    false,
  ],
  ['phosh-gnome-shell-dbus', 'org.gnome.Shell.xml', 'org', false, false],
  [
    'phosh-display-dbus',
    'org.gnome.Mutter.DisplayConfig.xml',
    'org.gnome.Mutter',
    false,
    false,
  ],
  [
    'phosh-screen-saver-dbus',
    'org.gnome.ScreenSaver.xml',
    'org.gnome',
    false,
    false,
  ],
  [
    'phosh-brightness-dbus',
    'org.gnome.Shell.Brightness.xml',
    'org.gnome.Shell',
    false,
    false,
  ],
  [
    'phosh-screenshot-dbus',
    'org.gnome.Shell.Screenshot.xml',
    'org.gnome.Shell',
    true,
    false,
  ],
  [
    'phosh-end-session-dialog-dbus',
    'org.gnome.SessionManager.EndSessionDialog.xml',
    'org.gnome.SessionManager',
    false,
    false,
  ],
  [
    'phosh-gtk-mountoperation-dbus',
    'org.Gtk.MountOperationHandler.xml',
    'org.Gtk',
    false,
    false,
  ],
  [
    'phosh-searchd',
    'mobi.phosh.Shell.Search.xml',
    'mobi.phosh.Shell',
    false,
    false,
  ],
]

foreach p : dbus_client_protos + dbus_server_protos
  name = p[0]
  xml = p[1]
  prefix = p[2]
  is_libphosh_header = p[3]
  object_manager = p[4]
  targets = gnome.gdbus_codegen(
    name,
    xml,
    object_manager: object_manager,
    interface_prefix: prefix,
    install_header: bindings_lib and is_libphosh_header,
    install_dir: dbus_inc_dir,
    namespace: dbus_prefix,
  )
  generated_dbus_sources += targets[0]
  generated_dbus_headers += targets[1]
  if is_libphosh_header
    libphosh_generated_dbus_sources += targets[0]
    libphosh_generated_dbus_headers += targets[1]
  endif
endforeach

# TODO: Use `dbus_prefix` as namespace
targets = gnome.gdbus_codegen(
  'phosh-idle-dbus',
  'org.gnome.Mutter.IdleMonitor.xml',
  interface_prefix: 'org.gnome.Mutter',
  object_manager: true,
  namespace: 'PhoshIdleDBus',
)
generated_dbus_sources += targets[0]
generated_dbus_headers += targets[1]
