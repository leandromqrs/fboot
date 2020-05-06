cmake_minimum_required(VERSION 2.6)

PROJECT(e4rat)
set(E4RATFX_VERSION   "0.2.3")

IF( NOT CMAKE_BUILD_TYPE )
    SET( CMAKE_BUILD_TYPE "debug" )
ENDIF()

IF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/" CACHE PATH "e4rat install prefix" FORCE)
ENDIF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)

set(Boost_USE_STATIC_LIBS   OFF)
set(Boost_USE_MULTITHREADED ON)
add_definitions(-DBOOST_FILESYSTEM_VERSION=3)
find_package(Boost 1.41 COMPONENTS system filesystem regex REQUIRED)
set(${PROJECT_NAME}_LIBRARIES   ${${PROJECT_NAME}_LIBRARIES}
    ${Boost_LIBRARIES})
set(Boost_VERSION_STR  "${Boost_MAJOR_VERSION}.${Boost_MINOR_VERSION}.${Boost_SUBMINOR_VERSION}")

add_subdirectory(   ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_subdirectory(   ${CMAKE_CURRENT_SOURCE_DIR}/doc)

add_custom_target(src
        COMMAND test -e ${PROJECT_NAME}-${E4RAT_VERSION} || mkdir ${PROJECT_NAME}-${E4RAT_VERSION}
        COMMAND rsync -p --relative `git ls-files` ${PROJECT_NAME}-${E4RAT_VERSION}
        COMMAND tar pczf ${PROJECT_NAME}_${E4RAT_VERSION}_src.tar.gz ${PROJECT_NAME}-${E4RAT_VERSION}
        )

INSTALL(FILES e4rat.conf.sample
        DESTINATION "/etc"
        RENAME e4rat.conf
        )
