// SPDX-License-Identifier: GPL-3.0-or-later
#include "Wav.h"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace xv::sound {

namespace {

template<typename T>
std::optional<T> readAt(std::span<const std::byte> data, std::size_t offset) {
	if (offset > data.size() || data.size() - offset < sizeof(T)) {
		return std::nullopt;
	}
	T value{};
	std::memcpy(&value, data.data() + offset, sizeof(T));
	return value;
}

bool tagIs(std::span<const std::byte> data, std::size_t offset, std::string_view tag) {
	return offset + 4 <= data.size() && std::memcmp(data.data() + offset, tag.data(), 4) == 0;
}

} // namespace

double WavInfo::seconds() const {
	if (sampleCount && sampleRate) {
		return static_cast<double>(*sampleCount) / sampleRate;
	}
	return bytesPerSecond ? static_cast<double>(dataBytes) / bytesPerSecond : 0.0;
}

std::optional<WavInfo> readWavInfo(std::span<const std::byte> data) {
	if (!tagIs(data, 0, "RIFF") || !tagIs(data, 8, "WAVE")) {
		return std::nullopt;
	}
	WavInfo info;
	bool haveFormat = false;
	std::size_t offset = 12;
	while (offset + 8 <= data.size()) {
		const auto size = readAt<std::uint32_t>(data, offset + 4);
		if (!size) {
			break;
		}
		const std::size_t body = offset + 8;
		const std::size_t available = data.size() - body;
		const std::size_t length = std::min<std::size_t>(*size, available); // the data chunk is often truncated in practice

		if (tagIs(data, offset, "fmt ") && length >= 16) {
			info.format = *readAt<std::uint16_t>(data, body);
			info.channels = *readAt<std::uint16_t>(data, body + 2);
			info.sampleRate = *readAt<std::uint32_t>(data, body + 4);
			info.bytesPerSecond = *readAt<std::uint32_t>(data, body + 8);
			info.bitsPerSample = *readAt<std::uint16_t>(data, body + 14);
			haveFormat = true;
		} else if (tagIs(data, offset, "data")) {
			info.dataBytes = static_cast<std::uint32_t>(length);
		} else if (tagIs(data, offset, "fact") && length >= 4) {
			info.sampleCount = readAt<std::uint32_t>(data, body);
		} else if (tagIs(data, offset, "cue ") && length >= 4 + 24) {
			// A count, then 24-byte cue points whose sample offset sits at byte 20. The count is
			// ignored: stock files used for looping (CS:S's ambient/machines/fluorescent_hum_2.wav)
			// have a count of 0 followed by a real cue point.
			if (const auto start = readAt<std::uint32_t>(data, body + 4 + 20); start && !info.loopStart) {
				info.loopStart = *start;
			}
		} else if (tagIs(data, offset, "smpl") && length >= 36) {
			// loop count at byte 28, then 24-byte loops whose start sits at byte 8
			const auto loops = readAt<std::uint32_t>(data, body + 28);
			if (loops && *loops > 0 && length >= 36 + 24 && !info.loopStart) {
				info.loopStart = readAt<std::uint32_t>(data, body + 36 + 8);
			}
		}

		// Chunks are padded to an even size.
		const std::size_t next = body + *size + (*size & 1);
		if (next <= offset) {
			break;
		}
		offset = next;
	}
	if (!haveFormat) {
		return std::nullopt;
	}
	return info;
}

} // namespace xv::sound
