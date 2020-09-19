conf_data = configuration_data()
conf_data.set_quoted('PROGRAM_NAME', meson.project_name())
conf_data.set_quoted('VERSION', meson.project_version())
configure_file(
    output : 'global.h',
    configuration : conf_data
)
configuration_inc = include_directories('.')
# Dependencies to make the project
ext2fs_dep = dependency('ext2fs')
audit_dep = dependency('audit')
auparse_dep = dependency('auparse')
Threads_dep = dependency('Threads')

# Building source code
e4head_core_src = ['ini.c', 'config.c', 'logging.c', 'common.c', 'fiemap.c', 'device.c']
e4head_collect_exe = executable('fboot', 'collect.c', 'fileptr.c', 'listener.c', 'eventcatcher.c', include_directories : configuration_inc)
#e4head_preload_exe = executable('e4head-preload', 'e4rat-preload.c')
#e4head_realloc_exe = executable('e4head-realloc', 'e4rat-realloc.cc', 'defrag.cc', 'buddycache.cc')

e4head_core_lib = shared_library('fboot', e4head_core_src)
# set_target_properties(['e4head-core', 'PROPERTIES', 'SOVERSION', '0'])
# target_link_libraries(['e4head-core', e4head_libraries, e4head_static_libraries])
# foreach(['EXE', 'e4head-collect', 'e4head-realloc', 'e4head-preload'])
# target_link_libraries([exe, 'e4head-core'])
# endforeach('EXE')
###
# install project targets
###
e4head_targets = ['e4head-core', 'e4head-collect', 'e4head-preload', 'e4head-realloc']
# install(['TARGETS', e4head_targets, 'RUNTIME', 'DESTINATION', 'bin', 'LIBRARY', 'DESTINATION', '${CMAKE_INSTALL_PREFIX}/lib'])
# install(['DIRECTORY', 'DESTINATION', '/var/lib/e4head'])
