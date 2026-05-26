name = 'upcoming-events'

upcoming_events_plugin_deps = [gmobile_dep, plugin_dep]

upcoming_events_resources = gnome.compile_resources(
  'phosh-plugin-upcoming-events-resources',
  'phosh-plugin-upcoming-events.gresources.xml',
  c_name: 'phosh_plugin_upcoming_events',
)

upcoming_events_generated_dbus_sources = gnome.gdbus_codegen(
  'phosh-plugin-upcoming-events-phosh-calendar-dbus',
  app_id + '.CalendarServer.xml',
  interface_prefix: app_id,
  namespace: 'PhoshPluginDBus',
)

upcoming_events_plugin_sources = files(
  'calendar-event.c',
  'calendar-event.h',
  'event-list.c',
  'event-list.h',
  'phosh-plugin-upcoming-events.c',
  'upcoming-event.c',
  'upcoming-event.h',
  'upcoming-events.c',
  'upcoming-events.h',
)

phosh_upcoming_events_plugin = shared_module(
  'phosh-plugin-upcoming-events',
  upcoming_events_plugin_sources,
  upcoming_events_resources,
  upcoming_events_generated_dbus_sources,
  c_args: [
    '-DG_LOG_DOMAIN="phosh-plugin-@0@"'.format(name),
    '-DPLUGIN_NAME="@0@"'.format(name),
  ],
  dependencies: upcoming_events_plugin_deps,
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

upcoming_events_schema = 'sm.puri.phosh.plugins.upcoming-events.gschema.xml'
compiled = gnome.compile_schemas(depend_files: upcoming_events_schema)
compile_schemas = find_program('glib-compile-schemas', required: false)
if compile_schemas.found()
  test(
    'Validate @0@ schema file'.format(upcoming_events_schema),
    compile_schemas,
    args: ['--strict', '--dry-run', meson.current_source_dir()],
  )
endif
install_data(upcoming_events_schema, install_dir: 'share/glib-2.0/schemas')

subdir('prefs')
