//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Indices from Document::roots down to a node, e.g. {3, 1} is roots[3].child(1). Empty
//          means the whole file.
//
//===========================================================================//
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

namespace xv::sound
{

using NodePath_t = std::vector<std::size_t>;

struct Issue_t
{
	enum class Severity_t
	{
		SEVERITY_INFO,
		SEVERITY_WARNING,
		SEVERITY_ERROR
	};

	Severity_t m_nSeverity = Severity_t::SEVERITY_WARNING;
	NodePath_t m_Path;
	std::string m_szMessage;
};

struct LintContext_t
{
	// Whether a file exists in the mounted game, e.g. "sound/ambient/wind.wav". Unset skips the check.
	std::function<bool( std::string_view szGamePath )> m_pfnFileExists;
	// Whether a WAV has loop points; nullopt when that cannot be told. Unset skips the check.
	std::function<std::optional<bool>( std::string_view szGamePath )> m_pfnHasLoopPoints;

	// Soundscapes defined in other files (lowercase name -> file), for playsoundscape and duplicate checks.
	std::unordered_map<std::string, std::string> m_OtherSoundscapes;
	// The playsoundscape names inside each soundscape of other files (lowercase), for loop detection.
	std::unordered_map<std::string, std::vector<std::string>> m_OtherNesting;
	// Sound entries defined in other soundscripts (lowercase name -> file).
	std::unordered_map<std::string, std::string> m_OtherSounds;

	std::optional<bool> m_ListedInManifest;
};

[[nodiscard]] std::vector<Issue_t> Lint( const kv1::CDocument &document, FileKind_t nKind, const LintContext_t &context );

// Checks one value's syntax against its kind; returns what is wrong, or nullopt if it is fine.
[[nodiscard]] std::optional<std::string> CheckValue( ValueKind_t nKind, std::string_view szValue );

// Lowercase names of the soundscapes a soundscape plays through playsoundscape.
[[nodiscard]] std::vector<std::string> NestedSoundscapes( const kv1::CNode &soundscape );

// Files a manifest lists (values of "file", "precache_file" and "preload_file"), normalized.
[[nodiscard]] std::vector<std::string> ManifestFiles( const kv1::CDocument &manifest );

// Adds path to the manifest's block. Returns false if it was already listed or there is no block.
bool AddToManifest( kv1::CDocument &manifest, FileKind_t nKind, std::string_view szPath );
// Takes every listing of path out. Returns false if it wasn't listed.
bool RemoveFromManifest( kv1::CDocument &manifest, std::string_view szPath );

[[nodiscard]] std::string ToLower( std::string_view szText );

} // namespace xv::sound
