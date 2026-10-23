//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Decoding and playing sounds for previews, on top of miniaudio.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace xv::audio
{

// A whole sound decoded to interleaved float samples at its own sample rate.
struct CClip
{
	std::vector<float> m_Samples;
	std::uint32_t m_nChannels = 0;
	std::uint32_t m_nSampleRate = 0;
	// Frame that looping returns to: the WAV's first cue point (or sampler loop).
	std::optional<std::uint64_t> m_LoopStart;

	[[nodiscard]] std::uint64_t GetFrames() const;
	[[nodiscard]] double GetSeconds() const;
};

// WAV (PCM, float, Microsoft and IMA ADPCM) or MP3. Input is untrusted.
[[nodiscard]] std::shared_ptr<const CClip> Decode( std::span<const std::byte> data, std::string *pError = nullptr );

struct PlayParams_t
{
	float m_flVolume = 1.0f; // linear amplitude, as in Source's 0-1 volumes
	float m_flPitch = 1.0f;  // playback speed; Source's pitch 100 is 1.0, and like the engine it shifts the tone too
	float m_flPan = 0.0f;    // -1 left to 1 right
	bool m_bLoop = false;   // play to the end, then repeat from loopStart (or the start) until stopped
	double m_flDelaySeconds = 0.0;
	double m_flStartSeconds = 0.0; // where in the clip to begin
};

class CEngine
{
public:
	enum class Output_t
	{
		OUTPUT_DEVICE,  // the system's default audio output
		OUTPUT_OFFLINE, // no device; render() pulls the mix (for tests)
	};

	explicit CEngine( Output_t nOutput = Output_t::OUTPUT_DEVICE );
	~CEngine();
	CEngine( const CEngine & ) = delete;
	CEngine &operator=( const CEngine & ) = delete;

	[[nodiscard]] bool GetOk() const;
	[[nodiscard]] const std::string &GetError() const;
	[[nodiscard]] std::uint32_t GetSampleRate() const;

	using Voice_t = std::uint64_t; // 0 is never a voice

	Voice_t Play( std::shared_ptr<const CClip> pClip, const PlayParams_t &params );
	void Stop( Voice_t nVoice );
	void StopAll();
	[[nodiscard]] bool Playing( Voice_t nVoice );
	// Where a voice is in its clip, in seconds of the clip (pitch doesn't change the scale);
	// nullopt once it has finished or been stopped.
	[[nodiscard]] std::optional<double> Position( Voice_t nVoice );
	// Voices still playing; finished one-shots are released first.
	[[nodiscard]] std::size_t VoiceCount();
	void SetVolume( float flVolume );

	// Offline output only: mixes the next out.size() / 2 stereo frames.
	std::size_t Render( std::span<float> out );

private:
	struct CImpl;
	std::unique_ptr<CImpl> m_pImpl;
};

} // namespace xv::audio
