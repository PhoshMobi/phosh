name = 'simple-custom-quick-setting'

simple_custom_quick_setting_resources = gnome.compile_resources(
  'phosh-plugin-simple-custom-quick-setting-resources',
  'phosh-plugin-simple-custom-quick-setting.gresources.xml',
  c_name: 'phosh_plugin_simple_custom_quick_setting',
)

simple_custom_quick_setting_plugin_sources = files(
  'phosh-plugin-simple-custom-quick-setting.c',
  'simple-custom-quick-setting.c',
)

phosh_simple_custom_quick_setting_plugin = shared_module(
  'phosh-plugin-simple-custom-quick-setting',
  simple_custom_quick_setting_plugin_sources,
  simple_custom_quick_setting_resources,
  c_args: [
    '-DG_LOG_DOMAIN="phosh-plugin-@0@"'.format(name),
    '-DPLUGIN_NAME="@0@"'.format(name),
  ],
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
