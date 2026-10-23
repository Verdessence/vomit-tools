//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What keys and sub-blocks each kind of sound file accepts, and what their values mean.
//          Drives the editor's value pickers and the linter.
//
//===========================================================================//
#pragma once

#include <span>
#include <string_view>

namespace xv::kv1
{
struct CDocument;
}

namespace xv::sound
{

enum class FileKind_t
{
	FILE_KIND_UNKNOWN,
	FILE_KIND_SOUNDSCAPE,  // scripts/soundscapes_*.txt
	FILE_KIND_SOUNDSCRIPT, // scripts/game_sounds*.txt, scripts/level_sounds*.txt
};

[[nodiscard]] FileKind_t FileKindForPath( std::string_view szNormalizedPath );
// For files opened from anywhere on disk: by name first ("soundscapes_*.txt", "*_level_sounds.txt"...),
// then by which keys the entries use.
[[nodiscard]] FileKind_t GuessFileKind( std::string_view szFileName, const kv1::CDocument &document );
[[nodiscard]] std::string_view ManifestPathFor( FileKind_t nKind );

enum class ValueKind_t
{
	VALUE_KIND_TEXT,
	VALUE_KIND_NUMBER,
	VALUE_KIND_NUMBER_RANGE,    // "10" or "10,20"
	VALUE_KIND_VOLUME,         // number or VOL_NORM, optionally a "min,max" range
	VALUE_KIND_PITCH,          // number or PITCH_*, optionally a range
	VALUE_KIND_SOUND_LEVEL,     // SNDLVL_*, SNDLVL_<n>dB or a number, optionally a range
	VALUE_KIND_ATTENUATION,    // ATTN_* or a number
	VALUE_KIND_CHANNEL,        // CHAN_* or a number
	VALUE_KIND_DSP_PRESET,      // integer room effect
	VALUE_KIND_POSITION,       // 0-7: the env_soundscape's position0..position7 targets
	VALUE_KIND_RANDOM_POSITION, // a position, or "random" for a random spot around the listener
	VALUE_KIND_ORIGIN,         // "x y z"
	VALUE_KIND_WAVE,           // a sound file under sound/, with optional flag characters
	VALUE_KIND_SOUNDSCAPE_NAME, // another soundscape
	VALUE_KIND_BOOL,           // 0 or 1
};

struct KeyDef_t
{
	std::string_view m_szKey;
	ValueKind_t m_nKind;
	std::string_view m_szHelp;
};

struct BlockDef_t
{
	std::string_view m_szKey;                   // "playlooping", "rndwave"; empty for a top-level entry
	std::span<const KeyDef_t> m_Keys;
	std::span<const std::string_view> m_Blocks; // sub-blocks allowed inside
	std::string_view m_szHelp;
};

struct CSchema
{
	FileKind_t m_nKind;
	std::string_view m_szEntryNoun; // "soundscape", "sound"
	const BlockDef_t *m_pEntry;      // a top-level named block
	std::span<const BlockDef_t> m_Blocks;

	[[nodiscard]] const BlockDef_t *Block( std::string_view szKey ) const;
};

[[nodiscard]] const CSchema *SchemaFor( FileKind_t nKind );
[[nodiscard]] const KeyDef_t *FindKey( const BlockDef_t &block, std::string_view szKey );
[[nodiscard]] bool AllowsBlock( const BlockDef_t &block, std::string_view szKey );

} // namespace xv::sound
