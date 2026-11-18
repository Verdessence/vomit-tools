//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Demos' voice chat put in words: speech to text through whisper.cpp, on this
//          machine, from a ggml model file the user has (none ships with the program). Built
//          without whisper (VOMIT_TOOLS_WHISPER off), IsAvailable() is false and nothing loads.
//
//===========================================================================//
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "demo/Contents.h"

namespace xv::demo
{

// Samples a second whisper listens at.
constexpr int WHISPER_RATE = 16000;

// Mono samples from one rate to another (each output sample from a short windowed sinc of the
// input, low-passed below the lower rate's half so nothing folds back).
[[nodiscard]] std::vector<float> Resample( std::span<const float> samples, int nFrom, int nTo );

// A stretch of words, its times in seconds from the clip's start.
struct TranscriptPart_t
{
	double m_flFrom = 0.0;
	double m_flTo = 0.0;
	std::string m_szText;
};

struct Transcript_t
{
	std::string m_szText;     // the parts' words, joined
	std::string m_szLanguage; // what was heard ("en"); empty when the model has one language
	std::vector<TranscriptPart_t> m_Parts;
};

class CTranscriber
{
public:
	// Whether this build has whisper.cpp in.
	[[nodiscard]] static bool IsAvailable();

	// Loads the model (a ggml .bin, such as ggml-base.en.bin); IsOk() says whether it did.
	// szLanguage: "auto" to tell the language (a multilingual model), or a code ("en").
	explicit CTranscriber( const std::filesystem::path &model, std::string szLanguage = "auto" );
	~CTranscriber();
	CTranscriber( const CTranscriber & ) = delete;
	CTranscriber &operator=( const CTranscriber & ) = delete;

	[[nodiscard]] bool IsOk() const;
	[[nodiscard]] const std::string &GetError() const { return m_szError; }
	[[nodiscard]] bool IsMultilingual() const;
	// What it was asked to hear: "auto" or a language's code.
	[[nodiscard]] const std::string &GetLanguage() const { return m_szLanguage; }

	// How many clips can be heard at once: each lane has working memory of its own over the
	// one model, and is used by one thread at a time.
	[[nodiscard]] int GetLanes() const;

	// The language spoken in a clip (its first 30 seconds), as a code ("en"); empty when it
	// can't be told (a model with one language says that one).
	[[nodiscard]] std::string DetectLanguage( std::span<const float> samples, int nRate, int nLane = 0 );

	// One clip in words, in szLanguage (a code; empty: the transcriber's), on a lane. nullopt
	// when it couldn't be (the model isn't loaded, or pCancel was set meanwhile); an empty
	// text when nothing was said.
	[[nodiscard]] std::optional<Transcript_t> Transcribe( std::span<const float> samples, int nRate, const std::atomic<bool> *pCancel = nullptr, const std::string &szLanguage = {}, int nLane = 0 );

private:
	struct Impl_t;
	std::unique_ptr<Impl_t> m_pImpl;
	std::string m_szError;
	std::string m_szLanguage;
};

// The demo's stretches of talking (GetSpeech()'s indices, taken in the order given) put in
// words on every lane at once, pfnDone told of each as it is done (from any of the threads,
// one at a time; not quite in the order given). With a multilingual model asked to tell the
// language, each talker's is told once, from up to half a minute of all they said: a clip of
// a second or two is too short to tell it from. Stops early when pCancel is set.
void TranscribeSpeech( const CContents &contents, CTranscriber &transcriber, std::span<const std::size_t> order, const std::function<void( std::size_t, const Transcript_t & )> &pfnDone, const std::atomic<bool> *pCancel = nullptr );

} // namespace xv::demo
