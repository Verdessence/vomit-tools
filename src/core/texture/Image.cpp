// SPDX-License-Identifier: GPL-3.0-or-later
#include "Image.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>

#include "stb_image.h"
#include "stb_image_resize2.h"

namespace xv::texture {

namespace {

const std::array<float, 256>& linearTable() {
	static const std::array<float, 256> table = [] {
		std::array<float, 256> values{};
		for (std::size_t i = 0; i < values.size(); i++) {
			const float c = static_cast<float>(i) / 255.0f;
			values[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
		}
		return values;
	}();
	return table;
}

} // namespace

Image::Image(int width_, int height_)
		: width(std::max(width_, 0))
		, height(std::max(height_, 0))
		, pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) {}

bool isImageExtension(std::string_view extension) {
	static constexpr std::string_view EXTENSIONS[] = {"png", "jpg", "jpeg", "tga", "bmp", "psd", "gif", "hdr", "pnm", "ppm", "pgm"};
	return std::ranges::find(EXTENSIONS, extension) != std::end(EXTENSIONS);
}

std::optional<Image> loadImage(std::span<const std::byte> data, std::string* error) {
	if (data.empty() || data.size() > static_cast<std::size_t>(INT_MAX)) {
		if (error) {
			*error = data.empty() ? "empty file" : "file too large";
		}
		return std::nullopt;
	}
	int width = 0;
	int height = 0;
	int channels = 0;
	stbi_uc* decoded = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.data()), static_cast<int>(data.size()), &width, &height, &channels, 4);
	if (!decoded) {
		if (error) {
			const char* reason = stbi_failure_reason();
			*error = reason ? reason : "not an image";
		}
		return std::nullopt;
	}
	Image image{width, height};
	std::memcpy(image.pixels.data(), decoded, image.pixels.size());
	stbi_image_free(decoded);
	return image;
}

Image resize(const Image& image, int width, int height) {
	if (image.width == width && image.height == height) {
		return image;
	}
	Image out{width, height};
	if (!image.empty() && !out.empty()) {
		stbir_resize_uint8_srgb(image.pixels.data(), image.width, image.height, 0, out.pixels.data(), width, height, 0, STBIR_RGBA);
	}
	return out;
}

Image halfSize(const Image& image) {
	Image out{std::max(1, image.width / 2), std::max(1, image.height / 2)};
	const auto& linear = linearTable();
	for (int y = 0; y < out.height; y++) {
		for (int x = 0; x < out.width; x++) {
			std::array<float, 4> sum{};
			int count = 0;
			for (int dy = 0; dy < 2; dy++) {
				for (int dx = 0; dx < 2; dx++) {
					const int sx = std::min(x * 2 + dx, image.width - 1);
					const int sy = std::min(y * 2 + dy, image.height - 1);
					const std::uint8_t* p = image.pixel(sx, sy);
					for (int c = 0; c < 3; c++) {
						sum[c] += linear[p[c]];
					}
					sum[3] += p[3];
					count++;
				}
			}
			std::uint8_t* q = out.pixel(x, y);
			for (int c = 0; c < 3; c++) {
				q[c] = fromLinear(sum[c] / static_cast<float>(count));
			}
			q[3] = static_cast<std::uint8_t>(std::lround(sum[3] / static_cast<float>(count)));
		}
	}
	return out;
}

Image oriented(const Image& image, int quarterTurns, bool mirror) {
	quarterTurns = ((quarterTurns % 4) + 4) % 4;
	if (quarterTurns == 0 && !mirror) {
		return image;
	}
	const int w = image.width;
	const int h = image.height;
	const bool swapped = quarterTurns % 2 == 1;
	Image out{swapped ? h : w, swapped ? w : h};
	for (int y = 0; y < out.height; y++) {
		for (int x = 0; x < out.width; x++) {
			const int mx = mirror ? out.width - 1 - x : x; // the pixel of the turned (unmirrored) image
			int sx = mx;
			int sy = y;
			switch (quarterTurns) {
				case 1: // clockwise: the left column becomes the top row
					sx = y;
					sy = h - 1 - mx;
					break;
				case 2:
					sx = w - 1 - mx;
					sy = h - 1 - y;
					break;
				case 3:
					sx = w - 1 - y;
					sy = mx;
					break;
				default:
					break;
			}
			std::memcpy(out.pixel(x, y), image.pixel(sx, sy), 4);
		}
	}
	return out;
}

Orientation combined(Orientation first, Orientation second) {
	// A turn after a mirror is the mirror after the opposite turn.
	const int turns = first.quarterTurns + (first.mirror ? -second.quarterTurns : second.quarterTurns);
	return {((turns % 4) + 4) % 4, first.mirror != second.mirror};
}

int nearestPowerOfTwo(int n) {
	int power = 1;
	while (power < n && power < (1 << 29)) {
		power <<= 1;
	}
	// power >= n: step back when the smaller power is nearer.
	return power > 1 && n - power / 2 < power - n ? power / 2 : power;
}

float toLinear(std::uint8_t value) {
	return linearTable()[value];
}

std::uint8_t fromLinear(float value) {
	value = std::clamp(value, 0.0f, 1.0f);
	const float c = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
	return static_cast<std::uint8_t>(std::lround(c * 255.0f));
}

} // namespace xv::texture
