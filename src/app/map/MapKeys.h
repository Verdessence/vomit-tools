//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The map views' keys: presets (Hammer++'s, from its hammerplusplus_keybindings.cfg;
//          Hammer's; Blender's for Brush Edit) or the user's own (Preferences > Keys). A focused
//          view takes these before the window's own shortcuts, so F5 means textured in a map
//          view and Reload Files everywhere else.
//
//===========================================================================//
#pragma once

#include <optional>
#include <span>
#include <vector>

#include <QKeySequence>
#include <QStringList>
#include <QString>

class QKeyEvent;

enum class MapAction_t
{
	MAP_ACTION_TOOL_BRUSH_EDIT,
	// Anywhere in the map's views.
	MAP_ACTION_VIEW_TOP,             // F2: 2D x/y
	MAP_ACTION_VIEW_SIDE,            // F3: 2D x/z
	MAP_ACTION_VIEW_FRONT,           // F4: 2D y/z
	MAP_ACTION_VIEW_TEXTURED,        // F5
	MAP_ACTION_VIEW_TEXTURED_SHADED, // Shift+F5
	MAP_ACTION_TOGGLE_ORTHOGRAPHIC,  // none (Blender's keypad 5): the 3D view's parallel projection
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
	MAP_ACTION_TOGGLE_NODRAW,        // the tool bar: tools/toolsnodraw's faces, apart from the other tool textures
	MAP_ACTION_TOGGLE_SKYBOX_3D,     // Alt+V
	MAP_ACTION_TOGGLE_SKY_2D,        // the tool bar: the 2D sky behind everything in the 3D views
	MAP_ACTION_TOGGLE_ORIGIN_AXES,   // the tool bar: the world's axes at 0 0 0 in the 3D views
	MAP_ACTION_TOGGLE_FOG,           // the tool bar: the map's fog in the textured 3D views
	MAP_ACTION_TOGGLE_WALKABLE,      // the tool bar: Hammer's Display walkable area, on the selected displacements
	MAP_ACTION_TOGGLE_PARTICLES,     // the tool bar: particle systems playing in the 3D views
	MAP_ACTION_TOGGLE_PARTICLES_SELECTED, // the tool bar: only the selected ones
	MAP_ACTION_TOGGLE_DETAIL_PROPS,  // the tool bar: the detail props vbsp would scatter
	MAP_ACTION_PROPERTIES,           // Alt+Enter
	MAP_ACTION_GO_TO_BRUSH,          // Ctrl+Shift+G
	MAP_ACTION_TOOL_SELECT,          // Shift+S
	MAP_ACTION_TOOL_MAGNIFY,         // Shift+G
	MAP_ACTION_TOOL_CAMERA,          // Shift+C
	MAP_ACTION_TOOL_ENTITY,          // Shift+E
	MAP_ACTION_TOOL_BLOCK,           // Shift+B
	MAP_ACTION_CREATE_PREFAB,        // Ctrl+R
	MAP_ACTION_TOOL_OVERLAY,         // Shift+O
	MAP_ACTION_TOOL_DECAL,           // Shift+D
	MAP_ACTION_TOOL_TEXTURE,         // Shift+A
	MAP_ACTION_RUN_MAP,              // F9
	MAP_ACTION_TOOL_CLIP,            // Shift+X (again: the next clip mode)
	MAP_ACTION_TOOL_CORDON,          // the Map menu and tool bar: Edit Cordon Bounds
	MAP_ACTION_TOOL_VERTEX,          // Shift+V (again: the next handle mode)
	MAP_ACTION_TOOL_SPRINKLE,        // Shift+F (Hammer++'s Tools_Sprinkle)
	MAP_ACTION_TOGGLE_CORDON,        // the Map menu: the cordon on or off
	MAP_ACTION_CARVE,                // Ctrl+Shift+C
	MAP_ACTION_HOLLOW,               // the Map menu
	MAP_ACTION_TIE_TO_ENTITY,        // Ctrl+T
	MAP_ACTION_MOVE_TO_WORLD,        // Ctrl+Shift+W
	MAP_ACTION_GROUP,                // Ctrl+G
	MAP_ACTION_UNGROUP,              // Ctrl+U
	MAP_ACTION_TOGGLE_GROUP_IGNORE,  // Ctrl+W: Groups and Objects selection modes
	MAP_ACTION_APPLY_TEXTURE,        // Shift+T: the current texture onto the selected faces or objects
	MAP_ACTION_NEXT_HIT,             // Page Down
	MAP_ACTION_PREVIOUS_HIT,         // Page Up
	MAP_ACTION_ENTER,                // Enter
	MAP_ACTION_ESCAPE,               // Esc
	MAP_ACTION_DELETE,               // Delete
	MAP_ACTION_COPY,                 // Ctrl+C
	MAP_ACTION_CUT,                  // Ctrl+X
	MAP_ACTION_PASTE,                // Ctrl+V
	MAP_ACTION_PASTE_SPECIAL,        // Ctrl+Shift+V
	MAP_ACTION_REPLACE_TEXTURES,     // Ctrl+Shift+R
	MAP_ACTION_FIND_ENTITIES,        // Ctrl+Shift+F
	MAP_ACTION_ENTITY_REPORT,        // the Map menu
	MAP_ACTION_CHECK_PROBLEMS,       // Alt+P
	MAP_ACTION_TRANSFORM,            // Ctrl+M: the Transform dialog
	MAP_ACTION_FLIP_HORIZONTAL,      // Ctrl+L: mirrored across the view's horizontal axis
	MAP_ACTION_FLIP_VERTICAL,        // Ctrl+I
	MAP_ACTION_SELECT_ALL,           // Ctrl+Shift+A
	MAP_ACTION_TOGGLE_SNAP,          // Shift+W
	MAP_ACTION_SNAP_SELECTION,       // Ctrl+B
	MAP_ACTION_TOGGLE_TEXTURE_LOCK,  // Shift+L
	MAP_ACTION_TOGGLE_SCALE_LOCK,    // the tool bar's button
	MAP_ACTION_TOGGLE_PIVOT,         // Ctrl+P: Hammer++'s pivot
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
	MAP_ACTION_GIZMO_ALL,            // T
	MAP_ACTION_GIZMO_SCALE,          // E
	MAP_ACTION_TOOL_POLYGON,
	MAP_ACTION_TOOL_SPLINE,          // no key by default: Hammer++ has no such tool
	MAP_ACTION_TOOL_PHYSICS,         // no key by default, as Hammer++'s has none
	MAP_ACTION_MERGE_BRUSHES,
	MAP_ACTION_GIZMO_ORIENTATION,    // J: the world's axes or the entity's own
	// Brush Edit's part modes (and the vertex tool, for split and scale). These come before
	// the views' keys while the tool is active, so O there is proportional editing.
	MAP_ACTION_BRUSH_MOVE,            // G
	MAP_ACTION_BRUSH_ROTATE,          // R
	MAP_ACTION_BRUSH_SCALE,           // E (Blender's S)
	MAP_ACTION_BRUSH_EXTRUDE,         // Shift+E (Blender's E)
	MAP_ACTION_BRUSH_INSET,           // I (again while it runs: each face or one region)
	MAP_ACTION_BRUSH_BEVEL,           // Blender's Ctrl+B
	MAP_ACTION_BRUSH_LOOP_CUT,        // Blender's Ctrl+R
	MAP_ACTION_BRUSH_KNIFE,           // K
	MAP_ACTION_BRUSH_SPLIT_FACE,      // Ctrl+F (Hammer's)
	MAP_ACTION_BRUSH_SCALE_VERTICES,  // Alt+E (Hammer's)
	MAP_ACTION_BRUSH_PROPORTIONAL,    // O
	MAP_ACTION_BRUSH_SELECT_LINKED,   // L: the brush under the pointer
	MAP_ACTION_BRUSH_DESELECT_LINKED, // Shift+L
	MAP_ACTION_BRUSH_SELECT_ALL_LINKED, // Ctrl+L: every brush a part is on
	MAP_ACTION_BRUSH_SELECT_SIMILAR,  // Shift+G
	MAP_ACTION_BRUSH_GROW,            // Ctrl+keypad +
	MAP_ACTION_BRUSH_SHRINK,          // Ctrl+keypad -
	MAP_ACTION_BRUSH_MODE_VERTEX,     // Blender's 1
	MAP_ACTION_BRUSH_MODE_EDGE,       // Blender's 2
	MAP_ACTION_BRUSH_MODE_FACE,       // Blender's 3
	MAP_ACTION_BRUSH_LEAVE,           // Tab (Escape too, always): back to the selection tool
	MAP_ACTION_COUNT
};

