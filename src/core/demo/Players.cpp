//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Players.h.
//
//===========================================================================//
#include "Players.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>

namespace xv::demo
{

namespace
{

// What is kept of a player entity (CNetworkState's slots).
enum PlayerSlot_t
{
	PLAYER_SLOT_ORIGIN,
	PLAYER_SLOT_ANGLES,
	PLAYER_SLOT_TEAM,
	PLAYER_SLOT_LIFE,
	PLAYER_SLOT_HEALTH,
	PLAYER_SLOT_FLAGS,
	PLAYER_SLOT_COUNT,
};

// m_fFlags' bit for a player that is ducking.
constexpr int PLAYER_FLAG_DUCKING = 1 << 1;
// The userinfo table's record for a player: the name, the user id, the Steam id, then (among
// more) whether it is a bot and whether it is SourceTV.
constexpr std::size_t USER_INFO_NAME = 0;
constexpr std::size_t USER_INFO_NAME_SIZE = 32;
constexpr std::size_t USER_INFO_USER_ID = 32;
constexpr std::size_t USER_INFO_STEAM_ID = 36;
constexpr std::size_t USER_INFO_STEAM_ID_SIZE = 33;
constexpr std::size_t USER_INFO_ACCOUNT = 72;
// The four custom files' CRCs (the first is the spray), after the two flags, four-aligned.
constexpr std::size_t USER_INFO_LOGO = 112;
constexpr std::size_t USER_INFO_BOT = 108;
constexpr std::size_t USER_INFO_SOURCE_TV = 109;
// Players aren't numbered past this when the demo doesn't say how many there can be.
constexpr int FALLBACK_MAX_CLIENTS = 64;
// An individual account in Steam's public universe is this plus its account number.
constexpr std::uint64_t STEAM_ID64_BASE = 76561197960265728ULL;
constexpr double FALLBACK_TICK_INTERVAL = 0.015;
// Nothing moves faster than this (the engine's own limit on a velocity's parts, units a
// second): a player further along than that between two snapshots was put there.
constexpr double FASTEST_SPEED = 3500.0;
constexpr double TELEPORT_SLACK = 16.0;
constexpr int PROGRESS_EVERY = 256;

//-----------------------------------------------------------------------------
// Purpose: Which of a class's properties are kept: a player class's place, angles, team, life
//          state, health and flags. A player's own demo has two origins for a player (the one
//          only the player gets, finer, and the one the others get): both go to the same slot,
//          and whichever was sent last stands. Games that send the height apart from the rest
//          ("m_vecOrigin[2]") fill the slot's third number with it.
//-----------------------------------------------------------------------------
std::vector<int> WatchPlayers( const ServerClass_t &, std::span<const FlatProp_t> props )
{
	if ( std::ranges::none_of( props, []( const FlatProp_t &prop ) { return prop.m_szTable == "DT_BasePlayer"; } ) )
	{
		return {};
	}
	std::vector<int> watch( props.size(), -1 );
	for ( std::size_t i = 0; i < props.size(); i++ )
	{
		const FlatProp_t &prop = props[ i ];
		const std::string &szName = prop.m_szName;
		const bool bVector = prop.m_nKind == PropKind_t::PROP_KIND_VECTOR || prop.m_nKind == PropKind_t::PROP_KIND_VECTOR_XY;
		if ( szName == "m_vecOrigin" && bVector )
		{
			watch[ i ] = PLAYER_SLOT_ORIGIN * 4;
		}
		else if ( szName == "m_vecOrigin[2]" && prop.m_nKind == PropKind_t::PROP_KIND_FLOAT )
		{
			watch[ i ] = PLAYER_SLOT_ORIGIN * 4 + 2;
		}
		else if ( szName == "m_angEyeAngles[0]" || ( szName == "m_angEyeAngles" && bVector ) )
		{
			watch[ i ] = PLAYER_SLOT_ANGLES * 4;
		}
		else if ( szName == "m_angEyeAngles[1]" )
		{
			watch[ i ] = PLAYER_SLOT_ANGLES * 4 + 1;
		}
		else if ( szName == "m_iTeamNum" )
		{
			watch[ i ] = PLAYER_SLOT_TEAM * 4;
		}
		else if ( szName == "m_lifeState" )
		{
			watch[ i ] = PLAYER_SLOT_LIFE * 4;
		}
		else if ( szName == "m_iHealth" )
		{
			watch[ i ] = PLAYER_SLOT_HEALTH * 4;
		}
		else if ( szName == "m_fFlags" )
		{
			watch[ i ] = PLAYER_SLOT_FLAGS * 4;
		}
	}
	return watch;
}

//-----------------------------------------------------------------------------
// Purpose: A fixed text field of a userinfo record, up to its first zero.
//-----------------------------------------------------------------------------
std::string ReadField( const std::vector<std::byte> &data, std::size_t nAt, std::size_t nSize )
{
	if ( data.size() < nAt + nSize )
	{
		return {};
	}
	const char *pszText = reinterpret_cast<const char *>( data.data() + nAt );
	return std::string( pszText, strnlen( pszText, nSize ) );
}

//-----------------------------------------------------------------------------
// Purpose: Angles a to b by the short way round.
//-----------------------------------------------------------------------------
float LerpAngle( float flFrom, float flTo, float flPart )
{
	const float flTurn = std::remainder( flTo - flFrom, 360.0f );
	return flFrom + flTurn * flPart;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The account number in "STEAM_X:Y:Z" (Z * 2 + Y) or "[U:1:N]" (N); none for any
//          other text ("BOT", "STEAM_ID_PENDING", "STEAM_ID_LAN").
//-----------------------------------------------------------------------------
std::optional<std::uint64_t> SteamId64( const PlayerIdentity_t &identity )
{
	if ( identity.m_bBot || identity.m_bSourceTv )
	{
		return std::nullopt;
	}
	const std::string &szId = identity.m_szSteamId;
	const auto number = []( std::string_view szText ) -> std::optional<std::uint64_t> {
		std::uint64_t n = 0;
		const auto [ pEnd, ec ] = std::from_chars( szText.data(), szText.data() + szText.size(), n );
		if ( ec != std::errc{} || pEnd != szText.data() + szText.size() )
		{
			return std::nullopt;
		}
		return n;
	};
	std::optional<std::uint64_t> nAccount;
	if ( szId.starts_with( "STEAM_" ) )
	{
		const std::size_t nFirst = szId.find( ':' );
		const std::size_t nSecond = nFirst == std::string::npos ? std::string::npos : szId.find( ':', nFirst + 1 );
		if ( nSecond != std::string::npos )
		{
			const auto nLow = number( std::string_view( szId ).substr( nFirst + 1, nSecond - nFirst - 1 ) );
			const auto nHigh = number( std::string_view( szId ).substr( nSecond + 1 ) );
			if ( nLow && nHigh && *nLow <= 1 )
			{
				nAccount = *nHigh * 2 + *nLow;
			}
		}
	}
	else if ( szId.starts_with( "[U:1:" ) && szId.ends_with( ']' ) )
	{
		nAccount = number( std::string_view( szId ).substr( 5, szId.size() - 6 ) );
	}
	if ( !nAccount && identity.m_nAccount != 0 )
	{
		nAccount = identity.m_nAccount;
	}
	if ( !nAccount || *nAccount == 0 || *nAccount > 0xffffffffULL )
	{
		return std::nullopt;
	}
	return STEAM_ID64_BASE + *nAccount;
}

//-----------------------------------------------------------------------------
// Purpose: Universe 0, as Source games write it.
//-----------------------------------------------------------------------------
std::string SteamId2( std::uint64_t nSteamId64 )
{
	const std::uint64_t nAccount = nSteamId64 - STEAM_ID64_BASE;
	return "STEAM_0:" + std::to_string( nAccount & 1 ) + ":" + std::to_string( nAccount >> 1 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string SteamId3( std::uint64_t nSteamId64 )
{
	return "[U:1:" + std::to_string( nSteamId64 - STEAM_ID64_BASE ) + "]";
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string SteamProfileUrl( std::uint64_t nSteamId64 )
{
	return "https://steamcommunity.com/profiles/" + std::to_string( nSteamId64 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string TeamName( std::string_view szGameDir, int nTeam )
{
	if ( nTeam == 0 )
	{
		return "Unassigned";
	}
	if ( nTeam == 1 )
	{
		return "Spectators";
	}
	if ( szGameDir == "cstrike" )
	{
		if ( nTeam == 2 )
		{
			return "Terrorists";
		}
		if ( nTeam == 3 )
		{
			return "Counter-Terrorists";
		}
	}
	if ( szGameDir == "tf" )
	{
		if ( nTeam == 2 )
		{
			return "RED";
		}
		if ( nTeam == 3 )
		{
			return "BLU";
		}
	}
	return "Team " + std::to_string( nTeam );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const PlayerIdentity_t *PlayerTrack_t::IdentityAt( int nTick ) const
{
	const auto after = std::upper_bound( m_Identities.begin(), m_Identities.end(), nTick, []( int nAt, const PlayerIdentity_t &identity ) { return nAt < identity.m_nTick; } );
	if ( after == m_Identities.begin() )
	{
		return nullptr;
	}
	const PlayerIdentity_t &identity = *( after - 1 );
	return identity.m_szName.empty() ? nullptr : &identity;
}

//-----------------------------------------------------------------------------
// Purpose: Every frame in order: the tables once, then each packet's messages. After a packet
//          that changed the entities, each player slot's state is noted if it differs from
//          the last noted (with the last one noted again at the tick before, when it had
//          stood for a while: the move starts there, not back when it was first seen); the
//          names follow the userinfo table whenever that changes.
//-----------------------------------------------------------------------------
CPlayers CPlayers::Read( const CDemo &demo, std::span<const std::byte> file, const std::function<bool( double )> &pfnProgress )
{
	CPlayers players;
	players.m_flTickInterval = demo.GetTickInterval();
	CNetworkState state( WatchPlayers, PLAYER_SLOT_COUNT );
	std::vector<PlayerTrack_t> tracks;
	std::vector<int> lastSeen;
	std::uint64_t nSnapshots = 0;
	std::uint64_t nNames = 0;
	const std::vector<Frame_t> &frames = demo.GetFrames();

	const auto noteNames = [ & ]( int nTick ) {
		const StringTable_t *pTable = state.FindTable( "userinfo" );
		if ( !pTable || pTable->m_nRevision == nNames )
		{
			return;
		}
		nNames = pTable->m_nRevision;
		for ( std::size_t i = 0; i < pTable->m_Entries.size(); i++ )
		{
			const std::vector<std::byte> &data = pTable->m_Entries[ i ].m_Data;
			PlayerIdentity_t identity;
			identity.m_nTick = nTick;
			identity.m_szName = ReadField( data, USER_INFO_NAME, USER_INFO_NAME_SIZE );
			identity.m_szSteamId = ReadField( data, USER_INFO_STEAM_ID, USER_INFO_STEAM_ID_SIZE );
			if ( data.size() > USER_INFO_SOURCE_TV )
			{
				std::int32_t nUserId = 0;
				std::memcpy( &nUserId, data.data() + USER_INFO_USER_ID, sizeof( nUserId ) );
				identity.m_nUserId = nUserId;
				std::memcpy( &identity.m_nAccount, data.data() + USER_INFO_ACCOUNT, sizeof( identity.m_nAccount ) );
				if ( data.size() >= USER_INFO_LOGO + 4 )
					std::memcpy( &identity.m_nLogo, data.data() + USER_INFO_LOGO, sizeof( identity.m_nLogo ) );
				identity.m_bBot = data[ USER_INFO_BOT ] != std::byte{ 0 } || identity.m_szSteamId == "BOT";
				identity.m_bSourceTv = data[ USER_INFO_SOURCE_TV ] != std::byte{ 0 };
			}
			if ( tracks.size() <= i + 1 )
			{
				if ( identity.m_szName.empty() )
				{
					continue;
				}
				tracks.resize( i + 2 );
				lastSeen.resize( i + 2, 0 );
			}
			std::vector<PlayerIdentity_t> &identities = tracks[ i + 1 ].m_Identities;
			if ( identities.empty() ? !identity.m_szName.empty() : !identities.back().IsSamePlayer( identity ) )
			{
				identities.push_back( std::move( identity ) );
			}
		}
	};

	const auto noteSnapshot = [ & ]( int nTick ) {
		const std::span<const Entity_t> entities = state.GetEntities();
		const int nMaxClients = state.GetServerInfo() && state.GetServerInfo()->m_nMaxClients > 0 ? state.GetServerInfo()->m_nMaxClients : FALLBACK_MAX_CLIENTS;
		for ( int nEntity = 1; nEntity <= nMaxClients && static_cast<std::size_t>( nEntity ) < entities.size(); nEntity++ )
		{
			const Entity_t &entity = entities[ static_cast<std::size_t>( nEntity ) ];
			const bool bThere = entity.m_nClass >= 0 && entity.m_Values.size() == PLAYER_SLOT_COUNT;
			const std::size_t n = static_cast<std::size_t>( nEntity );
			if ( tracks.size() <= n )
			{
				if ( !bThere )
				{
					continue;
				}
				tracks.resize( n + 1 );
				lastSeen.resize( n + 1, 0 );
			}
			std::vector<PlayerSample_t> &samples = tracks[ n ].m_Samples;
			if ( !bThere )
			{
				if ( !samples.empty() && samples.back().m_bThere )
				{
					// Gone the tick it was noted (a player's own demo can have both in one
					// tick): the later word stands.
					if ( samples.back().m_nTick >= nTick )
					{
						samples.back().m_bThere = false;
					}
					else
					{
						PlayerSample_t gone = samples.back();
						gone.m_nTick = nTick;
						gone.m_bThere = false;
						samples.push_back( gone );
					}
				}
				continue;
			}
			PlayerSample_t sample;
			sample.m_nTick = nTick;
			const PropValue_t &origin = entity.m_Values[ PLAYER_SLOT_ORIGIN ];
			sample.m_vecOrigin = { static_cast<float>( origin[ 0 ] ), static_cast<float>( origin[ 1 ] ), static_cast<float>( origin[ 2 ] ) };
			// A pitch can come as 351 for 9 up.
			sample.m_flPitch = static_cast<float>( std::remainder( entity.m_Values[ PLAYER_SLOT_ANGLES ][ 0 ], 360.0 ) );
			sample.m_flYaw = static_cast<float>( entity.m_Values[ PLAYER_SLOT_ANGLES ][ 1 ] );
			sample.m_nHealth = static_cast<std::int16_t>( std::clamp( entity.m_Values[ PLAYER_SLOT_HEALTH ][ 0 ], -32768.0, 32767.0 ) );
			sample.m_nTeam = static_cast<std::uint8_t>( std::clamp( entity.m_Values[ PLAYER_SLOT_TEAM ][ 0 ], 0.0, 255.0 ) );
			sample.m_bAlive = entity.m_Values[ PLAYER_SLOT_LIFE ][ 0 ] == 0.0;
			sample.m_bDucked = ( static_cast<int>( entity.m_Values[ PLAYER_SLOT_FLAGS ][ 0 ] ) & PLAYER_FLAG_DUCKING ) != 0;
			if ( samples.empty() || !samples.back().IsSameState( sample ) )
			{
				if ( !samples.empty() && samples.back().m_bThere && lastSeen[ n ] > samples.back().m_nTick && lastSeen[ n ] < nTick )
				{
					PlayerSample_t held = samples.back();
					held.m_nTick = lastSeen[ n ];
					samples.push_back( held );
				}
				// A tick noted twice (a packet split in two) keeps the later word.
				if ( !samples.empty() && samples.back().m_nTick >= nTick )
				{
					samples.back() = sample;
				}
				else
				{
					samples.push_back( sample );
				}
			}
			lastSeen[ n ] = nTick;
		}
	};

	int nLastTick = 0;
	for ( std::size_t i = 0; i < frames.size(); i++ )
	{
		const Frame_t &frame = frames[ i ];
		if ( pfnProgress && i % PROGRESS_EVERY == 0 && !pfnProgress( static_cast<double>( i ) / static_cast<double>( frames.size() ) ) )
		{
			players.m_szWarning = "Reading the players was stopped.";
			break;
		}
		bool bRead = true;
		// Ticks before the game began aren't the demo's clock.
		const int nTick = frame.m_nKind == FrameKind_t::FRAME_KIND_PACKET ? std::max( frame.m_nTick, nLastTick ) : nLastTick;
		switch ( frame.m_nKind )
		{
			case FrameKind_t::FRAME_KIND_SIGNON:
			case FrameKind_t::FRAME_KIND_PACKET:
				bRead = state.ReadPacket( CDemo::FrameBlock( frame, file ) );
				break;
			case FrameKind_t::FRAME_KIND_DATA_TABLES:
				bRead = state.ReadDataTables( CDemo::FrameBlock( frame, file ) );
				break;
			case FrameKind_t::FRAME_KIND_STRING_TABLES:
				bRead = state.ReadStringTables( CDemo::FrameBlock( frame, file ) );
				break;
			default:
				break;
		}
		if ( !bRead )
		{
			players.m_szWarning = "The players aren't followed past tick " + std::to_string( nLastTick ) + ": " + state.GetError() + ".";
			break;
		}
		nLastTick = nTick;
		noteNames( nTick );
		if ( state.GetSnapshotCount() != nSnapshots )
		{
			nSnapshots = state.GetSnapshotCount();
			noteSnapshot( nTick );
		}
	}

	players.m_ServerInfo = state.GetServerInfo();
	if ( players.m_ServerInfo && players.m_ServerInfo->m_flTickInterval > 0.0f )
	{
		players.m_flTickInterval = players.m_ServerInfo->m_flTickInterval;
	}
	players.m_nSnapshots = static_cast<int>( nSnapshots );
	for ( std::size_t n = 0; n < tracks.size(); n++ )
	{
		if ( !tracks[ n ].m_Samples.empty() )
		{
			tracks[ n ].m_nEntity = static_cast<int>( n );
			tracks[ n ].m_Samples.shrink_to_fit();
			players.m_Tracks.push_back( std::move( tracks[ n ] ) );
		}
	}
	return players;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const PlayerTrack_t *CPlayers::FindTrack( int nEntity ) const
{
	const auto found = std::ranges::find( m_Tracks, nEntity, &PlayerTrack_t::m_nEntity );
	return found == m_Tracks.end() ? nullptr : &*found;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CPlayers::GetRecorderEntity() const
{
	return m_ServerInfo && !m_ServerInfo->m_bSourceTv ? m_ServerInfo->m_nPlayerSlot + 1 : 0;
}

//-----------------------------------------------------------------------------
// Purpose: The sample at or before the tick, moved towards the next by how far between them
//          the tick is. What isn't a place or an angle is the earlier sample's.
//-----------------------------------------------------------------------------
std::optional<PlayerState_t> CPlayers::StateOf( const PlayerTrack_t &track, double flTick ) const
{
	const std::vector<PlayerSample_t> &samples = track.m_Samples;
	const auto after = std::upper_bound( samples.begin(), samples.end(), flTick, []( double flAt, const PlayerSample_t &sample ) { return flAt < sample.m_nTick; } );
	if ( after == samples.begin() )
	{
		return std::nullopt;
	}
	const PlayerSample_t &from = *( after - 1 );
	if ( !from.m_bThere )
	{
		return std::nullopt;
	}
	PlayerState_t state;
	state.m_nEntity = track.m_nEntity;
	state.m_pIdentity = track.IdentityAt( static_cast<int>( flTick ) );
	state.m_vecOrigin = from.m_vecOrigin;
	state.m_flPitch = from.m_flPitch;
	state.m_flYaw = from.m_flYaw;
	state.m_nHealth = from.m_nHealth;
	state.m_nTeam = from.m_nTeam;
	state.m_bAlive = from.m_bAlive;
	state.m_bDucked = from.m_bDucked;
	if ( after != samples.end() && after->m_bThere && after->m_nTick > from.m_nTick )
	{
		const PlayerSample_t &to = *after;
		const double flTicks = to.m_nTick - from.m_nTick;
		const double flSeconds = flTicks * ( m_flTickInterval > 0.0 ? m_flTickInterval : FALLBACK_TICK_INTERVAL );
		if ( Length( to.m_vecOrigin - from.m_vecOrigin ) <= FASTEST_SPEED * flSeconds + TELEPORT_SLACK )
		{
			const float flPart = static_cast<float>( ( flTick - from.m_nTick ) / flTicks );
			state.m_vecOrigin = from.m_vecOrigin + ( to.m_vecOrigin - from.m_vecOrigin ) * flPart;
			state.m_flPitch = LerpAngle( from.m_flPitch, to.m_flPitch, flPart );
			state.m_flYaw = LerpAngle( from.m_flYaw, to.m_flYaw, flPart );
		}
	}
	return state;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<PlayerState_t> CPlayers::At( double flTick ) const
{
	std::vector<PlayerState_t> states;
	for ( const PlayerTrack_t &track : m_Tracks )
	{
		if ( auto state = StateOf( track, flTick ) )
		{
			states.push_back( *state );
		}
	}
	return states;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<PlayerState_t> CPlayers::At( int nEntity, double flTick ) const
{
	const PlayerTrack_t *pTrack = FindTrack( nEntity );
	return pTrack ? StateOf( *pTrack, flTick ) : std::nullopt;
}

} // namespace xv::demo
