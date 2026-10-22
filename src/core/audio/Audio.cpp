// SPDX-License-Identifier: GPL-3.0-or-later
#include "Audio.h"

#include <map>

#include <miniaudio.h>

#include "../sound/Wav.h"

namespace xv::audio {

namespace {

// Keeps a preview from eating all memory on a huge or hostile file: 20 minutes of 48 kHz stereo.
constexpr std::uint64_t MAX_SAMPLES = 48000ull * 2 * 60 * 20;

void setError(std::string* error, std::string message) {
	if (error) {
		*error = std::move(message);
	}
}

} // namespace

std::uint64_t Clip::frames() const {
	return channels ? samples.size() / channels : 0;
}

double Clip::seconds() const {
	return sampleRate ? static_cast<double>(frames()) / sampleRate : 0.0;
}

std::shared_ptr<const Clip> decode(std::span<const std::byte> data, std::string* error) {
	ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
	ma_decoder decoder;
	if (ma_decoder_init_memory(data.data(), data.size(), &config, &decoder) != MA_SUCCESS) {
		setError(error, "not a WAV or MP3 file this decoder understands");
		return nullptr;
	}

	auto clip = std::make_shared<Clip>();
	ma_format format{};
	if (ma_decoder_get_data_format(&decoder, &format, &clip->channels, &clip->sampleRate, nullptr, 0) != MA_SUCCESS
		|| clip->channels == 0 || clip->sampleRate == 0) {
		ma_decoder_uninit(&decoder);
		setError(error, "the file has no usable audio format");
		return nullptr;
	}

	constexpr ma_uint64 CHUNK = 4096;
	std::vector<float> chunk(CHUNK * clip->channels);
	while (clip->samples.size() < MAX_SAMPLES) {
		ma_uint64 read = 0;
		const ma_result result = ma_decoder_read_pcm_frames(&decoder, chunk.data(), CHUNK, &read);
		clip->samples.insert(clip->samples.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(read * clip->channels));
		if (result != MA_SUCCESS || read < CHUNK) {
			break;
		}
	}
	ma_decoder_uninit(&decoder);

	if (clip->frames() == 0) {
		setError(error, "the file has no audio data");
		return nullptr;
	}
	if (const auto info = sound::readWavInfo(data); info && info->loopStart && *info->loopStart < clip->frames()) {
		clip->loopStart = *info->loopStart;
	}
	return clip;
}

struct Engine::Impl {
	struct Playing {
		std::shared_ptr<const Clip> clip;
		ma_audio_buffer buffer{};
		ma_sound sound{};
		bool loop = false;
	};

	ma_engine engine{};
	bool initialized = false;
	std::string error;
	std::map<Voice, std::unique_ptr<Playing>> voices;
	Voice next = 1;

	static void release(Playing& playing) {
		ma_sound_uninit(&playing.sound);
		ma_audio_buffer_uninit(&playing.buffer);
	}

	void releaseFinished() {
		for (auto it = voices.begin(); it != voices.end();) {
			if (!it->second->loop && ma_sound_at_end(&it->second->sound)) {
				release(*it->second);
				it = voices.erase(it);
			} else {
				++it;
			}
		}
	}
};

Engine::Engine(Output output)
		: impl_(std::make_unique<Impl>()) {
	ma_engine_config config = ma_engine_config_init();
	if (output == Output::Offline) {
		config.noDevice = MA_TRUE;
		config.channels = 2;
		config.sampleRate = 48000;
	}
	if (ma_engine_init(&config, &impl_->engine) != MA_SUCCESS) {
		impl_->error = output == Output::Device ? "no audio output device could be opened" : "the audio mixer could not start";
		return;
	}
	impl_->initialized = true;
}

Engine::~Engine() {
	if (impl_->initialized) {
		stopAll();
		ma_engine_uninit(&impl_->engine);
	}
}

bool Engine::ok() const {
	return impl_->initialized;
}

const std::string& Engine::error() const {
	return impl_->error;
}

std::uint32_t Engine::sampleRate() const {
	return impl_->initialized ? ma_engine_get_sample_rate(&impl_->engine) : 0;
}

Engine::Voice Engine::play(std::shared_ptr<const Clip> clip, const PlayParams& params) {
	if (!impl_->initialized || !clip || clip->frames() == 0) {
		return 0;
	}
	impl_->releaseFinished();

	auto playing = std::make_unique<Impl::Playing>();
	playing->clip = std::move(clip);
	playing->loop = params.loop;
	const Clip& c = *playing->clip;

	ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(ma_format_f32, c.channels, c.frames(), c.samples.data(), nullptr);
	bufferConfig.sampleRate = c.sampleRate;
	if (ma_audio_buffer_init(&bufferConfig, &playing->buffer) != MA_SUCCESS) {
		return 0;
	}
	if (params.loop) {
		ma_data_source_set_loop_point_in_pcm_frames(&playing->buffer, c.loopStart.value_or(0), c.frames());
	}
	if (ma_sound_init_from_data_source(&impl_->engine, &playing->buffer, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &playing->sound) != MA_SUCCESS) {
		ma_audio_buffer_uninit(&playing->buffer);
		return 0;
	}
	ma_sound_set_looping(&playing->sound, params.loop ? MA_TRUE : MA_FALSE);
	ma_sound_set_volume(&playing->sound, params.volume);
	ma_sound_set_pitch(&playing->sound, params.pitch > 0 ? params.pitch : 1.0f);
	ma_sound_set_pan(&playing->sound, params.pan);
	if (params.delaySeconds > 0) {
		const auto delay = static_cast<ma_uint64>(params.delaySeconds * 1000.0);
		ma_sound_set_start_time_in_milliseconds(&playing->sound, ma_engine_get_time_in_milliseconds(&impl_->engine) + delay);
	}
	if (ma_sound_start(&playing->sound) != MA_SUCCESS) {
		Impl::release(*playing);
		return 0;
	}
	const Voice voice = impl_->next++;
	impl_->voices.emplace(voice, std::move(playing));
	return voice;
}

void Engine::stop(Voice voice) {
	const auto it = impl_->voices.find(voice);
	if (it != impl_->voices.end()) {
		Impl::release(*it->second);
		impl_->voices.erase(it);
	}
}

void Engine::stopAll() {
	for (auto& [voice, playing] : impl_->voices) {
		Impl::release(*playing);
	}
	impl_->voices.clear();
}

bool Engine::playing(Voice voice) {
	impl_->releaseFinished();
	return impl_->voices.contains(voice);
}

std::size_t Engine::voiceCount() {
	impl_->releaseFinished();
	return impl_->voices.size();
}

void Engine::setVolume(float volume) {
	if (impl_->initialized) {
		ma_engine_set_volume(&impl_->engine, volume);
	}
}

std::size_t Engine::render(std::span<float> out) {
	if (!impl_->initialized) {
		return 0;
	}
	ma_uint64 read = 0;
	ma_engine_read_pcm_frames(&impl_->engine, out.data(), out.size() / 2, &read);
	return static_cast<std::size_t>(read);
}

} // namespace xv::audio
