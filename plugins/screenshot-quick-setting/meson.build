name = 'screenshot-quick-setting'

screenshot_quick_setting_resources = gnome.compile_resources(
  'phosh-plugin-screenshot-quick-setting-resources',
  'screenshot-quick-setting.gresources.xml',
  c_name: 'phosh_plugin_screenshot_quick_setting',
)

screenshot_quick_setting_plugin_sources = files(
  'phosh-plugin-screenshot-quick-setting.c',
  'screenshot-quick-setting.c',
)

phosh_screenshot_quick_setting_plugin = shared_module(
  'phosh-plugin-screenshot-quick-setting',
  screenshot_quick_setting_plugin_sources,
  screenshot_quick_setting_resources,
  c_args: [
    '-DG_LOG_DOMAIN="phosh-plugin-@0@"'.format(name),
    '-DPLUGIN_NAME="@0@"'.format(name),
  ],
  dependencies: plugin_dep,
  include_directories: phosh_inc,
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

install_data(
  files('..' / '..' / 'data' / 'icons' / 'screenshot-portrait-symbolic.svg'),
  install_dir: phosh_plugin_icon_dir,
)

screenshot_quick_setting_schema = 'mobi.phosh.plugins.screenshot-quick-setting.gschema.xml'
compiled = gnome.compile_schemas(depend_files: screenshot_quick_setting_schema)
compile_schemas = find_program('glib-compile-schemas', required: false)
if compile_schemas.found()
  test(
    'Validate @0@ schema file'.format(screenshot_quick_setting_schema),
    compile_schemas,
    args: ['--strict', '--dry-run', meson.current_source_dir()],
  )
endif
install_data(
  screenshot_quick_setting_schema,
  install_dir: 'share/glib-2.0/schemas',
)
