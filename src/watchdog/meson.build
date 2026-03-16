watchdog_lib = static_library('watchdog', 'watchdog.c',
    dependencies : [core_dep])

watchdog_dep = declare_dependency(
    link_with           : watchdog_lib,
    include_directories : include_directories('..'))
