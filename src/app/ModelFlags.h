//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Studio header switches shared by the model editor and browser details.
//===========================================================================//
#pragma once
#include <QCoreApplication>
struct ModelFlag_t
{
	int m_nBit;
	const char *m_pszName;
};
inline constexpr ModelFlag_t MODEL_FLAGS[] = {
	{ 0x00000010, QT_TRANSLATE_NOOP( "CModelEditor", "Static prop" ) },
	{ 0x00000004, QT_TRANSLATE_NOOP( "CModelEditor", "Force opaque" ) },
	{ 0x00000008, QT_TRANSLATE_NOOP( "CModelEditor", "Translucent, two passes" ) },
	{ 0x00000100, QT_TRANSLATE_NOOP( "CModelEditor", "Shadow detail level uses real materials" ) },
	{ 0x00000800, QT_TRANSLATE_NOOP( "CModelEditor", "No forced fade" ) },
	{ 0x00001000, QT_TRANSLATE_NOOP( "CModelEditor", "Force phoneme crossfade" ) },
	{ 0x00002000, QT_TRANSLATE_NOOP( "CModelEditor", "Constant directional light" ) },
	{ 0x00010000, QT_TRANSLATE_NOOP( "CModelEditor", "Ambient boost" ) },
	{ 0x00020000, QT_TRANSLATE_NOOP( "CModelEditor", "Doesn't cast shadows" ) },
	{ 0x00040000, QT_TRANSLATE_NOOP( "CModelEditor", "Casts texture shadows (props_static)" ) },
};
