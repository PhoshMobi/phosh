run_phoc_tests = false

subdir('stubs')

if not get_option('tests')
  subdir_done()
endif

subdir('services')
subdir('system')
subdir('user')
subdir('integration')

screenshot_langs = ['ar_AE.UTF-8', 'de_DE.UTF-8', 'ja_JP.UTF-8', 'uk_UA.UTF-8']

phoc = find_program('phoc', required: get_option('phoc_tests'), disabler: true)
run_phoc_tests = phoc.found() and not get_option('phoc_tests').disabled()

test_env_common = environment()
test_env_common.set('G_TEST_SRCDIR', meson.current_source_dir())
test_env_common.set('G_TEST_BUILDDIR', meson.current_build_dir())
test_env_common.set('G_DEBUG', 'gc-friendly,fatal-warnings')
test_env_common.set('GSETTINGS_BACKEND', 'memory')
test_env_common.set(
  'GSETTINGS_SCHEMA_DIR',
  '@0@/data:@1@'.format(meson.project_build_root(), plugin_schema_path),
)
# Make sure we use Phosh's gsetting prefs
test_env_common.set('XDG_CURRENT_DESKTOP', 'Phosh:GNOME')
test_env_common.set('PYTHONDONTWRITEBYTECODE', 'yes')
test_env_common.set('MALLOC_CHECK_', '2')
test_env_common.set('NO_AT_BRIDGE', '1')
# For GTK4 apps running in the tests:
test_env_common.set('GTK_A11Y', 'none')
# Make sure gvfs doesn't linger around
test_env_common.set('GIO_USE_VFS', 'local')

# For -Db_sanitize=address
test_env_common.set(
  'LSAN_OPTIONS',
  'suppressions=@0@/data/leak-suppress.txt'.format(meson.project_source_root()),
)
# as our libraries lack frame pointers since otherwise back traces are incomplete
test_env_common.set('ASAN_OPTIONS', 'fast_unwind_on_malloc=0')
# Useful when running locally and glib hits an assertion:
#test_env_common.set('ASAN_OPTIONS', 'fast_unwind_on_malloc=0:disable_coredump=0:unmap_shadow_on_exit=1:abort_on_error=1')

# xdg setup for the app-grid:
test_env_unit = test_env_common
test_env_unit.set(
  'XDG_CONFIG_HOME',
  '@0@/user/config/'.format(meson.current_source_dir()),
)
test_env_unit.set(
  'XDG_CONFIG_DIRS',
  '@0@/system/config/'.format(meson.current_source_dir()),
)
test_env_unit.set(
  'XDG_DATA_HOME',
  '@0@/user/share/'.format(meson.current_source_dir()),
)
test_env_unit.set('XDG_DATA_DIRS', '/usr/local/share/', '/usr/share/')
# Ideally we would just set it so that we have a known set of .desktop etc
# but then we can't find the system gschemas
test_env_unit.prepend(
  'XDG_DATA_DIRS',
  '@0@/system/share/'.format(meson.current_source_dir()),
)


test_resources = gnome.compile_resources(
  'phosh-test-resources',
  'phosh-test.gresources.xml',
  extra_args: '--manual-register',
  c_name: 'phosh_test',
)

test_cflags = [
  '-DTEST_BUILD_DIR="@0@"'.format(meson.project_build_root()),
  '-DTEST_DATA_DIR="@0@/data"'.format(meson.current_source_dir()),
  '-DTEST_OUTPUT_DIR="@0@"'.format(meson.current_build_dir()),
  '-DTEST_PHOC_INI="@0@/data/phoc.ini"'.format(meson.project_source_root()),
  '-DTEST_TOOLS="@0@/tools"'.format(meson.project_build_root()),
]

test_link_args = ['-fPIC']

testlib_sources = [
  'testlib.c',
  'testlib-head-stub.c',
  'testlib-compositor.c',
  'testlib-full-shell.c',
  'testlib-calls-mock.c',
  'testlib-mpris-mock.c',
  'testlib-emergency-calls.c',
  'testlib-wait-for-shell-state.c',
] + test_resources

