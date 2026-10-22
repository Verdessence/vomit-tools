// SPDX-License-Identifier: GPL-3.0-or-later
#include "SoundSchema.h"

#include <algorithm>
#include <array>

#include "../kv1/Kv1.h"
#include "SoundLint.h"

namespace xv::sound {

namespace {

// ---- Soundscapes ---------------------------------------------------------------------------
// Volume and pitch are plain numbers here: stock soundscapes never use VOL_/PITCH_ names,
// unlike soundscripts, while they do use SNDLVL_ and ATTN_ names.

constexpr std::array SOUNDSCAPE_KEYS{
	KeyDef{"dsp", ValueKind::DspPreset, "Room effect applied while this soundscape is active"},
	KeyDef{"dsp_volume", ValueKind::Number, "Strength of the room effect (newer branches; verify the 2013 engine reads it)"},
	KeyDef{"soundmixer", ValueKind::Text, "Sound mixer to switch to, from scripts/soundmixers.txt (used by HL2's Citadel soundscapes)"},
};
constexpr std::array<std::string_view, 3> SOUNDSCAPE_BLOCKS{"playlooping", "playrandom", "playsoundscape"};

constexpr std::array PLAY_LOOPING_KEYS{
	KeyDef{"wave", ValueKind::Wave, "Sound to loop. It only loops if the WAV has loop points"},
	KeyDef{"volume", ValueKind::Number, "0 to 1"},
	KeyDef{"pitch", ValueKind::Number, "100 is unchanged"},
	KeyDef{"soundlevel", ValueKind::SoundLevel, "How far it carries when played from a position"},
	KeyDef{"attenuation", ValueKind::Attenuation, "Older alternative to soundlevel"},
	KeyDef{"position", ValueKind::Position, "Play from the env_soundscape's positionN target instead of everywhere"},
	KeyDef{"origin", ValueKind::Origin, "Play from a fixed world position"},
	KeyDef{"suppress_on_restore", ValueKind::Bool, "1: don't restart this sound when a saved game is loaded"},
};

constexpr std::array PLAY_RANDOM_KEYS{
	KeyDef{"time", ValueKind::NumberRange, "Seconds between plays, as min,max"},
	KeyDef{"volume", ValueKind::NumberRange, "0 to 1, or a min,max range"},
	KeyDef{"pitch", ValueKind::NumberRange, "100 is unchanged, or a min,max range"},
	KeyDef{"soundlevel", ValueKind::SoundLevel, "How far it carries when played from a position"},
	KeyDef{"attenuation", ValueKind::Attenuation, "Older alternative to soundlevel"},
	KeyDef{"position", ValueKind::RandomPosition, "Play from the env_soundscape's positionN target, or \"random\" for random spots around the listener"},
	KeyDef{"origin", ValueKind::Origin, "Play from a fixed world position"},
};
constexpr std::array<std::string_view, 1> PLAY_RANDOM_BLOCKS{"rndwave"};

constexpr std::array PLAY_SOUNDSCAPE_KEYS{
	KeyDef{"name", ValueKind::SoundscapeName, "Soundscape to play inside this one"},
	KeyDef{"volume", ValueKind::Number, "Scales the nested soundscape's volume"},
	KeyDef{"positionoverride", ValueKind::Position, "Play all of its positioned sounds from this position"},
	KeyDef{"ambientpositionoverride", ValueKind::Position, "Play its ambient (unpositioned) sounds from this position"},
	KeyDef{"position", ValueKind::Position, "Used this way in Valve's soundscapes; positionoverride is the documented key"},
};

constexpr std::array RNDWAVE_KEYS{
	KeyDef{"wave", ValueKind::Wave, "One candidate; one is picked at random each time"},
};

constexpr std::array<std::string_view, 0> NO_BLOCKS{};

constexpr BlockDef SOUNDSCAPE_ENTRY{"", SOUNDSCAPE_KEYS, SOUNDSCAPE_BLOCKS, "A soundscape, started by env_soundscape or trigger_soundscape"};

constexpr std::array SOUNDSCAPE_BLOCK_DEFS{
	BlockDef{"playlooping", PLAY_LOOPING_KEYS, NO_BLOCKS, "A sound that loops for as long as the soundscape plays"},
	BlockDef{"playrandom", PLAY_RANDOM_KEYS, PLAY_RANDOM_BLOCKS, "Sounds played one at a time at random intervals"},
	BlockDef{"playsoundscape", PLAY_SOUNDSCAPE_KEYS, NO_BLOCKS, "Another soundscape played inside this one"},
	BlockDef{"rndwave", RNDWAVE_KEYS, NO_BLOCKS, "Candidate sounds; one is picked each time"},
};

// ---- Soundscripts --------------------------------------------------------------------------

constexpr std::array SOUNDSCRIPT_KEYS{
	KeyDef{"channel", ValueKind::Channel, "Which channel of the entity plays it"},
	KeyDef{"volume", ValueKind::Volume, "0 to 1 or VOL_NORM, or a min,max range"},
	KeyDef{"pitch", ValueKind::Pitch, "100 or PITCH_NORM is unchanged, or a min,max range"},
	KeyDef{"soundlevel", ValueKind::SoundLevel, "How far it carries"},
	KeyDef{"attenuation", ValueKind::Attenuation, "Older alternative to soundlevel"},
	KeyDef{"compatibilityattenuation", ValueKind::Attenuation, "Older alternative to soundlevel, used throughout CS:S's scripts"},
	KeyDef{"wave", ValueKind::Wave, "The sound file"},
	KeyDef{"delay_msec", ValueKind::Number, "Delay before playing, in milliseconds"},
	KeyDef{"play_to_owner_only", ValueKind::Bool, "Only the owning player hears it"},
};
constexpr std::array<std::string_view, 1> SOUNDSCRIPT_BLOCKS{"rndwave"};

constexpr BlockDef SOUNDSCRIPT_ENTRY{"", SOUNDSCRIPT_KEYS, SOUNDSCRIPT_BLOCKS, "A named sound that code, models and maps play"};

constexpr std::array SOUNDSCRIPT_BLOCK_DEFS{
	BlockDef{"rndwave", RNDWAVE_KEYS, NO_BLOCKS, "Candidate sounds; one is picked each time it plays"},
};

constexpr Schema SOUNDSCAPE_SCHEMA{FileKind::Soundscape, "soundscape", &SOUNDSCAPE_ENTRY, SOUNDSCAPE_BLOCK_DEFS};
constexpr Schema SOUNDSCRIPT_SCHEMA{FileKind::Soundscript, "sound", &SOUNDSCRIPT_ENTRY, SOUNDSCRIPT_BLOCK_DEFS};

} // namespace

FileKind fileKindForPath(std::string_view path) {
	if (!path.starts_with("scripts/") || !path.ends_with(".txt") || path.ends_with("_manifest.txt")) {
		return FileKind::Unknown;
	}
	const std::string_view name = path.substr(8);
	if (name.starts_with("soundscapes_")) {
		return FileKind::Soundscape;
	}
	if (name.starts_with("game_sounds") || name.starts_with("level_sounds")) {
		return FileKind::Soundscript;
	}
	return FileKind::Unknown;
}

FileKind guessFileKind(std::string_view fileName, const kv1::Document& document) {
	const std::string name = toLower(fileName);
	if (const FileKind kind = fileKindForPath("scripts/" + name); kind != FileKind::Unknown) {
		return kind;
	}
	if (name.ends_with("_manifest.txt")) {
		return FileKind::Unknown;
	}
	if (name.ends_with("_level_sounds.txt")) { // maps/<map>_level_sounds.txt
		return FileKind::Soundscript;
	}
	int soundscape = 0;
	int soundscript = 0;
	for (const kv1::Node& root : document.roots) {
		for (const kv1::Node& child : root.children()) {
			const std::string key = toLower(child.key());
			if (key == "playlooping" || key == "playrandom" || key == "playsoundscape" || key == "dsp") {
				soundscape++;
			} else if (key == "wave" || key == "rndwave" || key == "channel" || key == "compatibilityattenuation") {
				soundscript++;
			}
		}
	}
	if (soundscape == 0 && soundscript == 0) {
		return FileKind::Unknown;
	}
	return soundscape > soundscript ? FileKind::Soundscape : FileKind::Soundscript;
}

std::string_view manifestPathFor(FileKind kind) {
	switch (kind) {
		case FileKind::Soundscape:  return "scripts/soundscapes_manifest.txt";
		case FileKind::Soundscript: return "scripts/game_sounds_manifest.txt";
		default:                    return {};
	}
}

const BlockDef* Schema::block(std::string_view key) const {
	const auto it = std::ranges::find_if(blocks, [key](const BlockDef& b) { return kv1::equalsIgnoreCase(b.key, key); });
	return it == blocks.end() ? nullptr : &*it;
}

const Schema* schemaFor(FileKind kind) {
	switch (kind) {
		case FileKind::Soundscape:  return &SOUNDSCAPE_SCHEMA;
		case FileKind::Soundscript: return &SOUNDSCRIPT_SCHEMA;
		default:                    return nullptr;
	}
}

const KeyDef* findKey(const BlockDef& block, std::string_view key) {
	const auto it = std::ranges::find_if(block.keys, [key](const KeyDef& k) { return kv1::equalsIgnoreCase(k.key, key); });
	return it == block.keys.end() ? nullptr : &*it;
}

bool allowsBlock(const BlockDef& block, std::string_view key) {
	return std::ranges::any_of(block.blocks, [key](std::string_view b) { return kv1::equalsIgnoreCase(b, key); });
}

} // namespace xv::sound
