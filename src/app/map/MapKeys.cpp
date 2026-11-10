//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapKeys.h.
//
//===========================================================================//
#include "MapKeys.h"

#include "Preferences.h"

#include <algorithm>
#include <array>

#include <QCoreApplication>
#include <QKeyEvent>

namespace
{

constexpr unsigned ONLY_2D = MapKeyContextBit( MapKeyContext_t::MAP_KEY_CONTEXT_2D );
constexpr unsigned ONLY_3D = MapKeyContextBit( MapKeyContext_t::MAP_KEY_CONTEXT_3D );
constexpr unsigned BOTH = ONLY_2D | ONLY_3D;
constexpr unsigned BRUSH = MapKeyContextBit( MapKeyContext_t::MAP_KEY_CONTEXT_BRUSH );

using A = MapAction_t;

// Every action, in the order the keybind editor lists them. QT_TRANSLATE_NOOP keeps the names
// for the translation tools; MapActionName translates them.
const MapActionInfo_t ACTIONS[] = {
	{ A::MAP_ACTION_VIEW_TOP, "View_2D_X_Y", QT_TRANSLATE_NOOP( "MapKeys", "Top view (x/y)" ), QT_TRANSLATE_NOOP( "MapKeys", "View" ), BOTH },
	{ A::MAP_ACTION_VIEW_SIDE, "View_2D_X_Z", QT_TRANSLATE_NOOP( "MapKeys", "Side view (x/z)" ), "View", BOTH },
	{ A::MAP_ACTION_VIEW_FRONT, "View_2D_Y_Z", QT_TRANSLATE_NOOP( "MapKeys", "Front view (y/z)" ), "View", BOTH },
	{ A::MAP_ACTION_VIEW_TEXTURED, "View_3D_Textured", QT_TRANSLATE_NOOP( "MapKeys", "3D textured" ), "View", BOTH },
	{ A::MAP_ACTION_VIEW_TEXTURED_SHADED, "View_3D_Textured_Shaded", QT_TRANSLATE_NOOP( "MapKeys", "3D textured, shaded" ), "View", BOTH },
	{ A::MAP_ACTION_NEXT_PANE, "View_Next_Pane", QT_TRANSLATE_NOOP( "MapKeys", "Next pane" ), "View", BOTH },
	{ A::MAP_ACTION_PREVIOUS_PANE, "View_Prev_Pane", QT_TRANSLATE_NOOP( "MapKeys", "Previous pane" ), "View", BOTH },
	{ A::MAP_ACTION_MAXIMIZE, "View_Maximize_Pane", QT_TRANSLATE_NOOP( "MapKeys", "Maximize pane" ), "View", BOTH },
	{ A::MAP_ACTION_AUTOSIZE, "View_Autosize", QT_TRANSLATE_NOOP( "MapKeys", "Autosize panes" ), "View", BOTH },
	{ A::MAP_ACTION_CENTER_2D, "View_Center_2D", QT_TRANSLATE_NOOP( "MapKeys", "Center 2D views on the selection" ), "View", BOTH },
	{ A::MAP_ACTION_CENTER_3D, "View_Center_3D", QT_TRANSLATE_NOOP( "MapKeys", "Center 3D views on the selection" ), "View", BOTH },
	{ A::MAP_ACTION_GRID_SMALLER, "Map_Grid_Lower", QT_TRANSLATE_NOOP( "MapKeys", "Smaller grid" ), "View", BOTH },
	{ A::MAP_ACTION_GRID_LARGER, "Map_Grid_Higher", QT_TRANSLATE_NOOP( "MapKeys", "Larger grid" ), "View", BOTH },
	{ A::MAP_ACTION_TOGGLE_GRID, "View_Grid", QT_TRANSLATE_NOOP( "MapKeys", "Show the grid" ), "View", BOTH },
	{ A::MAP_ACTION_QUICK_HIDE, "View_Quick_Hide", QT_TRANSLATE_NOOP( "MapKeys", "Hide the selection" ), "View", BOTH },
	{ A::MAP_ACTION_QUICK_HIDE_OTHERS, "View_Quick_Hide_Unselected", QT_TRANSLATE_NOOP( "MapKeys", "Hide everything else" ), "View", BOTH },
	{ A::MAP_ACTION_QUICK_UNHIDE, "View_Quick_Unhide", QT_TRANSLATE_NOOP( "MapKeys", "Show what was hidden" ), "View", BOTH },
	{ A::MAP_ACTION_TOGGLE_HELPERS, "View_Toggle_Helpers", QT_TRANSLATE_NOOP( "MapKeys", "Show helpers" ), "View", BOTH },
	{ A::MAP_ACTION_TOGGLE_MODELS_2D, "View_Toggle_Models_2D", QT_TRANSLATE_NOOP( "MapKeys", "Show models in 2D" ), "View", BOTH },
	{ A::MAP_ACTION_TOGGLE_NAMES, "View_Toggle_Names", QT_TRANSLATE_NOOP( "MapKeys", "Show entity names" ), "View", BOTH },
	{ A::MAP_ACTION_TOGGLE_TOOL_TEXTURES, "View_Toggle_Tool_Textures", QT_TRANSLATE_NOOP( "MapKeys", "Show tool textures" ), "View", BOTH },
	{ A::MAP_ACTION_TOGGLE_NODRAW, "View_Toggle_Nodraw", QT_TRANSLATE_NOOP( "MapKeys", "Show nodraw" ), "View", BOTH },
	{ A::MAP_ACTION_TOGGLE_SKYBOX_3D, "View_Toggle_3D_Skybox", QT_TRANSLATE_NOOP( "MapKeys", "Show the 3D skybox" ), "View", BOTH },
	{ A::MAP_ACTION_TOGGLE_SKY_2D, "View_Toggle_2D_Sky", QT_TRANSLATE_NOOP( "MapKeys", "Show the 2D sky" ), "View", BOTH },
	{ A::MAP_ACTION_TOGGLE_ORIGIN_AXES, "View_Toggle_Origin_Axes", QT_TRANSLATE_NOOP( "MapKeys", "Show the axes at the origin" ), "View", BOTH },
	{ A::MAP_ACTION_TOGGLE_FOG, "View_Toggle_Fog", QT_TRANSLATE_NOOP( "MapKeys", "Show fog" ), "View", BOTH },
	{ A::MAP_ACTION_TOGGLE_WALKABLE, "View_Toggle_Walkable", QT_TRANSLATE_NOOP( "MapKeys", "Show walkable area" ), "View", BOTH },
	{ A::MAP_ACTION_TOGGLE_PARTICLES, "View_Toggle_Particles", QT_TRANSLATE_NOOP( "MapKeys", "Play particles" ), "View", BOTH },
	{ A::MAP_ACTION_TOGGLE_PARTICLES_SELECTED, "View_Toggle_Particles_Selected", QT_TRANSLATE_NOOP( "MapKeys", "Play only the selected particles" ), "View", BOTH },
	{ A::MAP_ACTION_TOGGLE_DETAIL_PROPS, "View_Toggle_Detail_Props", QT_TRANSLATE_NOOP( "MapKeys", "Show detail props" ), "View", BOTH },
	{ A::MAP_ACTION_GO_TO_BRUSH, "View_Goto_Brush", QT_TRANSLATE_NOOP( "MapKeys", "Go to brush number" ), "View", BOTH },

	{ A::MAP_ACTION_TOOL_SELECT, "Tools_Pointer", QT_TRANSLATE_NOOP( "MapKeys", "Selection tool" ), QT_TRANSLATE_NOOP( "MapKeys", "Tools" ), BOTH },
	{ A::MAP_ACTION_TOOL_MAGNIFY, "Tools_Magnify", QT_TRANSLATE_NOOP( "MapKeys", "Magnify" ), "Tools", BOTH },
	{ A::MAP_ACTION_TOOL_CAMERA, "Tools_Camera", QT_TRANSLATE_NOOP( "MapKeys", "Camera" ), "Tools", BOTH },
	{ A::MAP_ACTION_TOOL_ENTITY, "Tools_Entity", QT_TRANSLATE_NOOP( "MapKeys", "Entity tool" ), "Tools", BOTH },
	{ A::MAP_ACTION_TOOL_BLOCK, "Tools_Block", QT_TRANSLATE_NOOP( "MapKeys", "Block tool" ), "Tools", BOTH },
	{ A::MAP_ACTION_TOOL_POLYGON, "Tools_Polygon", QT_TRANSLATE_NOOP( "MapKeys", "Polygon tool" ), "Tools", BOTH },
	{ A::MAP_ACTION_TOOL_SPLINE, "Tools_Spline", QT_TRANSLATE_NOOP( "MapKeys", "Spline tool" ), "Tools", BOTH },
	{ A::MAP_ACTION_TOOL_TEXTURE, "Tools_Texture_Application", QT_TRANSLATE_NOOP( "MapKeys", "Texture application" ), "Tools", BOTH },
	{ A::MAP_ACTION_TOOL_OVERLAY, "Tools_Overlay", QT_TRANSLATE_NOOP( "MapKeys", "Overlay tool" ), "Tools", BOTH },
	{ A::MAP_ACTION_TOOL_DECAL, "Tools_Apply_Decals", QT_TRANSLATE_NOOP( "MapKeys", "Apply decals" ), "Tools", BOTH },
	{ A::MAP_ACTION_TOOL_CLIP, "Tools_Clipping", QT_TRANSLATE_NOOP( "MapKeys", "Clipping tool" ), "Tools", BOTH },
	{ A::MAP_ACTION_TOOL_VERTEX, "Tools_Morph", QT_TRANSLATE_NOOP( "MapKeys", "Vertex tool" ), "Tools", BOTH },
	{ A::MAP_ACTION_TOOL_BRUSH_EDIT, "Tools_Brush_Edit", QT_TRANSLATE_NOOP( "MapKeys", "Brush Edit" ), "Tools", BOTH },
	{ A::MAP_ACTION_TOOL_SPRINKLE, "Tools_Sprinkle", QT_TRANSLATE_NOOP( "MapKeys", "Sprinkle tool" ), "Tools", BOTH },
	{ A::MAP_ACTION_TOOL_CORDON, "Tools_Cordon", QT_TRANSLATE_NOOP( "MapKeys", "Edit cordon bounds" ), "Tools", BOTH },
	{ A::MAP_ACTION_TOGGLE_CORDON, "Map_Toggle_Cordon", QT_TRANSLATE_NOOP( "MapKeys", "Cordon on or off" ), "Tools", BOTH },
	{ A::MAP_ACTION_RUN_MAP, "File_Run_Map", QT_TRANSLATE_NOOP( "MapKeys", "Run map" ), "Tools", BOTH },
	{ A::MAP_ACTION_CHECK_PROBLEMS, "Map_Check_For_Problems", QT_TRANSLATE_NOOP( "MapKeys", "Check for problems" ), "Tools", BOTH },
	{ A::MAP_ACTION_ENTITY_REPORT, "Map_Entity_Report", QT_TRANSLATE_NOOP( "MapKeys", "Entity report" ), "Tools", BOTH },
	{ A::MAP_ACTION_FIND_ENTITIES, "Edit_Find_Entities", QT_TRANSLATE_NOOP( "MapKeys", "Find entities" ), "Tools", BOTH },
	{ A::MAP_ACTION_REPLACE_TEXTURES, "Edit_Replace", QT_TRANSLATE_NOOP( "MapKeys", "Replace textures" ), "Tools", BOTH },
	{ A::MAP_ACTION_CREATE_PREFAB, "Tools_Create_Prefab", QT_TRANSLATE_NOOP( "MapKeys", "Create prefab" ), "Tools", BOTH },

	{ A::MAP_ACTION_PROPERTIES, "Edit_Properties", QT_TRANSLATE_NOOP( "MapKeys", "Object properties" ), QT_TRANSLATE_NOOP( "MapKeys", "Edit" ), BOTH },
	{ A::MAP_ACTION_CLEAR_SELECTION, "Edit_Clear_Selection", QT_TRANSLATE_NOOP( "MapKeys", "Clear the selection" ), "Edit", BOTH },
	{ A::MAP_ACTION_SELECT_ALL, "Edit_Select_All", QT_TRANSLATE_NOOP( "MapKeys", "Select all" ), "Edit", BOTH },
	{ A::MAP_ACTION_COPY, "Edit_Copy", QT_TRANSLATE_NOOP( "MapKeys", "Copy" ), "Edit", BOTH },
	{ A::MAP_ACTION_CUT, "Edit_Cut", QT_TRANSLATE_NOOP( "MapKeys", "Cut" ), "Edit", BOTH },
	{ A::MAP_ACTION_PASTE, "Edit_Paste", QT_TRANSLATE_NOOP( "MapKeys", "Paste" ), "Edit", BOTH },
	{ A::MAP_ACTION_PASTE_SPECIAL, "Edit_Paste_Special", QT_TRANSLATE_NOOP( "MapKeys", "Paste special" ), "Edit", BOTH },
	{ A::MAP_ACTION_DELETE, "Edit_Delete", QT_TRANSLATE_NOOP( "MapKeys", "Delete" ), "Edit", BOTH },
	{ A::MAP_ACTION_APPLY_TEXTURE, "Edit_Apply_Texture", QT_TRANSLATE_NOOP( "MapKeys", "Apply the current texture" ), "Edit", BOTH },
	{ A::MAP_ACTION_CARVE, "Tools_Carve", QT_TRANSLATE_NOOP( "MapKeys", "Carve" ), "Edit", BOTH },
	{ A::MAP_ACTION_HOLLOW, "Tools_Hollow", QT_TRANSLATE_NOOP( "MapKeys", "Hollow" ), "Edit", BOTH },
	{ A::MAP_ACTION_MERGE_BRUSHES, "Tools_Merge", QT_TRANSLATE_NOOP( "MapKeys", "Merge brushes" ), "Edit", BOTH },
	{ A::MAP_ACTION_TIE_TO_ENTITY, "Edit_To_Entity", QT_TRANSLATE_NOOP( "MapKeys", "Tie to entity" ), "Edit", BOTH },
	{ A::MAP_ACTION_MOVE_TO_WORLD, "Edit_To_World", QT_TRANSLATE_NOOP( "MapKeys", "Move to world" ), "Edit", BOTH },
	{ A::MAP_ACTION_GROUP, "Tools_Group", QT_TRANSLATE_NOOP( "MapKeys", "Group" ), "Edit", BOTH },
	{ A::MAP_ACTION_UNGROUP, "Tools_Ungroup", QT_TRANSLATE_NOOP( "MapKeys", "Ungroup" ), "Edit", BOTH },
	{ A::MAP_ACTION_TOGGLE_GROUP_IGNORE, "Edit_Toggle_Group_Ignore", QT_TRANSLATE_NOOP( "MapKeys", "Groups or objects" ), "Edit", BOTH },
	{ A::MAP_ACTION_TRANSFORM, "Tools_Transform", QT_TRANSLATE_NOOP( "MapKeys", "Transform…" ), "Edit", BOTH },
	{ A::MAP_ACTION_FLIP_HORIZONTAL, "Edit_Flip_Horizontal", QT_TRANSLATE_NOOP( "MapKeys", "Flip horizontally" ), "Edit", BOTH },
	{ A::MAP_ACTION_FLIP_VERTICAL, "Edit_Flip_Vertical", QT_TRANSLATE_NOOP( "MapKeys", "Flip vertically" ), "Edit", BOTH },
	{ A::MAP_ACTION_TOGGLE_SNAP, "Map_Snap_To_Grid", QT_TRANSLATE_NOOP( "MapKeys", "Snap to the grid" ), "Edit", BOTH },
	{ A::MAP_ACTION_SNAP_SELECTION, "Tools_Snap_Selected_To_Grid", QT_TRANSLATE_NOOP( "MapKeys", "Snap the selection to the grid" ), "Edit", BOTH },
	{ A::MAP_ACTION_TOGGLE_TEXTURE_LOCK, "Tools_Texture_Lock", QT_TRANSLATE_NOOP( "MapKeys", "Texture lock" ), "Edit", BOTH },
	{ A::MAP_ACTION_TOGGLE_SCALE_LOCK, "Tools_Texture_Scale_Lock", QT_TRANSLATE_NOOP( "MapKeys", "Texture scale lock" ), "Edit", BOTH },
	{ A::MAP_ACTION_TOGGLE_PIVOT, "Tools_Toggle_Pivot", QT_TRANSLATE_NOOP( "MapKeys", "Pivot" ), "Edit", BOTH },
	{ A::MAP_ACTION_EDIT_MODE, "Edit_Handle_Mode", QT_TRANSLATE_NOOP( "MapKeys", "Resize, rotate or shear handles" ), "Edit", BOTH },
	{ A::MAP_ACTION_NEXT_HIT, "Edit_Next_Hit", QT_TRANSLATE_NOOP( "MapKeys", "Next object under the pointer" ), "Edit", BOTH },
	{ A::MAP_ACTION_PREVIOUS_HIT, "Edit_Previous_Hit", QT_TRANSLATE_NOOP( "MapKeys", "Previous object under the pointer" ), "Edit", BOTH },
	{ A::MAP_ACTION_ENTER, "Edit_Enter", QT_TRANSLATE_NOOP( "MapKeys", "Confirm (create, clip, select the box)" ), "Edit", BOTH },
	{ A::MAP_ACTION_ESCAPE, "Edit_Escape", QT_TRANSLATE_NOOP( "MapKeys", "Cancel" ), "Edit", BOTH },

	{ A::MAP_ACTION_NEXT_AXIS, "View_2D_Next_Axis", QT_TRANSLATE_NOOP( "MapKeys", "Next axis" ), QT_TRANSLATE_NOOP( "MapKeys", "2D views" ), ONLY_2D },
	{ A::MAP_ACTION_ZOOM_IN, "View_Zoom_In", QT_TRANSLATE_NOOP( "MapKeys", "Zoom in" ), "2D views", ONLY_2D },
	{ A::MAP_ACTION_ZOOM_OUT, "View_Zoom_Out", QT_TRANSLATE_NOOP( "MapKeys", "Zoom out" ), "2D views", ONLY_2D },
	{ A::MAP_ACTION_ZOOM_IN_ALL, "View_Zoom_In_All", QT_TRANSLATE_NOOP( "MapKeys", "Zoom in, all 2D views" ), "2D views", ONLY_2D },
	{ A::MAP_ACTION_ZOOM_OUT_ALL, "View_Zoom_Out_All", QT_TRANSLATE_NOOP( "MapKeys", "Zoom out, all 2D views" ), "2D views", ONLY_2D },
	{ A::MAP_ACTION_SCROLL_UP, "View_Scroll_Up", QT_TRANSLATE_NOOP( "MapKeys", "Scroll or nudge up" ), "2D views", ONLY_2D },
	{ A::MAP_ACTION_SCROLL_DOWN, "View_Scroll_Down", QT_TRANSLATE_NOOP( "MapKeys", "Scroll or nudge down" ), "2D views", ONLY_2D },
	{ A::MAP_ACTION_SCROLL_LEFT, "View_Scroll_Left", QT_TRANSLATE_NOOP( "MapKeys", "Scroll or nudge left" ), "2D views", ONLY_2D },
	{ A::MAP_ACTION_SCROLL_RIGHT, "View_Scroll_Right", QT_TRANSLATE_NOOP( "MapKeys", "Scroll or nudge right" ), "2D views", ONLY_2D },
	{ A::MAP_ACTION_CLONE_UP, "Edit_Clone_Up", QT_TRANSLATE_NOOP( "MapKeys", "Copy a step up" ), "2D views", ONLY_2D },
	{ A::MAP_ACTION_CLONE_DOWN, "Edit_Clone_Down", QT_TRANSLATE_NOOP( "MapKeys", "Copy a step down" ), "2D views", ONLY_2D },
	{ A::MAP_ACTION_CLONE_LEFT, "Edit_Clone_Left", QT_TRANSLATE_NOOP( "MapKeys", "Copy a step left" ), "2D views", ONLY_2D },
	{ A::MAP_ACTION_CLONE_RIGHT, "Edit_Clone_Right", QT_TRANSLATE_NOOP( "MapKeys", "Copy a step right" ), "2D views", ONLY_2D },

	{ A::MAP_ACTION_MOUSELOOK, "View_Mouselook", QT_TRANSLATE_NOOP( "MapKeys", "Mouselook" ), QT_TRANSLATE_NOOP( "MapKeys", "3D views" ), ONLY_3D },
	{ A::MAP_ACTION_MOVE_FORWARD, "Camera_Forward", QT_TRANSLATE_NOOP( "MapKeys", "Forward" ), "3D views", ONLY_3D },
	{ A::MAP_ACTION_MOVE_BACK, "Camera_Back", QT_TRANSLATE_NOOP( "MapKeys", "Back" ), "3D views", ONLY_3D },
	{ A::MAP_ACTION_MOVE_LEFT, "Camera_Left", QT_TRANSLATE_NOOP( "MapKeys", "Left" ), "3D views", ONLY_3D },
	{ A::MAP_ACTION_MOVE_RIGHT, "Camera_Right", QT_TRANSLATE_NOOP( "MapKeys", "Right" ), "3D views", ONLY_3D },
	{ A::MAP_ACTION_STRAFE_UP, "Camera_Strafe_Up", QT_TRANSLATE_NOOP( "MapKeys", "Up" ), "3D views", ONLY_3D },
	{ A::MAP_ACTION_STRAFE_DOWN, "Camera_Strafe_Down", QT_TRANSLATE_NOOP( "MapKeys", "Down" ), "3D views", ONLY_3D },
	{ A::MAP_ACTION_STRAFE_LEFT, "Camera_Strafe_Left", QT_TRANSLATE_NOOP( "MapKeys", "Strafe left" ), "3D views", ONLY_3D },
	{ A::MAP_ACTION_STRAFE_RIGHT, "Camera_Strafe_Right", QT_TRANSLATE_NOOP( "MapKeys", "Strafe right" ), "3D views", ONLY_3D },
	{ A::MAP_ACTION_RENDER_CLOSER, "View_Render_Closer", QT_TRANSLATE_NOOP( "MapKeys", "Draw less far" ), "3D views", ONLY_3D },
	{ A::MAP_ACTION_RENDER_FURTHER, "View_Render_Further", QT_TRANSLATE_NOOP( "MapKeys", "Draw further" ), "3D views", ONLY_3D },
	{ A::MAP_ACTION_STATISTICS, "View_Statistics", QT_TRANSLATE_NOOP( "MapKeys", "Frame statistics" ), "3D views", ONLY_3D },
	{ A::MAP_ACTION_GIZMO_TRANSLATE, "Tools_Gizmo_Translate", QT_TRANSLATE_NOOP( "MapKeys", "Gizmo: move" ), "3D views", ONLY_3D },
	{ A::MAP_ACTION_GIZMO_ROTATE, "Tools_Gizmo_Rotate", QT_TRANSLATE_NOOP( "MapKeys", "Gizmo: rotate" ), "3D views", ONLY_3D },
	{ A::MAP_ACTION_GIZMO_ALL, "Tools_Gizmo_All", QT_TRANSLATE_NOOP( "MapKeys", "Gizmo: all" ), "3D views", ONLY_3D },
	{ A::MAP_ACTION_GIZMO_SCALE, "Tools_Gizmo_Scale", QT_TRANSLATE_NOOP( "MapKeys", "Gizmo: scale" ), "3D views", ONLY_3D },
	{ A::MAP_ACTION_GIZMO_ORIENTATION, "Tools_Toggle_Gizmo_Orientation", QT_TRANSLATE_NOOP( "MapKeys", "Gizmo: world or local axes" ), "3D views", ONLY_3D },

	{ A::MAP_ACTION_BRUSH_MOVE, "Brush_Move", QT_TRANSLATE_NOOP( "MapKeys", "Move" ), QT_TRANSLATE_NOOP( "MapKeys", "Brush Edit" ), BRUSH },
	{ A::MAP_ACTION_BRUSH_ROTATE, "Brush_Rotate", QT_TRANSLATE_NOOP( "MapKeys", "Rotate" ), "Brush Edit", BRUSH },
	{ A::MAP_ACTION_BRUSH_SCALE, "Brush_Scale", QT_TRANSLATE_NOOP( "MapKeys", "Scale" ), "Brush Edit", BRUSH },
	{ A::MAP_ACTION_BRUSH_EXTRUDE, "Brush_Extrude", QT_TRANSLATE_NOOP( "MapKeys", "Extrude" ), "Brush Edit", BRUSH },
	{ A::MAP_ACTION_BRUSH_INSET, "Brush_Inset", QT_TRANSLATE_NOOP( "MapKeys", "Inset" ), "Brush Edit", BRUSH },
	{ A::MAP_ACTION_BRUSH_BEVEL, "Brush_Bevel", QT_TRANSLATE_NOOP( "MapKeys", "Bevel" ), "Brush Edit", BRUSH },
	{ A::MAP_ACTION_BRUSH_LOOP_CUT, "Brush_Loop_Cut", QT_TRANSLATE_NOOP( "MapKeys", "Loop cut" ), "Brush Edit", BRUSH },
	{ A::MAP_ACTION_BRUSH_KNIFE, "Brush_Knife", QT_TRANSLATE_NOOP( "MapKeys", "Knife" ), "Brush Edit", BRUSH },
	{ A::MAP_ACTION_BRUSH_SPLIT_FACE, "Brush_Split_Face", QT_TRANSLATE_NOOP( "MapKeys", "Split face" ), "Brush Edit", BRUSH },
	{ A::MAP_ACTION_BRUSH_SCALE_VERTICES, "Brush_Scale_Vertices", QT_TRANSLATE_NOOP( "MapKeys", "Scale vertices" ), "Brush Edit", BRUSH },
	{ A::MAP_ACTION_BRUSH_PROPORTIONAL, "Brush_Proportional", QT_TRANSLATE_NOOP( "MapKeys", "Proportional editing" ), "Brush Edit", BRUSH },
	{ A::MAP_ACTION_BRUSH_SELECT_LINKED, "Brush_Select_Linked", QT_TRANSLATE_NOOP( "MapKeys", "Select the brush under the pointer" ), "Brush Edit", BRUSH },
	{ A::MAP_ACTION_BRUSH_DESELECT_LINKED, "Brush_Deselect_Linked", QT_TRANSLATE_NOOP( "MapKeys", "Deselect the brush under the pointer" ), "Brush Edit", BRUSH },
	{ A::MAP_ACTION_BRUSH_SELECT_ALL_LINKED, "Brush_Select_All_Linked", QT_TRANSLATE_NOOP( "MapKeys", "Select every brush a part is on" ), "Brush Edit", BRUSH },
	{ A::MAP_ACTION_BRUSH_SELECT_SIMILAR, "Brush_Select_Similar", QT_TRANSLATE_NOOP( "MapKeys", "Select similar" ), "Brush Edit", BRUSH },
	{ A::MAP_ACTION_BRUSH_GROW, "Brush_Grow", QT_TRANSLATE_NOOP( "MapKeys", "Grow the selection" ), "Brush Edit", BRUSH },
	{ A::MAP_ACTION_BRUSH_SHRINK, "Brush_Shrink", QT_TRANSLATE_NOOP( "MapKeys", "Shrink the selection" ), "Brush Edit", BRUSH },
	{ A::MAP_ACTION_BRUSH_MODE_VERTEX, "Brush_Mode_Vertex", QT_TRANSLATE_NOOP( "MapKeys", "Vertices" ), "Brush Edit", BRUSH },
	{ A::MAP_ACTION_BRUSH_MODE_EDGE, "Brush_Mode_Edge", QT_TRANSLATE_NOOP( "MapKeys", "Edges" ), "Brush Edit", BRUSH },
	{ A::MAP_ACTION_BRUSH_MODE_FACE, "Brush_Mode_Face", QT_TRANSLATE_NOOP( "MapKeys", "Faces" ), "Brush Edit", BRUSH },
	{ A::MAP_ACTION_BRUSH_LEAVE, "Brush_Leave", QT_TRANSLATE_NOOP( "MapKeys", "Leave Brush Edit" ), "Brush Edit", BRUSH },
};
static_assert( std::size( ACTIONS ) == static_cast<std::size_t>( MapAction_t::MAP_ACTION_COUNT ), "every action is listed" );

constexpr Qt::KeyboardModifiers SHIFT = Qt::ShiftModifier;
constexpr Qt::KeyboardModifiers CTRL = Qt::ControlModifier;
constexpr Qt::KeyboardModifiers ALT = Qt::AltModifier;
constexpr Qt::KeyboardModifiers NONE = Qt::NoModifier;

struct Default_t
{
	MapAction_t m_nAction;
	Qt::KeyboardModifiers m_Modifiers;
	Qt::Key m_nKey;
};

// Hammer++'s keys (hammerplusplus_keybindings.cfg), and Brush Edit's own.
const Default_t HAMMER_PLUS_PLUS[] = {
	{ A::MAP_ACTION_VIEW_TOP, NONE, Qt::Key_F2 },
	{ A::MAP_ACTION_VIEW_SIDE, NONE, Qt::Key_F3 },
	{ A::MAP_ACTION_VIEW_FRONT, NONE, Qt::Key_F4 },
	{ A::MAP_ACTION_VIEW_TEXTURED, NONE, Qt::Key_F5 },
	{ A::MAP_ACTION_VIEW_TEXTURED_SHADED, SHIFT, Qt::Key_F5 },
	{ A::MAP_ACTION_NEXT_PANE, NONE, Qt::Key_F7 },
	{ A::MAP_ACTION_PREVIOUS_PANE, SHIFT, Qt::Key_F7 },
	{ A::MAP_ACTION_GRID_SMALLER, NONE, Qt::Key_BracketLeft },
	{ A::MAP_ACTION_GRID_LARGER, NONE, Qt::Key_BracketRight },
	{ A::MAP_ACTION_TOGGLE_GRID, SHIFT, Qt::Key_R },
	{ A::MAP_ACTION_MAXIMIZE, SHIFT, Qt::Key_Z },
	{ A::MAP_ACTION_AUTOSIZE, CTRL, Qt::Key_A },
	{ A::MAP_ACTION_CENTER_2D, CTRL, Qt::Key_E },
	{ A::MAP_ACTION_CENTER_3D, CTRL | SHIFT, Qt::Key_E },
	{ A::MAP_ACTION_CLEAR_SELECTION, SHIFT, Qt::Key_Q },
	{ A::MAP_ACTION_QUICK_HIDE, NONE, Qt::Key_H },
	{ A::MAP_ACTION_QUICK_HIDE_OTHERS, CTRL, Qt::Key_H },
	{ A::MAP_ACTION_QUICK_UNHIDE, NONE, Qt::Key_U },
	{ A::MAP_ACTION_TOGGLE_HELPERS, CTRL | SHIFT, Qt::Key_H },
	{ A::MAP_ACTION_TOGGLE_MODELS_2D, SHIFT, Qt::Key_M },
	{ A::MAP_ACTION_TOGGLE_NAMES, CTRL | SHIFT, Qt::Key_N },
	{ A::MAP_ACTION_TOGGLE_TOOL_TEXTURES, CTRL | SHIFT, Qt::Key_F2 },
	{ A::MAP_ACTION_TOGGLE_SKYBOX_3D, ALT, Qt::Key_V },
	{ A::MAP_ACTION_PROPERTIES, ALT, Qt::Key_Return },
	{ A::MAP_ACTION_GO_TO_BRUSH, CTRL | SHIFT, Qt::Key_G },
	{ A::MAP_ACTION_TOOL_SELECT, SHIFT, Qt::Key_S },
	{ A::MAP_ACTION_TOOL_MAGNIFY, SHIFT, Qt::Key_G },
	{ A::MAP_ACTION_TOOL_CAMERA, SHIFT, Qt::Key_C },
	{ A::MAP_ACTION_TOOL_ENTITY, SHIFT, Qt::Key_E },
	{ A::MAP_ACTION_TOOL_BLOCK, SHIFT, Qt::Key_B },
	{ A::MAP_ACTION_CREATE_PREFAB, CTRL, Qt::Key_R },
	{ A::MAP_ACTION_TOOL_OVERLAY, SHIFT, Qt::Key_O },
	{ A::MAP_ACTION_TOOL_DECAL, SHIFT, Qt::Key_D },
	{ A::MAP_ACTION_TOOL_TEXTURE, SHIFT, Qt::Key_A },
	{ A::MAP_ACTION_RUN_MAP, NONE, Qt::Key_F9 },
	{ A::MAP_ACTION_TOOL_CLIP, SHIFT, Qt::Key_X },
	{ A::MAP_ACTION_TOOL_VERTEX, SHIFT, Qt::Key_V },
	{ A::MAP_ACTION_TOOL_SPRINKLE, SHIFT, Qt::Key_F },
	{ A::MAP_ACTION_TOOL_POLYGON, SHIFT, Qt::Key_N },
	{ A::MAP_ACTION_MERGE_BRUSHES, CTRL | SHIFT, Qt::Key_M },
	{ A::MAP_ACTION_CARVE, CTRL | SHIFT, Qt::Key_C },
	{ A::MAP_ACTION_TIE_TO_ENTITY, CTRL, Qt::Key_T },
	{ A::MAP_ACTION_MOVE_TO_WORLD, CTRL | SHIFT, Qt::Key_W },
	{ A::MAP_ACTION_GROUP, CTRL, Qt::Key_G },
	{ A::MAP_ACTION_UNGROUP, CTRL, Qt::Key_U },
	{ A::MAP_ACTION_TOGGLE_GROUP_IGNORE, CTRL, Qt::Key_W },
	{ A::MAP_ACTION_APPLY_TEXTURE, SHIFT, Qt::Key_T },
	{ A::MAP_ACTION_NEXT_HIT, NONE, Qt::Key_PageDown },
	{ A::MAP_ACTION_PREVIOUS_HIT, NONE, Qt::Key_PageUp },
	{ A::MAP_ACTION_ENTER, NONE, Qt::Key_Return },
	{ A::MAP_ACTION_ESCAPE, NONE, Qt::Key_Escape },
	{ A::MAP_ACTION_DELETE, NONE, Qt::Key_Delete },
	{ A::MAP_ACTION_COPY, CTRL, Qt::Key_C },
	{ A::MAP_ACTION_CUT, CTRL, Qt::Key_X },
	{ A::MAP_ACTION_PASTE, CTRL, Qt::Key_V },
	{ A::MAP_ACTION_PASTE_SPECIAL, CTRL | SHIFT, Qt::Key_V },
	{ A::MAP_ACTION_REPLACE_TEXTURES, CTRL | SHIFT, Qt::Key_R },
	{ A::MAP_ACTION_FIND_ENTITIES, CTRL | SHIFT, Qt::Key_F },
	{ A::MAP_ACTION_CHECK_PROBLEMS, ALT, Qt::Key_P },
	{ A::MAP_ACTION_TRANSFORM, CTRL, Qt::Key_M },
	{ A::MAP_ACTION_FLIP_HORIZONTAL, CTRL, Qt::Key_L },
	{ A::MAP_ACTION_FLIP_VERTICAL, CTRL, Qt::Key_I },
	{ A::MAP_ACTION_SELECT_ALL, CTRL | SHIFT, Qt::Key_A },
	{ A::MAP_ACTION_TOGGLE_SNAP, SHIFT, Qt::Key_W },
	{ A::MAP_ACTION_SNAP_SELECTION, CTRL, Qt::Key_B },
	{ A::MAP_ACTION_TOGGLE_TEXTURE_LOCK, SHIFT, Qt::Key_L },
	{ A::MAP_ACTION_TOGGLE_PIVOT, CTRL, Qt::Key_P },
	{ A::MAP_ACTION_EDIT_MODE, NONE, Qt::Key_X },
	{ A::MAP_ACTION_NEXT_AXIS, NONE, Qt::Key_Tab },
	// Hammer's zoom keys are the keypad's; the main row's work too, for keyboards without one.
	{ A::MAP_ACTION_ZOOM_IN, NONE, Qt::Key_Plus },
	{ A::MAP_ACTION_ZOOM_IN, NONE, Qt::Key_Equal },
	{ A::MAP_ACTION_ZOOM_OUT, NONE, Qt::Key_Minus },
	{ A::MAP_ACTION_ZOOM_IN_ALL, CTRL, Qt::Key_Plus },
	{ A::MAP_ACTION_ZOOM_IN_ALL, CTRL, Qt::Key_Equal },
	{ A::MAP_ACTION_ZOOM_OUT_ALL, CTRL, Qt::Key_Minus },
	{ A::MAP_ACTION_SCROLL_UP, NONE, Qt::Key_Up },
	{ A::MAP_ACTION_SCROLL_DOWN, NONE, Qt::Key_Down },
	{ A::MAP_ACTION_SCROLL_LEFT, NONE, Qt::Key_Left },
	{ A::MAP_ACTION_SCROLL_RIGHT, NONE, Qt::Key_Right },
	{ A::MAP_ACTION_CLONE_UP, SHIFT, Qt::Key_Up },
	{ A::MAP_ACTION_CLONE_DOWN, SHIFT, Qt::Key_Down },
	{ A::MAP_ACTION_CLONE_LEFT, SHIFT, Qt::Key_Left },
	{ A::MAP_ACTION_CLONE_RIGHT, SHIFT, Qt::Key_Right },
	{ A::MAP_ACTION_MOUSELOOK, NONE, Qt::Key_Z },
	{ A::MAP_ACTION_MOVE_FORWARD, NONE, Qt::Key_W },
	{ A::MAP_ACTION_MOVE_BACK, NONE, Qt::Key_S },
	{ A::MAP_ACTION_MOVE_LEFT, NONE, Qt::Key_A },
	{ A::MAP_ACTION_MOVE_RIGHT, NONE, Qt::Key_D },
	{ A::MAP_ACTION_STRAFE_UP, NONE, Qt::Key_Up },
	{ A::MAP_ACTION_STRAFE_DOWN, NONE, Qt::Key_Down },
	{ A::MAP_ACTION_STRAFE_LEFT, NONE, Qt::Key_Left },
	{ A::MAP_ACTION_STRAFE_RIGHT, NONE, Qt::Key_Right },
	{ A::MAP_ACTION_RENDER_CLOSER, NONE, Qt::Key_1 },
	{ A::MAP_ACTION_RENDER_FURTHER, NONE, Qt::Key_2 },
	{ A::MAP_ACTION_STATISTICS, NONE, Qt::Key_O },
	{ A::MAP_ACTION_GIZMO_TRANSLATE, NONE, Qt::Key_G },
	{ A::MAP_ACTION_GIZMO_ROTATE, NONE, Qt::Key_R },
	{ A::MAP_ACTION_GIZMO_ALL, NONE, Qt::Key_T },
	{ A::MAP_ACTION_GIZMO_SCALE, NONE, Qt::Key_E },
	{ A::MAP_ACTION_GIZMO_ORIENTATION, NONE, Qt::Key_J },
	// Brush Edit: the gizmo's G/R/E, Hammer's Ctrl+F and Alt+E, and the few of Blender's
	// that no Hammer key means there.
	{ A::MAP_ACTION_BRUSH_MOVE, NONE, Qt::Key_G },
	{ A::MAP_ACTION_BRUSH_ROTATE, NONE, Qt::Key_R },
	{ A::MAP_ACTION_BRUSH_SCALE, NONE, Qt::Key_E },
	{ A::MAP_ACTION_BRUSH_EXTRUDE, SHIFT, Qt::Key_E },
	{ A::MAP_ACTION_BRUSH_INSET, NONE, Qt::Key_I },
	{ A::MAP_ACTION_BRUSH_KNIFE, NONE, Qt::Key_K },
	{ A::MAP_ACTION_BRUSH_SPLIT_FACE, CTRL, Qt::Key_F },
	{ A::MAP_ACTION_BRUSH_SCALE_VERTICES, ALT, Qt::Key_E },
	{ A::MAP_ACTION_BRUSH_PROPORTIONAL, NONE, Qt::Key_O },
	{ A::MAP_ACTION_BRUSH_SELECT_LINKED, NONE, Qt::Key_L },
	{ A::MAP_ACTION_BRUSH_DESELECT_LINKED, SHIFT, Qt::Key_L },
	{ A::MAP_ACTION_BRUSH_SELECT_ALL_LINKED, CTRL, Qt::Key_L },
	{ A::MAP_ACTION_BRUSH_SELECT_SIMILAR, SHIFT, Qt::Key_G },
	{ A::MAP_ACTION_BRUSH_GROW, CTRL | Qt::KeypadModifier, Qt::Key_Plus },
	{ A::MAP_ACTION_BRUSH_SHRINK, CTRL | Qt::KeypadModifier, Qt::Key_Minus },
	{ A::MAP_ACTION_BRUSH_LEAVE, NONE, Qt::Key_Tab },
};

// What Hammer++ added and Hammer doesn't have: the gizmo, the pivot, the sprinkle and polygon
// tools and Merge. Unconfirmed against Hammer itself (docs/plan/hammer-check.md).
constexpr MapAction_t HAMMER_PLUS_PLUS_ONLY[] = {
	A::MAP_ACTION_GIZMO_TRANSLATE,
	A::MAP_ACTION_GIZMO_ROTATE,
	A::MAP_ACTION_GIZMO_ALL,
	A::MAP_ACTION_GIZMO_SCALE,
	A::MAP_ACTION_GIZMO_ORIENTATION,
	A::MAP_ACTION_TOGGLE_PIVOT,
	A::MAP_ACTION_TOOL_SPRINKLE,
	A::MAP_ACTION_TOOL_POLYGON,
	A::MAP_ACTION_MERGE_BRUSHES,
};

// Blender's mesh editing keys, over Hammer++'s.
const Default_t BLENDER[] = {
	{ A::MAP_ACTION_BRUSH_SCALE, NONE, Qt::Key_S },
	{ A::MAP_ACTION_BRUSH_EXTRUDE, NONE, Qt::Key_E },
	{ A::MAP_ACTION_BRUSH_BEVEL, CTRL, Qt::Key_B },
	{ A::MAP_ACTION_BRUSH_LOOP_CUT, CTRL, Qt::Key_R },
	{ A::MAP_ACTION_BRUSH_MODE_VERTEX, NONE, Qt::Key_1 },
	{ A::MAP_ACTION_BRUSH_MODE_EDGE, NONE, Qt::Key_2 },
	{ A::MAP_ACTION_BRUSH_MODE_FACE, NONE, Qt::Key_3 },
};

constexpr Qt::KeyboardModifiers MODIFIERS = Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;

//-----------------------------------------------------------------------------
// Purpose: Enter is Return, and only the modifiers a binding can name are kept.
//-----------------------------------------------------------------------------
QKeyCombination Normalized( QKeyCombination key )
{
	const Qt::Key nKey = key.key() == Qt::Key_Enter ? Qt::Key_Return : key.key();
	return QKeyCombination{ key.keyboardModifiers() & ( MODIFIERS | Qt::KeypadModifier ), nKey };
}

//-----------------------------------------------------------------------------
// Purpose: Whether two bindings take the same press: one off the keypad takes the keypad's too.
//-----------------------------------------------------------------------------
bool SameKey( QKeyCombination a, QKeyCombination b )
{
	a = Normalized( a );
	b = Normalized( b );
	if ( a.key() != b.key() || ( a.keyboardModifiers() & MODIFIERS ) != ( b.keyboardModifiers() & MODIFIERS ) )
		return false;
	const bool bPadA = a.keyboardModifiers().testFlag( Qt::KeypadModifier ), bPadB = b.keyboardModifiers().testFlag( Qt::KeypadModifier );
	return bPadA == bPadB || !bPadA || !bPadB;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const MapActionInfo_t *FindAction( QStringView szId )
{
	for ( const MapActionInfo_t &info : ACTIONS )
	{
		if ( szId == QLatin1String( info.m_pszId ) )
			return &info;
	}
	return nullptr;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::span<const MapActionInfo_t> MapActions()
{
	return ACTIONS;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const MapActionInfo_t &MapActionInfo( MapAction_t nAction )
{
	// Looked up for every binding at each key press, so by the action's number.
	static const auto byAction = [] {
		std::array<const MapActionInfo_t *, static_cast<std::size_t>( MapAction_t::MAP_ACTION_COUNT )> table{};
		for ( const MapActionInfo_t &info : ACTIONS )
			table[ static_cast<std::size_t>( info.m_nAction ) ] = &info;
		return table;
	}();
	const auto nIndex = static_cast<std::size_t>( nAction );
	return nIndex < byAction.size() && byAction[ nIndex ] ? *byAction[ nIndex ] : ACTIONS[ 0 ];
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString MapActionName( MapAction_t nAction )
{
	return QCoreApplication::translate( "MapKeys", MapActionInfo( nAction ).m_pszName );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString MapKeyPresetName( MapKeyPreset_t nPreset )
{
	switch ( nPreset )
	{
		case MapKeyPreset_t::MAP_KEY_PRESET_HAMMER_PLUS_PLUS: return QCoreApplication::translate( "MapKeys", "Hammer++" );
		case MapKeyPreset_t::MAP_KEY_PRESET_HAMMER: return QCoreApplication::translate( "MapKeys", "Hammer" );
		case MapKeyPreset_t::MAP_KEY_PRESET_BLENDER: return QCoreApplication::translate( "MapKeys", "Blender" );
		default: return QCoreApplication::translate( "MapKeys", "My own" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Each preset is Hammer++'s with its differences; the user's own replaces the keys
//          of every action its list names.
//-----------------------------------------------------------------------------
std::vector<MapKeyBinding_t> MapPresetKeys( MapKeyPreset_t nPreset, const QStringList &custom )
{
	std::vector<MapKeyBinding_t> keys;
	for ( const Default_t &key : HAMMER_PLUS_PLUS )
		keys.push_back( { key.m_nAction, QKeyCombination{ key.m_Modifiers, key.m_nKey } } );
	const auto replace = [ &keys ]( const std::vector<MapKeyBinding_t> &over ) {
		for ( const MapKeyBinding_t &binding : over )
			std::erase_if( keys, [ & ]( const MapKeyBinding_t &key ) { return key.m_nAction == binding.m_nAction; } );
		for ( const MapKeyBinding_t &binding : over )
		{
			if ( binding.m_Key.key() != Qt::Key_unknown )
				keys.push_back( binding );
		}
	};
	if ( nPreset == MapKeyPreset_t::MAP_KEY_PRESET_HAMMER )
	{
		std::erase_if( keys, []( const MapKeyBinding_t &key ) { return std::ranges::find( HAMMER_PLUS_PLUS_ONLY, key.m_nAction ) != std::end( HAMMER_PLUS_PLUS_ONLY ); } );
	}
	else if ( nPreset == MapKeyPreset_t::MAP_KEY_PRESET_BLENDER )
	{
		std::vector<MapKeyBinding_t> over;
		for ( const Default_t &key : BLENDER )
			over.push_back( { key.m_nAction, QKeyCombination{ key.m_Modifiers, key.m_nKey } } );
		replace( over );
	}
	else if ( nPreset == MapKeyPreset_t::MAP_KEY_PRESET_CUSTOM )
	{
		replace( MapKeysFromSettings( custom ) );
	}
	return keys;
}

//-----------------------------------------------------------------------------
// Purpose: Every action is written, those without a key as "Id=", so a list read back means
//          the same keys whatever Hammer++'s are by then.
//-----------------------------------------------------------------------------
QStringList MapKeysToSettings( const std::vector<MapKeyBinding_t> &keys )
{
	QStringList settings;
	for ( const MapActionInfo_t &info : ACTIONS )
	{
		bool bAny = false;
		for ( const MapKeyBinding_t &binding : keys )
		{
			if ( binding.m_nAction == info.m_nAction )
			{
				settings.append( QStringLiteral( "%1=%2" ).arg( QLatin1String( info.m_pszId ), QKeySequence{ binding.m_Key }.toString( QKeySequence::PortableText ) ) );
				bAny = true;
			}
		}
		if ( !bAny )
			settings.append( QStringLiteral( "%1=" ).arg( QLatin1String( info.m_pszId ) ) );
	}
	return settings;
}

//-----------------------------------------------------------------------------
// Purpose: "Id=" is kept as a binding with no key, which MapPresetKeys reads as "none".
//          Names it doesn't know (from a later version) are skipped.
//-----------------------------------------------------------------------------
std::vector<MapKeyBinding_t> MapKeysFromSettings( const QStringList &custom )
{
	std::vector<MapKeyBinding_t> keys;
	for ( const QString &szEntry : custom )
	{
		const qsizetype nEquals = szEntry.indexOf( QLatin1Char( '=' ) );
		if ( nEquals < 0 )
			continue;
		const MapActionInfo_t *pInfo = FindAction( QStringView{ szEntry }.left( nEquals ) );
		if ( !pInfo )
			continue;
		const QKeySequence sequence = QKeySequence::fromString( szEntry.mid( nEquals + 1 ), QKeySequence::PortableText );
		keys.push_back( { pInfo->m_nAction, sequence.isEmpty() ? QKeyCombination{ Qt::Key_unknown } : sequence[ 0 ] } );
	}
	return keys;
}

//-----------------------------------------------------------------------------
// Purpose: Each pair once, the first action first as the list has them.
//-----------------------------------------------------------------------------
std::vector<MapKeyClash_t> FindMapKeyClashes( const std::vector<MapKeyBinding_t> &keys )
{
	std::vector<MapKeyClash_t> clashes;
	for ( std::size_t i = 0; i < keys.size(); i++ )
	{
		for ( std::size_t j = i + 1; j < keys.size(); j++ )
		{
			if ( keys[ i ].m_nAction == keys[ j ].m_nAction || !SameKey( keys[ i ].m_Key, keys[ j ].m_Key ) )
				continue;
			const unsigned nA = MapActionInfo( keys[ i ].m_nAction ).m_nContexts, nB = MapActionInfo( keys[ j ].m_nAction ).m_nContexts;
			if ( ( nA & nB ) != 0 )
				clashes.push_back( { keys[ i ].m_nAction, keys[ j ].m_nAction, keys[ i ].m_Key, false } );
			else if ( ( nA | nB ) & BRUSH )
				clashes.push_back( { keys[ i ].m_nAction, keys[ j ].m_nAction, keys[ i ].m_Key, true } );
		}
	}
	return clashes;
}

//-----------------------------------------------------------------------------
// Purpose: Built again when the map preferences change.
//-----------------------------------------------------------------------------
const std::vector<MapKeyBinding_t> &ActiveMapKeys()
{
	static std::vector<MapKeyBinding_t> keys;
	static std::uint64_t nRevision = ~std::uint64_t{ 0 };
	const CPreferences &preferences = CPreferences::Instance();
	if ( nRevision != preferences.GetMapRevision() )
	{
		const MapPreferences_t &map = preferences.GetMapPreferences();
		keys = MapPresetKeys( map.m_nKeyPreset, map.m_CustomKeys );
		nRevision = preferences.GetMapRevision();
	}
	return keys;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool MapKeyMatches( QKeyCombination key, const QKeyEvent &event )
{
	const QKeyCombination pressed = Normalized( QKeyCombination{ event.modifiers(), static_cast<Qt::Key>( event.key() ) } );
	key = Normalized( key );
	if ( key.key() != pressed.key() || ( key.keyboardModifiers() & MODIFIERS ) != ( pressed.keyboardModifiers() & MODIFIERS ) )
		return false;
	return !key.keyboardModifiers().testFlag( Qt::KeypadModifier ) || pressed.keyboardModifiers().testFlag( Qt::KeypadModifier );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<MapAction_t> MapActionFor( const QKeyEvent &event, MapKeyContext_t nContext )
{
	const unsigned nWanted = MapKeyContextBit( nContext );
	for ( const MapKeyBinding_t &binding : ActiveMapKeys() )
	{
		if ( ( MapActionInfo( binding.m_nAction ).m_nContexts & nWanted ) != 0 && MapKeyMatches( binding.m_Key, event ) )
		{
			return binding.m_nAction;
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QKeySequence MapActionKey( MapAction_t nAction )
{
	for ( const MapKeyBinding_t &binding : ActiveMapKeys() )
	{
		if ( binding.m_nAction == nAction )
		{
			return QKeySequence{ binding.m_Key };
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString MapKeyText( QKeyCombination key )
{
	return QKeySequence{ key }.toString( QKeySequence::NativeText );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString MapActionTip( const QString &szName, MapAction_t nAction )
{
	const QKeySequence key = MapActionKey( nAction );
	return key.isEmpty() ? szName : QStringLiteral( "%1 (%2)" ).arg( szName, key.toString( QKeySequence::NativeText ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsHeldMapAction( MapAction_t nAction )
{
	switch ( nAction )
	{
		case MapAction_t::MAP_ACTION_MOVE_FORWARD:
		case MapAction_t::MAP_ACTION_MOVE_BACK:
		case MapAction_t::MAP_ACTION_MOVE_LEFT:
		case MapAction_t::MAP_ACTION_MOVE_RIGHT:
		case MapAction_t::MAP_ACTION_STRAFE_UP:
		case MapAction_t::MAP_ACTION_STRAFE_DOWN:
		case MapAction_t::MAP_ACTION_STRAFE_LEFT:
		case MapAction_t::MAP_ACTION_STRAFE_RIGHT:
			return true;
		default:
			return false;
	}
}
