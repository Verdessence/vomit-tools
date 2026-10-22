include(FetchContent)

# Qt is only needed by the app and the tests; the core library is plain C++.
if(VOMIT_TOOLS_BUILD_APP OR VOMIT_TOOLS_BUILD_TESTS)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Test)
    qt_standard_project_setup()
endif()

# sourcepp (MIT): only vpkpp is needed for now. vtfpp gets switched on with the skybox editor.
# For offline builds, point FETCHCONTENT_SOURCE_DIR_SOURCEPP at a local checkout.
set(SOURCEPP_LIBS_START_ENABLED   OFF CACHE INTERNAL "")
set(SOURCEPP_USE_VPKPP            ON  CACHE INTERNAL "")
set(SOURCEPP_VPKPP_SUPPORT_VPK_V54 OFF CACHE INTERNAL "")
set(SOURCEPP_BUILD_TESTS          OFF CACHE INTERNAL "")

FetchContent_Declare(sourcepp
        URL https://github.com/craftablescience/sourcepp/archive/5dcbbe8ac556b1c833cccf31a2b016e114dbc3f0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(sourcepp)

# miniaudio (public domain / MIT-0): decoding WAV/MP3 and playing sounds for previews.
# Only its sources are used; its own CMake project is not.
FetchContent_Declare(miniaudio
        URL https://github.com/mackron/miniaudio/archive/refs/tags/0.11.25.tar.gz
        URL_HASH SHA256=b900edcffe979816e2560a0580b9b1216d674b4f17fbadeca8f777a7f8ab0274
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        SOURCE_SUBDIR do-not-add-subdirectory)
FetchContent_MakeAvailable(miniaudio)

add_library(vomit_tools_miniaudio STATIC "${miniaudio_SOURCE_DIR}/miniaudio.c")
target_include_directories(vomit_tools_miniaudio SYSTEM PUBLIC "${miniaudio_SOURCE_DIR}")
# Encoding and waveform generation are never used.
target_compile_definitions(vomit_tools_miniaudio PUBLIC MA_NO_ENCODING MA_NO_GENERATION)
find_package(Threads REQUIRED)
target_link_libraries(vomit_tools_miniaudio PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX AND NOT APPLE)
    target_link_libraries(vomit_tools_miniaudio PRIVATE m)
endif()
