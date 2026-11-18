//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The demo viewer's sound (F): the sounds the demo has (svc_Sounds), each started
//          as the playhead passes it while playing, heard from the view's camera: quieter
//          with distance by its sound level, panned by its direction, as the engine's falloff
//          has it (no occlusion, no DSP). A sound on an entity follows the entity; one on the
//          player seen through is heard as that player hears its own. Stops and channel
//          replacement as the engine does them; looping sounds are picked up again after a
//          seek. Under them, the map's soundscape: the env_soundscape nearest the camera whose
//          radius takes it in (the last one while none does), played from the game's
//          soundscape files. Voice chat over it all, as the game plays it: flat, or at the
//          talker for proximity voice; Steam voice (Opus) and vaudio_celt are decoded.
//
//===========================================================================//
#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <QElapsedTimer>
#include <QString>

#include "HudEditor.h"
#include "audio/Audio.h"
#include "demo/Contents.h"
#include "demo/Logic.h"
#include "demo/Players.h"
#include "kv1/Kv1.h"
#include "map/ViewCamera.h"
#include "sound/SoundPlayback.h"

// What a sound is, for its own volume.
enum class DemoSoundKind_t
{
	DEMO_SOUND_WORLD,   // what the demo's entities and players made
	DEMO_SOUND_MUSIC,   // "#" sounds (the engine's music, without effects) and music/
	DEMO_SOUND_AMBIENT, // the map's soundscape
	DEMO_SOUND_VOICE,   // voice chat
	DEMO_SOUND_KIND_COUNT,
};

class CDemoSound
{
public:
	CDemoSound( const xv::demo::CContents *pContents, const xv::demo::CPlayers &players, std::function<bool( int, int )> pfnPlayerMuted = {} );
	~CDemoSound();

	// The game's files to read the sounds and soundscapes from, and where the audio goes
	// (tests: offline). The audio output opens when something is first played.
	void SetFiles( CHudEditor::ReadFile_t pfnRead, xv::audio::CEngine::Output_t nOutput );
	// The map's entities (for its env_soundscapes), once read.
	void SetMap( const xv::demo::LogicMap_t *pMap );
	[[nodiscard]] bool IsOn() const { return m_bOn; }
	void SetOn( bool bOn );
	void SetVolume( float flVolume );
	[[nodiscard]] float GetVolume() const { return m_flVolume; }
	// Each kind's own volume (0 to 1), over the master's.
	[[nodiscard]] float GetKindVolume( DemoSoundKind_t nKind ) const { return m_KindVolumes[ static_cast<std::size_t>( nKind ) ]; }
	void SetKindVolume( DemoSoundKind_t nKind, float flVolume );
	// The kind of a demo's sound, by its name.
	[[nodiscard]] static DemoSoundKind_t KindOf( std::string_view szName );
	// Sounds not heard, by their names as the demo has them (lower case): not started, and
	// silent where they play.
	[[nodiscard]] bool IsMuted( const std::string &szName ) const { return m_Muted.contains( szName ); }
	void SetMuted( const std::string &szName, bool bMuted );
	[[nodiscard]] const std::set<std::string> &GetMuted() const { return m_Muted; }

	// The playhead at flTick, playing or not at flSpeed, heard from camera; nEar: the player
	// seen through (its own sounds are heard as its own), 0 for none.
	void Update( double flTick, bool bPlaying, double flSpeed, const xv::map::CViewCamera &camera, int nEar );
	void StopAll();

	// A stretch of talking (GetSpeech()'s index) decoded, as a clip; null when it isn't.
	[[nodiscard]] std::shared_ptr<const xv::audio::CClip> SpeechClip( std::size_t nSpeech );
	// One stretch of talking heard alone, flat, from its start (the Voice tab's Play); another
	// stops the one before. False when it doesn't decode or there is no audio output.
	[[nodiscard]] bool CanPlaySpeech( std::size_t nSpeech ) const;
	bool PlaySpeech( std::size_t nSpeech );
	void ApplyPlayerMutes();
	void StopSpeech();
	[[nodiscard]] bool IsPlayingSpeech() const;

