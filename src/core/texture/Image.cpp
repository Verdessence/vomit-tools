//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Image.h.
//
//===========================================================================//
#include "Image.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"

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
	static constexpr std::string_view EXTENSIONS[] = { "png", "apng", "jpg", "jpeg", "tga", "bmp", "psd", "gif", "hdr", "pnm", "ppm", "pgm", "qoi", "pfm" };
	return std::ranges::find( EXTENSIONS, szExtension ) != std::end( EXTENSIONS );
}

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::uint32_t BigEndian32( const std::byte *p )
{
	return ( std::to_integer<std::uint32_t>( p[ 0 ] ) << 24 ) | ( std::to_integer<std::uint32_t>( p[ 1 ] ) << 16 ) | ( std::to_integer<std::uint32_t>( p[ 2 ] ) << 8 ) |
		std::to_integer<std::uint32_t>( p[ 3 ] );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void PutBigEndian32( std::vector<std::byte> &out, std::uint32_t n )
{
	for ( int nShift = 24; nShift >= 0; nShift -= 8 )
	{
		out.push_back( static_cast<std::byte>( ( n >> nShift ) & 0xFF ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: QOI ("Quite OK Image"): a run of small steps from the last pixel, a recent colour by
//          its hash, or a whole new colour.
//-----------------------------------------------------------------------------
std::optional<CImage> LoadQoi( std::span<const std::byte> data )
{
	if ( data.size() < 22 || std::memcmp( data.data(), "qoif", 4 ) != 0 )
	{
		return std::nullopt;
	}
	const std::uint32_t nWidth = BigEndian32( data.data() + 4 );
	const std::uint32_t nHeight = BigEndian32( data.data() + 8 );
	if ( nWidth == 0 || nHeight == 0 || nWidth > 16384 || nHeight > 16384 )
	{
		return std::nullopt;
	}
	CImage image{ static_cast<int>( nWidth ), static_cast<int>( nHeight ) };
	std::array<std::array<std::uint8_t, 4>, 64> recent{};
	std::array<std::uint8_t, 4> px{ 0, 0, 0, 255 };
	std::size_t nAt = 14;
	const std::size_t nEnd = data.size() - 8;
	const auto byte = [ &data ]( std::size_t i ) { return std::to_integer<std::uint8_t>( data[ i ] ); };
	int nRun = 0;
	for ( std::size_t i = 0; i < image.m_Pixels.size(); i += 4 )
	{
		if ( nRun > 0 )
		{
			nRun--;
		}
		else if ( nAt < nEnd )
		{
			const std::uint8_t b = byte( nAt++ );
			if ( b == 0xFE && nAt + 3 <= nEnd )
			{
				px[ 0 ] = byte( nAt );
				px[ 1 ] = byte( nAt + 1 );
				px[ 2 ] = byte( nAt + 2 );
				nAt += 3;
			}
			else if ( b == 0xFF && nAt + 4 <= nEnd )
			{
				for ( std::size_t c = 0; c < 4; c++ )
				{
					px[ c ] = byte( nAt + c );
				}
				nAt += 4;
			}
			else if ( ( b & 0xC0 ) == 0x00 )
			{
				px = recent[ b ];
			}
			else if ( ( b & 0xC0 ) == 0x40 )
			{
				px[ 0 ] = static_cast<std::uint8_t>( px[ 0 ] + ( ( b >> 4 ) & 3 ) - 2 );
				px[ 1 ] = static_cast<std::uint8_t>( px[ 1 ] + ( ( b >> 2 ) & 3 ) - 2 );
				px[ 2 ] = static_cast<std::uint8_t>( px[ 2 ] + ( b & 3 ) - 2 );
			}
			else if ( ( b & 0xC0 ) == 0x80 && nAt < nEnd )
			{
				const std::uint8_t b2 = byte( nAt++ );
				const int nGreen = ( b & 0x3F ) - 32;
				px[ 0 ] = static_cast<std::uint8_t>( px[ 0 ] + nGreen - 8 + ( ( b2 >> 4 ) & 0x0F ) );
				px[ 1 ] = static_cast<std::uint8_t>( px[ 1 ] + nGreen );
				px[ 2 ] = static_cast<std::uint8_t>( px[ 2 ] + nGreen - 8 + ( b2 & 0x0F ) );
			}
			else if ( ( b & 0xC0 ) == 0xC0 )
			{
				nRun = b & 0x3F;
			}
			recent[ ( px[ 0 ] * 3 + px[ 1 ] * 5 + px[ 2 ] * 7 + px[ 3 ] * 11 ) % 64 ] = px;
		}
		std::memcpy( image.m_Pixels.data() + i, px.data(), 4 );
	}
	return image;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::byte> SaveQoi( const CImage &image )
{
	std::vector<std::byte> out{ std::byte{ 'q' }, std::byte{ 'o' }, std::byte{ 'i' }, std::byte{ 'f' } };
	PutBigEndian32( out, static_cast<std::uint32_t>( image.m_nWidth ) );
	PutBigEndian32( out, static_cast<std::uint32_t>( image.m_nHeight ) );
	out.push_back( std::byte{ 4 } ); // RGBA
	out.push_back( std::byte{ 0 } ); // sRGB with linear alpha
	std::array<std::array<std::uint8_t, 4>, 64> recent{};
	std::array<std::uint8_t, 4> previous{ 0, 0, 0, 255 };
	int nRun = 0;
	const auto put = [ &out ]( unsigned v ) { out.push_back( static_cast<std::byte>( v ) ); };
	const std::size_t nLast = image.m_Pixels.size() - 4;
	for ( std::size_t i = 0; i < image.m_Pixels.size(); i += 4 )
	{
		std::array<std::uint8_t, 4> px{};
		std::memcpy( px.data(), image.m_Pixels.data() + i, 4 );
		if ( px == previous )
		{
			nRun++;
			if ( nRun == 62 || i == nLast )
			{
				put( 0xC0 | static_cast<unsigned>( nRun - 1 ) );
				nRun = 0;
			}
			continue;
		}
		if ( nRun > 0 )
		{
			put( 0xC0 | static_cast<unsigned>( nRun - 1 ) );
			nRun = 0;
		}
		const unsigned nHash = static_cast<unsigned>( px[ 0 ] * 3 + px[ 1 ] * 5 + px[ 2 ] * 7 + px[ 3 ] * 11 ) % 64;
		if ( recent[ nHash ] == px )
		{
			put( nHash );
		}
		else
		{
			recent[ nHash ] = px;
			if ( px[ 3 ] == previous[ 3 ] )
			{
				const int dr = static_cast<std::int8_t>( static_cast<std::uint8_t>( px[ 0 ] - previous[ 0 ] ) );
				const int dg = static_cast<std::int8_t>( static_cast<std::uint8_t>( px[ 1 ] - previous[ 1 ] ) );
				const int db = static_cast<std::int8_t>( static_cast<std::uint8_t>( px[ 2 ] - previous[ 2 ] ) );
				const int drg = dr - dg;
				const int dbg = db - dg;
				if ( dr >= -2 && dr <= 1 && dg >= -2 && dg <= 1 && db >= -2 && db <= 1 )
				{
					put( 0x40 | static_cast<unsigned>( ( dr + 2 ) << 4 | ( dg + 2 ) << 2 | ( db + 2 ) ) );
				}
				else if ( dg >= -32 && dg <= 31 && drg >= -8 && drg <= 7 && dbg >= -8 && dbg <= 7 )
				{
					put( 0x80 | static_cast<unsigned>( dg + 32 ) );
					put( static_cast<unsigned>( ( drg + 8 ) << 4 | ( dbg + 8 ) ) );
				}
				else
				{
					put( 0xFE );
					put( px[ 0 ] );
					put( px[ 1 ] );
					put( px[ 2 ] );
				}
			}
			else
			{
				put( 0xFF );
				for ( const std::uint8_t c : px )
				{
					put( c );
				}
			}
		}
		previous = px;
	}
	for ( int i = 0; i < 7; i++ )
	{
		put( 0 );
	}
	put( 1 );
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: PFM: a text header ("PF" colour or "Pf" grey, size, scale: negative for little-endian),
//          then floats, rows from the bottom.
//-----------------------------------------------------------------------------
std::optional<CFloatImage> LoadPfm( std::span<const std::byte> data )
{
	if ( data.size() < 8 || data[ 0 ] != std::byte{ 'P' } || ( data[ 1 ] != std::byte{ 'F' } && data[ 1 ] != std::byte{ 'f' } ) )
	{
		return std::nullopt;
	}
	const bool bColour = data[ 1 ] == std::byte{ 'F' };
	// Three whitespace-separated words after the magic: width, height, scale.
	std::size_t nAt = 2;
	std::array<std::string, 3> words;
	for ( std::string &szWord : words )
	{
		while ( nAt < data.size() && std::isspace( std::to_integer<int>( data[ nAt ] ) ) )
		{
			nAt++;
		}
		while ( nAt < data.size() && !std::isspace( std::to_integer<int>( data[ nAt ] ) ) && szWord.size() < 32 )
		{
			szWord += static_cast<char>( data[ nAt++ ] );
		}
	}
	nAt++; // the one whitespace character before the data
	const int nWidth = std::atoi( words[ 0 ].c_str() );
	const int nHeight = std::atoi( words[ 1 ].c_str() );
	const float flScale = static_cast<float>( std::atof( words[ 2 ].c_str() ) );
	const std::size_t nChannels = bColour ? 3 : 1;
	if ( nWidth <= 0 || nHeight <= 0 || nWidth > 16384 || nHeight > 16384 || flScale == 0.0f ||
		data.size() < nAt || ( data.size() - nAt ) / ( 4 * nChannels ) < static_cast<std::size_t>( nWidth ) * static_cast<std::size_t>( nHeight ) )
	{
		return std::nullopt;
	}
	CFloatImage image;
	image.m_nWidth = nWidth;
	image.m_nHeight = nHeight;
	image.m_Pixels.resize( static_cast<std::size_t>( nWidth ) * static_cast<std::size_t>( nHeight ) * 4 );
	const bool bBig = flScale > 0.0f;
	for ( int y = 0; y < nHeight; y++ )
	{
		for ( int x = 0; x < nWidth; x++ )
		{
			float *p = image.m_Pixels.data() + ( static_cast<std::size_t>( nHeight - 1 - y ) * static_cast<std::size_t>( nWidth ) + static_cast<std::size_t>( x ) ) * 4;
			for ( std::size_t c = 0; c < 3; c++ )
			{
				const std::byte *q = data.data() + nAt + ( ( static_cast<std::size_t>( y ) * static_cast<std::size_t>( nWidth ) + static_cast<std::size_t>( x ) ) * nChannels + std::min( c, nChannels - 1 ) ) * 4;
				std::array<std::byte, 4> bytes{ q[ 0 ], q[ 1 ], q[ 2 ], q[ 3 ] };
				if ( bBig )
				{
					std::ranges::reverse( bytes );
				}
				float flValue = 0.0f;
				std::memcpy( &flValue, bytes.data(), 4 );
				p[ c ] = std::isfinite( flValue ) ? flValue : 0.0f;
			}
			p[ 3 ] = 1.0f;
		}
	}
	return image;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::byte> SavePfm( const CFloatImage &image )
{
	const std::string szHeader = "PF\n" + std::to_string( image.m_nWidth ) + ' ' + std::to_string( image.m_nHeight ) + "\n-1.0\n";
	std::vector<std::byte> out( reinterpret_cast<const std::byte *>( szHeader.data() ), reinterpret_cast<const std::byte *>( szHeader.data() ) + szHeader.size() );
	for ( int y = image.m_nHeight - 1; y >= 0; y-- )
	{
		for ( int x = 0; x < image.m_nWidth; x++ )
		{
			const auto *p = reinterpret_cast<const std::byte *>( image.Pixel( x, y ) );
			out.insert( out.end(), p, p + 12 ); // little-endian floats, as the -1 says
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::uint32_t Crc32( std::span<const std::byte> data )
{
	static const std::array<std::uint32_t, 256> table = [] {
		std::array<std::uint32_t, 256> values{};
		for ( std::uint32_t n = 0; n < 256; n++ )
		{
			std::uint32_t c = n;
			for ( int k = 0; k < 8; k++ )
			{
				c = c & 1 ? 0xEDB88320u ^ ( c >> 1 ) : c >> 1;
			}
			values[ n ] = c;
		}
		return values;
	}();
	std::uint32_t c = 0xFFFFFFFFu;
	for ( const std::byte b : data )
	{
		c = table[ ( c ^ std::to_integer<std::uint32_t>( b ) ) & 0xFF ] ^ ( c >> 8 );
	}
	return c ^ 0xFFFFFFFFu;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void PutChunk( std::vector<std::byte> &out, std::string_view szType, std::span<const std::byte> data )
{
	PutBigEndian32( out, static_cast<std::uint32_t>( data.size() ) );
	const std::size_t nTypeAt = out.size();
	for ( const char c : szType )
	{
		out.push_back( static_cast<std::byte>( c ) );
	}
	out.insert( out.end(), data.begin(), data.end() );
	PutBigEndian32( out, Crc32( std::span{ out }.subspan( nTypeAt ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: The frames of an animated PNG: each frame's data repacked as a PNG of its own, decoded,
//          and laid over the canvas the way its blend and dispose settings say. Empty when it
//          isn't one.
//-----------------------------------------------------------------------------
std::vector<CImage> LoadApng( std::span<const std::byte> data )
{
	static constexpr unsigned char SIGNATURE[] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	if ( data.size() < 8 || std::memcmp( data.data(), SIGNATURE, 8 ) != 0 )
	{
		return {};
	}
	struct Frame_t
	{
		std::uint32_t m_nWidth = 0, m_nHeight = 0, m_nX = 0, m_nY = 0;
		std::uint8_t m_nDispose = 0, m_nBlend = 0;
		std::vector<std::byte> m_Data;
	};
	std::vector<std::byte> header;   // IHDR's data
	std::vector<std::byte> shared;   // PLTE, tRNS and the like, as whole chunks
	std::vector<Frame_t> frames;
	bool bAnimated = false;
	bool bFirstIsImage = false; // the default image (IDAT) is the first frame
	for ( std::size_t nAt = 8; nAt + 12 <= data.size(); )
	{
		const std::uint32_t nLength = BigEndian32( data.data() + nAt );
		if ( nLength > data.size() - nAt - 12 )
		{
			return {};
		}
		const std::string szType( reinterpret_cast<const char *>( data.data() + nAt + 4 ), 4 );
		const std::span<const std::byte> chunk = data.subspan( nAt + 8, nLength );
		if ( szType == "IHDR" && nLength == 13 )
		{
			header.assign( chunk.begin(), chunk.end() );
		}
		else if ( szType == "acTL" )
		{
			bAnimated = true;
		}
		else if ( szType == "fcTL" && nLength >= 26 )
		{
			Frame_t frame;
			frame.m_nWidth = BigEndian32( chunk.data() + 4 );
			frame.m_nHeight = BigEndian32( chunk.data() + 8 );
			frame.m_nX = BigEndian32( chunk.data() + 12 );
			frame.m_nY = BigEndian32( chunk.data() + 16 );
			frame.m_nDispose = std::to_integer<std::uint8_t>( chunk[ 24 ] );
			frame.m_nBlend = std::to_integer<std::uint8_t>( chunk[ 25 ] );
			if ( frames.empty() )
			{
				bFirstIsImage = true; // set before any IDAT: the default image is frame 0
			}
			frames.push_back( std::move( frame ) );
		}
		else if ( szType == "IDAT" )
		{
			if ( bFirstIsImage && frames.size() == 1 )
			{
				frames.back().m_Data.insert( frames.back().m_Data.end(), chunk.begin(), chunk.end() );
			}
			bFirstIsImage = bFirstIsImage && frames.size() == 1;
		}
		else if ( szType == "fdAT" && nLength >= 4 && !frames.empty() )
		{
			frames.back().m_Data.insert( frames.back().m_Data.end(), chunk.begin() + 4, chunk.end() );
		}
		else if ( szType == "PLTE" || szType == "tRNS" || szType == "gAMA" || szType == "sRGB" || szType == "cHRM" || szType == "iCCP" || szType == "sBIT" )
		{
			shared.insert( shared.end(), data.begin() + static_cast<std::ptrdiff_t>( nAt ), data.begin() + static_cast<std::ptrdiff_t>( nAt + 12 + nLength ) );
		}
		else if ( szType == "IEND" )
		{
			break;
		}
		nAt += 12 + nLength;
	}
	if ( !bAnimated || header.size() != 13 || frames.empty() )
	{
		return {};
	}
	const std::uint32_t nCanvasWidth = BigEndian32( header.data() );
	const std::uint32_t nCanvasHeight = BigEndian32( header.data() + 4 );
	if ( nCanvasWidth == 0 || nCanvasHeight == 0 || nCanvasWidth > 16384 || nCanvasHeight > 16384 )
	{
		return {};
	}
	CImage canvas{ static_cast<int>( nCanvasWidth ), static_cast<int>( nCanvasHeight ) };
	std::vector<CImage> out;
	for ( const Frame_t &frame : frames )
	{
		if ( frame.m_Data.empty() || frame.m_nWidth == 0 || frame.m_nHeight == 0 || frame.m_nX + frame.m_nWidth > nCanvasWidth || frame.m_nY + frame.m_nHeight > nCanvasHeight )
		{
			continue;
		}
		std::vector<std::byte> png( reinterpret_cast<const std::byte *>( SIGNATURE ), reinterpret_cast<const std::byte *>( SIGNATURE ) + 8 );
		std::vector<std::byte> frameHeader = header;
		std::vector<std::byte> size;
		PutBigEndian32( size, frame.m_nWidth );
		PutBigEndian32( size, frame.m_nHeight );
		std::copy( size.begin(), size.end(), frameHeader.begin() );
		PutChunk( png, "IHDR", frameHeader );
		png.insert( png.end(), shared.begin(), shared.end() );
		PutChunk( png, "IDAT", frame.m_Data );
		PutChunk( png, "IEND", {} );
		const auto image = LoadImage( png );
		if ( !image )
		{
			continue;
		}
		const CImage before = canvas;
		for ( int y = 0; y < image->m_nHeight; y++ )
		{
			for ( int x = 0; x < image->m_nWidth; x++ )
			{
				const std::uint8_t *s = image->Pixel( x, y );
				std::uint8_t *d = canvas.Pixel( static_cast<int>( frame.m_nX ) + x, static_cast<int>( frame.m_nY ) + y );
				if ( frame.m_nBlend == 0 || s[ 3 ] == 255 )
				{
					std::memcpy( d, s, 4 );
					continue;
				}
				// Over what is there, straight alpha.
				const float flSource = static_cast<float>( s[ 3 ] ) / 255.0f;
				const float flBelow = static_cast<float>( d[ 3 ] ) / 255.0f * ( 1.0f - flSource );
				const float flOut = flSource + flBelow;
				for ( int c = 0; c < 3; c++ )
				{
					d[ c ] = flOut > 0.0f ? static_cast<std::uint8_t>( std::lround( ( s[ c ] * flSource + d[ c ] * flBelow ) / flOut ) ) : 0;
				}
				d[ 3 ] = static_cast<std::uint8_t>( std::lround( flOut * 255.0f ) );
			}
		}
		out.push_back( canvas );
		if ( frame.m_nDispose == 1 || ( frame.m_nDispose == 2 && out.size() == 1 ) )
		{
			for ( std::uint32_t y = 0; y < frame.m_nHeight; y++ )
			{
				std::memset( canvas.Pixel( static_cast<int>( frame.m_nX ), static_cast<int>( frame.m_nY + y ) ), 0, static_cast<std::size_t>( frame.m_nWidth ) * 4 );
			}
		}
		else if ( frame.m_nDispose == 2 )
		{
			canvas = before;
		}
	}
	return out;
}

} // namespace

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
	if ( auto qoi = LoadQoi( data ) )
	{
		return qoi;
	}
	if ( auto pfm = LoadPfm( data ) )
	{
		CImage image{ pfm->m_nWidth, pfm->m_nHeight };
		for ( std::size_t i = 0; i < image.m_Pixels.size(); i++ )
		{
			image.m_Pixels[ i ] = i % 4 == 3 ? 255 : FromLinear( pfm->m_Pixels[ i ] );
		}
		return image;
	}
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
// Purpose: Every frame of an animated GIF (other files: their one picture).
//-----------------------------------------------------------------------------
std::vector<CImage> LoadImageFrames( std::span<const std::byte> data, std::string *pError )
{
	const auto *pData = reinterpret_cast<const stbi_uc *>( data.data() );
	const bool bGif = data.size() >= 6 && data.size() <= static_cast<std::size_t>( INT_MAX ) && std::memcmp( pData, "GIF8", 4 ) == 0;
	if ( !bGif )
	{
		if ( auto frames = LoadApng( data ); frames.size() > 1 )
		{
			return frames;
		}
		auto image = LoadImage( data, pError );
		return image ? std::vector<CImage>{ std::move( *image ) } : std::vector<CImage>{};
	}
	int *pDelays = nullptr;
	int nWidth = 0;
	int nHeight = 0;
	int nFrames = 0;
	int nChannels = 0;
	stbi_uc *pDecoded = stbi_load_gif_from_memory( pData, static_cast<int>( data.size() ), &pDelays, &nWidth, &nHeight, &nFrames, &nChannels, 4 );
	if ( !pDecoded )
	{
		if ( pError )
		{
			const char *pszReason = stbi_failure_reason();
			*pError = pszReason ? pszReason : "not an image";
		}
		return {};
	}
	std::vector<CImage> frames;
	const std::size_t nFrameBytes = static_cast<std::size_t>( nWidth ) * static_cast<std::size_t>( nHeight ) * 4;
	for ( int i = 0; i < nFrames; i++ )
	{
		CImage frame{ nWidth, nHeight };
		std::memcpy( frame.m_Pixels.data(), pDecoded + static_cast<std::size_t>( i ) * nFrameBytes, nFrameBytes );
		frames.push_back( std::move( frame ) );
	}
	stbi_image_free( pDecoded );
	stbi_image_free( pDelays );
	return frames;
}

//-----------------------------------------------------------------------------
// Purpose: The type for a file name's extension; PNG for anything else.
//-----------------------------------------------------------------------------
ImageFileType_t ImageFileTypeFor( std::string_view szExtension )
{
	std::string szLower{ szExtension };
	for ( char &c : szLower )
	{
		c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
	}
	if ( szLower == "tga" )
		return ImageFileType_t::IMAGE_FILE_TYPE_TGA;
	if ( szLower == "bmp" )
		return ImageFileType_t::IMAGE_FILE_TYPE_BMP;
	if ( szLower == "jpg" || szLower == "jpeg" )
		return ImageFileType_t::IMAGE_FILE_TYPE_JPG;
	if ( szLower == "hdr" )
		return ImageFileType_t::IMAGE_FILE_TYPE_HDR;
	if ( szLower == "qoi" )
		return ImageFileType_t::IMAGE_FILE_TYPE_QOI;
	if ( szLower == "pfm" )
		return ImageFileType_t::IMAGE_FILE_TYPE_PFM;
	return ImageFileType_t::IMAGE_FILE_TYPE_PNG;
}

namespace
{

//-----------------------------------------------------------------------------
// Purpose: stb's writers hand over the file in pieces.
//-----------------------------------------------------------------------------
void AppendBytes( void *pContext, void *pData, int nSize )
{
	auto *pOut = static_cast<std::vector<std::byte> *>( pContext );
	const auto *p = static_cast<const std::byte *>( pData );
	pOut->insert( pOut->end(), p, p + nSize );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: A picture as the bytes of an image file.
//-----------------------------------------------------------------------------
std::vector<std::byte> SaveImage( const CImage &image, ImageFileType_t nType )
{
	if ( image.Empty() )
	{
		return {};
	}
	if ( nType == ImageFileType_t::IMAGE_FILE_TYPE_QOI )
	{
		return SaveQoi( image );
	}
	if ( nType == ImageFileType_t::IMAGE_FILE_TYPE_HDR || nType == ImageFileType_t::IMAGE_FILE_TYPE_PFM )
	{
		CFloatImage light;
		light.m_nWidth = image.m_nWidth;
		light.m_nHeight = image.m_nHeight;
		light.m_Pixels.resize( image.m_Pixels.size() );
		for ( std::size_t i = 0; i < image.m_Pixels.size(); i++ )
		{
			light.m_Pixels[ i ] = i % 4 == 3 ? static_cast<float>( image.m_Pixels[ i ] ) / 255.0f : ToLinear( image.m_Pixels[ i ] );
		}
		return SaveFloatImage( light, nType );
	}
	std::vector<std::byte> out;
	const int nStride = image.m_nWidth * 4;
	int nOk = 0;
	switch ( nType )
	{
		case ImageFileType_t::IMAGE_FILE_TYPE_TGA: nOk = stbi_write_tga_to_func( AppendBytes, &out, image.m_nWidth, image.m_nHeight, 4, image.m_Pixels.data() ); break;
		case ImageFileType_t::IMAGE_FILE_TYPE_BMP: nOk = stbi_write_bmp_to_func( AppendBytes, &out, image.m_nWidth, image.m_nHeight, 4, image.m_Pixels.data() ); break;
		case ImageFileType_t::IMAGE_FILE_TYPE_JPG: nOk = stbi_write_jpg_to_func( AppendBytes, &out, image.m_nWidth, image.m_nHeight, 4, image.m_Pixels.data(), 95 ); break;
		default: nOk = stbi_write_png_to_func( AppendBytes, &out, image.m_nWidth, image.m_nHeight, 4, image.m_Pixels.data(), nStride ); break;
	}
	return nOk ? out : std::vector<std::byte>{};
}

//-----------------------------------------------------------------------------
// Purpose: Light values as a Radiance HDR (or PFM) file, keeping values above 1.
//-----------------------------------------------------------------------------
std::vector<std::byte> SaveFloatImage( const CFloatImage &image, ImageFileType_t nType )
{
	if ( image.Empty() )
	{
		return {};
	}
	if ( nType == ImageFileType_t::IMAGE_FILE_TYPE_PFM )
	{
		return SavePfm( image );
	}
	std::vector<std::byte> out;
	return stbi_write_hdr_to_func( AppendBytes, &out, image.m_nWidth, image.m_nHeight, 4, image.m_Pixels.data() ) ? out : std::vector<std::byte>{};
}

//-----------------------------------------------------------------------------
// Purpose: A Radiance HDR file as its light values; nullopt for other files.
//-----------------------------------------------------------------------------
std::optional<CFloatImage> LoadFloatImage( std::span<const std::byte> data )
{
	if ( data.empty() || data.size() > static_cast<std::size_t>( INT_MAX ) )
	{
		return std::nullopt;
	}
	if ( auto pfm = LoadPfm( data ) )
	{
		return pfm;
	}
	const auto *pData = reinterpret_cast<const stbi_uc *>( data.data() );
	const int nSize = static_cast<int>( data.size() );
	if ( !stbi_is_hdr_from_memory( pData, nSize ) )
	{
		return std::nullopt;
	}
	int nWidth = 0;
	int nHeight = 0;
	int nChannels = 0;
	float *pDecoded = stbi_loadf_from_memory( pData, nSize, &nWidth, &nHeight, &nChannels, 4 );
	if ( !pDecoded )
	{
		return std::nullopt;
	}
	CFloatImage image;
	image.m_nWidth = nWidth;
	image.m_nHeight = nHeight;
	image.m_Pixels.assign( pDecoded, pDecoded + static_cast<std::size_t>( nWidth ) * static_cast<std::size_t>( nHeight ) * 4 );
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
// Purpose: Light values resampled the same way.
//-----------------------------------------------------------------------------
CFloatImage Resize( const CFloatImage &image, int nWidth, int nHeight )
{
	if ( image.m_nWidth == nWidth && image.m_nHeight == nHeight )
	{
		return image;
	}
	CFloatImage out;
	out.m_nWidth = std::max( nWidth, 0 );
	out.m_nHeight = std::max( nHeight, 0 );
	out.m_Pixels.resize( static_cast<std::size_t>( out.m_nWidth ) * static_cast<std::size_t>( out.m_nHeight ) * 4 );
	if ( !image.Empty() && !out.Empty() )
	{
		stbir_resize_float_linear( image.m_Pixels.data(), image.m_nWidth, image.m_nHeight, 0, out.m_Pixels.data(), nWidth, nHeight, 0, STBIR_RGBA );
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
namespace
{

//-----------------------------------------------------------------------------
// Purpose: Turned clockwise, then mirrored left to right, for either kind of picture.
//-----------------------------------------------------------------------------
template <typename Image_t>
Image_t OrientedAny( const Image_t &image, int nQuarterTurns, bool bMirror )
{
	nQuarterTurns = ( ( nQuarterTurns % 4 ) + 4 ) % 4;
	if ( nQuarterTurns == 0 && !bMirror )
	{
		return image;
	}
	const int w = image.m_nWidth;
	const int h = image.m_nHeight;
	const bool bSwapped = nQuarterTurns % 2 == 1;
	Image_t out = image;
	out.m_nWidth = bSwapped ? h : w;
	out.m_nHeight = bSwapped ? w : h;
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
			const auto nTo = ( static_cast<std::size_t>( y ) * static_cast<std::size_t>( out.m_nWidth ) + static_cast<std::size_t>( x ) ) * 4;
			const auto nFrom = ( static_cast<std::size_t>( nSy ) * static_cast<std::size_t>( w ) + static_cast<std::size_t>( nSx ) ) * 4;
			std::copy_n( image.m_Pixels.begin() + static_cast<std::ptrdiff_t>( nFrom ), 4, out.m_Pixels.begin() + static_cast<std::ptrdiff_t>( nTo ) );
		}
	}
	return out;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Turned clockwise by quarterTurns (0-3), then mirrored left to right if mirror is set.
//-----------------------------------------------------------------------------
CImage Oriented( const CImage &image, int nQuarterTurns, bool bMirror )
{
	return OrientedAny( image, nQuarterTurns, bMirror );
}

//-----------------------------------------------------------------------------
// Purpose: The same for light values.
//-----------------------------------------------------------------------------
CFloatImage Oriented( const CFloatImage &image, int nQuarterTurns, bool bMirror )
{
	return OrientedAny( image, nQuarterTurns, bMirror );
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
