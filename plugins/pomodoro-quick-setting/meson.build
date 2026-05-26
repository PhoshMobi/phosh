name = 'pomodoro-quick-setting'

pomodoro_enum_sources = gnome.mkenums_simple(
  'pomodoro-enums',
  sources: 'pomodoro-quick-setting.h',
)

pomodoro_quick_setting_resources = gnome.compile_resources(
  'phosh-plugin-pomodoro-quick-setting-resources',
  'phosh-plugin-pomodoro-quick-setting.gresources.xml',
  c_name: 'phosh_plugin_pomodoro_quick_setting',
)

pomodoro_quick_setting_plugin_sources = files(
  'phosh-plugin-pomodoro-quick-setting.c',
  'pomodoro-quick-setting.c',
)

phosh_pomodoro_quick_setting_plugin = shared_module(
  'phosh-plugin-pomodoro-quick-setting',
  pomodoro_quick_setting_plugin_sources,
  pomodoro_quick_setting_resources,
  c_args: [
    '-DG_LOG_DOMAIN="phosh-plugin-@0@"'.format(name),
    '-DPLUGIN_NAME="@0@"'.format(name),
  ],
  sources: pomodoro_enum_sources,
  dependencies: [plugin_dep, libcall_ui_dep],
  include_directories: phosh_inc,
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

install_data(
  'icons/pomodoro-active-symbolic.svg',
  rename: 'pomodoro-quick-setting-symbolic.svg',
  install_dir: phosh_plugin_icon_dir,
)


pomodoro_quick_setting_schema = 'mobi.phosh.plugins.pomodoro.gschema.xml'
compiled = gnome.compile_schemas(depend_files: pomodoro_quick_setting_schema)
compile_schemas = find_program('glib-compile-schemas', required: false)
if compile_schemas.found()
  test(
    'Validate @0@ schema file'.format(pomodoro_quick_setting_schema),
    compile_schemas,
    args: ['--strict', '--dry-run', meson.current_source_dir()],
  )
endif
install_data(
  pomodoro_quick_setting_schema,
  install_dir: 'share/glib-2.0/schemas',
)

subdir('prefs')
