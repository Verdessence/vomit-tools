//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The map views' keys, Hammer++'s defaults (from its hammerplusplus_keybindings.cfg).
//          A focused view takes these before the window's own shortcuts, so F5 means textured
//          in a map view and Reload Files everywhere else.
//
//===========================================================================//
#pragma once

#include <optional>

#include <QKeySequence>
#include <QString>

class QKeyEvent;

enum class MapAction_t
{
	// Anywhere in the map's views.
	MAP_ACTION_VIEW_TOP,             // F2: 2D x/y
	MAP_ACTION_VIEW_SIDE,            // F3: 2D x/z
	MAP_ACTION_VIEW_FRONT,           // F4: 2D y/z
	MAP_ACTION_VIEW_TEXTURED,        // F5
	MAP_ACTION_VIEW_TEXTURED_SHADED, // Shift+F5
	MAP_ACTION_NEXT_PANE,            // F7
	MAP_ACTION_PREVIOUS_PANE,        // Shift+F7
	MAP_ACTION_GRID_SMALLER,         // [
	MAP_ACTION_GRID_LARGER,          // ]
	MAP_ACTION_TOGGLE_GRID,          // Shift+R
	MAP_ACTION_MAXIMIZE,             // Shift+Z
	MAP_ACTION_AUTOSIZE,             // Ctrl+A
	MAP_ACTION_CENTER_2D,            // Ctrl+E
	MAP_ACTION_CENTER_3D,            // Ctrl+Shift+E
	MAP_ACTION_CLEAR_SELECTION,      // Shift+Q
	MAP_ACTION_QUICK_HIDE,           // H
	MAP_ACTION_QUICK_HIDE_OTHERS,    // Ctrl+H
	MAP_ACTION_QUICK_UNHIDE,         // U
	MAP_ACTION_TOGGLE_HELPERS,       // Ctrl+Shift+H
	MAP_ACTION_TOGGLE_MODELS_2D,     // Shift+M
	MAP_ACTION_TOGGLE_NAMES,         // Ctrl+Shift+N
	MAP_ACTION_TOGGLE_TOOL_TEXTURES, // Ctrl+Shift+F2
	MAP_ACTION_TOGGLE_SKYBOX_3D,     // Alt+V
	MAP_ACTION_PROPERTIES,           // Alt+Enter
	MAP_ACTION_GO_TO_BRUSH,          // Ctrl+Shift+G
	MAP_ACTION_TOOL_SELECT,          // Shift+S
	MAP_ACTION_TOOL_MAGNIFY,         // Shift+G
	MAP_ACTION_TOOL_CAMERA,          // Shift+C
	MAP_ACTION_TOOL_ENTITY,          // Shift+E
	MAP_ACTION_TOOL_BLOCK,           // Shift+B
	MAP_ACTION_NEXT_HIT,             // Page Down
	MAP_ACTION_PREVIOUS_HIT,         // Page Up
	MAP_ACTION_ENTER,                // Enter
	MAP_ACTION_ESCAPE,               // Esc
	MAP_ACTION_DELETE,               // Delete
	MAP_ACTION_COPY,                 // Ctrl+C
	MAP_ACTION_CUT,                  // Ctrl+X
	MAP_ACTION_PASTE,                // Ctrl+V
	MAP_ACTION_SELECT_ALL,           // Ctrl+Shift+A
	MAP_ACTION_TOGGLE_SNAP,          // Shift+W
	MAP_ACTION_SNAP_SELECTION,       // Ctrl+B
	MAP_ACTION_TOGGLE_TEXTURE_LOCK,  // Shift+L
	MAP_ACTION_TOGGLE_SCALE_LOCK,    // the tool bar's button
	MAP_ACTION_EDIT_MODE,            // X: the selection's handles resize, rotate or shear
	// 2D views.
	MAP_ACTION_NEXT_AXIS,            // Tab
	MAP_ACTION_ZOOM_IN,              // keypad +
	MAP_ACTION_ZOOM_OUT,             // keypad -
	MAP_ACTION_ZOOM_IN_ALL,          // Ctrl+keypad +
	MAP_ACTION_ZOOM_OUT_ALL,         // Ctrl+keypad -
	MAP_ACTION_SCROLL_UP,            // arrows: they nudge the selection when there is one
	MAP_ACTION_SCROLL_DOWN,
	MAP_ACTION_SCROLL_LEFT,
	MAP_ACTION_SCROLL_RIGHT,
	MAP_ACTION_CLONE_UP,             // Shift+arrows: a copy a grid step along
	MAP_ACTION_CLONE_DOWN,
	MAP_ACTION_CLONE_LEFT,
	MAP_ACTION_CLONE_RIGHT,
	// 3D views.
	MAP_ACTION_MOUSELOOK,            // Z
	MAP_ACTION_MOVE_FORWARD,         // W
	MAP_ACTION_MOVE_BACK,            // S
	MAP_ACTION_MOVE_LEFT,            // A
	MAP_ACTION_MOVE_RIGHT,           // D
	MAP_ACTION_STRAFE_UP,            // Up
	MAP_ACTION_STRAFE_DOWN,          // Down
	MAP_ACTION_STRAFE_LEFT,          // Left
	MAP_ACTION_STRAFE_RIGHT,         // Right
	MAP_ACTION_RENDER_CLOSER,        // 1
	MAP_ACTION_RENDER_FURTHER,       // 2
	MAP_ACTION_STATISTICS,           // O
	MAP_ACTION_GIZMO_TRANSLATE,      // G (Hammer++'s gizmo)
	MAP_ACTION_GIZMO_ROTATE,         // R
	MAP_ACTION_GIZMO_SCALE,          // E
	MAP_ACTION_GIZMO_ORIENTATION,    // J: the world's axes or the entity's own
};

enum class MapKeyContext_t
{
	MAP_KEY_CONTEXT_2D,
	MAP_KEY_CONTEXT_3D,
};

// The action a key press means in a view of that kind, if any. Space (pan or look) is held,
// not pressed, and the views handle it themselves.
[[nodiscard]] std::optional<MapAction_t> MapActionFor( const QKeyEvent &event, MapKeyContext_t nContext );
// The default key, for tooltips and menus.
[[nodiscard]] QKeySequence MapActionKey( MapAction_t nAction );
// Whether the action is held down (movement), rather than done once per press.
[[nodiscard]] bool IsHeldMapAction( MapAction_t nAction );
