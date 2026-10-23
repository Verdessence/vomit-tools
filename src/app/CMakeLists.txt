# Widgets shared by the app and the GUI tests.
add_library(vomit_tools_ui STATIC
        AboutDialog.cpp
        AboutDialog.h
        BuildInfo.h
        ${CMAKE_CURRENT_BINARY_DIR}/BuildInfo.cpp
        FileViews.cpp
        FileViews.h
        FolderBrowser.cpp
        FolderBrowser.h
        ImageFiles.cpp
        ImageFiles.h
        KvCard.cpp
        KvCard.h
        MainWindow.cpp
        MainWindow.h
        ParticleDocument.cpp
        ParticleDocument.h
        ParticleEditor.cpp
        ParticleEditor.h
        ParticlePreview.cpp
        ParticlePreview.h
        ParticleSearch.cpp
        ParticleSearch.h
        QtPaths.h
        SkyDocument.cpp
        SkyDocument.h
        SkyEditor.cpp
        SkyEditor.h
        SkyTestPattern.cpp
        SkyTestPattern.h
        SkyView.cpp
        SkyView.h
        SoundDocument.cpp
        SoundDocument.h
        SoundEditor.cpp
        SoundEditor.h
        SoundIndex.cpp
        SoundIndex.h
        SoundPlayer.cpp
        SoundPlayer.h
        SoundValues.cpp
        SoundValues.h
        VfsModel.cpp
        VfsModel.h)
target_include_directories(vomit_tools_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# The version and `git describe` shown in Help → About. Regenerated at every build so the commit
# stays current; the file is only rewritten (and recompiled) when it changes.
add_custom_target(vomit_tools_build_info
        COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DVERSION=${PROJECT_VERSION}"
            "-DTEMPLATE=${CMAKE_CURRENT_SOURCE_DIR}/BuildInfo.cpp.in"
            "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/BuildInfo.cpp"
            -P "${PROJECT_SOURCE_DIR}/cmake/BuildInfo.cmake"
        BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/BuildInfo.cpp"
        VERBATIM)
add_dependencies(vomit_tools_ui vomit_tools_build_info)

# The particle preview draws on the GPU where Qt has QRhiWidget (6.7+) and the shader tools to build
# its shaders; otherwise it draws in software only.
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.7 AND TARGET Qt6::ShaderTools AND TARGET Qt6::GuiPrivate)
    target_sources(vomit_tools_ui PRIVATE ParticleGpuView.cpp ParticleGpuView.h)
    qt_add_shaders(vomit_tools_ui "vomit_tools_particle_shaders"
            PREFIX "/shaders"
            BASE shaders
            FILES shaders/particle.vert shaders/particle.frag)
    target_link_libraries(vomit_tools_ui PRIVATE Qt6::GuiPrivate)
    target_compile_definitions(vomit_tools_ui PRIVATE VOMIT_TOOLS_GPU_PREVIEW)
    message(STATUS "vomit-tools: particle preview on the GPU")
else()
    message(STATUS "vomit-tools: particle preview in software only (needs Qt 6.7+ with ShaderTools for the GPU)")
endif()
target_link_libraries(vomit_tools_ui PUBLIC vomit_tools::core Qt6::Widgets)
vomit_tools_set_warnings(vomit_tools_ui)

qt_add_executable(vomit-tools
        main.cpp)
target_link_libraries(vomit-tools PRIVATE vomit_tools_ui)
vomit_tools_set_warnings(vomit-tools)

if(WIN32)
    set_target_properties(vomit-tools PROPERTIES WIN32_EXECUTABLE ON)
    if(MSVC)
        target_sources(vomit-tools PRIVATE vomit-tools.manifest)
    else()
        target_sources(vomit-tools PRIVATE vomit-tools.rc)
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS vomit-tools
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# Copies the Qt libraries and plugins next to the executable on Windows and macOS. Linux builds
# use the system Qt (or linuxdeploy for the AppImage).
if(WIN32 OR APPLE)
    # Qt before 6.9 rejects a relative prefix (`cmake --install build --prefix dist`) when it
    # writes qt.conf, so make it absolute first.
    install(CODE [[cmake_path(ABSOLUTE_PATH CMAKE_INSTALL_PREFIX NORMALIZE)]])
    qt_generate_deploy_app_script(TARGET vomit-tools OUTPUT_SCRIPT deploy_script)
    install(SCRIPT ${deploy_script})
endif()
