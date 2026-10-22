# Widgets shared by the app and the GUI tests.
add_library(vomit_tools_ui STATIC
        FileViews.cpp
        FileViews.h
        FolderBrowser.cpp
        FolderBrowser.h
        KvCard.cpp
        KvCard.h
        MainWindow.cpp
        MainWindow.h
        QtPaths.h
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
target_link_libraries(vomit_tools_ui PUBLIC vomit_tools::core Qt6::Widgets)
vomit_tools_set_warnings(vomit_tools_ui)

qt_add_executable(vomit-tools
        main.cpp)
target_link_libraries(vomit-tools PRIVATE vomit_tools_ui)
target_compile_definitions(vomit-tools PRIVATE VOMIT_TOOLS_VERSION="${PROJECT_VERSION}")
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
# Copies the Qt libraries and plugins next to the executable on Windows and macOS.
qt_generate_deploy_app_script(TARGET vomit-tools OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${deploy_script})
