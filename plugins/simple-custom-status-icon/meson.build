name = 'simple-custom-status-icon'

simple_custom_status_icon_resources = gnome.compile_resources(
  'phosh-plugin-simple-custom-status-icon-resources',
  'phosh-plugin-simple-custom-status-icon.gresources.xml',
  c_name: 'phosh_plugin_simple_custom_status_icon',
)

simple_custom_status_icon_plugin_sources = files(
  'phosh-plugin-simple-custom-status-icon.c',
  'simple-custom-status-icon.c',
)

phosh_simple_custom_status_icon_plugin = shared_module(
  'phosh-plugin-simple-custom-status-icon',
  simple_custom_status_icon_plugin_sources,
  simple_custom_status_icon_resources,
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
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: plugins_dir,
  type: 'desktop',
)
