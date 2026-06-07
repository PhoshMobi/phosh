subdir('icons')

name = 'load-meter-status-icon'

load_meter_status_icon_resources = gnome.compile_resources(
  'phosh-plugin-load-meter-status-icon-resources',
  'phosh-plugin-load-meter-status-icon.gresources.xml',
  c_name: 'phosh_plugin_load_meter_status_icon',
)

load_meter_status_icon_plugin_sources = files(
  'load-meter-status-icon.c',
  'phosh-plugin-load-meter-status-icon.c',
)

phosh_load_meter_status_icon_plugin = shared_module(
  'phosh-plugin-load-meter-status-icon',
  load_meter_status_icon_plugin_sources,
  load_meter_status_icon_resources,
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
