//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Sprays.h.
//
//===========================================================================//
#include "demo/Sprays.h"

#include <cstdio>
#include <map>

namespace xv::demo
{

namespace
{

// A spray sent here on every axis takes the player's spray off.
constexpr float REMOVED_AT = 16384.0f;

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The CTEPlayerDecals, each with its player's logo at that moment.
//-----------------------------------------------------------------------------
CSprays::CSprays( const CContents &contents, const CPlayers &players )
{
	const int nClass = contents.FindClass( "CTEPlayerDecal" );
	if ( nClass < 0 )
		return;
	const auto value = [ & ]( const TempEntity_t &temp, std::string_view szName, std::size_t n = 0 ) -> std::optional<double> {
		if ( const PropValue_t *pValue = contents.TempValue( temp, szName ) )
			return ( *pValue )[ n ];
		return std::nullopt;
	};
	for ( const TempEntity_t &temp : contents.GetTempEntities() )
	{
		if ( temp.m_nClass != nClass )
			continue;
		DemoSpray_t spray;
		spray.m_nTick = temp.m_nTick;
		spray.m_nPlayer = static_cast<int>( value( temp, "m_nPlayer" ).value_or( 0 ) );
		spray.m_nEntity = static_cast<int>( value( temp, "m_nEntity" ).value_or( 0 ) );
		if ( const PropValue_t *pOrigin = contents.TempValue( temp, "m_vecOrigin" ) )
			spray.m_vecOrigin = { static_cast<float>( ( *pOrigin )[ 0 ] ), static_cast<float>( ( *pOrigin )[ 1 ] ), static_cast<float>( ( *pOrigin )[ 2 ] ) };
		else
			spray.m_vecOrigin = { static_cast<float>( value( temp, "m_vecOrigin[0]" ).value_or( 0.0 ) ), static_cast<float>( value( temp, "m_vecOrigin[1]" ).value_or( 0.0 ) ), static_cast<float>( value( temp, "m_vecOrigin[2]" ).value_or( 0.0 ) ) };
		for ( const PlayerTrack_t &track : players.GetTracks() )
		{
			if ( track.m_nEntity != spray.m_nPlayer )
				continue;
			if ( const PlayerIdentity_t *pIdentity = track.IdentityAt( spray.m_nTick ) )
				spray.m_nLogo = pIdentity->m_nLogo;
		}
		m_Sprays.push_back( spray );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Each player's last up to the tick.
//-----------------------------------------------------------------------------
std::vector<DemoSpray_t> CSprays::At( int nTick ) const
{
	std::map<int, const DemoSpray_t *> last;
	for ( const DemoSpray_t &spray : m_Sprays )
	{
		if ( spray.m_nTick > nTick )
			break;
		last[ spray.m_nPlayer ] = &spray;
	}
	std::vector<DemoSpray_t> up;
	for ( const auto &[ nPlayer, pSpray ] : last )
	{
		const CVec3 &vecAt = pSpray->m_vecOrigin;
		if ( vecAt.m_flX >= REMOVED_AT && vecAt.m_flY >= REMOVED_AT && vecAt.m_flZ >= REMOVED_AT )
			continue;
		up.push_back( *pSpray );
	}
	return up;
}

//-----------------------------------------------------------------------------
// Purpose: The CRC's bytes as they lie in the userinfo, in hex: the folder is the first.
//-----------------------------------------------------------------------------
std::string SprayFile( std::uint32_t nLogo )
{
	const unsigned b0 = nLogo & 0xff, b1 = ( nLogo >> 8 ) & 0xff, b2 = ( nLogo >> 16 ) & 0xff, b3 = nLogo >> 24;
	char szPath[ 64 ];
	std::snprintf( szPath, sizeof( szPath ), "download/user_custom/%02x/%02x%02x%02x%02x.dat", b0, b0, b1, b2, b3 );
	return szPath;
}

} // namespace xv::demo
