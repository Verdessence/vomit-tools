// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Decoding and playing sounds for previews, on top of miniaudio.
namespace xv::audio {

// A whole sound decoded to interleaved float samples at its own sample rate.
struct Clip {
	std::vector<float> samples;
	std::uint32_t channels = 0;
	std::uint32_t sampleRate = 0;
	// Frame that looping returns to: the WAV's first cue point (or sampler loop).
	std::optional<std::uint64_t> loopStart;

	[[nodiscard]] std::uint64_t frames() const;
	[[nodiscard]] double seconds() const;
};

// WAV (PCM, float, Microsoft and IMA ADPCM) or MP3. Input is untrusted.
[[nodiscard]] std::shared_ptr<const Clip> decode(std::span<const std::byte> data, std::string* error = nullptr);

struct PlayParams {
	float volume = 1.0f; // linear amplitude, as in Source's 0-1 volumes
	float pitch = 1.0f;  // playback speed; Source's pitch 100 is 1.0, and like the engine it shifts the tone too
	float pan = 0.0f;    // -1 left to 1 right
	bool loop = false;   // play to the end, then repeat from loopStart (or the start) until stopped
	double delaySeconds = 0.0;
	double startSeconds = 0.0; // where in the clip to begin
};

class Engine {
public:
	enum class Output {
		Device,  // the system's default audio output
		Offline, // no device; render() pulls the mix (for tests)
	};

	explicit Engine(Output output = Output::Device);
	~Engine();
	Engine(const Engine&) = delete;
	Engine& operator=(const Engine&) = delete;

	[[nodiscard]] bool ok() const;
	[[nodiscard]] const std::string& error() const;
	[[nodiscard]] std::uint32_t sampleRate() const;

	using Voice = std::uint64_t; // 0 is never a voice

	Voice play(std::shared_ptr<const Clip> clip, const PlayParams& params);
	void stop(Voice voice);
	void stopAll();
	[[nodiscard]] bool playing(Voice voice);
	// Where a voice is in its clip, in seconds of the clip (pitch doesn't change the scale);
	// nullopt once it has finished or been stopped.
	[[nodiscard]] std::optional<double> position(Voice voice);
	// Voices still playing; finished one-shots are released first.
	[[nodiscard]] std::size_t voiceCount();
	void setVolume(float volume);

	// Offline output only: mixes the next out.size() / 2 stereo frames.
	std::size_t render(std::span<float> out);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace xv::audio
