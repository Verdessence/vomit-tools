//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Vtf.h.
//
//===========================================================================//
#include "Vtf.h"

#include <algorithm>
#include <bit>
#include <climits>
#include <cmath>
#include <cstring>
#include <format>
#include <initializer_list>
#include <numbers>
#include <utility>

#include "stb_dxt.h"

namespace xv::texture
{

namespace
{

constexpr std::uint32_t NO_FORMAT = 0xFFFFFFFF;
constexpr int HEADER_SIZE_71 = 64;
constexpr int HEADER_SIZE_72 = 80;
constexpr std::size_t RESOURCES_OFFSET = 80; // 7.3+: resource entries follow the 7.2 header

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
template <typename T>
T ReadAt( std::span<const std::byte> data, std::size_t nOffset )
{
	T value{};
	std::memcpy( &value, data.data() + nOffset, sizeof( T ) );
	return value;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
template <typename T>
void WriteAt( std::vector<std::byte> &out, std::size_t nOffset, T value )
{
	std::memcpy( out.data() + nOffset, &value, sizeof( T ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::uint16_t U16le( const std::byte *p )
{
	return static_cast<std::uint16_t>( std::to_integer<unsigned>( p[ 0 ] ) | ( std::to_integer<unsigned>( p[ 1 ] ) << 8 ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::uint8_t At( std::span<const std::byte> data, std::size_t i )
{
	return std::to_integer<std::uint8_t>( data[ i ] );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::uint8_t Expand5( unsigned v )
{
	return static_cast<std::uint8_t>( ( v << 3 ) | ( v >> 2 ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::uint8_t Expand6( unsigned v )
{
	return static_cast<std::uint8_t>( ( v << 2 ) | ( v >> 4 ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::uint8_t Expand4( unsigned v )
{
	return static_cast<std::uint8_t>( v * 17 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float HalfToFloat( std::uint16_t h )
{
	const unsigned nSign = ( h >> 15 ) & 1;
	const unsigned nExponent = ( h >> 10 ) & 31;
	const unsigned nMantissa = h & 1023;
	float flValue = 0.0f;
	if ( nExponent == 0 )
	{
		flValue = std::ldexp( static_cast<float>( nMantissa ), -24 );
	}
	else if ( nExponent == 31 )
	{
		flValue = nMantissa ? 0.0f : 65504.0f; // NaN reads as black, infinity as the largest half
	}
	else
	{
		flValue = std::ldexp( static_cast<float>( nMantissa | 1024 ), static_cast<int>( nExponent ) - 25 );
	}
	return nSign ? -flValue : flValue;
}

//-----------------------------------------------------------------------------
// Purpose: The four colours of a DXT colour block; fourColour is false for DXT1's three-colour
//          mode, where
//          index 3 is transparent black.
//-----------------------------------------------------------------------------
void DecodeColourBlock( const std::byte *pBlock, bool bAlwaysFourColour, std::array<std::array<std::uint8_t, 4>, 4> &colours, std::uint32_t &nIndices )
{
	const unsigned nC0 = U16le( pBlock );
	const unsigned nC1 = U16le( pBlock + 2 );
	nIndices = ReadAt<std::uint32_t>( std::span{ pBlock, 8 }, 4 );
	colours[ 0 ] = { Expand5( nC0 >> 11 ), Expand6( ( nC0 >> 5 ) & 63 ), Expand5( nC0 & 31 ), 255 };
	colours[ 1 ] = { Expand5( nC1 >> 11 ), Expand6( ( nC1 >> 5 ) & 63 ), Expand5( nC1 & 31 ), 255 };
	if ( bAlwaysFourColour || nC0 > nC1 )
	{
		for ( int c = 0; c < 3; c++ )
		{
			colours[ 2 ][ c ] = static_cast<std::uint8_t>( ( 2 * colours[ 0 ][ c ] + colours[ 1 ][ c ] + 1 ) / 3 );
			colours[ 3 ][ c ] = static_cast<std::uint8_t>( ( colours[ 0 ][ c ] + 2 * colours[ 1 ][ c ] + 1 ) / 3 );
		}
		colours[ 2 ][ 3 ] = 255;
		colours[ 3 ][ 3 ] = 255;
	}
	else
	{
		for ( int c = 0; c < 3; c++ )
		{
			colours[ 2 ][ c ] = static_cast<std::uint8_t>( ( colours[ 0 ][ c ] + colours[ 1 ][ c ] ) / 2 );
		}
		colours[ 2 ][ 3 ] = 255;
		colours[ 3 ] = { 0, 0, 0, 0 };
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void DecodeDxt( VtfFormat_t nFormat, int nWidth, int nHeight, std::span<const std::byte> data, CImage &out )
{
	const int nBlockBytes = nFormat == VtfFormat_t::DXT1 || nFormat == VtfFormat_t::DXT1_ONEBITALPHA ? 8 : 16;
	const int nBlocksWide = std::max( 1, ( nWidth + 3 ) / 4 );
	const int nBlocksHigh = std::max( 1, ( nHeight + 3 ) / 4 );
	for ( int nBy = 0; nBy < nBlocksHigh; nBy++ )
	{
		for ( int nBx = 0; nBx < nBlocksWide; nBx++ )
		{
			const std::byte *pBlock = data.data() + ( static_cast<std::size_t>( nBy ) * static_cast<std::size_t>( nBlocksWide ) + static_cast<std::size_t>( nBx ) ) * static_cast<std::size_t>( nBlockBytes );
			std::array<std::uint8_t, 16> alpha{};
			alpha.fill( 255 );
			if ( nFormat == VtfFormat_t::DXT3 )
			{
				for ( int i = 0; i < 16; i++ )
				{
					const unsigned nNibble = ( std::to_integer<unsigned>( pBlock[ i / 2 ] ) >> ( ( i % 2 ) * 4 ) ) & 15;
					alpha[ static_cast<std::size_t>( i ) ] = Expand4( nNibble );
				}
			}
			else if ( nFormat == VtfFormat_t::DXT5 )
			{
				const unsigned nA0 = std::to_integer<unsigned>( pBlock[ 0 ] );
				const unsigned nA1 = std::to_integer<unsigned>( pBlock[ 1 ] );
				std::array<unsigned, 8> palette{ nA0, nA1 };
				if ( nA0 > nA1 )
				{
					for ( unsigned i = 1; i < 7; i++ )
					{
						palette[ i + 1 ] = ( ( 7 - i ) * nA0 + i * nA1 + 3 ) / 7;
					}
				}
				else
				{
					for ( unsigned i = 1; i < 5; i++ )
					{
						palette[ i + 1 ] = ( ( 5 - i ) * nA0 + i * nA1 + 2 ) / 5;
					}
					palette[ 6 ] = 0;
					palette[ 7 ] = 255;
				}
				std::uint64_t nBits = 0;
				for ( int i = 0; i < 6; i++ )
				{
					nBits |= static_cast<std::uint64_t>( std::to_integer<unsigned>( pBlock[ 2 + i ] ) ) << ( 8 * i );
				}
				for ( std::size_t i = 0; i < 16; i++ )
				{
					alpha[ i ] = static_cast<std::uint8_t>( palette[ ( nBits >> ( 3 * i ) ) & 7 ] );
				}
			}
			std::array<std::array<std::uint8_t, 4>, 4> colours{};
			std::uint32_t nIndices = 0;
			DecodeColourBlock( pBlock + ( nBlockBytes - 8 ), nBlockBytes == 16, colours, nIndices );
			for ( int i = 0; i < 16; i++ )
			{
				const int x = nBx * 4 + i % 4;
				const int y = nBy * 4 + i / 4;
				if ( x >= nWidth || y >= nHeight )
				{
					continue;
				}
				const auto &colour = colours[ ( nIndices >> ( 2 * i ) ) & 3 ];
				std::uint8_t *p = out.Pixel( x, y );
				std::memcpy( p, colour.data(), 4 );
				if ( nBlockBytes == 16 )
				{
					p[ 3 ] = alpha[ static_cast<std::size_t>( i ) ];
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Encodes one 4x4 block; pixels outside the image repeat the nearest edge pixel.
//-----------------------------------------------------------------------------
void EncodeDxtBlock( const CImage &image, int nBx, int nBy, bool bWithAlpha, std::byte *pOut )
{
	std::array<std::uint8_t, 64> rgba{};
	for ( int i = 0; i < 16; i++ )
	{
		const int x = std::min( nBx * 4 + i % 4, image.m_nWidth - 1 );
		const int y = std::min( nBy * 4 + i / 4, image.m_nHeight - 1 );
		std::memcpy( rgba.data() + i * 4, image.Pixel( x, y ), 4 );
	}
	auto *pDest = reinterpret_cast<unsigned char *>( pOut );
	stb_compress_dxt_block( pDest, rgba.data(), bWithAlpha ? 1 : 0, STB_DXT_HIGHQUAL );
	if ( !bWithAlpha )
	{
		// Opaque DXT1 must stay in four-colour mode (first endpoint greater), or index 3 would be
		// transparent black.
		const std::uint16_t nC0 = U16le( pOut );
		const std::uint16_t nC1 = U16le( pOut + 2 );
		std::uint32_t nIndices = ReadAt<std::uint32_t>( std::span{ pOut, 8 }, 4 );
		if ( nC0 < nC1 )
		{
			std::swap( pDest[ 0 ], pDest[ 2 ] );
			std::swap( pDest[ 1 ], pDest[ 3 ] );
			nIndices ^= 0x55555555u; // endpoints swapped: 0<->1, 2<->3
		}
		else if ( nC0 == nC1 )
		{
			nIndices = 0;
		}
		std::memcpy( pDest + 4, &nIndices, 4 );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int MipCountFor( int nWidth, int nHeight )
{
	return std::bit_width( static_cast<unsigned>( std::max( nWidth, nHeight ) ) );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view FormatName( VtfFormat_t nFormat )
{
	switch ( nFormat )
	{
		case VtfFormat_t::VTF_FORMAT_NONE: return "none";
		case VtfFormat_t::RGBA8888: return "RGBA8888";
		case VtfFormat_t::ABGR8888: return "ABGR8888";
		case VtfFormat_t::RGB888: return "RGB888";
		case VtfFormat_t::BGR888: return "BGR888";
		case VtfFormat_t::RGB565: return "RGB565";
		case VtfFormat_t::I8: return "I8";
		case VtfFormat_t::IA88: return "IA88";
		case VtfFormat_t::P8: return "P8";
		case VtfFormat_t::A8: return "A8";
		case VtfFormat_t::RGB888_BLUESCREEN: return "RGB888_BLUESCREEN";
		case VtfFormat_t::BGR888_BLUESCREEN: return "BGR888_BLUESCREEN";
		case VtfFormat_t::ARGB8888: return "ARGB8888";
		case VtfFormat_t::BGRA8888: return "BGRA8888";
		case VtfFormat_t::DXT1: return "DXT1";
		case VtfFormat_t::DXT3: return "DXT3";
		case VtfFormat_t::DXT5: return "DXT5";
		case VtfFormat_t::BGRX8888: return "BGRX8888";
		case VtfFormat_t::BGR565: return "BGR565";
		case VtfFormat_t::BGRX5551: return "BGRX5551";
		case VtfFormat_t::BGRA4444: return "BGRA4444";
		case VtfFormat_t::DXT1_ONEBITALPHA: return "DXT1_ONEBITALPHA";
		case VtfFormat_t::BGRA5551: return "BGRA5551";
		case VtfFormat_t::UV88: return "UV88";
		case VtfFormat_t::UVWQ8888: return "UVWQ8888";
		case VtfFormat_t::RGBA16161616F: return "RGBA16161616F";
		case VtfFormat_t::RGBA16161616: return "RGBA16161616";
		case VtfFormat_t::UVLX8888: return "UVLX8888";
		case VtfFormat_t::R32F: return "R32F";
		case VtfFormat_t::RGB323232F: return "RGB323232F";
		case VtfFormat_t::RGBA32323232F: return "RGBA32323232F";
	}
	return "unknown";
}

//-----------------------------------------------------------------------------
// Purpose: The names of the known flags that are set, e.g. {"CLAMPS", "CLAMPT"}.
//-----------------------------------------------------------------------------
std::vector<std::string> FlagNames( std::uint32_t nFlags )
{
	static constexpr std::pair<std::uint32_t, std::string_view> NAMES[] = {
		{ 0x1, "POINTSAMPLE" },
		{ 0x2, "TRILINEAR" },
		{ 0x4, "CLAMPS" },
		{ 0x8, "CLAMPT" },
		{ 0x10, "ANISOTROPIC" },
		{ 0x20, "HINT_DXT5" },
		{ 0x80, "NORMAL" },
		{ 0x100, "NOMIP" },
		{ 0x200, "NOLOD" },
		{ 0x400, "ALL_MIPS" },
		{ 0x800, "PROCEDURAL" },
		{ 0x1000, "ONEBITALPHA" },
		{ 0x2000, "EIGHTBITALPHA" },
		{ 0x4000, "ENVMAP" },
		{ 0x8000, "RENDERTARGET" },
		{ 0x10000, "DEPTHRENDERTARGET" },
		{ 0x20000, "NODEBUGOVERRIDE" },
		{ 0x40000, "SINGLECOPY" },
		{ 0x800000, "NODEPTHBUFFER" },
		{ 0x2000000, "CLAMPU" },
		{ 0x4000000, "VERTEXTEXTURE" },
		{ 0x8000000, "SSBUMP" },
		{ 0x20000000, "BORDER" },
	};
	std::vector<std::string> names;
	std::uint32_t nUnknown = nFlags;
	for ( const auto &[ bit, name ] : NAMES )
	{
		if ( nFlags & bit )
		{
			names.emplace_back( name );
			nUnknown &= ~bit;
		}
	}
	for ( std::uint32_t nBit = 1; nBit; nBit <<= 1 )
	{
		if ( nUnknown & nBit )
		{
			names.push_back( std::format( "0x{:x}", nBit ) );
		}
	}
	return names;
}

//-----------------------------------------------------------------------------
// Purpose: Bytes of one width x height image; 0 for a format of unknown size.
//-----------------------------------------------------------------------------
std::size_t ImageSize( VtfFormat_t nFormat, int nWidth, int nHeight )
{
	const auto w = static_cast<std::size_t>( std::max( nWidth, 1 ) );
	const auto h = static_cast<std::size_t>( std::max( nHeight, 1 ) );
	const std::size_t nBlocks = ( ( w + 3 ) / 4 ) * ( ( h + 3 ) / 4 );
	switch ( nFormat )
	{
		case VtfFormat_t::DXT1:
		case VtfFormat_t::DXT1_ONEBITALPHA:
			return nBlocks * 8;
		case VtfFormat_t::DXT3:
		case VtfFormat_t::DXT5:
			return nBlocks * 16;
		case VtfFormat_t::I8:
		case VtfFormat_t::P8:
		case VtfFormat_t::A8:
			return w * h;
		case VtfFormat_t::RGB565:
		case VtfFormat_t::IA88:
		case VtfFormat_t::BGR565:
		case VtfFormat_t::BGRX5551:
		case VtfFormat_t::BGRA4444:
		case VtfFormat_t::BGRA5551:
		case VtfFormat_t::UV88:
			return w * h * 2;
		case VtfFormat_t::RGB888:
		case VtfFormat_t::BGR888:
		case VtfFormat_t::RGB888_BLUESCREEN:
		case VtfFormat_t::BGR888_BLUESCREEN:
			return w * h * 3;
		case VtfFormat_t::RGBA8888:
		case VtfFormat_t::ABGR8888:
		case VtfFormat_t::ARGB8888:
		case VtfFormat_t::BGRA8888:
		case VtfFormat_t::BGRX8888:
		case VtfFormat_t::UVWQ8888:
		case VtfFormat_t::UVLX8888:
		case VtfFormat_t::R32F:
			return w * h * 4;
		case VtfFormat_t::RGBA16161616F:
		case VtfFormat_t::RGBA16161616:
			return w * h * 8;
		case VtfFormat_t::RGB323232F:
			return w * h * 12;
		case VtfFormat_t::RGBA32323232F:
			return w * h * 16;
		case VtfFormat_t::VTF_FORMAT_NONE:
			return 0;
	}
	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: The data of a resource; empty if the file has none with that tag, or it is cut off.
//-----------------------------------------------------------------------------
std::span<const std::byte> CVtf::Resource( std::array<std::uint8_t, 3> tag ) const
{
	for ( const VtfResource_t &entry : m_Resources )
	{
		if ( entry.m_Tag != tag || ( entry.m_nFlags & VtfResource_t::NO_DATA ) )
		{
			continue;
		}
		if ( entry.m_nValue > m_Data.size() || m_Data.size() - entry.m_nValue < 4 )
		{
			return {};
		}
		const std::uint32_t nSize = ReadAt<std::uint32_t>( m_Data, entry.m_nValue );
		if ( m_Data.size() - entry.m_nValue - 4 < nSize )
		{
			return {};
		}
		return std::span<const std::byte>{ m_Data }.subspan( entry.m_nValue + 4, nSize );
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose: The bytes of one image: mip 0 is full size. Empty if out of range.
//-----------------------------------------------------------------------------
std::span<const std::byte> CVtf::Image( int nMip, int nFrame, int nFace, int nSlice ) const
{
	if ( nMip < 0 || nMip >= m_Info.m_nMipCount || nFrame < 0 || nFrame >= m_Info.m_nFrames || nFace < 0 || nFace >= m_Info.m_nFaces || nSlice < 0 )
	{
		return {};
	}
	const auto levelSize = [ this ]( int nLevel ) {
		return ImageSize( m_Info.m_nFormat, m_Info.m_nWidth >> nLevel, m_Info.m_nHeight >> nLevel );
	};
	const auto levelDepth = [ this ]( int nLevel ) {
		return static_cast<std::size_t>( std::max( 1, m_Info.m_nDepth >> nLevel ) );
	};
	if ( static_cast<std::size_t>( nSlice ) >= levelDepth( nMip ) )
	{
		return {};
	}
	const auto nImages = static_cast<std::size_t>( m_Info.m_nFrames ) * static_cast<std::size_t>( m_Info.m_nFaces );
	std::size_t nOffset = m_nImageOffset;
	for ( int nLevel = m_Info.m_nMipCount - 1; nLevel > nMip; nLevel-- )
	{
		nOffset += nImages * levelDepth( nLevel ) * levelSize( nLevel );
	}
	const std::size_t nSize = levelSize( nMip );
	nOffset += ( ( static_cast<std::size_t>( nFrame ) * static_cast<std::size_t>( m_Info.m_nFaces ) + static_cast<std::size_t>( nFace ) ) * levelDepth( nMip ) + static_cast<std::size_t>( nSlice ) ) * nSize;
	if ( nOffset > m_Data.size() || m_Data.size() - nOffset < nSize )
	{
		return {};
	}
	return std::span{ m_Data }.subspan( nOffset, nSize );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<CImage> CVtf::Decode( int nMip, int nFrame, int nFace, std::string *pError ) const
{
	const auto bytes = Image( nMip, nFrame, nFace );
	if ( bytes.empty() )
	{
		if ( pError )
		{
			*pError = "no such image in the file";
		}
		return std::nullopt;
	}
	return DecodeImage( m_Info.m_nFormat, std::max( 1, m_Info.m_nWidth >> nMip ), std::max( 1, m_Info.m_nHeight >> nMip ), bytes, pError );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<CFloatImage> CVtf::DecodeFloat( int nMip, int nFrame, int nFace, std::string *pError ) const
{
	const auto bytes = Image( nMip, nFrame, nFace );
	if ( bytes.empty() )
	{
		if ( pError )
		{
			*pError = "no such image in the file";
		}
		return std::nullopt;
	}
	return texture::DecodeFloat( m_Info.m_nFormat, std::max( 1, m_Info.m_nWidth >> nMip ), std::max( 1, m_Info.m_nHeight >> nMip ), bytes, pError );
}

//-----------------------------------------------------------------------------
// Purpose: Input is untrusted: every size and offset is checked against the file.
//-----------------------------------------------------------------------------
std::optional<CVtf> ReadVtf( std::span<const std::byte> data, std::string *pError )
{
	const auto fail = [ pError ]( std::string szMessage ) -> std::optional<CVtf> {
		if ( pError )
		{
			*pError = std::move( szMessage );
		}
		return std::nullopt;
	};
	if ( data.size() < HEADER_SIZE_71 || std::memcmp( data.data(), "VTF\0", 4 ) != 0 )
	{
		return fail( "not a VTF file" );
	}
	CVtf vtf;
	VtfInfo_t &info = vtf.m_Info;
	info.m_nVersionMajor = static_cast<int>( ReadAt<std::uint32_t>( data, 4 ) );
	info.m_nVersionMinor = static_cast<int>( ReadAt<std::uint32_t>( data, 8 ) );
	if ( info.m_nVersionMajor != 7 || info.m_nVersionMinor < 0 || info.m_nVersionMinor > 5 )
	{
		return fail( std::format( "VTF version {}.{} is not supported (7.0 to 7.5 are)", info.m_nVersionMajor, info.m_nVersionMinor ) );
	}
	const std::uint32_t nHeaderSize = ReadAt<std::uint32_t>( data, 12 );
	info.m_nWidth = ReadAt<std::uint16_t>( data, 16 );
	info.m_nHeight = ReadAt<std::uint16_t>( data, 18 );
	info.m_nFlags = ReadAt<std::uint32_t>( data, 20 );
	info.m_nFrames = ReadAt<std::uint16_t>( data, 24 );
	info.m_nFirstFrame = ReadAt<std::uint16_t>( data, 26 );
	for ( std::size_t i = 0; i < 3; i++ )
	{
		info.m_vecReflectivity[ i ] = ReadAt<float>( data, 32 + i * 4 );
	}
	info.m_flBumpScale = ReadAt<float>( data, 48 );
	info.m_nFormat = static_cast<VtfFormat_t>( ReadAt<std::int32_t>( data, 52 ) );
	info.m_nMipCount = At( data, 56 );
	const std::uint32_t nLowResFormat = ReadAt<std::uint32_t>( data, 57 );
	info.m_nLowResFormat = nLowResFormat == NO_FORMAT ? VtfFormat_t::VTF_FORMAT_NONE : static_cast<VtfFormat_t>( nLowResFormat );
	info.m_nLowResWidth = At( data, 61 );
	info.m_nLowResHeight = At( data, 62 );
	if ( info.m_nVersionMinor >= 2 && data.size() >= 65 )
	{
		info.m_nDepth = std::max<int>( 1, ReadAt<std::uint16_t>( data, 63 ) );
	}
	if ( nHeaderSize < HEADER_SIZE_71 || nHeaderSize > data.size() )
	{
		return fail( "the header is cut off" );
	}
	if ( info.m_nWidth == 0 || info.m_nHeight == 0 || info.m_nFrames == 0 || info.m_nMipCount == 0 || info.m_nMipCount > 16 )
	{
		return fail( "the header has an empty size, frame count or mipmap count" );
	}
	if ( ImageSize( info.m_nFormat, 1, 1 ) == 0 )
	{
		return fail( std::format( "image format {} is not supported", static_cast<int>( info.m_nFormat ) ) );
	}
	if ( info.m_nFlags & vtf_flag::ENVMAP )
	{
		info.m_nFaces = info.m_nVersionMinor < 5 && info.m_nFirstFrame != 0xFFFF ? 7 : 6;
	}

	if ( info.m_nVersionMinor >= 3 )
	{
		const std::uint32_t nCount = ReadAt<std::uint32_t>( data, 68 );
		if ( nCount > 32 || RESOURCES_OFFSET + nCount * 8 > nHeaderSize )
		{
			return fail( "the resource list is cut off" );
		}
		std::optional<std::uint32_t> imageData;
		for ( std::uint32_t i = 0; i < nCount; i++ )
		{
			const std::size_t nEntry = RESOURCES_OFFSET + i * 8;
			const VtfResource_t resource{ { At( data, nEntry ), At( data, nEntry + 1 ), At( data, nEntry + 2 ) }, At( data, nEntry + 3 ), ReadAt<std::uint32_t>( data, nEntry + 4 ) };
			if ( resource.m_Tag == std::array<std::uint8_t, 3>{ 0x30, 0, 0 } )
			{
				imageData = resource.m_nValue;
			}
			vtf.m_Resources.push_back( resource );
		}
		if ( !imageData )
		{
			return fail( "the file has no image data" );
		}
		vtf.m_nImageOffset = *imageData;
	}
	else
	{
		// Some tools name a low-res format but give it a 0x0 size: then it takes no space.
		const bool bHasLowRes = info.m_nLowResFormat != VtfFormat_t::VTF_FORMAT_NONE && info.m_nLowResWidth > 0 && info.m_nLowResHeight > 0;
		const std::size_t nLowRes = bHasLowRes ? ImageSize( info.m_nLowResFormat, info.m_nLowResWidth, info.m_nLowResHeight ) : 0;
		vtf.m_nImageOffset = nHeaderSize + nLowRes;
	}

	const auto totalSize = [ &info ] {
		std::uint64_t nTotal = 0;
		for ( int nLevel = 0; nLevel < info.m_nMipCount; nLevel++ )
		{
			const auto nDepth = static_cast<std::uint64_t>( std::max( 1, info.m_nDepth >> nLevel ) );
			nTotal += static_cast<std::uint64_t>( info.m_nFrames ) * static_cast<std::uint64_t>( info.m_nFaces ) * nDepth * ImageSize( info.m_nFormat, info.m_nWidth >> nLevel, info.m_nHeight >> nLevel );
		}
		return nTotal;
	};
	if ( vtf.m_nImageOffset + totalSize() > data.size() && info.m_nFaces == 7 )
	{
		info.m_nFaces = 6; // a cube map written without the sphere map
	}
	if ( vtf.m_nImageOffset > data.size() || vtf.m_nImageOffset + totalSize() > data.size() )
	{
		return fail( "the image data is cut off" );
	}
	vtf.m_Data.assign( data.begin(), data.end() );
	return vtf;
}

//-----------------------------------------------------------------------------
// Purpose: Raw image data to RGBA8. Float formats are clamped to 0-1 and converted from linear to
//          sRGB.
//-----------------------------------------------------------------------------
std::optional<CImage> DecodeImage( VtfFormat_t nFormat, int nWidth, int nHeight, std::span<const std::byte> data, std::string *pError )
{
	const std::size_t nSize = ImageSize( nFormat, nWidth, nHeight );
	if ( nSize == 0 || data.size() < nSize || nFormat == VtfFormat_t::P8 )
	{
		if ( pError )
		{
			*pError = nSize == 0 || nFormat == VtfFormat_t::P8 ? std::format( "can't decode {}", FormatName( nFormat ) ) : std::string{ "the image data is cut off" };
		}
		return std::nullopt;
	}
	CImage out{ nWidth, nHeight };
	if ( nFormat == VtfFormat_t::DXT1 || nFormat == VtfFormat_t::DXT1_ONEBITALPHA || nFormat == VtfFormat_t::DXT3 || nFormat == VtfFormat_t::DXT5 )
	{
		DecodeDxt( nFormat, nWidth, nHeight, data, out );
		return out;
	}
	const std::size_t nPixelBytes = nSize / ( static_cast<std::size_t>( nWidth ) * static_cast<std::size_t>( nHeight ) );
	for ( std::size_t i = 0; i < static_cast<std::size_t>( nWidth ) * static_cast<std::size_t>( nHeight ); i++ )
	{
		const std::byte *s = data.data() + i * nPixelBytes;
		const auto b = [ s ]( int k ) { return std::to_integer<std::uint8_t>( s[ k ] ); };
		std::uint8_t *p = out.m_Pixels.data() + i * 4;
		const auto set = [ p ]( unsigned r, unsigned g, unsigned nBl, unsigned a ) {
			p[ 0 ] = static_cast<std::uint8_t>( r );
			p[ 1 ] = static_cast<std::uint8_t>( g );
			p[ 2 ] = static_cast<std::uint8_t>( nBl );
			p[ 3 ] = static_cast<std::uint8_t>( a );
		};
		switch ( nFormat )
		{
			case VtfFormat_t::RGBA8888: set( b( 0 ), b( 1 ), b( 2 ), b( 3 ) ); break;
			case VtfFormat_t::ABGR8888: set( b( 3 ), b( 2 ), b( 1 ), b( 0 ) ); break;
			case VtfFormat_t::RGB888: set( b( 0 ), b( 1 ), b( 2 ), 255 ); break;
			case VtfFormat_t::BGR888: set( b( 2 ), b( 1 ), b( 0 ), 255 ); break;
			case VtfFormat_t::RGB888_BLUESCREEN: set( b( 0 ), b( 1 ), b( 2 ), b( 0 ) == 0 && b( 1 ) == 0 && b( 2 ) == 255 ? 0 : 255 ); break;
			case VtfFormat_t::BGR888_BLUESCREEN: set( b( 2 ), b( 1 ), b( 0 ), b( 2 ) == 0 && b( 1 ) == 0 && b( 0 ) == 255 ? 0 : 255 ); break;
			case VtfFormat_t::ARGB8888: set( b( 1 ), b( 2 ), b( 3 ), b( 0 ) ); break;
			case VtfFormat_t::BGRA8888: set( b( 2 ), b( 1 ), b( 0 ), b( 3 ) ); break;
			case VtfFormat_t::BGRX8888: set( b( 2 ), b( 1 ), b( 0 ), 255 ); break;
			case VtfFormat_t::UVWQ8888:
			case VtfFormat_t::UVLX8888: set( b( 0 ), b( 1 ), b( 2 ), b( 3 ) ); break;
			case VtfFormat_t::I8: set( b( 0 ), b( 0 ), b( 0 ), 255 ); break;
			case VtfFormat_t::IA88: set( b( 0 ), b( 0 ), b( 0 ), b( 1 ) ); break;
			case VtfFormat_t::A8: set( 255, 255, 255, b( 0 ) ); break;
			case VtfFormat_t::UV88: set( b( 0 ), b( 1 ), 0, 255 ); break;
			case VtfFormat_t::RGB565:
			case VtfFormat_t::BGR565:
			{
				const unsigned v = U16le( s );
				const unsigned nHigh = Expand5( v >> 11 );
				const unsigned nLow = Expand5( v & 31 );
				const unsigned nGreen = Expand6( ( v >> 5 ) & 63 );
				nFormat == VtfFormat_t::BGR565 ? set( nHigh, nGreen, nLow, 255 ) : set( nLow, nGreen, nHigh, 255 );
				break;
			}
			case VtfFormat_t::BGRA4444:
			{
				const unsigned v = U16le( s );
				set( Expand4( ( v >> 8 ) & 15 ), Expand4( ( v >> 4 ) & 15 ), Expand4( v & 15 ), Expand4( v >> 12 ) );
				break;
			}
			case VtfFormat_t::BGRA5551:
			case VtfFormat_t::BGRX5551:
			{
				const unsigned v = U16le( s );
				set( Expand5( ( v >> 10 ) & 31 ), Expand5( ( v >> 5 ) & 31 ), Expand5( v & 31 ), nFormat == VtfFormat_t::BGRX5551 || ( v >> 15 ) ? 255 : 0 );
				break;
			}
			case VtfFormat_t::RGBA16161616F:
			{
				for ( int c = 0; c < 4; c++ )
				{
					const float flValue = HalfToFloat( U16le( s + c * 2 ) );
					p[ c ] = c < 3 ? FromLinear( flValue ) : static_cast<std::uint8_t>( std::lround( std::clamp( flValue, 0.0f, 1.0f ) * 255.0f ) );
				}
				break;
			}
			case VtfFormat_t::RGBA16161616: // linear, like the float formats
			{
				for ( int c = 0; c < 4; c++ )
				{
					const float flValue = static_cast<float>( U16le( s + c * 2 ) ) / 65535.0f;
					p[ c ] = c < 3 ? FromLinear( flValue ) : static_cast<std::uint8_t>( std::lround( flValue * 255.0f ) );
				}
				break;
			}
			case VtfFormat_t::R32F:
			case VtfFormat_t::RGB323232F:
			case VtfFormat_t::RGBA32323232F:
			{
				const int nChannels = nFormat == VtfFormat_t::R32F ? 1 : nFormat == VtfFormat_t::RGB323232F ? 3
																											: 4;
				for ( int c = 0; c < 3; c++ )
				{
					p[ c ] = FromLinear( ReadAt<float>( std::span{ s, nPixelBytes }, static_cast<std::size_t>( std::min( c, nChannels - 1 ) ) * 4 ) );
				}
				p[ 3 ] = nChannels == 4 ? static_cast<std::uint8_t>( std::lround( std::clamp( ReadAt<float>( std::span{ s, nPixelBytes }, 12 ), 0.0f, 1.0f ) * 255.0f ) ) : 255;
				break;
			}
			default:
				break;
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsFloatFormat( VtfFormat_t nFormat )
{
	return nFormat == VtfFormat_t::RGBA16161616F || nFormat == VtfFormat_t::RGBA16161616 || nFormat == VtfFormat_t::R32F || nFormat == VtfFormat_t::RGB323232F || nFormat == VtfFormat_t::RGBA32323232F;
}

//-----------------------------------------------------------------------------
// Purpose: Raw image data to light values. The float formats come back as they are; RGBA16161616
//          as 0-1; 8-bit formats are taken as sRGB and made linear.
//-----------------------------------------------------------------------------
std::optional<CFloatImage> DecodeFloat( VtfFormat_t nFormat, int nWidth, int nHeight, std::span<const std::byte> data, std::string *pError )
{
	CFloatImage out;
	if ( !IsFloatFormat( nFormat ) )
	{
		const auto image = DecodeImage( nFormat, nWidth, nHeight, data, pError );
		if ( !image )
		{
			return std::nullopt;
		}
		out.m_nWidth = nWidth;
		out.m_nHeight = nHeight;
		out.m_Pixels.resize( image->m_Pixels.size() );
		for ( std::size_t i = 0; i < image->m_Pixels.size(); i++ )
		{
			out.m_Pixels[ i ] = i % 4 == 3 ? static_cast<float>( image->m_Pixels[ i ] ) / 255.0f : ToLinear( image->m_Pixels[ i ] );
		}
		return out;
	}
	const std::size_t nSize = ImageSize( nFormat, nWidth, nHeight );
	if ( nSize == 0 || data.size() < nSize )
	{
		if ( pError )
		{
			*pError = "the image data is cut off";
		}
		return std::nullopt;
	}
	out.m_nWidth = nWidth;
	out.m_nHeight = nHeight;
	const std::size_t nPixels = static_cast<std::size_t>( nWidth ) * static_cast<std::size_t>( nHeight );
	out.m_Pixels.resize( nPixels * 4 );
	const std::size_t nPixelBytes = nSize / nPixels;
	for ( std::size_t i = 0; i < nPixels; i++ )
	{
		const std::span<const std::byte> pixel = data.subspan( i * nPixelBytes, nPixelBytes );
		float *p = out.m_Pixels.data() + i * 4;
		switch ( nFormat )
		{
			case VtfFormat_t::RGBA16161616F:
				for ( int c = 0; c < 4; c++ )
				{
					p[ c ] = HalfToFloat( U16le( pixel.data() + c * 2 ) );
				}
				break;
			case VtfFormat_t::RGBA16161616:
				for ( int c = 0; c < 4; c++ )
				{
					p[ c ] = static_cast<float>( U16le( pixel.data() + c * 2 ) ) / 65535.0f;
				}
				break;
			default:
			{
				const std::size_t nChannels = nPixelBytes / 4;
				for ( std::size_t c = 0; c < 3; c++ )
				{
					p[ c ] = ReadAt<float>( pixel, std::min( c, nChannels - 1 ) * 4 );
				}
				p[ 3 ] = nChannels == 4 ? ReadAt<float>( pixel, 12 ) : 1.0f;
				break;
			}
		}
		for ( int c = 0; c < 4; c++ )
		{
			if ( !std::isfinite( p[ c ] ) )
			{
				p[ c ] = 0.0f;
			}
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CanEncode( VtfFormat_t nFormat )
{
	return nFormat != VtfFormat_t::P8 && ImageSize( nFormat, 1, 1 ) != 0;
}

//-----------------------------------------------------------------------------
// Purpose: Whether a format keeps an alpha channel (DXT1_ONEBITALPHA and the bluescreen formats
//          keep one bit).
//-----------------------------------------------------------------------------
bool HasAlpha( VtfFormat_t nFormat )
{
	switch ( nFormat )
	{
		case VtfFormat_t::RGBA8888:
		case VtfFormat_t::ABGR8888:
		case VtfFormat_t::ARGB8888:
		case VtfFormat_t::BGRA8888:
		case VtfFormat_t::RGB888_BLUESCREEN:
		case VtfFormat_t::BGR888_BLUESCREEN:
		case VtfFormat_t::DXT1_ONEBITALPHA:
		case VtfFormat_t::DXT3:
		case VtfFormat_t::DXT5:
		case VtfFormat_t::BGRA4444:
		case VtfFormat_t::BGRA5551:
		case VtfFormat_t::IA88:
		case VtfFormat_t::A8:
		case VtfFormat_t::UVWQ8888:
		case VtfFormat_t::UVLX8888:
		case VtfFormat_t::RGBA16161616F:
		case VtfFormat_t::RGBA16161616:
		case VtfFormat_t::RGBA32323232F:
			return true;
		default:
			return false;
	}
}

namespace
{

//-----------------------------------------------------------------------------
// Purpose: An 8-bit value in fewer bits, rounded.
//-----------------------------------------------------------------------------
unsigned Bits( std::uint8_t nValue, unsigned nMax )
{
	return ( nValue * nMax + 127 ) / 255;
}

//-----------------------------------------------------------------------------
// Purpose: A float as a half, rounded to nearest; too large clamps to the largest half, NaN is 0.
//-----------------------------------------------------------------------------
std::uint16_t FloatToHalf( float flValue )
{
	if ( std::isnan( flValue ) )
	{
		return 0;
	}
	const std::uint16_t nSign = std::signbit( flValue ) ? 0x8000 : 0;
	const float flSize = std::fabs( flValue );
	if ( flSize >= 65504.0f )
	{
		return static_cast<std::uint16_t>( nSign | 0x7BFF );
	}
	if ( flSize < 6.103515625e-05f ) // below the smallest normal: steps of 2^-24
	{
		return static_cast<std::uint16_t>( nSign | std::lround( std::ldexp( flSize, 24 ) ) );
	}
	int nExponent = 0;
	const float flFraction = std::frexp( flSize, &nExponent ); // 0.5 to 1
	long nMantissa = std::lround( ( flFraction * 2.0f - 1.0f ) * 1024.0f );
	int nBiased = nExponent - 1 + 15;
	if ( nMantissa == 1024 )
	{
		nMantissa = 0;
		nBiased++;
	}
	if ( nBiased >= 31 )
	{
		return static_cast<std::uint16_t>( nSign | 0x7BFF );
	}
	return static_cast<std::uint16_t>( nSign | ( nBiased << 10 ) | nMantissa );
}

//-----------------------------------------------------------------------------
// Purpose: A 4x4 block of DXT1 with one-bit alpha: clear pixels (alpha under 128) take index 3 of
//          the three-colour mode, and the solid ones the nearest of the other three colours.
//-----------------------------------------------------------------------------
void EncodeDxt1AlphaBlock( const CImage &image, int nBx, int nBy, std::byte *pOut )
{
	std::array<std::uint8_t, 64> rgba{};
	std::array<bool, 16> clear{};
	int nFirstSolid = -1;
	for ( int i = 0; i < 16; i++ )
	{
		const int x = std::min( nBx * 4 + i % 4, image.m_nWidth - 1 );
		const int y = std::min( nBy * 4 + i / 4, image.m_nHeight - 1 );
		std::memcpy( rgba.data() + i * 4, image.Pixel( x, y ), 4 );
		clear[ static_cast<std::size_t>( i ) ] = rgba[ static_cast<std::size_t>( i * 4 + 3 ) ] < 128;
		if ( !clear[ static_cast<std::size_t>( i ) ] && nFirstSolid < 0 )
		{
			nFirstSolid = i;
		}
	}
	auto *pDest = reinterpret_cast<unsigned char *>( pOut );
	if ( std::ranges::none_of( clear, []( bool b ) { return b; } ) )
	{
		EncodeDxtBlock( image, nBx, nBy, false, pOut );
		return;
	}
	if ( nFirstSolid < 0 )
	{
		std::memset( pDest, 0, 4 );
		std::memset( pDest + 4, 0xFF, 4 ); // all index 3: clear
		return;
	}
	// Clear pixels take a solid one's colour so they don't pull the endpoints.
	for ( std::size_t i = 0; i < 16; i++ )
	{
		if ( clear[ i ] )
		{
			std::memcpy( rgba.data() + i * 4, rgba.data() + nFirstSolid * 4, 3 );
		}
	}
	stb_compress_dxt_block( pDest, rgba.data(), 0, STB_DXT_HIGHQUAL );
	if ( U16le( pOut ) > U16le( pOut + 2 ) ) // three-colour mode needs the first endpoint not greater
	{
		std::swap( pDest[ 0 ], pDest[ 2 ] );
		std::swap( pDest[ 1 ], pDest[ 3 ] );
	}
	std::array<std::array<std::uint8_t, 4>, 4> colours{};
	std::uint32_t nUnused = 0;
	DecodeColourBlock( pOut, false, colours, nUnused );
	std::uint32_t nIndices = 0;
	for ( std::size_t i = 0; i < 16; i++ )
	{
		unsigned nBest = 3;
		if ( !clear[ i ] )
		{
			int nBestDistance = INT_MAX;
			for ( unsigned k = 0; k < 3; k++ )
			{
				int nDistance = 0;
				for ( std::size_t c = 0; c < 3; c++ )
				{
					const int d = static_cast<int>( rgba[ i * 4 + c ] ) - static_cast<int>( colours[ k ][ c ] );
					nDistance += d * d;
				}
				if ( nDistance < nBestDistance )
				{
					nBestDistance = nDistance;
					nBest = k;
				}
			}
		}
		nIndices |= nBest << ( 2 * i );
	}
	std::memcpy( pDest + 4, &nIndices, 4 );
}

//-----------------------------------------------------------------------------
// Purpose: A 4x4 block of DXT3: four bits of alpha per pixel, then an opaque colour block.
//-----------------------------------------------------------------------------
void EncodeDxt3Block( const CImage &image, int nBx, int nBy, std::byte *pOut )
{
	std::memset( pOut, 0, 8 );
	for ( int i = 0; i < 16; i++ )
	{
		const int x = std::min( nBx * 4 + i % 4, image.m_nWidth - 1 );
		const int y = std::min( nBy * 4 + i / 4, image.m_nHeight - 1 );
		const unsigned nAlpha = Bits( image.Pixel( x, y )[ 3 ], 15 );
		pOut[ i / 2 ] |= static_cast<std::byte>( nAlpha << ( ( i % 2 ) * 4 ) );
	}
	EncodeDxtBlock( image, nBx, nBy, false, pOut + 8 );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: RGBA8 to raw image data, in every format but P8. Float formats take the pixels as sRGB.
//-----------------------------------------------------------------------------
std::vector<std::byte> EncodeImage( const CImage &image, VtfFormat_t nFormat )
{
	if ( !CanEncode( nFormat ) )
	{
		return {};
	}
	if ( IsFloatFormat( nFormat ) )
	{
		return EncodeFloat( ToFloat( image ), nFormat );
	}
	std::vector<std::byte> out( ImageSize( nFormat, image.m_nWidth, image.m_nHeight ) );
	if ( nFormat == VtfFormat_t::DXT1 || nFormat == VtfFormat_t::DXT1_ONEBITALPHA || nFormat == VtfFormat_t::DXT3 || nFormat == VtfFormat_t::DXT5 )
	{
		const int nBlockBytes = nFormat == VtfFormat_t::DXT1 || nFormat == VtfFormat_t::DXT1_ONEBITALPHA ? 8 : 16;
		const int nBlocksWide = std::max( 1, ( image.m_nWidth + 3 ) / 4 );
		const int nBlocksHigh = std::max( 1, ( image.m_nHeight + 3 ) / 4 );
		for ( int nBy = 0; nBy < nBlocksHigh; nBy++ )
		{
			for ( int nBx = 0; nBx < nBlocksWide; nBx++ )
			{
				const std::size_t nBlock = static_cast<std::size_t>( nBy ) * static_cast<std::size_t>( nBlocksWide ) + static_cast<std::size_t>( nBx );
				std::byte *pBlock = out.data() + nBlock * static_cast<std::size_t>( nBlockBytes );
				switch ( nFormat )
				{
					case VtfFormat_t::DXT1_ONEBITALPHA: EncodeDxt1AlphaBlock( image, nBx, nBy, pBlock ); break;
					case VtfFormat_t::DXT3: EncodeDxt3Block( image, nBx, nBy, pBlock ); break;
					default: EncodeDxtBlock( image, nBx, nBy, nFormat == VtfFormat_t::DXT5, pBlock ); break;
				}
			}
		}
		return out;
	}
	const std::size_t nPixels = static_cast<std::size_t>( image.m_nWidth ) * static_cast<std::size_t>( image.m_nHeight );
	const std::size_t nPixelBytes = nPixels ? out.size() / nPixels : 0;
	for ( std::size_t i = 0; i < nPixels; i++ )
	{
		const std::uint8_t *p = image.m_Pixels.data() + i * 4;
		std::byte *d = out.data() + i * nPixelBytes;
		const auto put = [ d ]( std::initializer_list<unsigned> values ) {
			std::size_t k = 0;
			for ( const unsigned v : values )
			{
				d[ k++ ] = static_cast<std::byte>( v );
			}
		};
		const auto put16 = [ d ]( unsigned v ) {
			d[ 0 ] = static_cast<std::byte>( v & 0xFF );
			d[ 1 ] = static_cast<std::byte>( v >> 8 );
		};
		const bool bClear = p[ 3 ] < 128;
		// Luminance as Valve's tools take it from colour.
		const auto luma = [ p ] { return static_cast<unsigned>( std::lround( p[ 0 ] * 0.299 + p[ 1 ] * 0.587 + p[ 2 ] * 0.114 ) ); };
		switch ( nFormat )
		{
			case VtfFormat_t::RGBA8888: put( { p[ 0 ], p[ 1 ], p[ 2 ], p[ 3 ] } ); break;
			case VtfFormat_t::ABGR8888: put( { p[ 3 ], p[ 2 ], p[ 1 ], p[ 0 ] } ); break;
			case VtfFormat_t::ARGB8888: put( { p[ 3 ], p[ 0 ], p[ 1 ], p[ 2 ] } ); break;
			case VtfFormat_t::BGRA8888: put( { p[ 2 ], p[ 1 ], p[ 0 ], p[ 3 ] } ); break;
			case VtfFormat_t::BGRX8888: put( { p[ 2 ], p[ 1 ], p[ 0 ], 255 } ); break;
			case VtfFormat_t::RGB888: put( { p[ 0 ], p[ 1 ], p[ 2 ] } ); break;
			case VtfFormat_t::BGR888: put( { p[ 2 ], p[ 1 ], p[ 0 ] } ); break;
			// Clear pixels become the pure blue the game reads as clear.
			case VtfFormat_t::RGB888_BLUESCREEN: bClear ? put( { 0, 0, 255 } ) : put( { p[ 0 ], p[ 1 ], p[ 2 ] } ); break;
			case VtfFormat_t::BGR888_BLUESCREEN: bClear ? put( { 255, 0, 0 } ) : put( { p[ 2 ], p[ 1 ], p[ 0 ] } ); break;
			case VtfFormat_t::UVWQ8888:
			case VtfFormat_t::UVLX8888: put( { p[ 0 ], p[ 1 ], p[ 2 ], p[ 3 ] } ); break;
			case VtfFormat_t::UV88: put( { p[ 0 ], p[ 1 ] } ); break;
			case VtfFormat_t::I8: put( { luma() } ); break;
			case VtfFormat_t::IA88: put( { luma(), p[ 3 ] } ); break;
			case VtfFormat_t::A8: put( { p[ 3 ] } ); break;
			case VtfFormat_t::RGB565: put16( Bits( p[ 2 ], 31 ) << 11 | Bits( p[ 1 ], 63 ) << 5 | Bits( p[ 0 ], 31 ) ); break;
			case VtfFormat_t::BGR565: put16( Bits( p[ 0 ], 31 ) << 11 | Bits( p[ 1 ], 63 ) << 5 | Bits( p[ 2 ], 31 ) ); break;
			case VtfFormat_t::BGRA4444: put16( Bits( p[ 3 ], 15 ) << 12 | Bits( p[ 0 ], 15 ) << 8 | Bits( p[ 1 ], 15 ) << 4 | Bits( p[ 2 ], 15 ) ); break;
			case VtfFormat_t::BGRA5551: put16( ( bClear ? 0u : 1u ) << 15 | Bits( p[ 0 ], 31 ) << 10 | Bits( p[ 1 ], 31 ) << 5 | Bits( p[ 2 ], 31 ) ); break;
			case VtfFormat_t::BGRX5551: put16( 1u << 15 | Bits( p[ 0 ], 31 ) << 10 | Bits( p[ 1 ], 31 ) << 5 | Bits( p[ 2 ], 31 ) ); break;
			default: return {};
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Light values to raw image data: the float formats keep values above 1; the others are
//          clamped and go through 8 bits.
//-----------------------------------------------------------------------------
std::vector<std::byte> EncodeFloat( const CFloatImage &image, VtfFormat_t nFormat )
{
	if ( !IsFloatFormat( nFormat ) )
	{
		return EncodeImage( ToEightBit( image ), nFormat );
	}
	std::vector<std::byte> out( ImageSize( nFormat, image.m_nWidth, image.m_nHeight ) );
	const std::size_t nPixels = static_cast<std::size_t>( image.m_nWidth ) * static_cast<std::size_t>( image.m_nHeight );
	const std::size_t nPixelBytes = nPixels ? out.size() / nPixels : 0;
	for ( std::size_t i = 0; i < nPixels; i++ )
	{
		const float *p = image.m_Pixels.data() + i * 4;
		std::byte *d = out.data() + i * nPixelBytes;
		const auto put16 = [ d ]( std::size_t nAt, unsigned v ) {
			d[ nAt * 2 ] = static_cast<std::byte>( v & 0xFF );
			d[ nAt * 2 + 1 ] = static_cast<std::byte>( v >> 8 );
		};
		switch ( nFormat )
		{
			case VtfFormat_t::RGBA16161616F:
				for ( std::size_t c = 0; c < 4; c++ )
				{
					put16( c, FloatToHalf( p[ c ] ) );
				}
				break;
			case VtfFormat_t::RGBA16161616:
				for ( std::size_t c = 0; c < 4; c++ )
				{
					put16( c, static_cast<unsigned>( std::lround( std::clamp( p[ c ], 0.0f, 1.0f ) * 65535.0f ) ) );
				}
				break;
			default:
				std::memcpy( d, p, nPixelBytes ); // R32F, RGB323232F, RGBA32323232F: the first channels as they are
				break;
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: sRGB 8-bit to light values; alpha stays straight.
//-----------------------------------------------------------------------------
CFloatImage ToFloat( const CImage &image )
{
	CFloatImage out;
	out.m_nWidth = image.m_nWidth;
	out.m_nHeight = image.m_nHeight;
	out.m_Pixels.resize( image.m_Pixels.size() );
	for ( std::size_t i = 0; i < image.m_Pixels.size(); i++ )
	{
		out.m_Pixels[ i ] = i % 4 == 3 ? static_cast<float>( image.m_Pixels[ i ] ) / 255.0f : ToLinear( image.m_Pixels[ i ] );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Light values to sRGB 8-bit, clamped; alpha stays straight.
//-----------------------------------------------------------------------------
CImage ToEightBit( const CFloatImage &image )
{
	CImage out{ image.m_nWidth, image.m_nHeight };
	for ( std::size_t i = 0; i < image.m_Pixels.size(); i++ )
	{
		out.m_Pixels[ i ] = i % 4 == 3 ? static_cast<std::uint8_t>( std::lround( std::clamp( image.m_Pixels[ i ], 0.0f, 1.0f ) * 255.0f ) ) : FromLinear( image.m_Pixels[ i ] );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: The next mipmap level of light values: each pixel the average of up to four.
//-----------------------------------------------------------------------------
CFloatImage HalfSize( const CFloatImage &image )
{
	CFloatImage out;
	out.m_nWidth = std::max( 1, image.m_nWidth / 2 );
	out.m_nHeight = std::max( 1, image.m_nHeight / 2 );
	out.m_Pixels.resize( static_cast<std::size_t>( out.m_nWidth ) * static_cast<std::size_t>( out.m_nHeight ) * 4 );
	for ( int y = 0; y < out.m_nHeight; y++ )
	{
		for ( int x = 0; x < out.m_nWidth; x++ )
		{
			std::array<float, 4> sum{};
			int nCount = 0;
			for ( int dy = 0; dy < 2; dy++ )
			{
				for ( int dx = 0; dx < 2; dx++ )
				{
					const int sx = x * 2 + dx;
					const int sy = y * 2 + dy;
					if ( sx < image.m_nWidth && sy < image.m_nHeight )
					{
						const float *p = image.Pixel( sx, sy );
						for ( std::size_t c = 0; c < 4; c++ )
						{
							sum[ c ] += p[ c ];
						}
						nCount++;
					}
				}
			}
			float *d = out.m_Pixels.data() + ( static_cast<std::size_t>( y ) * static_cast<std::size_t>( out.m_nWidth ) + static_cast<std::size_t>( x ) ) * 4;
			for ( std::size_t c = 0; c < 4; c++ )
			{
				d[ c ] = sum[ c ] / static_cast<float>( nCount );
			}
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: The alpha flags Valve's tools set: ONEBITALPHA when every pixel is clear or solid and
//          some are clear, EIGHTBITALPHA when some are in between, none when all are solid.
//-----------------------------------------------------------------------------
std::uint32_t AlphaFlags( std::span<const CImage> images )
{
	bool bClear = false;
	for ( const CImage &image : images )
	{
		for ( std::size_t i = 3; i < image.m_Pixels.size(); i += 4 )
		{
			const std::uint8_t nAlpha = image.m_Pixels[ i ];
			if ( nAlpha != 0 && nAlpha != 255 )
			{
				return vtf_flag::EIGHTBITALPHA;
			}
			bClear = bClear || nAlpha == 0;
		}
	}
	return bClear ? vtf_flag::ONEBITALPHA : 0;
}

//-----------------------------------------------------------------------------
// Purpose: The resources of a file other than its two images, to carry into a rewrite.
//-----------------------------------------------------------------------------
std::vector<VtfExtraResource_t> ExtraResources( const CVtf &vtf )
{
	std::vector<VtfExtraResource_t> extras;
	for ( const VtfResource_t &resource : vtf.m_Resources )
	{
		if ( resource.m_Tag == LOW_RES_RESOURCE || resource.m_Tag == IMAGE_RESOURCE )
		{
			continue;
		}
		VtfExtraResource_t extra{ resource.m_Tag, resource.m_nFlags, resource.m_nValue, {} };
		if ( !( resource.m_nFlags & VtfResource_t::NO_DATA ) )
		{
			const auto data = vtf.Resource( resource.m_Tag );
			extra.m_Data.assign( data.begin(), data.end() );
		}
		extras.push_back( std::move( extra ) );
	}
	return extras;
}

//-----------------------------------------------------------------------------
// Purpose: The next mipmap level of light values with a filter (negative ringing clamped to 0).
//-----------------------------------------------------------------------------
namespace
{
CFloatImage HalveAxis( const CFloatImage &image, MipFilter_t nFilter, bool bAcross, bool bWrap );
} // namespace

CFloatImage HalfSize( const CFloatImage &image, MipFilter_t nFilter, bool bWrapX, bool bWrapY )
{
	CFloatImage out = HalveAxis( HalveAxis( image, nFilter, true, bWrapX ), nFilter, false, bWrapY );
	for ( float &flValue : out.m_Pixels )
	{
		flValue = std::max( flValue, 0.0f );
	}
	return out;
}

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::byte> Encode( const CImage &image, VtfFormat_t nFormat )
{
	return EncodeImage( image, nFormat );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::byte> Encode( const CFloatImage &image, VtfFormat_t nFormat )
{
	return EncodeFloat( image, nFormat );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CImage EightBit( const CImage &image )
{
	return image;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CImage EightBit( const CFloatImage &image )
{
	return ToEightBit( image );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float LinearAt( const CImage &image, std::size_t i )
{
	return ToLinear( image.m_Pixels[ i ] );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float LinearAt( const CFloatImage &image, std::size_t i )
{
	return image.m_Pixels[ i ];
}

//-----------------------------------------------------------------------------
// Purpose: Two pictures blended half and half (two slices of a volume texture becoming one).
//-----------------------------------------------------------------------------
CImage Blend( const CImage &a, const CImage &b, bool bRaw )
{
	CImage out{ a.m_nWidth, a.m_nHeight };
	for ( std::size_t i = 0; i < out.m_Pixels.size(); i++ )
	{
		out.m_Pixels[ i ] = bRaw || i % 4 == 3 ? static_cast<std::uint8_t>( ( a.m_Pixels[ i ] + b.m_Pixels[ i ] + 1 ) / 2 )
											   : FromLinear( ( ToLinear( a.m_Pixels[ i ] ) + ToLinear( b.m_Pixels[ i ] ) ) * 0.5f );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CFloatImage Blend( const CFloatImage &a, const CFloatImage &b, bool )
{
	CFloatImage out = a;
	for ( std::size_t i = 0; i < out.m_Pixels.size(); i++ )
	{
		out.m_Pixels[ i ] = ( a.m_Pixels[ i ] + b.m_Pixels[ i ] ) * 0.5f;
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Each pixel's colour taken as a direction (0-255 for -1 to 1) and made one long again.
//-----------------------------------------------------------------------------
void Renormalize( CImage &image )
{
	for ( std::size_t i = 0; i < image.m_Pixels.size(); i += 4 )
	{
		std::array<float, 3> v{};
		for ( std::size_t c = 0; c < 3; c++ )
		{
			v[ c ] = static_cast<float>( image.m_Pixels[ i + c ] ) / 127.5f - 1.0f;
		}
		const float flLength = std::sqrt( v[ 0 ] * v[ 0 ] + v[ 1 ] * v[ 1 ] + v[ 2 ] * v[ 2 ] );
		if ( flLength < 1e-6f )
		{
			continue;
		}
		for ( std::size_t c = 0; c < 3; c++ )
		{
			image.m_Pixels[ i + c ] = static_cast<std::uint8_t>( std::lround( std::clamp( ( v[ c ] / flLength + 1.0f ) * 127.5f, 0.0f, 255.0f ) ) );
		}
	}
}

void Renormalize( CFloatImage & )
{}

//-----------------------------------------------------------------------------
// Purpose: A filter's weight t output texels from the centre, and how far it reaches.
//-----------------------------------------------------------------------------
float FilterWeight( MipFilter_t nFilter, float t )
{
	t = std::abs( t );
	switch ( nFilter )
	{
		case MipFilter_t::MIP_FILTER_TRIANGLE: return std::max( 0.0f, 1.0f - t );
		case MipFilter_t::MIP_FILTER_MITCHELL:
		{
			constexpr float B = 1.0f / 3.0f;
			constexpr float C = 1.0f / 3.0f;
			if ( t < 1.0f )
			{
				return ( ( 12 - 9 * B - 6 * C ) * t * t * t + ( -18 + 12 * B + 6 * C ) * t * t + ( 6 - 2 * B ) ) / 6.0f;
			}
			if ( t < 2.0f )
			{
				return ( ( -B - 6 * C ) * t * t * t + ( 6 * B + 30 * C ) * t * t + ( -12 * B - 48 * C ) * t + ( 8 * B + 24 * C ) ) / 6.0f;
			}
			return 0.0f;
		}
		case MipFilter_t::MIP_FILTER_KAISER:
		{
			// A sinc windowed by Kaiser's bell, three texels wide, alpha 4.
			constexpr float WIDTH = 3.0f;
			constexpr float ALPHA = 4.0f;
			if ( t >= WIDTH )
			{
				return 0.0f;
			}
			const auto bessel = []( float x ) {
				// I0 by its series.
				float flSum = 1.0f;
				float flTerm = 1.0f;
				for ( int k = 1; k < 20; k++ )
				{
					flTerm *= ( x / ( 2.0f * static_cast<float>( k ) ) ) * ( x / ( 2.0f * static_cast<float>( k ) ) );
					flSum += flTerm;
				}
				return flSum;
			};
			const float flSinc = t < 1e-5f ? 1.0f : std::sin( std::numbers::pi_v<float> * t ) / ( std::numbers::pi_v<float> * t );
			const float flX = t / WIDTH;
			return flSinc * bessel( ALPHA * std::sqrt( 1.0f - flX * flX ) ) / bessel( ALPHA );
		}
		default: return t <= 0.5f ? 1.0f : 0.0f;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float FilterReach( MipFilter_t nFilter )
{
	switch ( nFilter )
	{
		case MipFilter_t::MIP_FILTER_TRIANGLE: return 1.0f;
		case MipFilter_t::MIP_FILTER_MITCHELL: return 2.0f;
		case MipFilter_t::MIP_FILTER_KAISER: return 3.0f;
		default: return 0.5f;
	}
}

//-----------------------------------------------------------------------------
// Purpose: One axis halved: each output texel a weighted sum of the source texels around it.
//-----------------------------------------------------------------------------
CFloatImage HalveAxis( const CFloatImage &image, MipFilter_t nFilter, bool bAcross, bool bWrap )
{
	const int nFrom = bAcross ? image.m_nWidth : image.m_nHeight;
	const int nTo = std::max( 1, nFrom / 2 );
	CFloatImage out;
	out.m_nWidth = bAcross ? nTo : image.m_nWidth;
	out.m_nHeight = bAcross ? image.m_nHeight : nTo;
	out.m_Pixels.assign( static_cast<std::size_t>( out.m_nWidth ) * static_cast<std::size_t>( out.m_nHeight ) * 4, 0.0f );
	if ( nFrom == 1 )
	{
		out.m_Pixels = image.m_Pixels;
		return out;
	}
	const float flReach = FilterReach( nFilter );
	const float flScale = static_cast<float>( nFrom ) / static_cast<float>( nTo );
	// The taps of each output texel along the axis, shared by every row.
	std::vector<std::vector<std::pair<int, float>>> taps( static_cast<std::size_t>( nTo ) );
	for ( int o = 0; o < nTo; o++ )
	{
		const float flCentre = ( static_cast<float>( o ) + 0.5f ) * flScale;
		const int nFirst = static_cast<int>( std::floor( flCentre - flReach * flScale ) );
		const int nLast = static_cast<int>( std::ceil( flCentre + flReach * flScale ) );
		float flTotal = 0.0f;
		for ( int s = nFirst; s <= nLast; s++ )
		{
			const float flWeight = FilterWeight( nFilter, ( static_cast<float>( s ) + 0.5f - flCentre ) / flScale );
			if ( flWeight == 0.0f )
			{
				continue;
			}
			const int nAt = bWrap ? ( ( s % nFrom ) + nFrom ) % nFrom : std::clamp( s, 0, nFrom - 1 );
			taps[ static_cast<std::size_t>( o ) ].emplace_back( nAt, flWeight );
			flTotal += flWeight;
		}
		for ( auto &tap : taps[ static_cast<std::size_t>( o ) ] )
		{
			tap.second /= flTotal;
		}
	}
	const int nLines = bAcross ? image.m_nHeight : image.m_nWidth;
	for ( int nLine = 0; nLine < nLines; nLine++ )
	{
		for ( int o = 0; o < nTo; o++ )
		{
			const int ox = bAcross ? o : nLine;
			const int oy = bAcross ? nLine : o;
			float *d = out.m_Pixels.data() + ( static_cast<std::size_t>( oy ) * static_cast<std::size_t>( out.m_nWidth ) + static_cast<std::size_t>( ox ) ) * 4;
			for ( const auto &[ nAt, flWeight ] : taps[ static_cast<std::size_t>( o ) ] )
			{
				const float *p = bAcross ? image.Pixel( nAt, nLine ) : image.Pixel( nLine, nAt );
				for ( std::size_t c = 0; c < 4; c++ )
				{
					d[ c ] += p[ c ] * flWeight;
				}
			}
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: The next mipmap level. A normal map's texels are directions, not colours: they are
//          averaged as they are stored and made one long again.
//-----------------------------------------------------------------------------
CImage NextLevel( const CImage &image, const VtfWriteOptions_t &options )
{
	const bool bNormal = options.m_bNormalMap;
	if ( options.m_nMipFilter != MipFilter_t::MIP_FILTER_BOX )
	{
		const bool bWrapX = !( options.m_nFlags & vtf_flag::CLAMPS );
		const bool bWrapY = !( options.m_nFlags & vtf_flag::CLAMPT );
		if ( !bNormal )
		{
			return ToEightBit( HalfSize( ToFloat( image ), options.m_nMipFilter, bWrapX, bWrapY ) );
		}
		// Raw values for a normal map, then one long again.
		CFloatImage raw;
		raw.m_nWidth = image.m_nWidth;
		raw.m_nHeight = image.m_nHeight;
		raw.m_Pixels.assign( image.m_Pixels.begin(), image.m_Pixels.end() );
		const CFloatImage half = HalfSize( raw, options.m_nMipFilter, bWrapX, bWrapY );
		CImage out{ half.m_nWidth, half.m_nHeight };
		for ( std::size_t i = 0; i < out.m_Pixels.size(); i++ )
		{
			out.m_Pixels[ i ] = static_cast<std::uint8_t>( std::lround( std::clamp( half.m_Pixels[ i ], 0.0f, 255.0f ) ) );
		}
		Renormalize( out );
		return out;
	}
	if ( !bNormal )
	{
		return HalfSize( image );
	}
	CImage out{ std::max( 1, image.m_nWidth / 2 ), std::max( 1, image.m_nHeight / 2 ) };
	for ( int y = 0; y < out.m_nHeight; y++ )
	{
		for ( int x = 0; x < out.m_nWidth; x++ )
		{
			std::array<int, 4> sum{};
			int nCount = 0;
			for ( int dy = 0; dy < 2; dy++ )
			{
				for ( int dx = 0; dx < 2; dx++ )
				{
					const int sx = x * 2 + dx;
					const int sy = y * 2 + dy;
					if ( sx < image.m_nWidth && sy < image.m_nHeight )
					{
						const std::uint8_t *p = image.Pixel( sx, sy );
						for ( std::size_t c = 0; c < 4; c++ )
						{
							sum[ c ] += p[ c ];
						}
						nCount++;
					}
				}
			}
			std::uint8_t *d = out.Pixel( x, y );
			for ( std::size_t c = 0; c < 4; c++ )
			{
				d[ c ] = static_cast<std::uint8_t>( ( sum[ c ] + nCount / 2 ) / nCount );
			}
		}
	}
	Renormalize( out );
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CFloatImage NextLevel( const CFloatImage &image, const VtfWriteOptions_t &options )
{
	if ( options.m_nMipFilter == MipFilter_t::MIP_FILTER_BOX )
	{
		return HalfSize( image );
	}
	return HalfSize( image, options.m_nMipFilter, !( options.m_nFlags & vtf_flag::CLAMPS ), !( options.m_nFlags & vtf_flag::CLAMPT ) );
}

// What goes in a VTF's header and where its parts sit, apart from the parts themselves.
struct Layout_t
{
	int m_nMinor = 2;
	int m_nWidth = 0;
	int m_nHeight = 0;
	int m_nDepth = 1;
	std::uint32_t m_nFlags = 0;
	int m_nFrames = 1;
	int m_nFirstFrame = 0;
	std::array<float, 3> m_vecReflectivity{};
	float m_flBumpScale = 1.0f;
	VtfFormat_t m_nFormat = VtfFormat_t::DXT1;
	int m_nMipCount = 1;
	VtfFormat_t m_nLowResFormat = VtfFormat_t::VTF_FORMAT_NONE; // none: no thumbnail
	int m_nLowResWidth = 0;
	int m_nLowResHeight = 0;
};

//-----------------------------------------------------------------------------
// Purpose: Valve's layout: the header (7.3+: with the resource list, sorted by tag), the low-res
//          image, the image data from the smallest mipmap up (frames, faces, slices within each),
//          then the data of other resources. Older versions leave the other resources out.
//-----------------------------------------------------------------------------
std::vector<std::byte> Assemble( const Layout_t &layout, std::span<const std::byte> lowRes, std::span<const std::byte> imageData, const std::vector<VtfExtraResource_t> &extras )
{
	const bool bLowRes = layout.m_nLowResFormat != VtfFormat_t::VTF_FORMAT_NONE && !lowRes.empty();
	std::vector<VtfExtraResource_t> resources;
	if ( layout.m_nMinor >= 3 )
	{
		if ( bLowRes )
		{
			resources.push_back( { LOW_RES_RESOURCE, 0, 0, {} } );
		}
		resources.push_back( { IMAGE_RESOURCE, 0, 0, {} } );
		for ( const VtfExtraResource_t &extra : extras )
		{
			if ( extra.m_Tag != LOW_RES_RESOURCE && extra.m_Tag != IMAGE_RESOURCE )
			{
				resources.push_back( extra );
			}
		}
		std::ranges::stable_sort( resources, {}, &VtfExtraResource_t::m_Tag );
	}
	const std::size_t nHeaderSize = layout.m_nMinor >= 3 ? RESOURCES_OFFSET + resources.size() * 8
														 : static_cast<std::size_t>( layout.m_nMinor == 2 ? HEADER_SIZE_72 : HEADER_SIZE_71 );

	std::vector<std::byte> out( nHeaderSize );
	std::memcpy( out.data(), "VTF\0", 4 );
	WriteAt<std::uint32_t>( out, 4, 7 );
	WriteAt<std::uint32_t>( out, 8, static_cast<std::uint32_t>( layout.m_nMinor ) );
	WriteAt<std::uint32_t>( out, 12, static_cast<std::uint32_t>( nHeaderSize ) );
	WriteAt<std::uint16_t>( out, 16, static_cast<std::uint16_t>( layout.m_nWidth ) );
	WriteAt<std::uint16_t>( out, 18, static_cast<std::uint16_t>( layout.m_nHeight ) );
	WriteAt<std::uint32_t>( out, 20, layout.m_nFlags );
	WriteAt<std::uint16_t>( out, 24, static_cast<std::uint16_t>( layout.m_nFrames ) );
	WriteAt<std::uint16_t>( out, 26, static_cast<std::uint16_t>( layout.m_nFirstFrame ) );
	for ( std::size_t c = 0; c < 3; c++ )
	{
		WriteAt<float>( out, 32 + c * 4, layout.m_vecReflectivity[ c ] );
	}
	WriteAt<float>( out, 48, layout.m_flBumpScale );
	WriteAt<std::int32_t>( out, 52, static_cast<std::int32_t>( layout.m_nFormat ) );
	out[ 56 ] = static_cast<std::byte>( layout.m_nMipCount );
	WriteAt<std::uint32_t>( out, 57, bLowRes ? static_cast<std::uint32_t>( layout.m_nLowResFormat ) : NO_FORMAT );
	out[ 61 ] = static_cast<std::byte>( bLowRes ? layout.m_nLowResWidth : 0 );
	out[ 62 ] = static_cast<std::byte>( bLowRes ? layout.m_nLowResHeight : 0 );
	if ( layout.m_nMinor >= 2 )
	{
		WriteAt<std::uint16_t>( out, 63, static_cast<std::uint16_t>( layout.m_nDepth ) );
	}
	if ( layout.m_nMinor >= 3 )
	{
		WriteAt<std::uint32_t>( out, 68, static_cast<std::uint32_t>( resources.size() ) );
	}

	std::vector<std::uint32_t> offsets( resources.size() );
	const auto offsetOf = [ & ]( std::array<std::uint8_t, 3> tag ) {
		for ( std::size_t i = 0; i < resources.size(); i++ )
		{
			if ( resources[ i ].m_Tag == tag )
			{
				offsets[ i ] = static_cast<std::uint32_t>( out.size() );
			}
		}
	};
	offsetOf( LOW_RES_RESOURCE );
	if ( bLowRes )
	{
		out.insert( out.end(), lowRes.begin(), lowRes.end() );
	}
	offsetOf( IMAGE_RESOURCE );
	out.insert( out.end(), imageData.begin(), imageData.end() );
	for ( std::size_t i = 0; i < resources.size(); i++ )
	{
		const VtfExtraResource_t &resource = resources[ i ];
		if ( resource.m_Tag == LOW_RES_RESOURCE || resource.m_Tag == IMAGE_RESOURCE )
		{
			continue;
		}
		if ( resource.m_nFlags & VtfResource_t::NO_DATA )
		{
			offsets[ i ] = resource.m_nValue;
			continue;
		}
		offsets[ i ] = static_cast<std::uint32_t>( out.size() );
		out.resize( out.size() + 4 );
		WriteAt<std::uint32_t>( out, out.size() - 4, static_cast<std::uint32_t>( resource.m_Data.size() ) );
		out.insert( out.end(), resource.m_Data.begin(), resource.m_Data.end() );
	}
	for ( std::size_t i = 0; i < resources.size(); i++ )
	{
		const std::size_t nEntry = RESOURCES_OFFSET + i * 8;
		for ( std::size_t k = 0; k < 3; k++ )
		{
			out[ nEntry + k ] = static_cast<std::byte>( resources[ i ].m_Tag[ k ] );
		}
		out[ nEntry + 3 ] = static_cast<std::byte>( resources[ i ].m_nFlags );
		WriteAt<std::uint32_t>( out, nEntry + 4, offsets[ i ] );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: The thumbnail's size for an image: halved until 16 or less on each side.
//-----------------------------------------------------------------------------
std::pair<int, int> LowResSize( int nWidth, int nHeight )
{
	while ( nWidth > 16 || nHeight > 16 )
	{
		nWidth = std::max( 1, nWidth / 2 );
		nHeight = std::max( 1, nHeight / 2 );
	}
	return { nWidth, nHeight };
}

//-----------------------------------------------------------------------------
// Purpose: Encodes every mipmap of every picture and lays the file out (see Assemble).
//-----------------------------------------------------------------------------
template <typename Image_t>
std::vector<std::byte> WriteImages( std::span<const Image_t> images, int nFaces, const VtfWriteOptions_t &options )
{
	const int nDepth = std::max( 1, options.m_nDepth );
	const std::size_t nPerFrame = static_cast<std::size_t>( nFaces ) * static_cast<std::size_t>( nDepth );
	if ( images.empty() || ( nFaces != 1 && nFaces != 6 ) || ( nFaces == 6 && nDepth > 1 ) || images.size() % nPerFrame != 0 || !CanEncode( options.m_nFormat ) )
	{
		return {};
	}
	const int nWidth = images.front().m_nWidth;
	const int nHeight = images.front().m_nHeight;
	const std::size_t nFrames = images.size() / nPerFrame;
	if ( nWidth <= 0 || nHeight <= 0 || nWidth > 65535 || nHeight > 65535 || nDepth > 65535 || nFrames > 65535 ||
		std::ranges::any_of( images, [ nWidth, nHeight ]( const Image_t &image ) { return image.m_nWidth != nWidth || image.m_nHeight != nHeight; } ) )
	{
		return {};
	}
	const int nMinor = std::clamp( options.m_nVersionMinor, 1, 5 );
	const bool bCube = nFaces == 6;
	const int nStoredFaces = bCube && nMinor < 5 ? 7 : nFaces; // the sphere map, before 7.5
	const int nMipCount = options.m_bMipmaps ? MipCountFor( std::max( nWidth, nDepth ), nHeight ) : 1;

	// Every mipmap of every face of every frame: levels[ frame * faces + face ][ level ][ slice ].
	std::vector<std::vector<std::vector<Image_t>>> levels;
	for ( std::size_t nImage = 0; nImage < nFrames * static_cast<std::size_t>( nFaces ); nImage++ )
	{
		std::vector<std::vector<Image_t>> mips;
		mips.emplace_back( images.begin() + static_cast<std::ptrdiff_t>( nImage * static_cast<std::size_t>( nDepth ) ), images.begin() + static_cast<std::ptrdiff_t>( ( nImage + 1 ) * static_cast<std::size_t>( nDepth ) ) );
		for ( int nLevel = 1; nLevel < nMipCount; nLevel++ )
		{
			const std::vector<Image_t> &above = mips.back();
			std::vector<Image_t> halved;
			for ( const Image_t &slice : above )
			{
				halved.push_back( NextLevel( slice, options ) );
			}
			// A volume's depth halves too: slices are blended in pairs.
			std::vector<Image_t> level;
			for ( std::size_t i = 0; i < halved.size(); i += 2 )
			{
				if ( i + 1 < halved.size() )
				{
					Image_t blended = Blend( halved[ i ], halved[ i + 1 ], options.m_bNormalMap );
					if ( options.m_bNormalMap )
					{
						Renormalize( blended );
					}
					level.push_back( std::move( blended ) );
				}
				else
				{
					level.push_back( halved[ i ] );
				}
			}
			mips.push_back( std::move( level ) );
		}
		levels.push_back( std::move( mips ) );
	}

	Layout_t layout;
	layout.m_nMinor = nMinor;
	layout.m_nWidth = nWidth;
	layout.m_nHeight = nHeight;
	layout.m_nDepth = nDepth;
	layout.m_nFlags = options.m_nFlags | ( bCube ? vtf_flag::ENVMAP : 0 );
	layout.m_nFrames = static_cast<int>( nFrames );
	layout.m_nFirstFrame = options.m_nFirstFrame;
	layout.m_flBumpScale = options.m_flBumpScale;
	layout.m_nFormat = options.m_nFormat;
	layout.m_nMipCount = nMipCount;

	std::vector<std::byte> lowRes;
	if ( options.m_bLowRes )
	{
		const auto [ nLowWidth, nLowHeight ] = LowResSize( nWidth, nHeight );
		lowRes = EncodeImage( Resize( EightBit( images.front() ), nLowWidth, nLowHeight ), VtfFormat_t::DXT1 );
		layout.m_nLowResFormat = VtfFormat_t::DXT1;
		layout.m_nLowResWidth = nLowWidth;
		layout.m_nLowResHeight = nLowHeight;
	}

	// The average colour of the first frame in linear light, which the engine uses for bounced
	// light.
	std::array<double, 3> reflectivity{};
	double flPixels = 0.0;
	for ( std::size_t i = 0; i < nPerFrame; i++ )
	{
		const Image_t &image = images[ i ];
		for ( std::size_t p = 0; p < image.m_Pixels.size(); p += 4 )
		{
			for ( std::size_t c = 0; c < 3; c++ )
			{
				reflectivity[ c ] += LinearAt( image, p + c );
			}
			flPixels += 1.0;
		}
	}
	for ( std::size_t c = 0; c < 3; c++ )
	{
		layout.m_vecReflectivity[ c ] = static_cast<float>( flPixels > 0.0 ? reflectivity[ c ] / flPixels : 0.0 );
	}

	std::vector<std::byte> imageData;
	for ( int nLevel = nMipCount - 1; nLevel >= 0; nLevel-- )
	{
		const std::size_t nLevelSize = ImageSize( options.m_nFormat, std::max( 1, nWidth >> nLevel ), std::max( 1, nHeight >> nLevel ) );
		const std::size_t nSlices = static_cast<std::size_t>( std::max( 1, nDepth >> nLevel ) );
		for ( std::size_t nFrame = 0; nFrame < nFrames; nFrame++ )
		{
			for ( int nFace = 0; nFace < nStoredFaces; nFace++ )
			{
				if ( nFace >= nFaces )
				{
					imageData.resize( imageData.size() + nLevelSize ); // the sphere map: black
					continue;
				}
				const auto &slices = levels[ nFrame * static_cast<std::size_t>( nFaces ) + static_cast<std::size_t>( nFace ) ][ static_cast<std::size_t>( nLevel ) ];
				for ( std::size_t nSlice = 0; nSlice < nSlices && nSlice < slices.size(); nSlice++ )
				{
					const auto encoded = Encode( slices[ nSlice ], options.m_nFormat );
					imageData.insert( imageData.end(), encoded.begin(), encoded.end() );
				}
			}
		}
	}
	return Assemble( layout, lowRes, imageData, options.m_Resources );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The image's width and height don't have to be powers of two, but the game expects them
//          to be.
//-----------------------------------------------------------------------------
std::vector<std::byte> WriteVtf( const CImage &image, const VtfWriteOptions_t &options )
{
	return WriteImages( std::span{ &image, 1 }, 1, options );
}

//-----------------------------------------------------------------------------
// Purpose: Several images, all the same size, frame after frame, each with nFaces faces.
//-----------------------------------------------------------------------------
std::vector<std::byte> WriteVtf( std::span<const CImage> images, int nFaces, const VtfWriteOptions_t &options )
{
	return WriteImages( images, nFaces, options );
}

//-----------------------------------------------------------------------------
// Purpose: The same from light values, for the float formats (HDR).
//-----------------------------------------------------------------------------
std::vector<std::byte> WriteVtf( std::span<const CFloatImage> images, int nFaces, const VtfWriteOptions_t &options )
{
	return WriteImages( images, nFaces, options );
}

//-----------------------------------------------------------------------------
// Purpose: The thumbnail's bytes: after the header before 7.3, else where its resource says.
//-----------------------------------------------------------------------------
std::span<const std::byte> CVtf::LowRes() const
{
	if ( m_Info.m_nLowResFormat == VtfFormat_t::VTF_FORMAT_NONE || m_Info.m_nLowResWidth <= 0 || m_Info.m_nLowResHeight <= 0 || m_Data.size() < 16 )
	{
		return {};
	}
	std::size_t nOffset = ReadAt<std::uint32_t>( m_Data, 12 );
	if ( m_Info.m_nVersionMinor >= 3 )
	{
		const auto it = std::ranges::find( m_Resources, LOW_RES_RESOURCE, &VtfResource_t::m_Tag );
		if ( it == m_Resources.end() )
		{
			return {};
		}
		nOffset = it->m_nValue;
	}
	const std::size_t nSize = ImageSize( m_Info.m_nLowResFormat, m_Info.m_nLowResWidth, m_Info.m_nLowResHeight );
	if ( nOffset > m_Data.size() || m_Data.size() - nOffset < nSize )
	{
		return {};
	}
	return std::span{ m_Data }.subspan( nOffset, nSize );
}

//-----------------------------------------------------------------------------
// Purpose: The average colour of the first frame in linear light, as the header keeps it.
//-----------------------------------------------------------------------------
std::array<float, 3> Reflectivity( const CVtf &vtf )
{
	std::array<double, 3> sum{};
	double flPixels = 0.0;
	const int nFaces = vtf.m_Info.m_nFaces == 7 ? 6 : vtf.m_Info.m_nFaces;
	for ( int nFace = 0; nFace < nFaces; nFace++ )
	{
		const auto light = vtf.DecodeFloat( 0, 0, nFace );
		if ( !light )
		{
			continue;
		}
		for ( std::size_t i = 0; i < light->m_Pixels.size(); i += 4 )
		{
			for ( std::size_t c = 0; c < 3; c++ )
			{
				sum[ c ] += light->m_Pixels[ i + c ];
			}
			flPixels += 1.0;
		}
	}
	std::array<float, 3> out{};
	for ( std::size_t c = 0; c < 3; c++ )
	{
		out[ c ] = static_cast<float>( flPixels > 0.0 ? sum[ c ] / flPixels : 0.0 );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: What a header edit starts from: the file as it is.
//-----------------------------------------------------------------------------
VtfHeaderEdit_t HeaderOf( const CVtf &vtf )
{
	const VtfInfo_t &info = vtf.m_Info;
	VtfHeaderEdit_t edit;
	edit.m_nVersionMinor = std::clamp( info.m_nVersionMinor, 1, 5 );
	edit.m_nFlags = info.m_nFlags;
	edit.m_nFirstFrame = info.m_nFirstFrame;
	edit.m_flBumpScale = info.m_flBumpScale;
	edit.m_vecReflectivity = info.m_vecReflectivity;
	edit.m_Resources = ExtraResources( vtf );
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose: The file again with another header, its pictures copied as they are.
//-----------------------------------------------------------------------------
std::vector<std::byte> RewriteVtf( const CVtf &vtf, const VtfHeaderEdit_t &edit )
{
	const VtfInfo_t &info = vtf.m_Info;
	Layout_t layout;
	layout.m_nMinor = std::clamp( edit.m_nVersionMinor, 1, 5 );
	layout.m_nWidth = info.m_nWidth;
	layout.m_nHeight = info.m_nHeight;
	layout.m_nDepth = info.m_nDepth;
	// ENVMAP says how many faces the data has, so it follows the file.
	layout.m_nFlags = ( edit.m_nFlags & ~vtf_flag::ENVMAP ) | ( info.m_nFlags & vtf_flag::ENVMAP );
	layout.m_nFrames = info.m_nFrames;
	layout.m_nFirstFrame = edit.m_nFirstFrame;
	layout.m_vecReflectivity = edit.m_vecReflectivity;
	layout.m_flBumpScale = edit.m_flBumpScale;
	layout.m_nFormat = info.m_nFormat;
	layout.m_nMipCount = info.m_nMipCount;
	if ( layout.m_nDepth > 1 && layout.m_nMinor < 2 )
	{
		return {}; // 7.1 has no depth
	}

	const bool bCube = info.m_nFaces >= 6;
	const int nStoredFaces = bCube ? ( layout.m_nMinor < 5 ? 7 : 6 ) : info.m_nFaces;
	if ( bCube && nStoredFaces == 7 && layout.m_nFirstFrame == 0xFFFF )
	{
		layout.m_nFirstFrame = 0; // -1 there says "no sphere map"
	}
	std::vector<std::byte> imageData;
	if ( nStoredFaces == info.m_nFaces )
	{
		const auto first = vtf.Image( info.m_nMipCount - 1 );
		const std::size_t nStart = static_cast<std::size_t>( first.data() - vtf.m_Data.data() );
		const auto last = vtf.Image( 0, info.m_nFrames - 1, info.m_nFaces - 1, std::max( 1, info.m_nDepth ) - 1 );
		const std::size_t nEnd = static_cast<std::size_t>( last.data() - vtf.m_Data.data() ) + last.size();
		imageData.assign( vtf.m_Data.begin() + static_cast<std::ptrdiff_t>( nStart ), vtf.m_Data.begin() + static_cast<std::ptrdiff_t>( nEnd ) );
	}
	else
	{
		// A cube map gains or loses the sphere map face: copied face by face.
		for ( int nLevel = info.m_nMipCount - 1; nLevel >= 0; nLevel-- )
		{
			const std::size_t nLevelSize = ImageSize( info.m_nFormat, std::max( 1, info.m_nWidth >> nLevel ), std::max( 1, info.m_nHeight >> nLevel ) );
			for ( int nFrame = 0; nFrame < info.m_nFrames; nFrame++ )
			{
				for ( int nFace = 0; nFace < nStoredFaces; nFace++ )
				{
					const auto bytes = nFace < info.m_nFaces ? vtf.Image( nLevel, nFrame, nFace ) : std::span<const std::byte>{};
					if ( bytes.empty() )
					{
						imageData.resize( imageData.size() + nLevelSize );
					}
					else
					{
						imageData.insert( imageData.end(), bytes.begin(), bytes.end() );
					}
				}
			}
		}
	}

	std::vector<std::byte> lowRes;
	switch ( edit.m_nLowRes )
	{
		case LowResEdit_t::LOW_RES_EDIT_KEEP:
		{
			const auto kept = vtf.LowRes();
			lowRes.assign( kept.begin(), kept.end() );
			layout.m_nLowResFormat = info.m_nLowResFormat;
			layout.m_nLowResWidth = info.m_nLowResWidth;
			layout.m_nLowResHeight = info.m_nLowResHeight;
			break;
		}
		case LowResEdit_t::LOW_RES_EDIT_MAKE:
		{
			if ( const auto image = vtf.Decode( 0 ) )
			{
				const auto [ nLowWidth, nLowHeight ] = LowResSize( info.m_nWidth, info.m_nHeight );
				lowRes = EncodeImage( Resize( *image, nLowWidth, nLowHeight ), VtfFormat_t::DXT1 );
				layout.m_nLowResFormat = VtfFormat_t::DXT1;
				layout.m_nLowResWidth = nLowWidth;
				layout.m_nLowResHeight = nLowHeight;
			}
			break;
		}
		case LowResEdit_t::LOW_RES_EDIT_REMOVE: break;
	}
	return Assemble( layout, lowRes, imageData, edit.m_Resources );
}

} // namespace xv::texture
