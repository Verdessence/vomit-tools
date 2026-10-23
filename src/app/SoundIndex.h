//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What the sound editors need to know about the whole mounted game: every sound file,
//          every soundscape and sound entry in other files, and which WAVs loop. Rebuilt whenever
//          the game is mounted.
//
//===========================================================================//
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <QStringList>

#include "kv1/Kv1.h"
#include "sound/SoundLint.h"
#include "sound/SoundSchema.h"

namespace xv::vfs
{
class CFileSystem;
}
class CSoundDocument;

class CSoundIndex
{
public:
	void Rebuild( const xv::vfs::CFileSystem &fileSystem );

	// Paths relative to sound/, as soundscripts and soundscapes write them.
	[[nodiscard]] const QStringList &GetSoundFiles() const { return m_SoundFiles; }
	[[nodiscard]] QStringList SoundscapeNames( const CSoundDocument *pIncluding = nullptr ) const;

	[[nodiscard]] bool HasGame() const;
	[[nodiscard]] bool Exists( std::string_view szGamePath ) const;
	[[nodiscard]] std::optional<bool> HasLoopPoints( std::string_view szGamePath ) const;

	// A soundscape by name, looked up in `first` before the game's other soundscape files.
	[[nodiscard]] const xv::kv1::CNode *FindSoundscape( std::string_view szName, const CSoundDocument *pFirst = nullptr ) const;
	[[nodiscard]] std::vector<std::string> ManifestFiles( xv::sound::FileKind_t nKind ) const;

	// Everything the linter needs, with the document's own file left out of the cross-file maps.
	[[nodiscard]] xv::sound::LintContext_t LintContext( const CSoundDocument &document ) const;

private:
	struct Entry_t
	{
		std::string m_szName;                // as written
		std::vector<std::string> m_Nested; // soundscapes only: playsoundscape targets, lowercase
	};

	const xv::vfs::CFileSystem *m_pFileSystem = nullptr;
	QStringList m_SoundFiles;
	std::map<std::string, std::vector<Entry_t>> m_Soundscapes; // file -> entries
	std::map<std::string, std::vector<Entry_t>> m_Sounds;      // file -> entries
	std::map<std::string, xv::kv1::CDocument> m_SoundscapeDocuments; // file -> parsed, for playsoundscape playback
	mutable std::unordered_map<std::string, std::optional<bool>> m_Loops;
};
