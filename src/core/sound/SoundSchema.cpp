//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See SoundSchema.h.
//
//===========================================================================//
#include "SoundSchema.h"

#include <algorithm>
#include <array>

#include "../kv1/Kv1.h"
#include "SoundLint.h"

namespace xv::sound
{

namespace
{

// ---- Soundscapes ---------------------------------------------------------------------------
// Volume and pitch are plain numbers here: stock soundscapes never use VOL_/PITCH_ names,
// unlike soundscripts, while they do use SNDLVL_ and ATTN_ names.

constexpr std::array SOUNDSCAPE_KEYS{
	KeyDef_t{ "dsp", ValueKind_t::VALUE_KIND_DSP_PRESET, "Room effect applied while this soundscape is active" },
	KeyDef_t{ "dsp_volume", ValueKind_t::VALUE_KIND_NUMBER, "Strength of the room effect (newer branches; verify the 2013 engine reads it)" },
	KeyDef_t{ "soundmixer", ValueKind_t::VALUE_KIND_TEXT, "Sound mixer to switch to, from scripts/soundmixers.txt (used by HL2's Citadel soundscapes)" },
};
constexpr std::array<std::string_view, 3> SOUNDSCAPE_BLOCKS{ "playlooping", "playrandom", "playsoundscape" };

constexpr std::array PLAY_LOOPING_KEYS{
	KeyDef_t{ "wave", ValueKind_t::VALUE_KIND_WAVE, "Sound to loop. It only loops if the WAV has loop points" },
	KeyDef_t{ "volume", ValueKind_t::VALUE_KIND_NUMBER, "0 to 1" },
	KeyDef_t{ "pitch", ValueKind_t::VALUE_KIND_NUMBER, "100 is unchanged" },
	KeyDef_t{ "soundlevel", ValueKind_t::VALUE_KIND_SOUND_LEVEL, "How far it carries when played from a position" },
	KeyDef_t{ "attenuation", ValueKind_t::VALUE_KIND_ATTENUATION, "Older alternative to soundlevel" },
	KeyDef_t{ "position", ValueKind_t::VALUE_KIND_POSITION, "Play from the env_soundscape's positionN target instead of everywhere" },
	KeyDef_t{ "origin", ValueKind_t::VALUE_KIND_ORIGIN, "Play from a fixed world position" },
	KeyDef_t{ "suppress_on_restore", ValueKind_t::VALUE_KIND_BOOL, "1: don't restart this sound when a saved game is loaded" },
};

constexpr std::array PLAY_RANDOM_KEYS{
	KeyDef_t{ "time", ValueKind_t::VALUE_KIND_NUMBER_RANGE, "Seconds between plays, as min,max" },
	KeyDef_t{ "volume", ValueKind_t::VALUE_KIND_NUMBER_RANGE, "0 to 1, or a min,max range" },
	KeyDef_t{ "pitch", ValueKind_t::VALUE_KIND_NUMBER_RANGE, "100 is unchanged, or a min,max range" },
	KeyDef_t{ "soundlevel", ValueKind_t::VALUE_KIND_SOUND_LEVEL, "How far it carries when played from a position" },
	KeyDef_t{ "attenuation", ValueKind_t::VALUE_KIND_ATTENUATION, "Older alternative to soundlevel" },
	KeyDef_t{ "position", ValueKind_t::VALUE_KIND_RANDOM_POSITION, "Play from the env_soundscape's positionN target, or \"random\" for random spots around the listener" },
	KeyDef_t{ "origin", ValueKind_t::VALUE_KIND_ORIGIN, "Play from a fixed world position" },
};
constexpr std::array<std::string_view, 1> PLAY_RANDOM_BLOCKS{ "rndwave" };

constexpr std::array PLAY_SOUNDSCAPE_KEYS{
	KeyDef_t{ "name", ValueKind_t::VALUE_KIND_SOUNDSCAPE_NAME, "Soundscape to play inside this one" },
	KeyDef_t{ "volume", ValueKind_t::VALUE_KIND_NUMBER, "Scales the nested soundscape's volume" },
	KeyDef_t{ "positionoverride", ValueKind_t::VALUE_KIND_POSITION, "Play all of its positioned sounds from this position" },
	KeyDef_t{ "ambientpositionoverride", ValueKind_t::VALUE_KIND_POSITION, "Play its ambient (unpositioned) sounds from this position" },
	KeyDef_t{ "position", ValueKind_t::VALUE_KIND_POSITION, "Used this way in Valve's soundscapes; positionoverride is the documented key" },
};

constexpr std::array RNDWAVE_KEYS{
	KeyDef_t{ "wave", ValueKind_t::VALUE_KIND_WAVE, "One candidate; one is picked at random each time" },
};

constexpr std::array<std::string_view, 0> NO_BLOCKS{};

constexpr BlockDef_t SOUNDSCAPE_ENTRY{ "", SOUNDSCAPE_KEYS, SOUNDSCAPE_BLOCKS, "A soundscape, started by env_soundscape or trigger_soundscape" };

constexpr std::array SOUNDSCAPE_BLOCK_DEFS{
	BlockDef_t{ "playlooping", PLAY_LOOPING_KEYS, NO_BLOCKS, "A sound that loops for as long as the soundscape plays" },
	BlockDef_t{ "playrandom", PLAY_RANDOM_KEYS, PLAY_RANDOM_BLOCKS, "Sounds played one at a time at random intervals" },
	BlockDef_t{ "playsoundscape", PLAY_SOUNDSCAPE_KEYS, NO_BLOCKS, "Another soundscape played inside this one" },
	BlockDef_t{ "rndwave", RNDWAVE_KEYS, NO_BLOCKS, "Candidate sounds; one is picked each time" },
};

// ---- Soundscripts --------------------------------------------------------------------------

constexpr std::array SOUNDSCRIPT_KEYS{
	KeyDef_t{ "channel", ValueKind_t::VALUE_KIND_CHANNEL, "Which channel of the entity plays it" },
	KeyDef_t{ "volume", ValueKind_t::VALUE_KIND_VOLUME, "0 to 1 or VOL_NORM, or a min,max range" },
	KeyDef_t{ "pitch", ValueKind_t::VALUE_KIND_PITCH, "100 or PITCH_NORM is unchanged, or a min,max range" },
	KeyDef_t{ "soundlevel", ValueKind_t::VALUE_KIND_SOUND_LEVEL, "How far it carries" },
	KeyDef_t{ "attenuation", ValueKind_t::VALUE_KIND_ATTENUATION, "Older alternative to soundlevel" },
	KeyDef_t{ "compatibilityattenuation", ValueKind_t::VALUE_KIND_ATTENUATION, "Older alternative to soundlevel, used throughout CS:S's scripts" },
	KeyDef_t{ "wave", ValueKind_t::VALUE_KIND_WAVE, "The sound file" },
	KeyDef_t{ "delay_msec", ValueKind_t::VALUE_KIND_NUMBER, "Delay before playing, in milliseconds" },
	KeyDef_t{ "play_to_owner_only", ValueKind_t::VALUE_KIND_BOOL, "Only the owning player hears it" },
};
constexpr std::array<std::string_view, 1> SOUNDSCRIPT_BLOCKS{ "rndwave" };

constexpr BlockDef_t SOUNDSCRIPT_ENTRY{ "", SOUNDSCRIPT_KEYS, SOUNDSCRIPT_BLOCKS, "A named sound that code, models and maps play" };

constexpr std::array SOUNDSCRIPT_BLOCK_DEFS{
	BlockDef_t{ "rndwave", RNDWAVE_KEYS, NO_BLOCKS, "Candidate sounds; one is picked each time it plays" },
};

constexpr CSchema SOUNDSCAPE_SCHEMA{ FileKind_t::FILE_KIND_SOUNDSCAPE, "soundscape", &SOUNDSCAPE_ENTRY, SOUNDSCAPE_BLOCK_DEFS };
constexpr CSchema SOUNDSCRIPT_SCHEMA{ FileKind_t::FILE_KIND_SOUNDSCRIPT, "sound", &SOUNDSCRIPT_ENTRY, SOUNDSCRIPT_BLOCK_DEFS };

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
FileKind_t FileKindForPath( std::string_view szPath )
{
	if ( !szPath.starts_with( "scripts/" ) || !szPath.ends_with( ".txt" ) || szPath.ends_with( "_manifest.txt" ) )
	{
		return FileKind_t::FILE_KIND_UNKNOWN;
	}
	const std::string_view szName = szPath.substr( 8 );
	if ( szName.starts_with( "soundscapes_" ) )
	{
		return FileKind_t::FILE_KIND_SOUNDSCAPE;
	}
	if ( szName.starts_with( "game_sounds" ) || szName.starts_with( "level_sounds" ) )
	{
		return FileKind_t::FILE_KIND_SOUNDSCRIPT;
	}
	return FileKind_t::FILE_KIND_UNKNOWN;
}

//-----------------------------------------------------------------------------
// Purpose: For files opened from anywhere on disk: by name first ("soundscapes_*.txt",
//          "*_level_sounds.txt"...),
//          then by which keys the entries use.
//-----------------------------------------------------------------------------
FileKind_t GuessFileKind( std::string_view szFileName, const kv1::CDocument &document )
{
	const std::string szName = ToLower( szFileName );
	if ( const FileKind_t nKind = FileKindForPath( "scripts/" + szName ); nKind != FileKind_t::FILE_KIND_UNKNOWN )
	{
		return nKind;
	}
	if ( szName.ends_with( "_manifest.txt" ) )
	{
		return FileKind_t::FILE_KIND_UNKNOWN;
	}
	if ( szName.ends_with( "_level_sounds.txt" ) )
	{ // maps/<map>_level_sounds.txt
		return FileKind_t::FILE_KIND_SOUNDSCRIPT;
	}
	int nSoundscape = 0;
	int nSoundscript = 0;
	for ( const kv1::CNode &root : document.m_Roots )
	{
		for ( const kv1::CNode &child : root.GetChildren() )
		{
			const std::string szKey = ToLower( child.GetKey() );
			if ( szKey == "playlooping" || szKey == "playrandom" || szKey == "playsoundscape" || szKey == "dsp" )
			{
				nSoundscape++;
			}
			else if ( szKey == "wave" || szKey == "rndwave" || szKey == "channel" || szKey == "compatibilityattenuation" )
			{
				nSoundscript++;
			}
		}
	}
	if ( nSoundscape == 0 && nSoundscript == 0 )
	{
		return FileKind_t::FILE_KIND_UNKNOWN;
	}
	return nSoundscape > nSoundscript ? FileKind_t::FILE_KIND_SOUNDSCAPE : FileKind_t::FILE_KIND_SOUNDSCRIPT;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view ManifestPathFor( FileKind_t nKind )
{
	switch ( nKind )
	{
		case FileKind_t::FILE_KIND_SOUNDSCAPE: return "scripts/soundscapes_manifest.txt";
		case FileKind_t::FILE_KIND_SOUNDSCRIPT: return "scripts/game_sounds_manifest.txt";
		default: return {};
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const BlockDef_t *CSchema::Block( std::string_view szKey ) const
{
	const auto it = std::ranges::find_if( m_Blocks, [ szKey ]( const BlockDef_t &b ) { return kv1::EqualsIgnoreCase( b.m_szKey, szKey ); } );
	return it == m_Blocks.end() ? nullptr : &*it;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const CSchema *SchemaFor( FileKind_t nKind )
{
	switch ( nKind )
	{
		case FileKind_t::FILE_KIND_SOUNDSCAPE: return &SOUNDSCAPE_SCHEMA;
		case FileKind_t::FILE_KIND_SOUNDSCRIPT: return &SOUNDSCRIPT_SCHEMA;
		default: return nullptr;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const KeyDef_t *FindKey( const BlockDef_t &block, std::string_view szKey )
{
	const auto it = std::ranges::find_if( block.m_Keys, [ szKey ]( const KeyDef_t &k ) { return kv1::EqualsIgnoreCase( k.m_szKey, szKey ); } );
	return it == block.m_Keys.end() ? nullptr : &*it;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool AllowsBlock( const BlockDef_t &block, std::string_view szKey )
{
	return std::ranges::any_of( block.m_Blocks, [ szKey ]( std::string_view b ) { return kv1::EqualsIgnoreCase( b, szKey ); } );
}

} // namespace xv::sound
