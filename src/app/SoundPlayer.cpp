// SPDX-License-Identifier: GPL-3.0-or-later
#include "SoundPlayer.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>

#include <QStringList>
#include <QTimer>

#include "vfs/Vfs.h"

using xv::sound::PlayRequest;

namespace {

constexpr std::size_t CACHE_LIMIT = 256u * 1024 * 1024; // bytes of decoded audio kept around

std::mt19937& randomEngine() {
	static std::mt19937 engine{std::random_device{}()};
	return engine;
}

QString describe(const PlayRequest& request) {
	QStringList parts{QString::fromStdString(request.wave)};
	if (request.volume != 1) {
		parts << QObject::tr("volume %1").arg(request.volume, 0, 'g', 3);
	}
	if (request.pitch != 100) {
		parts << QObject::tr("pitch %1").arg(request.pitch, 0, 'f', 0);
	}
	if (request.loop) {
		parts << QObject::tr("looping");
	}
	if (request.delaySeconds > 0) {
		parts << QObject::tr("after %1 s").arg(request.delaySeconds, 0, 'g', 3);
	}
	return parts.join(QStringLiteral(" · "));
}

} // namespace

SoundPlayer::SoundPlayer(const xv::vfs::FileSystem& fileSystem, QObject* parent, xv::audio::Engine::Output output)
		: QObject(parent)
		, fileSystem_(fileSystem)
		, output_(output)
		, timer_(new QTimer(this)) {
	timer_->setInterval(50);
	connect(timer_, &QTimer::timeout, this, &SoundPlayer::tick);
}

SoundPlayer::~SoundPlayer() = default;

bool SoundPlayer::available() {
	if (!triedEngine_) {
		triedEngine_ = true;
		engine_ = std::make_unique<xv::audio::Engine>(output_);
		if (engine_->ok()) {
			engine_->setVolume(volume_);
		} else {
			reason_ = tr("No audio output: %1").arg(QString::fromStdString(engine_->error()));
			engine_.reset();
		}
	}
	if (!engine_) {
		emit failed(reason_);
	}
	return engine_ != nullptr;
}

