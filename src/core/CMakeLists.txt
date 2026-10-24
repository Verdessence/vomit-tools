add_library(vomit_tools_core STATIC
        audio/Audio.cpp
        audio/Audio.h
        dmx/Dmx.cpp
        dmx/Dmx.h
        kv1/Kv1.cpp
        kv1/Kv1.h
        material/Material.cpp
        material/Material.h
        material/Preview.cpp
        material/Preview.h
        model/Model.cpp
        model/Model.h
        particles/Builtin.cpp
        particles/Builtin.h
        particles/Names.cpp
        particles/Names.h
        particles/Particles.cpp
        particles/Particles.h
        particles/Render.cpp
        particles/Render.h
        particles/Simulation.cpp
        particles/Simulation.h
        sky/Sky.cpp
        sky/Sky.h
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
        texture/Build.cpp
        texture/Build.h
        texture/Image.cpp
        texture/Image.h
        texture/Sheet.cpp
        texture/Sheet.h
        texture/Vtf.cpp
        texture/Vtf.h
        util/Numbers.h
        util/Vec3.h
        vfs/Steam.cpp
        vfs/Steam.h
        vfs/Vfs.cpp
        vfs/Vfs.h)
add_library(vomit_tools::core ALIAS vomit_tools_core)

target_include_directories(vomit_tools_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vomit_tools_core PRIVATE sourcepp::vpkpp vomit_tools_miniaudio vomit_tools_stb Threads::Threads)
vomit_tools_set_warnings(vomit_tools_core)

# stb's implementations (stb is fetched in cmake/Dependencies.cmake), built once and without our
# warning flags.
add_library(vomit_tools_stb STATIC texture/StbImplementation.c)
target_include_directories(vomit_tools_stb SYSTEM PUBLIC "${stb_SOURCE_DIR}")
