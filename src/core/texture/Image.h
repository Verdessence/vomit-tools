//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: 8-bit RGBA, rows from the top, no padding.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace xv::texture
{

struct CImage
{
	int m_nWidth = 0;
	int m_nHeight = 0;
	std::vector<std::uint8_t> m_Pixels; // width * height * 4

	CImage() = default;
	CImage( int nWidth, int nHeight ); // transparent black

	[[nodiscard]] bool Empty() const { return m_nWidth <= 0 || m_nHeight <= 0; }
	[[nodiscard]] std::uint8_t *Pixel( int x, int y ) { return m_Pixels.data() + ( static_cast<std::size_t>( y ) * static_cast<std::size_t>( m_nWidth ) + static_cast<std::size_t>( x ) ) * 4; }
	[[nodiscard]] const std::uint8_t *Pixel( int x, int y ) const { return m_Pixels.data() + ( static_cast<std::size_t>( y ) * static_cast<std::size_t>( m_nWidth ) + static_cast<std::size_t>( x ) ) * 4; }
	[[nodiscard]] bool operator==( const CImage & ) const = default;
};

// Light values as an HDR texture holds them: linear, not clamped, red, green, blue and alpha per
// pixel, rows from the top.
struct CFloatImage
{
	int m_nWidth = 0;
	int m_nHeight = 0;
	std::vector<float> m_Pixels; // width * height * 4

	[[nodiscard]] bool Empty() const { return m_nWidth <= 0 || m_nHeight <= 0; }
	[[nodiscard]] const float *Pixel( int x, int y ) const { return m_Pixels.data() + ( static_cast<std::size_t>( y ) * static_cast<std::size_t>( m_nWidth ) + static_cast<std::size_t>( x ) ) * 4; }
};

// The extensions loadImage reads ("png", "tga", ...), lowercase and without the dot.
[[nodiscard]] bool IsImageExtension( std::string_view szExtension );

// PNG, JPEG, TGA, BMP, PSD (composited), GIF (first frame), PNM, QOI, and Radiance HDR and PFM
// (gamma-mapped to 8 bits), recognised by content. Input is untrusted.
[[nodiscard]] std::optional<CImage> LoadImage( std::span<const std::byte> data, std::string *pError = nullptr );
// Every frame of an animated GIF or PNG (APNG); other files, their one picture. Input is
// untrusted.
[[nodiscard]] std::vector<CImage> LoadImageFrames( std::span<const std::byte> data, std::string *pError = nullptr );

// File types pictures can be saved as.
enum class ImageFileType_t
{
	IMAGE_FILE_TYPE_PNG,
	IMAGE_FILE_TYPE_TGA,
	IMAGE_FILE_TYPE_BMP,
	IMAGE_FILE_TYPE_JPG,
	IMAGE_FILE_TYPE_HDR, // Radiance HDR: light values, from SaveFloatImage
	IMAGE_FILE_TYPE_QOI,
	IMAGE_FILE_TYPE_PFM, // Portable Float Map: light values, from SaveFloatImage
};
// The type for a file name's extension ("png", "TGA"...); PNG for anything else.
[[nodiscard]] ImageFileType_t ImageFileTypeFor( std::string_view szExtension );
// A picture as the bytes of an image file (JPEG at quality 95; HDR goes through light values).
// Empty if it can't be written.
[[nodiscard]] std::vector<std::byte> SaveImage( const CImage &image, ImageFileType_t nType );
// Light values as a Radiance HDR (or PFM) file, keeping values above 1.
[[nodiscard]] std::vector<std::byte> SaveFloatImage( const CFloatImage &image, ImageFileType_t nType = ImageFileType_t::IMAGE_FILE_TYPE_HDR );

// A Radiance HDR or PFM file as its light values; nullopt for other files (read those with
// LoadImage).
[[nodiscard]] std::optional<CFloatImage> LoadFloatImage( std::span<const std::byte> data );

// Resampled in linear light (the pixels are taken as sRGB), with a Mitchell filter when shrinking
// and Catmull-Rom when enlarging.
[[nodiscard]] CImage Resize( const CImage &image, int nWidth, int nHeight );
// Light values resampled the same way.
[[nodiscard]] CFloatImage Resize( const CFloatImage &image, int nWidth, int nHeight );

// The next mipmap level: half the size (at least 1x1), each pixel the average of up to four, in
// linear light.
[[nodiscard]] CImage HalfSize( const CImage &image );

// Turned clockwise by quarterTurns (0-3), then mirrored left to right if mirror is set.
[[nodiscard]] CImage Oriented( const CImage &image, int nQuarterTurns, bool bMirror );
[[nodiscard]] CFloatImage Oriented( const CFloatImage &image, int nQuarterTurns, bool bMirror );

struct COrientation
{
	int m_nQuarterTurns = 0; // clockwise, 0-3
	bool m_bMirror = false;  // applied after the turn
	[[nodiscard]] bool operator==( const COrientation & ) const = default;
};
// The one orientation that equals applying first, then second.
[[nodiscard]] COrientation Combined( COrientation first, COrientation second );

[[nodiscard]] constexpr bool IsPowerOfTwo( int n )
{
	return n > 0 && ( n & ( n - 1 ) ) == 0;
}
// The power of two nearest to n, at least 1.
[[nodiscard]] int NearestPowerOfTwo( int n );

// sRGB 8-bit value to linear light 0-1 and back.
[[nodiscard]] float ToLinear( std::uint8_t nValue );
[[nodiscard]] std::uint8_t FromLinear( float flValue );

} // namespace xv::texture
