//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See SoundPlayer.h.
//
//===========================================================================//
#include "SoundPlayer.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>

#include <QStringList>
#include <QTimer>

#include "vfs/Vfs.h"

using xv::sound::PlayRequest_t;

namespace
{

constexpr std::size_t CACHE_LIMIT = 256u * 1024 * 1024; // bytes of decoded audio kept around

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::mt19937 &RandomEngine()
{
	static std::mt19937 engine{ std::random_device{}() };
	return engine;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Describe( const PlayRequest_t &request )
{
	QStringList parts{ QString::fromStdString( request.m_szWave ) };
	if ( request.m_flVolume != 1 )
	{
		parts << QObject::tr( "volume %1" ).arg( request.m_flVolume, 0, 'g', 3 );
	}
	if ( request.m_flPitch != 100 )
	{
		parts << QObject::tr( "pitch %1" ).arg( request.m_flPitch, 0, 'f', 0 );
	}
	if ( request.m_bLoop )
	{
		parts << QObject::tr( "looping" );
	}
	if ( request.m_flDelaySeconds > 0 )
	{
		parts << QObject::tr( "after %1 s" ).arg( request.m_flDelaySeconds, 0, 'g', 3 );
	}
	return parts.join( QStringLiteral( " · " ) );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Tests pass Output::Offline: everything works, nothing is heard.
//-----------------------------------------------------------------------------
CSoundPlayer::CSoundPlayer( const xv::vfs::CFileSystem &fileSystem, QObject *pParent, xv::audio::CEngine::Output_t nOutput )
	: QObject( pParent ),
	  m_FileSystem( fileSystem ),
	  m_nOutput( nOutput ),
	  m_pTimer( new QTimer( this ) )
{
	m_pTimer->setInterval( 50 );
	connect( m_pTimer, &QTimer::timeout, this, &CSoundPlayer::Tick );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CSoundPlayer::~CSoundPlayer() = default;

//-----------------------------------------------------------------------------
// Purpose: False when no audio output can be opened; GetReason() says why.
//-----------------------------------------------------------------------------
bool CSoundPlayer::Available()
{
	if ( !m_bTriedEngine )
	{
		m_bTriedEngine = true;
		m_pEngine = std::make_unique<xv::audio::CEngine>( m_nOutput );
		if ( m_pEngine->GetOk() )
		{
			m_pEngine->SetVolume( m_flVolume );
		}
		else
		{
			m_szReason = tr( "No audio output: %1" ).arg( QString::fromStdString( m_pEngine->GetError() ) );
			m_pEngine.reset();
		}
	}
	if ( !m_pEngine )
	{
		emit Failed( m_szReason );
	}
	return m_pEngine != nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Game files from the mounted game, else from the mod folder a file was opened from.
//-----------------------------------------------------------------------------
std::optional<std::vector<std::byte>> CSoundPlayer::ReadGameFile( const xv::vfs::CFileSystem &fileSystem, const std::string &szGamePath, const std::filesystem::path &localRoot )
{
	if ( auto bytes = fileSystem.Read( szGamePath ) )
	{
		return bytes;
	}
	if ( localRoot.empty() )
	{
		return std::nullopt;
	}
	const auto file = xv::vfs::FindCaseInsensitive( localRoot / xv::vfs::FromUtf8( szGamePath ) );
	std::ifstream stream{ file, std::ios::binary };
	if ( !stream )
	{
		return std::nullopt;
	}
	const std::vector<char> chars{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
	std::vector<std::byte> bytes( chars.size() );
	std::memcpy( bytes.data(), chars.data(), chars.size() );
	return bytes;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::shared_ptr<const xv::audio::CClip> CSoundPlayer::DecodeCached( const std::string &szKey, std::span<const std::byte> bytes, QString *pError )
{
	if ( const auto it = m_Cache.find( szKey ); it != m_Cache.end() )
	{
		return it->second;
	}
	std::string szMessage;
	auto pClip = xv::audio::Decode( bytes, &szMessage );
	if ( !pClip )
	{
		if ( pError )
		{
			*pError = QString::fromStdString( szMessage );
		}
		return nullptr;
	}
	m_Cache.emplace( szKey, pClip );
	m_CacheOrder.push_back( szKey );
	m_nCacheBytes += pClip->m_Samples.size() * sizeof( float );
	while ( m_nCacheBytes > CACHE_LIMIT && m_CacheOrder.size() > 1 )
	{
		const auto evicted = m_Cache.find( m_CacheOrder.front() );
		m_nCacheBytes -= evicted->second->m_Samples.size() * sizeof( float );
		m_Cache.erase( evicted );
		m_CacheOrder.pop_front();
	}
	return pClip;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::shared_ptr<const xv::audio::CClip> CSoundPlayer::Load( const std::string &szGamePath, const std::filesystem::path &localRoot )
{
	const std::string szKey = szGamePath + '\n' + xv::vfs::ToUtf8( localRoot );
	if ( const auto it = m_Cache.find( szKey ); it != m_Cache.end() )
	{
		return it->second;
	}
	const auto bytes = ReadGameFile( m_FileSystem, szGamePath, localRoot );
	if ( !bytes )
	{
		emit Failed( tr( "%1 isn't in the game" ).arg( QString::fromStdString( szGamePath ) ) );
		return nullptr;
	}
	QString szError;
	auto pClip = DecodeCached( szKey, *bytes, &szError );
	if ( !pClip )
	{
		emit Failed( tr( "Can't play %1: %2" ).arg( QString::fromStdString( szGamePath ), szError ) );
	}
	return pClip;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::audio::CEngine::Voice_t CSoundPlayer::Start( std::shared_ptr<const xv::audio::CClip> pClip, const PlayRequest_t &request, double flStartSeconds )
{
	if ( !pClip || !m_pEngine )
	{
		return 0;
	}
	xv::audio::PlayParams_t params;
	const xv::sound::PositionMix_t mix = MixFor( request );
	params.m_flVolume = static_cast<float>( request.m_flVolume * mix.m_flGain );
	params.m_flPitch = static_cast<float>( request.m_flPitch / 100.0 );
	params.m_bLoop = request.m_bLoop;
	params.m_flDelaySeconds = request.m_flDelaySeconds;
	params.m_flStartSeconds = flStartSeconds;
	params.m_flPan = static_cast<float>( mix.m_flPan );
	// "position random" puts each sound somewhere around the listener; a stereo pan stands in for that.
	if ( request.m_bRandomPosition )
	{
		params.m_flPan = std::uniform_real_distribution<float>{ -0.8f, 0.8f }( RandomEngine() );
	}
	const auto nVoice = m_pEngine->Play( std::move( pClip ), params );
	if ( nVoice == 0 )
	{
		emit Failed( tr( "Could not play %1" ).arg( QString::fromStdString( request.m_szWave ) ) );
		return 0;
	}
	SetActive( true );
	if ( request.m_Position && *request.m_Position >= 0 && *request.m_Position < 8 )
	{
		m_Positioned.emplace( nVoice, request );
		emit PositionHeard( *request.m_Position );
	}
	return nVoice;
}

//-----------------------------------------------------------------------------
// Purpose: Positions outside 0-7, or with listening off, play as written.
//-----------------------------------------------------------------------------
xv::sound::PositionMix_t CSoundPlayer::MixFor( const PlayRequest_t &request ) const
{
	if ( !m_Scene.m_bOn || !request.m_Position || *request.m_Position < 0 || *request.m_Position >= 8 )
	{
		return {};
	}
	const QPointF offset = m_Scene.m_Positions[ static_cast<std::size_t>( *request.m_Position ) ] - m_Scene.m_Listener;
	return xv::sound::MixAt( request.m_SoundLevel, offset.x(), offset.y() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundPlayer::SetListenScene( const ListenScene_t &scene )
{
	m_Scene = scene;
	if ( !m_pEngine )
	{
		return;
	}
	for ( auto it = m_Positioned.begin(); it != m_Positioned.end(); )
	{
		if ( !m_pEngine->Playing( it->first ) )
		{
			it = m_Positioned.erase( it );
			continue;
		}
		const xv::sound::PositionMix_t mix = MixFor( it->second );
		m_pEngine->SetMix( it->first, static_cast<float>( it->second.m_flVolume * mix.m_flGain ), static_cast<float>( mix.m_flPan ) );
		++it;
	}
}

//-----------------------------------------------------------------------------
// Purpose: A sound file's bytes as they are, from startSeconds on; loop uses the file's loop
//          point.
//-----------------------------------------------------------------------------
bool CSoundPlayer::PlayData( const QString &szName, std::span<const std::byte> bytes, bool bLoop, double flStartSeconds )
{
	if ( !Available() )
	{
		return false;
	}
	Stop();
	QString szError;
	auto pClip = DecodeCached( "file:" + szName.toStdString() + ':' + std::to_string( bytes.size() ), bytes, &szError );
	if ( !pClip )
	{
		emit Failed( tr( "Can't play %1: %2" ).arg( szName, szError ) );
		return false;
	}
	PlayRequest_t request;
	request.m_szWave = szName.toStdString();
	request.m_bLoop = bLoop;
	m_nLastVoice = Start( std::move( pClip ), request, flStartSeconds );
	if ( m_nLastVoice == 0 )
	{
		return false;
	}
	emit Started( Describe( request ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: A game sound ("sound/ambient/wind.wav") as it is, from the game or a mod folder on
//          disk.
//-----------------------------------------------------------------------------
bool CSoundPlayer::PlayGameSound( const std::string &szGamePath, const std::filesystem::path &localRoot )
{
	PlayRequest_t request;
	request.m_szGamePath = szGamePath;
	request.m_szWave = szGamePath.starts_with( "sound/" ) ? szGamePath.substr( 6 ) : szGamePath;
	return Play( request, localRoot );
}

//-----------------------------------------------------------------------------
// Purpose: A sound with its volume, pitch, looping and delay applied.
//-----------------------------------------------------------------------------
bool CSoundPlayer::Play( const PlayRequest_t &request, const std::filesystem::path &localRoot )
{
	if ( !Available() )
	{
		return false;
	}
	if ( request.m_szGamePath.empty() )
	{
		emit Failed( tr( "%1 is a sentence or voice stream, not a sound file" ).arg( QString::fromStdString( request.m_szWave ) ) );
		return false;
	}
	Stop();
	m_nLastVoice = Start( Load( request.m_szGamePath, localRoot ), request );
	if ( m_nLastVoice == 0 )
	{
		return false;
	}
	emit Started( Describe( request ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Plays a soundscape until stopped: loops now, random sounds at their intervals.
//-----------------------------------------------------------------------------
bool CSoundPlayer::PlaySoundscape( const xv::sound::SoundscapePlan_t &plan, const QString &szName, const std::filesystem::path &localRoot )
{
	if ( !Available() )
	{
		return false;
	}
	Stop();
	if ( plan.m_Rules.empty() )
	{
		emit Failed( tr( "%1 has nothing to play" ).arg( szName ) );
		return false;
	}
	m_Simulation.emplace( plan.m_Rules, std::random_device{}() );
	m_SimulationRoot = localRoot;
	m_szSimulationName = szName;
	int nLoops = 0;
	m_nLastVoice = 0;
	for ( const PlayRequest_t &request : m_Simulation->Start() )
	{
		nLoops += Start( Load( request.m_szGamePath, localRoot ), request ) != 0 ? 1 : 0;
	}
	const auto nRandoms = std::ranges::count_if( plan.m_Rules, []( const auto &rule ) { return rule.m_nKind == xv::sound::SoundscapeRule_t::Kind_t::KIND_RANDOM; } );
	m_Clock.start();
	SetActive( true );
	emit Started( tr( "Soundscape %1: %2 looping, %3 random" ).arg( szName ).arg( nLoops ).arg( nRandoms ) );
	if ( !plan.m_Problems.empty() )
	{
		emit Failed( QString::fromStdString( plan.m_Problems.front() ) );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundPlayer::Tick()
{
	if ( m_Simulation )
	{
		const double flElapsed = static_cast<double>( m_Clock.restart() ) / 1000.0;
		for ( const PlayRequest_t &request : m_Simulation->Advance( flElapsed ) )
		{
			if ( Start( Load( request.m_szGamePath, m_SimulationRoot ), request ) != 0 )
			{
				emit Started( tr( "%1: %2" ).arg( m_szSimulationName, Describe( request ) ) );
			}
		}
		return;
	}
	if ( !m_pEngine || m_pEngine->VoiceCount() == 0 )
	{
		SetActive( false );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<double> CSoundPlayer::Position( xv::audio::CEngine::Voice_t nVoice )
{
	return m_pEngine && nVoice != 0 ? m_pEngine->Position( nVoice ) : std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundPlayer::Stop()
{
	m_Simulation.reset();
	m_nLastVoice = 0;
	m_Positioned.clear();
	if ( m_pEngine )
	{
		m_pEngine->StopAll();
	}
	SetActive( false );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundPlayer::SetVolume( float flVolume )
{
	m_flVolume = flVolume;
	if ( m_pEngine )
	{
		m_pEngine->SetVolume( flVolume );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundPlayer::SetActive( bool bActive )
{
	if ( m_bActive == bActive )
	{
		return;
	}
	m_bActive = bActive;
	if ( bActive )
	{
		m_pTimer->start();
	}
	else
	{
		m_pTimer->stop();
		emit Stopped();
	}
}
