//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Vtf.h.
//
//===========================================================================//
#include "Vtf.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <initializer_list>
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
			case VtfFormat_t::RGBA16161616: set( b( 1 ), b( 3 ), b( 5 ), b( 7 ) ); break; // the high byte of each channel
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
	switch ( nFormat )
	{
		case VtfFormat_t::DXT1:
		case VtfFormat_t::DXT5:
		case VtfFormat_t::BGR888:
		case VtfFormat_t::RGB888:
		case VtfFormat_t::BGRA8888:
		case VtfFormat_t::RGBA8888:
		case VtfFormat_t::BGRX8888:
			return true;
		default:
			return false;
	}
}

//-----------------------------------------------------------------------------
// Purpose: RGBA8 to raw image data. Writes DXT1, DXT5, BGR888, RGB888, BGRA8888, RGBA8888 and
//          BGRX8888.
//-----------------------------------------------------------------------------
std::vector<std::byte> EncodeImage( const CImage &image, VtfFormat_t nFormat )
{
	std::vector<std::byte> out( ImageSize( nFormat, image.m_nWidth, image.m_nHeight ) );
	if ( nFormat == VtfFormat_t::DXT1 || nFormat == VtfFormat_t::DXT5 )
	{
		const int nBlockBytes = nFormat == VtfFormat_t::DXT1 ? 8 : 16;
		const int nBlocksWide = std::max( 1, ( image.m_nWidth + 3 ) / 4 );
		const int nBlocksHigh = std::max( 1, ( image.m_nHeight + 3 ) / 4 );
		for ( int nBy = 0; nBy < nBlocksHigh; nBy++ )
		{
			for ( int nBx = 0; nBx < nBlocksWide; nBx++ )
			{
				const std::size_t nBlock = static_cast<std::size_t>( nBy ) * static_cast<std::size_t>( nBlocksWide ) + static_cast<std::size_t>( nBx );
				EncodeDxtBlock( image, nBx, nBy, nFormat == VtfFormat_t::DXT5, out.data() + nBlock * static_cast<std::size_t>( nBlockBytes ) );
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
		const auto put = [ d ]( std::initializer_list<std::uint8_t> values ) {
			std::size_t k = 0;
			for ( std::uint8_t v : values )
			{
				d[ k++ ] = static_cast<std::byte>( v );
			}
		};
		switch ( nFormat )
		{
			case VtfFormat_t::BGR888: put( { p[ 2 ], p[ 1 ], p[ 0 ] } ); break;
			case VtfFormat_t::RGB888: put( { p[ 0 ], p[ 1 ], p[ 2 ] } ); break;
			case VtfFormat_t::BGRA8888: put( { p[ 2 ], p[ 1 ], p[ 0 ], p[ 3 ] } ); break;
			case VtfFormat_t::RGBA8888: put( { p[ 0 ], p[ 1 ], p[ 2 ], p[ 3 ] } ); break;
			case VtfFormat_t::BGRX8888: put( { p[ 2 ], p[ 1 ], p[ 0 ], 255 } ); break;
			default: return {};
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: The image's width and height don't have to be powers of two, but the game expects them
//          to be.
//-----------------------------------------------------------------------------
std::vector<std::byte> WriteVtf( const CImage &image, const VtfWriteOptions_t &options )
{
	if ( image.Empty() || !CanEncode( options.m_nFormat ) || image.m_nWidth > 65535 || image.m_nHeight > 65535 )
	{
		return {};
	}
	const int nHeaderSize = options.m_nVersionMinor >= 2 ? HEADER_SIZE_72 : HEADER_SIZE_71;
	const int nMipCount = options.m_bMipmaps ? MipCountFor( image.m_nWidth, image.m_nHeight ) : 1;

	std::vector<CImage> mips{ image };
	for ( int nLevel = 1; nLevel < nMipCount; nLevel++ )
	{
		mips.push_back( HalfSize( mips.back() ) );
	}

	int nLowWidth = image.m_nWidth;
	int nLowHeight = image.m_nHeight;
	while ( nLowWidth > 16 || nLowHeight > 16 )
	{
		nLowWidth = std::max( 1, nLowWidth / 2 );
		nLowHeight = std::max( 1, nLowHeight / 2 );
	}
	const std::vector<std::byte> lowRes = options.m_bLowRes ? EncodeImage( Resize( image, nLowWidth, nLowHeight ), VtfFormat_t::DXT1 ) : std::vector<std::byte>{};

	// The average colour in linear light, which the engine uses for bounced light.
	std::array<double, 3> reflectivity{};
	for ( std::size_t i = 0; i < image.m_Pixels.size(); i += 4 )
	{
		for ( std::size_t c = 0; c < 3; c++ )
		{
			reflectivity[ c ] += ToLinear( image.m_Pixels[ i + c ] );
		}
	}

	std::vector<std::byte> out( static_cast<std::size_t>( nHeaderSize ) );
	std::memcpy( out.data(), "VTF\0", 4 );
	WriteAt<std::uint32_t>( out, 4, 7 );
	WriteAt<std::uint32_t>( out, 8, static_cast<std::uint32_t>( options.m_nVersionMinor >= 2 ? 2 : 1 ) );
	WriteAt<std::uint32_t>( out, 12, static_cast<std::uint32_t>( nHeaderSize ) );
	WriteAt<std::uint16_t>( out, 16, static_cast<std::uint16_t>( image.m_nWidth ) );
	WriteAt<std::uint16_t>( out, 18, static_cast<std::uint16_t>( image.m_nHeight ) );
	WriteAt<std::uint32_t>( out, 20, options.m_nFlags );
	WriteAt<std::uint16_t>( out, 24, 1 ); // frames
	WriteAt<std::uint16_t>( out, 26, 0 ); // first frame
	const double flPixels = static_cast<double>( image.m_Pixels.size() ) / 4.0;
	for ( std::size_t c = 0; c < 3; c++ )
	{
		WriteAt<float>( out, 32 + c * 4, static_cast<float>( reflectivity[ c ] / flPixels ) );
	}
	WriteAt<float>( out, 48, 1.0f ); // bumpmap scale
	WriteAt<std::int32_t>( out, 52, static_cast<std::int32_t>( options.m_nFormat ) );
	out[ 56 ] = static_cast<std::byte>( nMipCount );
	WriteAt<std::uint32_t>( out, 57, options.m_bLowRes ? static_cast<std::uint32_t>( VtfFormat_t::DXT1 ) : NO_FORMAT );
	out[ 61 ] = static_cast<std::byte>( options.m_bLowRes ? nLowWidth : 0 );
	out[ 62 ] = static_cast<std::byte>( options.m_bLowRes ? nLowHeight : 0 );
	if ( options.m_nVersionMinor >= 2 )
	{
		WriteAt<std::uint16_t>( out, 63, 1 ); // depth
	}

	out.insert( out.end(), lowRes.begin(), lowRes.end() );
	for ( int nLevel = nMipCount - 1; nLevel >= 0; nLevel-- )
	{
		const auto encoded = EncodeImage( mips[ static_cast<std::size_t>( nLevel ) ], options.m_nFormat );
		out.insert( out.end(), encoded.begin(), encoded.end() );
	}
	return out;
}

} // namespace xv::texture
