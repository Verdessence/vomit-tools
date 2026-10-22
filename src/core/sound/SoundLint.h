// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../kv1/Kv1.h"
#include "SoundSchema.h"

namespace xv::sound {

// Indices from Document::roots down to a node, e.g. {3, 1} is roots[3].child(1). Empty means the whole file.
using NodePath = std::vector<std::size_t>;

struct Issue {
	enum class Severity { Info, Warning, Error };

	Severity severity = Severity::Warning;
	NodePath path;
	std::string message;
};

struct LintContext {
	// Whether a file exists in the mounted game, e.g. "sound/ambient/wind.wav". Unset skips the check.
	std::function<bool(std::string_view gamePath)> fileExists;
	// Whether a WAV has loop points; nullopt when that cannot be told. Unset skips the check.
	std::function<std::optional<bool>(std::string_view gamePath)> hasLoopPoints;

	// Soundscapes defined in other files (lowercase name -> file), for playsoundscape and duplicate checks.
	std::unordered_map<std::string, std::string> otherSoundscapes;
	// The playsoundscape names inside each soundscape of other files (lowercase), for loop detection.
	std::unordered_map<std::string, std::vector<std::string>> otherNesting;
	// Sound entries defined in other soundscripts (lowercase name -> file).
	std::unordered_map<std::string, std::string> otherSounds;

	std::optional<bool> listedInManifest;
};

[[nodiscard]] std::vector<Issue> lint(const kv1::Document& document, FileKind kind, const LintContext& context);

// Checks one value's syntax against its kind; returns what is wrong, or nullopt if it is fine.
[[nodiscard]] std::optional<std::string> checkValue(ValueKind kind, std::string_view value);

// Lowercase names of the soundscapes a soundscape plays through playsoundscape.
[[nodiscard]] std::vector<std::string> nestedSoundscapes(const kv1::Node& soundscape);

// Files a manifest lists (values of "file", "precache_file" and "preload_file"), normalized.
[[nodiscard]] std::vector<std::string> manifestFiles(const kv1::Document& manifest);

// Adds path to the manifest's block. Returns false if it was already listed or there is no block.
bool addToManifest(kv1::Document& manifest, FileKind kind, std::string_view path);

[[nodiscard]] std::string toLower(std::string_view text);

} // namespace xv::sound
