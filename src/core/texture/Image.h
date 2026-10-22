// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace xv::texture {

// 8-bit RGBA, rows from the top, no padding.
struct Image {
	int width = 0;
	int height = 0;
	std::vector<std::uint8_t> pixels; // width * height * 4

	Image() = default;
	Image(int width, int height); // transparent black

	[[nodiscard]] bool empty() const { return width <= 0 || height <= 0; }
	[[nodiscard]] std::uint8_t* pixel(int x, int y) { return pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4; }
	[[nodiscard]] const std::uint8_t* pixel(int x, int y) const { return pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4; }
	[[nodiscard]] bool operator==(const Image&) const = default;
};

// The extensions loadImage reads ("png", "tga", ...), lowercase and without the dot.
[[nodiscard]] bool isImageExtension(std::string_view extension);

// PNG, JPEG, TGA, BMP, PSD (composited), GIF (first frame), PNM, and Radiance HDR (gamma-mapped to
// 8 bits), recognised by content. Input is untrusted.
[[nodiscard]] std::optional<Image> loadImage(std::span<const std::byte> data, std::string* error = nullptr);

// Resampled in linear light (the pixels are taken as sRGB), with a Mitchell filter when shrinking
// and Catmull-Rom when enlarging.
[[nodiscard]] Image resize(const Image& image, int width, int height);

// The next mipmap level: half the size (at least 1x1), each pixel the average of up to four, in
// linear light.
[[nodiscard]] Image halfSize(const Image& image);

// Turned clockwise by quarterTurns (0-3), then mirrored left to right if mirror is set.
[[nodiscard]] Image oriented(const Image& image, int quarterTurns, bool mirror);

struct Orientation {
	int quarterTurns = 0; // clockwise, 0-3
	bool mirror = false;  // applied after the turn
	[[nodiscard]] bool operator==(const Orientation&) const = default;
};
// The one orientation that equals applying first, then second.
[[nodiscard]] Orientation combined(Orientation first, Orientation second);

[[nodiscard]] constexpr bool isPowerOfTwo(int n) { return n > 0 && (n & (n - 1)) == 0; }
// The power of two nearest to n, at least 1.
[[nodiscard]] int nearestPowerOfTwo(int n);

// sRGB 8-bit value to linear light 0-1 and back.
[[nodiscard]] float toLinear(std::uint8_t value);
[[nodiscard]] std::uint8_t fromLinear(float value);

} // namespace xv::texture
