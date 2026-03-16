cpu_collector_lib = static_library('cpu_collector',
    'cpu_collector.c',
    dependencies : [core_dep, watchdog_dep, sink_dep])

cpu_collector_dep = declare_dependency(
    link_with           : cpu_collector_lib,
    include_directories : include_directories('../..'))
