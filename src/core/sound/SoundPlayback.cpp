// SPDX-License-Identifier: GPL-3.0-or-later
#include "SoundPlayback.h"

#include <algorithm>
#include <format>

#include "../util/Numbers.h"
#include "../vfs/Vfs.h"
#include "SoundConstants.h"
#include "SoundLint.h"

namespace xv::sound {

namespace {

std::string_view trim(std::string_view text) {
	const auto first = text.find_first_not_of(" \t\r\n");
	if (first == std::string_view::npos) {
		return {};
	}
	return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::optional<double> resolvePart(ValueKind kind, std::string_view part) {
	const auto constant = [part](std::span<const Constant> table) -> std::optional<double> {
		if (const Constant* c = findConstant(table, part)) {
			return c->value;
		}
		return parseNumber<double>(part);
	};
	switch (kind) {
		case ValueKind::Volume:      return constant(volumes());
		case ValueKind::Pitch:       return constant(pitches());
		case ValueKind::Attenuation: return constant(attenuations());
		case ValueKind::SoundLevel:  return parseSoundLevel(part);
		default:                     return parseNumber<double>(part);
	}
}

// The settings a block plays with, read the way its file type reads them.
struct Settings {
	Range volume{1, 1};
	Range pitch{100, 100};
	std::optional<int> position;
	bool randomPosition = false;
	std::optional<double> soundLevel;
	double delaySeconds = 0;
};

Settings readSettings(FileKind kind, const kv1::Node& block) {
	const bool script = kind == FileKind::Soundscript;
	Settings settings;
	if (const kv1::Node* volume = block.find("volume")) {
		settings.volume = parseRange(script ? ValueKind::Volume : ValueKind::NumberRange, volume->value()).value_or(Range{1, 1});
	}
	if (const kv1::Node* pitch = block.find("pitch")) {
		settings.pitch = parseRange(script ? ValueKind::Pitch : ValueKind::NumberRange, pitch->value()).value_or(Range{100, 100});
	}
	if (const kv1::Node* level = block.find("soundlevel")) {
		if (const auto range = parseRange(ValueKind::SoundLevel, level->value())) {
			settings.soundLevel = range->low;
		}
	} else if (const kv1::Node* attenuation = block.find("attenuation") ? block.find("attenuation") : block.find("compatibilityattenuation")) {
		if (const auto range = parseRange(ValueKind::Attenuation, attenuation->value())) {
			settings.soundLevel = attenuationToSoundLevel(range->low);
		}
	}
	if (const kv1::Node* position = block.find("position")) {
		if (kv1::equalsIgnoreCase(trim(position->value()), "random")) {
			settings.randomPosition = true;
		} else {
			settings.position = parseNumber<int>(trim(position->value()));
		}
	}
	if (const kv1::Node* delay = block.find("delay_msec")) {
		settings.delaySeconds = parseNumber<double>(trim(delay->value())).value_or(0) / 1000.0;
	}
	return settings;
}

std::vector<std::string> wavesIn(const kv1::Node& block) {
	std::vector<std::string> waves;
	for (const kv1::Node* wave : block.findAll("wave")) {
		waves.push_back(wave->value());
	}
	if (const kv1::Node* rndwave = block.find("rndwave"); rndwave && rndwave->isBlock()) {
		for (const kv1::Node* wave : rndwave->findAll("wave")) {
			waves.push_back(wave->value());
		}
	}
	std::erase_if(waves, [](const std::string& wave) { return trim(wave).empty(); });
	return waves;
}

PlayRequest makeRequest(const std::vector<std::string>& waves, const Settings& settings, bool loop, std::string origin, std::mt19937& random) {
	PlayRequest request;
	request.wave = waves[std::uniform_int_distribution<std::size_t>{0, waves.size() - 1}(random)];
	request.gamePath = waveGamePath(request.wave);
	request.volume = settings.volume.pick(random);
	request.pitch = settings.pitch.pick(random);
	request.loop = loop;
	request.delaySeconds = settings.delaySeconds;
	request.position = settings.position;
	request.randomPosition = settings.randomPosition;
	request.soundLevel = settings.soundLevel;
	request.origin = std::move(origin);
	return request;
}

} // namespace

double Range::pick(std::mt19937& random) const {
	return low >= high ? low : std::uniform_real_distribution<double>{low, high}(random);
}

std::optional<Range> parseRange(ValueKind kind, std::string_view text) {
	std::vector<std::string_view> parts;
	std::size_t start = 0;
	while (true) {
		const auto comma = text.find(',', start);
		parts.push_back(trim(text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start)));
		if (comma == std::string_view::npos) {
			break;
		}
		start = comma + 1;
	}
	if (parts.size() > 2) {
		return std::nullopt;
	}
	const auto low = resolvePart(kind, parts[0]);
	const auto high = parts.size() == 2 ? resolvePart(kind, parts[1]) : low;
	if (!low || !high) {
		return std::nullopt;
	}
	return Range{std::min(*low, *high), std::max(*low, *high)};
}

std::string waveGamePath(std::string_view wave) {
	for (std::size_t i = 0; i < 2 && i < wave.size(); i++) {
		if (wave[i] == '!' || wave[i] == '?') {
			return {};
		}
	}
	const std::string_view file = trim(stripPathFlags(wave));
	return file.empty() ? std::string{} : "sound/" + vfs::normalizePath(file);
}

std::optional<PlayRequest> playBlock(FileKind kind, const kv1::Node& block, const kv1::Node* parent, std::mt19937& random) {
	if (!block.isBlock()) {
		return std::nullopt;
	}
	const std::string key = toLower(block.key());
	if (key == "rndwave") {
		// An rndwave plays with the settings of the entry or playrandom around it.
		std::vector<std::string> waves;
		for (const kv1::Node* wave : block.findAll("wave")) {
			if (!trim(wave->value()).empty()) {
				waves.push_back(wave->value());
			}
		}
		if (waves.empty()) {
			return std::nullopt;
		}
		const Settings settings = parent ? readSettings(kind, *parent) : Settings{};
		return makeRequest(waves, settings, false, parent ? parent->key() + " › rndwave" : "rndwave", random);
	}

	const bool looping = kind == FileKind::Soundscape && key == "playlooping";
	const bool entry = kind == FileKind::Soundscript && parent == nullptr;
	if (!looping && !entry && !(kind == FileKind::Soundscape && key == "playrandom")) {
		return std::nullopt;
	}
	const auto waves = wavesIn(block);
	if (waves.empty()) {
		return std::nullopt;
	}
	return makeRequest(waves, readSettings(kind, block), looping, block.key(), random);
}

namespace {

class Planner {
public:
	explicit Planner(const SoundscapeLookup& lookup)
			: lookup_(lookup) {}

