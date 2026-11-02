//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Dust.h.
//
//===========================================================================//
#include "Dust.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "kv1/Kv1.h"
#include "map/VmfValues.h"
#include "util/Numbers.h"

namespace xv::map
{

namespace
{

// How fast a mote's sideways drift settles to the wind's (there is none in the editor), in units
// a second each second.
constexpr float DUST_SETTLE = 50.0f;
// Tries at a random point in the bounds for one inside a brush.
constexpr int SPAWN_TRIES = 10;
// The longest step taken at once.
constexpr float MAX_STEP = 0.1f;

//-----------------------------------------------------------------------------
// Purpose: A key's number, or the default when it is missing or doesn't read.
//-----------------------------------------------------------------------------
float KeyNumber( const Entity_t &entity, std::string_view szKey, float flDefault )
{
	return ParseNumber<float>( entity.GetValue( szKey ) ).value_or( flDefault );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: A dust cloud's defaults are its own (big, faint motes); the rest are shared. Dust
//          that starts disabled plays too, as info_particle_system does whatever start_active
//          says: most dust is turned on by an input.
//-----------------------------------------------------------------------------
std::optional<DustEmitter_t> ReadDustEmitter( const CScene &scene, std::size_t nEntity )
{
	const Entity_t &entity = scene.m_Map.m_Entities[ nEntity ];
	const std::string_view szClass = entity.GetClassname();
	const bool bMotes = kv1::EqualsIgnoreCase( szClass, "func_dustmotes" );
	if ( entity.m_bDeleted || ( !bMotes && !kv1::EqualsIgnoreCase( szClass, "func_dustcloud" ) ) )
	{
		return std::nullopt;
	}
	DustEmitter_t emitter;
	emitter.m_bMotes = bMotes;
	if ( const auto color = ParseColor( entity.GetValue( "Color" ) ) )
	{
		emitter.m_Color = { color->m_nR / 255.0f, color->m_nG / 255.0f, color->m_nB / 255.0f };
	}
	emitter.m_flAlpha = std::clamp( KeyNumber( entity, "Alpha", bMotes ? 255.0f : 30.0f ), 0.0f, 255.0f ) / 255.0f;
	emitter.m_nSpawnRate = std::max( 0, static_cast<int>( KeyNumber( entity, "SpawnRate", 40.0f ) ) );
	emitter.m_flSizeMin = KeyNumber( entity, "SizeMin", bMotes ? 10.0f : 100.0f );
	emitter.m_flSizeMax = std::max( emitter.m_flSizeMin, KeyNumber( entity, "SizeMax", bMotes ? 20.0f : 200.0f ) );
	emitter.m_flSpeedMax = std::max( 0.0f, KeyNumber( entity, "SpeedMax", 13.0f ) );
	// The game keeps lifetimes in whole seconds.
	emitter.m_flLifetimeMin = std::max( 0.0f, std::floor( KeyNumber( entity, "LifetimeMin", 3.0f ) ) );
	emitter.m_flLifetimeMax = std::max( emitter.m_flLifetimeMin, std::floor( KeyNumber( entity, "LifetimeMax", 5.0f ) ) );
	emitter.m_flDistMax = std::max( 1.0f, KeyNumber( entity, "DistMax", 1024.0f ) );
	emitter.m_flFallSpeed = KeyNumber( entity, "FallSpeed", 0.0f );
	emitter.m_bFrozen = entity.GetValue( "Frozen" ).starts_with( '1' );
	const auto &solids = scene.m_Map.m_Solids;
	for ( std::size_t s = 0; s < solids.size() && s < scene.m_Solids.size(); s++ )
	{
		if ( solids[ s ].m_bDeleted || solids[ s ].m_nEntity != static_cast<int>( nEntity ) || scene.m_Solids[ s ].m_Faces.empty() )
			continue;
		std::vector<Plane_t> planes;
		for ( const FaceGeometry_t &face : scene.m_Solids[ s ].m_Faces )
			planes.push_back( face.m_Plane );
		emitter.m_Brushes.push_back( std::move( planes ) );
		emitter.m_Bounds.Add( scene.m_Solids[ s ].m_Bounds );
	}
	if ( emitter.m_Brushes.empty() || emitter.m_Bounds.IsEmpty() )
	{
		return std::nullopt;
	}
	return emitter;
}

//-----------------------------------------------------------------------------
// Purpose: A frozen emitter makes its motes now, as many as it would make in a second.
//-----------------------------------------------------------------------------
CDust::CDust( DustEmitter_t emitter, std::uint32_t nSeed )
	: m_Emitter( std::move( emitter ) ),
	  m_Random( nSeed )
{
	if ( m_Emitter.m_bFrozen )
	{
		for ( int i = 0; i < m_Emitter.m_nSpawnRate && m_Motes.size() < MAX_MOTES; i++ )
			Spawn();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float CDust::Random( float flMin, float flMax )
{
	return flMin + ( flMax - flMin ) * std::uniform_real_distribution<float>{ 0.0f, 1.0f }( m_Random );
}

//-----------------------------------------------------------------------------
// Purpose: A mote at a random point of the bounds that lies inside one of the brushes; none
//          when every try misses.
//-----------------------------------------------------------------------------
void CDust::Spawn()
{
	const Box_t &bounds = m_Emitter.m_Bounds;
	for ( int nTry = 0; nTry < SPAWN_TRIES; nTry++ )
	{
		Point_t at{};
		for ( std::size_t i = 0; i < 3; i++ )
			at[ i ] = bounds.m_Mins[ i ] + ( bounds.m_Maxs[ i ] - bounds.m_Mins[ i ] ) * static_cast<double>( Random( 0.0f, 1.0f ) );
		const bool bInside = std::ranges::any_of( m_Emitter.m_Brushes, [ & ]( const std::vector<Plane_t> &planes ) {
			return std::ranges::all_of( planes, [ & ]( const Plane_t &plane ) { return plane.Distance( at ) <= 0.0; } );
		} );
		if ( !bInside )
			continue;
		Mote_t mote;
		mote.m_Position = at;
		const float flSpeed = m_Emitter.m_flSpeedMax;
		mote.m_Velocity = { Random( -flSpeed, flSpeed ), Random( -flSpeed, flSpeed ), Random( -flSpeed, flSpeed ) - m_Emitter.m_flFallSpeed };
		mote.m_flLife = std::max( Random( m_Emitter.m_flLifetimeMin, m_Emitter.m_flLifetimeMax ), 1.0e-3f );
		// A dust mote's size is a fraction of its distance (Layout), so it stays the same on screen.
		const float flScale = m_Emitter.m_bMotes ? 1.0f / 10000.0f : 1.0f;
		mote.m_flSize = Random( m_Emitter.m_flSizeMin, m_Emitter.m_flSizeMax ) * flScale;
		m_Motes.push_back( mote );
		return;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Motes age and die, their sideways drift settling, then move; new ones are born at
//          the spawn rate.
//-----------------------------------------------------------------------------
void CDust::Advance( float flSeconds )
{
	if ( m_Emitter.m_bFrozen )
	{
		return;
	}
	while ( flSeconds > 0.0f )
	{
		const float flStep = std::min( flSeconds, MAX_STEP );
		flSeconds -= flStep;
		std::erase_if( m_Motes, [ & ]( Mote_t &mote ) {
			mote.m_flAge += flStep;
			if ( mote.m_flAge >= mote.m_flLife )
				return true;
			for ( std::size_t i = 0; i < 2; i++ )
			{
				const double flSettle = DUST_SETTLE * flStep;
				mote.m_Velocity[ i ] = mote.m_Velocity[ i ] > 0.0 ? std::max( 0.0, mote.m_Velocity[ i ] - flSettle ) : std::min( 0.0, mote.m_Velocity[ i ] + flSettle );
			}
			mote.m_Position = Add( mote.m_Position, Scale( mote.m_Velocity, flStep ) );
			return false;
		} );
		m_flOwed += static_cast<float>( m_Emitter.m_nSpawnRate ) * flStep;
		while ( m_flOwed >= 1.0f )
		{
			m_flOwed -= 1.0f;
			if ( m_Motes.size() < MAX_MOTES )
				Spawn();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Each mote fades in and out over its life (a frozen one never does), and toward
//          nothing as it nears DistMax ahead of the eye.
//-----------------------------------------------------------------------------
void CDust::Layout( Point_t eye, Point_t right, Point_t up, std::uint32_t nMaterial, std::array<int, 2> textureSize, std::uint32_t nObject, std::vector<ParticleSprite_t> &out ) const
{
	const Point_t forward = Cross( up, right );
	struct Seen_t
	{
		double m_flDepth;
		const Mote_t *m_pMote;
	};
	std::vector<Seen_t> seen;
	for ( const Mote_t &mote : m_Motes )
	{
		const double flDepth = Dot( Subtract( mote.m_Position, eye ), forward );
		if ( flDepth > 0.0 && flDepth <= m_Emitter.m_flDistMax )
			seen.push_back( { flDepth, &mote } );
	}
	std::ranges::sort( seen, []( const Seen_t &a, const Seen_t &b ) { return a.m_flDepth > b.m_flDepth; } );
	for ( const Seen_t &at : seen )
	{
		const Mote_t &mote = *at.m_pMote;
		float flAlpha = 1.0f;
		if ( !m_Emitter.m_bFrozen )
		{
			const float flTurn = mote.m_flAge / mote.m_flLife * 2.0f * std::numbers::pi_v<float>;
			flAlpha = 0.5f - 0.5f * std::cos( flTurn );
		}
		flAlpha *= 1.0f - static_cast<float>( at.m_flDepth ) / m_Emitter.m_flDistMax;
		const double flSize = m_Emitter.m_bMotes ? mote.m_flSize * at.m_flDepth : mote.m_flSize;
		const Point_t across = Scale( right, flSize );
		const Point_t above = Scale( up, flSize );
		ParticleSprite_t sprite;
		sprite.m_Corners = { ToVec3( Add( Subtract( mote.m_Position, across ), above ) ), ToVec3( Add( Add( mote.m_Position, across ), above ) ),
			ToVec3( Subtract( Add( mote.m_Position, across ), above ) ), ToVec3( Subtract( Subtract( mote.m_Position, across ), above ) ) };
		sprite.m_Color = { m_Emitter.m_Color[ 0 ], m_Emitter.m_Color[ 1 ], m_Emitter.m_Color[ 2 ], flAlpha * m_Emitter.m_flAlpha };
		sprite.m_nMaterial = nMaterial;
		sprite.m_TextureSize = textureSize;
		sprite.m_nObject = nObject;
		out.push_back( sprite );
	}
}

} // namespace xv::map
