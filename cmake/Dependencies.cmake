include(FetchContent)

# Qt is only needed by the app and the tests; the core library is plain C++.
if(VOMIT_TOOLS_BUILD_APP OR VOMIT_TOOLS_BUILD_TESTS)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network Test)
    find_package(Qt6 QUIET OPTIONAL_COMPONENTS ShaderTools ShaderToolsPrivate GuiPrivate)
    # The desktop's file chooser portal is reached over D-Bus (Linux and other freedesktop systems).
    if(UNIX AND NOT APPLE)
        find_package(Qt6 QUIET OPTIONAL_COMPONENTS DBus)
        # Pinning the pointer for the map's mouselook needs the compositor's protocols, which Qt
        # only lets a program bind itself (QtWaylandClient and its scanner).
        find_package(Qt6 QUIET OPTIONAL_COMPONENTS WaylandClient)
        find_program(VOMIT_TOOLS_WAYLAND_SCANNER wayland-scanner)
    endif()
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

# libopus (BSD-3-Clause): decoding Steam voice in demos (Opus at 24 kHz). Xiph's release
# tarball, which carries the version file its CMake project reads; only the static decoder
# and encoder library is built, without its programs, tests or install rules.
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_DRED OFF CACHE BOOL "" FORCE)
set(OPUS_DEEP_PLC OFF CACHE BOOL "" FORCE)
set(OPUS_OSCE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(opus
        URL https://downloads.xiph.org/releases/opus/opus-1.5.2.tar.gz
        URL_HASH SHA256=65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        EXCLUDE_FROM_ALL)
block(SCOPE_FOR VARIABLES)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF)
    FetchContent_MakeAvailable(opus)
endblock()

# CELT 0.11.3 (BSD-2-Clause): decoding vaudio_celt voice in demos, the codec Source games use
# for voice besides Steam's. The version matters: CELT's bitstream changed from release to
# release, and only 0.11.3 (with the post-filter) decodes the game's frames as the game's own
# vaudio_celt does (every frame within 2 of 32768 of it; 0.11.0 to 0.11.2 match none). Not the
# CELT inside libopus either, whose bitstream is later still.
# Its decoder and encoder sources are built here (no tests or tools), every global name
# renamed by src/core/demo/CeltNames.h so they don't meet libopus's at link time.
FetchContent_Declare(celt
        URL https://downloads.xiph.org/releases/celt/celt-0.11.3.tar.gz
        URL_HASH SHA256=7e64815d4a8a009d0280ecd235ebd917da3abdcfd8f7d0812218c085f9480836
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        SOURCE_SUBDIR do-not-add-subdirectory)
FetchContent_MakeAvailable(celt)
set(_celt_sources bands.c celt.c cwrs.c entcode.c entdec.c entenc.c header.c kiss_fft.c laplace.c
        mathops.c mdct.c modes.c pitch.c plc.c quant_bands.c rate.c vq.c)
list(TRANSFORM _celt_sources PREPEND "${celt_SOURCE_DIR}/libcelt/")
add_library(vomit_tools_celt STATIC ${_celt_sources})
target_include_directories(vomit_tools_celt SYSTEM PUBLIC "${celt_SOURCE_DIR}/libcelt")
# Custom modes: Source's voice runs at 22050 Hz, not one of CELT's built-in rates. The pitch
# post-filter (configure's --enable-postfilter): Source's build has it; without it a third of
# the game's frames are refused and the rest decode differently.
target_compile_definitions(vomit_tools_celt PRIVATE CUSTOM_MODES ENABLE_POSTFILTER $<IF:$<C_COMPILER_ID:MSVC>,USE_ALLOCA,VAR_ARRAYS>)
target_compile_options(vomit_tools_celt PRIVATE
        $<IF:$<C_COMPILER_ID:MSVC>,/FI${PROJECT_SOURCE_DIR}/src/core/demo/CeltNames.h,-include${PROJECT_SOURCE_DIR}/src/core/demo/CeltNames.h>
        $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>)
