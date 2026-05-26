name = 'launcher-box'

launcher_box_plugin_deps = [plugin_dep, gio_unix_dep]

launcher_box_resources = gnome.compile_resources(
  'phosh-plugin-launcher-box-resources',
  'phosh-plugin-launcher-box.gresources.xml',
  c_name: 'phosh_plugin_launcher_box',
)

launcher_box_plugin_sources = files(
  'launcher-box.c',
  'launcher-box.h',
  'launcher-item.c',
  'launcher-item.h',
  'launcher-row.c',
  'launcher-row.h',
  'phosh-plugin-launcher-box.c',
)

phosh_launcher_box_plugin = shared_module(
  'phosh-plugin-launcher-box',
  launcher_box_plugin_sources,
  launcher_box_resources,
  c_args: ['-DG_LOG_DOMAIN="phosh-plugin-@0@"'.format(name), '-DPLUGIN_NAME="@0@"'.format(name)],
  dependencies: launcher_box_plugin_deps,
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

launcher_box_schema = 'sm.puri.phosh.plugins.launcher-box.gschema.xml'
compiled = gnome.compile_schemas(depend_files: launcher_box_schema)
compile_schemas = find_program('glib-compile-schemas', required: false)
if compile_schemas.found()
  test(
    'Validate @0@ schema file'.format(launcher_box_schema),
    compile_schemas,
    args: ['--strict', '--dry-run', meson.current_source_dir()],
  )
endif
install_data(launcher_box_schema, install_dir: 'share/glib-2.0/schemas')