std::optional<std::vector<std::byte>> SoundPlayer::readGameFile(const xv::vfs::FileSystem& fileSystem, const std::string& gamePath, const std::filesystem::path& localRoot) {
	if (auto bytes = fileSystem.read(gamePath)) {
		return bytes;
	}
	if (localRoot.empty()) {
		return std::nullopt;
	}
	const auto file = xv::vfs::findCaseInsensitive(localRoot / xv::vfs::fromUtf8(gamePath));
	std::ifstream stream{file, std::ios::binary};
	if (!stream) {
		return std::nullopt;
	}
	const std::vector<char> chars{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
	std::vector<std::byte> bytes(chars.size());
	std::memcpy(bytes.data(), chars.data(), chars.size());
	return bytes;
}

std::shared_ptr<const xv::audio::Clip> SoundPlayer::decodeCached(const std::string& key, std::span<const std::byte> bytes, QString* error) {
	if (const auto it = cache_.find(key); it != cache_.end()) {
		return it->second;
	}
	std::string message;
	auto clip = xv::audio::decode(bytes, &message);
	if (!clip) {
		if (error) {
			*error = QString::fromStdString(message);
		}
		return nullptr;
	}
	cache_.emplace(key, clip);
	cacheOrder_.push_back(key);
	cacheBytes_ += clip->samples.size() * sizeof(float);
	while (cacheBytes_ > CACHE_LIMIT && cacheOrder_.size() > 1) {
		const auto evicted = cache_.find(cacheOrder_.front());
		cacheBytes_ -= evicted->second->samples.size() * sizeof(float);
		cache_.erase(evicted);
		cacheOrder_.pop_front();
	}
	return clip;
}

std::shared_ptr<const xv::audio::Clip> SoundPlayer::load(const std::string& gamePath, const std::filesystem::path& localRoot) {
	const std::string key = gamePath + '\n' + xv::vfs::toUtf8(localRoot);
	if (const auto it = cache_.find(key); it != cache_.end()) {
		return it->second;
	}
	const auto bytes = readGameFile(fileSystem_, gamePath, localRoot);
	if (!bytes) {
		emit failed(tr("%1 isn't in the game").arg(QString::fromStdString(gamePath)));
		return nullptr;
	}
	QString error;
	auto clip = decodeCached(key, *bytes, &error);
	if (!clip) {
		emit failed(tr("Can't play %1: %2").arg(QString::fromStdString(gamePath), error));
	}
	return clip;
}

xv::audio::Engine::Voice SoundPlayer::start(std::shared_ptr<const xv::audio::Clip> clip, const PlayRequest& request, double startSeconds) {
	if (!clip || !engine_) {
		return 0;
	}
	xv::audio::PlayParams params;
	params.volume = static_cast<float>(request.volume);
	params.pitch = static_cast<float>(request.pitch / 100.0);
	params.loop = request.loop;
	params.delaySeconds = request.delaySeconds;
	params.startSeconds = startSeconds;
	// "position random" puts each sound somewhere around the listener; a stereo pan stands in for that.
	if (request.randomPosition) {
		params.pan = std::uniform_real_distribution<float>{-0.8f, 0.8f}(randomEngine());
	}
	const auto voice = engine_->play(std::move(clip), params);
	if (voice == 0) {
		emit failed(tr("Could not play %1").arg(QString::fromStdString(request.wave)));
		return 0;
	}
	setActive(true);
	return voice;
}

bool SoundPlayer::playData(const QString& name, std::span<const std::byte> bytes, bool loop, double startSeconds) {
	if (!available()) {
		return false;
	}
	stop();
	QString error;
	auto clip = decodeCached("file:" + name.toStdString() + ':' + std::to_string(bytes.size()), bytes, &error);
	if (!clip) {
		emit failed(tr("Can't play %1: %2").arg(name, error));
		return false;
	}
	PlayRequest request;
	request.wave = name.toStdString();
	request.loop = loop;
	lastVoice_ = start(std::move(clip), request, startSeconds);
	if (lastVoice_ == 0) {
		return false;
	}
	emit started(describe(request));
	return true;
}

bool SoundPlayer::playGameSound(const std::string& gamePath, const std::filesystem::path& localRoot) {
	PlayRequest request;
	request.gamePath = gamePath;
	request.wave = gamePath.starts_with("sound/") ? gamePath.substr(6) : gamePath;
	return play(request, localRoot);
}

bool SoundPlayer::play(const PlayRequest& request, const std::filesystem::path& localRoot) {
	if (!available()) {
		return false;
	}
	if (request.gamePath.empty()) {
		emit failed(tr("%1 is a sentence or voice stream, not a sound file").arg(QString::fromStdString(request.wave)));
		return false;
	}
	stop();
	lastVoice_ = start(load(request.gamePath, localRoot), request);
	if (lastVoice_ == 0) {
		return false;
	}
	emit started(describe(request));
	return true;
}

bool SoundPlayer::playSoundscape(const xv::sound::SoundscapePlan& plan, const QString& name, const std::filesystem::path& localRoot) {
	if (!available()) {
		return false;
	}
	stop();
	if (plan.rules.empty()) {
		emit failed(tr("%1 has nothing to play").arg(name));
		return false;
	}
	simulation_.emplace(plan.rules, std::random_device{}());
	simulationRoot_ = localRoot;
	simulationName_ = name;
	int loops = 0;
	lastVoice_ = 0;
	for (const PlayRequest& request : simulation_->start()) {
		loops += start(load(request.gamePath, localRoot), request) != 0 ? 1 : 0;
	}
	const auto randoms = std::ranges::count_if(plan.rules, [](const auto& rule) { return rule.kind == xv::sound::SoundscapeRule::Kind::Random; });
	clock_.start();
	setActive(true);
	emit started(tr("Soundscape %1: %2 looping, %3 random").arg(name).arg(loops).arg(randoms));
	if (!plan.problems.empty()) {
		emit failed(QString::fromStdString(plan.problems.front()));
	}
	return true;
}

void SoundPlayer::tick() {
	if (simulation_) {
		const double elapsed = static_cast<double>(clock_.restart()) / 1000.0;
		for (const PlayRequest& request : simulation_->advance(elapsed)) {
			if (start(load(request.gamePath, simulationRoot_), request) != 0) {
				emit started(tr("%1: %2").arg(simulationName_, describe(request)));
			}
		}
		return;
	}
	if (!engine_ || engine_->voiceCount() == 0) {
		setActive(false);
	}
}

std::optional<double> SoundPlayer::position(xv::audio::Engine::Voice voice) {
	return engine_ && voice != 0 ? engine_->position(voice) : std::nullopt;
}

void SoundPlayer::stop() {
	simulation_.reset();
	lastVoice_ = 0;
	if (engine_) {
		engine_->stopAll();
	}
	setActive(false);
}

void SoundPlayer::setVolume(float volume) {
	volume_ = volume;
	if (engine_) {
		engine_->setVolume(volume);
	}
}

void SoundPlayer::setActive(bool active) {
	if (active_ == active) {
		return;
	}
	active_ = active;
	if (active) {
		timer_->start();
	} else {
		timer_->stop();
		emit stopped();
	}
}
