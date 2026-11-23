//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See DemoSound.h.
//
//===========================================================================//
#include "DemoSound.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include "map/Geometry.h"
#include "demo/Voice.h"
#include "sound/SoundSchema.h"

using namespace xv::demo;
using xv::map::Point_t;

namespace
{

//-----------------------------------------------------------------------------
// Purpose: A sound's name in lower case, its slashes forward.
//-----------------------------------------------------------------------------
std::string LowerName( std::string szName )
{
	for ( char &ch : szName )
		ch = ch == '\\' ? '/' : static_cast<char>( std::tolower( static_cast<unsigned char>( ch ) ) );
	return szName;
}

// The engine's channels that never replace what an entity already plays on them.
constexpr int CHANNEL_AUTO = 0;
constexpr int CHANNEL_STATIC = 6;
// Sounds play only at speeds near the demo's own; past these, they'd be noise.
constexpr double SPEED_LEAST = 0.5;
constexpr double SPEED_MOST = 2.0;
// A playhead that moved further than this in one step (seconds of the demo) jumped.
constexpr double STEP_MOST_SECONDS = 0.5;
// A place further out than this on any axis is the field's least (no place given).
constexpr float PLACE_MOST = 16000.0f;
// Decoded sounds kept, at most, and their samples' bytes at most (a map's music decodes to
// tens of megabytes a track); past either, the cache starts over (what plays keeps its own).
constexpr std::size_t CLIPS_KEPT = 512;
constexpr std::size_t CLIP_BYTES_KEPT = std::size_t{ 384 } << 20;
// After a seek, sounds that play once are looked for back this far (seconds): longer than a
// map's longest music.
constexpr double RESUME_SECONDS = 600.0;
// Decoded stretches of talking kept, at most.
constexpr std::size_t SPEECH_KEPT = 64;
// How far ahead of the playhead sounds and talking start decoding (seconds of the demo), so
// most are in by the time they're due.
constexpr double DECODE_AHEAD_SECONDS = 1.5;
// Proximity voice is heard at this sound level from the talker (the engine's talking level).
constexpr double VOICE_SOUND_LEVEL = 80.0;

//-----------------------------------------------------------------------------
// Purpose: A sound level as the demo sends it: 9 bits, of which the top one isn't part of
//          the level (sounds the server plays at 70 dB come as 326); 0 is everywhere.
//-----------------------------------------------------------------------------
std::optional<double> SoundLevel( int nLevel )
{
	const int nDb = nLevel & 0xff;
	return nDb == 0 ? std::nullopt : std::optional<double>( nDb );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CDemoSound::CDemoSound( const CContents *pContents, const CPlayers &players, std::function<bool( int, int )> pfnPlayerMuted )
	: m_pContents( pContents ),
	  m_Players( players ),
	  m_pfnPlayerMuted( std::move( pfnPlayerMuted ) )
{
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CDemoSound::~CDemoSound() = default;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoSound::SetFiles( CHudEditor::ReadFile_t pfnRead, xv::audio::CEngine::Output_t nOutput )
{
	StopAll();
	m_pfnRead = std::move( pfnRead );
	m_nOutput = nOutput;
	m_bDecodeOnThreads = nOutput == xv::audio::CEngine::Output_t::OUTPUT_DEVICE;
	m_Speech.clear();
	m_SpeechDecoding.clear();
	m_Decoding.clear();
	m_Clips.clear();
	m_nClipBytes = 0;
	m_ClipSeconds.clear();
	m_SoundscapeFiles.clear();
	m_bSoundscapesRead = false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoSound::SetMap( const LogicMap_t *pMap )
{
	m_pMap = pMap;
	// The env_soundscapes that can be heard, read once: looking through every entity's keys
	// each frame cost milliseconds on a big map.
	m_SoundscapeSpots.clear();
	if ( !pMap )
		return;
	for ( const xv::bsp::Entity_t &entity : pMap->m_Entities )
	{
		if ( !xv::kv1::EqualsIgnoreCase( entity.Get( "classname" ), "env_soundscape" ) || entity.Get( "soundscape" ).empty() || entity.Get( "StartDisabled" ) == "1" )
			continue;
		double fl[ 3 ] = {};
		if ( std::sscanf( std::string( entity.Get( "origin" ) ).c_str(), "%lf %lf %lf", &fl[ 0 ], &fl[ 1 ], &fl[ 2 ] ) != 3 )
			continue;
		m_SoundscapeSpots.push_back( { Point_t{ fl[ 0 ], fl[ 1 ], fl[ 2 ] }, std::atof( std::string( entity.Get( "radius" ) ).c_str() ), std::string( entity.Get( "soundscape" ) ) } );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoSound::SetOn( bool bOn )
{
	m_bOn = bOn;
	if ( !bOn )
		StopAll();
	m_flLastTick = -1.0; // turned on again, the loops are picked up
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoSound::SetKindVolume( DemoSoundKind_t nKind, float flVolume )
{
	m_KindVolumes[ static_cast<std::size_t>( nKind ) ] = std::clamp( flVolume, 0.0f, 1.0f );
}

//-----------------------------------------------------------------------------
// Purpose: The engine's music marks ("#", and "*" after it) and the game's music folder.
//-----------------------------------------------------------------------------
DemoSoundKind_t CDemoSound::KindOf( std::string_view szName )
{
	while ( !szName.empty() && ( szName.front() == '*' || szName.front() == '^' || szName.front() == ')' || szName.front() == '(' || szName.front() == '<' || szName.front() == '>' || szName.front() == '@' || szName.front() == '!' || szName.front() == '?' ) )
		szName.remove_prefix( 1 );
	if ( szName.starts_with( '#' ) || szName.starts_with( "music/" ) || szName.starts_with( "music\\" ) )
		return DemoSoundKind_t::DEMO_SOUND_MUSIC;
	return DemoSoundKind_t::DEMO_SOUND_WORLD;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoSound::SetMuted( const std::string &szName, bool bMuted )
{
	if ( bMuted )
		m_Muted.insert( szName );
	else
		m_Muted.erase( szName );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoSound::SetVolume( float flVolume )
{
	m_flVolume = std::clamp( flVolume, 0.0f, 1.0f );
	if ( m_pEngine )
		m_pEngine->SetVolume( m_flVolume );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoSound::StopAll()
{
	if ( m_pEngine )
	{
		for ( const Playing_t &playing : m_Playing )
			m_pEngine->Stop( playing.m_nVoice );
	}
	m_Playing.clear();
	m_Waiting.clear();
	m_szSoundscape.clear();
	m_Simulation.reset();
}

//-----------------------------------------------------------------------------
// Purpose: The audio output, opened once.
//-----------------------------------------------------------------------------
bool CDemoSound::OpenEngine()
{
	if ( !m_bEngineTried )
	{
		m_bEngineTried = true;
		auto pEngine = std::make_unique<xv::audio::CEngine>( m_nOutput );
		if ( pEngine->GetOk() )
		{
			pEngine->SetVolume( m_flVolume );
			m_pEngine = std::move( pEngine );
		}
		else
		{
			qWarning( "demo sound: no audio output (%s)", pEngine->GetError().c_str() );
		}
	}
	return m_pEngine != nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: A game sound decoded, kept; null when it can't be read or decoded, or while it
//          decodes on a worker thread (IsDecoding). The file is read here: the game's file
//          system isn't for other threads.
//-----------------------------------------------------------------------------
std::shared_ptr<const xv::audio::CClip> CDemoSound::Clip( const std::string &szGamePath )
{
	if ( szGamePath.empty() || !m_pfnRead )
		return nullptr;
	if ( const auto found = m_Clips.find( szGamePath ); found != m_Clips.end() )
		return found->second;
	if ( IsDecoding( szGamePath ) )
		return nullptr;
	if ( m_Clips.size() >= CLIPS_KEPT || m_nClipBytes >= CLIP_BYTES_KEPT )
	{
		m_Clips.clear();
		m_nClipBytes = 0;
	}
	std::optional<std::vector<std::byte>> bytes = m_pfnRead( szGamePath );
	if ( bytes && m_bDecodeOnThreads )
	{
		m_Decoding.emplace( szGamePath, std::async( std::launch::async, [ data = std::move( *bytes ) ] { return xv::audio::Decode( data ); } ) );
		return nullptr;
	}
	std::shared_ptr<const xv::audio::CClip> pClip = bytes ? xv::audio::Decode( *bytes ) : nullptr;
	m_Clips[ szGamePath ] = pClip;
	if ( pClip )
	{
		m_nClipBytes += pClip->m_Samples.size() * sizeof( float );
		m_ClipSeconds[ szGamePath ] = pClip->GetSeconds();
	}
	return pClip;
}

//-----------------------------------------------------------------------------
// Purpose: The clips the workers have finished join the kept ones; each sound that waited
//          for one starts as far into it as it would be by now (one that plays once and
//          would be over is let go).
//-----------------------------------------------------------------------------
void CDemoSound::TakeDecoded( double flTick, double flSpeed )
{
	for ( auto it = m_Decoding.begin(); it != m_Decoding.end(); )
	{
		if ( it->second.wait_for( std::chrono::seconds( 0 ) ) != std::future_status::ready )
		{
			++it;
			continue;
		}
		std::shared_ptr<const xv::audio::CClip> pClip = it->second.get();
		if ( pClip )
		{
			m_nClipBytes += pClip->m_Samples.size() * sizeof( float );
			m_ClipSeconds[ it->first ] = pClip->GetSeconds();
		}
		m_Clips[ it->first ] = std::move( pClip );
		it = m_Decoding.erase( it );
	}
	for ( auto it = m_SpeechDecoding.begin(); it != m_SpeechDecoding.end(); )
	{
		if ( it->second.wait_for( std::chrono::seconds( 0 ) ) != std::future_status::ready )
		{
			++it;
			continue;
		}
		if ( m_Speech.size() >= SPEECH_KEPT )
			m_Speech.clear();
		m_Speech[ it->first ] = it->second.get();
		it = m_SpeechDecoding.erase( it );
	}
	if ( m_Waiting.empty() || !m_pContents )
		return;
	const double flInterval = m_pContents->GetTickInterval() > 0.0 ? m_pContents->GetTickInterval() : 0.015;
	std::vector<Waiting_t> waiting = std::move( m_Waiting );
	m_Waiting.clear();
	for ( Waiting_t &wait : waiting )
	{
		if ( wait.m_Speech )
		{
			if ( m_SpeechDecoding.contains( *wait.m_Speech ) )
				m_Waiting.push_back( std::move( wait ) );
			else if ( CanPlaySpeech( *wait.m_Speech ) )
				PlaySpeech( *wait.m_Speech, flTick, flSpeed );
			continue;
		}
		if ( IsDecoding( wait.m_szPath ) )
		{
			m_Waiting.push_back( std::move( wait ) );
			continue;
		}
		const auto found = m_Clips.find( wait.m_szPath );
		if ( found == m_Clips.end() || !found->second )
			continue;
		const double flPitch = wait.m_Sound ? wait.m_Sound->m_nPitch / 100.0 : wait.m_Loop->m_flPitch / 100.0;
		const double flLate = std::max( 0.0, ( flTick - wait.m_flTick ) * flInterval * flPitch );
		if ( wait.m_Sound )
			Start( *wait.m_Sound, flTick, flSpeed, wait.m_flStartSeconds + flLate );
		else
			StartSoundscapeLoop( *wait.m_Loop, found->second, wait.m_flStartSeconds + flLate );
	}
}

//-----------------------------------------------------------------------------
// Purpose: A player's eyes, else an entity's place as the demo has it; nullopt for one not
//          there.
//-----------------------------------------------------------------------------
std::optional<Point_t> CDemoSound::EntityPlace( int nEntity, double flTick ) const
{
	if ( nEntity <= 0 )
		return std::nullopt;
	if ( const auto player = m_Players.At( nEntity, flTick ) )
		return xv::map::ToPoint( player->GetEye() );
	if ( m_pContents )
	{
		if ( const EntityLife_t *pLife = m_pContents->LifeAt( nEntity, static_cast<int>( flTick ) ) )
		{
			const std::array<float, 3> at = pLife->ValueAt( ENTITY_FIELD_ORIGIN, static_cast<int>( flTick ) );
			return Point_t{ at[ 0 ], at[ 1 ], at[ 2 ] };
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: A sound started: a stop ends what it names; anything else replaces what its
//          entity plays on its channel (but the auto and static ones), then plays at its
//          pitch (and the playback's speed), from where it is.
//-----------------------------------------------------------------------------
void CDemoSound::Start( const Sound_t &sound, double flTick, double flSpeed, double flStartSeconds )
{
	// A change of volume or pitch alters the same sound where it plays; only where it
	// doesn't is it started.
	if ( sound.IsChange() )
	{
		const auto playing = std::ranges::find_if( m_Playing, [ & ]( const Playing_t &p ) { return !p.m_bSoundscape && p.m_nEntity == sound.m_nEntity && p.m_nChannel == sound.m_nChannel && p.m_nSound == sound.m_nSound; } );
		if ( playing != m_Playing.end() )
		{
			if ( sound.m_nFlags & SOUND_FLAG_CHANGE_VOLUME )
				playing->m_flVolume = sound.m_flVolume;
			if ( sound.m_nFlags & SOUND_FLAG_CHANGE_PITCH )
				m_pEngine->SetPitch( playing->m_nVoice, static_cast<float>( sound.m_nPitch / 100.0 * flSpeed ) );
			return;
		}
		// Started silent, it would be heard only once turned up; it can wait for that.
		if ( sound.m_flVolume <= 0.0f )
			return;
	}
	const bool bReplaces = sound.m_nChannel != CHANNEL_AUTO && sound.m_nChannel != CHANNEL_STATIC;
	std::erase_if( m_Playing, [ & ]( const Playing_t &playing ) {
		const bool bSame = !playing.m_bSoundscape && playing.m_nEntity == sound.m_nEntity && playing.m_nChannel == sound.m_nChannel;
		const bool bEnds = bSame && ( sound.IsStop() ? ( sound.m_nSound == 0 || playing.m_nSound == sound.m_nSound ) : bReplaces );
		if ( bEnds )
			m_pEngine->Stop( playing.m_nVoice );
		return bEnds;
	} );
	// What waits for its clip on that channel ends the same way.
	std::erase_if( m_Waiting, [ & ]( const Waiting_t &wait ) {
		if ( !wait.m_Sound )
			return false;
		const Sound_t &other = *wait.m_Sound;
		const bool bSame = other.m_nEntity == sound.m_nEntity && other.m_nChannel == sound.m_nChannel;
		return bSame && ( sound.IsStop() ? ( sound.m_nSound == 0 || other.m_nSound == sound.m_nSound ) : bReplaces );
	} );
	if ( sound.IsStop() || sound.m_bSentence || !m_pContents )
		return;
	const std::string szName = LowerName( m_pContents->SoundName( sound ) );
	if ( m_Muted.contains( szName ) )
		return;
	const std::string szPath = xv::sound::WaveGamePath( m_pContents->SoundName( sound ) );
	const std::shared_ptr<const xv::audio::CClip> pClip = Clip( szPath );
	if ( !pClip )
	{
		if ( IsDecoding( szPath ) )
			m_Waiting.push_back( { szPath, flTick, flStartSeconds, sound, std::nullopt, std::nullopt } );
		return;
	}
	// Started late (its clip was decoding), it may have played out or looped round by now.
	const double flLength = pClip->GetSeconds();
	if ( flStartSeconds >= flLength )
	{
		const double flLoopFrom = pClip->m_LoopStart && pClip->m_nSampleRate ? static_cast<double>( *pClip->m_LoopStart ) / pClip->m_nSampleRate : -1.0;
		if ( flLoopFrom < 0.0 || flLength <= flLoopFrom )
			return;
		flStartSeconds = flLoopFrom + std::fmod( flStartSeconds - flLoopFrom, flLength - flLoopFrom );
	}
	xv::audio::PlayParams_t params;
	params.m_flVolume = 0.0f; // the mix sets it, from where it is
	params.m_flPitch = static_cast<float>( sound.m_nPitch / 100.0 * flSpeed );
	params.m_bLoop = pClip->m_LoopStart.has_value();
	params.m_flDelaySeconds = std::max( 0.0f, sound.m_flDelay );
	params.m_flStartSeconds = flStartSeconds;
	Playing_t playing;
	playing.m_nVoice = m_pEngine->Play( pClip, params );
	if ( playing.m_nVoice == 0 )
		return;
	playing.m_nEntity = sound.m_nEntity;
	playing.m_nChannel = sound.m_nChannel;
	playing.m_nSound = sound.m_nSound;
	playing.m_flVolume = sound.m_flVolume;
	playing.m_szName = szName;
	playing.m_nKind = KindOf( szName );
	playing.m_SoundLevel = SoundLevel( sound.m_nSoundLevel );
	const xv::CVec3 &at = sound.m_vecOrigin;
	const bool bPlace = !( at.m_flX == 0.0f && at.m_flY == 0.0f && at.m_flZ == 0.0f ) && std::abs( at.m_flX ) < PLACE_MOST && std::abs( at.m_flY ) < PLACE_MOST && std::abs( at.m_flZ ) < PLACE_MOST;
	// A sound on a moving thing (a player's steps, a train) goes with it; a fixed one stays.
	if ( bPlace && ( sound.m_nEntity <= 0 || !EntityPlace( sound.m_nEntity, flTick ) ) )
		playing.m_Origin = xv::map::ToPoint( at );
	playing.m_bPositioned = playing.m_SoundLevel.has_value() && ( playing.m_Origin || sound.m_nEntity > 0 );
	m_Playing.push_back( playing );
	m_nStarted++;
}

//-----------------------------------------------------------------------------
// Purpose: Each voice's loudness and pan from the camera, as the engine falls off; the
//          finished ones let go.
//-----------------------------------------------------------------------------
void CDemoSound::Remix( double flTick, const xv::map::CViewCamera &camera, int nEar )
{
	const Point_t right = camera.GetRight();
	const Point_t forward = camera.GetForward();
	const Point_t up = camera.GetUp();
	std::erase_if( m_Playing, [ & ]( const Playing_t &playing ) { return !m_pEngine->Playing( playing.m_nVoice ); } );
	for ( const Playing_t &playing : m_Playing )
	{
		xv::sound::PositionMix_t mix;
		std::optional<Point_t> at = playing.m_Origin;
		if ( !at && playing.m_nEntity != nEar )
			at = EntityPlace( playing.m_nEntity, flTick );
		if ( playing.m_bPositioned && at && playing.m_nEntity != nEar )
		{
			const Point_t away = xv::map::Subtract( *at, camera.m_Eye );
			mix = xv::sound::MixAt( playing.m_SoundLevel, xv::map::Dot( away, right ), std::hypot( xv::map::Dot( away, forward ), xv::map::Dot( away, up ) ) );
		}
		const DemoSoundKind_t nKind = playing.m_Speech ? DemoSoundKind_t::DEMO_SOUND_VOICE : playing.m_bSoundscape ? DemoSoundKind_t::DEMO_SOUND_AMBIENT
																												   : playing.m_nKind;
		const float flKind = m_KindVolumes[ static_cast<std::size_t>( nKind ) ] * ( !playing.m_szName.empty() && m_Muted.contains( playing.m_szName ) ? 0.0f : 1.0f );
		m_pEngine->SetMix( playing.m_nVoice, static_cast<float>( playing.m_flVolume * mix.m_flGain * flKind ), static_cast<float>( mix.m_flPan ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Every sound started before the tick and not stopped (nor replaced on its channel,
//          nor turned down to nothing) since, that loops or hasn't come to its end, from as
//          far into it as it would be.
//-----------------------------------------------------------------------------
void CDemoSound::ResumeLoops( double flTick, double flSpeed )
{
	if ( !m_pContents )
		return;
	const std::vector<Sound_t> &sounds = m_pContents->GetSounds();
	std::map<std::pair<int, int>, const Sound_t *> latest; // entity and channel -> what plays there
	std::map<std::pair<int, int>, float> volumes;          // and its volume, where a change set it
	for ( const Sound_t &sound : sounds )
	{
		if ( sound.m_nTick > flTick )
			break;
		const std::pair<int, int> key{ sound.m_nEntity, sound.m_nChannel * 100000 + ( sound.m_nChannel == CHANNEL_STATIC || sound.m_nChannel == CHANNEL_AUTO ? sound.m_nSound : 0 ) };
		if ( sound.IsStop() )
		{
			if ( sound.m_nSound == 0 )
				std::erase_if( latest, [ & ]( const auto &entry ) { return entry.first.first == sound.m_nEntity && entry.first.second / 100000 == sound.m_nChannel; } );
			else
				latest.erase( { sound.m_nEntity, sound.m_nChannel * 100000 + ( sound.m_nChannel == CHANNEL_STATIC || sound.m_nChannel == CHANNEL_AUTO ? sound.m_nSound : 0 ) } );
			continue;
		}
		// A change alters what plays there (its volume now), or starts it when nothing does.
		if ( sound.IsChange() )
		{
			if ( const auto found = latest.find( key ); found != latest.end() && found->second->m_nSound == sound.m_nSound )
			{
				volumes[ key ] = sound.m_flVolume;
				continue;
			}
			if ( sound.m_flVolume <= 0.0f )
				continue;
		}
		latest[ key ] = &sound;
		volumes.erase( key );
	}
	const double flInterval = m_pContents->GetTickInterval();
	for ( const auto &[ key, pSound ] : latest )
	{
		// A sound heard before that plays once and is over by now isn't decoded again.
		const std::string szPath = xv::sound::WaveGamePath( m_pContents->SoundName( *pSound ) );
		const double flSince = ( flTick - pSound->m_nTick ) * flInterval * pSound->m_nPitch / 100.0;
		if ( const auto known = m_ClipSeconds.find( szPath ); known != m_ClipSeconds.end() && flSince >= known->second && !m_Clips.contains( szPath ) )
			continue;
		if ( flSince > RESUME_SECONDS && !m_Clips.contains( szPath ) )
			continue;
		const std::shared_ptr<const xv::audio::CClip> pClip = Clip( szPath );
		const double flIn = ( flTick - pSound->m_nTick ) * flInterval * pSound->m_nPitch / 100.0;
		if ( !pClip )
		{
			// Still decoding: it waits, and starts from as far in as it is by then (Start
			// wraps a loop, and lets go of one that's over).
			if ( IsDecoding( szPath ) )
			{
				Sound_t sound = *pSound;
				sound.m_nFlags &= ~( SOUND_FLAG_CHANGE_VOLUME | SOUND_FLAG_CHANGE_PITCH );
				if ( const auto volume = volumes.find( key ); volume != volumes.end() )
					sound.m_flVolume = volume->second;
				if ( sound.m_flVolume > 0.0f )
					Start( sound, flTick, flSpeed, flIn );
			}
			continue;
		}
		const double flLength = pClip->GetSeconds();
		// One that plays once is picked up while it would still be playing (a map's music).
		if ( !pClip->m_LoopStart && flIn >= flLength )
			continue;
		const double flLoopFrom = pClip->m_nSampleRate ? static_cast<double>( *pClip->m_LoopStart ) / pClip->m_nSampleRate : 0.0;
		double flAt = flIn;
		if ( flAt > flLength && flLength > flLoopFrom )
			flAt = flLoopFrom + std::fmod( flAt - flLoopFrom, flLength - flLoopFrom );
		Sound_t sound = *pSound;
		sound.m_nFlags &= ~( SOUND_FLAG_CHANGE_VOLUME | SOUND_FLAG_CHANGE_PITCH );
		if ( const auto volume = volumes.find( key ); volume != volumes.end() )
			sound.m_flVolume = volume->second;
		if ( sound.m_flVolume <= 0.0f )
			continue;
		Start( sound, flTick, flSpeed, std::clamp( flAt, 0.0, std::max( 0.0, flLength - 0.01 ) ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: A stretch of talking decoded, kept; null when the codec isn't decoded.
//-----------------------------------------------------------------------------
std::shared_ptr<const xv::audio::CClip> CDemoSound::SpeechClip( std::size_t nSpeech )
{
	if ( const auto found = m_Speech.find( nSpeech ); found != m_Speech.end() )
		return found->second;
	if ( m_SpeechDecoding.contains( nSpeech ) )
		return nullptr;
	if ( m_Speech.size() >= SPEECH_KEPT )
		m_Speech.clear();
	const Speech_t &speech = m_pContents->GetSpeech()[ nSpeech ];
	// The contents outlive this (the viewer holds them longer), and nothing in them changes.
	if ( m_bDecodeOnThreads )
	{
		m_SpeechDecoding.emplace( nSpeech, std::async( std::launch::async, [ pContents = m_pContents, speech ] { return SpeechToClip( *pContents, speech ); } ) );
		return nullptr;
	}
	std::shared_ptr<const xv::audio::CClip> pClip = SpeechToClip( *m_pContents, speech );
	m_Speech[ nSpeech ] = pClip;
	return pClip;
}

//-----------------------------------------------------------------------------
// Purpose: The talking's samples as a clip; null when nothing decodes.
//-----------------------------------------------------------------------------
std::shared_ptr<const xv::audio::CClip> CDemoSound::SpeechToClip( const xv::demo::CContents &contents, const xv::demo::Speech_t &speech )
{
	int nRate = 0;
	std::vector<float> samples = DecodeSpeech( contents, speech, &nRate );
	if ( samples.empty() || nRate <= 0 )
		return nullptr;
	auto pDecoded = std::make_shared<xv::audio::CClip>();
	pDecoded->m_Samples = std::move( samples );
	pDecoded->m_nChannels = 1;
	pDecoded->m_nSampleRate = static_cast<std::uint32_t>( nRate );
	return pDecoded;
}

//-----------------------------------------------------------------------------
// Purpose: Whether the speech exists and its talker can be heard.
//-----------------------------------------------------------------------------
bool CDemoSound::CanPlaySpeech( std::size_t nSpeech ) const
{
	if ( !m_pContents || nSpeech >= m_pContents->GetSpeech().size() )
		return false;
	const Speech_t &speech = m_pContents->GetSpeech()[ nSpeech ];
	return !m_pfnPlayerMuted || !m_pfnPlayerMuted( speech.m_nEntity, speech.m_nFrom );
}

//-----------------------------------------------------------------------------
// Purpose: Silence muted voices without interrupting other audio.
//-----------------------------------------------------------------------------
void CDemoSound::ApplyPlayerMutes()
{
	if ( !m_pEngine )
		return;
	if ( m_nAlone && !CanPlaySpeech( m_nAloneSpeech ) )
		StopSpeech();
	std::erase_if( m_Playing, [ this ]( const Playing_t &playing ) {
		if ( !playing.m_Speech || CanPlaySpeech( *playing.m_Speech ) )
			return false;
		m_pEngine->Stop( playing.m_nVoice );
		return true;
	} );
}

//-----------------------------------------------------------------------------
// Purpose: Play the clip alone at full volume, centred, at its own speed.
//-----------------------------------------------------------------------------
bool CDemoSound::PlaySpeech( std::size_t nSpeech )
{
	if ( !CanPlaySpeech( nSpeech ) || !OpenEngine() )
		return false;
	StopSpeech();
	const std::shared_ptr<const xv::audio::CClip> pClip = SpeechClip( nSpeech );
	if ( !pClip )
		return false;
	m_nAloneSpeech = nSpeech;
	m_nAlone = m_pEngine->Play( pClip, {} );
	return m_nAlone != 0;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoSound::StopSpeech()
{
	if ( m_pEngine && m_nAlone != 0 )
		m_pEngine->Stop( m_nAlone );
	m_nAlone = 0;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CDemoSound::IsPlayingSpeech() const
{
	return m_pEngine && m_nAlone != 0 && m_pEngine->Playing( m_nAlone );
}

//-----------------------------------------------------------------------------
// Purpose: Each stretch of talking that starts in (flFrom, flTo], and with bResume the ones
//          under way at flTo, played from where they'd be: flat as the game plays voice,
//          or from the talker when it was sent as proximity voice.
//-----------------------------------------------------------------------------
void CDemoSound::StartSpeech( double flFrom, double flTo, double flSpeed, bool bResume )
{
	if ( !m_bVoiceDecoded )
		m_bVoiceDecoded = xv::demo::CVoiceDecoder( m_pContents->GetVoiceCodec() ).IsSupported();
	if ( !*m_bVoiceDecoded )
		return;
	const double flInterval = m_pContents->GetTickInterval();
	const std::vector<Speech_t> &speech = m_pContents->GetSpeech();
	for ( std::size_t i = 0; i < speech.size() && speech[ i ].m_nFrom <= flTo; i++ )
	{
		const Speech_t &talk = speech[ i ];
		const bool bStarts = talk.m_nFrom > flFrom || ( bResume && talk.m_nFrom == flTo );
		const bool bUnderWay = bResume && talk.m_nFrom <= flTo && talk.m_nTo >= flTo - 1;
		if ( ( !bStarts && !bUnderWay ) || !CanPlaySpeech( i ) )
			continue;
		PlaySpeech( i, flTo, flSpeed );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoSound::PlaySpeech( std::size_t nSpeech, double flTick, double flSpeed )
{
	const Speech_t &talk = m_pContents->GetSpeech()[ nSpeech ];
	const std::shared_ptr<const xv::audio::CClip> pClip = SpeechClip( nSpeech );
	if ( !pClip )
	{
		if ( m_SpeechDecoding.contains( nSpeech ) )
			m_Waiting.push_back( { {}, flTick, 0.0, std::nullopt, std::nullopt, nSpeech } );
		return;
	}
	const double flIn = std::max( 0.0, ( flTick - talk.m_nFrom ) * m_pContents->GetTickInterval() );
	if ( flIn >= pClip->GetSeconds() )
		return;
	xv::audio::PlayParams_t params;
	params.m_flVolume = 0.0f; // the mix sets it
	params.m_flPitch = static_cast<float>( flSpeed );
	params.m_flStartSeconds = flIn;
	Playing_t playing;
	playing.m_nVoice = m_pEngine->Play( pClip, params );
	if ( playing.m_nVoice == 0 )
		return;
	playing.m_nEntity = talk.m_nEntity;
	playing.m_nChannel = -1;
	playing.m_Speech = nSpeech;
	const std::vector<VoicePacket_t> &packets = m_pContents->GetVoicePackets();
	const auto first = std::ranges::lower_bound( packets, talk.m_nFrom, {}, &VoicePacket_t::m_nTick );
	const bool bProximity = first != packets.end() && first->m_bProximity;
	playing.m_bPositioned = bProximity;
	if ( bProximity )
		playing.m_SoundLevel = VOICE_SOUND_LEVEL;
	m_Playing.push_back( playing );
	m_nVoices++;
}

//-----------------------------------------------------------------------------
// Purpose: The sounds and talking that start in the next DECODE_AHEAD_SECONDS of the demo
//          (at this speed) set decoding now, so they're in when they're due.
//-----------------------------------------------------------------------------
void CDemoSound::DecodeAhead( double flTick, double flSpeed )
{
	if ( !m_bDecodeOnThreads || !m_pContents )
		return;
	const double flInterval = m_pContents->GetTickInterval() > 0.0 ? m_pContents->GetTickInterval() : 0.015;
	const double flUntil = flTick + DECODE_AHEAD_SECONDS * std::max( 1.0, flSpeed ) / flInterval;
	const std::vector<Sound_t> &sounds = m_pContents->GetSounds();
	const auto from = std::upper_bound( sounds.begin(), sounds.end(), flTick, []( double flAt, const Sound_t &sound ) { return flAt < sound.m_nTick; } );
	for ( auto sound = from; sound != sounds.end() && sound->m_nTick <= flUntil; ++sound )
	{
		if ( sound->IsStop() || sound->m_bSentence || m_Muted.contains( LowerName( m_pContents->SoundName( *sound ) ) ) )
			continue;
		(void)Clip( xv::sound::WaveGamePath( m_pContents->SoundName( *sound ) ) );
	}
	if ( !m_bVoiceDecoded || !*m_bVoiceDecoded )
		return;
	const std::vector<Speech_t> &speech = m_pContents->GetSpeech();
	for ( std::size_t i = 0; i < speech.size() && speech[ i ].m_nFrom <= flUntil; i++ )
	{
		if ( speech[ i ].m_nFrom > flTick && CanPlaySpeech( i ) )
			(void)SpeechClip( i );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The game's soundscape files (the manifest's, and the map's own), read once; a
//          soundscape by name from them.
//-----------------------------------------------------------------------------
const xv::kv1::CNode *CDemoSound::FindSoundscape( std::string_view szName )
{
	if ( !m_bSoundscapesRead && m_pfnRead )
	{
		m_bSoundscapesRead = true;
		std::vector<std::string> files;
		if ( m_pContents && m_pContents->GetServerInfo() )
			files.push_back( "scripts/soundscapes_" + m_pContents->GetServerInfo()->m_szMap + ".txt" );
		if ( const auto manifest = m_pfnRead( "scripts/soundscapes_manifest.txt" ) )
		{
			const xv::kv1::CDocument document = xv::kv1::Parse( std::string_view( reinterpret_cast<const char *>( manifest->data() ), manifest->size() ) ).m_Document;
			for ( const xv::kv1::CNode &root : document.m_Roots )
			{
				if ( !root.IsBlock() )
					continue;
				for ( const xv::kv1::CNode &child : root.GetChildren() )
				{
					if ( child.IsPair() && xv::kv1::EqualsIgnoreCase( child.GetKey(), "file" ) )
						files.push_back( std::string( child.GetValue() ) );
				}
			}
		}
		for ( const std::string &szFile : files )
		{
			if ( const auto bytes = m_pfnRead( szFile ) )
				m_SoundscapeFiles.push_back( xv::kv1::Parse( std::string_view( reinterpret_cast<const char *>( bytes->data() ), bytes->size() ) ).m_Document );
		}
	}
	for ( const xv::kv1::CDocument &document : m_SoundscapeFiles )
	{
		for ( const xv::kv1::CNode &root : document.m_Roots )
		{
			if ( root.IsBlock() && xv::kv1::EqualsIgnoreCase( root.GetKey(), szName ) )
				return &root;
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: The env_soundscape nearest the camera whose radius takes it in (as the server
//          picks one for a player, without its line of sight); the one before while none
//          does. Its loops and random sounds play everywhere, a positioned one from the
//          env_soundscape's position target.
//-----------------------------------------------------------------------------
void CDemoSound::UpdateSoundscape( double flTick, const xv::map::CViewCamera &camera )
{
	if ( !m_pMap )
		return;
	std::string szBest;
	double flBest = std::numeric_limits<double>::infinity();
	const SoundscapeSpot_t *pBest = nullptr;
	for ( const SoundscapeSpot_t &spot : m_SoundscapeSpots )
	{
		const double flAway = xv::map::Length( xv::map::Subtract( camera.m_Eye, spot.m_At ) );
		if ( ( spot.m_flRadius < 0.0 || flAway <= spot.m_flRadius ) && flAway < flBest )
		{
			flBest = flAway;
			szBest = spot.m_szName;
			pBest = &spot;
		}
	}
	if ( !pBest || xv::kv1::EqualsIgnoreCase( szBest, m_szSoundscape ) )
	{
		if ( m_Simulation && m_SoundscapeClock.isValid() )
		{
			const double flSeconds = m_SoundscapeClock.restart() / 1000.0;
			for ( const xv::sound::PlayRequest_t &request : m_Simulation->Advance( flSeconds ) )
			{
				if ( const auto pClip = Clip( request.m_szGamePath ) )
				{
					xv::audio::PlayParams_t params;
					params.m_flVolume = static_cast<float>( request.m_flVolume );
					params.m_flPitch = static_cast<float>( request.m_flPitch / 100.0 );
					m_pEngine->Play( pClip, params );
				}
			}
		}
		return;
	}
	// Another soundscape: the last one's sounds stop.
	std::erase_if( m_Playing, [ & ]( const Playing_t &playing ) {
		if ( playing.m_bSoundscape )
			m_pEngine->Stop( playing.m_nVoice );
		return playing.m_bSoundscape;
	} );
	std::erase_if( m_Waiting, []( const Waiting_t &wait ) { return wait.m_Loop.has_value(); } );
	m_szSoundscape = szBest;
	m_Simulation.reset();
	const xv::kv1::CNode *pNode = FindSoundscape( szBest );
	if ( !pNode )
		return;
	const xv::sound::SoundscapePlan_t plan = xv::sound::PlanSoundscape( *pNode, [ this ]( std::string_view szName ) { return FindSoundscape( szName ); } );
	m_Simulation.emplace( plan.m_Rules, 1u );
	m_SoundscapeClock.restart();
	for ( const xv::sound::PlayRequest_t &request : m_Simulation->Start() )
	{
		if ( const auto pClip = Clip( request.m_szGamePath ) )
			StartSoundscapeLoop( request, pClip, 0.0 );
		else if ( IsDecoding( request.m_szGamePath ) )
			m_Waiting.push_back( { request.m_szGamePath, flTick, 0.0, std::nullopt, request, std::nullopt } );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoSound::StartSoundscapeLoop( const xv::sound::PlayRequest_t &request, std::shared_ptr<const xv::audio::CClip> pClip, double flStartSeconds )
{
	const double flLength = pClip->GetSeconds();
	if ( flStartSeconds >= flLength )
	{
		if ( !request.m_bLoop || flLength <= 0.0 )
			return;
		flStartSeconds = std::fmod( flStartSeconds, flLength );
	}
	xv::audio::PlayParams_t params;
	params.m_flVolume = static_cast<float>( request.m_flVolume );
	params.m_flPitch = static_cast<float>( request.m_flPitch / 100.0 );
	params.m_bLoop = request.m_bLoop;
	params.m_flStartSeconds = flStartSeconds;
	Playing_t playing;
	playing.m_nVoice = m_pEngine->Play( std::move( pClip ), params );
	playing.m_bPositioned = false;
	playing.m_flVolume = static_cast<float>( request.m_flVolume );
	playing.m_bSoundscape = true;
	if ( playing.m_nVoice )
		m_Playing.push_back( playing );
}

//-----------------------------------------------------------------------------
// Purpose: Playing on: the sounds the playhead passed start. A jump (or starting to play):
//          everything stops and the loops that would be going start again. Paused, or too
//          fast or slow to hear: silence.
//-----------------------------------------------------------------------------
void CDemoSound::Update( double flTick, bool bPlaying, double flSpeed, const xv::map::CViewCamera &camera, int nEar )
{
	const bool bHeard = m_bOn && bPlaying && flSpeed >= SPEED_LEAST && flSpeed <= SPEED_MOST && m_pContents && m_pfnRead;
	if ( !bHeard )
	{
		if ( m_bWasPlaying || !m_Playing.empty() )
			StopAll();
		m_bWasPlaying = false;
		m_flLastTick = flTick;
		return;
	}
	if ( !OpenEngine() )
		return;
	TakeDecoded( flTick, flSpeed );
	const double flInterval = m_pContents->GetTickInterval() > 0.0 ? m_pContents->GetTickInterval() : 0.015;
	const bool bStep = m_bWasPlaying && m_flLastTick >= 0.0 && flTick >= m_flLastTick && ( flTick - m_flLastTick ) * flInterval <= STEP_MOST_SECONDS * std::max( 1.0, flSpeed );
	if ( !bStep )
	{
		StopAll();
		ResumeLoops( flTick, flSpeed );
		StartSpeech( flTick, flTick, flSpeed, true );
	}
	else
	{
		const std::vector<Sound_t> &sounds = m_pContents->GetSounds();
		const auto from = std::upper_bound( sounds.begin(), sounds.end(), m_flLastTick, []( double flAt, const Sound_t &sound ) { return flAt < sound.m_nTick; } );
		for ( auto sound = from; sound != sounds.end() && sound->m_nTick <= flTick; ++sound )
			Start( *sound, flTick, flSpeed );
		StartSpeech( m_flLastTick, flTick, flSpeed, false );
	}
	UpdateSoundscape( flTick, camera );
	Remix( flTick, camera, nEar );
	DecodeAhead( flTick, flSpeed );
	m_bWasPlaying = true;
	m_flLastTick = flTick;
}
