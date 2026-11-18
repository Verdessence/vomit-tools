//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Sky.h.
//
//===========================================================================//
#include "Sky.h"

#include <cstdint>
#include <string>

namespace xv::demo
{

namespace
{

// The players' slots: entities 1 to this.
constexpr int PLAYER_ENTITY_MOST = 64;
// m_skybox3d.area when the player sees no 3D skybox.
constexpr int SKYBOX_NO_AREA = 255;

//-----------------------------------------------------------------------------
// Purpose: A fog's settings from a life's stats under a prefix ("m_fog." or
//          "m_skybox3d.fog."); nullopt when it is off or the life hasn't them.
//-----------------------------------------------------------------------------
std::optional<map::Fog_t> FogOf( const CContents &contents, const EntityLife_t &life, std::string_view szPrefix, int nTick )
{
	const auto stat = [ & ]( std::string_view szName ) { return contents.StatOf( life, std::string( szPrefix ) + std::string( szName ), nTick ); };
	const auto enable = stat( "enable" );
	if ( !enable || *enable == 0.0f )
	{
		return std::nullopt;
	}
	// A colour's bytes: red lowest (alpha isn't kept).
	const auto color = []( std::optional<float> flColor ) {
		const auto nColor = static_cast<std::uint32_t>( flColor.value_or( 0.0f ) );
		return CVec3{ static_cast<float>( nColor & 0xff ) / 255.0f, static_cast<float>( ( nColor >> 8 ) & 0xff ) / 255.0f, static_cast<float>( ( nColor >> 16 ) & 0xff ) / 255.0f };
	};
	map::Fog_t fog;
	fog.m_vecColor = color( stat( "colorPrimary" ) );
	fog.m_vecColor2 = color( stat( "colorSecondary" ) );
	fog.m_bBlend = stat( "blend" ).value_or( 0.0f ) != 0.0f;
	fog.m_bRadial = stat( "radial" ).value_or( 0.0f ) != 0.0f;
	fog.m_flStart = stat( "start" ).value_or( 0.0f );
	fog.m_flEnd = stat( "end" ).value_or( 0.0f );
	fog.m_flMaxDensity = stat( "maxdensity" ).value_or( 1.0f );
	const auto x = stat( "dirPrimary[0]" );
	const auto y = stat( "dirPrimary[1]" );
	const auto z = stat( "dirPrimary[2]" );
	if ( x && y && z && ( *x != 0.0f || *y != 0.0f || *z != 0.0f ) )
	{
		fog.m_Direction = { *x, *y, *z };
	}
	return fog;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
DemoSky_t SkyAt( const CContents &contents, int nTick, int nPlayer )
{
	DemoSky_t sky;
	const int nFogClass = contents.FindClass( "CFogController" );
	// The players to ask, the one seen through first.
	std::vector<const EntityLife_t *> players;
	if ( nPlayer > 0 )
	{
		if ( const EntityLife_t *pLife = contents.LifeAt( nPlayer, nTick ) )
		{
			players.push_back( pLife );
		}
	}
	for ( int nEntity = 1; nEntity <= PLAYER_ENTITY_MOST; nEntity++ )
	{
		if ( nEntity == nPlayer )
		{
			continue;
		}
		if ( const EntityLife_t *pLife = contents.LifeAt( nEntity, nTick ) )
		{
			players.push_back( pLife );
		}
	}

	// The fog: the controller a player's m_PlayerFog names, else the first there is.
	const EntityLife_t *pController = nullptr;
	for ( const EntityLife_t *pPlayer : players )
	{
		const auto handle = contents.StatOf( *pPlayer, "m_PlayerFog.m_hCtrl", nTick );
		if ( !handle )
		{
			continue;
		}
		const auto nHandle = static_cast<std::uint32_t>( *handle );
		if ( nHandle == ENTITY_NO_HANDLE )
		{
			continue;
		}
		const EntityLife_t *pLife = contents.LifeAt( static_cast<int>( nHandle & ENTITY_HANDLE_INDEX_MASK ), nTick );
		if ( pLife && pLife->m_nClass == nFogClass )
		{
			pController = pLife;
			break;
		}
	}
	if ( !pController && nFogClass >= 0 )
	{
		for ( const EntityLife_t &life : contents.GetLives() )
		{
			if ( life.m_nClass == nFogClass && life.m_nFrom <= nTick && ( life.m_nTo < 0 || nTick < life.m_nTo ) && ( !pController || life.m_nEntity < pController->m_nEntity ) )
			{
				pController = &life;
			}
		}
	}
	if ( pController )
	{
		sky.m_bFogKnown = true;
		sky.m_Fog = FogOf( contents, *pController, "m_fog.", nTick );
	}

	// The 3D skybox's camera.
	for ( const EntityLife_t *pPlayer : players )
	{
		const auto scale = contents.StatOf( *pPlayer, "m_skybox3d.scale", nTick );
		const auto area = contents.StatOf( *pPlayer, "m_skybox3d.area", nTick );
		if ( !scale || *scale <= 0.0f || ( area && static_cast<int>( *area ) == SKYBOX_NO_AREA ) )
		{
			continue;
		}
		DemoSkyCamera_t camera;
		camera.m_flScale = *scale;
		camera.m_nArea = area ? static_cast<int>( *area ) : 0;
		camera.m_vecOrigin = { contents.StatOf( *pPlayer, "m_skybox3d.origin[0]", nTick ).value_or( 0.0f ), contents.StatOf( *pPlayer, "m_skybox3d.origin[1]", nTick ).value_or( 0.0f ), contents.StatOf( *pPlayer, "m_skybox3d.origin[2]", nTick ).value_or( 0.0f ) };
		camera.m_Fog = FogOf( contents, *pPlayer, "m_skybox3d.fog.", nTick );
		sky.m_Camera = camera;
		break;
	}
	return sky;
}

} // namespace xv::demo
