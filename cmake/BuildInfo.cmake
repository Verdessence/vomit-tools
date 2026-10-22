# Writes BuildInfo.cpp with the project version and the `git describe` of the source tree.
# Runs at every build (see src/app/CMakeLists.txt); configure_file leaves the output untouched
# when nothing changed, so nothing gets recompiled.
#   cmake -DSOURCE_DIR=... -DVERSION=... -DTEMPLATE=... -DOUTPUT=... -P BuildInfo.cmake

set(COMMIT "")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
            WORKING_DIRECTORY "${SOURCE_DIR}"
            OUTPUT_VARIABLE describe
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE result)
    # Fails outside a git checkout (a source tarball), which just leaves the commit out.
    if(result EQUAL 0)
        set(COMMIT "${describe}")
    endif()
endif()

configure_file("${TEMPLATE}" "${OUTPUT}" @ONLY)
