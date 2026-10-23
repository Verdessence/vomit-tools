//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Wav.h.
//
//===========================================================================//
#include "Wav.h"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace xv::sound
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
template <typename T>
std::optional<T> ReadAt( std::span<const std::byte> data, std::size_t nOffset )
{
	if ( nOffset > data.size() || data.size() - nOffset < sizeof( T ) )
	{
		return std::nullopt;
	}
	T value{};
	std::memcpy( &value, data.data() + nOffset, sizeof( T ) );
	return value;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool TagIs( std::span<const std::byte> data, std::size_t nOffset, std::string_view szTag )
{
	return nOffset + 4 <= data.size() && std::memcmp( data.data() + nOffset, szTag.data(), 4 ) == 0;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double CWavInfo::GetSeconds() const
{
	if ( m_SampleCount && m_nSampleRate )
	{
		return static_cast<double>( *m_SampleCount ) / m_nSampleRate;
	}
	return m_nBytesPerSecond ? static_cast<double>( m_nDataBytes ) / m_nBytesPerSecond : 0.0;
}

//-----------------------------------------------------------------------------
// Purpose: Reads the RIFF header chunks only. Input is untrusted; returns nullopt for anything
//          malformed.
//-----------------------------------------------------------------------------
std::optional<CWavInfo> ReadWavInfo( std::span<const std::byte> data )
{
	if ( !TagIs( data, 0, "RIFF" ) || !TagIs( data, 8, "WAVE" ) )
	{
		return std::nullopt;
	}
	CWavInfo info;
	bool bHaveFormat = false;
	std::size_t nOffset = 12;
	while ( nOffset + 8 <= data.size() )
	{
		const auto size = ReadAt<std::uint32_t>( data, nOffset + 4 );
		if ( !size )
		{
			break;
		}
		const std::size_t nBody = nOffset + 8;
		const std::size_t nAvailable = data.size() - nBody;
		const std::size_t nLength = std::min<std::size_t>( *size, nAvailable ); // the data chunk is often truncated in practice

		if ( TagIs( data, nOffset, "fmt " ) && nLength >= 16 )
		{
			info.m_nFormat = *ReadAt<std::uint16_t>( data, nBody );
			info.m_nChannels = *ReadAt<std::uint16_t>( data, nBody + 2 );
			info.m_nSampleRate = *ReadAt<std::uint32_t>( data, nBody + 4 );
			info.m_nBytesPerSecond = *ReadAt<std::uint32_t>( data, nBody + 8 );
			info.m_nBitsPerSample = *ReadAt<std::uint16_t>( data, nBody + 14 );
			bHaveFormat = true;
		}
		else if ( TagIs( data, nOffset, "data" ) )
		{
			info.m_nDataBytes = static_cast<std::uint32_t>( nLength );
		}
		else if ( TagIs( data, nOffset, "fact" ) && nLength >= 4 )
		{
			info.m_SampleCount = ReadAt<std::uint32_t>( data, nBody );
		}
		else if ( TagIs( data, nOffset, "cue " ) && nLength >= 4 + 24 )
		{
			// A count, then 24-byte cue points whose sample offset sits at byte 20. The count is
			// ignored: stock files used for looping (CS:S's ambient/machines/fluorescent_hum_2.wav)
			// have a count of 0 followed by a real cue point.
			if ( const auto start = ReadAt<std::uint32_t>( data, nBody + 4 + 20 ); start && !info.m_LoopStart )
			{
				info.m_LoopStart = *start;
			}
		}
		else if ( TagIs( data, nOffset, "smpl" ) && nLength >= 36 )
		{
			// loop count at byte 28, then 24-byte loops whose start sits at byte 8
			const auto loops = ReadAt<std::uint32_t>( data, nBody + 28 );
			if ( loops && *loops > 0 && nLength >= 36 + 24 && !info.m_LoopStart )
			{
				info.m_LoopStart = ReadAt<std::uint32_t>( data, nBody + 36 + 8 );
			}
		}

		// Chunks are padded to an even size.
		const std::size_t nNext = nBody + *size + ( *size & 1 );
		if ( nNext <= nOffset )
		{
			break;
		}
		nOffset = nNext;
	}
	if ( !bHaveFormat )
	{
		return std::nullopt;
	}
	return info;
}

} // namespace xv::sound
