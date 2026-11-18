//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Logic.h.
//
//===========================================================================//
#include "Logic.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <tuple>

#include "map/Io.h"

namespace xv::demo
{

namespace
{

// A thing that stood still this long and moves again has started moving (seconds).
constexpr double STILL_SECONDS = 1.0;
// Smaller steps than these are the network's rounding, not a move (units, degrees).
constexpr float MOVE_LEAST = 0.5f;
// What is there in the demo's first second was there all along, not made to appear.
constexpr double START_GRACE_SECONDS = 1.0;
// How far either side of when a source would have fired the demo is looked at (seconds):
// snapshots are a few ticks apart, and a trigger fires as a player's move is checked.
constexpr double FIRE_WINDOW_SECONDS = 0.35;
// A round's start this near a logic_auto's firing backs it.
constexpr double ROUND_WINDOW_SECONDS = 1.5;
// How near a player must be to a button to have pressed it (units, beyond its box).
constexpr double USE_REACH = 96.0;
// The player's hull, about its origin (its feet).
constexpr double HULL_HALF = 16.0;
constexpr double HULL_TALL = 72.0;
// How many sources back a chain is followed, and how many causes a change keeps.
constexpr int CHAIN_DEPTH = 3;
constexpr std::size_t CAUSES_KEPT = 12;
// Render mode none: not drawn.
constexpr int RENDER_MODE_NONE = 10;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string Lower( std::string_view szText )
{
	std::string szOut( szText );
	std::ranges::transform( szOut, szOut.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose: "1 2 3" as a point; nullopt when it isn't three numbers.
//-----------------------------------------------------------------------------
std::optional<map::Point_t> ParsePoint( std::string_view szText )
{
	const std::string szCopy( szText );
	double fl[ 3 ] = {};
	const char *p = szCopy.c_str();
	for ( double &flPart : fl )
	{
		char *pEnd = nullptr;
		flPart = std::strtod( p, &pEnd );
		if ( pEnd == p )
			return std::nullopt;
		p = pEnd;
	}
	return map::Point_t{ fl[ 0 ], fl[ 1 ], fl[ 2 ] };
}

// An output of an entity of the lump.
struct Output_t
{
	std::size_t m_nEntity = 0;
	std::string m_szOutput, m_szTarget, m_szInput, m_szParameter; // the target lower-case
	float m_flDelay = 0.0f;
};

// What backs a source having fired.
struct Backing_t
{
	LogicTrust_t m_nTrust = LogicTrust_t::LOGIC_TRUST_POSSIBLE;
	std::string m_szWhy;
	int m_nPlayer = 0;
	int m_nEntity = 0;
};

// One change of a demo entity. A move's end isn't guessed at (nothing is sent to stop a door)
// but backs the outputs fired then (OnFullyOpen).
struct Change_t
{
	int m_nTick = 0;
	LogicChange_t m_nChange = LogicChange_t::LOGIC_CHANGE_MOVE;
	bool m_bStop = false;
};

class CGuesser
{
public:
	CGuesser( const CContents &contents, const CPlayers &players, const CFeed &feed, const LogicMap_t &map );
	std::vector<LogicGuess_t> Run();

private:
	void IndexMap();
	[[nodiscard]] std::optional<std::size_t> MapEntityOf( const EntityLife_t &life ) const;
	[[nodiscard]] std::vector<Change_t> ChangesOf( const EntityLife_t &life ) const;
	[[nodiscard]] std::vector<const Output_t *> OutputsTo( std::size_t nEntity ) const;
	[[nodiscard]] Backing_t Back( std::size_t nSource, const std::string &szOutput, int nFired, int nDepth );
	[[nodiscard]] std::string Describe( std::size_t nEntity ) const;
	[[nodiscard]] std::string PlayerName( int nEntity, int nTick ) const;
	[[nodiscard]] int Ticks( double flSeconds ) const { return static_cast<int>( std::lround( flSeconds / m_flInterval ) ); }

	const CContents &m_Contents;
	const CPlayers &m_Players;
	const CFeed &m_Feed;
	const LogicMap_t &m_Map;
	double m_flInterval = 0.015;
	int m_nStart = 0; // the demo's first tick with entities
	std::vector<Output_t> m_Outputs;
	std::map<std::string, std::vector<std::size_t>> m_ByTarget; // lower-case target -> outputs
	std::vector<std::size_t> m_Wildcards;                       // outputs whose target ends in "*"
	std::map<int, std::size_t> m_ByBrushModel;                  // "*N" -> its entity
	std::map<std::string, std::vector<std::size_t>> m_ByModel;  // lower-case model -> entities
	std::vector<std::vector<Change_t>> m_ChangesByEntity;       // the demo's, of the lives tied to the map
	std::vector<std::vector<std::size_t>> m_LivesOfMapEntity;   // a map entity's lives in the demo
	std::map<std::tuple<std::size_t, std::string, int>, Backing_t> m_Memo;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CGuesser::CGuesser( const CContents &contents, const CPlayers &players, const CFeed &feed, const LogicMap_t &map )
	: m_Contents( contents ),
	  m_Players( players ),
	  m_Feed( feed ),
	  m_Map( map ),
	  m_flInterval( contents.GetTickInterval() > 0.0 ? contents.GetTickInterval() : 0.015 )
{
	IndexMap();
}

//-----------------------------------------------------------------------------
// Purpose: The lump's outputs by their target, and its entities by their models.
//-----------------------------------------------------------------------------
void CGuesser::IndexMap()
{
	for ( std::size_t i = 0; i < m_Map.m_Entities.size(); i++ )
	{
		const bsp::Entity_t &entity = m_Map.m_Entities[ i ];
		for ( const auto &[ szKey, szValue ] : entity.m_KeyValues )
		{
			const bool bOutput = ( szKey.size() > 2 && ( szKey[ 0 ] == 'O' || szKey[ 0 ] == 'o' ) && ( szKey[ 1 ] == 'n' || szKey[ 1 ] == 'N' ) ) || Lower( szKey ).starts_with( "out" );
			if ( !bOutput )
				continue;
			const map::Connection_t connection = map::ParseConnection( szKey, szValue );
			if ( connection.m_bMalformed || connection.m_szTarget.empty() )
				continue;
			Output_t output;
			output.m_nEntity = i;
			output.m_szOutput = szKey;
			output.m_szTarget = Lower( connection.m_szTarget );
			output.m_szInput = connection.m_szInput;
			output.m_szParameter = connection.m_szParameter;
			output.m_flDelay = connection.m_flDelay;
			m_Outputs.push_back( std::move( output ) );
		}
		const std::string_view szModel = entity.Get( "model" );
		if ( szModel.starts_with( '*' ) )
			m_ByBrushModel.emplace( std::atoi( std::string( szModel.substr( 1 ) ).c_str() ), i );
		else if ( !szModel.empty() )
			m_ByModel[ Lower( szModel ) ].push_back( i );
	}
	for ( std::size_t i = 0; i < m_Outputs.size(); i++ )
	{
		const std::string &szTarget = m_Outputs[ i ].m_szTarget;
		if ( szTarget.ends_with( '*' ) )
			m_Wildcards.push_back( i );
		else
			m_ByTarget[ szTarget ].push_back( i );
	}
}

//-----------------------------------------------------------------------------
// Purpose: A brush model names its entity; a studio model, the one of the lump with it that
//          stood nearest where the life began (copies of a template share theirs).
//-----------------------------------------------------------------------------
std::optional<std::size_t> CGuesser::MapEntityOf( const EntityLife_t &life ) const
{
	const int nModel = static_cast<int>( life.ValueAt( ENTITY_FIELD_MODEL, life.m_nFrom )[ 0 ] );
	const std::string_view szModel = m_Contents.ModelName( nModel );
	if ( szModel.starts_with( '*' ) )
	{
		const auto found = m_ByBrushModel.find( std::atoi( std::string( szModel.substr( 1 ) ).c_str() ) );
		return found == m_ByBrushModel.end() ? std::nullopt : std::optional<std::size_t>( found->second );
	}
	const auto found = m_ByModel.find( Lower( szModel ) );
	if ( szModel.empty() || found == m_ByModel.end() )
		return std::nullopt;
	const std::array<float, 3> at = life.ValueAt( ENTITY_FIELD_ORIGIN, life.m_nFrom );
	std::optional<std::size_t> best;
	double flBest = 4.0;
	for ( const std::size_t nEntity : found->second )
	{
		if ( const auto origin = ParsePoint( m_Map.m_Entities[ nEntity ].Get( "origin" ) ) )
		{
			const double flAway = std::abs( ( *origin )[ 0 ] - at[ 0 ] ) + std::abs( ( *origin )[ 1 ] - at[ 1 ] ) + std::abs( ( *origin )[ 2 ] - at[ 2 ] );
			if ( flAway < flBest )
			{
				flBest = flAway;
				best = nEntity;
			}
		}
	}
	return best;
}

//-----------------------------------------------------------------------------
// Purpose: When it started moving (after standing still), came, went, was shown or hidden.
//-----------------------------------------------------------------------------
std::vector<Change_t> CGuesser::ChangesOf( const EntityLife_t &life ) const
{
	std::vector<Change_t> changes;
	const int nGrace = m_nStart + Ticks( START_GRACE_SECONDS );
	if ( life.m_nFrom > nGrace )
		changes.push_back( { life.m_nFrom, LogicChange_t::LOGIC_CHANGE_APPEAR } );
	const std::vector<EntityKey_t> &there = life.m_Fields[ ENTITY_FIELD_THERE ];
	if ( !there.empty() && there.back().m_flValue[ 0 ] == 0.0f )
		changes.push_back( { there.back().m_nTick, LogicChange_t::LOGIC_CHANGE_DISAPPEAR } );

	// Moves: a key that differs from the one before, after the thing stood still. A key
	// holding the old value at the snapshot before (as the reader notes one) marks the start;
	// the last key before it stands still again, the stop.
	std::vector<int> starts;
	std::vector<int> stops;
	for ( const EntityField_t nField : { ENTITY_FIELD_ORIGIN, ENTITY_FIELD_ANGLES } )
	{
		const std::vector<EntityKey_t> &keys = life.m_Fields[ nField ];
		int nLastChange = life.m_nFrom;
		for ( std::size_t i = 1; i < keys.size(); i++ )
		{
			float flStep = 0.0f;
			for ( int n = 0; n < 3; n++ )
				flStep = std::max( flStep, std::abs( nField == ENTITY_FIELD_ANGLES ? std::remainder( keys[ i ].m_flValue[ n ] - keys[ i - 1 ].m_flValue[ n ], 360.0f ) : keys[ i ].m_flValue[ n ] - keys[ i - 1 ].m_flValue[ n ] ) );
			if ( flStep < MOVE_LEAST )
				continue;
			const bool bHold = i >= 2 && keys[ i - 1 ].m_flValue == keys[ i - 2 ].m_flValue;
			const int nStart = bHold ? keys[ i - 1 ].m_nTick : keys[ i ].m_nTick;
			if ( nStart - nLastChange >= Ticks( STILL_SECONDS ) && nStart > nGrace )
			{
				if ( nLastChange > life.m_nFrom )
					stops.push_back( nLastChange );
				starts.push_back( nStart );
			}
			nLastChange = keys[ i ].m_nTick;
		}
		if ( nLastChange > life.m_nFrom && !keys.empty() )
			stops.push_back( nLastChange );
	}
	for ( const int nStop : stops )
		changes.push_back( { nStop, LogicChange_t::LOGIC_CHANGE_MOVE, true } );
	std::ranges::sort( starts );
	for ( std::size_t i = 0; i < starts.size(); i++ )
	{
		// A turn and a move together are one start.
		if ( i == 0 || starts[ i ] - starts[ i - 1 ] > Ticks( STILL_SECONDS / 2.0 ) )
			changes.push_back( { starts[ i ], LogicChange_t::LOGIC_CHANGE_MOVE } );
	}

	// Shown and hidden: no-draw, render mode none, or clear in a mode that blends.
	std::vector<int> ticks;
	for ( const EntityField_t nField : { ENTITY_FIELD_EFFECTS, ENTITY_FIELD_RENDER, ENTITY_FIELD_COLOR } )
	{
		for ( const EntityKey_t &key : life.m_Fields[ nField ] )
			ticks.push_back( key.m_nTick );
	}
	std::ranges::sort( ticks );
	ticks.erase( std::unique( ticks.begin(), ticks.end() ), ticks.end() );
	std::optional<bool> bDrawn;
	for ( const int nTick : ticks )
	{
		const int nEffects = static_cast<int>( life.ValueAt( ENTITY_FIELD_EFFECTS, nTick )[ 0 ] );
		const int nMode = static_cast<int>( life.ValueAt( ENTITY_FIELD_RENDER, nTick )[ 0 ] );
		const float flAlpha = life.m_Fields[ ENTITY_FIELD_COLOR ].empty() ? 255.0f : life.ValueAt( ENTITY_FIELD_COLOR, nTick )[ 1 ];
		const bool bNow = ( nEffects & ENTITY_EFFECT_NO_DRAW ) == 0 && nMode != RENDER_MODE_NONE && !( nMode != 0 && flAlpha == 0.0f );
		if ( bDrawn && *bDrawn != bNow && nTick > nGrace )
			changes.push_back( { nTick, bNow ? LogicChange_t::LOGIC_CHANGE_SHOW : LogicChange_t::LOGIC_CHANGE_HIDE } );
		bDrawn = bNow;
	}
	std::ranges::sort( changes, {}, &Change_t::m_nTick );
	return changes;
}

//-----------------------------------------------------------------------------
// Purpose: The outputs naming an entity: by its name, a wildcard that takes it, or its class.
//-----------------------------------------------------------------------------
std::vector<const Output_t *> CGuesser::OutputsTo( std::size_t nEntity ) const
{
	std::vector<const Output_t *> outputs;
	const bsp::Entity_t &entity = m_Map.m_Entities[ nEntity ];
	const std::string szName = Lower( entity.Get( "targetname" ) );
	for ( const std::string &szKey : { szName, Lower( entity.Get( "classname" ) ) } )
	{
		if ( szKey.empty() )
			continue;
		if ( const auto found = m_ByTarget.find( szKey ); found != m_ByTarget.end() )
		{
			for ( const std::size_t i : found->second )
				outputs.push_back( &m_Outputs[ i ] );
		}
	}
	for ( const std::size_t i : m_Wildcards )
	{
		const std::string &szTarget = m_Outputs[ i ].m_szTarget;
		if ( !szName.empty() && szName.starts_with( std::string_view( szTarget ).substr( 0, szTarget.size() - 1 ) ) )
			outputs.push_back( &m_Outputs[ i ] );
	}
	return outputs;
}

//-----------------------------------------------------------------------------
// Purpose: "trigger_once boss_start", or the class alone.
//-----------------------------------------------------------------------------
std::string CGuesser::Describe( std::size_t nEntity ) const
{
	const bsp::Entity_t &entity = m_Map.m_Entities[ nEntity ];
	const std::string_view szName = entity.Get( "targetname" );
	return std::string( entity.Get( "classname" ) ) + ( szName.empty() ? std::string{} : " " + std::string( szName ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string CGuesser::PlayerName( int nEntity, int nTick ) const
{
	if ( const PlayerTrack_t *pTrack = m_Players.FindTrack( nEntity ) )
	{
		if ( const PlayerIdentity_t *pIdentity = pTrack->IdentityAt( nTick ) )
			return pIdentity->m_szName;
	}
	return "player " + std::to_string( nEntity );
}

//-----------------------------------------------------------------------------
// Purpose: What in the demo says a source fired near a tick: a trigger with a player in it,
//          a button with one by it, a round starting for logic_auto, the source itself
//          breaking or moving, else another output reaching the source, followed back.
//-----------------------------------------------------------------------------
Backing_t CGuesser::Back( std::size_t nSource, const std::string &szOutput, int nFired, int nDepth )
{
	const auto key = std::make_tuple( nSource, Lower( szOutput ), nFired );
	if ( const auto found = m_Memo.find( key ); found != m_Memo.end() )
		return found->second;
	m_Memo[ key ] = {}; // a loop back here backs nothing
	const bsp::Entity_t &source = m_Map.m_Entities[ nSource ];
	const std::string szClass = Lower( source.Get( "classname" ) );
	const std::string szOut = Lower( szOutput );
	const std::string_view szModel = source.Get( "model" );
	Backing_t backing;
	const int nWindow = Ticks( FIRE_WINDOW_SECONDS );

	// The source as the demo has it: going, starting or stopping when the output says.
	for ( const std::size_t nLife : m_LivesOfMapEntity[ nSource ] )
	{
		const EntityLife_t &life = m_Contents.GetLives()[ nLife ];
		if ( nFired < life.m_nFrom - nWindow || ( life.m_nTo >= 0 && nFired > life.m_nTo + nWindow ) )
			continue;
		backing.m_nEntity = life.m_nEntity;
		for ( const Change_t &change : m_ChangesByEntity[ nLife ] )
		{
			if ( std::abs( change.m_nTick - nFired ) > nWindow )
				continue;
			const bool bBreak = change.m_nChange == LogicChange_t::LOGIC_CHANGE_DISAPPEAR && ( szOut == "onbreak" || szOut == "onkilled" || szOut == "ondeath" || szOut.starts_with( "onhealth" ) );
			const bool bStart = change.m_nChange == LogicChange_t::LOGIC_CHANGE_MOVE && !change.m_bStop && ( szOut == "onopen" || szOut == "onclose" || szOut == "onstart" || szOut == "onstartforward" || szOut == "onstartbackward" );
			const bool bStop = change.m_nChange == LogicChange_t::LOGIC_CHANGE_MOVE && change.m_bStop && ( szOut == "onfullyopen" || szOut == "onfullyclosed" || szOut.starts_with( "onreached" ) || szOut == "onarrivedattarget" || szOut == "onpass" );
			if ( bBreak || bStart || bStop )
			{
				backing.m_nTrust = LogicTrust_t::LOGIC_TRUST_LIKELY;
				backing.m_szWhy = bBreak ? "it went then" : bStart ? "it started moving then"
																   : "it stopped moving then";
			}
		}
	}

	std::optional<map::Box_t> bounds;
	if ( szModel.starts_with( '*' ) )
	{
		const std::size_t nModel = static_cast<std::size_t>( std::atoi( std::string( szModel.substr( 1 ) ).c_str() ) );
		if ( nModel < m_Map.m_ModelBounds.size() && !m_Map.m_ModelBounds[ nModel ].IsEmpty() )
		{
			bounds = m_Map.m_ModelBounds[ nModel ];
			if ( const auto origin = ParsePoint( source.Get( "origin" ) ) )
			{
				bounds->m_Mins = map::Add( bounds->m_Mins, *origin );
				bounds->m_Maxs = map::Add( bounds->m_Maxs, *origin );
			}
		}
	}
	const bool bTrigger = szClass.starts_with( "trigger_" ) && ( szOut == "onstarttouch" || szOut == "ontrigger" || szOut == "onendtouch" || szOut == "ontouching" || szOut == "onstarttouchall" || szOut == "onendtouchall" );
	const bool bButton = ( szClass.find( "button" ) != std::string::npos || szClass == "func_door" || szClass == "func_door_rotating" ) && ( szOut == "onpressed" || szOut == "onin" || szOut == "onout" || szOut == "onuselocked" || szOut == "ondamaged" || szOut == "onopen" );
	if ( backing.m_nTrust != LogicTrust_t::LOGIC_TRUST_LIKELY && bounds && ( bTrigger || bButton ) )
	{
		const double flReach = bButton ? USE_REACH : 0.0;
		for ( int nTick = nFired - nWindow; nTick <= nFired + nWindow && backing.m_nTrust != LogicTrust_t::LOGIC_TRUST_LIKELY; nTick += std::max( 1, nWindow / 3 ) )
		{
			for ( const PlayerState_t &player : m_Players.At( static_cast<double>( nTick ) ) )
			{
				if ( !player.m_bAlive || player.m_nTeam < 2 )
					continue;
				const map::Point_t at = map::ToPoint( player.m_vecOrigin );
				const bool bIn = at[ 0 ] + HULL_HALF + flReach >= bounds->m_Mins[ 0 ] && at[ 0 ] - HULL_HALF - flReach <= bounds->m_Maxs[ 0 ] && at[ 1 ] + HULL_HALF + flReach >= bounds->m_Mins[ 1 ] && at[ 1 ] - HULL_HALF - flReach <= bounds->m_Maxs[ 1 ] && at[ 2 ] + HULL_TALL + flReach >= bounds->m_Mins[ 2 ] && at[ 2 ] - flReach <= bounds->m_Maxs[ 2 ];
				if ( bIn )
				{
					backing.m_nTrust = LogicTrust_t::LOGIC_TRUST_LIKELY;
					backing.m_nPlayer = player.m_nEntity;
					backing.m_szWhy = PlayerName( player.m_nEntity, nTick ) + ( bButton ? " was by it" : " was inside it" );
					break;
				}
			}
		}
		if ( backing.m_nTrust != LogicTrust_t::LOGIC_TRUST_LIKELY && bTrigger && !m_Players.IsEmpty() )
		{
			backing.m_nTrust = LogicTrust_t::LOGIC_TRUST_UNLIKELY;
			backing.m_szWhy = "no player was inside it";
		}
	}
	if ( backing.m_nTrust != LogicTrust_t::LOGIC_TRUST_LIKELY && szClass == "logic_auto" )
	{
		for ( const Round_t &round : m_Feed.GetRounds() )
		{
			if ( std::abs( round.m_nStart - nFired ) <= Ticks( ROUND_WINDOW_SECONDS ) && round.m_nStart > 0 )
			{
				backing.m_nTrust = LogicTrust_t::LOGIC_TRUST_LIKELY;
				backing.m_szWhy = "a round started";
			}
		}
	}
	// Else whatever reached the source in turn.
	if ( backing.m_nTrust == LogicTrust_t::LOGIC_TRUST_POSSIBLE && nDepth < CHAIN_DEPTH && !source.Get( "targetname" ).empty() )
	{
		for ( const Output_t *pOutput : OutputsTo( nSource ) )
		{
			const int nEarlier = nFired - Ticks( pOutput->m_flDelay );
			const Backing_t before = Back( pOutput->m_nEntity, pOutput->m_szOutput, nEarlier, nDepth + 1 );
			if ( before.m_nTrust == LogicTrust_t::LOGIC_TRUST_LIKELY )
			{
				backing.m_nTrust = LogicTrust_t::LOGIC_TRUST_LIKELY;
				backing.m_nPlayer = before.m_nPlayer;
				backing.m_szWhy = "sent " + pOutput->m_szInput + " by " + Describe( pOutput->m_nEntity ) + " " + pOutput->m_szOutput + ( before.m_szWhy.empty() ? std::string{} : " (" + before.m_szWhy + ")" );
				break;
			}
		}
	}
	m_Memo[ key ] = backing;
	return backing;
}

//-----------------------------------------------------------------------------
// Purpose: The lives tied to named map entities, their changes, and for each change the
//          outputs that fit it, backed as far as the demo goes.
//-----------------------------------------------------------------------------
std::vector<LogicGuess_t> CGuesser::Run()
{
	const std::vector<EntityLife_t> &lives = m_Contents.GetLives();
	m_nStart = lives.empty() ? 0 : std::ranges::min( lives, {}, &EntityLife_t::m_nFrom ).m_nFrom;
	m_ChangesByEntity.assign( lives.size(), {} );
	m_LivesOfMapEntity.assign( m_Map.m_Entities.size(), {} );
	std::vector<std::optional<std::size_t>> mapOfLife( lives.size() );
	for ( std::size_t i = 0; i < lives.size(); i++ )
	{
		mapOfLife[ i ] = MapEntityOf( lives[ i ] );
		if ( !mapOfLife[ i ] )
			continue;
		m_LivesOfMapEntity[ *mapOfLife[ i ] ].push_back( i );
		m_ChangesByEntity[ i ] = ChangesOf( lives[ i ] );
	}
	std::vector<LogicGuess_t> guesses;
	for ( std::size_t i = 0; i < lives.size(); i++ )
	{
		if ( !mapOfLife[ i ] || m_ChangesByEntity[ i ].empty() )
			continue;
		const std::size_t nMapEntity = *mapOfLife[ i ];
		const bsp::Entity_t &entity = m_Map.m_Entities[ nMapEntity ];
		const std::vector<const Output_t *> outputs = OutputsTo( nMapEntity );
		if ( outputs.empty() )
			continue;
		for ( const Change_t &change : m_ChangesByEntity[ i ] )
		{
			if ( change.m_bStop )
				continue;
			LogicGuess_t guess;
			guess.m_nTick = change.m_nTick;
			guess.m_nEntity = lives[ i ].m_nEntity;
			guess.m_nMapEntity = nMapEntity;
			guess.m_szName = entity.Get( "targetname" );
			guess.m_szClass = entity.Get( "classname" );
			guess.m_nChange = change.m_nChange;
			for ( const Output_t *pOutput : outputs )
			{
				if ( !InputFits( pOutput->m_szInput, change.m_nChange ) )
					continue;
				LogicCause_t cause;
				cause.m_nSource = pOutput->m_nEntity;
				cause.m_szSource = Describe( pOutput->m_nEntity );
				cause.m_szOutput = pOutput->m_szOutput;
				cause.m_szInput = pOutput->m_szInput;
				cause.m_szParameter = pOutput->m_szParameter;
				cause.m_flDelay = pOutput->m_flDelay;
				cause.m_nFired = change.m_nTick - Ticks( pOutput->m_flDelay );
				const Backing_t backing = Back( pOutput->m_nEntity, pOutput->m_szOutput, cause.m_nFired, 1 );
				cause.m_nTrust = backing.m_nTrust;
				cause.m_szWhy = backing.m_szWhy;
				cause.m_nPlayer = backing.m_nPlayer;
				cause.m_nSourceEntity = backing.m_nEntity;
				guess.m_Causes.push_back( std::move( cause ) );
			}
			// A round's start makes the map's entities again: what came or went then needs no
			// output.
			const bool bRestart = ( change.m_nChange == LogicChange_t::LOGIC_CHANGE_APPEAR || change.m_nChange == LogicChange_t::LOGIC_CHANGE_DISAPPEAR ) && std::ranges::any_of( m_Feed.GetRounds(), [ & ]( const Round_t &round ) { return round.m_nStart > 0 && std::abs( round.m_nStart - change.m_nTick ) <= Ticks( ROUND_WINDOW_SECONDS ); } );
			if ( bRestart )
			{
				for ( LogicCause_t &cause : guess.m_Causes )
					cause.m_nTrust = std::max( cause.m_nTrust, LogicTrust_t::LOGIC_TRUST_POSSIBLE );
				LogicCause_t restart;
				restart.m_nSource = SIZE_MAX;
				restart.m_szSource = "the round restarting";
				restart.m_nFired = change.m_nTick;
				restart.m_nTrust = LogicTrust_t::LOGIC_TRUST_LIKELY;
				restart.m_szWhy = "a round started: the map's entities are made again";
				guess.m_Causes.insert( guess.m_Causes.begin(), std::move( restart ) );
			}
			if ( guess.m_Causes.empty() )
				continue;
			std::ranges::stable_sort( guess.m_Causes, []( const LogicCause_t &a, const LogicCause_t &b ) { return std::tie( a.m_nTrust, a.m_flDelay ) < std::tie( b.m_nTrust, b.m_flDelay ); } );
			if ( guess.m_Causes.size() > CAUSES_KEPT )
				guess.m_Causes.resize( CAUSES_KEPT );
			guesses.push_back( std::move( guess ) );
		}
	}
	std::ranges::stable_sort( guesses, {}, &LogicGuess_t::m_nTick );
	return guesses;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view LogicChangeName( LogicChange_t nChange )
{
	switch ( nChange )
	{
		case LogicChange_t::LOGIC_CHANGE_MOVE:
			return "started moving";
		case LogicChange_t::LOGIC_CHANGE_APPEAR:
			return "appeared";
		case LogicChange_t::LOGIC_CHANGE_DISAPPEAR:
			return "went";
		case LogicChange_t::LOGIC_CHANGE_SHOW:
			return "was shown";
		case LogicChange_t::LOGIC_CHANGE_HIDE:
			return "was hidden";
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose: The inputs of the usual entities, by what they do to what is seen.
//-----------------------------------------------------------------------------
bool InputFits( std::string_view szInput, LogicChange_t nChange )
{
	static const std::vector<std::string_view> MOVES = { "open", "close", "toggle", "setposition", "setpositionimmediately", "startforward", "startbackward", "forward", "reverse", "start", "resume", "setspeed", "setspeeddir", "setspeedreal", "setanimation", "teleport", "startpatrol", "setparent", "clearparent", "setparentattachment", "setparentattachmentmaintainoffset", "use", "press", "pressin", "pressout", "unlock", "enablemotion", "wake", "setangles", "setdefaultanimation" };
	static const std::vector<std::string_view> APPEARS = { "forcespawn", "forcespawnatentityorigin", "spawn", "enable", "toggle", "turnon", "show" };
	static const std::vector<std::string_view> GOES = { "kill", "killhierarchy", "break", "disable", "toggle", "turnoff", "hide", "sethealth", "removehealth" };
	static const std::vector<std::string_view> SHOWS = { "enable", "disable", "toggle", "turnon", "turnoff", "show", "hide", "alpha", "setalpha", "color", "enabledraw", "disabledraw" };
	const std::string szLower = Lower( szInput );
	const std::vector<std::string_view> *pList = &MOVES;
	switch ( nChange )
	{
		case LogicChange_t::LOGIC_CHANGE_MOVE:
			pList = &MOVES;
			break;
		case LogicChange_t::LOGIC_CHANGE_APPEAR:
			pList = &APPEARS;
			break;
		case LogicChange_t::LOGIC_CHANGE_DISAPPEAR:
			pList = &GOES;
			break;
		case LogicChange_t::LOGIC_CHANGE_SHOW:
		case LogicChange_t::LOGIC_CHANGE_HIDE:
			pList = &SHOWS;
			break;
	}
	return std::ranges::find( *pList, std::string_view( szLower ) ) != pList->end();
}

//-----------------------------------------------------------------------------
// Purpose: The models lump is 48 bytes a model: its box's two corners first.
//-----------------------------------------------------------------------------
LogicMap_t ReadLogicMap( std::span<const std::byte> bsp )
{
	constexpr int LUMP_MODELS = 14;
	constexpr std::size_t MODEL_BYTES = 48;
	LogicMap_t map;
	const std::optional<bsp::CBsp> file = bsp::CBsp::Parse( bsp );
	if ( !file )
		return map;
	map.m_Entities = file->GetEntities();
	if ( const auto lump = bsp::ReadLump( bsp, file->GetLumps()[ LUMP_MODELS ] ) )
	{
		for ( std::size_t nAt = 0; nAt + MODEL_BYTES <= lump->size(); nAt += MODEL_BYTES )
		{
			float fl[ 6 ];
			std::memcpy( fl, lump->data() + nAt, sizeof( fl ) );
			map::Box_t box;
			box.Add( map::Point_t{ fl[ 0 ], fl[ 1 ], fl[ 2 ] } );
			box.Add( map::Point_t{ fl[ 3 ], fl[ 4 ], fl[ 5 ] } );
			map.m_ModelBounds.push_back( box );
		}
	}
	return map;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<LogicGuess_t> GuessLogic( const CContents &contents, const CPlayers &players, const CFeed &feed, const LogicMap_t &map )
{
	return CGuesser( contents, players, feed, map ).Run();
}

} // namespace xv::demo
