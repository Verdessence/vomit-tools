//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
//===========================================================================//
// tinyexr's implementation, compiled once in its own target (vomit_tools_tinyexr) so our warning
// flags don't apply to it, and the wrapper Exr.h declares. tinyexr inflates through stb's zlib
// (vomit_tools_stb) rather than its bundled miniz.

#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1
#define TINYEXR_USE_THREAD 0
#define TINYEXR_USE_OPENMP 0
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"

#include "texture/Exr.h"

#include <climits>
#include <cstdlib>
#include <cstring>

namespace xv::texture
{

namespace
{

// As stb's STBI_MAX_DIMENSIONS in StbImplementation.c.
constexpr int MAX_DIMENSION = 16384;

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Whether the bytes start with OpenEXR's magic number (76 2f 31 01).
//-----------------------------------------------------------------------------
bool IsExr( std::span<const std::byte> data )
{
	static constexpr unsigned char MAGIC[] = { 0x76, 0x2f, 0x31, 0x01 };
	return data.size() >= 4 && std::memcmp( data.data(), MAGIC, 4 ) == 0;
}

//-----------------------------------------------------------------------------
// Purpose: The file's first part as RGBA light values. The header is read first so an image too
//          large to hold is refused before tinyexr allocates for it.
//-----------------------------------------------------------------------------
std::optional<CFloatImage> LoadExr( std::span<const std::byte> data, std::string *pError )
{
	const auto fail = [ pError ]( const char *pszReason ) -> std::optional<CFloatImage> {
		if ( pError )
		{
			*pError = pszReason ? pszReason : "not an OpenEXR file";
		}
		return std::nullopt;
	};
	if ( !IsExr( data ) || data.size() > static_cast<std::size_t>( INT_MAX ) )
	{
		return fail( "not an OpenEXR file" );
	}
	const auto *pMemory = reinterpret_cast<const unsigned char *>( data.data() );

	EXRVersion version;
	if ( ParseEXRVersionFromMemory( &version, pMemory, data.size() ) != TINYEXR_SUCCESS )
	{
		return fail( "not an OpenEXR file" );
	}
	if ( version.multipart || version.non_image )
	{
		return fail( "multi-part and deep OpenEXR files aren't read" );
	}
	EXRHeader header;
	InitEXRHeader( &header );
	const char *pszError = nullptr;
	if ( ParseEXRHeaderFromMemory( &header, &version, pMemory, data.size(), &pszError ) != TINYEXR_SUCCESS )
	{
		std::string szReason = pszError ? pszError : "bad OpenEXR header";
		FreeEXRErrorMessage( pszError );
		return fail( szReason.c_str() );
	}
	const long long nWidth = static_cast<long long>( header.data_window.max_x ) - header.data_window.min_x + 1;
	const long long nHeight = static_cast<long long>( header.data_window.max_y ) - header.data_window.min_y + 1;
	FreeEXRHeader( &header );
	if ( nWidth <= 0 || nHeight <= 0 || nWidth > MAX_DIMENSION || nHeight > MAX_DIMENSION )
	{
		return fail( "image too large" );
	}

	float *pRgba = nullptr;
	int nOutWidth = 0;
	int nOutHeight = 0;
	if ( LoadEXRFromMemory( &pRgba, &nOutWidth, &nOutHeight, pMemory, data.size(), &pszError ) != TINYEXR_SUCCESS )
	{
		std::string szReason = pszError ? pszError : "OpenEXR file could not be read";
		FreeEXRErrorMessage( pszError );
		return fail( szReason.c_str() );
	}
	CFloatImage image;
	image.m_nWidth = nOutWidth;
	image.m_nHeight = nOutHeight;
	image.m_Pixels.assign( pRgba, pRgba + static_cast<std::size_t>( nOutWidth ) * static_cast<std::size_t>( nOutHeight ) * 4 );
	std::free( pRgba );
	return image;
}

} // namespace xv::texture
