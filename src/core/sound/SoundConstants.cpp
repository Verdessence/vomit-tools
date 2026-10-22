// SPDX-License-Identifier: GPL-3.0-or-later
#include "SoundConstants.h"

#include <algorithm>
#include <array>

#include "../kv1/Kv1.h"
#include "../util/Numbers.h"

namespace xv::sound {

namespace {

constexpr std::array CHANNELS{
	Constant{"CHAN_AUTO",   0, "Any free channel"},
	Constant{"CHAN_WEAPON", 1, "Weapon sounds; a new one cuts off the previous"},
	Constant{"CHAN_VOICE",  2, "Speech; a new line cuts off the previous"},
	Constant{"CHAN_ITEM",   3, "Item pickups and use sounds"},
	Constant{"CHAN_BODY",   4, "Footsteps, impacts and other body sounds"},
	Constant{"CHAN_STREAM", 5, "Streamed from disk; for long sounds and music"},
	Constant{"CHAN_STATIC", 6, "Static channel; never cut off by other sounds on the entity"},
};

constexpr std::array VOLUMES{
	Constant{"VOL_NORM", 1.0, "Full volume (1.0)"},
};

constexpr std::array PITCHES{
	Constant{"PITCH_NORM", 100, "Unchanged pitch (100)"},
	Constant{"PITCH_LOW",   95, "Slightly lower (95)"},
	Constant{"PITCH_HIGH", 120, "Higher (120)"},
};

constexpr std::array SOUND_LEVELS{
	Constant{"SNDLVL_NONE",      0, "Heard everywhere at full volume; no falloff"},
	Constant{"SNDLVL_20dB",     20, "Rustling leaves"},
	Constant{"SNDLVL_25dB",     25, "Whispering"},
	Constant{"SNDLVL_30dB",     30, "Library"},
	Constant{"SNDLVL_35dB",     35, ""},
	Constant{"SNDLVL_40dB",     40, ""},
	Constant{"SNDLVL_45dB",     45, "Refrigerator"},
	Constant{"SNDLVL_50dB",     50, "Average home"},
	Constant{"SNDLVL_55dB",     55, ""},
	Constant{"SNDLVL_IDLE",     60, "Same as 60 dB"},
	Constant{"SNDLVL_60dB",     60, "Normal conversation"},
	Constant{"SNDLVL_65dB",     65, "Washing machine"},
	Constant{"SNDLVL_STATIC",   66, "Same as 66 dB"},
	Constant{"SNDLVL_70dB",     70, "Vacuum cleaner"},
	Constant{"SNDLVL_NORM",     75, "Default level"},
	Constant{"SNDLVL_75dB",     75, "Busy traffic"},
	Constant{"SNDLVL_80dB",     80, "Noisy restaurant"},
	Constant{"SNDLVL_TALKING",  80, "Same as 80 dB (a stock manifest comment says 60)"},
	Constant{"SNDLVL_85dB",     85, "Average factory"},
	Constant{"SNDLVL_90dB",     90, "Passing motorcycle"},
	Constant{"SNDLVL_95dB",     95, ""},
	Constant{"SNDLVL_100dB",   100, "Subway train, jackhammer"},
	Constant{"SNDLVL_105dB",   105, "Helicopter"},
	Constant{"SNDLVL_110dB",   110, "Sandblasting"},
	Constant{"SNDLVL_120dB",   120, "Car horn, propeller aircraft"},
	Constant{"SNDLVL_130dB",   130, "Air raid siren"},
	Constant{"SNDLVL_GUNFIRE", 140, "Gunshots"},
	Constant{"SNDLVL_140dB",   140, "Jet engine"},
	Constant{"SNDLVL_150dB",   150, ""},
	Constant{"SNDLVL_180dB",   180, "Rocket launch"},
};

constexpr std::array ATTENUATIONS{
	Constant{"ATTN_NONE",     0.0,  "No falloff (SNDLVL_NONE)"},
	Constant{"ATTN_NORM",     0.8,  "About 75 dB"},
	Constant{"ATTN_IDLE",     2.0,  "About 60 dB"},
	Constant{"ATTN_STATIC",   1.25, "About 66 dB"},
	Constant{"ATTN_RICOCHET", 1.5,  "About 65 dB"},
	Constant{"ATTN_GUNFIRE",  0.27, "About 140 dB"},
};

constexpr std::array DSP_PRESETS{
	Constant{"Normal (off)",     0, "No room effect"},
	Constant{"Generic",          1, ""},
	Constant{"Metal Small",      2, ""},
	Constant{"Metal Medium",     3, ""},
	Constant{"Metal Large",      4, ""},
	Constant{"Tunnel Small",     5, ""},
	Constant{"Tunnel Medium",    6, ""},
	Constant{"Tunnel Large",     7, ""},
	Constant{"Chamber Small",    8, ""},
	Constant{"Chamber Medium",   9, ""},
	Constant{"Chamber Large",   10, ""},
	Constant{"Bright Small",    11, ""},
	Constant{"Bright Medium",   12, ""},
	Constant{"Bright Large",    13, ""},
	Constant{"Water 1",         14, ""},
	Constant{"Water 2",         15, ""},
	Constant{"Water 3",         16, ""},
	Constant{"Concrete Small",  17, ""},
	Constant{"Concrete Medium", 18, ""},
	Constant{"Concrete Large",  19, ""},
	Constant{"Big 1",           20, ""},
	Constant{"Big 2",           21, ""},
	Constant{"Big 3",           22, ""},
	Constant{"Cavern Small",    23, ""},
	Constant{"Cavern Medium",   24, ""},
	Constant{"Cavern Large",    25, ""},
	Constant{"Weirdo 1",        26, ""},
	Constant{"Weirdo 2",        27, ""},
	Constant{"Weirdo 3",        28, ""},
};

constexpr std::array PATH_FLAGS{
	PathFlag{'*', "Stream from disk"},
	PathFlag{'#', "Dry mix: skip the room (DSP) effect"},
	PathFlag{'@', "Omnidirectional"},
	PathFlag{'>', "Doppler stereo: left is approaching, right is receding"},
	PathFlag{'<', "Directional stereo: left faces front, right faces back"},
	PathFlag{'^', "Distance variant stereo: left is near, right is far"},
	PathFlag{')', "Spatialized stereo"},
	PathFlag{'}', "Fast, low-quality pitch shifting"},
	PathFlag{'!', "Sentence name, not a file"},
	PathFlag{'?', "Live voice data, not a file"},
};

} // namespace

std::span<const Constant> channels() { return CHANNELS; }
std::span<const Constant> volumes() { return VOLUMES; }
std::span<const Constant> pitches() { return PITCHES; }
std::span<const Constant> soundLevels() { return SOUND_LEVELS; }
std::span<const Constant> attenuations() { return ATTENUATIONS; }
std::span<const Constant> dspPresets() { return DSP_PRESETS; }
std::span<const PathFlag> pathFlags() { return PATH_FLAGS; }

const Constant* findConstant(std::span<const Constant> table, std::string_view name) {
	const auto it = std::ranges::find_if(table, [name](const Constant& c) { return kv1::equalsIgnoreCase(c.name, name); });
	return it == table.end() ? nullptr : &*it;
}

std::optional<double> parseSoundLevel(std::string_view text) {
	if (const Constant* constant = findConstant(soundLevels(), text)) {
		return constant->value;
	}
	constexpr std::string_view PREFIX = "SNDLVL_";
	if (text.size() > PREFIX.size() && kv1::equalsIgnoreCase(text.substr(0, PREFIX.size()), PREFIX)) {
		text.remove_prefix(PREFIX.size());
		if (text.size() > 2 && kv1::equalsIgnoreCase(text.substr(text.size() - 2), "dB")) {
			text.remove_suffix(2);
		} else {
			return std::nullopt;
		}
	}
	return parseNumber<double>(text);
}

double soundLevelToAttenuation(double soundLevel) {
	return soundLevel > 50 ? 20.0 / (soundLevel - 50) : 4.0;
}

double attenuationToSoundLevel(double attenuation) {
	return attenuation > 0 ? 50 + 20 / attenuation : 0;
}

std::string_view stripPathFlags(std::string_view wave) {
	for (int i = 0; i < 2 && !wave.empty(); i++) {
		const bool flag = std::ranges::any_of(PATH_FLAGS, [c = wave.front()](const PathFlag& f) { return f.character == c; });
		if (!flag) {
			break;
		}
		wave.remove_prefix(1);
	}
	return wave;
}

} // namespace xv::sound
