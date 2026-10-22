// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace xv::sound {

struct WavInfo {
	std::uint16_t format = 0; // 1 PCM, 2 Microsoft ADPCM, 0x11 IMA ADPCM, 0x55 MP3, ...
	std::uint16_t channels = 0;
	std::uint32_t sampleRate = 0;
	std::uint32_t bytesPerSecond = 0;
	std::uint16_t bitsPerSample = 0;
	std::uint32_t dataBytes = 0;
	std::optional<std::uint32_t> sampleCount; // from the "fact" chunk, for compressed formats

	// The engine loops a sound from its first cue point (or sampler loop) to the end.
	// Without one, playlooping plays it once.
	std::optional<std::uint32_t> loopStart;

	[[nodiscard]] double seconds() const;
};

// Reads the RIFF header chunks only. Input is untrusted; returns nullopt for anything malformed.
[[nodiscard]] std::optional<WavInfo> readWavInfo(std::span<const std::byte> data);

} // namespace xv::sound
