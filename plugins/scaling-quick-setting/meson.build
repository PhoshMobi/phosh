name = 'scaling-quick-setting'

scaling_quick_setting_resources = gnome.compile_resources(
  'phosh-plugin-scaling-quick-setting-resources',
  'phosh-plugin-scaling-quick-setting.gresources.xml',
  c_name: 'phosh_plugin_scaling_quick_setting',
)

scaling_quick_setting_plugin_sources = files(
  'phosh-plugin-scaling-quick-setting.c',
  'scale-row.c',
  'scaling-quick-setting.c',
)

phosh_scaling_quick_setting_plugin = shared_module(
  'phosh-plugin-scaling-quick-setting',
  scaling_quick_setting_plugin_sources,
  scaling_quick_setting_resources,
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

install_data(
  'icons/screen-scaling-small-symbolic.svg',
  rename: 'scaling-quick-setting-symbolic.svg',
  install_dir: phosh_plugin_icon_dir,
)
