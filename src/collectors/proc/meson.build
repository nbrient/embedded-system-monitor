proc_collector_lib = static_library('proc_collector',
    'proc_collector.c',
    dependencies : [core_dep, watchdog_dep, sink_dep])

proc_collector_dep = declare_dependency(
    link_with           : proc_collector_lib,
    include_directories : include_directories('../..'))
