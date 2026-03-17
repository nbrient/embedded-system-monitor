subdir('core')
subdir('watchdog')
subdir('sink')
subdir('collectors/cpu')
subdir('collectors/mem')

executable('embedded-monitor',
    'main.c',
    dependencies : [core_dep, sink_dep, cpu_collector_dep, mem_collector_dep])
