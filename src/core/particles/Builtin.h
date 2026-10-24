//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The particle functions the game builds in, and every setting each one reads: its
//          name, its type and the value the game uses when a file leaves it out. They are the
//          tables of the Source SDK 2013's particle library (src/lib/public/linux64/particles.a):
//          facts about the format, read from its data, not its code. CS:S's client has every one
//          of them.
//
//===========================================================================//
#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "dmx/Dmx.h"
#include "particles/Particles.h"

namespace xv::particles
{

// What a setting holds, beyond its type.
enum class SettingHint_t
{
	SETTING_HINT_NONE,
	SETTING_HINT_SCALAR_FIELD, // a particle field holding one number
	SETTING_HINT_VECTOR_FIELD, // a particle field holding three
	SETTING_HINT_MATERIAL,
};

struct BuiltinSetting_t
{
	std::string_view m_szName;
	dmx::AttrType_t m_nType = dmx::AttrType_t::ATTR_TYPE_UNKNOWN;
	std::string_view m_szDefault; // as the game has it: ".25", "4.0f", "0 0 1", "255 255 255 255"
	SettingHint_t m_nHint = SettingHint_t::SETTING_HINT_NONE;

	// The default as a value of the setting's type, read the way the game reads it ("4.0f" is 4,
	// and "1.0" is 1 for a whole number).
	[[nodiscard]] dmx::Value_t Default() const;
};

struct BuiltinFunction_t
{
	Category_t m_nCategory = Category_t::CATEGORY_OPERATORS;
	std::string_view m_szName;
	std::span<const BuiltinSetting_t> m_Own; // the settings only this function has
	bool m_bVisibility = false;              // a renderer with the visibility settings

	// Every setting it reads, in the game's order: when it runs, then visibility (most
	// renderers), then its own.
	[[nodiscard]] std::vector<const BuiltinSetting_t *> Settings() const;
	// A setting by name, ignoring case; nullptr if the function doesn't read it.
	[[nodiscard]] const BuiltinSetting_t *Setting( std::string_view szName ) const;
};

// Every built-in function: category by category, each in the order the game registers them.
[[nodiscard]] std::span<const BuiltinFunction_t> BuiltinFunctions();
// A built-in function by name, ignoring case as the game does, older names included; nullptr for
// others.
[[nodiscard]] const BuiltinFunction_t *FindBuiltin( Category_t nCategory, std::string_view szName );
// The name a function is registered under. The game still takes some older names ("alpha_fade"
// for Alpha Fade and Decay) and renames them as it reads a file; other names come back as they
// are.
[[nodiscard]] std::string_view CurrentName( std::string_view szFunction );
// The system's own settings, without children and the six function lists.
[[nodiscard]] std::span<const BuiltinSetting_t> SystemSettings();
[[nodiscard]] const BuiltinSetting_t *SystemSetting( std::string_view szName );
// A child link's settings ("delay").
[[nodiscard]] std::span<const BuiltinSetting_t> ChildSettings();

// Whether a particle field holds three numbers (position, colour...) rather than one.
[[nodiscard]] bool IsVectorField( int nField );

} // namespace xv::particles
