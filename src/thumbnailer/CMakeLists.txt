# A KIO thumbnail plugin, so Dolphin and KDE's file dialogs show VTF thumbnails. Built when KDE
# Frameworks 6 KIO is found.
add_library(vomit_tools_thumbnail MODULE VtfThumbnailer.cpp)
set_target_properties(vomit_tools_thumbnail PROPERTIES AUTOMOC ON PREFIX "")
target_link_libraries(vomit_tools_thumbnail PRIVATE vomit_tools::core KF6::KIOGui KF6::CoreAddons Qt6::Gui)
vomit_tools_set_warnings(vomit_tools_thumbnail)
install(TARGETS vomit_tools_thumbnail LIBRARY DESTINATION ${VOMIT_TOOLS_QT_PLUGIN_DIR}/kf6/thumbcreator)
