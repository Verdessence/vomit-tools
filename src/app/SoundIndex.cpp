// SPDX-License-Identifier: GPL-3.0-or-later
#include "SoundIndex.h"

#include <algorithm>
#include <set>

#include "SoundDocument.h"
#include "SoundPlayer.h"
#include "kv1/Kv1.h"
#include "sound/Wav.h"
#include "vfs/Vfs.h"

using xv::sound::FileKind;

void SoundIndex::rebuild(const xv::vfs::FileSystem& fileSystem) {
	fileSystem_ = &fileSystem;
	soundFiles_.clear();
	soundscapes_.clear();
	sounds_.clear();
	soundscapeDocuments_.clear();
	loops_.clear();

	for (const std::string& path : fileSystem.find("sound", "", true)) {
		if (path.ends_with(".wav") || path.ends_with(".mp3")) {
			soundFiles_ << QString::fromStdString(path.substr(6));
		}
	}

	for (const std::string& file : fileSystem.find("scripts", ".txt")) {
		const FileKind kind = xv::sound::fileKindForPath(file);
		if (kind == FileKind::Unknown) {
			continue;
		}
		const auto text = fileSystem.readText(file);
		if (!text) {
			continue;
		}
		auto parsed = xv::kv1::parse(*text);
		auto& entries = (kind == FileKind::Soundscape ? soundscapes_ : sounds_)[file];
		for (const xv::kv1::Node& root : parsed.document.roots) {
			if (root.isBlock()) {
				entries.push_back({root.key(), kind == FileKind::Soundscape ? xv::sound::nestedSoundscapes(root) : std::vector<std::string>{}});
			}
		}
		if (kind == FileKind::Soundscape) {
			soundscapeDocuments_.emplace(file, std::move(parsed.document));
		}
	}
}

QStringList SoundIndex::soundscapeNames(const SoundDocument* including) const {
	std::set<QString> names;
	const std::string skip = including ? including->gamePath().toStdString() : std::string{};
	for (const auto& [file, entries] : soundscapes_) {
		if (file != skip) {
			for (const Entry& entry : entries) {
				names.insert(QString::fromStdString(entry.name));
			}
		}
	}
	if (including && including->kind() == FileKind::Soundscape) {
		for (const xv::kv1::Node& root : including->document().roots) {
			if (root.isBlock()) {
				names.insert(QString::fromStdString(root.key()));
			}
		}
	}
	return {names.begin(), names.end()};
}

bool SoundIndex::hasGame() const {
	return fileSystem_ && !fileSystem_->records().empty();
}

bool SoundIndex::exists(std::string_view gamePath) const {
	return fileSystem_ && fileSystem_->resolve(gamePath) != nullptr;
}

const xv::kv1::Node* SoundIndex::findSoundscape(std::string_view name, const SoundDocument* first) const {
	std::string skip;
	if (first && first->kind() == FileKind::Soundscape) {
		if (const xv::kv1::Node* found = first->document().find(name); found && found->isBlock()) {
			return found;
		}
		skip = xv::vfs::normalizePath(first->gamePath().toStdString());
	}
	for (const auto& [file, document] : soundscapeDocuments_) {
		if (file != skip) {
			if (const xv::kv1::Node* found = document.find(name); found && found->isBlock()) {
				return found;
			}
		}
	}
	return nullptr;
}

std::optional<bool> SoundIndex::hasLoopPoints(std::string_view gamePath) const {
	auto [it, inserted] = loops_.try_emplace(std::string{gamePath});
	if (inserted && fileSystem_) {
		if (const auto bytes = fileSystem_->read(gamePath)) {
			if (const auto info = xv::sound::readWavInfo(*bytes)) {
				it->second = info->loopStart.has_value();
			}
		}
	}
	return it->second;
}

std::vector<std::string> SoundIndex::manifestFiles(FileKind kind) const {
	if (!fileSystem_) {
		return {};
	}
	const auto text = fileSystem_->readText(xv::sound::manifestPathFor(kind));
	return text ? xv::sound::manifestFiles(xv::kv1::parse(*text).document) : std::vector<std::string>{};
}

xv::sound::LintContext SoundIndex::lintContext(const SoundDocument& document) const {
	xv::sound::LintContext context;
	// Sounds are looked for in the mounted game and, for files opened from a mod folder, beside them.
	const std::filesystem::path root = document.localRoot();
	if (hasGame() || !root.empty()) {
		context.fileExists = [this, root](std::string_view path) {
			std::error_code ec;
			return exists(path) || (!root.empty() && std::filesystem::exists(xv::vfs::findCaseInsensitive(root / xv::vfs::fromUtf8(path)), ec));
		};
		context.hasLoopPoints = [this, root](std::string_view path) -> std::optional<bool> {
			if (exists(path)) {
				return hasLoopPoints(path);
			}
			if (root.empty() || !fileSystem_) {
				return std::nullopt;
			}
			const auto bytes = SoundPlayer::readGameFile(*fileSystem_, std::string{path}, root);
			const auto info = bytes ? xv::sound::readWavInfo(*bytes) : std::nullopt;
			return info ? std::optional{info->loopStart.has_value()} : std::nullopt;
		};
	}

	const std::string self = xv::vfs::normalizePath(document.gamePath().toStdString());
	const bool soundscape = document.kind() == FileKind::Soundscape;
	for (const auto& [file, entries] : soundscape ? soundscapes_ : sounds_) {
		if (file == self) {
			continue;
		}
		for (const Entry& entry : entries) {
			const std::string name = xv::sound::toLower(entry.name);
			if (soundscape) {
				context.otherSoundscapes.emplace(name, file);
				context.otherNesting.emplace(name, entry.nested);
			} else {
				context.otherSounds.emplace(name, file);
			}
		}
	}
	if (hasGame()) {
		const auto listed = manifestFiles(document.kind());
		context.listedInManifest = std::ranges::find(listed, self) != listed.end();
	}
	return context;
}
