//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A demo's voice chat decoded (H5). Steam voice (the "steam" codec) is Opus at
//          24 kHz behind Steam's own framing: each packet's audio part is a run of frames,
//          each its length, a sequence number and one Opus packet of 20 ms; a length of
//          0xffff ends the stream (the decoder starts over), and frames missing from the
//          sequence are filled by Opus's loss concealment. vaudio_celt is CELT 0.11.3 (not
//          Opus's CELT): frames of 64 bytes, 512 samples each at 22050 Hz, back to back.
//          vaudio_speex isn't decoded.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "demo/Contents.h"

namespace xv::demo
{

// Samples a second, as Steam voice is decoded.
constexpr int STEAM_VOICE_RATE = 24000;

// One talker's decoder, kept from packet to packet.
class CVoiceDecoder
{
public:
	explicit CVoiceDecoder( const VoiceCodec_t &codec );
	~CVoiceDecoder();
	CVoiceDecoder( const CVoiceDecoder & ) = delete;
	CVoiceDecoder &operator=( const CVoiceDecoder & ) = delete;

	// Whether the codec is one this decodes.
	[[nodiscard]] bool IsSupported() const;
	[[nodiscard]] int GetSampleRate() const;
	// A packet's audio (svc_VoiceData's bytes), appended to samples (mono, -1 to 1). False when it doesn't read (a
	// part not understood, an Opus frame that doesn't decode); what did is kept.
	bool Decode( std::span<const std::byte> packet, std::vector<float> &samples );
	void Reset();
	// Frames that didn't decode, so far.
	[[nodiscard]] int GetFailures() const { return m_nFailures; }

private:
	struct Impl_t;
	std::unique_ptr<Impl_t> m_pImpl;
	int m_nFailures = 0;
};

// A stretch of talking decoded from its packets (one talker's, from its first tick to its
// last); empty when the codec isn't decoded. Its rate is GetSampleRate()'s.
[[nodiscard]] std::vector<float> DecodeSpeech( const CContents &contents, const Speech_t &speech, int *pSampleRate = nullptr, int *pFailures = nullptr );

} // namespace xv::demo
