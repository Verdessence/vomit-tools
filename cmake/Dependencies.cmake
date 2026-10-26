include(FetchContent)

# Qt is only needed by the app and the tests; the core library is plain C++.
if(VOMIT_TOOLS_BUILD_APP OR VOMIT_TOOLS_BUILD_TESTS)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network Test)
    find_package(Qt6 QUIET OPTIONAL_COMPONENTS ShaderTools GuiPrivate)
    qt_standard_project_setup()
endif()

# sourcepp (MIT): only vpkpp is used. VTF reading and writing is our own (src/core/texture): skies
# need a handful of formats, and vtfpp would add compressonator, bcdec and more remote fetches.
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

# stb (public domain / MIT): image loading (PNG, JPEG, TGA, BMP, PSD, HDR), resizing and DXT
# compression, for the skybox editor. Pinned to the last commit before stb_image_resize2 moved to
# its own repository. Its implementations are compiled in src/core (vomit_tools_stb).
FetchContent_Declare(stb
        URL https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz
        URL_HASH SHA256=9a955b1b49a4410088a2e0ee2a9c057c3c907d0c1d75454144cb980aca0ba515
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        SOURCE_SUBDIR do-not-add-subdirectory)
FetchContent_MakeAvailable(stb)

# CoACD without OpenVDB preprocessing: closed meshes only. Keep its build isolated so its
# compiler flags, shared library and install rules do not change our packages.
FetchContent_Declare(coacd
        URL https://api.github.com/repos/SarahWeiii/CoACD/tarball/131010c4f03fec375ea88c5c6263f120046cb7f1
        URL_HASH SHA256=cf7b56ba3669c1467f9fb3ad24c262c6e02e2cc7e84c24720fcd6d398fef2e60
        DOWNLOAD_EXTRACT_TIMESTAMP ON SOURCE_SUBDIR do-not-add-subdirectory)
FetchContent_Declare(coacd_cdt
        URL https://api.github.com/repos/artem-ogre/CDT/tarball/ec03b309fd18102ab1da069f2edf3b37be5d1fb3
        URL_HASH SHA256=bb8726bac0860474daf6c56ac62eeefbadc304a5e2cf9df3a0fd836d80271ac5
        DOWNLOAD_EXTRACT_TIMESTAMP ON SOURCE_SUBDIR do-not-add-subdirectory)
FetchContent_Declare(coacd_spdlog
        URL https://api.github.com/repos/gabime/spdlog/tarball/v1.15.3
        URL_HASH SHA256=2f33b90dee78e9a910abbba1a294398277d4608831c6b8161ac74a7ca5493b49
        DOWNLOAD_EXTRACT_TIMESTAMP ON SOURCE_SUBDIR do-not-add-subdirectory)
FetchContent_MakeAvailable(coacd coacd_cdt coacd_spdlog)
set(_coacd_sources public/coacd.cpp src/bvh.cpp src/clip.cpp src/cost.cpp src/io.cpp
        src/logger.cpp src/mcts.cpp src/model_obj.cpp src/process.cpp src/shape.cpp src/sobol.cpp
        src/quickhull/QuickHull.cpp src/btConvexHull/btConvexHullComputer.cpp
        src/btConvexHull/btAlignedAllocator.cpp)
list(TRANSFORM _coacd_sources PREPEND "${coacd_SOURCE_DIR}/")
add_library(vomit_tools_coacd STATIC ${_coacd_sources})
target_include_directories(vomit_tools_coacd SYSTEM PUBLIC "${coacd_SOURCE_DIR}/public")
target_include_directories(vomit_tools_coacd SYSTEM PRIVATE "${coacd_cdt_SOURCE_DIR}/CDT/include"
        "${coacd_spdlog_SOURCE_DIR}/include")
target_compile_definitions(vomit_tools_coacd PUBLIC COACD_STATIC PRIVATE WITH_3RD_PARTY_LIBS=0
        SPDLOG_HEADER_ONLY _USE_MATH_DEFINES)
target_link_libraries(vomit_tools_coacd PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(vomit_tools_coacd PRIVATE /bigobj /utf-8)
    target_compile_definitions(vomit_tools_coacd PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# A pinned voxel fallback for non-manifold render meshes. Only the single-header library is used.
FetchContent_Declare(vhacd
        URL https://api.github.com/repos/kmammou/v-hacd/tarball/f900e42361491f525262d4825e758845e4969897
        URL_HASH SHA256=1234f1fd6ea9b8cfcafb2a57774b1e81af7e534de4b6c8c9f86f78d01b84f06a
        DOWNLOAD_EXTRACT_TIMESTAMP ON SOURCE_SUBDIR do-not-add-subdirectory)
FetchContent_MakeAvailable(vhacd)
add_library(vomit_tools_vhacd STATIC "${PROJECT_SOURCE_DIR}/src/core/model/VhacdImplementation.cpp")
target_include_directories(vomit_tools_vhacd SYSTEM PUBLIC "${vhacd_SOURCE_DIR}/include")
target_link_libraries(vomit_tools_vhacd PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(vomit_tools_vhacd PRIVATE /bigobj /utf-8)
    target_compile_definitions(vomit_tools_vhacd PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
