name = 'camera-status-icon'

camera_status_icon_resources = gnome.compile_resources(
  'phosh-plugin-camera-status-icon-resources',
  'phosh-plugin-camera-status-icon.gresources.xml',
  c_name: 'phosh_plugin_camera_status_icon',
)

camera_status_icon_plugin_sources = files(
  'phosh-plugin-camera-status-icon.c',
  'camera-status-icon.c',
)

phosh_camera_status_icon_plugin = shared_module(
  'phosh-plugin-camera-status-icon',
  camera_status_icon_plugin_sources,
  camera_status_icon_resources,
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
