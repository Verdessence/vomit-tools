// SPDX-License-Identifier: GPL-3.0-or-later
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

namespace xv::vfs {
class FileSystem;
}
class SoundDocument;

// What the sound editors need to know about the whole mounted game: every sound file, every
// soundscape and sound entry in other files, and which WAVs loop. Rebuilt whenever the game is mounted.
class SoundIndex {
public:
	void rebuild(const xv::vfs::FileSystem& fileSystem);

	// Paths relative to sound/, as soundscripts and soundscapes write them.
	[[nodiscard]] const QStringList& soundFiles() const { return soundFiles_; }
	[[nodiscard]] QStringList soundscapeNames(const SoundDocument* including = nullptr) const;

	[[nodiscard]] bool hasGame() const;
	[[nodiscard]] bool exists(std::string_view gamePath) const;
	[[nodiscard]] std::optional<bool> hasLoopPoints(std::string_view gamePath) const;

	// A soundscape by name, looked up in `first` before the game's other soundscape files.
	[[nodiscard]] const xv::kv1::Node* findSoundscape(std::string_view name, const SoundDocument* first = nullptr) const;
	[[nodiscard]] std::vector<std::string> manifestFiles(xv::sound::FileKind kind) const;

	// Everything the linter needs, with the document's own file left out of the cross-file maps.
	[[nodiscard]] xv::sound::LintContext lintContext(const SoundDocument& document) const;

private:
	struct Entry {
		std::string name;                // as written
		std::vector<std::string> nested; // soundscapes only: playsoundscape targets, lowercase
	};

	const xv::vfs::FileSystem* fileSystem_ = nullptr;
	QStringList soundFiles_;
	std::map<std::string, std::vector<Entry>> soundscapes_; // file -> entries
	std::map<std::string, std::vector<Entry>> sounds_;      // file -> entries
	std::map<std::string, xv::kv1::Document> soundscapeDocuments_; // file -> parsed, for playsoundscape playback
	mutable std::unordered_map<std::string, std::optional<bool>> loops_;
};
