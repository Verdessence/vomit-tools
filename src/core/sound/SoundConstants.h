//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Named constants the engine accepts in soundscripts and soundscapes (Source 2013
//          branch).
//
//===========================================================================//
#pragma once

#include <optional>
#include <span>
#include <string_view>

namespace xv::sound
{

struct Constant_t
{
	std::string_view m_szName; // as written in scripts, e.g. "SNDLVL_GUNFIRE"
	double m_flValue;          // what the engine turns it into
	std::string_view m_szNote; // short description for tooltips
};

[[nodiscard]] std::span<const Constant_t> Channels();     // CHAN_*
[[nodiscard]] std::span<const Constant_t> Volumes();      // VOL_*
[[nodiscard]] std::span<const Constant_t> Pitches();      // PITCH_*
[[nodiscard]] std::span<const Constant_t> SoundLevels();  // SNDLVL_*
[[nodiscard]] std::span<const Constant_t> Attenuations(); // ATTN_*
[[nodiscard]] std::span<const Constant_t> DspPresets();   // soundscape "dsp" values, named like Hammer does

// Case-insensitive, since stock files mix "SNDLVL_100dB" and "SNDLVL_100DB".
[[nodiscard]] const Constant_t *FindConstant( std::span<const Constant_t> table, std::string_view szName );

// "SNDLVL_GUNFIRE", "SNDLVL_85dB" (any number) or a plain number, in decibels.
[[nodiscard]] std::optional<double> ParseSoundLevel( std::string_view szText );

// The engine's conversions between distance attenuation and sound level.
[[nodiscard]] double SoundLevelToAttenuation( double flSoundLevel );
[[nodiscard]] double AttenuationToSoundLevel( double flAttenuation );

// Sound paths may start with up to two flag characters ("*", "#", ")", ...). These are the
// ones the 2013 engine knows, with what they do.
struct PathFlag_t
{
	char m_chCharacter;
	std::string_view m_szMeaning;
};
[[nodiscard]] std::span<const PathFlag_t> PathFlags();

// "*)weapons/ak47/ak47-1.wav" -> "weapons/ak47/ak47-1.wav"
[[nodiscard]] std::string_view StripPathFlags( std::string_view szWave );

} // namespace xv::sound
