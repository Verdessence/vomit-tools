// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string_view>

namespace xv::kv1 {
struct Document;
}

// What keys and sub-blocks each kind of sound file accepts, and what their values mean.
// Drives the editor's value pickers and the linter.
namespace xv::sound {

enum class FileKind {
	Unknown,
	Soundscape,  // scripts/soundscapes_*.txt
	Soundscript, // scripts/game_sounds*.txt, scripts/level_sounds*.txt
};

[[nodiscard]] FileKind fileKindForPath(std::string_view normalizedPath);
// For files opened from anywhere on disk: by name first ("soundscapes_*.txt", "*_level_sounds.txt"...),
// then by which keys the entries use.
[[nodiscard]] FileKind guessFileKind(std::string_view fileName, const kv1::Document& document);
[[nodiscard]] std::string_view manifestPathFor(FileKind kind);

enum class ValueKind {
	Text,
	Number,
	NumberRange,    // "10" or "10,20"
	Volume,         // number or VOL_NORM, optionally a "min,max" range
	Pitch,          // number or PITCH_*, optionally a range
	SoundLevel,     // SNDLVL_*, SNDLVL_<n>dB or a number, optionally a range
	Attenuation,    // ATTN_* or a number
	Channel,        // CHAN_* or a number
	DspPreset,      // integer room effect
	Position,       // 0-7: the env_soundscape's position0..position7 targets
	RandomPosition, // a position, or "random" for a random spot around the listener
	Origin,         // "x y z"
	Wave,           // a sound file under sound/, with optional flag characters
	SoundscapeName, // another soundscape
	Bool,           // 0 or 1
};

struct KeyDef {
	std::string_view key;
	ValueKind kind;
	std::string_view help;
};

struct BlockDef {
	std::string_view key;                   // "playlooping", "rndwave"; empty for a top-level entry
	std::span<const KeyDef> keys;
	std::span<const std::string_view> blocks; // sub-blocks allowed inside
	std::string_view help;
};

struct Schema {
	FileKind kind;
	std::string_view entryNoun; // "soundscape", "sound"
	const BlockDef* entry;      // a top-level named block
	std::span<const BlockDef> blocks;

	[[nodiscard]] const BlockDef* block(std::string_view key) const;
};

[[nodiscard]] const Schema* schemaFor(FileKind kind);
[[nodiscard]] const KeyDef* findKey(const BlockDef& block, std::string_view key);
[[nodiscard]] bool allowsBlock(const BlockDef& block, std::string_view key);

} // namespace xv::sound
