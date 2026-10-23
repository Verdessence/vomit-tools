//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See SoundConstants.h.
//
//===========================================================================//
#include "SoundConstants.h"

#include <algorithm>
#include <array>

#include "../kv1/Kv1.h"
#include "../util/Numbers.h"

namespace xv::sound
{

namespace
{

constexpr std::array CHANNELS{
	Constant_t{ "CHAN_AUTO", 0, "Any free channel" },
	Constant_t{ "CHAN_WEAPON", 1, "Weapon sounds; a new one cuts off the previous" },
	Constant_t{ "CHAN_VOICE", 2, "Speech; a new line cuts off the previous" },
	Constant_t{ "CHAN_ITEM", 3, "Item pickups and use sounds" },
	Constant_t{ "CHAN_BODY", 4, "Footsteps, impacts and other body sounds" },
	Constant_t{ "CHAN_STREAM", 5, "Streamed from disk; for long sounds and music" },
	Constant_t{ "CHAN_STATIC", 6, "Static channel; never cut off by other sounds on the entity" },
};

constexpr std::array VOLUMES{
	Constant_t{ "VOL_NORM", 1.0, "Full volume (1.0)" },
};

constexpr std::array PITCHES{
	Constant_t{ "PITCH_NORM", 100, "Unchanged pitch (100)" },
	Constant_t{ "PITCH_LOW", 95, "Slightly lower (95)" },
	Constant_t{ "PITCH_HIGH", 120, "Higher (120)" },
};

constexpr std::array SOUND_LEVELS{
	Constant_t{ "SNDLVL_NONE", 0, "Heard everywhere at full volume; no falloff" },
	Constant_t{ "SNDLVL_20dB", 20, "Rustling leaves" },
	Constant_t{ "SNDLVL_25dB", 25, "Whispering" },
	Constant_t{ "SNDLVL_30dB", 30, "Library" },
	Constant_t{ "SNDLVL_35dB", 35, "" },
	Constant_t{ "SNDLVL_40dB", 40, "" },
	Constant_t{ "SNDLVL_45dB", 45, "Refrigerator" },
	Constant_t{ "SNDLVL_50dB", 50, "Average home" },
	Constant_t{ "SNDLVL_55dB", 55, "" },
	Constant_t{ "SNDLVL_IDLE", 60, "Same as 60 dB" },
	Constant_t{ "SNDLVL_60dB", 60, "Normal conversation" },
	Constant_t{ "SNDLVL_65dB", 65, "Washing machine" },
	Constant_t{ "SNDLVL_STATIC", 66, "Same as 66 dB" },
	Constant_t{ "SNDLVL_70dB", 70, "Vacuum cleaner" },
	Constant_t{ "SNDLVL_NORM", 75, "Default level" },
	Constant_t{ "SNDLVL_75dB", 75, "Busy traffic" },
	Constant_t{ "SNDLVL_80dB", 80, "Noisy restaurant" },
	Constant_t{ "SNDLVL_TALKING", 80, "Same as 80 dB (a stock manifest comment says 60)" },
	Constant_t{ "SNDLVL_85dB", 85, "Average factory" },
	Constant_t{ "SNDLVL_90dB", 90, "Passing motorcycle" },
	Constant_t{ "SNDLVL_95dB", 95, "" },
	Constant_t{ "SNDLVL_100dB", 100, "Subway train, jackhammer" },
	Constant_t{ "SNDLVL_105dB", 105, "Helicopter" },
	Constant_t{ "SNDLVL_110dB", 110, "Sandblasting" },
	Constant_t{ "SNDLVL_120dB", 120, "Car horn, propeller aircraft" },
	Constant_t{ "SNDLVL_130dB", 130, "Air raid siren" },
	Constant_t{ "SNDLVL_GUNFIRE", 140, "Gunshots" },
	Constant_t{ "SNDLVL_140dB", 140, "Jet engine" },
	Constant_t{ "SNDLVL_150dB", 150, "" },
	Constant_t{ "SNDLVL_180dB", 180, "Rocket launch" },
};

constexpr std::array ATTENUATIONS{
	Constant_t{ "ATTN_NONE", 0.0, "No falloff (SNDLVL_NONE)" },
	Constant_t{ "ATTN_NORM", 0.8, "About 75 dB" },
	Constant_t{ "ATTN_IDLE", 2.0, "About 60 dB" },
	Constant_t{ "ATTN_STATIC", 1.25, "About 66 dB" },
	Constant_t{ "ATTN_RICOCHET", 1.5, "About 65 dB" },
	Constant_t{ "ATTN_GUNFIRE", 0.27, "About 140 dB" },
};

constexpr std::array DSP_PRESETS{
	Constant_t{ "Normal (off)", 0, "No room effect" },
	Constant_t{ "Generic", 1, "" },
	Constant_t{ "Metal Small", 2, "" },
	Constant_t{ "Metal Medium", 3, "" },
	Constant_t{ "Metal Large", 4, "" },
	Constant_t{ "Tunnel Small", 5, "" },
	Constant_t{ "Tunnel Medium", 6, "" },
	Constant_t{ "Tunnel Large", 7, "" },
	Constant_t{ "Chamber Small", 8, "" },
	Constant_t{ "Chamber Medium", 9, "" },
	Constant_t{ "Chamber Large", 10, "" },
	Constant_t{ "Bright Small", 11, "" },
	Constant_t{ "Bright Medium", 12, "" },
	Constant_t{ "Bright Large", 13, "" },
	Constant_t{ "Water 1", 14, "" },
	Constant_t{ "Water 2", 15, "" },
	Constant_t{ "Water 3", 16, "" },
	Constant_t{ "Concrete Small", 17, "" },
	Constant_t{ "Concrete Medium", 18, "" },
	Constant_t{ "Concrete Large", 19, "" },
	Constant_t{ "Big 1", 20, "" },
	Constant_t{ "Big 2", 21, "" },
	Constant_t{ "Big 3", 22, "" },
	Constant_t{ "Cavern Small", 23, "" },
	Constant_t{ "Cavern Medium", 24, "" },
	Constant_t{ "Cavern Large", 25, "" },
	Constant_t{ "Weirdo 1", 26, "" },
	Constant_t{ "Weirdo 2", 27, "" },
	Constant_t{ "Weirdo 3", 28, "" },
};

constexpr std::array PATH_FLAGS{
	PathFlag_t{ '*', "Stream from disk" },
	PathFlag_t{ '#', "Dry mix: skip the room (DSP) effect" },
	PathFlag_t{ '@', "Omnidirectional" },
	PathFlag_t{ '>', "Doppler stereo: left is approaching, right is receding" },
	PathFlag_t{ '<', "Directional stereo: left faces front, right faces back" },
	PathFlag_t{ '^', "Distance variant stereo: left is near, right is far" },
	PathFlag_t{ ')', "Spatialized stereo" },
	PathFlag_t{ '}', "Fast, low-quality pitch shifting" },
	PathFlag_t{ '!', "Sentence name, not a file" },
	PathFlag_t{ '?', "Live voice data, not a file" },
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::span<const Constant_t> Channels()
{
	return CHANNELS;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::span<const Constant_t> Volumes()
{
	return VOLUMES;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::span<const Constant_t> Pitches()
{
	return PITCHES;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::span<const Constant_t> SoundLevels()
{
	return SOUND_LEVELS;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::span<const Constant_t> Attenuations()
{
	return ATTENUATIONS;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::span<const Constant_t> DspPresets()
{
	return DSP_PRESETS;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::span<const PathFlag_t> PathFlags()
{
	return PATH_FLAGS;
}

//-----------------------------------------------------------------------------
// Purpose: Case-insensitive, since stock files mix "SNDLVL_100dB" and "SNDLVL_100DB".
//-----------------------------------------------------------------------------
const Constant_t *FindConstant( std::span<const Constant_t> table, std::string_view szName )
{
	const auto it = std::ranges::find_if( table, [ szName ]( const Constant_t &c ) { return kv1::EqualsIgnoreCase( c.m_szName, szName ); } );
	return it == table.end() ? nullptr : &*it;
}

//-----------------------------------------------------------------------------
// Purpose: "SNDLVL_GUNFIRE", "SNDLVL_85dB" (any number) or a plain number, in decibels.
//-----------------------------------------------------------------------------
std::optional<double> ParseSoundLevel( std::string_view szText )
{
	if ( const Constant_t *pConstant = FindConstant( SoundLevels(), szText ) )
	{
		return pConstant->m_flValue;
	}
	constexpr std::string_view szPREFIX = "SNDLVL_";
	if ( szText.size() > szPREFIX.size() && kv1::EqualsIgnoreCase( szText.substr( 0, szPREFIX.size() ), szPREFIX ) )
	{
		szText.remove_prefix( szPREFIX.size() );
		if ( szText.size() > 2 && kv1::EqualsIgnoreCase( szText.substr( szText.size() - 2 ), "dB" ) )
		{
			szText.remove_suffix( 2 );
		}
		else
		{
			return std::nullopt;
		}
	}
	return ParseNumber<double>( szText );
}

//-----------------------------------------------------------------------------
// Purpose: The engine's conversions between distance attenuation and sound level.
//-----------------------------------------------------------------------------
double SoundLevelToAttenuation( double flSoundLevel )
{
	return flSoundLevel > 50 ? 20.0 / ( flSoundLevel - 50 ) : 4.0;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double AttenuationToSoundLevel( double flAttenuation )
{
	return flAttenuation > 0 ? 50 + 20 / flAttenuation : 0;
}

//-----------------------------------------------------------------------------
// Purpose: "*)weapons/ak47/ak47-1.wav" -> "weapons/ak47/ak47-1.wav"
//-----------------------------------------------------------------------------
std::string_view StripPathFlags( std::string_view szWave )
{
	for ( int i = 0; i < 2 && !szWave.empty(); i++ )
	{
		const bool bFlag = std::ranges::any_of( PATH_FLAGS, [ c = szWave.front() ]( const PathFlag_t &f ) { return f.m_chCharacter == c; } );
		if ( !bFlag )
		{
			break;
		}
		szWave.remove_prefix( 1 );
	}
	return szWave;
}

} // namespace xv::sound
