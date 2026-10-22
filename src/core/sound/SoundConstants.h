// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <span>
#include <string_view>

// Named constants the engine accepts in soundscripts and soundscapes (Source 2013 branch).
namespace xv::sound {

struct Constant {
	std::string_view name; // as written in scripts, e.g. "SNDLVL_GUNFIRE"
	double value;          // what the engine turns it into
	std::string_view note; // short description for tooltips
};

[[nodiscard]] std::span<const Constant> channels();     // CHAN_*
[[nodiscard]] std::span<const Constant> volumes();      // VOL_*
[[nodiscard]] std::span<const Constant> pitches();      // PITCH_*
[[nodiscard]] std::span<const Constant> soundLevels();  // SNDLVL_*
[[nodiscard]] std::span<const Constant> attenuations(); // ATTN_*
[[nodiscard]] std::span<const Constant> dspPresets();   // soundscape "dsp" values, named like Hammer does

// Case-insensitive, since stock files mix "SNDLVL_100dB" and "SNDLVL_100DB".
[[nodiscard]] const Constant* findConstant(std::span<const Constant> table, std::string_view name);

// "SNDLVL_GUNFIRE", "SNDLVL_85dB" (any number) or a plain number, in decibels.
[[nodiscard]] std::optional<double> parseSoundLevel(std::string_view text);

// The engine's conversions between distance attenuation and sound level.
[[nodiscard]] double soundLevelToAttenuation(double soundLevel);
[[nodiscard]] double attenuationToSoundLevel(double attenuation);

// Sound paths may start with up to two flag characters ("*", "#", ")", ...). These are the
// ones the 2013 engine knows, with what they do.
struct PathFlag {
	char character;
	std::string_view meaning;
};
[[nodiscard]] std::span<const PathFlag> pathFlags();

// "*)weapons/ak47/ak47-1.wav" -> "weapons/ak47/ak47-1.wav"
[[nodiscard]] std::string_view stripPathFlags(std::string_view wave);

} // namespace xv::sound
