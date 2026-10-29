//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapKeys.h.
//
//===========================================================================//
#include "MapKeys.h"

#include <array>

#include <QKeyEvent>

namespace
{

constexpr unsigned BOTH = 3;
constexpr unsigned ONLY_2D = 1;
constexpr unsigned ONLY_3D = 2;

struct Binding_t
{
	MapAction_t m_nAction;
	int m_nKey;
	Qt::KeyboardModifiers m_Modifiers;
	unsigned m_nContexts;
};

const Binding_t BINDINGS[] = {
	{ MapAction_t::MAP_ACTION_VIEW_TOP, Qt::Key_F2, Qt::NoModifier, BOTH },
	{ MapAction_t::MAP_ACTION_VIEW_SIDE, Qt::Key_F3, Qt::NoModifier, BOTH },
	{ MapAction_t::MAP_ACTION_VIEW_FRONT, Qt::Key_F4, Qt::NoModifier, BOTH },
	{ MapAction_t::MAP_ACTION_VIEW_TEXTURED, Qt::Key_F5, Qt::NoModifier, BOTH },
	{ MapAction_t::MAP_ACTION_VIEW_TEXTURED_SHADED, Qt::Key_F5, Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_NEXT_PANE, Qt::Key_F7, Qt::NoModifier, BOTH },
	{ MapAction_t::MAP_ACTION_PREVIOUS_PANE, Qt::Key_F7, Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_GRID_SMALLER, Qt::Key_BracketLeft, Qt::NoModifier, BOTH },
	{ MapAction_t::MAP_ACTION_GRID_LARGER, Qt::Key_BracketRight, Qt::NoModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TOGGLE_GRID, Qt::Key_R, Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_MAXIMIZE, Qt::Key_Z, Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_AUTOSIZE, Qt::Key_A, Qt::ControlModifier, BOTH },
	{ MapAction_t::MAP_ACTION_CENTER_2D, Qt::Key_E, Qt::ControlModifier, BOTH },
	{ MapAction_t::MAP_ACTION_CENTER_3D, Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_CLEAR_SELECTION, Qt::Key_Q, Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_QUICK_HIDE, Qt::Key_H, Qt::NoModifier, BOTH },
	{ MapAction_t::MAP_ACTION_QUICK_HIDE_OTHERS, Qt::Key_H, Qt::ControlModifier, BOTH },
	{ MapAction_t::MAP_ACTION_QUICK_UNHIDE, Qt::Key_U, Qt::NoModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TOGGLE_HELPERS, Qt::Key_H, Qt::ControlModifier | Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TOGGLE_MODELS_2D, Qt::Key_M, Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TOGGLE_NAMES, Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TOGGLE_TOOL_TEXTURES, Qt::Key_F2, Qt::ControlModifier | Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TOGGLE_SKYBOX_3D, Qt::Key_V, Qt::AltModifier, BOTH },
	{ MapAction_t::MAP_ACTION_PROPERTIES, Qt::Key_Return, Qt::AltModifier, BOTH },
	{ MapAction_t::MAP_ACTION_PROPERTIES, Qt::Key_Enter, Qt::AltModifier, BOTH },
	{ MapAction_t::MAP_ACTION_GO_TO_BRUSH, Qt::Key_G, Qt::ControlModifier | Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TOOL_SELECT, Qt::Key_S, Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TOOL_MAGNIFY, Qt::Key_G, Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TOOL_CAMERA, Qt::Key_C, Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TOOL_ENTITY, Qt::Key_E, Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TOOL_BLOCK, Qt::Key_B, Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TOOL_OVERLAY, Qt::Key_O, Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TOOL_DECAL, Qt::Key_D, Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TOOL_TEXTURE, Qt::Key_A, Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_RUN_MAP, Qt::Key_F9, Qt::NoModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TOOL_CLIP, Qt::Key_X, Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TOOL_VERTEX, Qt::Key_V, Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_CARVE, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TIE_TO_ENTITY, Qt::Key_T, Qt::ControlModifier, BOTH },
	{ MapAction_t::MAP_ACTION_MOVE_TO_WORLD, Qt::Key_W, Qt::ControlModifier | Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_GROUP, Qt::Key_G, Qt::ControlModifier, BOTH },
	{ MapAction_t::MAP_ACTION_UNGROUP, Qt::Key_U, Qt::ControlModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TOGGLE_GROUP_IGNORE, Qt::Key_W, Qt::ControlModifier, BOTH },
	{ MapAction_t::MAP_ACTION_APPLY_TEXTURE, Qt::Key_T, Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_NEXT_HIT, Qt::Key_PageDown, Qt::NoModifier, BOTH },
	{ MapAction_t::MAP_ACTION_PREVIOUS_HIT, Qt::Key_PageUp, Qt::NoModifier, BOTH },
	{ MapAction_t::MAP_ACTION_ENTER, Qt::Key_Return, Qt::NoModifier, BOTH },
	{ MapAction_t::MAP_ACTION_ENTER, Qt::Key_Enter, Qt::NoModifier, BOTH },
	{ MapAction_t::MAP_ACTION_ESCAPE, Qt::Key_Escape, Qt::NoModifier, BOTH },
	{ MapAction_t::MAP_ACTION_DELETE, Qt::Key_Delete, Qt::NoModifier, BOTH },
	{ MapAction_t::MAP_ACTION_COPY, Qt::Key_C, Qt::ControlModifier, BOTH },
	{ MapAction_t::MAP_ACTION_CUT, Qt::Key_X, Qt::ControlModifier, BOTH },
	{ MapAction_t::MAP_ACTION_PASTE, Qt::Key_V, Qt::ControlModifier, BOTH },
	{ MapAction_t::MAP_ACTION_PASTE_SPECIAL, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_REPLACE_TEXTURES, Qt::Key_R, Qt::ControlModifier | Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_FIND_ENTITIES, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_CHECK_PROBLEMS, Qt::Key_P, Qt::AltModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TRANSFORM, Qt::Key_M, Qt::ControlModifier, BOTH },
	{ MapAction_t::MAP_ACTION_FLIP_HORIZONTAL, Qt::Key_L, Qt::ControlModifier, BOTH },
	{ MapAction_t::MAP_ACTION_FLIP_VERTICAL, Qt::Key_I, Qt::ControlModifier, BOTH },
	{ MapAction_t::MAP_ACTION_SELECT_ALL, Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TOGGLE_SNAP, Qt::Key_W, Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_SNAP_SELECTION, Qt::Key_B, Qt::ControlModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TOGGLE_TEXTURE_LOCK, Qt::Key_L, Qt::ShiftModifier, BOTH },
	{ MapAction_t::MAP_ACTION_TOGGLE_PIVOT, Qt::Key_P, Qt::ControlModifier, BOTH },
	{ MapAction_t::MAP_ACTION_EDIT_MODE, Qt::Key_X, Qt::NoModifier, BOTH },
	{ MapAction_t::MAP_ACTION_NEXT_AXIS, Qt::Key_Tab, Qt::NoModifier, ONLY_2D },
	// Hammer's zoom keys are the keypad's; the main row's work too, for keyboards without one.
	{ MapAction_t::MAP_ACTION_ZOOM_IN, Qt::Key_Plus, Qt::NoModifier, ONLY_2D },
	{ MapAction_t::MAP_ACTION_ZOOM_IN, Qt::Key_Equal, Qt::NoModifier, ONLY_2D },
	{ MapAction_t::MAP_ACTION_ZOOM_OUT, Qt::Key_Minus, Qt::NoModifier, ONLY_2D },
	{ MapAction_t::MAP_ACTION_ZOOM_IN_ALL, Qt::Key_Plus, Qt::ControlModifier, ONLY_2D },
	{ MapAction_t::MAP_ACTION_ZOOM_IN_ALL, Qt::Key_Equal, Qt::ControlModifier, ONLY_2D },
	{ MapAction_t::MAP_ACTION_ZOOM_OUT_ALL, Qt::Key_Minus, Qt::ControlModifier, ONLY_2D },
	{ MapAction_t::MAP_ACTION_SCROLL_UP, Qt::Key_Up, Qt::NoModifier, ONLY_2D },
	{ MapAction_t::MAP_ACTION_SCROLL_DOWN, Qt::Key_Down, Qt::NoModifier, ONLY_2D },
	{ MapAction_t::MAP_ACTION_SCROLL_LEFT, Qt::Key_Left, Qt::NoModifier, ONLY_2D },
	{ MapAction_t::MAP_ACTION_SCROLL_RIGHT, Qt::Key_Right, Qt::NoModifier, ONLY_2D },
	{ MapAction_t::MAP_ACTION_CLONE_UP, Qt::Key_Up, Qt::ShiftModifier, ONLY_2D },
	{ MapAction_t::MAP_ACTION_CLONE_DOWN, Qt::Key_Down, Qt::ShiftModifier, ONLY_2D },
	{ MapAction_t::MAP_ACTION_CLONE_LEFT, Qt::Key_Left, Qt::ShiftModifier, ONLY_2D },
	{ MapAction_t::MAP_ACTION_CLONE_RIGHT, Qt::Key_Right, Qt::ShiftModifier, ONLY_2D },
	{ MapAction_t::MAP_ACTION_MOUSELOOK, Qt::Key_Z, Qt::NoModifier, ONLY_3D },
	{ MapAction_t::MAP_ACTION_MOVE_FORWARD, Qt::Key_W, Qt::NoModifier, ONLY_3D },
	{ MapAction_t::MAP_ACTION_MOVE_BACK, Qt::Key_S, Qt::NoModifier, ONLY_3D },
	{ MapAction_t::MAP_ACTION_MOVE_LEFT, Qt::Key_A, Qt::NoModifier, ONLY_3D },
	{ MapAction_t::MAP_ACTION_MOVE_RIGHT, Qt::Key_D, Qt::NoModifier, ONLY_3D },
	{ MapAction_t::MAP_ACTION_STRAFE_UP, Qt::Key_Up, Qt::NoModifier, ONLY_3D },
	{ MapAction_t::MAP_ACTION_STRAFE_DOWN, Qt::Key_Down, Qt::NoModifier, ONLY_3D },
	{ MapAction_t::MAP_ACTION_STRAFE_LEFT, Qt::Key_Left, Qt::NoModifier, ONLY_3D },
	{ MapAction_t::MAP_ACTION_STRAFE_RIGHT, Qt::Key_Right, Qt::NoModifier, ONLY_3D },
	{ MapAction_t::MAP_ACTION_RENDER_CLOSER, Qt::Key_1, Qt::NoModifier, ONLY_3D },
	{ MapAction_t::MAP_ACTION_RENDER_FURTHER, Qt::Key_2, Qt::NoModifier, ONLY_3D },
	{ MapAction_t::MAP_ACTION_STATISTICS, Qt::Key_O, Qt::NoModifier, ONLY_3D },
	{ MapAction_t::MAP_ACTION_GIZMO_TRANSLATE, Qt::Key_G, Qt::NoModifier, ONLY_3D },
	{ MapAction_t::MAP_ACTION_GIZMO_ROTATE, Qt::Key_R, Qt::NoModifier, ONLY_3D },
	{ MapAction_t::MAP_ACTION_GIZMO_SCALE, Qt::Key_E, Qt::NoModifier, ONLY_3D },
	{ MapAction_t::MAP_ACTION_GIZMO_ORIENTATION, Qt::Key_J, Qt::NoModifier, ONLY_3D },
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<MapAction_t> MapActionFor( const QKeyEvent &event, MapKeyContext_t nContext )
{
	const unsigned nWanted = nContext == MapKeyContext_t::MAP_KEY_CONTEXT_2D ? ONLY_2D : ONLY_3D;
	const Qt::KeyboardModifiers modifiers = event.modifiers() & ( Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier );
	for ( const Binding_t &binding : BINDINGS )
	{
		if ( binding.m_nKey == event.key() && binding.m_Modifiers == modifiers && ( binding.m_nContexts & nWanted ) != 0 )
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
	for ( const Binding_t &binding : BINDINGS )
	{
		if ( binding.m_nAction == nAction )
		{
			return QKeySequence{ QKeyCombination{ binding.m_Modifiers, static_cast<Qt::Key>( binding.m_nKey ) } };
		}
	}
	return {};
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