enum class MapKeyContext_t
{
	MAP_KEY_CONTEXT_2D,
	MAP_KEY_CONTEXT_3D,
	MAP_KEY_CONTEXT_BRUSH, // Brush Edit's part modes and the vertex tool, in either kind of view
};

// Where the keys come from (Preferences > Keys).
enum class MapKeyPreset_t
{
	MAP_KEY_PRESET_HAMMER_PLUS_PLUS,
	MAP_KEY_PRESET_HAMMER,
	MAP_KEY_PRESET_BLENDER,
	MAP_KEY_PRESET_CUSTOM, // the user's own, kept in the preferences
	MAP_KEY_PRESET_COUNT
};

// One action, for the keybind editor.
struct MapActionInfo_t
{
	MapAction_t m_nAction;
	const char *m_pszId;    // kept in the settings
	const char *m_pszName;  // shown (MapActionName translates it)
	const char *m_pszGroup;
	unsigned m_nContexts;   // MapKeyContextBit()s
};

struct MapKeyBinding_t
{
	MapAction_t m_nAction;
	QKeyCombination m_Key;

	friend bool operator==( const MapKeyBinding_t &, const MapKeyBinding_t & ) = default;
};

// Two actions on one key where both can be meant: a clash, or (m_bOverride) a Brush Edit key
// over a view's, which Brush Edit takes while it is active.
struct MapKeyClash_t
{
	MapAction_t m_nAction;
	MapAction_t m_nOther;
	QKeyCombination m_Key;
	bool m_bOverride;
};