	struct Context {
		double volumeScale = 1;
		std::optional<int> positionOverride;
		std::optional<int> ambientPositionOverride;
	};

	void add(const kv1::Node& soundscape, const Context& context) {
		const std::string name = toLower(soundscape.key());
		if (std::ranges::find(stack_, name) != stack_.end()) {
			plan_.problems.push_back(std::format("playsoundscape loop at \"{}\"; not followed", soundscape.key()));
			return;
		}
		if (stack_.size() >= 8) {
			plan_.problems.push_back(std::format("playsoundscape nesting is too deep at \"{}\"", soundscape.key()));
			return;
		}
		stack_.push_back(name);
		for (const kv1::Node& child : soundscape.children()) {
			if (!child.isBlock()) {
				continue;
			}
			const std::string key = toLower(child.key());
			const std::string origin = soundscape.key() + " › " + child.key();
			if (key == "playlooping" || key == "playrandom") {
				addRule(child, key == "playlooping", origin, context);
			} else if (key == "playsoundscape") {
				addNested(child, origin, context);
			}
		}
		stack_.pop_back();
	}

	SoundscapePlan take() {
		return std::move(plan_);
	}

private:
	void addRule(const kv1::Node& block, bool looping, const std::string& origin, const Context& context) {
		SoundscapeRule rule;
		rule.kind = looping ? SoundscapeRule::Kind::Looping : SoundscapeRule::Kind::Random;
		rule.waves = wavesIn(block);
		if (rule.waves.empty()) {
			plan_.problems.push_back(origin + " has no sounds");
			return;
		}
		const Settings settings = readSettings(FileKind::Soundscape, block);
		rule.volume = {settings.volume.low * context.volumeScale, settings.volume.high * context.volumeScale};
		rule.pitch = settings.pitch;
		rule.position = settings.position;
		rule.randomPosition = settings.randomPosition;
		rule.soundLevel = settings.soundLevel;
		if (rule.position && context.positionOverride) {
			rule.position = context.positionOverride;
		} else if (!rule.position && !rule.randomPosition && context.ambientPositionOverride) {
			rule.position = context.ambientPositionOverride;
		}
		if (!looping) {
			if (const kv1::Node* time = block.find("time")) {
				rule.interval = parseRange(ValueKind::NumberRange, time->value()).value_or(Range{10, 10});
			}
		}
		rule.origin = origin;
		plan_.rules.push_back(std::move(rule));
	}

