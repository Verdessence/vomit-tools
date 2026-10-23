//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Plays sounds for previews: files as they are, sound entries with their settings, and
//          whole soundscapes. Shared by every editor and preview; the audio device opens on first
//          use.
//
//===========================================================================//
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

namespace xv::vfs
{
class CFileSystem;
}

class CSoundPlayer : public QObject
{
	Q_OBJECT

public:
	// Tests pass Output::Offline: everything works, nothing is heard.
	explicit CSoundPlayer( const xv::vfs::CFileSystem &fileSystem, QObject *pParent = nullptr, xv::audio::CEngine::Output_t nOutput = xv::audio::CEngine::Output_t::OUTPUT_DEVICE );
	~CSoundPlayer() override;

	// False when no audio output can be opened; GetReason() says why.
	bool Available();
	[[nodiscard]] QString GetReason() const { return m_szReason; }

	// Every play call below replaces what was playing: one preview at a time.

	// A sound file's bytes as they are, from startSeconds on; loop uses the file's loop point.
	bool PlayData( const QString &szName, std::span<const std::byte> bytes, bool bLoop = false, double flStartSeconds = 0.0 );
	// A game sound ("sound/ambient/wind.wav") as it is, from the game or a mod folder on disk.
	bool PlayGameSound( const std::string &szGamePath, const std::filesystem::path &localRoot = {} );
	// A sound with its volume, pitch, looping and delay applied.
	bool Play( const xv::sound::PlayRequest_t &request, const std::filesystem::path &localRoot = {} );
	// Plays a soundscape until stopped: loops now, random sounds at their intervals.
	bool PlaySoundscape( const xv::sound::SoundscapePlan_t &plan, const QString &szName, const std::filesystem::path &localRoot = {} );

	void Stop();
	[[nodiscard]] bool IsPlaying() const { return m_bActive; }
	// The voice the last play call started (0 if it failed), and how far into its sound it is:
	// for drawing a playhead. nullopt once that voice has ended or been replaced.
	[[nodiscard]] xv::audio::CEngine::Voice_t GetLastVoice() const { return m_nLastVoice; }
	[[nodiscard]] std::optional<double> Position( xv::audio::CEngine::Voice_t nVoice );
	void SetVolume( float flVolume );

	// Game files from the mounted game, else from the mod folder a file was opened from.
	[[nodiscard]] static std::optional<std::vector<std::byte>> ReadGameFile( const xv::vfs::CFileSystem &fileSystem, const std::string &szGamePath, const std::filesystem::path &localRoot );

signals:
	void Started( const QString &szDescription );
	void Stopped();
	void Failed( const QString &szMessage );

private:
	std::shared_ptr<const xv::audio::CClip> Load( const std::string &szGamePath, const std::filesystem::path &localRoot );
	std::shared_ptr<const xv::audio::CClip> DecodeCached( const std::string &szKey, std::span<const std::byte> bytes, QString *pError );
	xv::audio::CEngine::Voice_t Start( std::shared_ptr<const xv::audio::CClip> pClip, const xv::sound::PlayRequest_t &request, double flStartSeconds = 0.0 );
	void Tick();
	void SetActive( bool bActive );

	const xv::vfs::CFileSystem &m_FileSystem;
	xv::audio::CEngine::Output_t m_nOutput;
	std::unique_ptr<xv::audio::CEngine> m_pEngine;
	bool m_bTriedEngine = false;
	QString m_szReason;
	float m_flVolume = 1.0f;
	xv::audio::CEngine::Voice_t m_nLastVoice = 0;

	std::map<std::string, std::shared_ptr<const xv::audio::CClip>> m_Cache;
	std::deque<std::string> m_CacheOrder;
	std::size_t m_nCacheBytes = 0;

	std::optional<xv::sound::CSoundscapeSimulation> m_Simulation;
	std::filesystem::path m_SimulationRoot;
	QString m_szSimulationName;
	QElapsedTimer m_Clock;
	QTimer *m_pTimer = nullptr;
	bool m_bActive = false;
};
