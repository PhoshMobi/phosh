name = 'media-players'

media_players_plugin_deps = [plugin_dep, gio_unix_dep]

media_players_resources = gnome.compile_resources(
  'phosh-plugin-media-players-resources',
  'phosh-plugin-media-players.gresources.xml',
  c_name: 'phosh_plugin_media_players',
)

media_players_plugin_sources = files(
  'media-players.c',
  'media-players.h',
  'phosh-plugin-media-players.c',
)

phosh_media_players_plugin = shared_module(
  'phosh-plugin-media-players',
  media_players_plugin_sources,
  media_players_resources,
  c_args: [
    '-DG_LOG_DOMAIN="phosh-plugin-@0@"'.format(name),
    '-DPLUGIN_NAME="@0@"'.format(name),
  ],
  dependencies: media_players_plugin_deps,
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
