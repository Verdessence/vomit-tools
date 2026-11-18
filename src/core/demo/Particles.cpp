//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Particles.h.
//
//===========================================================================//
#include "Particles.h"

#include <algorithm>
#include <cmath>

namespace xv::demo
{

namespace
{

// A dispatched effect's key: apart from the systems' (whose lives number far fewer).
constexpr std::uint64_t DISPATCHED_KEY = std::uint64_t{ 1 } << 62;

//-----------------------------------------------------------------------------
// Purpose: The tick a particle system was last switched on by nTick: after the last key that
//          had it off, else when it came.
//-----------------------------------------------------------------------------
int SwitchedOnAt( const EntityLife_t &life, int nTick )
{
	const std::vector<EntityKey_t> &keys = life.m_Fields[ ENTITY_FIELD_EXTRA_0 ];
	int nOn = life.m_nFrom;
	for ( const EntityKey_t &key : keys )
	{
		if ( key.m_nTick > nTick )
		{
			break;
		}
		if ( key.m_flValue[ 0 ] == 0.0f )
		{
			nOn = -1;
		}
		else if ( nOn < 0 )
		{
			nOn = key.m_nTick;
		}
	}
	return std::max( nOn, life.m_nFrom );
}

//-----------------------------------------------------------------------------
// Purpose: A vector a temp entity has in three parts ("m_vOrigin[0]"...) or as one.
//-----------------------------------------------------------------------------
CVec3 TempVector( const CContents &contents, const TempEntity_t &temp, std::string_view szName )
{
	if ( const PropValue_t *pWhole = contents.TempValue( temp, szName ) )
	{
		return { static_cast<float>( ( *pWhole )[ 0 ] ), static_cast<float>( ( *pWhole )[ 1 ] ), static_cast<float>( ( *pWhole )[ 2 ] ) };
	}
	CVec3 vec;
	const std::string szBase( szName );
	if ( const PropValue_t *p = contents.TempValue( temp, szBase + "[0]" ) )
		vec.m_flX = static_cast<float>( ( *p )[ 0 ] );
	if ( const PropValue_t *p = contents.TempValue( temp, szBase + "[1]" ) )
		vec.m_flY = static_cast<float>( ( *p )[ 0 ] );
	if ( const PropValue_t *p = contents.TempValue( temp, szBase + "[2]" ) )
		vec.m_flZ = static_cast<float>( ( *p )[ 0 ] );
	return vec;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<DemoParticles_t> ParticlesAt( const CContents &contents, int nTick, int nDispatchTicks )
{
	std::vector<DemoParticles_t> systems;
	const int nClass = contents.FindClass( "CParticleSystem" );
	if ( nClass >= 0 )
	{
		const std::vector<EntityLife_t> &lives = contents.GetLives();
		for ( std::size_t i = 0; i < lives.size(); i++ )
		{
			const EntityLife_t &life = lives[ i ];
			if ( life.m_nClass != nClass || life.m_nFrom > nTick || ( life.m_nTo >= 0 && nTick >= life.m_nTo ) || !life.IsThere( nTick ) )
			{
				continue;
			}
			const std::array<float, 3> active = life.ValueAt( ENTITY_FIELD_EXTRA_0, nTick );
			const std::array<float, 3> effect = life.ValueAt( ENTITY_FIELD_EXTRA_1, nTick );
			if ( active[ 0 ] == 0.0f )
			{
				continue;
			}
			DemoParticles_t system;
			system.m_szEffect = contents.ParticleName( static_cast<int>( effect[ 0 ] ) );
			if ( system.m_szEffect.empty() )
			{
				continue;
			}
			system.m_nStartTick = SwitchedOnAt( life, nTick );
			system.m_pLife = &life;
			system.m_nKey = ( static_cast<std::uint64_t>( i ) << 24 ) ^ static_cast<std::uint64_t>( system.m_nStartTick );
			systems.push_back( system );
		}
	}

	// Dispatched effects: "ParticleEffect" ones name their system by m_nHitBox.
	const int nDispatch = contents.FindClass( "CTEEffectDispatch" );
	const std::vector<TempEntity_t> &temps = contents.GetTempEntities();
	const auto first = std::ranges::lower_bound( temps, nTick - nDispatchTicks, {}, &TempEntity_t::m_nTick );
	for ( auto it = first; nDispatch >= 0 && it != temps.end() && it->m_nTick <= nTick; ++it )
	{
		if ( it->m_nClass != nDispatch )
		{
			continue;
		}
		const PropValue_t *pName = contents.TempValue( *it, "m_iEffectName" );
		const PropValue_t *pSystem = contents.TempValue( *it, "m_nHitBox" );
		if ( !pName || !pSystem || contents.EffectName( static_cast<int>( ( *pName )[ 0 ] ) ) != "ParticleEffect" )
		{
			continue;
		}
		DemoParticles_t system;
		system.m_szEffect = contents.ParticleName( static_cast<int>( ( *pSystem )[ 0 ] ) );
		if ( system.m_szEffect.empty() )
		{
			continue;
		}
		system.m_nStartTick = it->m_nTick;
		system.m_nKey = DISPATCHED_KEY | static_cast<std::uint64_t>( it - temps.begin() );
		system.m_vecOrigin = TempVector( contents, *it, "m_vOrigin" );
		system.m_vecAngles = TempVector( contents, *it, "m_vAngles" );
		if ( const PropValue_t *pEntity = contents.TempValue( *it, "entindex" ) )
		{
			system.m_nEntity = static_cast<int>( ( *pEntity )[ 0 ] );
		}
		systems.push_back( system );
	}
	return systems;
}

} // namespace xv::demo
