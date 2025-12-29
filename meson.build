project(
  'phosh',
  'c',
  version: '0.52.rc1',
  license: 'GPL-3.0-or-later',
  meson_version: '>= 1.7.0',
  default_options: [
    'warning_level=1',
    'buildtype=debugoptimized',
    'c_std=gnu11',
  ],
)

# libphosh's version is stable. It should only be bumped for API changes
libphosh_api_version = '0.45'

app_id = 'mobi.phosh.Shell'
ver_parts = meson.project_version().split('.')
lib_inc_subdir = 'libphosh-@0@'.format(libphosh_api_version)

prefix = get_option('prefix')
bindir = join_paths(prefix, get_option('bindir'))
datadir = join_paths(prefix, get_option('datadir'))
localedir = join_paths(prefix, get_option('localedir'))
libdir = join_paths(prefix, get_option('libdir'))
libexecdir = join_paths(prefix, get_option('libexecdir'))
desktopdir = join_paths(datadir, 'applications')
sessiondir = join_paths(datadir, 'gnome-session')
pkgdatadir = join_paths(datadir, meson.project_name())
pkglibdir = join_paths(libdir, meson.project_name())
systemddir = join_paths(prefix, 'lib/systemd')
systemduserdir = join_paths(systemddir, 'user')
plugins_dir = join_paths(prefix, libdir, 'phosh', 'plugins')
plugin_prefs_dir = join_paths(prefix, libdir, 'phosh', 'plugins', 'prefs')
phosh_plugin_icon_dir = datadir / 'phosh' / 'plugins' / 'icons'
servicedir = join_paths(datadir, 'dbus-1', 'services')
schemasdir = datadir / 'glib-2.0/schemas'
lib_inc_dir = prefix / get_option('includedir') / lib_inc_subdir
vapidir = prefix / datadir / 'vala' / 'vapi'

enable_introspection = get_option('introspection') or get_option('gtk_doc')
bindings_lib = get_option('bindings-lib')
abi_check = get_option('abi-check')

if prefix != '/usr'
  sysconfdir = join_paths(prefix, get_option('sysconfdir'))
else
  sysconfdir = get_option('sysconfdir')
endif

glib_ver = '2.80'
glib_ver_str = 'GLIB_VERSION_@0@'.format(glib_ver.replace('.', '_'))
glib_ver_cmp = '>=@0@'.format(glib_ver)

add_project_arguments(
  [
    '-DGLIB_VERSION_MIN_REQUIRED=@0@'.format(glib_ver_str),
    '-DGLIB_VERSION_MAX_REQUIRED=@0@'.format(glib_ver_str),
    '-DG_LOG_USE_STRUCTURED',
    '-DGMOBILE_USE_UNSTABLE_API',
    '-DLIBFEEDBACK_USE_UNSTABLE_API',
  ],
  language: 'c',
)

root_inc = include_directories('.')

cc = meson.get_compiler('c')

global_c_args = []
test_c_args = [
  '-Wcast-align',
  '-Wdate-time',
  '-Wdeclaration-after-statement',
  ['-Werror=format-security', '-Werror=format=2'],
  '-Wendif-labels',
  '-Werror=incompatible-pointer-types',
  '-Werror=missing-declarations',
  '-Werror=overflow',
  '-Werror=return-type',
  '-Werror=shift-count-overflow',
  '-Werror=shift-overflow=2',
  '-Werror=implicit-fallthrough=3',
  '-Wfloat-equal',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Winit-self',
  '-Wmaybe-uninitialized',
  '-Wmissing-field-initializers',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wnested-externs',
  '-Wno-missing-field-initializers',
  '-Wno-sign-compare',
  '-Wno-strict-aliasing',
  '-Wno-unused-parameter',
  '-Wold-style-definition',
  '-Wpointer-arith',
  '-Wredundant-decls',
  '-Wshadow',
  '-Wstrict-prototypes',
  '-Wswitch-default',
  '-Wswitch-enum',
  '-Wtype-limits',
  '-Wundef',
  '-Wunused-function',
]
if get_option('buildtype') != 'plain'
  test_c_args += '-fstack-protector-strong'
endif

foreach arg : test_c_args
  if cc.has_multi_arguments(arg)
    global_c_args += arg
  endif
endforeach

if cc.get_id() == 'clang'
  # Avoid cast align warnings for wl_container_of, etc
  global_c_args += '-Wno-cast-align'
endif

add_project_arguments(global_c_args, language: 'c')

run_data = configuration_data()
run_data.set('ABS_BUILDDIR', meson.current_build_dir())
run_data.set('ABS_SRCDIR', meson.current_source_dir())
configure_file(input: 'run.in', output: 'run', configuration: run_data)

gnome = import('gnome')
i18n = import('i18n')
pkgconfig = import('pkgconfig')

