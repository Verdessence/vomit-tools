# vomit-tools-cli: the texture tools from the command line (core only, no Qt).
add_executable(vomit-tools-cli main.cpp ModelCommands.cpp)
target_link_libraries(vomit-tools-cli PRIVATE vomit_tools::core)
vomit_tools_set_warnings(vomit-tools-cli)
install(TARGETS vomit-tools-cli RUNTIME DESTINATION bin)

# Linux file managers that follow the freedesktop thumbnailer spec (GNOME Files, Thunar, Nemo,
# Caja...) show VTF thumbnails through it. The MIME type matches the one other tools register.
if(UNIX AND NOT APPLE)
    install(FILES data/vomit-tools-vtf.xml DESTINATION share/mime/packages)
    install(FILES data/vomit-tools.thumbnailer DESTINATION share/thumbnailers)
endif()
