//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Transcribe.h.
//
//===========================================================================//
#include "demo/Transcribe.h"

#include "demo/Voice.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <mutex>
#include <numbers>
#include <thread>

#if defined( VOMIT_TOOLS_HAS_WHISPER )
#include <whisper.h>
#endif

namespace xv::demo
{

namespace
{

// Input samples either side of an output one the resampler weighs.
constexpr int RESAMPLE_TAPS = 16;
// Seconds of a talker's speech its language is told from (whisper's window).
constexpr double LANGUAGE_SECONDS = 30.0;

//-----------------------------------------------------------------------------
// Purpose: A text with the spaces whisper puts before each part taken off its ends.
//-----------------------------------------------------------------------------
std::string Trimmed( std::string_view szText )
{
	while ( !szText.empty() && std::isspace( static_cast<unsigned char>( szText.front() ) ) )
		szText.remove_prefix( 1 );
	while ( !szText.empty() && std::isspace( static_cast<unsigned char>( szText.back() ) ) )
		szText.remove_suffix( 1 );
	return std::string( szText );
}

#if defined( VOMIT_TOOLS_HAS_WHISPER )
// whisper hears nothing in less than this; shorter clips are padded with silence.
constexpr double LEAST_SECONDS = 1.1;
// Most threads one lane takes: past this whisper's CPU decoder gains little.
constexpr unsigned MOST_THREADS = 8;
// Clips put in words at once: a lane for every this many of the machine's threads, up to
// MOST_LANES (each lane's working memory is a few tens of megabytes for the base model, more
// for the larger ones).
constexpr unsigned LANE_THREADS = 4;
constexpr unsigned MOST_LANES = 4;
// A part whisper thinks this likely to be no speech is left out.
constexpr float NO_SPEECH = 0.6f;

//-----------------------------------------------------------------------------
// Purpose: whisper's and ggml's log lines go nowhere: the caller says what went wrong.
//-----------------------------------------------------------------------------
void Quiet( ggml_log_level, const char *, void * )
{
}

//-----------------------------------------------------------------------------
// Purpose: whisper asks between steps whether to stop.
//-----------------------------------------------------------------------------
bool Aborted( void *pUser )
{
	const auto *pCancel = static_cast<const std::atomic<bool> *>( pUser );
	return pCancel && pCancel->load();
}
#endif

} // namespace

//-----------------------------------------------------------------------------
// Purpose: A Lanczos-windowed sinc, its cut-off at the lower rate's half.
//-----------------------------------------------------------------------------
std::vector<float> Resample( std::span<const float> samples, int nFrom, int nTo )
{
	if ( nFrom <= 0 || nTo <= 0 || samples.empty() )
		return {};
	if ( nFrom == nTo )
		return { samples.begin(), samples.end() };
	const double flStep = static_cast<double>( nFrom ) / nTo;
	const double flCut = std::min( 1.0, static_cast<double>( nTo ) / nFrom );
	const auto nOut = static_cast<std::size_t>( std::floor( samples.size() / flStep ) );
	std::vector<float> out( nOut );
	const int nTaps = static_cast<int>( std::ceil( RESAMPLE_TAPS / flCut ) );
	for ( std::size_t i = 0; i < nOut; i++ )
	{
		const double flAt = i * flStep;
		const auto nMid = static_cast<long long>( std::floor( flAt ) );
		double flSum = 0.0;
		double flWeights = 0.0;
		for ( long long n = nMid - nTaps + 1; n <= nMid + nTaps; n++ )
		{
			if ( n < 0 || n >= static_cast<long long>( samples.size() ) )
				continue;
			const double x = ( flAt - static_cast<double>( n ) ) * flCut;
			const double flWindow = std::abs( x ) < RESAMPLE_TAPS ? ( x == 0.0 ? 1.0 : RESAMPLE_TAPS * std::sin( std::numbers::pi * x ) * std::sin( std::numbers::pi * x / RESAMPLE_TAPS ) / ( std::numbers::pi * std::numbers::pi * x * x ) ) : 0.0;
			flSum += samples[ static_cast<std::size_t>( n ) ] * flWindow;
			flWeights += flWindow;
		}
		out[ i ] = flWeights != 0.0 ? static_cast<float>( flSum / flWeights ) : 0.0f;
	}
	return out;
}

#if defined( VOMIT_TOOLS_HAS_WHISPER )
struct CTranscriber::Impl_t
{
	whisper_context *m_pContext = nullptr;
	std::vector<whisper_state *> m_States; // a lane's each
	int m_nThreads = 1;                    // a lane's
};
#else
struct CTranscriber::Impl_t
{
};
#endif

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CTranscriber::IsAvailable()
{
#if defined( VOMIT_TOOLS_HAS_WHISPER )
	return true;
#else
	return false;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: The model read now, on the CPU, and a state for each lane: the model is shared,
//          each lane's working memory its own.
//-----------------------------------------------------------------------------
CTranscriber::CTranscriber( const std::filesystem::path &model, std::string szLanguage )
	: m_pImpl( std::make_unique<Impl_t>() ),
	  m_szLanguage( std::move( szLanguage ) )
{
#if defined( VOMIT_TOOLS_HAS_WHISPER )
	whisper_log_set( Quiet, nullptr );
	std::error_code error;
	if ( !std::filesystem::is_regular_file( model, error ) )
	{
		m_szError = "the model file isn't there: " + model.string();
		return;
	}
	whisper_context_params params = whisper_context_default_params();
	params.use_gpu = false;
	m_pImpl->m_pContext = whisper_init_from_file_with_params_no_state( model.string().c_str(), params );
	if ( !m_pImpl->m_pContext )
	{
		m_szError = "not a whisper model (a ggml .bin): " + model.string();
		return;
	}
	const unsigned nHardware = std::max( 1u, std::thread::hardware_concurrency() );
	const unsigned nLanes = std::clamp( nHardware / LANE_THREADS, 1u, MOST_LANES );
	m_pImpl->m_nThreads = static_cast<int>( std::clamp( nHardware / nLanes, 1u, MOST_THREADS ) );
	for ( unsigned i = 0; i < nLanes; i++ )
	{
		whisper_state *pState = whisper_init_state( m_pImpl->m_pContext );
		if ( !pState )
			break;
		m_pImpl->m_States.push_back( pState );
	}
	if ( m_pImpl->m_States.empty() )
	{
		whisper_free( m_pImpl->m_pContext );
		m_pImpl->m_pContext = nullptr;
		m_szError = "not enough memory for the model's working state";
	}
#else
	(void)model;
	m_szError = "this build has no speech to text (built without whisper.cpp)";
#endif
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CTranscriber::~CTranscriber()
{
#if defined( VOMIT_TOOLS_HAS_WHISPER )
	for ( whisper_state *pState : m_pImpl->m_States )
		whisper_free_state( pState );
	if ( m_pImpl->m_pContext )
		whisper_free( m_pImpl->m_pContext );
#endif
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CTranscriber::IsOk() const
{
#if defined( VOMIT_TOOLS_HAS_WHISPER )
	return m_pImpl->m_pContext != nullptr && !m_pImpl->m_States.empty();
#else
	return false;
#endif
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CTranscriber::IsMultilingual() const
{
#if defined( VOMIT_TOOLS_HAS_WHISPER )
	return m_pImpl->m_pContext && whisper_is_multilingual( m_pImpl->m_pContext ) != 0;
#else
	return false;
#endif
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CTranscriber::GetLanes() const
{
#if defined( VOMIT_TOOLS_HAS_WHISPER )
	return static_cast<int>( m_pImpl->m_States.size() );
#else
	return 0;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: The clip's spectrum, then whisper's guess over its languages.
//-----------------------------------------------------------------------------
std::string CTranscriber::DetectLanguage( std::span<const float> samples, int nRate, int nLane )
{
#if defined( VOMIT_TOOLS_HAS_WHISPER )
	if ( !IsOk() || nLane < 0 || nLane >= GetLanes() )
		return {};
	if ( !IsMultilingual() )
		return "en";
	whisper_state *pState = m_pImpl->m_States[ static_cast<std::size_t>( nLane ) ];
	std::vector<float> heard = Resample( samples, nRate, WHISPER_RATE );
	heard.resize( std::max( heard.size(), static_cast<std::size_t>( LEAST_SECONDS * WHISPER_RATE ) ), 0.0f );
	if ( whisper_pcm_to_mel_with_state( m_pImpl->m_pContext, pState, heard.data(), static_cast<int>( heard.size() ), m_pImpl->m_nThreads ) != 0 )
		return {};
	std::vector<float> probabilities( static_cast<std::size_t>( whisper_lang_max_id() + 1 ), 0.0f );
	const int nId = whisper_lang_auto_detect_with_state( m_pImpl->m_pContext, pState, 0, m_pImpl->m_nThreads, probabilities.data() );
	const char *pszLanguage = nId >= 0 ? whisper_lang_str( nId ) : nullptr;
	return pszLanguage ? pszLanguage : std::string{};
#else
	(void)samples;
	(void)nRate;
	(void)nLane;
	return {};
#endif
}

//-----------------------------------------------------------------------------
// Purpose: The clip at whisper's rate, greedy decoding with nothing carried over from an
//          earlier clip (each talker's stretch stands alone), its parts and their times.
//-----------------------------------------------------------------------------
std::optional<Transcript_t> CTranscriber::Transcribe( std::span<const float> samples, int nRate, const std::atomic<bool> *pCancel, const std::string &szLanguage, int nLane )
{
#if defined( VOMIT_TOOLS_HAS_WHISPER )
	if ( !IsOk() || nLane < 0 || nLane >= GetLanes() )
		return std::nullopt;
	whisper_state *pState = m_pImpl->m_States[ static_cast<std::size_t>( nLane ) ];
	std::vector<float> heard = Resample( samples, nRate, WHISPER_RATE );
	if ( heard.empty() )
		return Transcript_t{};
	heard.resize( std::max( heard.size(), static_cast<std::size_t>( LEAST_SECONDS * WHISPER_RATE ) ), 0.0f );
	whisper_full_params params = whisper_full_default_params( WHISPER_SAMPLING_GREEDY );
	params.n_threads = m_pImpl->m_nThreads;
	params.no_context = true;
	params.print_progress = false;
	params.print_realtime = false;
	params.print_timestamps = false;
	params.print_special = false;
	const std::string &szAsked = szLanguage.empty() ? m_szLanguage : szLanguage;
	const bool bAuto = szAsked.empty() || szAsked == "auto";
	params.language = !IsMultilingual() ? "en" : bAuto ? "auto"
													   : szAsked.c_str();
	params.detect_language = false;
	params.abort_callback = Aborted;
	params.abort_callback_user_data = const_cast<std::atomic<bool> *>( pCancel );
	if ( whisper_full_with_state( m_pImpl->m_pContext, pState, params, heard.data(), static_cast<int>( heard.size() ) ) != 0 || ( pCancel && pCancel->load() ) )
		return std::nullopt;
	Transcript_t transcript;
	if ( IsMultilingual() )
	{
		if ( const char *pszLanguage = whisper_lang_str( whisper_full_lang_id_from_state( pState ) ) )
			transcript.m_szLanguage = pszLanguage;
	}
	const int nParts = whisper_full_n_segments_from_state( pState );
	for ( int i = 0; i < nParts; i++ )
	{
		TranscriptPart_t part;
		// whisper's times are in hundredths of a second.
		part.m_flFrom = whisper_full_get_segment_t0_from_state( pState, i ) / 100.0;
		part.m_flTo = whisper_full_get_segment_t1_from_state( pState, i ) / 100.0;
		part.m_szText = Trimmed( whisper_full_get_segment_text_from_state( pState, i ) );
		// What whisper says of a stretch without words ("[BLANK_AUDIO]", "(music)"), and what
		// it is unsure anyone said, isn't speech.
		const bool bTag = part.m_szText.size() >= 2 && ( ( part.m_szText.front() == '[' && part.m_szText.back() == ']' ) || ( part.m_szText.front() == '(' && part.m_szText.back() == ')' ) || ( part.m_szText.front() == '*' && part.m_szText.back() == '*' ) );
		if ( part.m_szText.empty() || bTag || whisper_full_get_segment_no_speech_prob_from_state( pState, i ) > NO_SPEECH )
			continue;
		transcript.m_szText += ( transcript.m_szText.empty() ? "" : " " ) + part.m_szText;
		transcript.m_Parts.push_back( std::move( part ) );
	}
	return transcript;
#else
	(void)samples;
	(void)nRate;
	(void)pCancel;
	(void)szLanguage;
	(void)nLane;
	return std::nullopt;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Each clip decoded and put in words on its own: whisper hears half a minute at once
//          whatever it is given, so clips heard together would be quicker, but it runs words
//          on either side of a gap together and they can't be told apart again. Instead the
//          lanes take clips in turn, each on a thread of its own. A talker's language is told
//          the first time one of its clips comes up.
//-----------------------------------------------------------------------------
void TranscribeSpeech( const CContents &contents, CTranscriber &transcriber, std::span<const std::size_t> order, const std::function<void( std::size_t, const Transcript_t & )> &pfnDone, const std::atomic<bool> *pCancel )
{
	const std::vector<Speech_t> &speech = contents.GetSpeech();
	const bool bTell = transcriber.IsMultilingual() && ( transcriber.GetLanguage().empty() || transcriber.GetLanguage() == "auto" );
	std::mutex mutex; // the languages, and pfnDone
	std::map<int, std::string> languages; // by talker
	std::atomic<std::size_t> nNext{ 0 };
	std::atomic<bool> bFailed{ false };
	const auto languageOf = [ & ]( int nEntity, int nLane ) -> std::string {
		if ( !bTell )
			return {};
		const std::lock_guard lock( mutex );
		if ( const auto found = languages.find( nEntity ); found != languages.end() )
			return found->second;
		// The talker's clips, longest first, up to whisper's window.
		std::vector<std::size_t> theirs;
		for ( std::size_t i = 0; i < speech.size(); i++ )
		{
			if ( speech[ i ].m_nEntity == nEntity )
				theirs.push_back( i );
		}
		std::ranges::sort( theirs, std::greater<>{}, [ & ]( std::size_t i ) { return speech[ i ].m_nTo - speech[ i ].m_nFrom; } );
		std::vector<float> heard;
		int nRate = 0;
		for ( const std::size_t i : theirs )
		{
			if ( nRate > 0 && heard.size() >= static_cast<std::size_t>( LANGUAGE_SECONDS * nRate ) )
				break;
			int nClipRate = 0;
			const std::vector<float> samples = DecodeSpeech( contents, speech[ i ], &nClipRate );
			if ( samples.empty() || ( nRate > 0 && nClipRate != nRate ) )
				continue;
			nRate = nClipRate;
			heard.insert( heard.end(), samples.begin(), samples.end() );
		}
		std::string szLanguage = heard.empty() ? std::string{} : transcriber.DetectLanguage( heard, nRate, nLane );
		languages[ nEntity ] = szLanguage;
		return szLanguage;
	};
	const auto lane = [ & ]( int nLane ) {
		while ( !bFailed && !( pCancel && pCancel->load() ) )
		{
			const std::size_t nAt = nNext++;
			if ( nAt >= order.size() )
				return;
			const std::size_t i = order[ nAt ];
			if ( i >= speech.size() )
				continue;
			int nRate = 0;
			const std::vector<float> samples = DecodeSpeech( contents, speech[ i ], &nRate );
			Transcript_t transcript;
			if ( !samples.empty() )
			{
				const std::string szLanguage = languageOf( speech[ i ].m_nEntity, nLane );
				auto said = transcriber.Transcribe( samples, nRate, pCancel, szLanguage, nLane );
				if ( !said )
				{
					bFailed = true;
					return;
				}
				transcript = std::move( *said );
				if ( transcript.m_szLanguage.empty() )
					transcript.m_szLanguage = szLanguage;
			}
			const std::lock_guard lock( mutex );
			pfnDone( i, transcript );
		}
	};
	std::vector<std::thread> others;
	for ( int nLane = 1; nLane < transcriber.GetLanes(); nLane++ )
		others.emplace_back( lane, nLane );
	lane( 0 );
	for ( std::thread &other : others )
		other.join();
}

} // namespace xv::demo
