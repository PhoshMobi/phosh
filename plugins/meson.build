phosh_plugin_header_subdir = 'phosh'

adw_ver = '1.5'
gtk4_ver = '4.12'

adw_ver_str = 'ADW_VERSION_@0@'.format(adw_ver.replace('.', '_'))
gtk4_ver_str = 'ADW_VERSION_@0@'.format(gtk4_ver.replace('.', '_'))
adw_ver_cmp = '>=@0@'.format(adw_ver)
gtk4_ver_cmp = '>=@0@'.format(gtk4_ver)

# Public header that can also be used by out of tree plugins
phosh_plugin_config = configuration_data()
phosh_plugin_config.set_quoted(
  'PHOSH_PLUGIN_EXTENSION_POINT_LOCKSCREEN_WIDGET',
  'phosh-lockscreen-widget',
)
phosh_plugin_config.set_quoted(
  'PHOSH_PLUGIN_EXTENSION_POINT_LOCKSCREEN_WIDGET_PREFS',
  'phosh-lockscreen-widget-prefs',
)
phosh_plugin_config.set_quoted(
  'PHOSH_PLUGIN_EXTENSION_POINT_QUICK_SETTING_WIDGET',
  'phosh-quick-setting-widget',
)
phosh_plugin_config.set_quoted(
  'PHOSH_PLUGIN_EXTENSION_POINT_QUICK_SETTING_WIDGET_PREFS',
  'phosh-quick-setting-widget-prefs',
)
phosh_plugin_config.set_quoted(
  'PHOSH_PLUGIN_EXTENSION_POINT_STATUS_ICON_WIDGET',
  'phosh-status-icon-widget',
)
phosh_plugin_config.set_quoted(
  'PHOSH_PLUGIN_EXTENSION_POINT_STATUS_ICON_WIDGET_PREFS',
  'phosh-status-icon-widget-prefs',
)

phosh_plugin_h = configure_file(
  input: 'phosh-plugin.h.in',
  output: 'phosh-plugin.h',
  configuration: phosh_plugin_config,
)
install_headers([phosh_plugin_h], subdir: phosh_plugin_header_subdir)

pkgconfig.generate(
  variables: [
    'lockscreen_plugins_dir=@0@'.format(plugins_dir),
    'lockscreen_prefs_dir=@0@'.format(plugin_prefs_dir),
    'quick_setting_plugins_dir=@0@'.format(plugins_dir),
    'quick_setting_prefs_dir=@0@'.format(plugin_prefs_dir),
    'status_icons_plugins_dir=@0@'.format(plugins_dir),
  ],
  subdirs: phosh_plugin_header_subdir,
  version: meson.project_version(),
  install_dir: libdir / 'pkgconfig',
  filebase: 'phosh-plugins',
  name: 'phosh-plugins',
  description: 'Phosh plugin configuration data',
)

lockscreen_plugins = [
  'calendar',
  'emergency-info',
  'launcher-box',
  'media-players',
  'ticket-box',
  'upcoming-events',
]

quick_setting_plugins = [
  'caffeine-quick-setting',
  'dark-mode-quick-setting',
  'location-quick-setting',
  'mobile-data-quick-setting',
  'night-light-quick-setting',
  'pomodoro-quick-setting',
  'scaling-quick-setting',
  'simple-custom-quick-setting',
  'wifi-hotspot-quick-setting',
]

status_icon_plugins = ['simple-custom-status-icon']

plugin_schema_dirs = []
foreach plugin : lockscreen_plugins + quick_setting_plugins
  plugin_schema_dirs += meson.project_build_root() / 'plugins' / plugin
endforeach
plugin_schema_path = ':'.join(plugin_schema_dirs)

if get_option('lockscreen-plugins') or get_option('quick-setting-plugins')
  plugin_inc = include_directories('.')
  plugin_prefs_inc = include_directories('.')

  # Like phosh-config.h but for preference plugins
  plugin_prefs_config = configuration_data()
  plugin_prefs_config.set_quoted('GETTEXT_PACKAGE', 'phosh')
  plugin_prefs_config.set_quoted('LOCALEDIR', localedir)
  plugin_prefs_config_h = configure_file(
    output: 'phosh-plugin-prefs-config.h',
    configuration: plugin_prefs_config,
  )

  plugin_dep = declare_dependency(
    sources: phosh_plugin_h,
    include_directories: plugin_inc,
    dependencies: [
      gio_unix_dep,
      gobject_dep,
      gtk_dep,
      libhandy_dep,
      libnm_dep,
      phosh_plugins_dep,
      gsettings_desktop_schemas_dep,
    ],
  )

  plugin_prefs_dep = declare_dependency(
    sources: [phosh_plugin_h, plugin_prefs_config_h],
    include_directories: plugin_prefs_inc,
    dependencies: [
      dependency('libadwaita-1', version: adw_ver_cmp),
      dependency('gtk4', version: gtk4_ver_cmp),
    ],
  )

  if get_option('lockscreen-plugins')
    foreach plugin : lockscreen_plugins
      subdir(plugin)
    endforeach
  endif

  if get_option('quick-setting-plugins')
    foreach plugin : quick_setting_plugins
      subdir(plugin)
    endforeach
  endif

  if get_option('status-icon-plugins')
    foreach plugin : status_icon_plugins
      subdir(plugin)
    endforeach
  endif

endif