set_target_properties(vomit_tools_celt PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(UNIX AND NOT APPLE)
    target_link_libraries(vomit_tools_celt PRIVATE m)
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

# tinyexr (BSD-3-Clause): reading OpenEXR panoramas for the sky editor's HDRI import. Only the
# release's single-header v1 API (tinyexr.h) is used, with stb's zlib in place of miniz; its v3
# C library (src/, deps/) is not built. The implementation is compiled in src/core
# (vomit_tools_tinyexr).
FetchContent_Declare(tinyexr
        URL https://github.com/syoyo/tinyexr/archive/refs/tags/v3.2.0.tar.gz
        URL_HASH SHA256=df2bd61124a35d8138f8b0bc22418a1d4fe33622c818e0e022f5522afc0821b0
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        SOURCE_SUBDIR do-not-add-subdirectory)
FetchContent_MakeAvailable(tinyexr)

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

# Box3D (v0.1.0), the map viewer's physics tool. Used through FetchContent, it builds the library
# alone (its samples, tests and validation stay with its own top-level build); SYSTEM keeps its
# headers' warnings out of ours.
FetchContent_Declare(box3d
        URL https://api.github.com/repos/erincatto/box3d/tarball/8441b4a06d6d09dcfb0b0f704df4d847d1437b92
        URL_HASH SHA256=188a106664038a7a207b9c558c9b02e4785cc9f914c6322a3f6131b5e1d4d73f
        DOWNLOAD_EXTRACT_TIMESTAMP ON SYSTEM)
FetchContent_MakeAvailable(box3d)

# JPEG XL picture caches use the public codec API directly, including on systems without a Qt
# image plugin. Prefer the system codec when it is recent enough to read the cache's metadata.
if(VOMIT_TOOLS_BUILD_APP)
    if(NOT WIN32)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(JXL QUIET IMPORTED_TARGET libjxl>=0.12)
        endif()
    endif()
    if(TARGET PkgConfig::JXL)
        set(VOMIT_TOOLS_JXL_TARGET PkgConfig::JXL)
    else()
        FetchContent_Declare(jpegxl
                GIT_REPOSITORY https://github.com/libjxl/libjxl.git
                GIT_TAG v0.12.0
                GIT_SHALLOW TRUE
                GIT_SUBMODULES third_party/brotli third_party/highway third_party/skcms
                GIT_SUBMODULES_RECURSE FALSE
                EXCLUDE_FROM_ALL)
        block(SCOPE_FOR VARIABLES)
            set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
            set(CMAKE_POLICY_DEFAULT_CMP0126 NEW)
            set(BUILD_SHARED_LIBS OFF)
            set(BUILD_TESTING OFF)
            set(JPEGXL_ENABLE_SKCMS ON)
            foreach(feature BENCHMARK DOXYGEN EXAMPLES FUZZERS JNI MANPAGES OPENEXR SJPEG TCMALLOC TOOLS TRANSCODE_JPEG)
                set(JPEGXL_ENABLE_${feature} OFF)
            endforeach()
            FetchContent_MakeAvailable(jpegxl)
        endblock()
        set(VOMIT_TOOLS_JXL_TARGET jxl)
    endif()
endif()

# AVIF pictures (the demo viewer's Save Image) through the system's libavif and its AV1
# encoders; without it (Windows), only through a Qt image plugin if one is installed.
if(VOMIT_TOOLS_BUILD_APP AND NOT WIN32)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(AVIF QUIET IMPORTED_TARGET libavif>=1.0)
    endif()
    if(TARGET PkgConfig::AVIF)
        set(VOMIT_TOOLS_AVIF_TARGET PkgConfig::AVIF)
        message(STATUS "AVIF pictures: libavif ${AVIF_VERSION}")
    else()
        message(STATUS "AVIF pictures: only through a Qt image plugin (no libavif)")
    endif()
endif()
