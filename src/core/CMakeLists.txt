add_library(vomit_tools_core STATIC
        audio/Audio.cpp
        audio/Audio.h
        dmx/Dmx.cpp
        dmx/Dmx.h
        kv1/Kv1.cpp
        kv1/Kv1.h
        sound/SoundConstants.cpp
        sound/SoundConstants.h
        sound/SoundLint.cpp
        sound/SoundLint.h
        sound/SoundPlayback.cpp
        sound/SoundPlayback.h
        sound/SoundSchema.cpp
        sound/SoundSchema.h
        sound/Wav.cpp
        sound/Wav.h
        util/Numbers.h
        vfs/Steam.cpp
        vfs/Steam.h
        vfs/Vfs.cpp
        vfs/Vfs.h)
add_library(vomit_tools::core ALIAS vomit_tools_core)

target_include_directories(vomit_tools_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vomit_tools_core PRIVATE sourcepp::vpkpp vomit_tools_miniaudio)
vomit_tools_set_warnings(vomit_tools_core)
