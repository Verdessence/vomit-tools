//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Voice.h.
//
//===========================================================================//
#include "Voice.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <opus.h>

// CELT 0.11.3's names as its library was built with them.
#include "CeltNames.h"
#include <celt.h>

namespace xv::demo
{

namespace
{

// A frame's length that says the stream ended.
constexpr std::uint16_t STEAM_VOICE_END = 0xffff;
// The longest Opus frame there is (120 ms at 24 kHz), in samples.
constexpr int OPUS_FRAME_MOST = STEAM_VOICE_RATE * 120 / 1000;
// Steam's frames are 20 ms; a gap of more than this many lost frames isn't filled.
constexpr int STEAM_VOICE_FRAME = STEAM_VOICE_RATE / 50;
constexpr int LOST_FRAMES_MOST = 10;
// Source's vaudio_celt: CELT frames of 64 bytes, each 512 samples at 22050 Hz (mono).
constexpr int CELT_RATE = 22050;
constexpr int CELT_FRAME_SAMPLES = 512;
constexpr std::size_t CELT_FRAME_BYTES = 64;

} // namespace

struct CVoiceDecoder::Impl_t
{
	OpusDecoder *m_pOpus = nullptr;
	CELTMode *m_pCeltMode = nullptr;
	CELTDecoder *m_pCelt = nullptr;
	int m_nNextSequence = -1; // -1: the next frame starts the stream
	std::vector<float> m_Frame = std::vector<float>( OPUS_FRAME_MOST );

