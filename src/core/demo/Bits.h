//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A cursor over bits, lowest bit of each byte first, as Source packs what it sends:
//          whole numbers of any width, strings, and the engine's ways of writing a coordinate.
//          Reading past the end gives zeros and sets a flag the caller checks once at the end.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

namespace xv::demo
{

class CBitReader
{
public:
	CBitReader() = default;
	explicit CBitReader( std::span<const std::byte> bytes )
		: m_pData( reinterpret_cast<const std::uint8_t *>( bytes.data() ) ),
		  m_nBytes( bytes.size() ),
		  m_nEnd( bytes.size() * 8 )
	{
	}

	[[nodiscard]] std::size_t GetLeft() const { return m_nEnd - m_nAt; }
	[[nodiscard]] bool IsOverflowed() const { return m_bOverflow; }

	// Up to 32 bits.
	[[nodiscard]] std::uint32_t ReadBits( int nCount )
	{
		if ( nCount <= 0 )
		{
			return 0;
		}
		if ( GetLeft() < static_cast<std::size_t>( nCount ) )
		{
			m_bOverflow = true;
			m_nAt = m_nEnd;
			return 0;
		}
		const std::size_t nByte = m_nAt >> 3;
		const int nShift = static_cast<int>( m_nAt & 7 );
		std::uint64_t nValue = 0;
		if ( nByte + 8 <= m_nBytes )
		{
			std::memcpy( &nValue, m_pData + nByte, 8 );
		}
		else
		{
			std::memcpy( &nValue, m_pData + nByte, m_nBytes - nByte );
		}
		m_nAt += static_cast<std::size_t>( nCount );
		return static_cast<std::uint32_t>( ( nValue >> nShift ) & ( ( std::uint64_t{ 1 } << nCount ) - 1 ) );
	}

	[[nodiscard]] std::int32_t ReadSignedBits( int nCount )
	{
		const std::uint32_t nValue = ReadBits( nCount );
		if ( nCount <= 0 || nCount >= 32 )
		{
			return static_cast<std::int32_t>( nValue );
		}
		const std::uint32_t nSign = std::uint32_t{ 1 } << ( nCount - 1 );
		return static_cast<std::int32_t>( ( nValue ^ nSign ) - nSign );
	}

	[[nodiscard]] bool ReadBit() { return ReadBits( 1 ) != 0; }

	[[nodiscard]] float ReadFloat()
	{
		const std::uint32_t nBits = ReadBits( 32 );
		float flValue = 0.0f;
		std::memcpy( &flValue, &nBits, sizeof( flValue ) );
		return flValue;
	}

	// Bytes up to a zero.
	[[nodiscard]] std::string ReadString()
	{
		std::string szText;
		while ( !m_bOverflow )
		{
			const char ch = static_cast<char>( ReadBits( 8 ) );
			if ( ch == '\0' )
			{
				break;
			}
			szText.push_back( ch );
		}
		return szText;
	}

	void ReadBytes( std::byte *pOut, std::size_t nCount )
	{
		for ( std::size_t i = 0; i < nCount; i++ )
		{
			pOut[ i ] = static_cast<std::byte>( ReadBits( 8 ) );
		}
	}

	void Skip( std::size_t nBits )
	{
		if ( GetLeft() < nBits )
		{
			m_bOverflow = true;
			m_nAt = m_nEnd;
			return;
		}
		m_nAt += nBits;
	}

	// The next nBits as a reader of their own, and this one moved past them.
	[[nodiscard]] CBitReader ReadBlock( std::size_t nBits )
	{
		CBitReader block = *this;
		if ( GetLeft() < nBits )
		{
			m_bOverflow = true;
			nBits = GetLeft();
		}
		block.m_nEnd = m_nAt + nBits;
		block.m_bOverflow = false;
		m_nAt += nBits;
		return block;
	}

	// Seven bits a byte, the eighth saying another follows.
	[[nodiscard]] std::uint32_t ReadVarInt()
	{
		std::uint32_t nValue = 0;
		for ( int nShift = 0; nShift < 35; nShift += 7 )
		{
			const std::uint32_t nByte = ReadBits( 8 );
			nValue |= ( nByte & 0x7f ) << nShift;
			if ( ( nByte & 0x80 ) == 0 )
			{
				break;
			}
		}
		return nValue;
	}

	// Two bits saying how wide the number after them is: 4, 8, 12 or 32 bits.
	[[nodiscard]] std::uint32_t ReadVarBits()
	{
		static constexpr int WIDTHS[ 4 ] = { 4, 8, 12, 32 };
		return ReadBits( WIDTHS[ ReadBits( 2 ) ] );
	}

	// A world coordinate: whether it has a whole part and a fraction, the sign, then 14 bits of
	// whole part (less one) and 5 of fraction (thirty-seconds).
	[[nodiscard]] float ReadCoord()
	{
		const bool bWhole = ReadBit();
		const bool bFraction = ReadBit();
		if ( !bWhole && !bFraction )
		{
			return 0.0f;
		}
		const bool bNegative = ReadBit();
		const std::uint32_t nWhole = bWhole ? ReadBits( 14 ) + 1 : 0;
		const std::uint32_t nFraction = bFraction ? ReadBits( 5 ) : 0;
		const float flValue = static_cast<float>( nWhole ) + static_cast<float>( nFraction ) / 32.0f;
		return bNegative ? -flValue : flValue;
	}

	// A multiplayer coordinate: one inside the usual map bounds takes 11 bits of whole part, not
	// 14; bIntegral ones have no fraction, bLowPrecision ones 3 bits of it (eighths).
	[[nodiscard]] float ReadCoordMp( bool bIntegral, bool bLowPrecision )
	{
		const bool bInBounds = ReadBit();
		const bool bWhole = ReadBit();
		if ( bIntegral )
		{
			if ( !bWhole )
			{
				return 0.0f;
			}
			const bool bNegative = ReadBit();
			const float flValue = static_cast<float>( ReadBits( bInBounds ? 11 : 14 ) + 1 );
			return bNegative ? -flValue : flValue;
		}
		const bool bNegative = ReadBit();
		const std::uint32_t nWhole = bWhole ? ReadBits( bInBounds ? 11 : 14 ) + 1 : 0;
		const std::uint32_t nFraction = ReadBits( bLowPrecision ? 3 : 5 );
		const float flValue = static_cast<float>( nWhole ) + static_cast<float>( nFraction ) / ( bLowPrecision ? 8.0f : 32.0f );
		return bNegative ? -flValue : flValue;
	}

private:
	const std::uint8_t *m_pData = nullptr;
	std::size_t m_nBytes = 0; // of the whole buffer m_pData begins
	std::size_t m_nAt = 0;    // in bits, from m_pData
	std::size_t m_nEnd = 0;
	bool m_bOverflow = false;
};

} // namespace xv::demo