	// For tests: sounds started so far, voices playing, the soundscape playing.
	[[nodiscard]] int GetStartedCount() const { return m_nStarted; }
	[[nodiscard]] int GetVoicesStarted() const { return m_nVoices; }
	[[nodiscard]] std::size_t GetPlayingCount() const { return m_Playing.size(); }
	[[nodiscard]] const std::string &GetSoundscape() const { return m_szSoundscape; }
	[[nodiscard]] xv::audio::CEngine *GetEngine() const { return m_pEngine.get(); }

private:
	struct Playing_t
	{
		xv::audio::CEngine::Voice_t m_nVoice = 0;
		int m_nEntity = 0;
		int m_nChannel = 0;
		int m_nSound = 0;
		std::optional<xv::map::Point_t> m_Origin; // fixed; else the entity's
		bool m_bPositioned = true;
		float m_flVolume = 1.0f;
		std::optional<double> m_SoundLevel;
		bool m_bSoundscape = false;
		std::optional<std::size_t> m_Speech;
		DemoSoundKind_t m_nKind = DemoSoundKind_t::DEMO_SOUND_WORLD;
		std::string m_szName; // lower case; empty for voice and the soundscape
	};
	[[nodiscard]] bool OpenEngine();
	[[nodiscard]] std::shared_ptr<const xv::audio::CClip> Clip( const std::string &szGamePath );
	void Start( const xv::demo::Sound_t &sound, double flTick, double flSpeed, double flStartSeconds = 0.0 );
	void Remix( double flTick, const xv::map::CViewCamera &camera, int nEar );
	[[nodiscard]] std::optional<xv::map::Point_t> EntityPlace( int nEntity, double flTick ) const;
	// After a seek: the sounds started before and not stopped that would still be playing
	// (looping, or not at their end), from where they'd be.
	void ResumeLoops( double flTick, double flSpeed );
	void UpdateSoundscape( double flTick, const xv::map::CViewCamera &camera );
	// Voice chat (H5): the stretches of talking that start in (flFrom, flTo]; with bResume,
	// those under way at flTo too, from as far into them as they'd be.
	void StartSpeech( double flFrom, double flTo, double flSpeed, bool bResume );
	[[nodiscard]] const xv::kv1::CNode *FindSoundscape( std::string_view szName );

	const xv::demo::CContents *m_pContents = nullptr;
	const xv::demo::CPlayers &m_Players;
	std::function<bool( int, int )> m_pfnPlayerMuted;
	const xv::demo::LogicMap_t *m_pMap = nullptr;
	CHudEditor::ReadFile_t m_pfnRead;
	xv::audio::CEngine::Output_t m_nOutput = xv::audio::CEngine::Output_t::OUTPUT_DEVICE;
	std::unique_ptr<xv::audio::CEngine> m_pEngine;
	bool m_bEngineTried = false;
	bool m_bOn = true;
	float m_flVolume = 0.7f;
	std::array<float, static_cast<std::size_t>( DemoSoundKind_t::DEMO_SOUND_KIND_COUNT )> m_KindVolumes{ 1.0f, 1.0f, 1.0f, 1.0f };
	std::set<std::string> m_Muted;
	std::map<std::string, std::shared_ptr<const xv::audio::CClip>> m_Clips; // null: won't load
	std::size_t m_nClipBytes = 0;                                            // m_Clips' samples
	std::map<std::string, double> m_ClipSeconds;                             // every clip decoded, kept or not
	std::vector<Playing_t> m_Playing;
	double m_flLastTick = -1.0;
	bool m_bWasPlaying = false;
	int m_nStarted = 0;
	int m_nVoices = 0;
	xv::audio::CEngine::Voice_t m_nAlone = 0; // PlaySpeech's
	std::size_t m_nAloneSpeech = 0;
	std::optional<bool> m_bVoiceDecoded; // the demo's voice codec is one that decodes; unknown until asked
	std::map<std::size_t, std::shared_ptr<const xv::audio::CClip>> m_Speech; // by GetSpeech()'s index; null: nothing decoded
	// The soundscape.
	std::vector<xv::kv1::CDocument> m_SoundscapeFiles;
	bool m_bSoundscapesRead = false;
	std::string m_szSoundscape;
	std::optional<xv::sound::CSoundscapeSimulation> m_Simulation;
	QElapsedTimer m_SoundscapeClock;
};
