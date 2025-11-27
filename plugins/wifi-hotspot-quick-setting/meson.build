name = 'wifi-hotspot-quick-setting'

qrcodegen_dep = dependency('qrcodegen')

wifi_hotspot_quick_setting_resources = gnome.compile_resources(
  'phosh-plugin-wifi-hotspot-quick-setting-resources',
  'phosh-plugin-wifi-hotspot-quick-setting.gresources.xml',
  c_name: 'phosh_plugin_wifi_hotspot_quick_setting',
)

wifi_hotspot_quick_setting_plugin_sources = files(
  'phosh-plugin-wifi-hotspot-quick-setting.c',
  'wifi-hotspot-quick-setting.c',
  'wifi-hotspot-status-page.c',
)

phosh_wifi_hotspot_quick_setting_plugin = shared_module(
  'phosh-plugin-wifi-hotspot-quick-setting',
  wifi_hotspot_quick_setting_plugin_sources,
  wifi_hotspot_quick_setting_resources,
  c_args: ['-DG_LOG_DOMAIN="phosh-plugin-@0@"'.format(name), '-DPLUGIN_NAME="@0@"'.format(name)],
  dependencies: [plugin_dep, qrcodegen_dep],
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

install_data('icons/wifi-hotspot-quick-setting-symbolic.svg', install_dir: phosh_plugin_icon_dir)
