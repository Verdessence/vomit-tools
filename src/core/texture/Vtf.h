// SPDX-License-Identifier: GPL-3.0-or-later
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

// Valve Texture Format, versions 7.0 to 7.5: reading, decoding the common image formats to RGBA8,
// and writing 7.1/7.2 files laid out the way Valve's tools write them.
namespace xv::texture {

// Image formats, numbered as in the file.
enum class VtfFormat : std::int32_t {
	None = -1,
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
namespace vtf_flag {
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

[[nodiscard]] std::string_view formatName(VtfFormat format);
// The names of the known flags that are set, e.g. {"CLAMPS", "CLAMPT"}.
[[nodiscard]] std::vector<std::string> flagNames(std::uint32_t flags);
// Bytes of one width x height image; 0 for a format of unknown size.
[[nodiscard]] std::size_t imageSize(VtfFormat format, int width, int height);

struct VtfInfo {
	int versionMajor = 7;
	int versionMinor = 2;
	int width = 0;
	int height = 0;
	int depth = 1;
	std::uint32_t flags = 0;
	int frames = 1;
	int firstFrame = 0;
	int faces = 1; // 6 for cube maps (7 with the old sphere map)
	int mipCount = 1;
	VtfFormat format = VtfFormat::None;
	VtfFormat lowResFormat = VtfFormat::None;
	int lowResWidth = 0;
	int lowResHeight = 0;
	std::array<float, 3> reflectivity{};
	float bumpScale = 1.0f;
};

struct Vtf {
	VtfInfo info;
	std::vector<std::byte> data; // the whole file
	std::size_t imageOffset = 0; // where the full-size image data (all mipmaps) starts

	// The bytes of one image: mip 0 is full size. Empty if out of range.
	[[nodiscard]] std::span<const std::byte> image(int mip = 0, int frame = 0, int face = 0, int slice = 0) const;
	[[nodiscard]] std::optional<Image> decode(int mip = 0, int frame = 0, int face = 0, std::string* error = nullptr) const;
};

// Input is untrusted: every size and offset is checked against the file.
[[nodiscard]] std::optional<Vtf> readVtf(std::span<const std::byte> data, std::string* error = nullptr);

// Raw image data to RGBA8. Float formats are clamped to 0-1 and converted from linear to sRGB.
[[nodiscard]] std::optional<Image> decodeImage(VtfFormat format, int width, int height, std::span<const std::byte> data, std::string* error = nullptr);
// RGBA8 to raw image data. Writes DXT1, DXT5, BGR888, RGB888, BGRA8888, RGBA8888 and BGRX8888.
[[nodiscard]] std::vector<std::byte> encodeImage(const Image& image, VtfFormat format);
[[nodiscard]] bool canEncode(VtfFormat format);

struct VtfWriteOptions {
	VtfFormat format = VtfFormat::DXT1; // see encodeImage
	std::uint32_t flags = 0;
	bool mipmaps = true;  // every level down to 1x1; otherwise only the image itself
	bool lowRes = true;   // the small DXT1 copy (16x16 at most) that tools show as a thumbnail
	int versionMinor = 2; // 7.1 or 7.2
};

// The image's width and height don't have to be powers of two, but the game expects them to be.
[[nodiscard]] std::vector<std::byte> writeVtf(const Image& image, const VtfWriteOptions& options);

} // namespace xv::texture