	~Impl_t()
	{
		if ( m_pOpus )
		{
			opus_decoder_destroy( m_pOpus );
		}
		if ( m_pCelt )
		{
			celt_decoder_destroy( m_pCelt );
		}
		if ( m_pCeltMode )
		{
			celt_mode_destroy( m_pCeltMode );
		}
	}
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVoiceDecoder::CVoiceDecoder( const VoiceCodec_t &codec )
	: m_pImpl( std::make_unique<Impl_t>() )
{
	if ( codec.m_szCodec == "steam" )
	{
		int nError = OPUS_OK;
		m_pImpl->m_pOpus = opus_decoder_create( STEAM_VOICE_RATE, 1, &nError );
		if ( nError != OPUS_OK )
		{
			m_pImpl->m_pOpus = nullptr;
		}
	}
	else if ( codec.m_szCodec == "vaudio_celt" )
	{
		int nError = CELT_OK;
		m_pImpl->m_pCeltMode = celt_mode_create( CELT_RATE, CELT_FRAME_SAMPLES, &nError );
		if ( m_pImpl->m_pCeltMode && nError == CELT_OK )
		{
			m_pImpl->m_pCelt = celt_decoder_create_custom( m_pImpl->m_pCeltMode, 1, &nError );
			if ( nError != CELT_OK )
			{
				m_pImpl->m_pCelt = nullptr;
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVoiceDecoder::~CVoiceDecoder() = default;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CVoiceDecoder::IsSupported() const
{
	return m_pImpl->m_pOpus != nullptr || m_pImpl->m_pCelt != nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CVoiceDecoder::GetSampleRate() const
{
	return m_pImpl->m_pCelt ? CELT_RATE : STEAM_VOICE_RATE;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CVoiceDecoder::Reset()
{
	if ( m_pImpl->m_pOpus )
	{
		opus_decoder_ctl( m_pImpl->m_pOpus, OPUS_RESET_STATE );
	}
	if ( m_pImpl->m_pCelt )
	{
		celt_decoder_ctl( m_pImpl->m_pCelt, CELT_RESET_STATE );
	}
	m_pImpl->m_nNextSequence = -1;
}

//-----------------------------------------------------------------------------
// Purpose: The packet's silence as zeros, then its frames in turn; a frame ahead of the
//          sequence has the ones missing before it concealed, one behind it (the talker
//          started again) starts the decoder over.
//-----------------------------------------------------------------------------
bool CVoiceDecoder::Decode( std::span<const std::byte> packet, std::vector<float> &samples )
{
	if ( !IsSupported() )
	{
		return false;
	}
	// vaudio_celt: nothing but frames, one after another.
	if ( m_pImpl->m_pCelt )
	{
		std::vector<float> &frame = m_pImpl->m_Frame;
		bool bOk = packet.size() % CELT_FRAME_BYTES == 0;
		for ( std::size_t nAt = 0; nAt + CELT_FRAME_BYTES <= packet.size(); nAt += CELT_FRAME_BYTES )
		{
			// The game decodes to 16 bits, which clips a loud microphone.
			const int nResult = celt_decode_float( m_pImpl->m_pCelt, reinterpret_cast<const unsigned char *>( packet.data() + nAt ), static_cast<int>( CELT_FRAME_BYTES ), frame.data(), CELT_FRAME_SAMPLES );
			if ( nResult < 0 )
			{
				bOk = false;
				continue;
			}
			for ( int i = 0; i < CELT_FRAME_SAMPLES; i++ )
			{
				samples.push_back( std::clamp( frame[ static_cast<std::size_t>( i ) ], -1.0f, 1.0f ) );
			}
		}
		if ( !bOk )
		{
			m_nFailures++;
		}
		return bOk;
	}
	const std::optional<SteamVoice_t> voice = ParseSteamVoice( packet );
	if ( !voice )
	{
		m_nFailures++;
		return false;
	}
	samples.insert( samples.end(), static_cast<std::size_t>( std::max( 0, voice->m_nSilence ) ), 0.0f );
	if ( voice->m_nCodec != STEAM_VOICE_OPUS_PLC )
	{
		return voice->m_nCodec < 0;
	}
	OpusDecoder *pOpus = m_pImpl->m_pOpus;
	std::vector<float> &frame = m_pImpl->m_Frame;
	const std::span<const std::byte> audio = voice->m_Audio;
	const auto number = [ & ]( std::size_t nAt ) {
		std::uint16_t n = 0;
		std::memcpy( &n, audio.data() + nAt, 2 );
		return n;
	};
	bool bOk = true;
	std::size_t nAt = 0;
	while ( nAt + 2 <= audio.size() )
	{
		const std::uint16_t nLength = number( nAt );
		nAt += 2;
		if ( nLength == STEAM_VOICE_END )
		{
			Reset();
			continue;
		}
		if ( nAt + 2 > audio.size() )
		{
			m_nFailures++;
			return false;
		}
		const int nSequence = number( nAt );
		nAt += 2;
		if ( nAt + nLength > audio.size() )
		{
			m_nFailures++;
			return false;
		}
		int &nNext = m_pImpl->m_nNextSequence;
		if ( nNext >= 0 && nSequence < nNext )
		{
			Reset();
		}
		else if ( nNext >= 0 && nSequence > nNext )
		{
			for ( int nLost = std::min( nSequence - nNext, LOST_FRAMES_MOST ); nLost > 0; nLost-- )
			{
				const int nSamples = opus_decode_float( pOpus, nullptr, 0, frame.data(), STEAM_VOICE_FRAME, 0 );
				if ( nSamples > 0 )
				{
					samples.insert( samples.end(), frame.begin(), frame.begin() + nSamples );
				}
			}
		}
		nNext = nSequence + 1;
		if ( nLength == 0 )
		{
			continue;
		}
		const int nSamples = opus_decode_float( pOpus, reinterpret_cast<const unsigned char *>( audio.data() + nAt ), static_cast<opus_int32>( nLength ), frame.data(), OPUS_FRAME_MOST, 0 );
		nAt += nLength;
		if ( nSamples < 0 )
		{
			m_nFailures++;
			bOk = false;
			continue;
		}
		samples.insert( samples.end(), frame.begin(), frame.begin() + nSamples );
	}
	return bOk;
}

//-----------------------------------------------------------------------------
// Purpose: The talker's packets from the speech's first tick to its last, through one
//          decoder.
//-----------------------------------------------------------------------------
std::vector<float> DecodeSpeech( const CContents &contents, const Speech_t &speech, int *pSampleRate, int *pFailures )
{
	std::vector<float> samples;
	CVoiceDecoder decoder( contents.GetVoiceCodec() );
	if ( pSampleRate )
	{
		*pSampleRate = decoder.GetSampleRate();
	}
	if ( !decoder.IsSupported() )
	{
		return samples;
	}
	const std::vector<VoicePacket_t> &packets = contents.GetVoicePackets();
	const auto first = std::ranges::lower_bound( packets, speech.m_nFrom, {}, &VoicePacket_t::m_nTick );
	for ( auto it = first; it != packets.end() && it->m_nTick <= speech.m_nTo; ++it )
	{
		if ( it->m_nEntity == speech.m_nEntity )
		{
			decoder.Decode( contents.GetVoiceBytes( *it ), samples );
		}
	}
	if ( pFailures )
	{
		*pFailures = decoder.GetFailures();
	}
	return samples;
}

} // namespace xv::demo
