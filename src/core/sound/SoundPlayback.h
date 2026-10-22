// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "../kv1/Kv1.h"
#include "SoundSchema.h"

// What the engine would play for a sound entry or soundscape, with its random choices made.
// Independent of any audio output, so it can be tested and driven by a clock of the caller's choosing.
namespace xv::sound {

struct Range {
	double low = 0;
	double high = 0;

	[[nodiscard]] double pick(std::mt19937& random) const;
};

// "0.5", "VOL_NORM", "95, 105", "SNDLVL_80dB"... as a range; nullopt if the value doesn't parse.
[[nodiscard]] std::optional<Range> parseRange(ValueKind kind, std::string_view text);

// "sound/<path>" for a wave value, flags stripped and normalized; empty for sentences and voice streams.
[[nodiscard]] std::string waveGamePath(std::string_view wave);

struct PlayRequest {
	std::string wave;     // as written, flags included
	std::string gamePath; // what to load
	double volume = 1;    // 0-1
	double pitch = 100;   // 100 is unchanged
	bool loop = false;
	double delaySeconds = 0;
	std::optional<int> position;
	bool randomPosition = false;
	std::optional<double> soundLevel; // dB; none means heard everywhere
	std::string origin;               // where it came from, for display
};

// One play of a block with its own settings: a soundscript entry, playlooping, playrandom, or an
// rndwave (which takes its settings from the enclosing block). nullopt if there is nothing to play.
[[nodiscard]] std::optional<PlayRequest> playBlock(FileKind kind, const kv1::Node& block, const kv1::Node* parent, std::mt19937& random);

struct SoundscapeRule {
	enum class Kind { Looping, Random };

	Kind kind = Kind::Looping;
	std::vector<std::string> waves;
	Range volume{1, 1};
	Range pitch{100, 100};
	Range interval{10, 10}; // seconds between random plays
	std::optional<int> position;
	bool randomPosition = false;
	std::optional<double> soundLevel;
	std::string origin;
};

struct SoundscapePlan {
	std::vector<SoundscapeRule> rules;
	std::vector<std::string> problems; // missing nested soundscapes, loops, unplayable blocks
	int dsp = 0;
};

using SoundscapeLookup = std::function<const kv1::Node*(std::string_view name)>;

// Every looping and random sound a soundscape plays, nested playsoundscapes included, with their
// volume scaling and position overrides applied.
[[nodiscard]] SoundscapePlan planSoundscape(const kv1::Node& soundscape, const SoundscapeLookup& lookup);

// Runs a soundscape plan on a clock: start() returns the loops, advance() the random sounds that
// came due. Each random sound waits a random time from its interval before playing.
class SoundscapeSimulation {
public:
	SoundscapeSimulation(std::vector<SoundscapeRule> rules, std::uint32_t seed);

	[[nodiscard]] std::vector<PlayRequest> start();
	[[nodiscard]] std::vector<PlayRequest> advance(double seconds);
	[[nodiscard]] double now() const { return now_; }

private:
	PlayRequest request(const SoundscapeRule& rule);

	std::vector<SoundscapeRule> rules_;
	std::vector<double> due_;
	double now_ = 0;
	std::mt19937 random_;
};

} // namespace xv::sound