testlib = static_library(
  'phoshtest',
  testlib_sources,
  dependencies: [phosh_deps, phosh_tool_dep],
  c_args: test_cflags,
)
testlib_dep = declare_dependency(
  include_directories: include_directories('.'),
  dependencies: phosh_tool_dep,
  link_with: testlib,
)

tests_unit = [
  'activity',
  'app-grid-button',
  'app-grid-folder-button',
  'app-list-model',
  'auto-brightness-bucket',
  'connectivity-info',
  'css',
  'fading-label',
  'favorite-model',
  'folder-info',
  'gamma-table',
  'head',
  'keypad',
  'media-player',
  'mount-notification',
  'notification',
  'notification-content',
  'notification-frame',
  'notification-list',
  'notification-source',
  'notify-feedback',
  'overview',
  'plugin-loader',
  'quick-setting',
  'quick-settings-box',
  'status-icon',
  'status-icons-box',
  'timestamp-label',
  'util',
  'wall-clock',
]

tests_searchd = ['search-result-meta', 'search-source', 'search-provider']

tests_phoc = [
  'app-auth-prompt',
  'background',
  'drag-surface',
  'end-session-dialog',
  'layer-surface',
  'lockshield',
  'notification-banner',
  'osd-window',
  'system-modal',
  'system-modal-dialog',
  'power-menu',
]

tests_manager = [
  ['calls-manager', true],
  ['gtk-mount-manager', true],
  ['idle-manager', true],
  ['lockscreen', true],
  ['monitor-manager', true],
  ['notify-manager', true],
  ['screenshot-manager', true],
]

# Unit tests
foreach test : tests_unit
  t = executable(
    'test-@0@'.format(test),
    ['test-@0@.c'.format(test)],
    c_args: test_cflags,
    pie: true,
    link_args: test_link_args,
    dependencies: [testlib_dep, test_stubs_dep],
  )
  test(test, t, env: test_env_unit, depends: compiled_schemas, suite: ['unit'])
endforeach

foreach test : tests_searchd
  t = executable(
    'test-@0@'.format(test),
    ['test-@0@.c'.format(test)],
    c_args: test_cflags,
    pie: true,
    link_args: test_link_args,
    dependencies: [testlib_dep, test_stubs_dep, phosh_search_dep],
  )
  test(
    test,
    t,
    env: test_env_unit,
    depends: mock_search_provider_exec,
    suite: ['unit'],
  )
endforeach

