// SPDX-License-Identifier: GPL-3.0-or-later
#include "SoundLint.h"

#include <algorithm>
#include <format>
#include <unordered_set>

#include "../util/Numbers.h"
#include "../vfs/Vfs.h"
#include "SoundConstants.h"

namespace xv::sound {

namespace {

std::string_view trim(std::string_view text) {
	const auto first = text.find_first_not_of(" \t\r\n");
	if (first == std::string_view::npos) {
		return {};
	}
	const auto last = text.find_last_not_of(" \t\r\n");
	return text.substr(first, last - first + 1);
}

template<typename T>
std::optional<T> parse(std::string_view text) {
	return parseNumber<T>(trim(text));
}

std::vector<std::string_view> split(std::string_view text, char separator) {
	std::vector<std::string_view> parts;
	std::size_t start = 0;
	while (true) {
		const auto end = text.find(separator, start);
		parts.push_back(trim(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start)));
		if (end == std::string_view::npos) {
			return parts;
		}
		start = end + 1;
	}
}

// A value that may be a "min,max" range, each part checked by checkPart.
template<typename F>
std::optional<std::string> checkRange(std::string_view value, F&& checkPart) {
	const auto parts = split(value, ',');
	if (parts.size() > 2) {
		return "expected one value or a min,max range";
	}
	for (const std::string_view part : parts) {
		if (part.empty()) {
			return "empty value";
		}
		if (auto problem = checkPart(part)) {
			return problem;
		}
	}
	return std::nullopt;
}

auto numberOr(std::span<const Constant> table, std::string_view prefix) {
	return [table, prefix](std::string_view part) -> std::optional<std::string> {
		if (findConstant(table, part) || parse<double>(part)) {
			return std::nullopt;
		}
		return std::format("\"{}\" is not a number or a {} constant", part, prefix);
	};
}

std::optional<std::string> notNumber(std::string_view part) {
	return parse<double>(part) ? std::nullopt : std::optional<std::string>{std::format("\"{}\" is not a number", part)};
}

bool endsWithIgnoreCase(std::string_view text, std::string_view suffix) {
	return text.size() >= suffix.size() && kv1::equalsIgnoreCase(text.substr(text.size() - suffix.size()), suffix);
}

class Linter {
public:
	Linter(const kv1::Document& document, FileKind kind, const Schema& schema, const LintContext& context)
			: doc_(document)
			, kind_(kind)
			, schema_(schema)
			, context_(context) {}

	std::vector<Issue> run() {
		if (context_.listedInManifest == false) {
			add(Issue::Severity::Warning, {}, std::format("Not listed in {}, so the game won't load this file", manifestPathFor(kind_)));
		}

		std::unordered_set<std::string> seen;
		for (std::size_t i = 0; i < doc_.roots.size(); i++) {
			const kv1::Node& root = doc_.roots[i];
			if (root.isPair()) {
				add(Issue::Severity::Warning, {i}, std::format("\"{}\" is a key/value pair at the top level; the engine only reads named blocks", root.key()));
				continue;
			}
			if (!root.isBlock()) {
				continue;
			}
			const std::string name = toLower(root.key());
			if (name.empty()) {
				add(Issue::Severity::Error, {i}, std::format("This {} has no name", schema_.entryNoun));
			} else if (!seen.insert(name).second) {
				add(Issue::Severity::Warning, {i}, std::format("\"{}\" is defined more than once in this file", root.key()));
			} else {
				const auto& others = kind_ == FileKind::Soundscape ? context_.otherSoundscapes : context_.otherSounds;
				if (const auto it = others.find(name); it != others.end()) {
					add(Issue::Severity::Info, {i}, std::format("\"{}\" is also defined in {}; only one definition is used", root.key(), it->second));
				}
			}
			if (kind_ == FileKind::Soundscape) {
				nesting_[name] = nestedSoundscapes(root);
			}
		}

		for (std::size_t i = 0; i < doc_.roots.size(); i++) {
			if (doc_.roots[i].isBlock()) {
				checkBlock(*schema_.entry, doc_.roots[i], {i});
			}
		}
		if (kind_ == FileKind::Soundscape) {
			checkLoops();
		}
		return std::move(issues_);
	}

private:
	void add(Issue::Severity severity, NodePath path, std::string message) {
		issues_.push_back({severity, std::move(path), std::move(message)});
	}