appstream_dep = dependency('appstream', version: '>= 1.0.0')
ecal_dep = dependency('libecal-2.0', version: '>= 3.33.1')
eds_dep = dependency('libedataserver-1.2', version: '>= 3.33.1')
fribidi_dep = dependency('fribidi')
gcr_dep = dependency('gcr-3', version: '>= 3.7.5')
glib_dep = dependency('glib-2.0', version: glib_ver_cmp)
gio_dep = dependency('gio-2.0', version: glib_ver_cmp)
gio_unix_dep = dependency('gio-unix-2.0', version: glib_ver_cmp)
gnome_bluetooth_dep = dependency('gnome-bluetooth-3.0', version: '>= 46.0')
gmobile_dep = dependency(
  'gmobile',
  version: '>= 0.1.0',
  fallback: ['gmobile', 'gmobile_dep'],
  default_options: [
    'examples=false',
    'introspection=false',
    'gtk_doc=false',
    'tests=false',
  ],
)
gmodule_dep = dependency('gmodule-no-export-2.0', version: glib_ver_cmp)
gnome_desktop_dep = dependency('gnome-desktop-3.0', version: '>=3.26')
gobject_dep = dependency('gobject-2.0', version: glib_ver_cmp)
gsettings_desktop_schemas_dep = dependency(
  'gsettings-desktop-schemas',
  version: '>=47',
)
gtk_dep = dependency('gtk+-3.0', version: '>=3.22')
gtk_wayland_dep = dependency('gtk+-wayland-3.0', version: '>=3.22')
gudev_dep = dependency('gudev-1.0')
libcall_ui = subproject(
  'libcall-ui',
  default_options: [
    'examples=false',
    'gtk_doc=false',
    'tests=false',
    'install-i18n=@0@'.format(get_option('callui-i18n')),
  ],
)
libfeedback_dep = dependency(
  'libfeedback-0.0',
  version: '>= 0.7.0',
  fallback: ['libfeedback', 'libfeedback_dep'],
  default_options: [
    'introspection=disabled',
    'daemon=false',
    'gtk_doc=false',
    'tests=false',
  ],
)
libgvc = subproject(
  'gvc',
  default_options: [
    'package_name=' + meson.project_name(),
    'package_version=' + meson.project_version(),
    'pkgdatadir=' + pkgdatadir,
    'pkglibdir=' + pkglibdir,
    'static=true',
    'introspection=false',
    'alsa=false',
  ],
)
libgvc_dep = libgvc.get_variable('libgvc_dep')
libhandy_dep = dependency(
  'libhandy-1',
  version: '>=1.1.90',
  fallback: ['libhandy', 'libhandy_dep'],
  default_options: ['introspection=disabled'],
)
libcall_ui_dep = libcall_ui.get_variable('libcall_ui_dep')
libnm_dep = dependency('libnm', version: '>= 1.14')
libpolkit_agent_dep = dependency('polkit-agent-1', version: '>= 0.122')
libsoup_dep = dependency('libsoup-3.0', version: '>= 3.6')
libsystemd_dep = dependency('libsystemd', 'libelogind', version: '>= 241')
mm_glib_dep = dependency('mm-glib', version: '>= 1.24')
network_agent_dep = dependency('libsecret-1')
upower_glib_dep = dependency('upower-glib', version: '>=1.90')
wayland_client_dep = dependency('wayland-client', version: '>=1.14')
wayland_protos_dep = dependency('wayland-protocols', version: '>=1.12')

code = '''
#include <linux/rfkill.h>

struct rfkill_event_ext e;
'''
have_rfkill_event_ext = cc.compiles(code, name: 'Have rfkill_event_ext')
have_memfd_create = cc.has_function(
  'memfd_create',
  prefix: ['#define _GNU_SOURCE', '#include <sys/mman.h>'],
)

config_h = configuration_data()
config_h.set_quoted('GETTEXT_PACKAGE', 'phosh')
config_h.set_quoted('LOCALEDIR', localedir)
config_h.set_quoted('PHOSH_PLUGINS_DIR', plugins_dir)
config_h.set_quoted('PHOSH_VERSION', meson.project_version())
config_h.set('PHOSH_ANIMATION_SLOWDOWN', get_option('animation-slowdown'))
config_h.set('HAVE_RFKILL_EVENT_EXT', have_rfkill_event_ext)
config_h.set(
  'PHOSH_HAVE_MEMFD_CREATE',
  have_memfd_create,
  description: 'Whether we have memdfd_create on Linux',
)
config_h.set(
  'PHOSH_USES_ASAN',
  get_option('b_sanitize') == 'address',
  description: 'Whether ASAN was enabled via meson',
)
config_h.set_quoted(
  'PHOSH_DATA_DIR',
  datadir,
  description: 'Systemwide data dir',
)
config_h.set_quoted('PHOSH_APP_ID', app_id)
config_h.set_quoted('PHOSH_DBUS_PATH_PREFIX', '/' + app_id.replace('.', '/'))

configure_file(
  output: 'phosh-config.h',
  configuration: config_h,
  install: bindings_lib,
  install_dir: lib_inc_dir,
)

subdir('data')
subdir('po')
subdir('protocol')
subdir('src')
subdir('plugins')
subdir('searchd')
subdir('tests')
subdir('tools')
subdir('docs')
subdir('calendar-server')

summary(
  {
    'Tests': get_option('tests'),
    'Phoc tests': run_phoc_tests,
    'Documentation': get_option('gtk_doc'),
    'Introspection': enable_introspection,
    'Manual pages': get_option('man'),
    'Tools': get_option('tools'),
    'Lockscreen Plugins': get_option('lockscreen-plugins'),
    'Quick Setting Plugins': get_option('quick-setting-plugins'),
    'Animation slowdown': get_option('animation-slowdown'),
    'Bindings Library': bindings_lib,
    'ABI Compliance Check': abi_check,
    'Searchd': get_option('searchd'),
  },
  bool_yn: true,
  section: 'Build',
)

summary(
  {'Compositor:': get_option('compositor')},
  bool_yn: true,
  section: 'Runtime',
)

gnome.post_install(glib_compile_schemas: true, gtk_update_icon_cache: true)
