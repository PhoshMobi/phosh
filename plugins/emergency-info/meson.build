name = 'emergency-info'

emergency_info_plugin_deps = [plugin_dep]

emergency_info_plugin_sources = files(
  'emergency-info-common.h',
  'emergency-info-row.c',
  'emergency-info.c',
  'phosh-plugin-emergency-info.c',
)

emergency_info_inc = include_directories('.')

emergency_info_resources = gnome.compile_resources(
  'phosh-plugin-emergency-info-resources',
  'phosh-plugin-emergency-info.gresources.xml',
  c_name: 'phosh_plugin_emergency_info',
)

phosh_emergency_info_plugin = shared_module(
  'phosh-plugin-emergency-info',
  emergency_info_plugin_sources,
  emergency_info_resources,
  c_args: [
    '-DG_LOG_DOMAIN="phosh-plugin-@0@"'.format(name),
    '-DPLUGIN_NAME="@0@"'.format(name),
  ],
  dependencies: emergency_info_plugin_deps,
  install: true,
  install_dir: plugins_dir,
)

pluginconf = configuration_data()
pluginconf.set('name', name)
pluginconf.set('plugins_dir', plugins_dir)
pluginconf.set('plugin_prefs_dir', plugin_prefs_dir)

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

subdir('prefs')