	void checkBlock(const BlockDef& def, const kv1::Node& node, const NodePath& path) {
		bool hasWave = false;
		bool hasRndwave = false;
		bool hasName = false;
		for (std::size_t j = 0; j < node.children().size(); j++) {
			const kv1::Node& child = node.child(j);
			NodePath childPath = path;
			childPath.push_back(j);
			const std::string key = child.key();

			if (child.isPair()) {
				const KeyDef* keyDef = findKey(def, key);
				if (!keyDef) {
					add(Issue::Severity::Warning, std::move(childPath), allowsBlock(def, key)
						? std::format("\"{}\" should be a block {{ ... }}, not a value", key)
						: std::format("Unknown key \"{}\"; the engine ignores it", key));
					continue;
				}
				hasWave |= keyDef->kind == ValueKind::Wave;
				hasName |= keyDef->kind == ValueKind::SoundscapeName;
				const std::string value = child.value();
				if (auto problem = checkValue(keyDef->kind, value)) {
					add(Issue::Severity::Warning, std::move(childPath), std::move(*problem));
					continue;
				}
				checkMeaning(def, *keyDef, value, childPath);
			} else if (child.isBlock()) {
				if (!allowsBlock(def, key)) {
					add(Issue::Severity::Warning, std::move(childPath), std::format("Unknown block \"{}\"; the engine ignores it", key));
					continue;
				}
				if (kv1::equalsIgnoreCase(key, "rndwave")) {
					hasRndwave = true;
					if (child.findAll("wave").empty()) {
						add(Issue::Severity::Warning, childPath, "rndwave has no wave entries");
					}
				}
				if (const BlockDef* sub = schema_.block(key)) {
					checkBlock(*sub, child, childPath);
				}
			}
		}

		if (kv1::equalsIgnoreCase(def.key, "playlooping") && !hasWave) {
			add(Issue::Severity::Warning, path, "playlooping has no wave, so it plays nothing");
		} else if (kv1::equalsIgnoreCase(def.key, "playrandom") && !hasRndwave) {
			add(Issue::Severity::Warning, path, "playrandom needs an rndwave block with the sounds to pick from");
		} else if (kv1::equalsIgnoreCase(def.key, "playsoundscape") && !hasName) {
			add(Issue::Severity::Warning, path, "playsoundscape has no name");
		} else if (&def == schema_.entry && kind_ == FileKind::Soundscript && !hasWave && !hasRndwave) {
			add(Issue::Severity::Error, path, "This sound has no wave or rndwave");
		}
	}

	void checkMeaning(const BlockDef& def, const KeyDef& keyDef, const std::string& value, const NodePath& path) {
		switch (keyDef.kind) {
			case ValueKind::Wave:
				checkWave(value, path, kv1::equalsIgnoreCase(def.key, "playlooping"));
				break;
			case ValueKind::SoundscapeName: {
				const std::string name = toLower(value);
				if (!nesting_.contains(name) && !context_.otherSoundscapes.contains(name)) {
					add(Issue::Severity::Error, path, std::format("No soundscape named \"{}\"", value));
				}
				break;
			}
			case ValueKind::DspPreset:
				if (const auto preset = parse<int>(value); preset && *preset >= static_cast<int>(dspPresets().size())) {
					add(Issue::Severity::Info, path, std::format("dsp {} is not one of the stock presets", *preset));
				}
				break;
			default:
				break;
		}
	}

