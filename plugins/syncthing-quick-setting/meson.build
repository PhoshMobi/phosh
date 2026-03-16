name = 'syncthing-quick-setting'

syncbus_interface_files = files(
  'dbus/mobi.phosh.syncbus.Folder.xml',
  'dbus/mobi.phosh.syncbus.Manager.xml',
)
syncthing_quick_setting_generated_sources = gnome.gdbus_codegen(
  'syncbus',
  sources: syncbus_interface_files,
  interface_prefix: 'mobi.phosh.syncbus',
  namespace: 'SyncbusImpl',
  object_manager: true,
)

syncthing_quick_setting_resources = gnome.compile_resources(
  'phosh-plugin-syncthing-quick-setting-resources',
  'phosh-plugin-syncthing-quick-setting.gresources.xml',
  c_name: 'phosh_plugin_syncthing_quick_setting',
)

syncthing_quick_setting_plugin_sources = files(
  'folder-row.c',
  'phosh-plugin-syncthing-quick-setting.c',
  'syncthing-quick-setting.c',
)

phosh_syncthing_quick_setting_plugin = shared_module(
  'phosh-plugin-syncthing-quick-setting',
  syncthing_quick_setting_generated_sources,
  syncthing_quick_setting_plugin_sources,
  syncthing_quick_setting_resources,
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
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: plugins_dir,
  type: 'desktop',
)
