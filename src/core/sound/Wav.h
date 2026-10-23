//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Reads the RIFF header chunks only. Input is untrusted; returns nullopt for anything
//          malformed.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace xv::sound
{

struct CWavInfo
{
	std::uint16_t m_nFormat = 0; // 1 PCM, 2 Microsoft ADPCM, 0x11 IMA ADPCM, 0x55 MP3, ...
	std::uint16_t m_nChannels = 0;
	std::uint32_t m_nSampleRate = 0;
	std::uint32_t m_nBytesPerSecond = 0;
	std::uint16_t m_nBitsPerSample = 0;
	std::uint32_t m_nDataBytes = 0;
	std::optional<std::uint32_t> m_SampleCount; // from the "fact" chunk, for compressed formats

	// The engine loops a sound from its first cue point (or sampler loop) to the end.
	// Without one, playlooping plays it once.
	std::optional<std::uint32_t> m_LoopStart;

	[[nodiscard]] double GetSeconds() const;
};

[[nodiscard]] std::optional<CWavInfo> ReadWavInfo( std::span<const std::byte> data );

} // namespace xv::sound
