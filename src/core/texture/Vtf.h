//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Valve Texture Format, versions 7.0 to 7.5: reading, decoding the common image formats
//          to RGBA8, and writing 7.1/7.2 files laid out the way Valve's tools write them.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "texture/Image.h"

namespace xv::texture
{

// Image formats, numbered as in the file.
enum class VtfFormat_t : std::int32_t
{
	VTF_FORMAT_NONE = -1,
	RGBA8888 = 0,
	ABGR8888 = 1,
	RGB888 = 2,
	BGR888 = 3,
	RGB565 = 4,
	I8 = 5,
	IA88 = 6,
	P8 = 7,
	A8 = 8,
	RGB888_BLUESCREEN = 9,
	BGR888_BLUESCREEN = 10,
	ARGB8888 = 11,
	BGRA8888 = 12,
	DXT1 = 13,
	DXT3 = 14,
	DXT5 = 15,
	BGRX8888 = 16,
	BGR565 = 17,
	BGRX5551 = 18,
	BGRA4444 = 19,
	DXT1_ONEBITALPHA = 20,
	BGRA5551 = 21,
	UV88 = 22,
	UVWQ8888 = 23,
	RGBA16161616F = 24,
	RGBA16161616 = 25,
	UVLX8888 = 26,
	R32F = 27,
	RGB323232F = 28,
	RGBA32323232F = 29,
};

// Texture flags (the header's flags field).
namespace vtf_flag
{
inline constexpr std::uint32_t POINTSAMPLE = 0x1;
inline constexpr std::uint32_t TRILINEAR = 0x2;
inline constexpr std::uint32_t CLAMPS = 0x4;
inline constexpr std::uint32_t CLAMPT = 0x8;
inline constexpr std::uint32_t ANISOTROPIC = 0x10;
inline constexpr std::uint32_t HINT_DXT5 = 0x20;
inline constexpr std::uint32_t NORMAL = 0x80;
inline constexpr std::uint32_t NOMIP = 0x100;
inline constexpr std::uint32_t NOLOD = 0x200;
inline constexpr std::uint32_t ALL_MIPS = 0x400;
inline constexpr std::uint32_t PROCEDURAL = 0x800;
inline constexpr std::uint32_t ONEBITALPHA = 0x1000;
inline constexpr std::uint32_t EIGHTBITALPHA = 0x2000;
inline constexpr std::uint32_t ENVMAP = 0x4000;
} // namespace vtf_flag

[[nodiscard]] std::string_view FormatName( VtfFormat_t nFormat );
// The names of the known flags that are set, e.g. {"CLAMPS", "CLAMPT"}.
[[nodiscard]] std::vector<std::string> FlagNames( std::uint32_t nFlags );
// Bytes of one width x height image; 0 for a format of unknown size.
[[nodiscard]] std::size_t ImageSize( VtfFormat_t nFormat, int nWidth, int nHeight );

struct VtfInfo_t
{
	int m_nVersionMajor = 7;
	int m_nVersionMinor = 2;
	int m_nWidth = 0;
	int m_nHeight = 0;
	int m_nDepth = 1;
	std::uint32_t m_nFlags = 0;
	int m_nFrames = 1;
	int m_nFirstFrame = 0;
	int m_nFaces = 1; // 6 for cube maps (7 with the old sphere map)
	int m_nMipCount = 1;
	VtfFormat_t m_nFormat = VtfFormat_t::VTF_FORMAT_NONE;
	VtfFormat_t m_nLowResFormat = VtfFormat_t::VTF_FORMAT_NONE;
	int m_nLowResWidth = 0;
	int m_nLowResHeight = 0;
	std::array<float, 3> m_vecReflectivity{};
	float m_flBumpScale = 1.0f;
};

// An entry of the resource list (7.3+): a three-byte tag and either an offset to its data (a
// 32-bit size, then the bytes) or, with NO_DATA set, a value of its own.
struct VtfResource_t
{
	static constexpr std::uint8_t NO_DATA = 0x02;

	std::array<std::uint8_t, 3> m_Tag{};
	std::uint8_t m_nFlags = 0;
	std::uint32_t m_nValue = 0;
};
inline constexpr std::array<std::uint8_t, 3> SHEET_RESOURCE{ 0x10, 0, 0 }; // sprite sheet sequences

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

struct CVtf
{
	VtfInfo_t m_Info;
	std::vector<std::byte> m_Data; // the whole file
	std::size_t m_nImageOffset = 0; // where the full-size image data (all mipmaps) starts
	std::vector<VtfResource_t> m_Resources;

	// The data of a resource; empty if the file has none with that tag, or it is cut off.
	[[nodiscard]] std::span<const std::byte> Resource( std::array<std::uint8_t, 3> tag ) const;

	// The bytes of one image: mip 0 is full size. Empty if out of range.
	[[nodiscard]] std::span<const std::byte> Image( int nMip = 0, int nFrame = 0, int nFace = 0, int nSlice = 0 ) const;
	[[nodiscard]] std::optional<CImage> Decode( int nMip = 0, int nFrame = 0, int nFace = 0, std::string *pError = nullptr ) const;
	// See DecodeFloat.
	[[nodiscard]] std::optional<CFloatImage> DecodeFloat( int nMip = 0, int nFrame = 0, int nFace = 0, std::string *pError = nullptr ) const;
};

// Input is untrusted: every size and offset is checked against the file.
[[nodiscard]] std::optional<CVtf> ReadVtf( std::span<const std::byte> data, std::string *pError = nullptr );

// Raw image data to RGBA8. Float formats are clamped to 0-1 and converted from linear to sRGB.
[[nodiscard]] std::optional<CImage> DecodeImage( VtfFormat_t nFormat, int nWidth, int nHeight, std::span<const std::byte> data, std::string *pError = nullptr );
// Raw image data to light values. The float formats come back as they are; RGBA16161616 as 0-1;
// 8-bit formats are taken as sRGB and made linear.
[[nodiscard]] std::optional<CFloatImage> DecodeFloat( VtfFormat_t nFormat, int nWidth, int nHeight, std::span<const std::byte> data, std::string *pError = nullptr );
// The formats whose values can go above 1.
[[nodiscard]] bool IsFloatFormat( VtfFormat_t nFormat );
// RGBA8 to raw image data. Writes DXT1, DXT5, BGR888, RGB888, BGRA8888, RGBA8888 and BGRX8888.
[[nodiscard]] std::vector<std::byte> EncodeImage( const CImage &image, VtfFormat_t nFormat );
[[nodiscard]] bool CanEncode( VtfFormat_t nFormat );

struct VtfWriteOptions_t
{
	VtfFormat_t m_nFormat = VtfFormat_t::DXT1; // see encodeImage
	std::uint32_t m_nFlags = 0;
	bool m_bMipmaps = true;  // every level down to 1x1; otherwise only the image itself
	bool m_bLowRes = true;   // the small DXT1 copy (16x16 at most) that tools show as a thumbnail
	int m_nVersionMinor = 2; // 7.1 or 7.2
};

// The image's width and height don't have to be powers of two, but the game expects them to be.
[[nodiscard]] std::vector<std::byte> WriteVtf( const CImage &image, const VtfWriteOptions_t &options );

} // namespace xv::texture
