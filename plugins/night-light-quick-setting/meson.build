name = 'night-light-quick-setting'

night_light_quick_setting_resources = gnome.compile_resources(
  'phosh-plugin-night-light-quick-setting-resources',
  'phosh-plugin-night-light-quick-setting.gresources.xml',
  c_name: 'phosh_plugin_night_light_quick_setting',
)

night_light_quick_setting_plugin_sources = files(
  'night-light-quick-setting.c',
  'phosh-plugin-night-light-quick-setting.c',
)

phosh_night_light_quick_setting_plugin = shared_module(
  'phosh-plugin-night-light-quick-setting',
  night_light_quick_setting_plugin_sources,
  night_light_quick_setting_resources,
  c_args: ['-DG_LOG_DOMAIN="phosh-plugin-@0@"'.format(name), '-DPLUGIN_NAME="@0@"'.format(name)],
  dependencies: plugin_dep,
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

install_data('icons/night-light-quick-setting-symbolic.svg', install_dir: phosh_plugin_icon_dir)
