irq_collector_lib = static_library('irq_collector',
    'irq_collector.c',
    dependencies : [core_dep, watchdog_dep, sink_dep])

irq_collector_dep = declare_dependency(
    link_with           : irq_collector_lib,
    include_directories : include_directories('../..'))
