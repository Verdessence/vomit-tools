//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Image.h.
//
//===========================================================================//
#include "Image.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>

#include "stb_image.h"
#include "stb_image_resize2.h"

namespace xv::texture
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const std::array<float, 256> &LinearTable()
{
	static const std::array<float, 256> table = [] {
		std::array<float, 256> values{};
		for ( std::size_t i = 0; i < values.size(); i++ )
		{
			const float c = static_cast<float>( i ) / 255.0f;
			values[ i ] = c <= 0.04045f ? c / 12.92f : std::pow( ( c + 0.055f ) / 1.055f, 2.4f );
		}
		return values;
	}();
	return table;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CImage::CImage( int nWidth, int nHeight )
	: m_nWidth( std::max( nWidth, 0 ) ),
	  m_nHeight( std::max( nHeight, 0 ) ),
	  m_Pixels( static_cast<std::size_t>( m_nWidth ) * static_cast<std::size_t>( m_nHeight ) * 4 )
{}

//-----------------------------------------------------------------------------
// Purpose: The extensions loadImage reads ("png", "tga", ...), lowercase and without the dot.
//-----------------------------------------------------------------------------
bool IsImageExtension( std::string_view szExtension )
{
	static constexpr std::string_view EXTENSIONS[] = { "png", "jpg", "jpeg", "tga", "bmp", "psd", "gif", "hdr", "pnm", "ppm", "pgm" };
	return std::ranges::find( EXTENSIONS, szExtension ) != std::end( EXTENSIONS );
}

//-----------------------------------------------------------------------------
// Purpose: PNG, JPEG, TGA, BMP, PSD (composited), GIF (first frame), PNM, and Radiance HDR (gamma-
//          mapped to
//          8 bits), recognised by content. Input is untrusted.
//-----------------------------------------------------------------------------
std::optional<CImage> LoadImage( std::span<const std::byte> data, std::string *pError )
{
	if ( data.empty() || data.size() > static_cast<std::size_t>( INT_MAX ) )
	{
		if ( pError )
		{
			*pError = data.empty() ? "empty file" : "file too large";
		}
		return std::nullopt;
	}
	int nWidth = 0;
	int nHeight = 0;
	int nChannels = 0;
	stbi_uc *pDecoded = stbi_load_from_memory( reinterpret_cast<const stbi_uc *>( data.data() ), static_cast<int>( data.size() ), &nWidth, &nHeight, &nChannels, 4 );
	if ( !pDecoded )
	{
		if ( pError )
		{
			const char *pszReason = stbi_failure_reason();
			*pError = pszReason ? pszReason : "not an image";
		}
		return std::nullopt;
	}
	CImage image{ nWidth, nHeight };
	std::memcpy( image.m_Pixels.data(), pDecoded, image.m_Pixels.size() );
	stbi_image_free( pDecoded );
	return image;
}

//-----------------------------------------------------------------------------
// Purpose: Resampled in linear light (the pixels are taken as sRGB), with a Mitchell filter when
//          shrinking
//          and Catmull-Rom when enlarging.
//-----------------------------------------------------------------------------
CImage Resize( const CImage &image, int nWidth, int nHeight )
{
	if ( image.m_nWidth == nWidth && image.m_nHeight == nHeight )
	{
		return image;
	}
	CImage out{ nWidth, nHeight };
	if ( !image.Empty() && !out.Empty() )
	{
		stbir_resize_uint8_srgb( image.m_Pixels.data(), image.m_nWidth, image.m_nHeight, 0, out.m_Pixels.data(), nWidth, nHeight, 0, STBIR_RGBA );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: The next mipmap level: half the size (at least 1x1), each pixel the average of up to
//          four, in
//          linear light.
//-----------------------------------------------------------------------------
CImage HalfSize( const CImage &image )
{
	CImage out{ std::max( 1, image.m_nWidth / 2 ), std::max( 1, image.m_nHeight / 2 ) };
	const auto &linear = LinearTable();
	for ( int y = 0; y < out.m_nHeight; y++ )
	{
		for ( int x = 0; x < out.m_nWidth; x++ )
		{
			std::array<float, 4> sum{};
			int nCount = 0;
			for ( int nDy = 0; nDy < 2; nDy++ )
			{
				for ( int nDx = 0; nDx < 2; nDx++ )
				{
					const int nSx = std::min( x * 2 + nDx, image.m_nWidth - 1 );
					const int nSy = std::min( y * 2 + nDy, image.m_nHeight - 1 );
					const std::uint8_t *p = image.Pixel( nSx, nSy );
					for ( int c = 0; c < 3; c++ )
					{
						sum[ c ] += linear[ p[ c ] ];
					}
					sum[ 3 ] += p[ 3 ];
					nCount++;
				}
			}
			std::uint8_t *q = out.Pixel( x, y );
			for ( int c = 0; c < 3; c++ )
			{
				q[ c ] = FromLinear( sum[ c ] / static_cast<float>( nCount ) );
			}
			q[ 3 ] = static_cast<std::uint8_t>( std::lround( sum[ 3 ] / static_cast<float>( nCount ) ) );
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Turned clockwise by quarterTurns (0-3), then mirrored left to right if mirror is set.
//-----------------------------------------------------------------------------
CImage Oriented( const CImage &image, int nQuarterTurns, bool bMirror )
{
	nQuarterTurns = ( ( nQuarterTurns % 4 ) + 4 ) % 4;
	if ( nQuarterTurns == 0 && !bMirror )
	{
		return image;
	}
	const int w = image.m_nWidth;
	const int h = image.m_nHeight;
	const bool bSwapped = nQuarterTurns % 2 == 1;
	CImage out{ bSwapped ? h : w, bSwapped ? w : h };
	for ( int y = 0; y < out.m_nHeight; y++ )
	{
		for ( int x = 0; x < out.m_nWidth; x++ )
		{
			const int nMx = bMirror ? out.m_nWidth - 1 - x : x; // the pixel of the turned (unmirrored) image
			int nSx = nMx;
			int nSy = y;
			switch ( nQuarterTurns )
			{
				case 1: // clockwise: the left column becomes the top row
					nSx = y;
					nSy = h - 1 - nMx;
					break;
				case 2:
					nSx = w - 1 - nMx;
					nSy = h - 1 - y;
					break;
				case 3:
					nSx = w - 1 - y;
					nSy = nMx;
					break;
				default:
					break;
			}
			std::memcpy( out.Pixel( x, y ), image.Pixel( nSx, nSy ), 4 );
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: The one orientation that equals applying first, then second.
//-----------------------------------------------------------------------------
COrientation Combined( COrientation first, COrientation second )
{
	// A turn after a mirror is the mirror after the opposite turn.
	const int nTurns = first.m_nQuarterTurns + ( first.m_bMirror ? -second.m_nQuarterTurns : second.m_nQuarterTurns );
	return { ( ( nTurns % 4 ) + 4 ) % 4, first.m_bMirror != second.m_bMirror };
}

//-----------------------------------------------------------------------------
// Purpose: The power of two nearest to n, at least 1.
//-----------------------------------------------------------------------------
int NearestPowerOfTwo( int n )
{
	int nPower = 1;
	while ( nPower < n && nPower < ( 1 << 29 ) )
	{
		nPower <<= 1;
	}
	// power >= n: step back when the smaller power is nearer.
	return nPower > 1 && n - nPower / 2 < nPower - n ? nPower / 2 : nPower;
}

//-----------------------------------------------------------------------------
// Purpose: sRGB 8-bit value to linear light 0-1 and back.
//-----------------------------------------------------------------------------
float ToLinear( std::uint8_t nValue )
{
	return LinearTable()[ nValue ];
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::uint8_t FromLinear( float flValue )
{
	flValue = std::clamp( flValue, 0.0f, 1.0f );
	const float c = flValue <= 0.0031308f ? flValue * 12.92f : 1.055f * std::pow( flValue, 1.0f / 2.4f ) - 0.055f;
	return static_cast<std::uint8_t>( std::lround( c * 255.0f ) );
}

} // namespace xv::texture
