name = 'ticket-box'

ticket_box_plugin_deps = [
  plugin_dep,
  gio_unix_dep,
  dependency('evince-document-3.0'),
  dependency('evince-view-3.0'),
]

ticket_box_resources = gnome.compile_resources(
  'phosh-plugin-ticket-box-resources',
  'phosh-plugin-ticket-box.gresources.xml',
  c_name: 'phosh_plugin_ticket_box',
)

ticket_box_plugin_sources = files(
  'phosh-plugin-ticket-box.c',
  'ticket-box.c',
  'ticket-box.h',
  'ticket-row.c',
  'ticket-row.h',
  'ticket.c',
  'ticket.h',
)

phosh_ticket_box_plugin = shared_module(
  'phosh-plugin-ticket-box',
  ticket_box_plugin_sources,
  ticket_box_resources,
  c_args: [
    '-DG_LOG_DOMAIN="phosh-plugin-@0@"'.format(name),
    '-DPLUGIN_NAME="@0@"'.format(name),
  ],
  dependencies: ticket_box_plugin_deps,
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

ticket_box_schema = 'sm.puri.phosh.plugins.ticket-box.gschema.xml'
compiled = gnome.compile_schemas(depend_files: ticket_box_schema)
compile_schemas = find_program('glib-compile-schemas', required: false)
if compile_schemas.found()
  test(
    'Validate @0@ schema file'.format(ticket_box_schema),
    compile_schemas,
    args: ['--strict', '--dry-run', meson.current_source_dir()],
  )
endif
install_data(ticket_box_schema, install_dir: 'share/glib-2.0/schemas')

subdir('prefs')
