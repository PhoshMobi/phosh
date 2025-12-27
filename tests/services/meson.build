service_data = configuration_data()
service_data.set('ABS_BUILDDIR', meson.project_build_root())

dbus_interface = meson.project_source_root() / 'src' / 'dbus' / 'org.gnome.Shell.SearchProvider2.xml'
service = 'org.gnome.Phosh.MockSearchProvider.service'

configure_file(
  input: service + '.in',
  output: service,
  configuration: service_data,
)

sources = files('testlib-search-provider-app.c', 'testlib-search-provider.c')

sources += gnome.gdbus_codegen(
  'phosh-search-provider-mock-generated',
  sources: dbus_interface,
  interface_prefix: 'org.gnome.',
  namespace: 'Phosh',
)

mock_search_provider_exec = executable(
  'mock-search-provider',
  sources,
  include_directories: root_inc,
  dependencies: [gio_dep, glib_dep, gtk_dep],
)
