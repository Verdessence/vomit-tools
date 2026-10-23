//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Audio.h.
//
//===========================================================================//
#include "Audio.h"

#include <algorithm>
#include <map>

#include <miniaudio.h>

#include "../sound/Wav.h"

namespace xv::audio
{

namespace
{

// Keeps a preview from eating all memory on a huge or hostile file: 20 minutes of 48 kHz stereo.
constexpr std::uint64_t MAX_SAMPLES = 48000ull * 2 * 60 * 20;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetError( std::string *pError, std::string szMessage )
{
	if ( pError )
	{
		*pError = std::move( szMessage );
	}
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::uint64_t CClip::GetFrames() const
{
	return m_nChannels ? m_Samples.size() / m_nChannels : 0;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double CClip::GetSeconds() const
{
	return m_nSampleRate ? static_cast<double>( GetFrames() ) / m_nSampleRate : 0.0;
}

//-----------------------------------------------------------------------------
// Purpose: WAV (PCM, float, Microsoft and IMA ADPCM) or MP3. Input is untrusted.
//-----------------------------------------------------------------------------
std::shared_ptr<const CClip> Decode( std::span<const std::byte> data, std::string *pError )
{
	ma_decoder_config config = ma_decoder_config_init( ma_format_f32, 0, 0 );
	ma_decoder decoder;
	if ( ma_decoder_init_memory( data.data(), data.size(), &config, &decoder ) != MA_SUCCESS )
	{
		SetError( pError, "not a WAV or MP3 file this decoder understands" );
		return nullptr;
	}

	auto pClip = std::make_shared<CClip>();
	ma_format nFormat{};
	if ( ma_decoder_get_data_format( &decoder, &nFormat, &pClip->m_nChannels, &pClip->m_nSampleRate, nullptr, 0 ) != MA_SUCCESS || pClip->m_nChannels == 0 || pClip->m_nSampleRate == 0 )
	{
		ma_decoder_uninit( &decoder );
		SetError( pError, "the file has no usable audio format" );
		return nullptr;
	}

	constexpr ma_uint64 nCHUNK = 4096;
	std::vector<float> chunk( nCHUNK * pClip->m_nChannels );
	while ( pClip->m_Samples.size() < MAX_SAMPLES )
	{
		ma_uint64 nRead = 0;
		const ma_result nResult = ma_decoder_read_pcm_frames( &decoder, chunk.data(), nCHUNK, &nRead );
		pClip->m_Samples.insert( pClip->m_Samples.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>( nRead * pClip->m_nChannels ) );
		if ( nResult != MA_SUCCESS || nRead < nCHUNK )
		{
			break;
		}
	}
	ma_decoder_uninit( &decoder );

	if ( pClip->GetFrames() == 0 )
	{
		SetError( pError, "the file has no audio data" );
		return nullptr;
	}
	if ( const auto info = sound::ReadWavInfo( data ); info && info->m_LoopStart && *info->m_LoopStart < pClip->GetFrames() )
	{
		pClip->m_LoopStart = *info->m_LoopStart;
	}
	return pClip;
}

struct CEngine::CImpl
{
	struct Playing_t
	{
		std::shared_ptr<const CClip> m_pClip;
		ma_audio_buffer m_Buffer{};
		ma_sound m_Sound{};
		bool m_bLoop = false;
	};

	ma_engine m_Engine{};
	bool m_bInitialized = false;
	std::string m_szError;
	std::map<Voice_t, std::unique_ptr<Playing_t>> m_Voices;
	Voice_t m_nNext = 1;

	static void Release( Playing_t &playing )
	{
		ma_sound_uninit( &playing.m_Sound );
		ma_audio_buffer_uninit( &playing.m_Buffer );
	}

	void ReleaseFinished()
	{
		for ( auto it = m_Voices.begin(); it != m_Voices.end(); )
		{
			if ( !it->second->m_bLoop && ma_sound_at_end( &it->second->m_Sound ) )
			{
				Release( *it->second );
				it = m_Voices.erase( it );
			}
			else
			{
				++it;
			}
		}
	}
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CEngine::CEngine( Output_t nOutput )
	: m_pImpl( std::make_unique<CImpl>() )
{
	ma_engine_config config = ma_engine_config_init();
	if ( nOutput == Output_t::OUTPUT_OFFLINE )
	{
		config.noDevice = MA_TRUE;
		config.channels = 2;
		config.sampleRate = 48000;
	}
	if ( ma_engine_init( &config, &m_pImpl->m_Engine ) != MA_SUCCESS )
	{
		m_pImpl->m_szError = nOutput == Output_t::OUTPUT_DEVICE ? "no audio output device could be opened" : "the audio mixer could not start";
		return;
	}
	m_pImpl->m_bInitialized = true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CEngine::~CEngine()
{
	if ( m_pImpl->m_bInitialized )
	{
		StopAll();
		ma_engine_uninit( &m_pImpl->m_Engine );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CEngine::GetOk() const
{
	return m_pImpl->m_bInitialized;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const std::string &CEngine::GetError() const
{
	return m_pImpl->m_szError;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::uint32_t CEngine::GetSampleRate() const
{
	return m_pImpl->m_bInitialized ? ma_engine_get_sample_rate( &m_pImpl->m_Engine ) : 0;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CEngine::Voice_t CEngine::Play( std::shared_ptr<const CClip> pClip, const PlayParams_t &params )
{
	if ( !m_pImpl->m_bInitialized || !pClip || pClip->GetFrames() == 0 )
	{
		return 0;
	}
	m_pImpl->ReleaseFinished();

	auto pPlaying = std::make_unique<CImpl::Playing_t>();
	pPlaying->m_pClip = std::move( pClip );
	pPlaying->m_bLoop = params.m_bLoop;
	const CClip &c = *pPlaying->m_pClip;

	ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init( ma_format_f32, c.m_nChannels, c.GetFrames(), c.m_Samples.data(), nullptr );
	bufferConfig.sampleRate = c.m_nSampleRate;
	if ( ma_audio_buffer_init( &bufferConfig, &pPlaying->m_Buffer ) != MA_SUCCESS )
	{
		return 0;
	}
	if ( params.m_bLoop )
	{
		ma_data_source_set_loop_point_in_pcm_frames( &pPlaying->m_Buffer, c.m_LoopStart.value_or( 0 ), c.GetFrames() );
	}
	if ( params.m_flStartSeconds > 0 )
	{
		const auto nFrame = static_cast<ma_uint64>( params.m_flStartSeconds * c.m_nSampleRate );
		ma_audio_buffer_seek_to_pcm_frame( &pPlaying->m_Buffer, std::min<ma_uint64>( nFrame, c.GetFrames() - 1 ) );
	}
	if ( ma_sound_init_from_data_source( &m_pImpl->m_Engine, &pPlaying->m_Buffer, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &pPlaying->m_Sound ) != MA_SUCCESS )
	{
		ma_audio_buffer_uninit( &pPlaying->m_Buffer );
		return 0;
	}
	ma_sound_set_looping( &pPlaying->m_Sound, params.m_bLoop ? MA_TRUE : MA_FALSE );
	ma_sound_set_volume( &pPlaying->m_Sound, params.m_flVolume );
	ma_sound_set_pitch( &pPlaying->m_Sound, params.m_flPitch > 0 ? params.m_flPitch : 1.0f );
	ma_sound_set_pan( &pPlaying->m_Sound, params.m_flPan );
	if ( params.m_flDelaySeconds > 0 )
	{
		const auto nDelay = static_cast<ma_uint64>( params.m_flDelaySeconds * 1000.0 );
		ma_sound_set_start_time_in_milliseconds( &pPlaying->m_Sound, ma_engine_get_time_in_milliseconds( &m_pImpl->m_Engine ) + nDelay );
	}
	if ( ma_sound_start( &pPlaying->m_Sound ) != MA_SUCCESS )
	{
		CImpl::Release( *pPlaying );
		return 0;
	}
	const Voice_t nVoice = m_pImpl->m_nNext++;
	m_pImpl->m_Voices.emplace( nVoice, std::move( pPlaying ) );
	return nVoice;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngine::Stop( Voice_t nVoice )
{
	const auto it = m_pImpl->m_Voices.find( nVoice );
	if ( it != m_pImpl->m_Voices.end() )
	{
		CImpl::Release( *it->second );
		m_pImpl->m_Voices.erase( it );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngine::StopAll()
{
	for ( auto &[ voice, playing ] : m_pImpl->m_Voices )
	{
		CImpl::Release( *playing );
	}
	m_pImpl->m_Voices.clear();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CEngine::Playing( Voice_t nVoice )
{
	m_pImpl->ReleaseFinished();
	return m_pImpl->m_Voices.contains( nVoice );
}

//-----------------------------------------------------------------------------
// Purpose: Where a voice is in its clip, in seconds of the clip (pitch doesn't change the scale);
//          nullopt once it has finished or been stopped.
//-----------------------------------------------------------------------------
std::optional<double> CEngine::Position( Voice_t nVoice )
{
	m_pImpl->ReleaseFinished();
	const auto it = m_pImpl->m_Voices.find( nVoice );
	if ( it == m_pImpl->m_Voices.end() )
	{
		return std::nullopt;
	}
	ma_uint64 nCursor = 0;
	if ( ma_data_source_get_cursor_in_pcm_frames( &it->second->m_Buffer, &nCursor ) != MA_SUCCESS )
	{
		return std::nullopt;
	}
	return static_cast<double>( nCursor ) / it->second->m_pClip->m_nSampleRate;
}

//-----------------------------------------------------------------------------
// Purpose: Voices still playing; finished one-shots are released first.
//-----------------------------------------------------------------------------
std::size_t CEngine::VoiceCount()
{
	m_pImpl->ReleaseFinished();
	return m_pImpl->m_Voices.size();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngine::SetVolume( float flVolume )
{
	if ( m_pImpl->m_bInitialized )
	{
		ma_engine_set_volume( &m_pImpl->m_Engine, flVolume );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Offline output only: mixes the next out.size() / 2 stereo frames.
//-----------------------------------------------------------------------------
std::size_t CEngine::Render( std::span<float> out )
{
	if ( !m_pImpl->m_bInitialized )
	{
		return 0;
	}
	ma_uint64 nRead = 0;
	ma_engine_read_pcm_frames( &m_pImpl->m_Engine, out.data(), out.size() / 2, &nRead );
	return static_cast<std::size_t>( nRead );
}

} // namespace xv::audio
