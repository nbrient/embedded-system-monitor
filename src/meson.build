subdir('core')
subdir('watchdog')
subdir('sink')
subdir('collectors/cpu')

executable('embedded-monitor',
    'main.c',
    dependencies : [core_dep, sink_dep, cpu_collector_dep])
