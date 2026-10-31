//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Lighting.h. The light entities' keys and what vrad does with them come from the
//          VDC articles (light, light_spot, light_environment, Constant-Linear-Quadratic
//          Falloff, Lighting) and vrad's behaviour; the tracing and gathering are our own.
//
//===========================================================================//
#include "Lighting.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <cctype>
#include <optional>
#include <string>
#include <utility>

#include "map/Entities.h"
#include "map/Scene.h"
#include "util/Numbers.h"
#include "util/Parallel.h"

namespace xv::map
{

namespace
{

// Light below this (in vrad's units, of 255) isn't worth a ray: one step of an 8-bit lightmap.
constexpr double LIGHT_THRESHOLD = 1.0;
// Rays leave a face this far along its normal, past the face and its coplanar neighbours.
constexpr double RAY_OFFSET = 0.5;
// As far as any ray goes: across the largest map there can be.
constexpr double RAY_LENGTH = 65536.0;
// vrad leaves a falloff's distance at least this.
constexpr double MIN_DISTANCE = 1.0;
// The sun's rays when it has a size (SunSpreadAngle).
constexpr int SUN_SPREAD_SAMPLES = 8;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double KeyNumber( const Entity_t &entity, std::string_view szKey, double flDefault = 0.0 )
{
	const std::string_view szValue = entity.GetValue( szKey );
	if ( szValue.empty() )
	{
		return flDefault;
	}
	// vrad reads keys as atof does: the leading number, 0 for none.
	std::size_t nEnd = 0;
	while ( nEnd < szValue.size() && szValue[ nEnd ] != ' ' && szValue[ nEnd ] != '\t' )
	{
		nEnd++;
	}
	return ParseNumber<double>( szValue.substr( 0, nEnd ) ).value_or( 0.0 );
}

//-----------------------------------------------------------------------------
// Purpose: The way a light points, from its keys as vrad reads them: "angle" (-1 up, -2 down)
//          or the yaw of "angles", and "pitch" unless it is 0, else the pitch of "angles", where
//          a positive pitch points up.
//-----------------------------------------------------------------------------
Point_t LightDirection( const Entity_t &entity )
{
	const std::array<double, 3> angles = ParseTriple( entity.GetValue( "angles" ) ).value_or( std::array<double, 3>{} );
	double flAngle = KeyNumber( entity, "angle" );
	double flPitch = KeyNumber( entity, "pitch" );
	Point_t dir{};
	if ( flAngle == -1.0 )
	{
		dir = { 0, 0, 1 };
	}
	else if ( flAngle == -2.0 )
	{
		dir = { 0, 0, -1 };
	}
	else
	{
		if ( flAngle == 0.0 )
		{
			flAngle = angles[ 1 ];
		}
		const double flYaw = flAngle * std::numbers::pi / 180.0;
		dir = { std::cos( flYaw ), std::sin( flYaw ), 0.0 };
	}
	if ( flPitch == 0.0 )
	{
		flPitch = angles[ 0 ];
	}
	const double flRadians = flPitch * std::numbers::pi / 180.0;
	dir[ 2 ] = std::sin( flRadians );
	dir[ 0 ] *= std::cos( flRadians );
	dir[ 1 ] *= std::cos( flRadians );
	return Normalized( dir );
}

//-----------------------------------------------------------------------------
// Purpose: The light's brightness, from _lightHDR (times _lightscaleHDR) in an HDR compile when
//          it is set, else _light.
//-----------------------------------------------------------------------------
CVec3 LightIntensity( const Entity_t &entity, bool bHdr, std::string_view szKey, std::string_view szHdrKey, std::string_view szHdrScale, bool &bFound )
{
	bFound = false;
	if ( bHdr )
	{
		if ( const auto hdr = ParseLightValue( entity.GetValue( szHdrKey ) ) )
		{
			bFound = true;
			return *hdr * static_cast<float>( KeyNumber( entity, szHdrScale, 1.0 ) );
		}
	}
	if ( const auto ldr = ParseLightValue( entity.GetValue( szKey ) ) )
	{
		bFound = true;
		return bHdr ? *ldr * static_cast<float>( KeyNumber( entity, szHdrScale, 1.0 ) ) : *ldr;
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose: A point or spot light's falloff from its keys: _fifty_percent_distance and
//          _zero_percent_distance when the first is set, else constant, linear and quadratic
//          with the light scaled so it is as bright as asked at 100 units.
//-----------------------------------------------------------------------------
void ReadFalloff( const Entity_t &entity, Light_t &light )
{
	const double d50 = KeyNumber( entity, "_fifty_percent_distance" );
	if ( d50 != 0.0 )
	{
		double d0 = KeyNumber( entity, "_zero_percent_distance" );
		if ( d0 < d50 )
		{
			d0 = 2.0 * d50;
		}
		const std::array<double, 3> falloff = FalloffForDistances( d50, d0 );
		light.m_flConstant = falloff[ 0 ];
		light.m_flLinear = falloff[ 1 ];
		light.m_flQuadratic = falloff[ 2 ];
		if ( KeyNumber( entity, "_hardfalloff" ) != 0.0 )
		{
			// Gone at the 0% distance, fading from three quarters of the way there from 50%.
			light.m_flFadeEnd = d0;
			light.m_flFadeStart = 0.75 * d0 + 0.25 * d50;
		}
		else if ( light.m_flQuadratic < 0.0 )
		{
			// A falloff that would brighten again past its turn stops there and fades out by ten
			// times as far.
			const double flTurn = light.m_flLinear / ( -2.0 * light.m_flQuadratic );
			if ( flTurn > 0.0 )
			{
				light.m_flCapDistance = flTurn;
				light.m_flFadeStart = flTurn;
				light.m_flFadeEnd = 10.0 * flTurn;
			}
		}
		return;
	}
	const auto positive = []( double fl ) { return fl < 1.0e-5 ? 0.0 : fl; };
	light.m_flConstant = positive( KeyNumber( entity, "_constant_attn" ) );
	light.m_flLinear = positive( KeyNumber( entity, "_linear_attn" ) );
	light.m_flQuadratic = positive( KeyNumber( entity, "_quadratic_attn" ) );
	if ( light.m_flConstant == 0.0 && light.m_flLinear == 0.0 && light.m_flQuadratic == 0.0 )
	{
		light.m_flConstant = 1.0;
	}
	light.m_vecIntensity *= static_cast<float>( light.m_flConstant + 100.0 * light.m_flLinear + 10000.0 * light.m_flQuadratic );
}

//-----------------------------------------------------------------------------
// Purpose: How far a point or spot light reaches before it adds less than LIGHT_THRESHOLD.
//-----------------------------------------------------------------------------
double LightReach( const Light_t &light )
{
	const double flBrightest = std::max( { light.m_vecIntensity.m_flX, light.m_vecIntensity.m_flY, light.m_vecIntensity.m_flZ } );
	if ( flBrightest <= 0.0 )
	{
		return 0.0;
	}
	// Falloffs with a cap aren't always decreasing, so step out and keep the last bright one.
	double flReach = MIN_DISTANCE;
	for ( double d = MIN_DISTANCE; d < RAY_LENGTH; d *= 1.05 )
	{
		if ( flBrightest * LightFalloff( light, d ) >= LIGHT_THRESHOLD )
		{
			flReach = d * 1.05;
		}
	}
	return flReach;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float Brightest( CVec3 vec )
{
	return std::max( { vec.m_flX, vec.m_flY, vec.m_flZ } );
}

// A triangle for tracing: a corner, two edges, the face it belongs to and its corners' luxels.
struct TraceTriangle_t
{
	std::array<float, 3> m_V0{};
	std::array<float, 3> m_E1{};
	std::array<float, 3> m_E2{};
	std::uint32_t m_nFace = 0;
	std::array<std::array<float, 2>, 3> m_Luxels{};
};

struct BvhNode_t
{
	std::array<float, 3> m_Mins{};
	std::uint32_t m_nFirst = 0; // the left child (the right follows it), or the first triangle
	std::array<float, 3> m_Maxs{};
	std::uint32_t m_nCount = 0; // triangles in a leaf; 0 for a branch
};

struct TraceHit_t
{
	std::uint32_t m_nTriangle = 0;
	float m_flDistance = 0.0f;
	float m_flU = 0.0f; // barycentric weights of the second and third corners
	float m_flV = 0.0f;
	bool m_bFront = false; // the ray met the side the face's normal points out of
};

struct Ray_t
{
	std::array<float, 3> m_Origin{};
	std::array<float, 3> m_Dir{};
	std::array<float, 3> m_InvDir{};
	float m_flMax = 0.0f;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Ray_t MakeRay( Point_t origin, Point_t dir, double flMax )
{
	Ray_t ray;
	for ( std::size_t i = 0; i < 3; i++ )
	{
		ray.m_Origin[ i ] = static_cast<float>( origin[ i ] );
		ray.m_Dir[ i ] = static_cast<float>( dir[ i ] );
		ray.m_InvDir[ i ] = ray.m_Dir[ i ] != 0.0f ? 1.0f / ray.m_Dir[ i ] : std::numeric_limits<float>::infinity();
	}
	ray.m_flMax = static_cast<float>( flMax );
	return ray;
}

// The faces that stop light, as a bounding volume hierarchy for rays.
class CTracer
{
public:
	void Build( std::vector<TraceTriangle_t> triangles );
	// Whether anything is in the way within the ray's length.
	[[nodiscard]] bool IsBlocked( const Ray_t &ray ) const;
	// The nearest triangle along the ray, if any.
	[[nodiscard]] std::optional<TraceHit_t> Nearest( const Ray_t &ray ) const;
	[[nodiscard]] const TraceTriangle_t &GetTriangle( std::uint32_t n ) const { return m_Triangles[ n ]; }

private:
	[[nodiscard]] static bool HitsBox( const BvhNode_t &node, const Ray_t &ray, float flMax );
	[[nodiscard]] static bool HitsTriangle( const TraceTriangle_t &triangle, const Ray_t &ray, float flMax, TraceHit_t &hit );

	std::vector<TraceTriangle_t> m_Triangles;
	std::vector<BvhNode_t> m_Nodes;
};

//-----------------------------------------------------------------------------
// Purpose: Splits each node where the triangles' centres divide the cheapest, over a few bins
//          along its longest axis (the surface area heuristic), until a leaf holds four or fewer.
//-----------------------------------------------------------------------------
void CTracer::Build( std::vector<TraceTriangle_t> triangles )
{
	m_Triangles = std::move( triangles );
	m_Nodes.clear();
	if ( m_Triangles.empty() )
	{
		return;
	}
	const std::size_t nCount = m_Triangles.size();
	std::vector<std::array<float, 3>> centres( nCount );
	std::vector<std::array<float, 3>> mins( nCount );
	std::vector<std::array<float, 3>> maxs( nCount );
	for ( std::size_t i = 0; i < nCount; i++ )
	{
		const TraceTriangle_t &t = m_Triangles[ i ];
		for ( std::size_t k = 0; k < 3; k++ )
		{
			const float a = t.m_V0[ k ];
			const float b = a + t.m_E1[ k ];
			const float c = a + t.m_E2[ k ];
			mins[ i ][ k ] = std::min( { a, b, c } );
			maxs[ i ][ k ] = std::max( { a, b, c } );
			centres[ i ][ k ] = ( a + b + c ) / 3.0f;
		}
	}
	std::vector<std::uint32_t> order( nCount );
	for ( std::size_t i = 0; i < nCount; i++ )
	{
		order[ i ] = static_cast<std::uint32_t>( i );
	}
	m_Nodes.reserve( nCount * 2 / 3 + 1 );
	m_Nodes.emplace_back();
	struct Pending_t
	{
		std::uint32_t m_nNode;
		std::uint32_t m_nBegin;
		std::uint32_t m_nEnd;
	};
	std::vector<Pending_t> stack{ { 0, 0, static_cast<std::uint32_t>( nCount ) } };
	constexpr int BINS = 12;
	constexpr std::uint32_t LEAF = 4;
	const auto area = []( const std::array<float, 3> &lo, const std::array<float, 3> &hi ) {
		const float x = std::max( 0.0f, hi[ 0 ] - lo[ 0 ] );
		const float y = std::max( 0.0f, hi[ 1 ] - lo[ 1 ] );
		const float z = std::max( 0.0f, hi[ 2 ] - lo[ 2 ] );
		return x * y + y * z + z * x;
	};
	constexpr float BIG = std::numeric_limits<float>::max();
	while ( !stack.empty() )
	{
		const Pending_t pending = stack.back();
		stack.pop_back();
		std::array<float, 3> lo{ BIG, BIG, BIG }, hi{ -BIG, -BIG, -BIG };
		std::array<float, 3> centreLo{ BIG, BIG, BIG }, centreHi{ -BIG, -BIG, -BIG };
		for ( std::uint32_t i = pending.m_nBegin; i < pending.m_nEnd; i++ )
		{
			const std::uint32_t n = order[ i ];
			for ( std::size_t k = 0; k < 3; k++ )
			{
				lo[ k ] = std::min( lo[ k ], mins[ n ][ k ] );
				hi[ k ] = std::max( hi[ k ], maxs[ n ][ k ] );
				centreLo[ k ] = std::min( centreLo[ k ], centres[ n ][ k ] );
				centreHi[ k ] = std::max( centreHi[ k ], centres[ n ][ k ] );
			}
		}
		BvhNode_t &node = m_Nodes[ pending.m_nNode ];
		node.m_Mins = lo;
		node.m_Maxs = hi;
		const std::uint32_t nHere = pending.m_nEnd - pending.m_nBegin;
		std::size_t nAxis = 0;
		for ( std::size_t k = 1; k < 3; k++ )
		{
			if ( centreHi[ k ] - centreLo[ k ] > centreHi[ nAxis ] - centreLo[ nAxis ] )
			{
				nAxis = k;
			}
		}
		const float flExtent = centreHi[ nAxis ] - centreLo[ nAxis ];
		std::uint32_t nMid = pending.m_nBegin;
		if ( nHere > LEAF && flExtent > 0.0f )
		{
			struct Bin_t
			{
				std::array<float, 3> m_Lo{ BIG, BIG, BIG };
				std::array<float, 3> m_Hi{ -BIG, -BIG, -BIG };
				std::uint32_t m_nCount = 0;
			};
			std::array<Bin_t, BINS> bins{};
			const auto binOf = [ & ]( std::uint32_t n ) {
				const int b = static_cast<int>( ( centres[ n ][ nAxis ] - centreLo[ nAxis ] ) / flExtent * BINS );
				return std::clamp( b, 0, BINS - 1 );
			};
			for ( std::uint32_t i = pending.m_nBegin; i < pending.m_nEnd; i++ )
			{
				const std::uint32_t n = order[ i ];
				Bin_t &bin = bins[ static_cast<std::size_t>( binOf( n ) ) ];
				bin.m_nCount++;
				for ( std::size_t k = 0; k < 3; k++ )
				{
					bin.m_Lo[ k ] = std::min( bin.m_Lo[ k ], mins[ n ][ k ] );
					bin.m_Hi[ k ] = std::max( bin.m_Hi[ k ], maxs[ n ][ k ] );
				}
			}
			// The cost of each split: the triangles on each side times their box's area.
			std::array<float, BINS - 1> costs{};
			Bin_t left;
			for ( std::size_t b = 0; b + 1 < BINS; b++ )
			{
				left.m_nCount += bins[ b ].m_nCount;
				for ( std::size_t k = 0; k < 3; k++ )
				{
					left.m_Lo[ k ] = std::min( left.m_Lo[ k ], bins[ b ].m_Lo[ k ] );
					left.m_Hi[ k ] = std::max( left.m_Hi[ k ], bins[ b ].m_Hi[ k ] );
				}
				costs[ b ] = left.m_nCount > 0 ? area( left.m_Lo, left.m_Hi ) * static_cast<float>( left.m_nCount ) : 0.0f;
			}
			Bin_t right;
			for ( std::size_t b = BINS - 1; b > 0; b-- )
			{
				right.m_nCount += bins[ b ].m_nCount;
				for ( std::size_t k = 0; k < 3; k++ )
				{
					right.m_Lo[ k ] = std::min( right.m_Lo[ k ], bins[ b ].m_Lo[ k ] );
					right.m_Hi[ k ] = std::max( right.m_Hi[ k ], bins[ b ].m_Hi[ k ] );
				}
				costs[ b - 1 ] += right.m_nCount > 0 ? area( right.m_Lo, right.m_Hi ) * static_cast<float>( right.m_nCount ) : 0.0f;
			}
			std::size_t nBest = 0;
			for ( std::size_t b = 1; b + 1 < BINS; b++ )
			{
				if ( costs[ b ] < costs[ nBest ] )
				{
					nBest = b;
				}
			}
			const auto it = std::partition( order.begin() + pending.m_nBegin, order.begin() + pending.m_nEnd, [ & ]( std::uint32_t n ) { return static_cast<std::size_t>( binOf( n ) ) <= nBest; } );
			nMid = static_cast<std::uint32_t>( it - order.begin() );
			if ( nMid == pending.m_nBegin || nMid == pending.m_nEnd )
			{
				nMid = pending.m_nBegin + nHere / 2;
				std::nth_element( order.begin() + pending.m_nBegin, order.begin() + nMid, order.begin() + pending.m_nEnd, [ & ]( std::uint32_t a, std::uint32_t b ) { return centres[ a ][ nAxis ] < centres[ b ][ nAxis ]; } );
			}
		}
		if ( nHere <= LEAF || flExtent <= 0.0f )
		{
			node.m_nFirst = pending.m_nBegin;
			node.m_nCount = nHere;
			continue;
		}
		const auto nLeft = static_cast<std::uint32_t>( m_Nodes.size() );
		m_Nodes[ pending.m_nNode ].m_nFirst = nLeft;
		m_Nodes[ pending.m_nNode ].m_nCount = 0;
		m_Nodes.emplace_back();
		m_Nodes.emplace_back();
		stack.push_back( { nLeft, pending.m_nBegin, nMid } );
		stack.push_back( { nLeft + 1, nMid, pending.m_nEnd } );
	}
	// Leaves point into the triangles in the order the build left them.
	std::vector<TraceTriangle_t> sorted( nCount );
	for ( std::size_t i = 0; i < nCount; i++ )
	{
		sorted[ i ] = m_Triangles[ order[ i ] ];
	}
	m_Triangles = std::move( sorted );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CTracer::HitsBox( const BvhNode_t &node, const Ray_t &ray, float flMax )
{
	float flNear = 0.0f;
	float flFar = flMax;
	for ( std::size_t k = 0; k < 3; k++ )
	{
		float t0 = ( node.m_Mins[ k ] - ray.m_Origin[ k ] ) * ray.m_InvDir[ k ];
		float t1 = ( node.m_Maxs[ k ] - ray.m_Origin[ k ] ) * ray.m_InvDir[ k ];
		if ( std::isnan( t0 ) || std::isnan( t1 ) )
		{
			// Parallel to this slab and on its edge: inside it.
			continue;
		}
		if ( t0 > t1 )
		{
			std::swap( t0, t1 );
		}
		flNear = std::max( flNear, t0 );
		flFar = std::min( flFar, t1 );
		if ( flNear > flFar )
		{
			return false;
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Either side of the triangle counts.
//-----------------------------------------------------------------------------
bool CTracer::HitsTriangle( const TraceTriangle_t &triangle, const Ray_t &ray, float flMax, TraceHit_t &hit )
{
	const auto cross = []( const std::array<float, 3> &a, const std::array<float, 3> &b ) {
		return std::array<float, 3>{ a[ 1 ] * b[ 2 ] - a[ 2 ] * b[ 1 ], a[ 2 ] * b[ 0 ] - a[ 0 ] * b[ 2 ], a[ 0 ] * b[ 1 ] - a[ 1 ] * b[ 0 ] };
	};
	const auto dot = []( const std::array<float, 3> &a, const std::array<float, 3> &b ) { return a[ 0 ] * b[ 0 ] + a[ 1 ] * b[ 1 ] + a[ 2 ] * b[ 2 ]; };
	const std::array<float, 3> p = cross( ray.m_Dir, triangle.m_E2 );
	const float flDet = dot( triangle.m_E1, p );
	if ( std::abs( flDet ) < 1.0e-12f )
	{
		return false;
	}
	const float flInv = 1.0f / flDet;
	const std::array<float, 3> s{ ray.m_Origin[ 0 ] - triangle.m_V0[ 0 ], ray.m_Origin[ 1 ] - triangle.m_V0[ 1 ], ray.m_Origin[ 2 ] - triangle.m_V0[ 2 ] };
	const float u = dot( s, p ) * flInv;
	if ( u < 0.0f || u > 1.0f )
	{
		return false;
	}
	const std::array<float, 3> q = cross( s, triangle.m_E1 );
	const float v = dot( ray.m_Dir, q ) * flInv;
	if ( v < 0.0f || u + v > 1.0f )
	{
		return false;
	}
	const float t = dot( triangle.m_E2, q ) * flInv;
	if ( t <= 0.0f || t >= flMax )
	{
		return false;
	}
	hit.m_flDistance = t;
	hit.m_flU = u;
	hit.m_flV = v;
	// The mesh winds faces clockwise seen from outside, so e2 × e1 points out of the front, and
	// a ray meeting the front runs against it: the determinant, dir · (e2 × e1), is negative.
	hit.m_bFront = flDet < 0.0f;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CTracer::IsBlocked( const Ray_t &ray ) const
{
	if ( m_Nodes.empty() )
	{
		return false;
	}
	std::array<std::uint32_t, 64> stack{};
	std::size_t nTop = 0;
	stack[ nTop++ ] = 0;
	TraceHit_t hit;
	while ( nTop > 0 )
	{
		const BvhNode_t &node = m_Nodes[ stack[ --nTop ] ];
		if ( !HitsBox( node, ray, ray.m_flMax ) )
		{
			continue;
		}
		if ( node.m_nCount > 0 )
		{
			for ( std::uint32_t i = 0; i < node.m_nCount; i++ )
			{
				if ( HitsTriangle( m_Triangles[ node.m_nFirst + i ], ray, ray.m_flMax, hit ) )
				{
					return true;
				}
			}
			continue;
		}
		if ( nTop + 2 > stack.size() )
		{
			return false;
		}
		stack[ nTop++ ] = node.m_nFirst;
		stack[ nTop++ ] = node.m_nFirst + 1;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Visits the nearer child first, so far ones are mostly skipped.
//-----------------------------------------------------------------------------
std::optional<TraceHit_t> CTracer::Nearest( const Ray_t &ray ) const
{
	if ( m_Nodes.empty() )
	{
		return std::nullopt;
	}
	std::array<std::uint32_t, 64> stack{};
	std::size_t nTop = 0;
	stack[ nTop++ ] = 0;
	TraceHit_t best;
	bool bFound = false;
	float flMax = ray.m_flMax;
	TraceHit_t hit;
	while ( nTop > 0 )
	{
		const BvhNode_t &node = m_Nodes[ stack[ --nTop ] ];
		if ( !HitsBox( node, ray, flMax ) )
		{
			continue;
		}
		if ( node.m_nCount > 0 )
		{
			for ( std::uint32_t i = 0; i < node.m_nCount; i++ )
			{
				if ( HitsTriangle( m_Triangles[ node.m_nFirst + i ], ray, flMax, hit ) )
				{
					hit.m_nTriangle = node.m_nFirst + i;
					best = hit;
					flMax = hit.m_flDistance;
					bFound = true;
				}
			}
			continue;
		}
		if ( nTop + 2 > stack.size() )
		{
			break;
		}
		const BvhNode_t &left = m_Nodes[ node.m_nFirst ];
		const BvhNode_t &right = m_Nodes[ node.m_nFirst + 1 ];
		const auto centre = [ &ray ]( const BvhNode_t &n ) {
			float fl = 0.0f;
			for ( std::size_t k = 0; k < 3; k++ )
			{
				fl += ( ( n.m_Mins[ k ] + n.m_Maxs[ k ] ) * 0.5f - ray.m_Origin[ k ] ) * ray.m_Dir[ k ];
			}
			return fl;
		};
		if ( centre( left ) < centre( right ) )
		{
			stack[ nTop++ ] = node.m_nFirst + 1;
			stack[ nTop++ ] = node.m_nFirst;
		}
		else
		{
			stack[ nTop++ ] = node.m_nFirst;
			stack[ nTop++ ] = node.m_nFirst + 1;
		}
	}
	if ( !bFound )
	{
		return std::nullopt;
	}
	return best;
}

// A luxel's point on its face.
struct LuxelSample_t
{
	Point_t m_Position{};
	Point_t m_Normal{};
};

// A face being lit.
struct BakeFace_t
{
	const CMapMesh *m_pMesh = nullptr;
	std::uint32_t m_nFace = 0;      // in that mesh
	bool m_bInstance = false;
	std::uint32_t m_nMaterial = 0;
	bool m_bLit = false;
	bool m_bBlocks = false;
	bool m_bSky = false;
	CVec3 m_vecReflectivity;
	std::vector<std::uint32_t> m_Triangles; // into the mesh's triangles
	Box_t m_Bounds;
	std::optional<Plane_t> m_Plane;         // flat faces
	float m_flMinU = 0.0f, m_flMinV = 0.0f, m_flMaxU = 0.0f, m_flMaxV = 0.0f;
	int m_nWidth = 0;
	int m_nHeight = 0;
	int m_nX = 0;                            // in the atlas
	int m_nY = 0;
	std::vector<LuxelSample_t> m_Samples;    // m_nWidth by m_nHeight
	std::vector<std::uint32_t> m_Lights;     // the point and spot lights that may reach it
};

//-----------------------------------------------------------------------------
// Purpose: A face's luxel span at a step: luxels from the floor of its least to past its most.
//-----------------------------------------------------------------------------
int LuxelSpan( float flMin, float flMax, int nStep )
{
	return static_cast<int>( std::ceil( ( flMax - flMin ) / static_cast<float>( nStep ) - 1.0e-4f ) ) + 1;
}

//-----------------------------------------------------------------------------
// Purpose: Where a point in a triangle's luxel space is, as weights of its corners: inside it,
//          or the nearest point of its edges.
//-----------------------------------------------------------------------------
std::array<double, 3> LuxelWeights( const std::array<std::array<double, 2>, 3> &t, double u, double v, double &flOutside )
{
	const double flArea = ( t[ 1 ][ 0 ] - t[ 0 ][ 0 ] ) * ( t[ 2 ][ 1 ] - t[ 0 ][ 1 ] ) - ( t[ 2 ][ 0 ] - t[ 0 ][ 0 ] ) * ( t[ 1 ][ 1 ] - t[ 0 ][ 1 ] );
	if ( std::abs( flArea ) > 1.0e-12 )
	{
		const double w1 = ( ( u - t[ 0 ][ 0 ] ) * ( t[ 2 ][ 1 ] - t[ 0 ][ 1 ] ) - ( t[ 2 ][ 0 ] - t[ 0 ][ 0 ] ) * ( v - t[ 0 ][ 1 ] ) ) / flArea;
		const double w2 = ( ( t[ 1 ][ 0 ] - t[ 0 ][ 0 ] ) * ( v - t[ 0 ][ 1 ] ) - ( u - t[ 0 ][ 0 ] ) * ( t[ 1 ][ 1 ] - t[ 0 ][ 1 ] ) ) / flArea;
		const double w0 = 1.0 - w1 - w2;
		if ( w0 >= -1.0e-9 && w1 >= -1.0e-9 && w2 >= -1.0e-9 )
		{
			flOutside = 0.0;
			return { w0, w1, w2 };
		}
	}
	// Outside: the nearest point on the three edges.
	flOutside = std::numeric_limits<double>::infinity();
	std::array<double, 3> best{ 1.0, 0.0, 0.0 };
	for ( std::size_t e = 0; e < 3; e++ )
	{
		const std::size_t a = e;
		const std::size_t b = ( e + 1 ) % 3;
		const double dx = t[ b ][ 0 ] - t[ a ][ 0 ];
		const double dy = t[ b ][ 1 ] - t[ a ][ 1 ];
		const double flLength = dx * dx + dy * dy;
		const double s = flLength > 0.0 ? std::clamp( ( ( u - t[ a ][ 0 ] ) * dx + ( v - t[ a ][ 1 ] ) * dy ) / flLength, 0.0, 1.0 ) : 0.0;
		const double px = t[ a ][ 0 ] + dx * s - u;
		const double py = t[ a ][ 1 ] + dy * s - v;
		const double flDistance = px * px + py * py;
		if ( flDistance < flOutside )
		{
			flOutside = flDistance;
			best = { 0.0, 0.0, 0.0 };
			best[ a ] = 1.0 - s;
			best[ b ] = s;
		}
	}
	return best;
}

//-----------------------------------------------------------------------------
// Purpose: Where each luxel of a face is in the world: the face's triangles laid out in luxel
//          space, each luxel's point found in one of them (or the nearest edge, for luxels past
//          the face's outline) and placed in 3D, displaced like the triangle.
//-----------------------------------------------------------------------------
void PlaceSamples( BakeFace_t &face, int nStep )
{
	const CMapMesh &mesh = *face.m_pMesh;
	face.m_Samples.assign( static_cast<std::size_t>( face.m_nWidth ) * static_cast<std::size_t>( face.m_nHeight ), {} );
	std::vector<std::array<std::array<double, 2>, 3>> luxels( face.m_Triangles.size() );
	for ( std::size_t t = 0; t < face.m_Triangles.size(); t++ )
	{
		for ( std::size_t k = 0; k < 3; k++ )
		{
			const MeshVertex_t &vertex = mesh.m_Vertices[ mesh.m_Indices[ face.m_Triangles[ t ] * 3 + k ] ];
			luxels[ t ][ k ] = { vertex.m_Luxel[ 0 ], vertex.m_Luxel[ 1 ] };
		}
	}
	Point_t centre = face.m_Bounds.GetCenter();
	for ( int j = 0; j < face.m_nHeight; j++ )
	{
		for ( int i = 0; i < face.m_nWidth; i++ )
		{
			const double u = face.m_flMinU + static_cast<double>( i * nStep );
			const double v = face.m_flMinV + static_cast<double>( j * nStep );
			double flBest = std::numeric_limits<double>::infinity();
			std::size_t nBest = 0;
			std::array<double, 3> weights{ 1.0, 0.0, 0.0 };
			for ( std::size_t t = 0; t < luxels.size(); t++ )
			{
				double flOutside = 0.0;
				const std::array<double, 3> w = LuxelWeights( luxels[ t ], u, v, flOutside );
				if ( flOutside < flBest )
				{
					flBest = flOutside;
					nBest = t;
					weights = w;
					if ( flOutside == 0.0 )
					{
						break;
					}
				}
			}
			LuxelSample_t &sample = face.m_Samples[ static_cast<std::size_t>( j ) * static_cast<std::size_t>( face.m_nWidth ) + static_cast<std::size_t>( i ) ];
			if ( luxels.empty() )
			{
				continue;
			}
			Point_t position{};
			Point_t normal{};
			for ( std::size_t k = 0; k < 3; k++ )
			{
				const MeshVertex_t &vertex = mesh.m_Vertices[ mesh.m_Indices[ face.m_Triangles[ nBest ] * 3 + k ] ];
				position = Add( position, Scale( ToPoint( vertex.m_vecPosition ), weights[ k ] ) );
				normal = Add( normal, Scale( ToPoint( vertex.m_vecNormal ), weights[ k ] ) );
			}
			if ( flBest > 0.0 )
			{
				// A luxel past the outline sits on its edge; drawn in a little, so rays from it
				// don't start inside the solid next door.
				const Point_t inward = Subtract( centre, position );
				const double flLength = Length( inward );
				if ( flLength > 1.0e-6 )
				{
					position = Add( position, Scale( inward, std::min( 0.25, flLength * 0.5 ) / flLength ) );
				}
			}
			sample.m_Position = position;
			sample.m_Normal = Length( normal ) > 1.0e-9 ? Normalized( normal ) : Point_t{ 0, 0, 1 };
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: A small hash, so each luxel turns the same set of directions by its own angle and
//          a bake comes out the same every time.
//-----------------------------------------------------------------------------
std::uint32_t Hash( std::uint32_t n )
{
	n ^= n >> 16;
	n *= 0x7feb352du;
	n ^= n >> 15;
	n *= 0x846ca68bu;
	n ^= n >> 16;
	return n;
}

// Directions spread evenly over a hemisphere around +z, more of them toward the top as the
// cosine law weights them (so averaging what they see weights it by the cosine).
std::vector<Point_t> CosineDirections( int nCount )
{
	std::vector<Point_t> dirs;
	dirs.reserve( static_cast<std::size_t>( nCount ) );
	const double flGolden = std::numbers::pi * ( 3.0 - std::sqrt( 5.0 ) );
	for ( int i = 0; i < nCount; i++ )
	{
		const double r = std::sqrt( ( static_cast<double>( i ) + 0.5 ) / static_cast<double>( nCount ) );
		const double flAngle = flGolden * static_cast<double>( i );
		const double x = r * std::cos( flAngle );
		const double y = r * std::sin( flAngle );
		dirs.push_back( { x, y, std::sqrt( std::max( 0.0, 1.0 - r * r ) ) } );
	}
	return dirs;
}

//-----------------------------------------------------------------------------
// Purpose: Two axes across a normal.
//-----------------------------------------------------------------------------
void Basis( Point_t normal, Point_t &tangent, Point_t &bitangent )
{
	const Point_t up = std::abs( normal[ 2 ] ) < 0.9 ? Point_t{ 0, 0, 1 } : Point_t{ 1, 0, 0 };
	tangent = Normalized( Cross( up, normal ) );
	bitangent = Cross( normal, tangent );
}

//-----------------------------------------------------------------------------
// Purpose: One of the hemisphere's directions around a normal, turned by an angle.
//-----------------------------------------------------------------------------
Point_t AroundNormal( Point_t local, Point_t normal, Point_t tangent, Point_t bitangent, double flCos, double flSin )
{
	const double x = local[ 0 ] * flCos - local[ 1 ] * flSin;
	const double y = local[ 0 ] * flSin + local[ 1 ] * flCos;
	return Normalized( Add( Add( Scale( tangent, x ), Scale( bitangent, y ) ), Scale( normal, local[ 2 ] ) ) );
}

using Rgb_t = std::array<float, 3>;

// Everything the passes share.
struct Bake_t
{
	const LightingOptions_t *m_pOptions = nullptr;
	std::vector<Light_t> m_Lights;
	std::vector<BakeFace_t> m_Faces;
	CTracer m_Tracer;
	std::vector<bool> m_Unlit; // by the scene's materials: no lightmap by design
	int m_nStep = 1;
	int m_nWidth = 0;
	int m_nHeight = 0;

	[[nodiscard]] bool IsCancelled() const { return m_pOptions->m_pCancel && m_pOptions->m_pCancel->load( std::memory_order_relaxed ); }
	void Progress( int nPercent, std::string_view szStage ) const
	{
		if ( m_pOptions->m_pfnProgress )
		{
			m_pOptions->m_pfnProgress( nPercent, szStage );
		}
	}
	[[nodiscard]] std::size_t TexelIndex( const BakeFace_t &face, int i, int j ) const
	{
		return static_cast<std::size_t>( face.m_nY + j ) * static_cast<std::size_t>( m_nWidth ) + static_cast<std::size_t>( face.m_nX + i );
	}
	// Whether a ray along dir from a point reaches a sky face first.
	[[nodiscard]] bool SeesSky( Point_t from, Point_t dir ) const
	{
		const std::optional<TraceHit_t> hit = m_Tracer.Nearest( MakeRay( from, dir, RAY_LENGTH ) );
		return hit && m_Faces[ m_Tracer.GetTriangle( hit->m_nTriangle ).m_nFace ].m_bSky;
	}
	// A face's light in a buffer at a luxel position, bilinear between its luxels.
	[[nodiscard]] Rgb_t SampleFace( const std::vector<Rgb_t> &buffer, const BakeFace_t &face, float u, float v ) const
	{
		const float x = std::clamp( ( u - face.m_flMinU ) / static_cast<float>( m_nStep ), 0.0f, static_cast<float>( face.m_nWidth - 1 ) );
		const float y = std::clamp( ( v - face.m_flMinV ) / static_cast<float>( m_nStep ), 0.0f, static_cast<float>( face.m_nHeight - 1 ) );
		const int x0 = static_cast<int>( x );
		const int y0 = static_cast<int>( y );
		const int x1 = std::min( x0 + 1, face.m_nWidth - 1 );
		const int y1 = std::min( y0 + 1, face.m_nHeight - 1 );
		const float fx = x - static_cast<float>( x0 );
		const float fy = y - static_cast<float>( y0 );
		Rgb_t out{};
		const Rgb_t &a = buffer[ TexelIndex( face, x0, y0 ) ];
		const Rgb_t &b = buffer[ TexelIndex( face, x1, y0 ) ];
		const Rgb_t &c = buffer[ TexelIndex( face, x0, y1 ) ];
		const Rgb_t &d = buffer[ TexelIndex( face, x1, y1 ) ];
		for ( std::size_t k = 0; k < 3; k++ )
		{
			out[ k ] = ( a[ k ] * ( 1.0f - fx ) + b[ k ] * fx ) * ( 1.0f - fy ) + ( c[ k ] * ( 1.0f - fx ) + d[ k ] * fx ) * fy;
		}
		return out;
	}
};

//-----------------------------------------------------------------------------
// Purpose: The light that reaches one luxel straight from the lights: each point and spot
//          light that isn't blocked, and the sun where the sky is in its direction.
//-----------------------------------------------------------------------------
Rgb_t DirectLight( const Bake_t &bake, std::span<const std::uint32_t> lights, const LuxelSample_t &sample, std::uint32_t nSeed )
{
	Rgb_t total{};
	const Point_t from = Add( sample.m_Position, Scale( sample.m_Normal, RAY_OFFSET ) );
	const auto add = [ &total ]( CVec3 vec, double flScale ) {
		total[ 0 ] += static_cast<float>( vec.m_flX * flScale );
		total[ 1 ] += static_cast<float>( vec.m_flY * flScale );
		total[ 2 ] += static_cast<float>( vec.m_flZ * flScale );
	};
	for ( const std::uint32_t nLight : lights )
	{
		const Light_t &light = bake.m_Lights[ nLight ];
		const CVec3 vecLight = PointLightAt( light, sample.m_Position, sample.m_Normal );
		if ( Brightest( vecLight ) < LIGHT_THRESHOLD )
		{
			continue;
		}
		const Point_t toLight = Subtract( light.m_Origin, from );
		const double flDistance = Length( toLight );
		if ( flDistance > 1.0e-6 && bake.m_Tracer.IsBlocked( MakeRay( from, Scale( toLight, 1.0 / flDistance ), flDistance ) ) )
		{
			continue;
		}
		add( vecLight, 1.0 );
	}
	const double flTurn = static_cast<double>( Hash( nSeed ) ) / 4294967296.0 * 2.0 * std::numbers::pi;
	for ( const Light_t &light : bake.m_Lights )
	{
		if ( light.m_nType == LightType_t::LIGHT_TYPE_SUN )
		{
			const Point_t toSun = Scale( light.m_Direction, -1.0 );
			const double flDot = Dot( toSun, sample.m_Normal );
			if ( flDot <= 0.0 )
			{
				continue;
			}
			if ( light.m_flSunSpread <= 0.0 )
			{
				if ( bake.SeesSky( from, toSun ) )
				{
					add( light.m_vecIntensity, flDot );
				}
				continue;
			}
			// A sun with a size: rays spread over its disc, the share that see sky.
			Point_t sunTangent{}, sunBitangent{};
			Basis( toSun, sunTangent, sunBitangent );
			int nSeen = 0;
			for ( int s = 0; s < SUN_SPREAD_SAMPLES; s++ )
			{
				const double r = std::sqrt( ( s + 0.5 ) / SUN_SPREAD_SAMPLES ) * light.m_flSunSpread;
				const double a = flTurn + s * 2.399963229728653;
				const Point_t dir = Normalized( Add( toSun, Add( Scale( sunTangent, r * std::cos( a ) ), Scale( sunBitangent, r * std::sin( a ) ) ) ) );
				nSeen += bake.SeesSky( from, dir ) ? 1 : 0;
			}
			add( light.m_vecIntensity, flDot * nSeen / SUN_SPREAD_SAMPLES );
		}
	}
	return total;
}

//-----------------------------------------------------------------------------
// Purpose: The sky's ambient light at a point: the share of rays over its hemisphere (weighted
//          by the cosine law) that reach a sky face.
//-----------------------------------------------------------------------------
Rgb_t SkyAmbient( const Bake_t &bake, const LuxelSample_t &sample, std::uint32_t nSeed, const std::vector<Point_t> &dirs )
{
	const auto ambient = std::ranges::find( bake.m_Lights, LightType_t::LIGHT_TYPE_SKY_AMBIENT, &Light_t::m_nType );
	if ( ambient == bake.m_Lights.end() || dirs.empty() )
	{
		return {};
	}
	const Point_t from = Add( sample.m_Position, Scale( sample.m_Normal, RAY_OFFSET ) );
	Point_t tangent{}, bitangent{};
	Basis( sample.m_Normal, tangent, bitangent );
	const double flTurn = static_cast<double>( Hash( nSeed ) ) / 4294967296.0 * 2.0 * std::numbers::pi;
	const double flCos = std::cos( flTurn );
	const double flSin = std::sin( flTurn );
	int nSeen = 0;
	for ( const Point_t &local : dirs )
	{
		nSeen += bake.SeesSky( from, AroundNormal( local, sample.m_Normal, tangent, bitangent, flCos, flSin ) ) ? 1 : 0;
	}
	const float flShare = static_cast<float>( nSeen ) / static_cast<float>( dirs.size() );
	return { ambient->m_vecIntensity.m_flX * flShare, ambient->m_vecIntensity.m_flY * flShare, ambient->m_vecIntensity.m_flZ * flShare };
}

//-----------------------------------------------------------------------------
// Purpose: Light that changes slowly across a face, gathered on every nSpacing-th luxel (and the
//          last row and column) by gather(sample, seed), then smoothed back over every luxel
//          between the four gathered around it, and added to out.
//-----------------------------------------------------------------------------
template <typename Gather>
void GatherSparse( const Bake_t &bake, int nSpacing, std::vector<Rgb_t> &out, Gather &&gather )
{
	ParallelFor( bake.m_Faces.size(), 16, [ & ]( std::size_t nBegin, std::size_t nEnd ) {
		std::vector<Rgb_t> coarse;
		for ( std::size_t f = nBegin; f < nEnd && !bake.IsCancelled(); f++ )
		{
			const BakeFace_t &face = bake.m_Faces[ f ];
			if ( !face.m_bLit )
			{
				continue;
			}
			// A face no wider than the spacing gets one gather across, in its middle.
			const auto count = [ nSpacing ]( int nSize ) { return nSize <= nSpacing ? 1 : ( nSize - 1 + nSpacing - 1 ) / nSpacing + 1; };
			const int nCoarseW = count( face.m_nWidth );
			const int nCoarseH = count( face.m_nHeight );
			const auto at = [ nSpacing ]( int n, int nSize ) { return nSize <= nSpacing ? nSize / 2 : std::min( n * nSpacing, nSize - 1 ); };
			coarse.assign( static_cast<std::size_t>( nCoarseW * nCoarseH ), Rgb_t{} );
			for ( int cj = 0; cj < nCoarseH; cj++ )
			{
				for ( int ci = 0; ci < nCoarseW; ci++ )
				{
					const int i = at( ci, face.m_nWidth );
					const int j = at( cj, face.m_nHeight );
					coarse[ static_cast<std::size_t>( cj * nCoarseW + ci ) ] = gather( face.m_Samples[ static_cast<std::size_t>( j * face.m_nWidth + i ) ], static_cast<std::uint32_t>( bake.TexelIndex( face, i, j ) ) );
				}
			}
			for ( int j = 0; j < face.m_nHeight; j++ )
			{
				const int cj0 = std::min( j / nSpacing, nCoarseH - 1 );
				const int cj1 = std::min( cj0 + 1, nCoarseH - 1 );
				const int j0 = at( cj0, face.m_nHeight );
				const int j1 = at( cj1, face.m_nHeight );
				const float ty = j1 > j0 ? static_cast<float>( j - j0 ) / static_cast<float>( j1 - j0 ) : 0.0f;
				for ( int i = 0; i < face.m_nWidth; i++ )
				{
					const int ci0 = std::min( i / nSpacing, nCoarseW - 1 );
					const int ci1 = std::min( ci0 + 1, nCoarseW - 1 );
					const int i0 = at( ci0, face.m_nWidth );
					const int i1 = at( ci1, face.m_nWidth );
					const float tx = i1 > i0 ? static_cast<float>( i - i0 ) / static_cast<float>( i1 - i0 ) : 0.0f;
					const Rgb_t &a = coarse[ static_cast<std::size_t>( cj0 * nCoarseW + ci0 ) ];
					const Rgb_t &b = coarse[ static_cast<std::size_t>( cj0 * nCoarseW + ci1 ) ];
					const Rgb_t &c = coarse[ static_cast<std::size_t>( cj1 * nCoarseW + ci0 ) ];
					const Rgb_t &d = coarse[ static_cast<std::size_t>( cj1 * nCoarseW + ci1 ) ];
					Rgb_t &texel = out[ bake.TexelIndex( face, i, j ) ];
					for ( std::size_t k = 0; k < 3; k++ )
					{
						texel[ k ] += ( a[ k ] * ( 1.0f - tx ) + b[ k ] * tx ) * ( 1.0f - ty ) + ( c[ k ] * ( 1.0f - tx ) + d[ k ] * tx ) * ty;
					}
				}
			}
		}
	} );
}

//-----------------------------------------------------------------------------
// Purpose: The light one bounce brings to a point: rays over its hemisphere, each adding what
//          the face it meets reflects of the light that face holds in last.
//-----------------------------------------------------------------------------
Rgb_t GatherBounce( const Bake_t &bake, const std::vector<Rgb_t> &last, const LuxelSample_t &sample, std::uint32_t nSeed, const std::vector<Point_t> &dirs )
{
	Rgb_t total{};
	const Point_t from = Add( sample.m_Position, Scale( sample.m_Normal, RAY_OFFSET ) );
	Point_t tangent{}, bitangent{};
	Basis( sample.m_Normal, tangent, bitangent );
	const double flTurn = static_cast<double>( Hash( nSeed ) ) / 4294967296.0 * 2.0 * std::numbers::pi;
	const double flCos = std::cos( flTurn );
	const double flSin = std::sin( flTurn );
	for ( const Point_t &local : dirs )
	{
		const std::optional<TraceHit_t> hit = bake.m_Tracer.Nearest( MakeRay( from, AroundNormal( local, sample.m_Normal, tangent, bitangent, flCos, flSin ), RAY_LENGTH ) );
		if ( !hit || !hit->m_bFront )
		{
			continue;
		}
		const TraceTriangle_t &triangle = bake.m_Tracer.GetTriangle( hit->m_nTriangle );
		const BakeFace_t &face = bake.m_Faces[ triangle.m_nFace ];
		if ( !face.m_bLit || face.m_nWidth == 0 )
		{
			continue;
		}
		const float w0 = 1.0f - hit->m_flU - hit->m_flV;
		const float u = triangle.m_Luxels[ 0 ][ 0 ] * w0 + triangle.m_Luxels[ 1 ][ 0 ] * hit->m_flU + triangle.m_Luxels[ 2 ][ 0 ] * hit->m_flV;
		const float v = triangle.m_Luxels[ 0 ][ 1 ] * w0 + triangle.m_Luxels[ 1 ][ 1 ] * hit->m_flU + triangle.m_Luxels[ 2 ][ 1 ] * hit->m_flV;
		const Rgb_t there = bake.SampleFace( last, face, u, v );
		total[ 0 ] += there[ 0 ] * face.m_vecReflectivity.m_flX;
		total[ 1 ] += there[ 1 ] * face.m_vecReflectivity.m_flY;
		total[ 2 ] += there[ 2 ] * face.m_vecReflectivity.m_flZ;
	}
	const float flShare = dirs.empty() ? 0.0f : 1.0f / static_cast<float>( dirs.size() );
	return { total[ 0 ] * flShare, total[ 1 ] * flShare, total[ 2 ] * flShare };
}

//-----------------------------------------------------------------------------
// Purpose: The faces to light, from the map's solids as they stand and the instances' mesh.
//          Blocking follows vrad: the world's opaque solids (func_detail is the world's to
//          vrad) and brush entities with vrad_brush_cast_shadows; displacements too.
//-----------------------------------------------------------------------------
void CollectFaces( const CScene &scene, const CMapMesh &live, std::span<const LightMaterial_t> materials, Bake_t &bake )
{
	const auto add = [ & ]( const CMapMesh &mesh, bool bInstance ) {
		std::vector<std::vector<std::uint32_t>> triangles( mesh.m_Faces.size() );
		for ( std::size_t t = 0; t < mesh.m_TriangleFaces.size(); t++ )
		{
			triangles[ mesh.m_TriangleFaces[ t ] ].push_back( static_cast<std::uint32_t>( t ) );
		}
		for ( std::size_t f = 0; f < mesh.m_Faces.size(); f++ )
		{
			const MeshFace_t &meshFace = mesh.m_Faces[ f ];
			BakeFace_t face;
			face.m_pMesh = &mesh;
			face.m_nFace = static_cast<std::uint32_t>( f );
			face.m_bInstance = bInstance;
			face.m_nMaterial = meshFace.m_nMaterial;
			const LightMaterial_t material = meshFace.m_nMaterial < materials.size() ? materials[ meshFace.m_nMaterial ] : LightMaterial_t{};
			face.m_bLit = material.m_bLit;
			face.m_bSky = material.m_bSky;
			face.m_vecReflectivity = material.m_vecReflectivity;
			bool bCasts = true;
			if ( !bInstance && meshFace.m_nSolid < scene.m_Map.m_Solids.size() )
			{
				const int nEntity = std::as_const( scene.m_Map.m_Solids )[ meshFace.m_nSolid ].m_nEntity;
				if ( nEntity >= 0 && static_cast<std::size_t>( nEntity ) < scene.m_Map.m_Entities.size() )
				{
					const Entity_t &entity = std::as_const( scene.m_Map.m_Entities )[ static_cast<std::size_t>( nEntity ) ];
					// vbsp folds these into the world, so they block light in the game.
					const std::string_view szClass = entity.GetClassname();
					bCasts = szClass.starts_with( "func_detail" ) || szClass == "func_group" || szClass == "func_nobevel" || KeyNumber( entity, "vrad_brush_cast_shadows" ) != 0.0;
				}
			}
			face.m_bBlocks = material.m_bBlocks && bCasts;
			face.m_Triangles = std::move( triangles[ f ] );
			for ( std::uint32_t v = 0; v < meshFace.m_nVertexCount; v++ )
			{
				const MeshVertex_t &vertex = mesh.m_Vertices[ meshFace.m_nFirstVertex + v ];
				face.m_Bounds.Add( ToPoint( vertex.m_vecPosition ) );
				if ( v == 0 )
				{
					face.m_flMinU = face.m_flMaxU = vertex.m_Luxel[ 0 ];
					face.m_flMinV = face.m_flMaxV = vertex.m_Luxel[ 1 ];
				}
				face.m_flMinU = std::min( face.m_flMinU, vertex.m_Luxel[ 0 ] );
				face.m_flMaxU = std::max( face.m_flMaxU, vertex.m_Luxel[ 0 ] );
				face.m_flMinV = std::min( face.m_flMinV, vertex.m_Luxel[ 1 ] );
				face.m_flMaxV = std::max( face.m_flMaxV, vertex.m_Luxel[ 1 ] );
			}
			if ( meshFace.m_nVertexCount > 0 && !meshFace.m_bDisplacement )
			{
				const MeshVertex_t &first = mesh.m_Vertices[ meshFace.m_nFirstVertex ];
				const Point_t normal = ToPoint( first.m_vecNormal );
				face.m_Plane = Plane_t{ normal, Dot( normal, ToPoint( first.m_vecPosition ) ) };
			}
			face.m_flMinU = std::floor( face.m_flMinU );
			face.m_flMinV = std::floor( face.m_flMinV );
			if ( !face.m_bLit && !face.m_bBlocks )
			{
				continue;
			}
			bake.m_Faces.push_back( std::move( face ) );
		}
	};
	add( live, false );
	add( scene.GetInstanceMesh(), true );
}

//-----------------------------------------------------------------------------
// Purpose: Rows of rectangles, tallest first, in an atlas as wide as asked; false when it would
//          be taller than nMaxHeight.
//-----------------------------------------------------------------------------
bool PackAtlas( std::vector<BakeFace_t> &faces, int nStep, int nWidth, int nMaxHeight, int &nHeight )
{
	std::vector<std::size_t> order;
	for ( std::size_t i = 0; i < faces.size(); i++ )
	{
		if ( faces[ i ].m_bLit )
		{
			faces[ i ].m_nWidth = LuxelSpan( faces[ i ].m_flMinU, faces[ i ].m_flMaxU, nStep );
			faces[ i ].m_nHeight = LuxelSpan( faces[ i ].m_flMinV, faces[ i ].m_flMaxV, nStep );
			if ( faces[ i ].m_nWidth > nWidth || faces[ i ].m_nHeight > nMaxHeight )
			{
				return false;
			}
			order.push_back( i );
		}
	}
	std::ranges::sort( order, [ & ]( std::size_t a, std::size_t b ) { return faces[ a ].m_nHeight != faces[ b ].m_nHeight ? faces[ a ].m_nHeight > faces[ b ].m_nHeight : a < b; } );
	int x = 0;
	int y = 0;
	int nRow = 0;
	for ( const std::size_t i : order )
	{
		BakeFace_t &face = faces[ i ];
		if ( x + face.m_nWidth > nWidth )
		{
			y += nRow;
			x = 0;
			nRow = 0;
		}
		face.m_nX = x;
		face.m_nY = y;
		x += face.m_nWidth;
		nRow = std::max( nRow, face.m_nHeight );
		if ( y + nRow > nMaxHeight )
		{
			return false;
		}
	}
	nHeight = std::max( 1, y + nRow );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: The point and spot lights that may reach a box.
//-----------------------------------------------------------------------------
std::vector<std::uint32_t> LightsReaching( const Bake_t &bake, const Box_t &bounds )
{
	std::vector<std::uint32_t> lights;
	for ( std::uint32_t l = 0; l < bake.m_Lights.size(); l++ )
	{
		const Light_t &light = bake.m_Lights[ l ];
		if ( light.m_nType != LightType_t::LIGHT_TYPE_POINT && light.m_nType != LightType_t::LIGHT_TYPE_SPOT )
		{
			continue;
		}
		double flOutside = 0.0;
		for ( std::size_t k = 0; k < 3; k++ )
		{
			const double flGap = std::max( { bounds.m_Mins[ k ] - light.m_Origin[ k ], 0.0, light.m_Origin[ k ] - bounds.m_Maxs[ k ] } );
			flOutside += flGap * flGap;
		}
		if ( flOutside <= light.m_flReach * light.m_flReach )
		{
			lights.push_back( l );
		}
	}
	return lights;
}

//-----------------------------------------------------------------------------
// Purpose: All the light at a point facing a way, as a face's luxel there would get it: straight
//          from the lights, the sky's ambient, and one gather of what the faces around hold in
//          total (which has every bounce in it).
//-----------------------------------------------------------------------------
Rgb_t LightPoint( const Bake_t &bake, const std::vector<Rgb_t> &total, std::span<const std::uint32_t> lights, const LuxelSample_t &sample, std::uint32_t nSeed, const std::vector<Point_t> &skyDirs, const std::vector<Point_t> &bounceDirs )
{
	Rgb_t light = DirectLight( bake, lights, sample, nSeed );
	const Rgb_t sky = SkyAmbient( bake, sample, nSeed, skyDirs );
	const Rgb_t bounced = GatherBounce( bake, total, sample, nSeed ^ 0x51ed270bu, bounceDirs );
	for ( std::size_t k = 0; k < 3; k++ )
	{
		light[ k ] += sky[ k ] + bounced[ k ];
	}
	return light;
}

//-----------------------------------------------------------------------------
// Purpose: Each prop's vertices lit where they are, facing their normals (vrad's static prop
//          lighting does much the same per vertex).
//-----------------------------------------------------------------------------
std::vector<LightVertex_t> LightPropVertices( const Bake_t &bake, const std::vector<Rgb_t> &total, const CMapMesh &mesh )
{
	std::vector<LightVertex_t> out( mesh.m_Vertices.size(), LIGHT_VERTEX_NONE );
	const std::vector<Point_t> skyDirs = CosineDirections( 24 );
	const std::vector<Point_t> bounceDirs = CosineDirections( 16 );
	// Lights by the solid's part of the mesh would be finer; a mesh's box is enough for props.
	Box_t bounds;
	for ( const MeshVertex_t &vertex : mesh.m_Vertices )
	{
		bounds.Add( ToPoint( vertex.m_vecPosition ) );
	}
	const std::vector<std::uint32_t> lights = LightsReaching( bake, bounds );
	ParallelFor( mesh.m_Vertices.size(), 256, [ & ]( std::size_t nBegin, std::size_t nEnd ) {
		for ( std::size_t v = nBegin; v < nEnd && !bake.IsCancelled(); v++ )
		{
			const MeshVertex_t &vertex = mesh.m_Vertices[ v ];
			const Point_t normal = ToPoint( vertex.m_vecNormal );
			const LuxelSample_t sample{ ToPoint( vertex.m_vecPosition ), Length( normal ) > 1.0e-6 ? Normalized( normal ) : Point_t{ 0, 0, 1 } };
			const Rgb_t light = LightPoint( bake, total, lights, sample, static_cast<std::uint32_t>( v ) * 2654435761u, skyDirs, bounceDirs );
			out[ v ] = { light[ 0 ] / 255.0f, light[ 1 ] / 255.0f, light[ 2 ] / 255.0f, LIGHT_KIND_VERTEX };
		}
	} );
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Overlays and decals take the light of the face they lie on, from its lightmap, so
//          a shadow's edge crosses them as it crosses the face. Each triangle finds its face
//          below its middle; on a flat face its corners' luxels follow the face's (they are
//          linear across its plane), clamped to the face's luxels. Elsewhere (a displacement,
//          or no face found) each corner takes the light where it is.
//-----------------------------------------------------------------------------
std::vector<LightVertex_t> LightOverlayVertices( const Bake_t &bake, const CTracer &surfaces, const std::vector<Rgb_t> &total, const CMapMesh &mesh )
{
	std::vector<LightVertex_t> out( mesh.m_Vertices.size(), LIGHT_VERTEX_NONE );
	const float flStep = static_cast<float>( bake.m_nStep );
	auto down = [ & ]( Point_t at, Point_t normal ) { return surfaces.Nearest( MakeRay( Add( at, Scale( normal, 2.0 ) ), Scale( normal, -1.0 ), 6.0 ) ); };
	auto luxelOf = []( const TraceTriangle_t &triangle, Point_t p ) {
		// Barycentric weights of p in the triangle's plane (outside it too).
		const Point_t v0{ triangle.m_V0[ 0 ], triangle.m_V0[ 1 ], triangle.m_V0[ 2 ] };
		const Point_t e1{ triangle.m_E1[ 0 ], triangle.m_E1[ 1 ], triangle.m_E1[ 2 ] };
		const Point_t e2{ triangle.m_E2[ 0 ], triangle.m_E2[ 1 ], triangle.m_E2[ 2 ] };
		const Point_t d = Subtract( p, v0 );
		const double a = Dot( e1, e1 ), b = Dot( e1, e2 ), c = Dot( e2, e2 ), d1 = Dot( d, e1 ), d2 = Dot( d, e2 );
		const double flDet = a * c - b * b;
		const double u = flDet != 0.0 ? ( c * d1 - b * d2 ) / flDet : 0.0;
		const double v = flDet != 0.0 ? ( a * d2 - b * d1 ) / flDet : 0.0;
		const double w = 1.0 - u - v;
		return std::array<float, 2>{ static_cast<float>( triangle.m_Luxels[ 0 ][ 0 ] * w + triangle.m_Luxels[ 1 ][ 0 ] * u + triangle.m_Luxels[ 2 ][ 0 ] * v ), static_cast<float>( triangle.m_Luxels[ 0 ][ 1 ] * w + triangle.m_Luxels[ 1 ][ 1 ] * u + triangle.m_Luxels[ 2 ][ 1 ] * v ) };
	};
	auto lightHere = [ & ]( Point_t at, Point_t normal ) -> LightVertex_t {
		if ( const auto hit = down( at, normal ) )
		{
			const TraceTriangle_t &triangle = surfaces.GetTriangle( hit->m_nTriangle );
			const BakeFace_t &face = bake.m_Faces[ triangle.m_nFace ];
			const std::array<float, 2> luxel = luxelOf( triangle, Add( Add( at, Scale( normal, 2.0 ) ), Scale( normal, -hit->m_flDistance ) ) );
			const Rgb_t light = bake.SampleFace( total, face, luxel[ 0 ], luxel[ 1 ] );
			return { light[ 0 ] / 255.0f, light[ 1 ] / 255.0f, light[ 2 ] / 255.0f, LIGHT_KIND_VERTEX };
		}
		return LIGHT_VERTEX_NONE;
	};
	const std::size_t nTriangles = mesh.m_Indices.size() / 3;
	ParallelFor( nTriangles, 64, [ & ]( std::size_t nBegin, std::size_t nEnd ) {
		for ( std::size_t t = nBegin; t < nEnd && !bake.IsCancelled(); t++ )
		{
			std::array<Point_t, 3> corners;
			std::array<std::uint32_t, 3> indices{};
			Point_t centre{};
			for ( std::size_t k = 0; k < 3; k++ )
			{
				indices[ k ] = mesh.m_Indices[ t * 3 + k ];
				corners[ k ] = ToPoint( mesh.m_Vertices[ indices[ k ] ].m_vecPosition );
				centre = Add( centre, Scale( corners[ k ], 1.0 / 3.0 ) );
			}
			Point_t normal = ToPoint( mesh.m_Vertices[ indices[ 0 ] ].m_vecNormal );
			normal = Length( normal ) > 1.0e-6 ? Normalized( normal ) : Point_t{ 0, 0, 1 };
			const auto hit = down( centre, normal );
			const BakeFace_t *pFace = hit ? &bake.m_Faces[ surfaces.GetTriangle( hit->m_nTriangle ).m_nFace ] : nullptr;
			if ( pFace && pFace->m_Plane )
			{
				const TraceTriangle_t &triangle = surfaces.GetTriangle( hit->m_nTriangle );
				for ( std::size_t k = 0; k < 3; k++ )
				{
					// Onto the face's plane, then its luxels, then the atlas.
					const Point_t onFace = Subtract( corners[ k ], Scale( pFace->m_Plane->m_Normal, pFace->m_Plane->Distance( corners[ k ] ) ) );
					const std::array<float, 2> luxel = luxelOf( triangle, onFace );
					const float x = std::clamp( static_cast<float>( pFace->m_nX ) + ( luxel[ 0 ] - pFace->m_flMinU ) / flStep + 0.5f, static_cast<float>( pFace->m_nX ) + 0.5f, static_cast<float>( pFace->m_nX + pFace->m_nWidth ) - 0.5f );
					const float y = std::clamp( static_cast<float>( pFace->m_nY ) + ( luxel[ 1 ] - pFace->m_flMinV ) / flStep + 0.5f, static_cast<float>( pFace->m_nY ) + 0.5f, static_cast<float>( pFace->m_nY + pFace->m_nHeight ) - 0.5f );
					out[ indices[ k ] ] = { x, y, 0.0f, LIGHT_KIND_LIGHTMAP };
				}
				continue;
			}
			for ( std::size_t k = 0; k < 3; k++ )
			{
				out[ indices[ k ] ] = lightHere( corners[ k ], normal );
			}
		}
	} );
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: The atlas from the light gathered (bounced may be empty), with where each face is.
//-----------------------------------------------------------------------------
std::shared_ptr<CLighting> MakeResult( const CScene &scene, const Bake_t &bake, const std::vector<Rgb_t> &direct, const std::vector<Rgb_t> &bounced )
{
	auto pLighting = std::make_shared<CLighting>();
	pLighting->m_nWidth = bake.m_nWidth;
	pLighting->m_nHeight = bake.m_nHeight;
	pLighting->m_nStep = bake.m_nStep;
	pLighting->m_nLights = bake.m_Lights.size();
	pLighting->m_nRevision = scene.m_nRevision;
	pLighting->m_pInstanceMesh = scene.m_pInstanceMesh;
	pLighting->m_InstanceFaces.assign( scene.GetInstanceMesh().m_Faces.size(), LightmapRect_t{} );
	pLighting->m_Unlit = bake.m_Unlit;
	pLighting->m_Texels.assign( direct.size() * 4, 0 );
	for ( std::size_t n = 0; n < direct.size(); n++ )
	{
		for ( std::size_t k = 0; k < 3; k++ )
		{
			const float flBounced = bounced.empty() ? 0.0f : bounced[ n ][ k ];
			pLighting->m_Texels[ n * 4 + k ] = FloatToHalf( ( direct[ n ][ k ] + flBounced ) / 255.0f );
		}
		pLighting->m_Texels[ n * 4 + 3 ] = FloatToHalf( 1.0f );
	}
	for ( const BakeFace_t &face : bake.m_Faces )
	{
		if ( !face.m_bLit || face.m_nWidth == 0 )
		{
			continue;
		}
		const MeshFace_t &meshFace = face.m_pMesh->m_Faces[ face.m_nFace ];
		const CVec3 vecFirst = meshFace.m_nVertexCount > 0 ? face.m_pMesh->m_Vertices[ meshFace.m_nFirstVertex ].m_vecPosition : CVec3{};
		LightmapRect_t rect{ face.m_nX, face.m_nY, face.m_nWidth, face.m_nHeight, face.m_flMinU, face.m_flMinV, face.m_flMaxU, face.m_flMaxV, vecFirst };
		if ( face.m_bInstance )
		{
			pLighting->m_InstanceFaces[ face.m_nFace ] = rect;
		}
		else
		{
			pLighting->m_Faces[ FaceRef_t{ meshFace.m_nSolid, meshFace.m_nSide } ] = rect;
		}
	}
	return pLighting;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<CVec3> ParseLightValue( std::string_view szValue )
{
	std::array<double, 4> values{};
	int nCount = 0;
	std::size_t nAt = 0;
	while ( nAt < szValue.size() && nCount < 4 )
	{
		while ( nAt < szValue.size() && ( szValue[ nAt ] == ' ' || szValue[ nAt ] == '\t' ) )
		{
			nAt++;
		}
		std::size_t nEnd = nAt;
		while ( nEnd < szValue.size() && szValue[ nEnd ] != ' ' && szValue[ nEnd ] != '\t' )
		{
			nEnd++;
		}
		if ( nEnd == nAt )
		{
			break;
		}
		const std::optional<double> value = ParseNumber<double>( szValue.substr( nAt, nEnd - nAt ) );
		if ( !value )
		{
			return std::nullopt;
		}
		values[ static_cast<std::size_t>( nCount++ ) ] = *value;
		nAt = nEnd;
	}
	if ( nCount != 1 && nCount != 3 && nCount != 4 )
	{
		return std::nullopt;
	}
	if ( values[ 0 ] < 0.0 || values[ 1 ] < 0.0 || values[ 2 ] < 0.0 || values[ 3 ] < 0.0 )
	{
		// What vrad does with a negative value: no light (and how HDR's "-1 -1 -1 1" says unset).
		return std::nullopt;
	}
	const auto linear = []( double fl ) { return std::pow( fl / 255.0, 2.2 ) * 255.0; };
	CVec3 vec;
	vec.m_flX = static_cast<float>( linear( values[ 0 ] ) );
	vec.m_flY = nCount == 1 ? vec.m_flX : static_cast<float>( linear( values[ 1 ] ) );
	vec.m_flZ = nCount == 1 ? vec.m_flX : static_cast<float>( linear( values[ 2 ] ) );
	if ( nCount == 4 )
	{
		vec *= static_cast<float>( values[ 3 ] / 255.0 );
	}
	return vec;
}

//-----------------------------------------------------------------------------
// Purpose: The quadratic c + b d + a d² through 1 at the light, 2 at d50 and 256 at d0. A
//          quadratic through those that starts out falling isn't wanted (the light would get
//          brighter going away), so the middle point is eased toward the straight line until
//          it rises from the start; then it is scaled to give exactly 2 at d50. One that turns
//          back further out is capped there by ReadFalloff.
//-----------------------------------------------------------------------------
std::array<double, 3> FalloffForDistances( double d50, double d0 )
{
	double a = 0.0;
	double b = 1.0;
	double c = 0.0;
	if ( d50 <= 0.0 || d0 <= d50 )
	{
		return { c, b, a };
	}
	const double flLine = 1.0 + 255.0 * d50 / d0;
	for ( double flEase = 0.0; flEase <= 1.0 + 1.0e-9; flEase += 0.05 )
	{
		const double y50 = 2.0 + ( flLine - 2.0 ) * flEase;
		// c = 1; b d50 + a d50² = y50 - 1; b d0 + a d0² = 255.
		const double flDet = d50 * d0 * d0 - d0 * d50 * d50;
		if ( std::abs( flDet ) < 1.0e-12 )
		{
			break;
		}
		const double r1 = y50 - 1.0;
		const double r2 = 255.0;
		c = 1.0;
		b = ( r1 * d0 * d0 - r2 * d50 * d50 ) / flDet;
		a = ( d50 * r2 - d0 * r1 ) / flDet;
		if ( b + 2.0 * a >= 0.0 )
		{
			break;
		}
	}
	const double v50 = c + d50 * ( b + d50 * a );
	if ( v50 > 0.0 )
	{
		const double flScale = 2.0 / v50;
		a *= flScale;
		b *= flScale;
		c *= flScale;
	}
	return { c, b, a };
}

//-----------------------------------------------------------------------------
// Purpose: The quintic fade vrad eases a hard falloff with.
//-----------------------------------------------------------------------------
double LightFalloff( const Light_t &light, double flDistance )
{
	const bool bFades = light.m_flFadeEnd > light.m_flFadeStart;
	if ( bFades && flDistance > light.m_flFadeEnd )
	{
		return 0.0;
	}
	const double d = std::max( flDistance, MIN_DISTANCE );
	const double e = std::min( d, light.m_flCapDistance );
	const double flDivisor = light.m_flConstant + light.m_flLinear * e + light.m_flQuadratic * e * e;
	double flFalloff = flDivisor > 0.0 ? 1.0 / flDivisor : 0.0;
	if ( bFades )
	{
		double t = std::clamp( ( d - light.m_flFadeStart ) / ( light.m_flFadeEnd - light.m_flFadeStart ), 0.0, 1.0 );
		t = 1.0 - t;
		flFalloff *= t * t * t * ( t * ( t * 6.0 - 15.0 ) + 10.0 );
	}
	return flFalloff;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVec3 PointLightAt( const Light_t &light, Point_t point, Point_t normal )
{
	const Point_t delta = Subtract( light.m_Origin, point );
	const double flDistance = Length( delta );
	if ( flDistance < 1.0e-9 )
	{
		return {};
	}
	const Point_t toLight = Scale( delta, 1.0 / flDistance );
	const double flDot = Dot( toLight, normal );
	if ( flDot <= 0.0 )
	{
		return {};
	}
	double flFalloff = LightFalloff( light, flDistance );
	if ( light.m_nType == LightType_t::LIGHT_TYPE_SPOT )
	{
		const double flAlong = -Dot( toLight, light.m_Direction );
		if ( flAlong <= light.m_flOuterCos )
		{
			return {};
		}
		flFalloff *= flAlong;
		if ( flAlong <= light.m_flInnerCos )
		{
			const double flSpan = light.m_flInnerCos - light.m_flOuterCos;
			double flEdge = flSpan > 0.0 ? std::clamp( ( flAlong - light.m_flOuterCos ) / flSpan, 0.0, 1.0 ) : 1.0;
			if ( light.m_flExponent != 0.0 && light.m_flExponent != 1.0 )
			{
				flEdge = std::pow( flEdge, light.m_flExponent );
			}
			flFalloff *= flEdge;
		}
	}
	return light.m_vecIntensity * static_cast<float>( flFalloff * flDot );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<Light_t> ReadLights( const CMap &map, bool bHdr )
{
	std::vector<Light_t> lights;
	bool bEnvironment = false;
	const auto &entities = std::as_const( map.m_Entities );
	for ( std::size_t e = 0; e < entities.size(); e++ )
	{
		const Entity_t &entity = entities[ e ];
		if ( entity.m_bDeleted )
		{
			continue;
		}
		const std::string_view szClass = entity.GetClassname();
		const bool bPoint = szClass == "light";
		const bool bSpot = szClass == "light_spot" || szClass == "light_glspot";
		const bool bSun = szClass == "light_environment";
		if ( !bPoint && !bSpot && !bSun )
		{
			continue;
		}
		// A named light flagged to start dark lights nothing until it is turned on.
		if ( !entity.GetValue( "targetname" ).empty() && ( static_cast<int>( KeyNumber( entity, "spawnflags" ) ) & 1 ) != 0 )
		{
			continue;
		}
		Light_t light;
		light.m_nEntity = static_cast<std::uint32_t>( e );
		light.m_Origin = ParseTriple( entity.GetValue( "origin" ) ).value_or( Point_t{} );
		bool bFound = false;
		light.m_vecIntensity = LightIntensity( entity, bHdr, "_light", "_lightHDR", "_lightscaleHDR", bFound );
		const std::string_view szTarget = entity.GetValue( "target" );
		bool bAimed = false;
		if ( !szTarget.empty() )
		{
			for ( const Entity_t &other : entities )
			{
				if ( !other.m_bDeleted && other.GetValue( "targetname" ) == szTarget )
				{
					if ( const auto origin = ParseTriple( other.GetValue( "origin" ) ) )
					{
						const Point_t delta = Subtract( *origin, light.m_Origin );
						if ( Length( delta ) > 1.0e-9 )
						{
							light.m_Direction = Normalized( delta );
							bAimed = true;
						}
					}
					break;
				}
			}
		}
		if ( !bAimed )
		{
			light.m_Direction = LightDirection( entity );
		}
		if ( bSun )
		{
			// Only the first light_environment lights the map.
			if ( bEnvironment )
			{
				continue;
			}
			bEnvironment = true;
			light.m_nType = LightType_t::LIGHT_TYPE_SUN;
			const double flSpread = KeyNumber( entity, "SunSpreadAngle" );
			light.m_flSunSpread = flSpread > 0.0 ? std::sin( flSpread * std::numbers::pi / 180.0 ) : 0.0;
			Light_t ambient = light;
			ambient.m_nType = LightType_t::LIGHT_TYPE_SKY_AMBIENT;
			bool bAmbient = false;
			ambient.m_vecIntensity = LightIntensity( entity, bHdr, "_ambient", "_ambientHDR", "_AmbientScaleHDR", bAmbient );
			if ( !bAmbient )
			{
				ambient.m_vecIntensity = light.m_vecIntensity * 0.5f;
			}
			lights.push_back( light );
			lights.push_back( ambient );
			continue;
		}
		light.m_nType = LightType_t::LIGHT_TYPE_POINT;
		if ( bSpot )
		{
			double flInner = KeyNumber( entity, "_inner_cone" );
			if ( flInner == 0.0 )
			{
				flInner = 10.0;
			}
			double flOuter = KeyNumber( entity, "_cone" );
			if ( flOuter == 0.0 )
			{
				flOuter = flInner;
			}
			flOuter = std::max( flOuter, flInner );
			if ( !( flInner == 180.0 && flOuter == 180.0 ) )
			{
				// Cones wider than 90 degrees are clamped to 90.
				flInner = std::min( flInner, 90.0 );
				flOuter = std::min( flOuter, 90.0 );
				light.m_nType = LightType_t::LIGHT_TYPE_SPOT;
				light.m_flInnerCos = std::cos( flInner * std::numbers::pi / 180.0 );
				light.m_flOuterCos = std::cos( flOuter * std::numbers::pi / 180.0 );
				light.m_flExponent = KeyNumber( entity, "_exponent" );
			}
		}
		ReadFalloff( entity, light );
		light.m_flReach = LightReach( light );
		if ( light.m_flReach > 0.0 )
		{
			lights.push_back( light );
		}
	}
	return lights;
}

//-----------------------------------------------------------------------------
// Purpose: Tool textures and the sky get no lightmap, nor do unlit shaders and water. What
//          stops light is what vrad counts as opaque: solid, and not see-through.
//-----------------------------------------------------------------------------
LightMaterial_t DescribeLightMaterial( const MaterialInfo_t &info, std::array<float, 3> reflectivity )
{
	LightMaterial_t material;
	material.m_bSky = info.IsSky();
	const std::string_view szShader = info.m_szShader;
	const bool bUnlit = szShader == "unlitgeneric" || szShader == "unlittwotexture" || szShader == "water" || szShader == "sprite" || szShader == "modulate";
	const bool bWater = ( info.m_nCompile & MATERIAL_COMPILE_WATER ) != 0;
	material.m_bLit = !info.IsTool() && !material.m_bSky && !bUnlit && !bWater;
	constexpr std::uint32_t PASSES = MATERIAL_COMPILE_CLIP | MATERIAL_COMPILE_PLAYERCLIP | MATERIAL_COMPILE_NPCCLIP | MATERIAL_COMPILE_TRIGGER | MATERIAL_COMPILE_HINT | MATERIAL_COMPILE_SKIP | MATERIAL_COMPILE_AREAPORTAL | MATERIAL_COMPILE_ORIGIN | MATERIAL_COMPILE_INVISIBLE | MATERIAL_COMPILE_OCCLUDER | MATERIAL_COMPILE_FOG | MATERIAL_COMPILE_LADDER | MATERIAL_COMPILE_WATER | MATERIAL_COMPILE_NONSOLID;
	material.m_bBlocks = material.m_bSky || ( ( info.m_nCompile & PASSES ) == 0 && !info.m_bTranslucent && !info.m_bAdditive );
	if ( reflectivity[ 0 ] > 0.0f || reflectivity[ 1 ] > 0.0f || reflectivity[ 2 ] > 0.0f )
	{
		material.m_vecReflectivity = { reflectivity[ 0 ], reflectivity[ 1 ], reflectivity[ 2 ] };
	}
	material.m_bKnown = true;
	return material;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
LightMaterial_t GuessLightMaterial( std::string_view szName )
{
	std::string szLower( szName );
	std::ranges::transform( szLower, szLower.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	std::ranges::replace( szLower, '\\', '/' );
	LightMaterial_t material;
	if ( !szLower.starts_with( "tools/" ) )
	{
		return material;
	}
	material.m_bLit = false;
	material.m_bSky = szLower == "tools/toolsskybox" || szLower == "tools/toolsskybox2d";
	material.m_bBlocks = material.m_bSky || szLower == "tools/toolsnodraw" || szLower == "tools/toolsblocklight" || szLower == "tools/toolsblack";
	return material;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::uint16_t FloatToHalf( float fl )
{
	const std::uint32_t n = std::bit_cast<std::uint32_t>( fl );
	const std::uint32_t nSign = ( n >> 16 ) & 0x8000u;
	const std::uint32_t nAbs = n & 0x7fffffffu;
	if ( nAbs >= 0x7f800000u )
	{
		return static_cast<std::uint16_t>( nSign | 0x7c00u | ( nAbs > 0x7f800000u ? 0x200u : 0u ) );
	}
	if ( nAbs >= 0x477ff000u )
	{
		// Past the largest half (65504 rounded): infinity.
		return static_cast<std::uint16_t>( nSign | 0x7c00u );
	}
	if ( nAbs < 0x38800000u )
	{
		// Below the smallest normal half: subnormal, rounded to nearest even.
		const float flAbs = std::bit_cast<float>( nAbs );
		const auto nSub = static_cast<std::uint32_t>( std::nearbyint( flAbs * 16777216.0f ) );
		return static_cast<std::uint16_t>( nSign | nSub );
	}
	// Normal: rebias the exponent and round the mantissa to nearest even.
	const std::uint32_t nRounded = nAbs + 0xfffu + ( ( nAbs >> 13 ) & 1u );
	return static_cast<std::uint16_t>( nSign | ( ( nRounded - 0x38000000u ) >> 13 ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float HalfToFloat( std::uint16_t nHalf )
{
	const std::uint32_t nSign = static_cast<std::uint32_t>( nHalf & 0x8000u ) << 16;
	const std::uint32_t nExponent = ( nHalf >> 10 ) & 0x1fu;
	const std::uint32_t nMantissa = nHalf & 0x3ffu;
	if ( nExponent == 0 )
	{
		const float fl = static_cast<float>( nMantissa ) / 16777216.0f;
		return nSign ? -fl : fl;
	}
	if ( nExponent == 31 )
	{
		return std::bit_cast<float>( nSign | 0x7f800000u | ( nMantissa << 13 ) );
	}
	return std::bit_cast<float>( nSign | ( ( nExponent + 112u ) << 23 ) | ( nMantissa << 13 ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::array<float, 3> CLighting::GetTexel( int x, int y ) const
{
	if ( x < 0 || y < 0 || x >= m_nWidth || y >= m_nHeight )
	{
		return {};
	}
	const std::size_t n = ( static_cast<std::size_t>( y ) * static_cast<std::size_t>( m_nWidth ) + static_cast<std::size_t>( x ) ) * 4;
	return { HalfToFloat( m_Texels[ n ] ), HalfToFloat( m_Texels[ n + 1 ] ), HalfToFloat( m_Texels[ n + 2 ] ) };
}

//-----------------------------------------------------------------------------
// Purpose: As a graphics card filters a texture linearly with clamping at its edges.
//-----------------------------------------------------------------------------
std::array<float, 3> CLighting::Sample( float x, float y ) const
{
	if ( m_nWidth <= 0 || m_nHeight <= 0 )
	{
		return {};
	}
	const float fx = x - 0.5f;
	const float fy = y - 0.5f;
	const float x0f = std::floor( fx );
	const float y0f = std::floor( fy );
	const float tx = fx - x0f;
	const float ty = fy - y0f;
	const int x0 = std::clamp( static_cast<int>( x0f ), 0, m_nWidth - 1 );
	const int y0 = std::clamp( static_cast<int>( y0f ), 0, m_nHeight - 1 );
	const int x1 = std::clamp( static_cast<int>( x0f ) + 1, 0, m_nWidth - 1 );
	const int y1 = std::clamp( static_cast<int>( y0f ) + 1, 0, m_nHeight - 1 );
	const std::array<float, 3> a = GetTexel( x0, y0 );
	const std::array<float, 3> b = GetTexel( x1, y0 );
	const std::array<float, 3> c = GetTexel( x0, y1 );
	const std::array<float, 3> d = GetTexel( x1, y1 );
	std::array<float, 3> out{};
	for ( std::size_t k = 0; k < 3; k++ )
	{
		out[ k ] = ( a[ k ] * ( 1.0f - tx ) + b[ k ] * tx ) * ( 1.0f - ty ) + ( c[ k ] * ( 1.0f - tx ) + d[ k ] * tx ) * ty;
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<LightVertex_t> CLighting::MeshVertices( const CScene &scene, const CMapMesh &mesh ) const
{
	if ( const auto extra = m_Extras.find( &mesh ); extra != m_Extras.end() && extra->second && extra->second->size() == mesh.m_Vertices.size() )
	{
		return *extra->second;
	}
	std::vector<LightVertex_t> out( mesh.m_Vertices.size(), LIGHT_VERTEX_NONE );
	const bool bInstance = m_pInstanceMesh && &mesh == m_pInstanceMesh.get() && &mesh == &scene.GetInstanceMesh();
	const float flStep = static_cast<float>( m_nStep );
	for ( std::size_t f = 0; f < mesh.m_Faces.size(); f++ )
	{
		const MeshFace_t &face = mesh.m_Faces[ f ];
		const LightmapRect_t *pRect = nullptr;
		if ( bInstance )
		{
			pRect = f < m_InstanceFaces.size() && m_InstanceFaces[ f ].m_nWidth > 0 ? &m_InstanceFaces[ f ] : nullptr;
		}
		else if ( const auto it = m_Faces.find( FaceRef_t{ face.m_nSolid, face.m_nSide } ); it != m_Faces.end() )
		{
			pRect = &it->second;
		}
		if ( !pRect )
		{
			if ( face.m_nMaterial < m_Unlit.size() && m_Unlit[ face.m_nMaterial ] )
			{
				std::fill_n( out.begin() + face.m_nFirstVertex, face.m_nVertexCount, LightVertex_t{ 0.0f, 0.0f, 0.0f, LIGHT_KIND_FULLBRIGHT } );
			}
			continue;
		}
		constexpr float SLACK = 0.01f;
		const CVec3 vecFirst = face.m_nVertexCount > 0 ? mesh.m_Vertices[ face.m_nFirstVertex ].m_vecPosition : CVec3{};
		bool bFits = Length( vecFirst - pRect->m_vecFirst ) <= SLACK;
		for ( std::uint32_t v = 0; v < face.m_nVertexCount && bFits; v++ )
		{
			const MeshVertex_t &vertex = mesh.m_Vertices[ face.m_nFirstVertex + v ];
			bFits = vertex.m_Luxel[ 0 ] >= pRect->m_flMinU - SLACK && vertex.m_Luxel[ 0 ] <= pRect->m_flMaxU + SLACK && vertex.m_Luxel[ 1 ] >= pRect->m_flMinV - SLACK && vertex.m_Luxel[ 1 ] <= pRect->m_flMaxV + SLACK;
		}
		if ( !bFits )
		{
			continue;
		}
		for ( std::uint32_t v = 0; v < face.m_nVertexCount; v++ )
		{
			const MeshVertex_t &vertex = mesh.m_Vertices[ face.m_nFirstVertex + v ];
			out[ face.m_nFirstVertex + v ] = { static_cast<float>( pRect->m_nX ) + ( vertex.m_Luxel[ 0 ] - pRect->m_flMinU ) / flStep + 0.5f, static_cast<float>( pRect->m_nY ) + ( vertex.m_Luxel[ 1 ] - pRect->m_flMinV ) / flStep + 0.5f, 0.0f, LIGHT_KIND_LIGHTMAP };
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Places every lit face's luxels in an atlas (coarser when there are too many),
//          builds the tracer from the faces that block light, then lights each luxel straight
//          from the lights, and bounces that light between faces pass by pass. Bounced light
//          is gathered on a sparser grid and smoothed back over the luxels, as it changes
//          slowly across a face.
//-----------------------------------------------------------------------------
std::shared_ptr<const CLighting> BakeLighting( const CScene &scene, std::span<const LightMaterial_t> materials, const LightingOptions_t &options, const LightingExtras_t &extras )
{
	Bake_t bake;
	bake.m_pOptions = &options;
	bake.Progress( 0, "Reading lights" );
	bake.m_Lights = ReadLights( scene.m_Map, options.m_bHdr );

	// The solids as they stand now (not the meshes as read with edits beside them).
	std::vector<MeshSolid_t> solids;
	const auto &mapSolids = std::as_const( scene.m_Map.m_Solids );
	for ( std::size_t i = 0; i < mapSolids.size() && i < scene.m_Solids.size(); i++ )
	{
		if ( !mapSolids[ i ].m_bDeleted )
		{
			solids.push_back( { static_cast<std::uint32_t>( i ), static_cast<std::uint32_t>( i ), &std::as_const( scene.m_Solids )[ i ] } );
		}
	}
	const CMapMesh live = BuildMesh( scene.m_Map, std::span<const MeshSolid_t>{ solids } );
	if ( bake.IsCancelled() )
	{
		return nullptr;
	}
	std::vector<LightMaterial_t> known( scene.m_Materials.size() );
	for ( std::size_t i = 0; i < known.size(); i++ )
	{
		known[ i ] = i < materials.size() && materials[ i ].m_bKnown ? materials[ i ] : GuessLightMaterial( scene.m_Materials[ i ] );
	}
	CollectFaces( scene, live, known, bake );
	bake.m_Unlit.resize( known.size() );
	for ( std::size_t i = 0; i < known.size(); i++ )
	{
		bake.m_Unlit[ i ] = !known[ i ].m_bLit;
	}

	// The atlas: a step that keeps the luxels under the limit, and rows that fit.
	std::size_t nLuxels = 0;
	int nStep = 1;
	for ( ;; nStep *= 2 )
	{
		nLuxels = 0;
		for ( const BakeFace_t &face : bake.m_Faces )
		{
			if ( face.m_bLit )
			{
				nLuxels += static_cast<std::size_t>( LuxelSpan( face.m_flMinU, face.m_flMaxU, nStep ) ) * static_cast<std::size_t>( LuxelSpan( face.m_flMinV, face.m_flMaxV, nStep ) );
			}
		}
		if ( nLuxels <= options.m_nMaxLuxels || nStep >= 64 )
		{
			break;
		}
	}
	constexpr int MAX_SIZE = 16384;
	int nWidth = 256;
	while ( nWidth < MAX_SIZE && static_cast<double>( nWidth ) * nWidth < static_cast<double>( nLuxels ) * 1.3 )
	{
		nWidth *= 2;
	}
	int nHeight = 1;
	while ( !PackAtlas( bake.m_Faces, nStep, nWidth, MAX_SIZE, nHeight ) )
	{
		if ( nWidth < MAX_SIZE )
		{
			nWidth *= 2;
		}
		else if ( nStep < 1024 )
		{
			nStep *= 2;
		}
		else
		{
			return nullptr;
		}
	}
	bake.m_nStep = nStep;
	bake.m_nWidth = nWidth;
	bake.m_nHeight = nHeight;

	ParallelFor( bake.m_Faces.size(), 64, [ & ]( std::size_t nBegin, std::size_t nEnd ) {
		for ( std::size_t f = nBegin; f < nEnd && !bake.IsCancelled(); f++ )
		{
			BakeFace_t &face = bake.m_Faces[ f ];
			if ( !face.m_bLit )
			{
				continue;
			}
			PlaceSamples( face, nStep );
			for ( std::uint32_t l = 0; l < bake.m_Lights.size(); l++ )
			{
				const Light_t &light = bake.m_Lights[ l ];
				if ( light.m_nType != LightType_t::LIGHT_TYPE_POINT && light.m_nType != LightType_t::LIGHT_TYPE_SPOT )
				{
					continue;
				}
				// Within reach of its box, and (for a flat face) in front of it.
				double flOutside = 0.0;
				for ( std::size_t k = 0; k < 3; k++ )
				{
					const double flGap = std::max( { face.m_Bounds.m_Mins[ k ] - light.m_Origin[ k ], 0.0, light.m_Origin[ k ] - face.m_Bounds.m_Maxs[ k ] } );
					flOutside += flGap * flGap;
				}
				if ( flOutside > light.m_flReach * light.m_flReach )
				{
					continue;
				}
				if ( face.m_Plane && face.m_Plane->Distance( light.m_Origin ) <= 0.0 )
				{
					continue;
				}
				face.m_Lights.push_back( l );
			}
		}
	} );
	if ( bake.IsCancelled() )
	{
		return nullptr;
	}

	bake.Progress( 5, "Building the tracer" );
	std::vector<TraceTriangle_t> triangles;
	for ( std::size_t f = 0; f < bake.m_Faces.size(); f++ )
	{
		const BakeFace_t &face = bake.m_Faces[ f ];
		if ( !face.m_bBlocks )
		{
			continue;
		}
		const CMapMesh &mesh = *face.m_pMesh;
		for ( const std::uint32_t t : face.m_Triangles )
		{
			TraceTriangle_t triangle;
			triangle.m_nFace = static_cast<std::uint32_t>( f );
			std::array<CVec3, 3> corners;
			for ( std::size_t k = 0; k < 3; k++ )
			{
				const MeshVertex_t &vertex = mesh.m_Vertices[ mesh.m_Indices[ t * 3 + k ] ];
				corners[ k ] = vertex.m_vecPosition;
				triangle.m_Luxels[ k ] = vertex.m_Luxel;
			}
			triangle.m_V0 = { corners[ 0 ].m_flX, corners[ 0 ].m_flY, corners[ 0 ].m_flZ };
			triangle.m_E1 = { corners[ 1 ].m_flX - corners[ 0 ].m_flX, corners[ 1 ].m_flY - corners[ 0 ].m_flY, corners[ 1 ].m_flZ - corners[ 0 ].m_flZ };
			triangle.m_E2 = { corners[ 2 ].m_flX - corners[ 0 ].m_flX, corners[ 2 ].m_flY - corners[ 0 ].m_flY, corners[ 2 ].m_flZ - corners[ 0 ].m_flZ };
			triangles.push_back( triangle );
		}
	}
	// prop_static's models cast shadows, as vrad's do (unless disableshadows): one face stands for
	// them all, lit by nothing and bouncing nothing.
	if ( extras.m_bPropShadows && !extras.m_Props.empty() )
	{
		const auto nPropFace = static_cast<std::uint32_t>( bake.m_Faces.size() );
		bake.m_Faces.emplace_back();
		std::map<std::uint32_t, bool> casts;
		auto castsShadow = [ & ]( std::uint32_t nObject ) {
			const auto [ it, bNew ] = casts.try_emplace( nObject, false );
			if ( bNew && nObject < scene.GetObjectCount() && !scene.IsSolidObject( nObject ) && !scene.IsDeleted( nObject ) )
			{
				const Entity_t &entity = std::as_const( scene.m_Map.m_Entities )[ scene.GetIndex( nObject ) ];
				it->second = entity.GetClassname() == "prop_static" && KeyNumber( entity, "disableshadows" ) == 0.0;
			}
			return it->second;
		};
		for ( const std::shared_ptr<const CMapMesh> &pMesh : extras.m_Props )
		{
			for ( std::size_t t = 0; pMesh && t + 2 < pMesh->m_Indices.size(); t += 3 )
			{
				const MeshVertex_t &a = pMesh->m_Vertices[ pMesh->m_Indices[ t ] ];
				if ( !castsShadow( a.m_nObject ) )
				{
					continue;
				}
				const CVec3 b = pMesh->m_Vertices[ pMesh->m_Indices[ t + 1 ] ].m_vecPosition;
				const CVec3 c = pMesh->m_Vertices[ pMesh->m_Indices[ t + 2 ] ].m_vecPosition;
				TraceTriangle_t triangle;
				triangle.m_nFace = nPropFace;
				triangle.m_V0 = { a.m_vecPosition.m_flX, a.m_vecPosition.m_flY, a.m_vecPosition.m_flZ };
				triangle.m_E1 = { b.m_flX - a.m_vecPosition.m_flX, b.m_flY - a.m_vecPosition.m_flY, b.m_flZ - a.m_vecPosition.m_flZ };
				triangle.m_E2 = { c.m_flX - a.m_vecPosition.m_flX, c.m_flY - a.m_vecPosition.m_flY, c.m_flZ - a.m_vecPosition.m_flZ };
				triangles.push_back( triangle );
			}
		}
	}
	bake.m_Tracer.Build( std::move( triangles ) );
	if ( bake.IsCancelled() )
	{
		return nullptr;
	}

	const std::size_t nTexels = static_cast<std::size_t>( nWidth ) * static_cast<std::size_t>( nHeight );
	std::vector<Rgb_t> direct( nTexels, Rgb_t{} );
	std::atomic<std::size_t> nDone{ 0 };
	const std::size_t nFaces = bake.m_Faces.size();
	bake.Progress( 10, "Direct light" );
	ParallelFor( nFaces, 16, [ & ]( std::size_t nBegin, std::size_t nEnd ) {
		for ( std::size_t f = nBegin; f < nEnd && !bake.IsCancelled(); f++ )
		{
			const BakeFace_t &face = bake.m_Faces[ f ];
			if ( !face.m_bLit )
			{
				continue;
			}
			for ( int j = 0; j < face.m_nHeight; j++ )
			{
				for ( int i = 0; i < face.m_nWidth; i++ )
				{
					const std::size_t nTexel = bake.TexelIndex( face, i, j );
					direct[ nTexel ] = DirectLight( bake, face.m_Lights, face.m_Samples[ static_cast<std::size_t>( j * face.m_nWidth + i ) ], static_cast<std::uint32_t>( nTexel ) );
				}
			}
		}
		const std::size_t n = nDone.fetch_add( nEnd - nBegin ) + ( nEnd - nBegin );
		bake.Progress( 10 + static_cast<int>( n * 40 / std::max<std::size_t>( nFaces, 1 ) ), "Direct light" );
	} );
	if ( bake.IsCancelled() )
	{
		return nullptr;
	}
	// The sky's ambient is soft: gathered on a sparser grid.
	bake.Progress( 50, "Sky light" );
	const std::vector<Point_t> skyDirs = CosineDirections( std::max( options.m_nSkySamples, 1 ) );
	GatherSparse( bake, std::max( 1, options.m_nSkySpacing ), direct, [ & ]( const LuxelSample_t &sample, std::uint32_t nSeed ) { return SkyAmbient( bake, sample, nSeed, skyDirs ); } );
	if ( bake.IsCancelled() )
	{
		return nullptr;
	}
	if ( options.m_pfnPartial && options.m_nBounces > 0 )
	{
		options.m_pfnPartial( MakeResult( scene, bake, direct, {} ) );
	}

	// Bounces: each pass gathers what the last one left on the faces.
	std::vector<Rgb_t> bounced( nTexels, Rgb_t{} );
	const std::vector<Point_t> bounceDirs = CosineDirections( std::max( options.m_nBounceSamples, 1 ) );
	std::vector<Rgb_t> last = direct;
	std::array<double, 3> lastTotal{};
	std::array<double, 3> ratio{};
	for ( int nBounce = 0; nBounce < options.m_nBounces; nBounce++ )
	{
		bake.Progress( 60 + 40 * nBounce / std::max( options.m_nBounces, 1 ), "Bounced light" );
		std::vector<Rgb_t> next( nTexels, Rgb_t{} );
		const std::uint32_t nSalt = static_cast<std::uint32_t>( nBounce + 1 ) * 0x9e3779b9u;
		GatherSparse( bake, std::max( 1, options.m_nBounceSpacing ), next, [ & ]( const LuxelSample_t &sample, std::uint32_t nSeed ) { return GatherBounce( bake, last, sample, nSeed ^ nSalt, bounceDirs ); } );
		if ( bake.IsCancelled() )
		{
			return nullptr;
		}
		std::array<double, 3> total{};
		for ( std::size_t n = 0; n < nTexels; n++ )
		{
			for ( std::size_t k = 0; k < 3; k++ )
			{
				bounced[ n ][ k ] += next[ n ][ k ];
				total[ k ] += next[ n ][ k ];
			}
		}
		for ( std::size_t k = 0; k < 3; k++ )
		{
			ratio[ k ] = lastTotal[ k ] > 0.0 ? total[ k ] / lastTotal[ k ] : 0.0;
		}
		lastTotal = total;
		last = std::move( next );
	}
	if ( options.m_nBounces >= 2 && options.m_bConverge )
	{
		// vrad bounces until the light stops changing. Each pass here brought about the same
		// share of the one before, so the passes not made add up to the last one times
		// r + r² + ... = r / (1 - r).
		for ( std::size_t k = 0; k < 3; k++ )
		{
			const double r = std::clamp( ratio[ k ], 0.0, 0.9 );
			const auto flRest = static_cast<float>( r / ( 1.0 - r ) );
			for ( std::size_t n = 0; n < nTexels; n++ )
			{
				bounced[ n ][ k ] += last[ n ][ k ] * flRest;
			}
		}
	}
	std::shared_ptr<CLighting> pLighting = MakeResult( scene, bake, direct, bounced );

	// The extras, lit from what the faces hold in the end.
	if ( !extras.m_Props.empty() || !extras.m_Overlays.empty() || !extras.m_Points.empty() )
	{
		bake.Progress( 95, "Props and overlays" );
		std::vector<Rgb_t> total = std::move( direct );
		for ( std::size_t n = 0; n < nTexels; n++ )
		{
			for ( std::size_t k = 0; k < 3; k++ )
			{
				total[ n ][ k ] += bounced[ n ][ k ];
			}
		}
		for ( const std::shared_ptr<const CMapMesh> &pMesh : extras.m_Props )
		{
			if ( pMesh && !pLighting->m_Extras.contains( pMesh.get() ) )
			{
				pLighting->m_Extras[ pMesh.get() ] = std::make_shared<const std::vector<LightVertex_t>>( LightPropVertices( bake, total, *pMesh ) );
				pLighting->m_ExtraMeshes.push_back( pMesh );
			}
		}
		if ( !extras.m_Overlays.empty() )
		{
			// Every face with luxels, blocking light or not, for overlays to find theirs.
			std::vector<TraceTriangle_t> lit;
			for ( std::size_t f = 0; f < bake.m_Faces.size(); f++ )
			{
				const BakeFace_t &face = bake.m_Faces[ f ];
				if ( !face.m_bLit || face.m_nWidth == 0 )
				{
					continue;
				}
				for ( const std::uint32_t t : face.m_Triangles )
				{
					TraceTriangle_t triangle;
					triangle.m_nFace = static_cast<std::uint32_t>( f );
					std::array<CVec3, 3> corners;
					for ( std::size_t k = 0; k < 3; k++ )
					{
						const MeshVertex_t &vertex = face.m_pMesh->m_Vertices[ face.m_pMesh->m_Indices[ t * 3 + k ] ];
						corners[ k ] = vertex.m_vecPosition;
						triangle.m_Luxels[ k ] = vertex.m_Luxel;
					}
					triangle.m_V0 = { corners[ 0 ].m_flX, corners[ 0 ].m_flY, corners[ 0 ].m_flZ };
					triangle.m_E1 = { corners[ 1 ].m_flX - corners[ 0 ].m_flX, corners[ 1 ].m_flY - corners[ 0 ].m_flY, corners[ 1 ].m_flZ - corners[ 0 ].m_flZ };
					triangle.m_E2 = { corners[ 2 ].m_flX - corners[ 0 ].m_flX, corners[ 2 ].m_flY - corners[ 0 ].m_flY, corners[ 2 ].m_flZ - corners[ 0 ].m_flZ };
					lit.push_back( triangle );
				}
			}
			CTracer surfaces;
			surfaces.Build( std::move( lit ) );
			for ( const std::shared_ptr<const CMapMesh> &pMesh : extras.m_Overlays )
			{
				if ( pMesh && !pLighting->m_Extras.contains( pMesh.get() ) )
				{
					pLighting->m_Extras[ pMesh.get() ] = std::make_shared<const std::vector<LightVertex_t>>( LightOverlayVertices( bake, surfaces, total, *pMesh ) );
					pLighting->m_ExtraMeshes.push_back( pMesh );
				}
			}
		}
		// Points (sprites) face every way: the six axes' light, averaged.
		const std::vector<Point_t> skyDirs = CosineDirections( 24 );
		const std::vector<Point_t> bounceDirs = CosineDirections( 16 );
		for ( const auto &[ nObject, origin ] : extras.m_Points )
		{
			Box_t at;
			at.Add( origin );
			const std::vector<std::uint32_t> lights = LightsReaching( bake, at );
			Rgb_t sum{};
			for ( const Point_t &normal : { Point_t{ 1, 0, 0 }, Point_t{ -1, 0, 0 }, Point_t{ 0, 1, 0 }, Point_t{ 0, -1, 0 }, Point_t{ 0, 0, 1 }, Point_t{ 0, 0, -1 } } )
			{
				const Rgb_t light = LightPoint( bake, total, lights, LuxelSample_t{ origin, normal }, nObject, skyDirs, bounceDirs );
				for ( std::size_t k = 0; k < 3; k++ )
				{
					sum[ k ] += light[ k ] / 6.0f;
				}
			}
			pLighting->m_Points[ nObject ] = { sum[ 0 ] / 255.0f, sum[ 1 ] / 255.0f, sum[ 2 ] / 255.0f };
		}
		if ( bake.IsCancelled() )
		{
			return nullptr;
		}
	}
	bake.Progress( 100, "Done" );
	return pLighting;
}

} // namespace xv::map