if run_phoc_tests
  test_env_phoc = test_env_common
  # Make sure this is valid when running the compositor
  test_env_phoc.set('XDG_RUNTIME_DIR', meson.current_build_dir())

  # Unit tests
  foreach test : tests_phoc
    t = executable(
      'test-@0@'.format(test),
      ['test-@0@.c'.format(test)],
      c_args: test_cflags,
      pie: true,
      link_args: test_link_args,
      dependencies: [testlib_dep, test_stubs_dep],
    )
    test(test, t, suite: ['unit', 'phoc'], env: test_env_phoc, is_parallel: true)
  endforeach

  # Integration tests using the full libphosh
  t = executable(
    'test-shell',
    'test-shell.c',
    c_args: test_cflags,
    pie: true,
    link_args: test_link_args,
    dependencies: [phosh_static_lib_dep, testlib_dep],
  )
  test('shell', t, env: test_env_phoc, suite: ['integration'], is_parallel: true)
  foreach item : tests_manager
    test = item.get(0)
    parallel = item.get(1)

    t = executable(
      'test-@0@'.format(test),
      ['test-@0@.c'.format(test), generated_dbus_sources],
      c_args: test_cflags,
      pie: true,
      link_args: test_link_args,
      dependencies: [phosh_static_lib_dep, testlib_dep],
    )
    test(
      test,
      t,
      env: test_env_phoc,
      suite: ['integration'],
      is_parallel: parallel,
    )
  endforeach

  # Tests for manual validation
  t = executable(
    'test-take-screenshots',
    ['test-take-screenshots.c', generated_dbus_sources],
    c_args: test_cflags,
    pie: true,
    link_args: test_link_args + phosh_export_sym_link_arg,
    dependencies: [phosh_static_lib_dep, testlib_dep],
  )
  foreach lang : screenshot_langs
    lang_env = test_env_phoc
    lang_env.set('LANGUAGE', lang)
    lang_env.set('LC_ALL', lang)
    lang_env.set('PHOSH_TEST_TYPE', lang)
    lang_env.set(
      'XDG_DATA_HOME',
      '@0@/user/share/'.format(meson.current_source_dir()),
    )
    test(
      'screenshots-@0@'.format(lang),
      t,
      env: lang_env,
      suite: ['screenshots'],
      timeout: 60,
    )
  endforeach

  what = 'high-contrast'
  hc_env = test_env_phoc
  hc_env.set('LANGUAGE', 'C')
  hc_env.set('PHOSH_TEST_TYPE', what)
  hc_env.set(
    'XDG_CONFIG_HOME',
    '@0@/user/config-@1@/'.format(meson.current_build_dir(), what),
  )
  hc_env.set(
    'XDG_DATA_HOME',
    '@0@/user/share/'.format(meson.current_source_dir()),
  )
  hc_env.set('GSETTINGS_BACKEND', 'keyfile')
  test(
    'screenshots-@0@'.format(what),
    t,
    env: hc_env,
    depends: copy_keyfile_hc,
    suite: ['screenshots'],
    timeout: 120,
  )

  what = 'landscape'
  landscape_env = test_env_phoc
  landscape_env.set('LANGUAGE', 'C')
  landscape_env.set('PHOSH_TEST_TYPE', what)
  landscape_env.set(
    'PHOSH_TEST_PHOC_INI',
    '@0@/data/phoc-landscape.ini'.format(meson.current_source_dir()),
  )
  landscape_env.set(
    'XDG_DATA_HOME',
    '@0@/user/share/'.format(meson.current_source_dir()),
  )
  test(
    'screenshots-@0@'.format(what),
    t,
    env: landscape_env,
    suite: ['screenshots'],
    timeout: 60,
  )

  what = 'notch'
  notch_env = test_env_phoc
  notch_env.set('LANGUAGE', 'C')
  notch_env.set('PHOC_DEBUG', 'cutouts')
  notch_env.set('PHOSH_DEBUG', 'fake-builtin')
  notch_env.set('PHOSH_TEST_TYPE', what)
  notch_env.set(
    'PHOSH_TEST_PHOC_INI',
    '@0@/data/phoc-notch.ini'.format(meson.current_source_dir()),
  )
  notch_env.set('GMOBILE_DT_COMPATIBLES', 'oneplus,fajita')
  notch_env.set(
    'XDG_DATA_HOME',
    '@0@/user/share/'.format(meson.current_source_dir()),
  )
  test(
    'screenshots-@0@'.format(what),
    t,
    env: notch_env,
    suite: ['screenshots'],
    timeout: 120,
  )
endif

foreach opt : ['--help', '--version']
  test(
    'phosh @0@'.format(opt),
    phosh,
    args: opt,
    env: test_env_unit,
    suite: ['unit'],
    timeout: 15,
    is_parallel: true,
  )
endforeach

flake8 = find_program('flake8', required: false)
black = find_program('black', required: false)
ruff = find_program('ruff', required: false)

pyfiles = files(
  'integration/__init__.py',
  'integration/test_dbus.py',
  'integration/test_output.py',
  'mock-mm-nm.py',
)

foreach pyfile : pyfiles
  if flake8.found()
    test(
      'flake8-@0@'.format(fs.name(pyfile)),
      flake8,
      args: ['--ignore', 'E501', pyfile],
      env: test_env_unit,
      suite: ['lint'],
      timeout: 15,
      is_parallel: true,
    )
  endif

  if black.found()
    test(
      'black-@0@'.format(fs.name(pyfile)),
      black,
      args: ['--check', '--diff', pyfile],
      env: test_env_unit,
      suite: ['lint'],
      timeout: 15,
      is_parallel: true,
    )
  endif

  if ruff.found()
    test(
      'ruff-@0@'.format(fs.name(pyfile)),
      ruff,
      args: ['check', '--no-cache', pyfile],
      env: test_env_unit,
      suite: ['lint'],
      timeout: 15,
      is_parallel: true,
    )
  endif

endforeach
