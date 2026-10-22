// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include "audio/Audio.h"
#include "sound/SoundPlayback.h"

class QTimer;

namespace xv::vfs {
class FileSystem;
}

// Plays sounds for previews: files as they are, sound entries with their settings, and whole
// soundscapes. Shared by every editor and preview; the audio device opens on first use.
class SoundPlayer : public QObject {
	Q_OBJECT

public:
	// Tests pass Output::Offline: everything works, nothing is heard.
	explicit SoundPlayer(const xv::vfs::FileSystem& fileSystem, QObject* parent = nullptr, xv::audio::Engine::Output output = xv::audio::Engine::Output::Device);
	~SoundPlayer() override;

	// False when no audio output can be opened; reason() says why.
	bool available();
	[[nodiscard]] QString reason() const { return reason_; }

	// Every play call below replaces what was playing: one preview at a time.

	// A sound file's bytes as they are, from startSeconds on; loop uses the file's loop point.
	bool playData(const QString& name, std::span<const std::byte> bytes, bool loop = false, double startSeconds = 0.0);
	// A game sound ("sound/ambient/wind.wav") as it is, from the game or a mod folder on disk.
	bool playGameSound(const std::string& gamePath, const std::filesystem::path& localRoot = {});
	// A sound with its volume, pitch, looping and delay applied.
	bool play(const xv::sound::PlayRequest& request, const std::filesystem::path& localRoot = {});
	// Plays a soundscape until stopped: loops now, random sounds at their intervals.
	bool playSoundscape(const xv::sound::SoundscapePlan& plan, const QString& name, const std::filesystem::path& localRoot = {});

	void stop();
	[[nodiscard]] bool isPlaying() const { return active_; }
	// The voice the last play call started (0 if it failed), and how far into its sound it is:
	// for drawing a playhead. nullopt once that voice has ended or been replaced.
	[[nodiscard]] xv::audio::Engine::Voice lastVoice() const { return lastVoice_; }
	[[nodiscard]] std::optional<double> position(xv::audio::Engine::Voice voice);
	void setVolume(float volume);

	// Game files from the mounted game, else from the mod folder a file was opened from.
	[[nodiscard]] static std::optional<std::vector<std::byte>> readGameFile(const xv::vfs::FileSystem& fileSystem, const std::string& gamePath, const std::filesystem::path& localRoot);

signals:
	void started(const QString& description);
	void stopped();
	void failed(const QString& message);

private:
	std::shared_ptr<const xv::audio::Clip> load(const std::string& gamePath, const std::filesystem::path& localRoot);
	std::shared_ptr<const xv::audio::Clip> decodeCached(const std::string& key, std::span<const std::byte> bytes, QString* error);
	xv::audio::Engine::Voice start(std::shared_ptr<const xv::audio::Clip> clip, const xv::sound::PlayRequest& request, double startSeconds = 0.0);
	void tick();
	void setActive(bool active);

	const xv::vfs::FileSystem& fileSystem_;
	xv::audio::Engine::Output output_;
	std::unique_ptr<xv::audio::Engine> engine_;
	bool triedEngine_ = false;
	QString reason_;
	float volume_ = 1.0f;
	xv::audio::Engine::Voice lastVoice_ = 0;

	std::map<std::string, std::shared_ptr<const xv::audio::Clip>> cache_;
	std::deque<std::string> cacheOrder_;
	std::size_t cacheBytes_ = 0;

	std::optional<xv::sound::SoundscapeSimulation> simulation_;
	std::filesystem::path simulationRoot_;
	QString simulationName_;
	QElapsedTimer clock_;
	QTimer* timer_ = nullptr;
	bool active_ = false;
};
