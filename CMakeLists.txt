cmake_minimum_required(VERSION 3.25)

project(vomit-tools
        VERSION 0.6.6
        DESCRIPTION "Soundscape, skybox and particle editors for Source engine games"
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# The core library also goes into a shared plugin (the KDE thumbnailer).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(VOMIT_TOOLS_BUILD_APP   "Build the Qt application" ON)
option(VOMIT_TOOLS_BUILD_TESTS "Build the test suite"     ON)
option(VOMIT_TOOLS_BUILD_CLI   "Build vomit-tools-cli"    ON)
option(VOMIT_TOOLS_KDE_THUMBNAILER "Build the KDE (Dolphin) thumbnail plugin when KDE Frameworks 6 KIO is found" ON)

if(WIN32)
    enable_language(RC)
endif()

include(cmake/Dependencies.cmake)
include(cmake/CompilerOptions.cmake)

add_subdirectory(src/core)

if(VOMIT_TOOLS_BUILD_CLI)
    add_subdirectory(src/cli)
endif()

if(VOMIT_TOOLS_BUILD_APP)
    add_subdirectory(src/app)
endif()

if(VOMIT_TOOLS_BUILD_APP AND VOMIT_TOOLS_KDE_THUMBNAILER AND UNIX AND NOT APPLE)
    find_package(KF6KIO QUIET)
    find_package(KF6CoreAddons QUIET)
    if(KF6KIO_FOUND AND KF6CoreAddons_FOUND)
        # Where Qt looks for plugins, relative to the install prefix (lib/qt6/plugins on Arch).
        get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
        execute_process(COMMAND ${_qmake} -query QT_INSTALL_PLUGINS OUTPUT_VARIABLE _plugins OUTPUT_STRIP_TRAILING_WHITESPACE)
        execute_process(COMMAND ${_qmake} -query QT_INSTALL_PREFIX OUTPUT_VARIABLE _prefix OUTPUT_STRIP_TRAILING_WHITESPACE)
        file(RELATIVE_PATH VOMIT_TOOLS_QT_PLUGIN_DIR "${_prefix}" "${_plugins}")
        add_subdirectory(src/thumbnailer)
        message(STATUS "vomit-tools: building the KDE thumbnail plugin")
    endif()
endif()

if(VOMIT_TOOLS_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    enable_testing()
    add_subdirectory(tests)
endif()
