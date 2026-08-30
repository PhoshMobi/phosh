caffeine_quick_setting_prefs_resources = gnome.compile_resources(
  'phosh-plugin-prefs-caffeine-quick-setting-resources',
  'phosh-plugin-prefs-caffeine-quick-setting.gresources.xml',
  c_name: 'phosh_plugin_prefs_caffeine_quick_setting',
)

caffeine_quick_setting_plugin_prefs_sources = files(
  'caffeine-quick-setting-prefs.c',
  'caffeine-quick-setting-prefs.h',
  'phosh-plugin-prefs-caffeine-quick-setting.c',
)

phosh_caffeine_quick_setting_plugin_prefs = shared_module(
  'phosh-plugin-prefs-caffeine-quick-setting',
  caffeine_quick_setting_plugin_prefs_sources,
  caffeine_quick_setting_prefs_resources,
  c_args: [
    '-DG_LOG_DOMAIN="phosh-plugin-prefs-caffeine-quick-setting"',
    '-DPLUGIN_PREFS_NAME="@0@-prefs"'.format(name),
    '-DADW_VERSION_MIN_REQUIRED=@0@'.format(adw_ver_str),
  ],
  sources: caffeine_enum_sources,
  include_directories: phosh_plugin_caffeine_inc,
  dependencies: [plugin_prefs_dep, libcall_ui_dep],
  install: true,
  install_dir: plugin_prefs_dir,
)
