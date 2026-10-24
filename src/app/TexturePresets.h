//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Saved choices for making textures (diffuse, normal map, UI icon, sky face...): the
//          built-in ones, and the user's own, kept in the settings.
//
//===========================================================================//
#pragma once

#include <vector>

#include <QString>

#include "texture/Build.h"

struct TexturePreset_t
{
	QString m_szName;
	xv::texture::BuildOptions_t m_Options;
	bool m_bBuiltin = false;
};

// The built-in presets, then the user's.
[[nodiscard]] std::vector<TexturePreset_t> TexturePresets();
// Saves (or replaces, by name) one of the user's presets. Built-in names can't be replaced.
bool SaveTexturePreset( const QString &szName, const xv::texture::BuildOptions_t &options );
void DeleteTexturePreset( const QString &szName );
// The preset picked last, to start the next import with; empty for none.
[[nodiscard]] QString LastTexturePreset();
void SetLastTexturePreset( const QString &szName );
