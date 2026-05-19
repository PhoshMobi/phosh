gsd_wants = ''
foreach component : gsd_required_components
  gsd_wants += 'Wants=' + component + '.target\n'
endforeach

session_dropins = ['gnome-session@phosh.target.d']

sessionconf = configuration_data()
sessionconf.set('gsd_wants', gsd_wants)
sessionconf.set('app_id', app_id)

foreach session_dropin : session_dropins
  configure_file(
    input: 'phosh.session.conf.in',
    output: 'session.conf',
    install_dir: systemduserdir / session_dropin,
    configuration: sessionconf,
    install: true,
  )
endforeach

serviceconf = configuration_data()
serviceconf.set('libexecdir', libexecdir)
configure_file(
  input: 'mobi.phosh.Shell.service.in',
  output: 'mobi.phosh.Shell.service',
  install_dir: systemduserdir,
  configuration: serviceconf,
  install: true,
)

foreach type : ['Shell', 'OSK']
  install_data(f'mobi.phosh.@type@.target', install_dir: systemduserdir)
endforeach
