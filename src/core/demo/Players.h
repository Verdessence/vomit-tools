//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Where the players of a demo were, over its whole length: the demo's packets are
//          followed from the start (Network.h) and each player's place, view angles, team and
//          health noted whenever they change. A SourceTV demo has every player all the time; a
//          player's own demo has the others only while the server sent them (near enough to be
//          seen or heard).
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "demo/Demo.h"
#include "demo/Network.h"
#include "util/Vec3.h"

namespace xv::demo
{

// A player's eyes above its origin, standing and ducked (Source's player hull).
constexpr float PLAYER_EYE_HEIGHT = 64.0f;
constexpr float PLAYER_DUCKED_EYE_HEIGHT = 47.0f;

// A player as a snapshot had it. One is noted when something differs from the one before.
struct PlayerSample_t
{
	int m_nTick = 0;
	CVec3 m_vecOrigin;      // its feet
	float m_flPitch = 0.0f; // where it looks: down is positive
	float m_flYaw = 0.0f;
	std::int16_t m_nHealth = 0;
	std::uint8_t m_nTeam = 0;
	bool m_bAlive = false;
	bool m_bDucked = false;
	bool m_bThere = true; // false: from this tick the demo doesn't have the player

	// Whether two say the same of the player, whatever their ticks.
	[[nodiscard]] bool IsSameState( const PlayerSample_t &other ) const
	{
		return m_vecOrigin == other.m_vecOrigin && m_flPitch == other.m_flPitch && m_flYaw == other.m_flYaw && m_nHealth == other.m_nHealth && m_nTeam == other.m_nTeam && m_bAlive == other.m_bAlive && m_bDucked == other.m_bDucked && m_bThere == other.m_bThere;
	}
};

// Who had a player slot from a tick on.
struct PlayerIdentity_t
{
	int m_nTick = 0;
	std::string m_szName; // empty: nobody (the player left)
	std::string m_szSteamId;
	int m_nUserId = 0;
	bool m_bBot = false;
	bool m_bSourceTv = false; // the server's own recorder, which takes a slot

	[[nodiscard]] bool IsSamePlayer( const PlayerIdentity_t &other ) const
	{
		return m_szName == other.m_szName && m_szSteamId == other.m_szSteamId && m_nUserId == other.m_nUserId;
	}
};

struct PlayerTrack_t
{
	int m_nEntity = 0; // the slot: entity 1 is the first player
	std::vector<PlayerSample_t> m_Samples;      // by tick
	std::vector<PlayerIdentity_t> m_Identities; // by tick

	// Who had the slot at a tick; null when nobody is known to.
	[[nodiscard]] const PlayerIdentity_t *IdentityAt( int nTick ) const;
};

// A player at a moment between snapshots.
struct PlayerState_t
{
	int m_nEntity = 0;
	const PlayerIdentity_t *m_pIdentity = nullptr;
	CVec3 m_vecOrigin;
	float m_flPitch = 0.0f;
	float m_flYaw = 0.0f;
	int m_nHealth = 0;
	int m_nTeam = 0;
	bool m_bAlive = false;
	bool m_bDucked = false;

	[[nodiscard]] CVec3 GetEye() const { return m_vecOrigin + CVec3{ 0.0f, 0.0f, m_bDucked ? PLAYER_DUCKED_EYE_HEIGHT : PLAYER_EYE_HEIGHT }; }
};

class CPlayers
{
public:
	// Follows the whole demo; file is the bytes demo was read from. pfnProgress is told how far
	// along it is (0 to 1) now and then, and stops the reading by answering false.
	[[nodiscard]] static CPlayers Read( const CDemo &demo, std::span<const std::byte> file, const std::function<bool( double )> &pfnProgress = {} );

	// The slots that ever had a player in sight, by entity.
	[[nodiscard]] const std::vector<PlayerTrack_t> &GetTracks() const { return m_Tracks; }
	[[nodiscard]] const PlayerTrack_t *FindTrack( int nEntity ) const;
	// Every player the demo has at a tick (and fractions of one), moved smoothly between the
	// snapshots either side; one that jumped (a teleport, a respawn) stays put until it lands.
	[[nodiscard]] std::vector<PlayerState_t> At( double flTick ) const;
	[[nodiscard]] std::optional<PlayerState_t> At( int nEntity, double flTick ) const;

	// Why the players stop short of the demo's end (packets this couldn't read); empty when
	// the whole demo was followed.
	[[nodiscard]] const std::string &GetWarning() const { return m_szWarning; }
	[[nodiscard]] const std::optional<ServerInfo_t> &GetServerInfo() const { return m_ServerInfo; }
	// The recorder's own entity; 0 for a SourceTV demo, or when the demo doesn't say.
	[[nodiscard]] int GetRecorderEntity() const;
	[[nodiscard]] int GetSnapshotCount() const { return m_nSnapshots; }
	[[nodiscard]] bool IsEmpty() const { return m_Tracks.empty(); }

private:
	[[nodiscard]] std::optional<PlayerState_t> StateOf( const PlayerTrack_t &track, double flTick ) const;

	std::vector<PlayerTrack_t> m_Tracks;
	std::optional<ServerInfo_t> m_ServerInfo;
	std::string m_szWarning;
	double m_flTickInterval = 0.0;
	int m_nSnapshots = 0;
};

// What a game calls a team ("Terrorists" for Counter-Strike's 2); "Team 5" for one it has no
// name for.
[[nodiscard]] std::string TeamName( std::string_view szGameDir, int nTeam );

} // namespace xv::demo
