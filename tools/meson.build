configure_file(input: 'run_tool.in', output: 'run_tool', configuration: run_data)

test(
  'check-exported-symbols',
  find_program('check-exported-symbols'),
  args: phosh,
  suite: 'tools',
)

test(
  'check-deprecated-ui-props',
  find_program('check-deprecated-ui-props'),
  args: meson.project_source_root(),
  suite: 'tools',
)

tools_or_tests = get_option('tools') or get_option('tests')

if tools_or_tests
  # app-buttons is used in the screenshot tests
  executable(
    'app-buttons',
    ['app-buttons.c'],
    dependencies: [phosh_tool_dep, test_stubs_dep],
  )
endif

if get_option('lockscreen-plugins') or get_option('quick-setting-plugins') or tools_or_tests
  # plugin-prefs is used in the tests
  executable(
    'plugin-prefs',
    ['plugin-prefs-standalone.c', '../src/plugin-loader.c'],
    c_args: [
      '-DBUILD_DIR="@0@"'.format(meson.project_build_root()),
      '-DLOCKSCREEN_PLUGINS="@0@"'.format(' '.join(lockscreen_plugins)),
      '-DQUICK_SETTING_PLUGINS="@0@"'.format(' '.join(quick_setting_plugins)),
      '-DTEST_INSTALLED="@0@/install"'.format(meson.project_build_root()),
    ],
    include_directories: [root_inc, phosh_inc],
    dependencies: [plugin_prefs_dep, gmodule_dep],
  )
endif

if get_option('tools')

  executable(
    'app-scroll',
    ['app-scroll.c'],
    dependencies: [phosh_tool_dep, test_stubs_dep],
  )

  executable(
    'app-grid-standalone',
    ['app-grid-standalone.c'],
    dependencies: [[phosh_tool_dep, phosh_search_dep], test_stubs_dep],
  )

  executable(
    'image-notify',
    ['image-notify.c'],
    dependencies: [phosh_tool_dep, test_stubs_dep],
  )

  executable('notify-blocks', ['notify-blocks.c'], dependencies: phosh_tool_dep)

  executable(
    'notify-server-standalone',
    ['notify-server-standalone.c'],
    dependencies: [phosh_tool_dep, test_stubs_dep],
  )

  executable(
    'dump-app-list',
    ['dump-app-list.c'],
    dependencies: [phosh_tool_dep, test_stubs_dep],
  )

  executable(
    'quick-settings-box-standalone',
    ['quick-settings-box-standalone.c'],
    dependencies: [phosh_tool_dep, test_stubs_dep],
  )

  executable(
    'status-icons-box-standalone',
    ['status-icons-box-standalone.c'],
    dependencies: [phosh_tool_dep, test_stubs_dep],
  )

  if get_option('lockscreen-plugins')
    executable(
      'widget-box',
      ['widget-box-standalone.c'],
      c_args: [
        '-DBUILD_DIR="@0@"'.format(meson.project_build_root()),
        '-DPLUGINS="@0@"'.format(' '.join(lockscreen_plugins)),
      ],
      link_args: phosh_export_sym_link_arg,
      dependencies: [gmodule_dep, phosh_tool_dep, test_stubs_dep],
    )
  endif

  if get_option('quick-setting-plugins')
    executable(
      'custom-quick-settings',
      ['custom-quick-settings-standalone.c'],
      c_args: [
        '-DBUILD_DIR="@0@"'.format(meson.project_build_root()),
        '-DPLUGINS="@0@"'.format(' '.join(quick_setting_plugins)),
      ],
      link_args: phosh_export_sym_link_arg,
      link_with: [phosh_lib],
      dependencies: [gmodule_dep, phosh_tool_dep, test_stubs_dep],
    )
  endif

  if get_option('status-icon-plugins')
    executable(
      'custom-status-icons',
      ['custom-status-icons-standalone.c'],
      c_args: [
        '-DBUILD_DIR="@0@"'.format(meson.project_build_root()),
        '-DPLUGINS="@0@"'.format(' '.join(status_icon_plugins)),
      ],
      link_args: phosh_export_sym_link_arg,
      link_with: [phosh_lib],
      dependencies: [gmodule_dep, phosh_tool_dep, test_stubs_dep],
    )
  endif

  executable(
    'search',
    ['search.c'],
    dependencies: [[phosh_tool_dep, phosh_search_dep], test_stubs_dep],
  )

endif
