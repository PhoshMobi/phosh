name = 'dark-mode-quick-setting'

dark_mode_quick_setting_deps = [
  dependency('gsettings-desktop-schemas', version: '>=42'),
]

dark_mode_quick_setting_resources = gnome.compile_resources(
  'phosh-plugin-dark-mode-quick-setting-resources',
  'phosh-plugin-dark-mode-quick-setting.gresources.xml',
  c_name: 'phosh_plugin_dark_mode_quick_setting',
)

dark_mode_quick_setting_plugin_sources = files(
  'dark-mode-quick-setting.c',
  'phosh-plugin-dark-mode-quick-setting.c',
)

phosh_dark_mode_quick_setting_plugin = shared_module(
  'phosh-plugin-dark-mode-quick-setting',
  dark_mode_quick_setting_plugin_sources,
  dark_mode_quick_setting_resources,
  c_args: [
    '-DG_LOG_DOMAIN="phosh-plugin-@0@"'.format(name),
    '-DPLUGIN_NAME="@0@"'.format(name),
  ],
  dependencies: [plugin_dep, dark_mode_quick_setting_deps],
  install: true,
  install_dir: plugins_dir,
)

pluginconf = configuration_data()
pluginconf.set('name', name)
pluginconf.set('plugins_dir', plugins_dir)

i18n.merge_file(
  input: configure_file(
    input: name + '.desktop.in.in',
    output: name + '.desktop.in',
    configuration: pluginconf,
  ),
  output: name + '.plugin',
  po_dir: meson.project_source_root() / 'po',
  install: true,
  install_dir: plugins_dir,
  type: 'desktop',
)

install_data(
  'icons/dark-mode-symbolic.svg',
  rename: 'dark-mode-quick-setting-symbolic.svg',
  install_dir: phosh_plugin_icon_dir,
)