	void checkWave(std::string_view value, const NodePath& path, bool looping) {
		for (std::size_t i = 0; i < 2 && i < value.size(); i++) {
			if (value[i] == '!' || value[i] == '?') {
				return; // a sentence or voice stream, not a file
			}
		}
		const std::string_view file = stripPathFlags(value);
		if (!endsWithIgnoreCase(file, ".wav") && !endsWithIgnoreCase(file, ".mp3")) {
			add(Issue::Severity::Warning, path, std::format("\"{}\" is not a .wav or .mp3 file", file));
			return;
		}
		const std::string gamePath = "sound/" + vfs::normalizePath(file);
		if (context_.fileExists && !context_.fileExists(gamePath)) {
			add(Issue::Severity::Warning, path, std::format("{} is not in the mounted game", gamePath));
			return;
		}
		if (looping && endsWithIgnoreCase(file, ".wav") && context_.hasLoopPoints) {
			if (const auto loops = context_.hasLoopPoints(gamePath); loops && !*loops) {
				add(Issue::Severity::Warning, path, std::format("{} has no loop points, so playlooping plays it only once", gamePath));
			}
		}
	}

	const std::vector<std::string>* nestedIn(const std::string& name) const {
		if (const auto it = nesting_.find(name); it != nesting_.end()) {
			return &it->second;
		}
		if (const auto it = context_.otherNesting.find(name); it != context_.otherNesting.end()) {
			return &it->second;
		}
		return nullptr;
	}

	// Depth-first search for a chain of playsoundscapes from `from` back to `target`.
	bool reaches(const std::string& from, const std::string& target, std::unordered_set<std::string>& visited, std::vector<std::string>& chain) const {
		if (from == target) {
			return true;
		}
		if (!visited.insert(from).second) {
			return false;
		}
		if (const auto* nested = nestedIn(from)) {
			for (const std::string& next : *nested) {
				chain.push_back(next);
				if (reaches(next, target, visited, chain)) {
					return true;
				}
				chain.pop_back();
			}
		}
		return false;
	}

	void checkLoops() {
		for (std::size_t i = 0; i < doc_.roots.size(); i++) {
			const kv1::Node& root = doc_.roots[i];
			if (!root.isBlock()) {
				continue;
			}
			const std::string name = toLower(root.key());
			for (std::size_t j = 0; j < root.children().size(); j++) {
				const kv1::Node& child = root.child(j);
				const kv1::Node* target = child.isBlock() && kv1::equalsIgnoreCase(child.key(), "playsoundscape") ? child.find("name") : nullptr;
				if (!target) {
					continue;
				}
				std::unordered_set<std::string> visited;
				std::vector<std::string> chain{name, toLower(target->value())};
				if (reaches(chain.back(), name, visited, chain)) {
					std::string text;
					for (const std::string& step : chain) {
						text += (text.empty() ? "" : " → ") + step;
					}
					add(Issue::Severity::Error, {i, j}, std::format("playsoundscape loop: {}", text));
				}
			}
		}
	}

	const kv1::Document& doc_;
	FileKind kind_;
	const Schema& schema_;
	const LintContext& context_;
	std::unordered_map<std::string, std::vector<std::string>> nesting_;
	std::vector<Issue> issues_;
};

} // namespace

std::string toLower(std::string_view text) {
	std::string out{text};
	std::ranges::transform(out, out.begin(), [](char c) {
		return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
	});
	return out;
}

