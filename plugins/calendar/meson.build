name = 'calendar'

calendar_resources = gnome.compile_resources(
  'phosh-plugin-calendar-resources',
  'phosh-plugin-calendar.gresources.xml',
  c_name: 'phosh_plugin_calendar',
)

calendar_plugin_sources = files('calendar.c', 'phosh-plugin-calendar.c')

phosh_calendar_plugin = shared_module(
  'phosh-plugin-calendar',
  calendar_plugin_sources,
  calendar_resources,
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
