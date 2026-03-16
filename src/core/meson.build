cyaml_dep   = dependency('libcyaml')
systemd_dep = dependency('libsystemd')

core_lib = static_library('core', 'config.c',
    dependencies : [cyaml_dep],
    include_directories : include_directories('..'))

core_dep = declare_dependency(
    link_with           : core_lib,
    include_directories : include_directories('..'),
    dependencies        : [cyaml_dep, systemd_dep])
