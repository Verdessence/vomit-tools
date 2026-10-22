// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vtf.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <initializer_list>
#include <utility>

#include "stb_dxt.h"

namespace xv::texture {

namespace {

constexpr std::uint32_t NO_FORMAT = 0xFFFFFFFF;
constexpr int HEADER_SIZE_71 = 64;
constexpr int HEADER_SIZE_72 = 80;
constexpr std::size_t RESOURCES_OFFSET = 80; // 7.3+: resource entries follow the 7.2 header

template<typename T>
T readAt(std::span<const std::byte> data, std::size_t offset) {
	T value{};
	std::memcpy(&value, data.data() + offset, sizeof(T));
	return value;
}

template<typename T>
void writeAt(std::vector<std::byte>& out, std::size_t offset, T value) {
	std::memcpy(out.data() + offset, &value, sizeof(T));
}

std::uint16_t u16le(const std::byte* p) {
	return static_cast<std::uint16_t>(std::to_integer<unsigned>(p[0]) | (std::to_integer<unsigned>(p[1]) << 8));
}

std::uint8_t at(std::span<const std::byte> data, std::size_t i) {
	return std::to_integer<std::uint8_t>(data[i]);
}

std::uint8_t expand5(unsigned v) {
	return static_cast<std::uint8_t>((v << 3) | (v >> 2));
}
std::uint8_t expand6(unsigned v) {
	return static_cast<std::uint8_t>((v << 2) | (v >> 4));
}
std::uint8_t expand4(unsigned v) {
	return static_cast<std::uint8_t>(v * 17);
}

float halfToFloat(std::uint16_t h) {
	const unsigned sign = (h >> 15) & 1;
	const unsigned exponent = (h >> 10) & 31;
	const unsigned mantissa = h & 1023;
	float value = 0.0f;
	if (exponent == 0) {
		value = std::ldexp(static_cast<float>(mantissa), -24);
	} else if (exponent == 31) {
		value = mantissa ? 0.0f : 65504.0f; // NaN reads as black, infinity as the largest half
	} else {
		value = std::ldexp(static_cast<float>(mantissa | 1024), static_cast<int>(exponent) - 25);
	}
	return sign ? -value : value;
}

// The four colours of a DXT colour block; fourColour is false for DXT1's three-colour mode, where
// index 3 is transparent black.
void decodeColourBlock(const std::byte* block, bool alwaysFourColour, std::array<std::array<std::uint8_t, 4>, 4>& colours, std::uint32_t& indices) {
	const unsigned c0 = u16le(block);
	const unsigned c1 = u16le(block + 2);
	indices = readAt<std::uint32_t>(std::span{block, 8}, 4);
	colours[0] = {expand5(c0 >> 11), expand6((c0 >> 5) & 63), expand5(c0 & 31), 255};
	colours[1] = {expand5(c1 >> 11), expand6((c1 >> 5) & 63), expand5(c1 & 31), 255};
	if (alwaysFourColour || c0 > c1) {
		for (int c = 0; c < 3; c++) {
			colours[2][c] = static_cast<std::uint8_t>((2 * colours[0][c] + colours[1][c] + 1) / 3);
			colours[3][c] = static_cast<std::uint8_t>((colours[0][c] + 2 * colours[1][c] + 1) / 3);
		}
		colours[2][3] = 255;
		colours[3][3] = 255;
	} else {
		for (int c = 0; c < 3; c++) {
			colours[2][c] = static_cast<std::uint8_t>((colours[0][c] + colours[1][c]) / 2);
		}
		colours[2][3] = 255;
		colours[3] = {0, 0, 0, 0};
	}
}

void decodeDxt(VtfFormat format, int width, int height, std::span<const std::byte> data, Image& out) {
	const int blockBytes = format == VtfFormat::DXT1 || format == VtfFormat::DXT1_ONEBITALPHA ? 8 : 16;
	const int blocksWide = std::max(1, (width + 3) / 4);
	const int blocksHigh = std::max(1, (height + 3) / 4);
	for (int by = 0; by < blocksHigh; by++) {
		for (int bx = 0; bx < blocksWide; bx++) {
			const std::byte* block = data.data() + (static_cast<std::size_t>(by) * static_cast<std::size_t>(blocksWide) + static_cast<std::size_t>(bx)) * static_cast<std::size_t>(blockBytes);
			std::array<std::uint8_t, 16> alpha{};
			alpha.fill(255);
			if (format == VtfFormat::DXT3) {
				for (int i = 0; i < 16; i++) {
					const unsigned nibble = (std::to_integer<unsigned>(block[i / 2]) >> ((i % 2) * 4)) & 15;
					alpha[static_cast<std::size_t>(i)] = expand4(nibble);
				}
			} else if (format == VtfFormat::DXT5) {
				const unsigned a0 = std::to_integer<unsigned>(block[0]);
				const unsigned a1 = std::to_integer<unsigned>(block[1]);
				std::array<unsigned, 8> palette{a0, a1};
				if (a0 > a1) {
					for (unsigned i = 1; i < 7; i++) {
						palette[i + 1] = ((7 - i) * a0 + i * a1 + 3) / 7;
					}
				} else {
					for (unsigned i = 1; i < 5; i++) {
						palette[i + 1] = ((5 - i) * a0 + i * a1 + 2) / 5;
					}
					palette[6] = 0;
					palette[7] = 255;
				}
				std::uint64_t bits = 0;
				for (int i = 0; i < 6; i++) {
					bits |= static_cast<std::uint64_t>(std::to_integer<unsigned>(block[2 + i])) << (8 * i);
				}
				for (std::size_t i = 0; i < 16; i++) {
					alpha[i] = static_cast<std::uint8_t>(palette[(bits >> (3 * i)) & 7]);
				}
			}
			std::array<std::array<std::uint8_t, 4>, 4> colours{};
			std::uint32_t indices = 0;
			decodeColourBlock(block + (blockBytes - 8), blockBytes == 16, colours, indices);
			for (int i = 0; i < 16; i++) {
				const int x = bx * 4 + i % 4;
				const int y = by * 4 + i / 4;
				if (x >= width || y >= height) {
					continue;
				}
				const auto& colour = colours[(indices >> (2 * i)) & 3];
				std::uint8_t* p = out.pixel(x, y);
				std::memcpy(p, colour.data(), 4);
				if (blockBytes == 16) {
					p[3] = alpha[static_cast<std::size_t>(i)];
				}
			}
		}
	}
}

// Encodes one 4x4 block; pixels outside the image repeat the nearest edge pixel.
void encodeDxtBlock(const Image& image, int bx, int by, bool withAlpha, std::byte* out) {
	std::array<std::uint8_t, 64> rgba{};
	for (int i = 0; i < 16; i++) {
		const int x = std::min(bx * 4 + i % 4, image.width - 1);
		const int y = std::min(by * 4 + i / 4, image.height - 1);
		std::memcpy(rgba.data() + i * 4, image.pixel(x, y), 4);
	}
	auto* dest = reinterpret_cast<unsigned char*>(out);
	stb_compress_dxt_block(dest, rgba.data(), withAlpha ? 1 : 0, STB_DXT_HIGHQUAL);
	if (!withAlpha) {
		// Opaque DXT1 must stay in four-colour mode (first endpoint greater), or index 3 would be
		// transparent black.
		const std::uint16_t c0 = u16le(out);
		const std::uint16_t c1 = u16le(out + 2);
		std::uint32_t indices = readAt<std::uint32_t>(std::span{out, 8}, 4);
		if (c0 < c1) {
			std::swap(dest[0], dest[2]);
			std::swap(dest[1], dest[3]);
			indices ^= 0x55555555u; // endpoints swapped: 0<->1, 2<->3
		} else if (c0 == c1) {
			indices = 0;
		}
		std::memcpy(dest + 4, &indices, 4);
	}
}

int mipCountFor(int width, int height) {
	return std::bit_width(static_cast<unsigned>(std::max(width, height)));
}

} // namespace

std::string_view formatName(VtfFormat format) {
	switch (format) {
		case VtfFormat::None: return "none";
		case VtfFormat::RGBA8888: return "RGBA8888";
		case VtfFormat::ABGR8888: return "ABGR8888";
		case VtfFormat::RGB888: return "RGB888";
		case VtfFormat::BGR888: return "BGR888";
		case VtfFormat::RGB565: return "RGB565";
		case VtfFormat::I8: return "I8";
		case VtfFormat::IA88: return "IA88";
		case VtfFormat::P8: return "P8";
		case VtfFormat::A8: return "A8";
		case VtfFormat::RGB888_BLUESCREEN: return "RGB888_BLUESCREEN";
		case VtfFormat::BGR888_BLUESCREEN: return "BGR888_BLUESCREEN";
		case VtfFormat::ARGB8888: return "ARGB8888";
		case VtfFormat::BGRA8888: return "BGRA8888";
		case VtfFormat::DXT1: return "DXT1";
		case VtfFormat::DXT3: return "DXT3";
		case VtfFormat::DXT5: return "DXT5";
		case VtfFormat::BGRX8888: return "BGRX8888";
		case VtfFormat::BGR565: return "BGR565";
		case VtfFormat::BGRX5551: return "BGRX5551";
		case VtfFormat::BGRA4444: return "BGRA4444";
		case VtfFormat::DXT1_ONEBITALPHA: return "DXT1_ONEBITALPHA";
		case VtfFormat::BGRA5551: return "BGRA5551";
		case VtfFormat::UV88: return "UV88";
		case VtfFormat::UVWQ8888: return "UVWQ8888";
		case VtfFormat::RGBA16161616F: return "RGBA16161616F";
		case VtfFormat::RGBA16161616: return "RGBA16161616";
		case VtfFormat::UVLX8888: return "UVLX8888";
		case VtfFormat::R32F: return "R32F";
		case VtfFormat::RGB323232F: return "RGB323232F";
		case VtfFormat::RGBA32323232F: return "RGBA32323232F";
	}
	return "unknown";
}

std::vector<std::string> flagNames(std::uint32_t flags) {
	static constexpr std::pair<std::uint32_t, std::string_view> NAMES[] = {
		{0x1, "POINTSAMPLE"}, {0x2, "TRILINEAR"}, {0x4, "CLAMPS"}, {0x8, "CLAMPT"},
		{0x10, "ANISOTROPIC"}, {0x20, "HINT_DXT5"}, {0x80, "NORMAL"}, {0x100, "NOMIP"},
		{0x200, "NOLOD"}, {0x400, "ALL_MIPS"}, {0x800, "PROCEDURAL"}, {0x1000, "ONEBITALPHA"},
		{0x2000, "EIGHTBITALPHA"}, {0x4000, "ENVMAP"}, {0x8000, "RENDERTARGET"},
		{0x10000, "DEPTHRENDERTARGET"}, {0x20000, "NODEBUGOVERRIDE"}, {0x40000, "SINGLECOPY"},
		{0x800000, "NODEPTHBUFFER"}, {0x2000000, "CLAMPU"}, {0x4000000, "VERTEXTEXTURE"},
		{0x8000000, "SSBUMP"}, {0x20000000, "BORDER"},
	};
	std::vector<std::string> names;
	std::uint32_t unknown = flags;
	for (const auto& [bit, name] : NAMES) {
		if (flags & bit) {
			names.emplace_back(name);
			unknown &= ~bit;
		}
	}
	for (std::uint32_t bit = 1; bit; bit <<= 1) {
		if (unknown & bit) {
			names.push_back(std::format("0x{:x}", bit));
		}
	}
	return names;
}

std::size_t imageSize(VtfFormat format, int width, int height) {
	const auto w = static_cast<std::size_t>(std::max(width, 1));
	const auto h = static_cast<std::size_t>(std::max(height, 1));
	const std::size_t blocks = ((w + 3) / 4) * ((h + 3) / 4);
	switch (format) {
		case VtfFormat::DXT1:
		case VtfFormat::DXT1_ONEBITALPHA:
			return blocks * 8;
		case VtfFormat::DXT3:
		case VtfFormat::DXT5:
			return blocks * 16;
		case VtfFormat::I8:
		case VtfFormat::P8:
		case VtfFormat::A8:
			return w * h;
		case VtfFormat::RGB565:
		case VtfFormat::IA88:
		case VtfFormat::BGR565:
		case VtfFormat::BGRX5551:
		case VtfFormat::BGRA4444:
		case VtfFormat::BGRA5551:
		case VtfFormat::UV88:
			return w * h * 2;
		case VtfFormat::RGB888:
		case VtfFormat::BGR888:
		case VtfFormat::RGB888_BLUESCREEN:
		case VtfFormat::BGR888_BLUESCREEN:
			return w * h * 3;
		case VtfFormat::RGBA8888:
		case VtfFormat::ABGR8888:
		case VtfFormat::ARGB8888:
		case VtfFormat::BGRA8888:
		case VtfFormat::BGRX8888:
		case VtfFormat::UVWQ8888:
		case VtfFormat::UVLX8888:
		case VtfFormat::R32F:
			return w * h * 4;
		case VtfFormat::RGBA16161616F:
		case VtfFormat::RGBA16161616:
			return w * h * 8;
		case VtfFormat::RGB323232F:
			return w * h * 12;
		case VtfFormat::RGBA32323232F:
			return w * h * 16;
		case VtfFormat::None:
			return 0;
	}
	return 0;
}

std::span<const std::byte> Vtf::image(int mip, int frame, int face, int slice) const {
	if (mip < 0 || mip >= info.mipCount || frame < 0 || frame >= info.frames || face < 0 || face >= info.faces || slice < 0) {
		return {};
	}
	const auto levelSize = [this](int level) {
		return imageSize(info.format, info.width >> level, info.height >> level);
	};
	const auto levelDepth = [this](int level) {
		return static_cast<std::size_t>(std::max(1, info.depth >> level));
	};
	if (static_cast<std::size_t>(slice) >= levelDepth(mip)) {
		return {};
	}
	const auto images = static_cast<std::size_t>(info.frames) * static_cast<std::size_t>(info.faces);
	std::size_t offset = imageOffset;
	for (int level = info.mipCount - 1; level > mip; level--) {
		offset += images * levelDepth(level) * levelSize(level);
	}
	const std::size_t size = levelSize(mip);
	offset += ((static_cast<std::size_t>(frame) * static_cast<std::size_t>(info.faces) + static_cast<std::size_t>(face)) * levelDepth(mip) + static_cast<std::size_t>(slice)) * size;
	if (offset > data.size() || data.size() - offset < size) {
		return {};
	}
	return std::span{data}.subspan(offset, size);
}

std::optional<Image> Vtf::decode(int mip, int frame, int face, std::string* error) const {
	const auto bytes = image(mip, frame, face);
	if (bytes.empty()) {
		if (error) {
			*error = "no such image in the file";
		}
		return std::nullopt;
	}
	return decodeImage(info.format, std::max(1, info.width >> mip), std::max(1, info.height >> mip), bytes, error);
}

std::optional<Vtf> readVtf(std::span<const std::byte> data, std::string* error) {
	const auto fail = [error](std::string message) -> std::optional<Vtf> {
		if (error) {
			*error = std::move(message);
		}
		return std::nullopt;
	};
	if (data.size() < HEADER_SIZE_71 || std::memcmp(data.data(), "VTF\0", 4) != 0) {
		return fail("not a VTF file");
	}
	Vtf vtf;
	VtfInfo& info = vtf.info;
	info.versionMajor = static_cast<int>(readAt<std::uint32_t>(data, 4));
	info.versionMinor = static_cast<int>(readAt<std::uint32_t>(data, 8));
	if (info.versionMajor != 7 || info.versionMinor < 0 || info.versionMinor > 5) {
		return fail(std::format("VTF version {}.{} is not supported (7.0 to 7.5 are)", info.versionMajor, info.versionMinor));
	}
	const std::uint32_t headerSize = readAt<std::uint32_t>(data, 12);
	info.width = readAt<std::uint16_t>(data, 16);
	info.height = readAt<std::uint16_t>(data, 18);
	info.flags = readAt<std::uint32_t>(data, 20);
	info.frames = readAt<std::uint16_t>(data, 24);
	info.firstFrame = readAt<std::uint16_t>(data, 26);
	for (std::size_t i = 0; i < 3; i++) {
		info.reflectivity[i] = readAt<float>(data, 32 + i * 4);
	}
	info.bumpScale = readAt<float>(data, 48);
	info.format = static_cast<VtfFormat>(readAt<std::int32_t>(data, 52));
	info.mipCount = at(data, 56);
	const std::uint32_t lowResFormat = readAt<std::uint32_t>(data, 57);
	info.lowResFormat = lowResFormat == NO_FORMAT ? VtfFormat::None : static_cast<VtfFormat>(lowResFormat);
	info.lowResWidth = at(data, 61);
	info.lowResHeight = at(data, 62);
	if (info.versionMinor >= 2 && data.size() >= 65) {
		info.depth = std::max<int>(1, readAt<std::uint16_t>(data, 63));
	}
	if (headerSize < HEADER_SIZE_71 || headerSize > data.size()) {
		return fail("the header is cut off");
	}
	if (info.width == 0 || info.height == 0 || info.frames == 0 || info.mipCount == 0 || info.mipCount > 16) {
		return fail("the header has an empty size, frame count or mipmap count");
	}
	if (imageSize(info.format, 1, 1) == 0) {
		return fail(std::format("image format {} is not supported", static_cast<int>(info.format)));
	}
	if (info.flags & vtf_flag::ENVMAP) {
		info.faces = info.versionMinor < 5 && info.firstFrame != 0xFFFF ? 7 : 6;
	}

	if (info.versionMinor >= 3) {
		const std::uint32_t count = readAt<std::uint32_t>(data, 68);
		if (count > 32 || RESOURCES_OFFSET + count * 8 > headerSize) {
			return fail("the resource list is cut off");
		}
		std::optional<std::uint32_t> imageData;
		for (std::uint32_t i = 0; i < count; i++) {
			const std::size_t entry = RESOURCES_OFFSET + i * 8;
			if (at(data, entry) == 0x30 && at(data, entry + 1) == 0 && at(data, entry + 2) == 0) {
				imageData = readAt<std::uint32_t>(data, entry + 4);
			}
		}
		if (!imageData) {
			return fail("the file has no image data");
		}
		vtf.imageOffset = *imageData;
	} else {
		// Some tools name a low-res format but give it a 0x0 size: then it takes no space.
		const bool hasLowRes = info.lowResFormat != VtfFormat::None && info.lowResWidth > 0 && info.lowResHeight > 0;
		const std::size_t lowRes = hasLowRes ? imageSize(info.lowResFormat, info.lowResWidth, info.lowResHeight) : 0;
		vtf.imageOffset = headerSize + lowRes;
	}

	const auto totalSize = [&info] {
		std::uint64_t total = 0;
		for (int level = 0; level < info.mipCount; level++) {
			const auto depth = static_cast<std::uint64_t>(std::max(1, info.depth >> level));
			total += static_cast<std::uint64_t>(info.frames) * static_cast<std::uint64_t>(info.faces) * depth * imageSize(info.format, info.width >> level, info.height >> level);
		}
		return total;
	};
	if (vtf.imageOffset + totalSize() > data.size() && info.faces == 7) {
		info.faces = 6; // a cube map written without the sphere map
	}
	if (vtf.imageOffset > data.size() || vtf.imageOffset + totalSize() > data.size()) {
		return fail("the image data is cut off");
	}
	vtf.data.assign(data.begin(), data.end());
	return vtf;
}

std::optional<Image> decodeImage(VtfFormat format, int width, int height, std::span<const std::byte> data, std::string* error) {
	const std::size_t size = imageSize(format, width, height);
	if (size == 0 || data.size() < size || format == VtfFormat::P8) {
		if (error) {
			*error = size == 0 || format == VtfFormat::P8 ? std::format("can't decode {}", formatName(format)) : std::string{"the image data is cut off"};
		}
		return std::nullopt;
	}
	Image out{width, height};
	if (format == VtfFormat::DXT1 || format == VtfFormat::DXT1_ONEBITALPHA || format == VtfFormat::DXT3 || format == VtfFormat::DXT5) {
		decodeDxt(format, width, height, data, out);
		return out;
	}
	const std::size_t pixelBytes = size / (static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
	for (std::size_t i = 0; i < static_cast<std::size_t>(width) * static_cast<std::size_t>(height); i++) {
		const std::byte* s = data.data() + i * pixelBytes;
		const auto b = [s](int k) { return std::to_integer<std::uint8_t>(s[k]); };
		std::uint8_t* p = out.pixels.data() + i * 4;
		const auto set = [p](unsigned r, unsigned g, unsigned bl, unsigned a) {
			p[0] = static_cast<std::uint8_t>(r);
			p[1] = static_cast<std::uint8_t>(g);
			p[2] = static_cast<std::uint8_t>(bl);
			p[3] = static_cast<std::uint8_t>(a);
		};
		switch (format) {
			case VtfFormat::RGBA8888: set(b(0), b(1), b(2), b(3)); break;
			case VtfFormat::ABGR8888: set(b(3), b(2), b(1), b(0)); break;
			case VtfFormat::RGB888: set(b(0), b(1), b(2), 255); break;
			case VtfFormat::BGR888: set(b(2), b(1), b(0), 255); break;
			case VtfFormat::RGB888_BLUESCREEN: set(b(0), b(1), b(2), b(0) == 0 && b(1) == 0 && b(2) == 255 ? 0 : 255); break;
			case VtfFormat::BGR888_BLUESCREEN: set(b(2), b(1), b(0), b(2) == 0 && b(1) == 0 && b(0) == 255 ? 0 : 255); break;
			case VtfFormat::ARGB8888: set(b(1), b(2), b(3), b(0)); break;
			case VtfFormat::BGRA8888: set(b(2), b(1), b(0), b(3)); break;
			case VtfFormat::BGRX8888: set(b(2), b(1), b(0), 255); break;
			case VtfFormat::UVWQ8888:
			case VtfFormat::UVLX8888: set(b(0), b(1), b(2), b(3)); break;
			case VtfFormat::I8: set(b(0), b(0), b(0), 255); break;
			case VtfFormat::IA88: set(b(0), b(0), b(0), b(1)); break;
			case VtfFormat::A8: set(255, 255, 255, b(0)); break;
			case VtfFormat::UV88: set(b(0), b(1), 0, 255); break;
			case VtfFormat::RGB565:
			case VtfFormat::BGR565: {
				const unsigned v = u16le(s);
				const unsigned high = expand5(v >> 11);
				const unsigned low = expand5(v & 31);
				const unsigned green = expand6((v >> 5) & 63);
				format == VtfFormat::BGR565 ? set(high, green, low, 255) : set(low, green, high, 255);
				break;
			}
			case VtfFormat::BGRA4444: {
				const unsigned v = u16le(s);
				set(expand4((v >> 8) & 15), expand4((v >> 4) & 15), expand4(v & 15), expand4(v >> 12));
				break;
			}
			case VtfFormat::BGRA5551:
			case VtfFormat::BGRX5551: {
				const unsigned v = u16le(s);
				set(expand5((v >> 10) & 31), expand5((v >> 5) & 31), expand5(v & 31), format == VtfFormat::BGRX5551 || (v >> 15) ? 255 : 0);
				break;
			}
			case VtfFormat::RGBA16161616F: {
				for (int c = 0; c < 4; c++) {
					const float value = halfToFloat(u16le(s + c * 2));
					p[c] = c < 3 ? fromLinear(value) : static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
				}
				break;
			}
			case VtfFormat::RGBA16161616: set(b(1), b(3), b(5), b(7)); break; // the high byte of each channel
			case VtfFormat::R32F:
			case VtfFormat::RGB323232F:
			case VtfFormat::RGBA32323232F: {
				const int channels = format == VtfFormat::R32F ? 1 : format == VtfFormat::RGB323232F ? 3 : 4;
				for (int c = 0; c < 3; c++) {
					p[c] = fromLinear(readAt<float>(std::span{s, pixelBytes}, static_cast<std::size_t>(std::min(c, channels - 1)) * 4));
				}
				p[3] = channels == 4 ? static_cast<std::uint8_t>(std::lround(std::clamp(readAt<float>(std::span{s, pixelBytes}, 12), 0.0f, 1.0f) * 255.0f)) : 255;
				break;
			}
			default:
				break;
		}
	}
	return out;
}

bool canEncode(VtfFormat format) {
	switch (format) {
		case VtfFormat::DXT1:
		case VtfFormat::DXT5:
		case VtfFormat::BGR888:
		case VtfFormat::RGB888:
		case VtfFormat::BGRA8888:
		case VtfFormat::RGBA8888:
		case VtfFormat::BGRX8888:
			return true;
		default:
			return false;
	}
}

std::vector<std::byte> encodeImage(const Image& image, VtfFormat format) {
	std::vector<std::byte> out(imageSize(format, image.width, image.height));
	if (format == VtfFormat::DXT1 || format == VtfFormat::DXT5) {
		const int blockBytes = format == VtfFormat::DXT1 ? 8 : 16;
		const int blocksWide = std::max(1, (image.width + 3) / 4);
		const int blocksHigh = std::max(1, (image.height + 3) / 4);
		for (int by = 0; by < blocksHigh; by++) {
			for (int bx = 0; bx < blocksWide; bx++) {
				const std::size_t block = static_cast<std::size_t>(by) * static_cast<std::size_t>(blocksWide) + static_cast<std::size_t>(bx);
				encodeDxtBlock(image, bx, by, format == VtfFormat::DXT5, out.data() + block * static_cast<std::size_t>(blockBytes));
			}
		}
		return out;
	}
	const std::size_t pixels = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
	const std::size_t pixelBytes = pixels ? out.size() / pixels : 0;
	for (std::size_t i = 0; i < pixels; i++) {
		const std::uint8_t* p = image.pixels.data() + i * 4;
		std::byte* d = out.data() + i * pixelBytes;
		const auto put = [d](std::initializer_list<std::uint8_t> values) {
			std::size_t k = 0;
			for (std::uint8_t v : values) {
				d[k++] = static_cast<std::byte>(v);
			}
		};
		switch (format) {
			case VtfFormat::BGR888: put({p[2], p[1], p[0]}); break;
			case VtfFormat::RGB888: put({p[0], p[1], p[2]}); break;
			case VtfFormat::BGRA8888: put({p[2], p[1], p[0], p[3]}); break;
			case VtfFormat::RGBA8888: put({p[0], p[1], p[2], p[3]}); break;
			case VtfFormat::BGRX8888: put({p[2], p[1], p[0], 255}); break;
			default: return {};
		}
	}
	return out;
}

std::vector<std::byte> writeVtf(const Image& image, const VtfWriteOptions& options) {
	if (image.empty() || !canEncode(options.format) || image.width > 65535 || image.height > 65535) {
		return {};
	}
	const int headerSize = options.versionMinor >= 2 ? HEADER_SIZE_72 : HEADER_SIZE_71;
	const int mipCount = options.mipmaps ? mipCountFor(image.width, image.height) : 1;

	std::vector<Image> mips{image};
	for (int level = 1; level < mipCount; level++) {
		mips.push_back(halfSize(mips.back()));
	}

	int lowWidth = image.width;
	int lowHeight = image.height;
	while (lowWidth > 16 || lowHeight > 16) {
		lowWidth = std::max(1, lowWidth / 2);
		lowHeight = std::max(1, lowHeight / 2);
	}
	const std::vector<std::byte> lowRes = options.lowRes ? encodeImage(resize(image, lowWidth, lowHeight), VtfFormat::DXT1) : std::vector<std::byte>{};

	// The average colour in linear light, which the engine uses for bounced light.
	std::array<double, 3> reflectivity{};
	for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
		for (std::size_t c = 0; c < 3; c++) {
			reflectivity[c] += toLinear(image.pixels[i + c]);
		}
	}

	std::vector<std::byte> out(static_cast<std::size_t>(headerSize));
	std::memcpy(out.data(), "VTF\0", 4);
	writeAt<std::uint32_t>(out, 4, 7);
	writeAt<std::uint32_t>(out, 8, static_cast<std::uint32_t>(options.versionMinor >= 2 ? 2 : 1));
	writeAt<std::uint32_t>(out, 12, static_cast<std::uint32_t>(headerSize));
	writeAt<std::uint16_t>(out, 16, static_cast<std::uint16_t>(image.width));
	writeAt<std::uint16_t>(out, 18, static_cast<std::uint16_t>(image.height));
	writeAt<std::uint32_t>(out, 20, options.flags);
	writeAt<std::uint16_t>(out, 24, 1); // frames
	writeAt<std::uint16_t>(out, 26, 0); // first frame
	const double pixels = static_cast<double>(image.pixels.size()) / 4.0;
	for (std::size_t c = 0; c < 3; c++) {
		writeAt<float>(out, 32 + c * 4, static_cast<float>(reflectivity[c] / pixels));
	}
	writeAt<float>(out, 48, 1.0f); // bumpmap scale
	writeAt<std::int32_t>(out, 52, static_cast<std::int32_t>(options.format));
	out[56] = static_cast<std::byte>(mipCount);
	writeAt<std::uint32_t>(out, 57, options.lowRes ? static_cast<std::uint32_t>(VtfFormat::DXT1) : NO_FORMAT);
	out[61] = static_cast<std::byte>(options.lowRes ? lowWidth : 0);
	out[62] = static_cast<std::byte>(options.lowRes ? lowHeight : 0);
	if (options.versionMinor >= 2) {
		writeAt<std::uint16_t>(out, 63, 1); // depth
	}

	out.insert(out.end(), lowRes.begin(), lowRes.end());
	for (int level = mipCount - 1; level >= 0; level--) {
		const auto encoded = encodeImage(mips[static_cast<std::size_t>(level)], options.format);
		out.insert(out.end(), encoded.begin(), encoded.end());
	}
	return out;
}

} // namespace xv::texture
