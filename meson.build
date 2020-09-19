project(
    'fboot',
    'c',
    version : '0.3.6',
    default_options : ['default_library=static']
)
as_version = meson.project_version()
locale_install_dir = '/usr/share/locale'
boost_use_static_libs = 'OFF'
boost_use_multithreaded = 'OFF'
#Boost_dep = dependency('Boost')
#boost_version_str = '${Boost_MAJOR_VERSION}.${Boost_MINOR_VERSION}.${Boost_SUBMINOR_VERSION}'
subdir('src')
subdir('doc')
#subdir('CMAKE_CURRENT_SOURCE_DIR')
# install(['FILES', 'e4rat-lite.conf', 'DESTINATION', '/etc/'])