std::optional<std::string> checkValue(ValueKind kind, std::string_view value) {
	const std::string_view text = trim(value);
	switch (kind) {
		case ValueKind::Text:
			return std::nullopt;
		case ValueKind::Number:
			return notNumber(text);
		case ValueKind::NumberRange:
			return checkRange(text, notNumber);
		case ValueKind::Volume:
			return checkRange(text, numberOr(volumes(), "VOL_"));
		case ValueKind::Pitch:
			return checkRange(text, numberOr(pitches(), "PITCH_"));
		case ValueKind::SoundLevel:
			return checkRange(text, [](std::string_view part) -> std::optional<std::string> {
				if (parseSoundLevel(part)) {
					return std::nullopt;
				}
				return std::format("\"{}\" is not a sound level (SNDLVL_* or a number of decibels)", part);
			});
		case ValueKind::Attenuation:
			return numberOr(attenuations(), "ATTN_")(text);
		case ValueKind::Channel:
			if (findConstant(channels(), text) || parse<int>(text)) {
				return std::nullopt;
			}
			return std::format("\"{}\" is not a CHAN_ constant", text);
		case ValueKind::DspPreset:
			if (const auto preset = parse<int>(text); preset && *preset >= 0) {
				return std::nullopt;
			}
			return std::format("\"{}\" is not a preset number", text);
		case ValueKind::Position:
		case ValueKind::RandomPosition:
			if (const auto position = parse<int>(text); position && *position >= 0 && *position <= 7) {
				return std::nullopt;
			}
			if (kind == ValueKind::RandomPosition && kv1::equalsIgnoreCase(text, "random")) {
				return std::nullopt;
			}
			if (kind == ValueKind::RandomPosition) {
				return std::format("\"{}\" is not a position from 0 to 7 or \"random\"", text);
			}
			return std::format("\"{}\" is not a position from 0 to 7", text);
		case ValueKind::Origin: {
			std::vector<std::string_view> parts;
			for (const std::string_view part : split(text, ' ')) {
				if (!part.empty()) {
					parts.push_back(part);
				}
			}
			if (parts.size() == 3 && std::ranges::all_of(parts, [](std::string_view p) { return parse<double>(p).has_value(); })) {
				return std::nullopt;
			}
			return std::format("\"{}\" is not an \"x y z\" position", text);
		}
		case ValueKind::Wave:
		case ValueKind::SoundscapeName:
			return text.empty() ? std::optional<std::string>{"empty"} : std::nullopt;
		case ValueKind::Bool:
			return text == "0" || text == "1" ? std::nullopt : std::optional<std::string>{std::format("\"{}\" is not 0 or 1", text)};
	}
	return std::nullopt;
}

std::vector<Issue> lint(const kv1::Document& document, FileKind kind, const LintContext& context) {
	const Schema* schema = schemaFor(kind);
	if (!schema) {
		return {};
	}
	return Linter{document, kind, *schema, context}.run();
}

std::vector<std::string> nestedSoundscapes(const kv1::Node& soundscape) {
	std::vector<std::string> names;
	for (const kv1::Node& child : soundscape.children()) {
		if (child.isBlock() && kv1::equalsIgnoreCase(child.key(), "playsoundscape")) {
			if (const kv1::Node* name = child.find("name")) {
				names.push_back(toLower(name->value()));
			}
		}
	}
	return names;
}

namespace {

kv1::Node* manifestBlock(kv1::Document& manifest) {
	const auto it = std::ranges::find_if(manifest.roots, &kv1::Node::isBlock);
	return it == manifest.roots.end() ? nullptr : &*it;
}

bool isManifestKey(std::string_view key) {
	return kv1::equalsIgnoreCase(key, "file") || kv1::equalsIgnoreCase(key, "precache_file") || kv1::equalsIgnoreCase(key, "preload_file");
}

} // namespace

std::vector<std::string> manifestFiles(const kv1::Document& manifest) {
	std::vector<std::string> files;
	for (const kv1::Node& root : manifest.roots) {
		if (!root.isBlock()) {
			continue;
		}
		for (const kv1::Node& child : root.children()) {
			if (child.isPair() && isManifestKey(child.key())) {
				files.push_back(vfs::normalizePath(child.value()));
			}
		}
		break;
	}
	return files;
}

bool addToManifest(kv1::Document& manifest, FileKind kind, std::string_view path) {
	kv1::Node* block = manifestBlock(manifest);
	const auto listed = manifestFiles(manifest);
	if (!block || std::ranges::find(listed, vfs::normalizePath(path)) != listed.end()) {
		return false;
	}
	block->appendChild(kv1::Node::makePair(kind == FileKind::Soundscape ? "file" : "precache_file", path));
	return true;
}

} // namespace xv::sound