	void addNested(const kv1::Node& block, const std::string& origin, const Context& context) {
		const kv1::Node* name = block.find("name");
		const kv1::Node* target = name ? lookup_(name->value()) : nullptr;
		if (!target) {
			plan_.problems.push_back(std::format("{}: no soundscape named \"{}\"", origin, name ? name->value() : std::string{}));
			return;
		}
		Context nested = context;
		if (const kv1::Node* volume = block.find("volume")) {
			nested.volumeScale *= parseNumber<double>(trim(volume->value())).value_or(1);
		}
		if (const kv1::Node* position = block.find("positionoverride")) {
			nested.positionOverride = parseNumber<int>(trim(position->value()));
		}
		if (const kv1::Node* position = block.find("ambientpositionoverride")) {
			nested.ambientPositionOverride = parseNumber<int>(trim(position->value()));
		}
		add(*target, nested);
	}

	const SoundscapeLookup& lookup_;
	std::vector<std::string> stack_;
	SoundscapePlan plan_;
};

} // namespace

SoundscapePlan planSoundscape(const kv1::Node& soundscape, const SoundscapeLookup& lookup) {
	Planner planner{lookup};
	planner.add(soundscape, {});
	SoundscapePlan plan = planner.take();
	if (const kv1::Node* dsp = soundscape.find("dsp")) {
		plan.dsp = parseNumber<int>(trim(dsp->value())).value_or(0);
	}
	return plan;
}

SoundscapeSimulation::SoundscapeSimulation(std::vector<SoundscapeRule> rules, std::uint32_t seed)
		: rules_(std::move(rules))
		, due_(rules_.size(), 0)
		, random_(seed) {}

PlayRequest SoundscapeSimulation::request(const SoundscapeRule& rule) {
	Settings settings;
	settings.volume = rule.volume;
	settings.pitch = rule.pitch;
	settings.position = rule.position;
	settings.randomPosition = rule.randomPosition;
	settings.soundLevel = rule.soundLevel;
	return makeRequest(rule.waves, settings, rule.kind == SoundscapeRule::Kind::Looping, rule.origin, random_);
}

std::vector<PlayRequest> SoundscapeSimulation::start() {
	now_ = 0;
	std::vector<PlayRequest> loops;
	for (std::size_t i = 0; i < rules_.size(); i++) {
		if (rules_[i].kind == SoundscapeRule::Kind::Looping) {
			loops.push_back(request(rules_[i]));
		} else {
			due_[i] = rules_[i].interval.pick(random_);
		}
	}
	return loops;
}

std::vector<PlayRequest> SoundscapeSimulation::advance(double seconds) {
	now_ += std::max(0.0, seconds);
	std::vector<PlayRequest> due;
	for (std::size_t i = 0; i < rules_.size(); i++) {
		if (rules_[i].kind != SoundscapeRule::Kind::Random) {
			continue;
		}
		while (due_[i] <= now_) {
			due.push_back(request(rules_[i]));
			// A zero interval would fire forever; the engine can't play faster than its frame rate anyway.
			due_[i] += std::max(0.05, rules_[i].interval.pick(random_));
		}
	}
	return due;
}

} // namespace xv::sound
