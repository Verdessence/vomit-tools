//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The map's logic as a demo lets it be guessed (E1). Outputs fire on the server and
//          are never recorded; what a demo has is their effects: a door starting to move, a
//          wall appearing or going, a brush shown or hidden. For each such change of a
//          named entity of the map, the outputs in the map's entity lump that send it an
//          input fitting the change are listed, each with when its source would have fired
//          (the change less the output's delay) and what in the demo backs it: a player
//          inside the trigger then, near the button, a round starting, the source itself
//          breaking or moving, or another guessed output reaching the source in turn.
//          They are guesses and say so: logic that leaves no trace (relays, counters) can't
//          be told apart, and a server's Stripper config may have rewired what the lump says.
//
//===========================================================================//
#pragma once

#include <span>
#include <string>
#include <vector>

#include "bsp/Bsp.h"
#include "demo/Contents.h"
#include "demo/Feed.h"
#include "demo/Players.h"
#include "map/Geometry.h"

namespace xv::demo
{

enum class LogicChange_t : std::uint8_t
{
	LOGIC_CHANGE_MOVE,      // started moving or turning
	LOGIC_CHANGE_APPEAR,    // came into the demo
	LOGIC_CHANGE_DISAPPEAR, // went
	LOGIC_CHANGE_SHOW,      // drawn again
	LOGIC_CHANGE_HIDE,      // no longer drawn
};
[[nodiscard]] std::string_view LogicChangeName( LogicChange_t nChange );

enum class LogicTrust_t : std::uint8_t
{
	LOGIC_TRUST_LIKELY,   // the demo shows what would fire it, then
	LOGIC_TRUST_POSSIBLE, // nothing in the demo says either way
	LOGIC_TRUST_UNLIKELY, // the demo shows what would fire it didn't (no one in the trigger)
};

// One output that could have caused a change.
struct LogicCause_t
{
	std::size_t m_nSource = 0; // the output's entity in the lump; SIZE_MAX for a round's restart
	std::string m_szSource;    // "trigger_once boss_start" (its classname and name)
	std::string m_szOutput;    // "OnStartTouch"
	std::string m_szInput;     // "Open"
	std::string m_szParameter;
	float m_flDelay = 0.0f;
	int m_nFired = 0;          // the tick the source would have fired at
	LogicTrust_t m_nTrust = LogicTrust_t::LOGIC_TRUST_POSSIBLE;
	std::string m_szWhy;       // what backs it ("Rohan was inside it"); empty when nothing does
	int m_nPlayer = 0;         // the player who set it off, when the demo shows one
	int m_nSourceEntity = 0;   // the source's entity in the demo, when it is networked
};

// A change of a named map entity in the demo, and what may have caused it.
struct LogicGuess_t
{
	int m_nTick = 0;
	int m_nEntity = 0;         // the demo's entity
	std::size_t m_nMapEntity = 0; // in the lump
	std::string m_szName;      // its targetname
	std::string m_szClass;     // its classname
	LogicChange_t m_nChange = LogicChange_t::LOGIC_CHANGE_MOVE;
	std::vector<LogicCause_t> m_Causes; // the likely first
	[[nodiscard]] bool IsLikely() const { return !m_Causes.empty() && m_Causes.front().m_nTrust == LogicTrust_t::LOGIC_TRUST_LIKELY; }
};

// What the guessing reads from the map.
struct LogicMap_t
{
	std::vector<bsp::Entity_t> m_Entities;
	std::vector<map::Box_t> m_ModelBounds; // the brush models' ("*N"), by number
};

// What the guessing needs of a compiled map, out of its file: the entity lump and the brush
// models' boxes (the models lump). Empty when the file isn't a map.
[[nodiscard]] LogicMap_t ReadLogicMap( std::span<const std::byte> bsp );

// Each change in time order. The demo's entities are tied to the lump's by their brush model
// ("*12": template copies share theirs), else by model and first place.
[[nodiscard]] std::vector<LogicGuess_t> GuessLogic( const CContents &contents, const CPlayers &players, const CFeed &feed, const LogicMap_t &map );

// An input fits a change: Open moves, Kill makes go, Disable hides... (lower case or not).
[[nodiscard]] bool InputFits( std::string_view szInput, LogicChange_t nChange );

} // namespace xv::demo
