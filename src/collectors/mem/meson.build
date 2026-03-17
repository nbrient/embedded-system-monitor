mem_collector_lib = static_library('mem_collector',
    'mem_collector.c',
    dependencies : [core_dep, watchdog_dep, sink_dep])

mem_collector_dep = declare_dependency(
    link_with           : mem_collector_lib,
    include_directories : include_directories('../..'))
