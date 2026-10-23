//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What the engine would play for a sound entry or soundscape, with its random choices
//          made. Independent of any audio output, so it can be tested and driven by a clock of the
//          caller's choosing.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "../kv1/Kv1.h"
#include "SoundSchema.h"

namespace xv::sound
{

struct CRange
{
	double m_flLow = 0;
	double m_flHigh = 0;

	[[nodiscard]] double Pick( std::mt19937 &random ) const;
};

// "0.5", "VOL_NORM", "95, 105", "SNDLVL_80dB"... as a range; nullopt if the value doesn't parse.
[[nodiscard]] std::optional<CRange> ParseRange( ValueKind_t nKind, std::string_view szText );

// "sound/<path>" for a wave value, flags stripped and normalized; empty for sentences and voice streams.
[[nodiscard]] std::string WaveGamePath( std::string_view szWave );

struct PlayRequest_t
{
	std::string m_szWave;     // as written, flags included
	std::string m_szGamePath; // what to load
	double m_flVolume = 1;    // 0-1
	double m_flPitch = 100;   // 100 is unchanged
	bool m_bLoop = false;
	double m_flDelaySeconds = 0;
	std::optional<int> m_Position;
	bool m_bRandomPosition = false;
	std::optional<double> m_SoundLevel; // dB; none means heard everywhere
	std::string m_szOrigin;               // where it came from, for display
};

// One play of a block with its own settings: a soundscript entry, playlooping, playrandom, or an
// rndwave (which takes its settings from the enclosing block). nullopt if there is nothing to play.
[[nodiscard]] std::optional<PlayRequest_t> PlayBlock( FileKind_t nKind, const kv1::CNode &block, const kv1::CNode *pParent, std::mt19937 &random );

struct SoundscapeRule_t
{
	enum class Kind_t
	{
		KIND_LOOPING,
		KIND_RANDOM
	};

	Kind_t m_nKind = Kind_t::KIND_LOOPING;
	std::vector<std::string> m_Waves;
	CRange m_Volume{ 1, 1 };
	CRange m_Pitch{ 100, 100 };
	CRange m_Interval{ 10, 10 }; // seconds between random plays
	std::optional<int> m_Position;
	bool m_bRandomPosition = false;
	std::optional<double> m_SoundLevel;
	std::string m_szOrigin;
};

struct SoundscapePlan_t
{
	std::vector<SoundscapeRule_t> m_Rules;
	std::vector<std::string> m_Problems; // missing nested soundscapes, loops, unplayable blocks
	int m_nDsp = 0;
};

using SoundscapeLookup_t = std::function<const kv1::CNode *( std::string_view szName )>;

// Every looping and random sound a soundscape plays, nested playsoundscapes included, with their
// volume scaling and position overrides applied.
[[nodiscard]] SoundscapePlan_t PlanSoundscape( const kv1::CNode &soundscape, const SoundscapeLookup_t &pfnLookup );

// Runs a soundscape plan on a clock: start() returns the loops, Advance() the random sounds that
// came due. Each random sound waits a random time from its interval before playing.
class CSoundscapeSimulation
{
public:
	CSoundscapeSimulation( std::vector<SoundscapeRule_t> rules, std::uint32_t nSeed );

	[[nodiscard]] std::vector<PlayRequest_t> Start();
	[[nodiscard]] std::vector<PlayRequest_t> Advance( double flSeconds );
	[[nodiscard]] double GetNow() const { return m_flNow; }

private:
	PlayRequest_t Request( const SoundscapeRule_t &rule );

	std::vector<SoundscapeRule_t> m_Rules;
	std::vector<double> m_Due;
	double m_flNow = 0;
	std::mt19937 m_Random;
};

} // namespace xv::sound
