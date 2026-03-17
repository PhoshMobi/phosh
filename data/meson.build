gsd_required_components = [
  'org.gnome.SettingsDaemon.A11ySettings',
  'org.gnome.SettingsDaemon.Color',
  'org.gnome.SettingsDaemon.Datetime',
  'org.gnome.SettingsDaemon.Housekeeping',
  'org.gnome.SettingsDaemon.Keyboard',
  'org.gnome.SettingsDaemon.MediaKeys',
  'org.gnome.SettingsDaemon.Power',
  'org.gnome.SettingsDaemon.PrintNotifications',
  'org.gnome.SettingsDaemon.Rfkill',
  'org.gnome.SettingsDaemon.ScreensaverProxy',
  'org.gnome.SettingsDaemon.Sharing',
  'org.gnome.SettingsDaemon.Smartcard',
  'org.gnome.SettingsDaemon.Sound',
  'org.gnome.SettingsDaemon.UsbProtection',
  'org.gnome.SettingsDaemon.Wwan',
]

desktop_required_components = gsd_required_components + [
  'mobi.phosh.Shell',
  'sm.puri.OSK0',
]

desktopconf = configuration_data()
desktopconf.set('bindir', bindir)
desktopconf.set('libexecdir', libexecdir)
desktopconf.set(
  'required_components',
  ';'.join(desktop_required_components) + ';',
)

desktop_utils = find_program('desktop-file-validate', required: false)
desktop_files = {'mobi.phosh.Shell.desktop': true}
foreach desktop_file, install : desktop_files
  merged = i18n.merge_file(
    input: configure_file(
      input: desktop_file + '.in.in',
      output: desktop_file + '.in',
      configuration: desktopconf,
    ),
    output: desktop_file,
    po_dir: '../po',
    install: true,
    install_dir: desktopdir,
    type: 'desktop',
  )

  if desktop_utils.found()
    test('Validate desktop file', desktop_utils, args: [merged], suite: 'tools')
  endif
endforeach

sessions = ['phosh.session']
foreach session : sessions
  desktop = session + '.desktop'
  i18n.merge_file(
    input: configure_file(
      input: desktop + '.in.in',
      output: desktop + '.in',
      configuration: desktopconf,
    ),
    output: session,
    po_dir: '../po',
    install: true,
    install_dir: join_paths(sessiondir, 'sessions'),
    type: 'desktop',
  )
endforeach

runconf = configuration_data()
runconf.set('bindir', bindir)
runconf.set('libexecdir', libexecdir)
runconf.set('pkgdatadir', pkgdatadir)
runconf.set('version', meson.project_version())
runconf.set('wlrootsdir', join_paths(libexecdir, 'wlroots'))
runconf.set('compositor', get_option('compositor'))

configure_file(
  input: 'phosh-session.in',
  output: 'phosh-session',
  install_dir: bindir,
  configuration: runconf,
  install: true,
)

# Generate XML enum definitions for GSettings schema
schema_enum_headers = files('..' / 'src' / 'phosh-settings-enums.h')
generate_enums_schema = gnome.mkenums(
  'mobi.phosh.shell.enums.xml',
  sources: schema_enum_headers,
  comments: '<!-- @comment@ -->',
  fhead: '<schemalist>',
  vhead: '  <@type@ id="mobi.phosh.shell.@EnumName@">',
  vprod: '    <value nick="@valuenick@" value="@valuenum@"/>',
  vtail: '  </@type@>',
  ftail: '</schemalist>',
  install_header: true,
  install_dir: schemasdir,
)

#workaround due to https://github.com/mesonbuild/meson/issues/1687
copy_schema = custom_target(
  'copy-gschema-to-builddir',
  input: 'mobi.phosh.shell.gschema.xml',
  output: 'mobi.phosh.shell.gschema.xml',
  command: ['cp', '@INPUT@', '@OUTPUT@'],
)
schemas = ['mobi.phosh.shell.gschema.xml']
compiled_schemas = custom_target(
  'glib-compile-schemas',
  build_by_default: true,
  output: 'gschemas.compiled',
  install: false,
  command: [find_program('glib-compile-schemas'), meson.current_build_dir()],
  depends: [generate_enums_schema, copy_schema],
)

overrides = ['00_mobi.Phosh.gschema.override']

install_data(schemas + overrides, install_dir: schemasdir)

subdir('systemd')
subdir('icons')

install_data('phoc.ini', install_dir: pkgdatadir)
install_data(
  'wayland-sessions/phosh.desktop',
  install_dir: 'share/wayland-sessions',
)

install_data(
  'phosh-shell.portal',
  install_dir: join_paths(datadir, 'xdg-desktop-portal', 'portals'),
)
install_data(
  'phosh-portals.conf',
  install_dir: join_paths(datadir, 'xdg-desktop-portal'),
)

shell_search_libexecdir = join_paths(prefix, get_option('libexecdir'))

serviceconf = configuration_data()
serviceconf.set('libexecdir', shell_search_libexecdir)

if get_option('searchd')
  configure_file(
    input: 'mobi.phosh.Shell.Search.service.in',
    output: 'mobi.phosh.Shell.Search.service',
    install_dir: servicedir,
    configuration: serviceconf,
    install: true,
  )
endif
