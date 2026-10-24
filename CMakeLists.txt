cmake_minimum_required(VERSION 3.25)

project(vomit-tools
        VERSION 0.5.0
        DESCRIPTION "Soundscape, skybox and particle editors for Source engine games"
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VOMIT_TOOLS_BUILD_APP   "Build the Qt application" ON)
option(VOMIT_TOOLS_BUILD_TESTS "Build the test suite"     ON)

if(WIN32)
    enable_language(RC)
endif()

include(cmake/Dependencies.cmake)
include(cmake/CompilerOptions.cmake)

add_subdirectory(src/core)

if(VOMIT_TOOLS_BUILD_APP)
    add_subdirectory(src/app)
endif()

if(VOMIT_TOOLS_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    enable_testing()
    add_subdirectory(tests)
endif()