[[nodiscard]] constexpr unsigned MapKeyContextBit( MapKeyContext_t nContext )
{
	return 1u << static_cast<unsigned>( nContext );
}
[[nodiscard]] std::span<const MapActionInfo_t> MapActions();
[[nodiscard]] const MapActionInfo_t &MapActionInfo( MapAction_t nAction );
[[nodiscard]] QString MapActionName( MapAction_t nAction );
[[nodiscard]] QString MapKeyPresetName( MapKeyPreset_t nPreset );
// A preset's keys, in the order they are matched; for the user's own, the stored list over
// Hammer++'s (an action it doesn't name keeps Hammer++'s keys).
[[nodiscard]] std::vector<MapKeyBinding_t> MapPresetKeys( MapKeyPreset_t nPreset, const QStringList &custom = {} );
// The user's own keys as stored ("Id=Ctrl+Shift+E", one entry a key, "Id=" for none), and back.
[[nodiscard]] QStringList MapKeysToSettings( const std::vector<MapKeyBinding_t> &keys );
[[nodiscard]] std::vector<MapKeyBinding_t> MapKeysFromSettings( const QStringList &custom );
[[nodiscard]] std::vector<MapKeyClash_t> FindMapKeyClashes( const std::vector<MapKeyBinding_t> &keys );
// The keys in use, from the preferences.
[[nodiscard]] const std::vector<MapKeyBinding_t> &ActiveMapKeys();
// Whether a key press is that combination (Enter is Return; a key bound off the keypad is
// either).
[[nodiscard]] bool MapKeyMatches( QKeyCombination key, const QKeyEvent &event );
// A key as the views' menus and tooltips write it; "name (key)" when it has one.
[[nodiscard]] QString MapKeyText( QKeyCombination key );
[[nodiscard]] QString MapActionTip( const QString &szName, MapAction_t nAction );

// The action a key press means in a view of that kind, if any. Space (pan or look) is held,
// not pressed, and the views handle it themselves.
[[nodiscard]] std::optional<MapAction_t> MapActionFor( const QKeyEvent &event, MapKeyContext_t nContext );
// The action's first key, for tooltips and menus.
[[nodiscard]] QKeySequence MapActionKey( MapAction_t nAction );
// Whether the action is held down (movement), rather than done once per press.
[[nodiscard]] bool IsHeldMapAction( MapAction_t nAction );
