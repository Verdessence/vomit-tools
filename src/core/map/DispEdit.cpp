//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See DispEdit.h. The grid's layout (which corner starts it, how its rows run, the
//          two triangles a square makes) is BuildDisplacement's; everything here goes through
//          the mesh it builds and writes back fields.
//
//===========================================================================//
#include "DispEdit.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <tuple>

namespace xv::map
{

namespace
{

constexpr double MEET = 0.5;  // how near two corners must be to count as one
constexpr double BUILDABLE_UP = 0.8; // cos of the slope a triangle can be built on
constexpr double WALKABLE_UP = 0.7;  // and walked up

//-----------------------------------------------------------------------------
// Purpose: How much of the brush a point this far from its centre gets: all at the centre,
//          none at the radius, smooth between.
//-----------------------------------------------------------------------------
double Weight( double flDistance, double flRadius, bool bSoft = true )
{
	if ( flRadius <= 0.0 || flDistance >= flRadius )
	{
		return 0.0;
	}
	if ( !bSoft )
	{
		return 1.0;
	}
	const double t = flDistance / flRadius;
	return 1.0 - t * t * ( 3.0 - 2.0 * t );
}

//-----------------------------------------------------------------------------
// Purpose: A point's grid neighbours (up to four), by index.
//-----------------------------------------------------------------------------
std::vector<std::size_t> Neighbours( std::size_t nSize, std::size_t i )
{
	std::vector<std::size_t> out;
	const std::size_t r = i / nSize;
	const std::size_t c = i % nSize;
	if ( r > 0 )
	{
		out.push_back( i - nSize );
	}
	if ( r + 1 < nSize )
	{
		out.push_back( i + nSize );
	}
	if ( c > 0 )
	{
		out.push_back( i - 1 );
	}
	if ( c + 1 < nSize )
	{
		out.push_back( i + 1 );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Spatial brushes use world distances; fixed brushes use steps about the closest
//          grid vertex, independent of the displacement's world size or power.
//-----------------------------------------------------------------------------
std::vector<double> BrushWeights( const DispMesh_t &mesh, const DispBrush_t &brush )
{
	std::vector<double> weights( mesh.m_Points.size(), 0.0 );
	if ( mesh.m_Points.empty() )
	{
		return weights;
	}
	const auto closest = std::ranges::min_element( mesh.m_Points, [ & ]( const Point_t &a, const Point_t &b ) { return Length( Subtract( a, brush.m_Centre ) ) < Length( Subtract( b, brush.m_Centre ) ); } );
	const std::size_t nClosest = static_cast<std::size_t>( closest - mesh.m_Points.begin() );
	const auto nSize = static_cast<std::size_t>( mesh.m_nSize );
	const double flStep = std::max( Length( Subtract( mesh.m_Corners[ 1 ], mesh.m_Corners[ 0 ] ) ), Length( Subtract( mesh.m_Corners[ 3 ], mesh.m_Corners[ 0 ] ) ) ) / ( nSize - 1 );
	if ( !brush.m_bSpatial && Length( Subtract( *closest, brush.m_Centre ) ) > flStep * ( brush.m_nVertexRadius + 0.75 ) )
	{
		return weights;
	}
	for ( std::size_t i = 0; i < weights.size(); ++i )
	{
		if ( brush.m_bSpatial )
		{
			weights[ i ] = Weight( Length( Subtract( mesh.m_Points[ i ], brush.m_Centre ) ), brush.m_flRadius, brush.m_bSoftEdge );
		}
		else
		{
			const double r = static_cast<double>( i / nSize ) - static_cast<double>( nClosest / nSize );
			const double c = static_cast<double>( i % nSize ) - static_cast<double>( nClosest % nSize );
			weights[ i ] = r * r + c * c <= brush.m_nVertexRadius * brush.m_nVertexRadius ? 1.0 : 0.0;
		}
	}
	return weights;
}

//-----------------------------------------------------------------------------
// Purpose: The point's position set, the elevation and offset taken out of what is stored.
//-----------------------------------------------------------------------------
void SetPosition( DispInfo_t &disp, const DispMesh_t &mesh, const FaceGeometry_t &face, std::size_t i, const Point_t &target )
{
	const Point_t field = Subtract( Subtract( target, mesh.m_Flat[ i ] ), Scale( face.m_Plane.m_Normal, disp.m_flElevation ) );
	SetDispField( disp, i, Subtract( field, i < disp.m_Offsets.size() ? ToPoint( disp.m_Offsets[ i ] ) : Point_t{} ) );
}

//-----------------------------------------------------------------------------
// Purpose: Where edge k (from grid corner k to the next) is at t along it, as a fractional
//          grid place (row, column).
//-----------------------------------------------------------------------------
std::pair<double, double> EdgePlace( std::size_t nSize, std::size_t k, double t )
{
	const double flLast = static_cast<double>( nSize - 1 );
	switch ( k )
	{
		case 0: return { t * flLast, 0.0 };
		case 1: return { flLast, t * flLast };
		case 2: return { ( 1.0 - t ) * flLast, flLast };
		default: return { 0.0, ( 1.0 - t ) * flLast };
	}
}

//-----------------------------------------------------------------------------
// Purpose: The mesh's position at a fractional grid place on one of its edges (one of the
//          coordinates is whole), between the two grid points round it.
//-----------------------------------------------------------------------------
Point_t EdgePosition( const DispMesh_t &mesh, std::pair<double, double> place )
{
	const auto nSize = static_cast<std::size_t>( mesh.m_nSize );
	auto at = [ & ]( std::size_t r, std::size_t c ) { return mesh.m_Points[ std::min( r, nSize - 1 ) * nSize + std::min( c, nSize - 1 ) ]; };
	const double r = std::clamp( place.first, 0.0, static_cast<double>( nSize - 1 ) );
	const double c = std::clamp( place.second, 0.0, static_cast<double>( nSize - 1 ) );
	const auto r0 = static_cast<std::size_t>( std::floor( r ) );
	const auto c0 = static_cast<std::size_t>( std::floor( c ) );
	const Point_t a = at( r0, c0 );
	const Point_t b = at( r0 + 1, c0 );
	const Point_t d = at( r0, c0 + 1 );
	return Add( a, Add( Scale( Subtract( b, a ), r - static_cast<double>( r0 ) ), Scale( Subtract( d, a ), c - static_cast<double>( c0 ) ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: The grid points along edge k, each with where it is along the edge (0 to 1).
//-----------------------------------------------------------------------------
std::vector<std::pair<std::size_t, double>> EdgePoints( std::size_t nSize, std::size_t k )
{
	std::vector<std::pair<std::size_t, double>> out;
	for ( std::size_t i = 0; i < nSize; i++ )
	{
		const double t = static_cast<double>( i ) / static_cast<double>( nSize - 1 );
		const auto [ r, c ] = EdgePlace( nSize, k, t );
		out.emplace_back( static_cast<std::size_t>( std::lround( r ) ) * nSize + static_cast<std::size_t>( std::lround( c ) ), t );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: How an edge of one face meets an edge of another: where t along the first lands
//          along the second (as t itself, reversed, or a half of it), or nothing.
//-----------------------------------------------------------------------------
std::optional<std::pair<double, double>> EdgeMatch( const Point_t &a0, const Point_t &a1, const Point_t &b0, const Point_t &b1 )
{
	auto same = []( const Point_t &p, const Point_t &q ) { return Length( Subtract( p, q ) ) <= MEET; };
	const Point_t mid = Lerp( b0, b1, 0.5 );
	// t along a maps to m_first + t * m_second along b.
	if ( same( a0, b0 ) && same( a1, b1 ) )
	{
		return std::pair{ 0.0, 1.0 };
	}
	if ( same( a0, b1 ) && same( a1, b0 ) )
	{
		return std::pair{ 1.0, -1.0 };
	}
	if ( same( a0, b0 ) && same( a1, mid ) )
	{
		return std::pair{ 0.0, 0.5 };
	}
	if ( same( a0, mid ) && same( a1, b1 ) )
	{
		return std::pair{ 0.5, 0.5 };
	}
	if ( same( a0, b1 ) && same( a1, mid ) )
	{
		return std::pair{ 1.0, -0.5 };
	}
	if ( same( a0, mid ) && same( a1, b0 ) )
	{
		return std::pair{ 0.5, -0.5 };
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: A quantised position, for telling shared points apart from near ones.
//-----------------------------------------------------------------------------
std::tuple<long long, long long, long long> Key( const Point_t &p )
{
	return { std::llround( p[ 0 ] * 16.0 ), std::llround( p[ 1 ] * 16.0 ), std::llround( p[ 2 ] * 16.0 ) };
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The start is the corner lowest down (then nearest the front, then the left), as
//          Hammer's "bottom left"; which corner it is sets how the rows run.
//-----------------------------------------------------------------------------
std::optional<DispInfo_t> MakeDisplacement( const FaceGeometry_t &face, int nPower )
{
	if ( face.m_Points.size() != 4 || nPower < 2 || nPower > 4 )
	{
		return std::nullopt;
	}
	DispInfo_t disp;
	disp.m_nPower = nPower;
	disp.m_Start = face.m_Points[ 0 ];
	for ( const Point_t &p : face.m_Points )
	{
		if ( std::tie( p[ 2 ], p[ 1 ], p[ 0 ] ) < std::tie( disp.m_Start[ 2 ], disp.m_Start[ 1 ], disp.m_Start[ 0 ] ) )
		{
			disp.m_Start = p;
		}
	}
	const auto nCount = static_cast<std::size_t>( disp.GetSize() ) * static_cast<std::size_t>( disp.GetSize() );
	const CVec3 normal = ToVec3( face.m_Plane.m_Normal );
	disp.m_Normals.assign( nCount, normal );
	disp.m_Distances.assign( nCount, 0.0f );
	disp.m_Offsets.assign( nCount, CVec3{} );
	disp.m_OffsetNormals.assign( nCount, normal );
	disp.m_Alphas.assign( nCount, 0.0f );
	disp.m_TriangleTags.assign( ( static_cast<std::size_t>( disp.GetSize() ) - 1 ) * ( static_cast<std::size_t>( disp.GetSize() ) - 1 ) * 2, DISP_TAG_LEVEL );
	disp.m_AllowedVerts.assign( 10, -1 );
	disp.m_bValid = true;
	TagTriangles( disp, face );
	return disp;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Point_t DispField( const DispInfo_t &disp, std::size_t nPoint )
{
	Point_t field{};
	if ( nPoint < disp.m_Normals.size() && nPoint < disp.m_Distances.size() )
	{
		field = Scale( ToPoint( disp.m_Normals[ nPoint ] ), disp.m_Distances[ nPoint ] );
	}
	if ( nPoint < disp.m_Offsets.size() )
	{
		field = Add( field, ToPoint( disp.m_Offsets[ nPoint ] ) );
	}
	return field;
}

//-----------------------------------------------------------------------------
// Purpose: A zero field keeps the normal it had (Hammer writes the face's), with no distance.
//-----------------------------------------------------------------------------
void SetDispField( DispInfo_t &disp, std::size_t nPoint, const Point_t &field )
{
	if ( nPoint >= disp.m_Normals.size() || nPoint >= disp.m_Distances.size() )
	{
		return;
	}
	const double flLength = Length( field );
	if ( flLength < 1e-9 )
	{
		disp.m_Distances[ nPoint ] = 0.0f;
		return;
	}
	disp.m_Normals[ nPoint ] = ToVec3( Scale( field, 1.0 / flLength ) );
	disp.m_Distances[ nPoint ] = static_cast<float>( flLength );
}

//-----------------------------------------------------------------------------
// Purpose: Bilinear over the old grid; the tags are made again by whoever applies the edit.
//-----------------------------------------------------------------------------
void SetDisplacementPower( DispInfo_t &disp, int nPower )
{
	nPower = std::clamp( nPower, 2, 4 );
	if ( nPower == disp.m_nPower || !disp.m_bValid )
	{
		disp.m_nPower = nPower;
		return;
	}
	const auto nOld = static_cast<std::size_t>( disp.GetSize() );
	const auto nNew = static_cast<std::size_t>( ( 1 << nPower ) + 1 );
	auto sample = [ & ]( auto pfnAt, double r, double c ) {
		const auto r0 = std::min( static_cast<std::size_t>( std::floor( r ) ), nOld - 1 );
		const auto c0 = std::min( static_cast<std::size_t>( std::floor( c ) ), nOld - 1 );
		const std::size_t r1 = std::min( r0 + 1, nOld - 1 );
		const std::size_t c1 = std::min( c0 + 1, nOld - 1 );
		const double fr = r - static_cast<double>( r0 );
		const double fc = c - static_cast<double>( c0 );
		const auto a = pfnAt( r0 * nOld + c0 );
		const auto b = pfnAt( r1 * nOld + c0 );
		const auto d = pfnAt( r0 * nOld + c1 );
		const auto e = pfnAt( r1 * nOld + c1 );
		return Add( Scale( Add( Scale( a, 1.0 - fc ), Scale( d, fc ) ), 1.0 - fr ), Scale( Add( Scale( b, 1.0 - fc ), Scale( e, fc ) ), fr ) );
	};
	std::vector<Point_t> fields( nNew * nNew ), offsets( nNew * nNew ), offsetNormals( nNew * nNew );
	std::vector<float> alphas( nNew * nNew );
	for ( std::size_t r = 0; r < nNew; r++ )
	{
		for ( std::size_t c = 0; c < nNew; c++ )
		{
			const double flR = static_cast<double>( r ) * static_cast<double>( nOld - 1 ) / static_cast<double>( nNew - 1 );
			const double flC = static_cast<double>( c ) * static_cast<double>( nOld - 1 ) / static_cast<double>( nNew - 1 );
			const std::size_t i = r * nNew + c;
			fields[ i ] = sample( [ & ]( std::size_t n ) { return Subtract( DispField( disp, n ), n < disp.m_Offsets.size() ? ToPoint( disp.m_Offsets[ n ] ) : Point_t{} ); }, flR, flC );
			offsets[ i ] = sample( [ & ]( std::size_t n ) { return n < disp.m_Offsets.size() ? ToPoint( disp.m_Offsets[ n ] ) : Point_t{}; }, flR, flC );
			offsetNormals[ i ] = sample( [ & ]( std::size_t n ) { return n < disp.m_OffsetNormals.size() ? ToPoint( disp.m_OffsetNormals[ n ] ) : Point_t{}; }, flR, flC );
			alphas[ i ] = static_cast<float>( sample( [ & ]( std::size_t n ) { return Point_t{ n < disp.m_Alphas.size() ? disp.m_Alphas[ n ] : 0.0f, 0, 0 }; }, flR, flC )[ 0 ] );
		}
	}
	disp.m_nPower = nPower;
	disp.m_Normals.assign( nNew * nNew, disp.m_Normals.empty() ? CVec3{ 0, 0, 1 } : disp.m_Normals.front() );
	disp.m_Distances.assign( nNew * nNew, 0.0f );
	disp.m_Offsets.resize( nNew * nNew );
	disp.m_OffsetNormals.resize( nNew * nNew );
	disp.m_Alphas = std::move( alphas );
	for ( std::size_t i = 0; i < nNew * nNew; i++ )
	{
		disp.m_Offsets[ i ] = ToVec3( offsets[ i ] );
		disp.m_OffsetNormals[ i ] = ToVec3( Length( offsetNormals[ i ] ) > 1e-9 ? Normalized( offsetNormals[ i ] ) : Point_t{ 0, 0, 1 } );
		SetDispField( disp, i, fields[ i ] );
	}
	disp.m_TriangleTags.assign( ( nNew - 1 ) * ( nNew - 1 ) * 2, DISP_TAG_LEVEL );
}

//-----------------------------------------------------------------------------
// Purpose: Every point within the brush's reach moves, by its share; Smooth works from the
//          positions before the stroke, so the order of points doesn't tell.
//-----------------------------------------------------------------------------
bool PaintGeometry( DispInfo_t &disp, const FaceGeometry_t &face, const DispBrush_t &brush )
{
	const std::optional<DispMesh_t> mesh = BuildDisplacement( disp, face );
	if ( !mesh || ( brush.m_bSpatial && brush.m_flRadius <= 0.0 ) )
	{
		return false;
	}
	const auto nSize = static_cast<std::size_t>( mesh->m_nSize );
	const Point_t axis = Length( brush.m_Axis ) > 1e-9 ? Normalized( brush.m_Axis ) : face.m_Plane.m_Normal;
	const std::vector<double> weights = BrushWeights( *mesh, brush );
	bool bChanged = false;
	for ( std::size_t i = 0; i < mesh->m_Points.size(); i++ )
	{
		const Point_t &p = mesh->m_Points[ i ];
		const Point_t direction = brush.m_bSubdivNormal && i < disp.m_OffsetNormals.size() && Length( ToPoint( disp.m_OffsetNormals[ i ] ) ) > 1e-9 ? Normalized( ToPoint( disp.m_OffsetNormals[ i ] ) ) : axis;
		const double w = weights[ i ];
		if ( w <= 0.0 )
		{
			continue;
		}
		Point_t move{};
		switch ( brush.m_nEffect )
		{
			case PaintEffect_t::PAINT_EFFECT_RAISE_LOWER:
				move = Scale( direction, brush.m_flAmount * w );
				break;
			case PaintEffect_t::PAINT_EFFECT_RAISE_TO:
				move = Scale( direction, ( brush.m_flAmount - Dot( Subtract( p, mesh->m_Flat[ i ] ), direction ) ) * w );
				break;
			case PaintEffect_t::PAINT_EFFECT_SMOOTH:
			{
				Point_t average{};
				const std::vector<std::size_t> round = Neighbours( nSize, i );
				for ( const std::size_t n : round )
				{
					average = Add( average, mesh->m_Points[ n ] );
				}
				average = Scale( average, 1.0 / static_cast<double>( std::max<std::size_t>( round.size(), 1 ) ) );
				move = Scale( Subtract( average, p ), w * std::clamp( brush.m_flAmount, 0.0, 1.0 ) );
				break;
			}
		}
		if ( Length( move ) < 1e-9 )
		{
			continue;
		}
		SetPosition( disp, *mesh, face, i, Add( p, move ) );
		bChanged = true;
	}
	return bChanged;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool PaintAlpha( DispInfo_t &disp, const FaceGeometry_t &face, const DispBrush_t &brush )
{
	const std::optional<DispMesh_t> mesh = BuildDisplacement( disp, face );
	if ( !mesh || ( brush.m_bSpatial && brush.m_flRadius <= 0.0 ) )
	{
		return false;
	}
	const auto nSize = static_cast<std::size_t>( mesh->m_nSize );
	const std::vector<float> before = disp.m_Alphas;
	const std::vector<double> weights = BrushWeights( *mesh, brush );
	bool bChanged = false;
	for ( std::size_t i = 0; i < mesh->m_Points.size() && i < disp.m_Alphas.size(); i++ )
	{
		const double w = weights[ i ];
		if ( w <= 0.0 )
		{
			continue;
		}
		double flAlpha = before[ i ];
		switch ( brush.m_nEffect )
		{
			case PaintEffect_t::PAINT_EFFECT_RAISE_LOWER:
				flAlpha += brush.m_flAmount * w;
				break;
			case PaintEffect_t::PAINT_EFFECT_RAISE_TO:
				flAlpha += ( brush.m_flAmount - flAlpha ) * w;
				break;
			case PaintEffect_t::PAINT_EFFECT_SMOOTH:
			{
				double flAverage = 0.0;
				const std::vector<std::size_t> round = Neighbours( nSize, i );
				for ( const std::size_t n : round )
				{
					flAverage += before[ n ];
				}
				flAverage /= static_cast<double>( std::max<std::size_t>( round.size(), 1 ) );
				flAlpha += ( flAverage - flAlpha ) * w * 0.5;
				break;
			}
		}
		const auto flNew = static_cast<float>( std::clamp( flAlpha, 0.0, 255.0 ) );
		bChanged = bChanged || flNew != before[ i ];
		disp.m_Alphas[ i ] = flNew;
	}
	return bChanged;
}

//-----------------------------------------------------------------------------
// Purpose: Show what a displacement's stored mask disables, and where unequal seam grids
//          force interpolation. Match full edges and half-edge T-junctions in either direction.
//-----------------------------------------------------------------------------
std::vector<std::size_t> CollapsibleVertices( const DispInfo_t &disp, const DispMesh_t &mesh, std::span<const DispMesh_t> neighbours )
{
	std::vector<bool> collapsed( mesh.m_Points.size(), false );
	for ( std::size_t i = 0; i < collapsed.size(); ++i )
	{
		collapsed[ i ] = i / 32 < disp.m_AllowedVerts.size() && !( static_cast<std::uint32_t>( disp.m_AllowedVerts[ i / 32 ] ) & ( 1u << ( i % 32 ) ) );
	}
	for ( const DispMesh_t &other : neighbours )
	{
		if ( other.m_nSize < 2 )
		{
			continue;
		}
		for ( std::size_t a = 0; a < 4; ++a )
		{
			for ( std::size_t b = 0; b < 4; ++b )
			{
				auto match = EdgeMatch( mesh.m_Corners[ a ], mesh.m_Corners[ ( a + 1 ) % 4 ], other.m_Corners[ b ], other.m_Corners[ ( b + 1 ) % 4 ] );
				if ( !match )
				{
					const auto reverse = EdgeMatch( other.m_Corners[ b ], other.m_Corners[ ( b + 1 ) % 4 ], mesh.m_Corners[ a ], mesh.m_Corners[ ( a + 1 ) % 4 ] );
					if ( reverse )
						match = std::pair{ -reverse->first / reverse->second, 1.0 / reverse->second };
				}
				if ( !match )
					continue;
				for ( const auto &[ i, t ] : EdgePoints( mesh.m_nSize, a ) )
				{
					const double u = match->first + match->second * t;
					if ( u < -1e-6 || u > 1.0 + 1e-6 )
						continue;
					const double flVertex = u * ( other.m_nSize - 1 );
					collapsed[ i ] = collapsed[ i ] || std::abs( flVertex - std::round( flVertex ) ) > 1e-6;
				}
			}
		}
	}
	std::vector<std::size_t> indices;
	for ( std::size_t i = 0; i < collapsed.size(); ++i )
	{
		if ( collapsed[ i ] )
			indices.push_back( i );
	}
	return indices;
}

//-----------------------------------------------------------------------------
// Purpose: Push adds a radial offset; Carve samples an editable cross-section. Attenuation
//          caps a vertex's contribution over the stroke instead of adding on every sample.
//-----------------------------------------------------------------------------
bool SculptGeometry( DispInfo_t &disp, const FaceGeometry_t &face, const DispBrush_t &brush, const SculptBrush_t &sculpt, std::vector<double> *pApplied )
{
	const auto mesh = BuildDisplacement( disp, face );
	if ( !mesh || brush.m_flRadius <= 0 )
	{
		return false;
	}
	if ( pApplied )
	{
		pApplied->resize( mesh->m_Points.size(), 0.0 );
	}
	const double flAmount = ( sculpt.m_bAdaptive ? sculpt.m_flPercent * brush.m_flRadius : sculpt.m_flDistance ) * ( brush.m_flAmount < 0 ? -1.0 : 1.0 );
	const Point_t axis = Length( brush.m_Axis ) > 1e-9 ? Normalized( brush.m_Axis ) : face.m_Plane.m_Normal;
	const Point_t across = Normalized( sculpt.m_Right );
	bool bChanged = false;
	for ( std::size_t i = 0; i < mesh->m_Points.size(); ++i )
	{
		const Point_t &p = mesh->m_Points[ i ];
		const double d = Length( Subtract( p, brush.m_Centre ) ) / brush.m_flRadius;
		if ( d >= 1.0 )
		{
			continue;
		}
		const double flPlateau = std::clamp( sculpt.m_flFalloffPosition, 0.0, 1.0 );
		const double w = d <= flPlateau ? 1.0 : sculpt.m_flFalloffFinal + ( 1.0 - sculpt.m_flFalloffFinal ) * Weight( d - flPlateau, 1.0 - flPlateau );
		double flProfile = 1.0;
		if ( sculpt.m_bCarve )
		{
			const double t = std::clamp( ( Dot( Subtract( p, brush.m_Centre ), across ) / brush.m_flRadius + 1.0 ) * 4.0, 0.0, 8.0 );
			const std::size_t n = std::min<std::size_t>( static_cast<std::size_t>( t ), 7 );
			flProfile = sculpt.m_Profile[ n ] + ( sculpt.m_Profile[ n + 1 ] - sculpt.m_Profile[ n ] ) * ( t - n );
		}
		double flOffset = flAmount * w * flProfile;
		if ( sculpt.m_bAttenuated && pApplied )
		{
			double &flWas = ( *pApplied )[ i ];
			const double flTarget = flOffset >= 0 ? std::max( flWas, flOffset ) : std::min( flWas, flOffset );
			flOffset = flTarget - flWas;
			flWas = flTarget;
		}
		Point_t direction = axis;
		if ( sculpt.m_bTowardsCentre )
		{
			const Point_t towards = Subtract( brush.m_Centre, p );
			if ( Length( towards ) > 1e-9 )
			{
				direction = Normalized( towards );
			}
		}
		Point_t move = Scale( direction, flOffset );
		if ( sculpt.m_flSmoothing > 0 && ( !sculpt.m_bAttenuated || std::abs( flOffset ) > 1e-9 ) )
		{
			const auto round = Neighbours( mesh->m_nSize, i );
			Point_t average{};
			for ( const std::size_t j : round )
			{
				average = Add( average, mesh->m_Points[ j ] );
			}
			if ( !round.empty() )
			{
				move = Add( move, Scale( Subtract( Scale( average, 1.0 / round.size() ), p ), std::clamp( sculpt.m_flSmoothing, 0.0, 1.0 ) * w ) );
			}
		}
		if ( Length( move ) > 1e-9 )
		{
			SetPosition( disp, *mesh, face, i, Add( p, move ) );
			bChanged = true;
		}
	}
	return bChanged;
}

//-----------------------------------------------------------------------------
// Purpose: The offsets (Hammer's own nudges) stay; what the normals and distances make scales.
//-----------------------------------------------------------------------------
void ScaleDisplacement( DispInfo_t &disp, double flScale )
{
	for ( float &flDistance : disp.m_Distances )
	{
		flDistance = static_cast<float>( flDistance * flScale );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void AddNoise( DispInfo_t &disp, double flMin, double flMax, const Point_t &axis, std::uint32_t nSeed )
{
	if ( flMax < flMin )
	{
		std::swap( flMin, flMax );
	}
	std::mt19937 random( nSeed );
	std::uniform_real_distribution<double> amount( flMin, flMax );
	const Point_t direction = Length( axis ) > 1e-9 ? Normalized( axis ) : Point_t{ 0, 0, 1 };
	for ( std::size_t i = 0; i < disp.m_Distances.size(); i++ )
	{
		const Point_t field = Subtract( DispField( disp, i ), i < disp.m_Offsets.size() ? ToPoint( disp.m_Offsets[ i ] ) : Point_t{} );
		SetDispField( disp, i, Add( field, Scale( direction, amount( random ) ) ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void InvertAlpha( DispInfo_t &disp )
{
	for ( float &flAlpha : disp.m_Alphas )
	{
		flAlpha = 255.0f - flAlpha;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int TagOfNormal( const Point_t &normal )
{
	const double flUp = normal[ 2 ];
	return flUp >= BUILDABLE_UP ? DISP_TAG_LEVEL : flUp >= WALKABLE_UP ? DISP_TAG_WALKABLE
																	   : DISP_TAG_STEEP;
}

//-----------------------------------------------------------------------------
// Purpose: The tags go square by square along each row, the square's two triangles in the
//          order the mesh makes them.
//-----------------------------------------------------------------------------
void TagTriangles( DispInfo_t &disp, const FaceGeometry_t &face )
{
	const std::optional<DispMesh_t> mesh = BuildDisplacement( disp, face );
	if ( !mesh )
	{
		return;
	}
	disp.m_TriangleTags.assign( mesh->m_Indices.size() / 3, DISP_TAG_LEVEL );
	for ( std::size_t t = 0; t * 3 + 2 < mesh->m_Indices.size(); t++ )
	{
		const Point_t a = mesh->m_Points[ mesh->m_Indices[ t * 3 ] ];
		const Point_t b = mesh->m_Points[ mesh->m_Indices[ t * 3 + 1 ] ];
		const Point_t c = mesh->m_Points[ mesh->m_Indices[ t * 3 + 2 ] ];
		// Wound clockwise from the front, as the face is.
		const Point_t normal = Cross( Subtract( c, a ), Subtract( b, a ) );
		disp.m_TriangleTags[ t ] = TagOfNormal( Length( normal ) > 1e-12 ? Normalized( normal ) : face.m_Plane.m_Normal );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Each displacement's edges are tried against every other face's; the meshes are
//          those from before any sewing, so a neighbour's edge is a fixed target.
//-----------------------------------------------------------------------------
bool SewDisplacements( const std::vector<DispFace_t> &faces )
{
	std::vector<std::optional<DispMesh_t>> meshes( faces.size() );
	for ( std::size_t i = 0; i < faces.size(); i++ )
	{
		if ( faces[ i ].m_pDisp && faces[ i ].m_pFace )
		{
			meshes[ i ] = BuildDisplacement( *faces[ i ].m_pDisp, *faces[ i ].m_pFace );
		}
	}
	bool bChanged = false;
	std::set<std::pair<std::size_t, std::size_t>> averaged; // pairs done both ways at once
	for ( std::size_t a = 0; a < faces.size(); a++ )
	{
		if ( !meshes[ a ] )
		{
			continue;
		}
		const DispMesh_t &meshA = *meshes[ a ];
		const auto nA = static_cast<std::size_t>( meshA.m_nSize );
		for ( std::size_t b = 0; b < faces.size(); b++ )
		{
			if ( a == b || !faces[ b ].m_pFace )
			{
				continue;
			}
			// The other face's edges: its grid's when it is a displacement, its polygon's else.
			std::vector<std::pair<Point_t, Point_t>> edgesB;
			if ( meshes[ b ] )
			{
				for ( std::size_t k = 0; k < 4; k++ )
				{
					edgesB.emplace_back( meshes[ b ]->m_Corners[ k ], meshes[ b ]->m_Corners[ ( k + 1 ) % 4 ] );
				}
			}
			else
			{
				const std::vector<Point_t> &points = faces[ b ].m_pFace->m_Points;
				for ( std::size_t k = 0; k < points.size(); k++ )
				{
					edgesB.emplace_back( points[ k ], points[ ( k + 1 ) % points.size() ] );
				}
			}
			for ( std::size_t ka = 0; ka < 4; ka++ )
			{
				for ( std::size_t kb = 0; kb < edgesB.size(); kb++ )
				{
					const std::optional<std::pair<double, double>> match = EdgeMatch( meshA.m_Corners[ ka ], meshA.m_Corners[ ( ka + 1 ) % 4 ], edgesB[ kb ].first, edgesB[ kb ].second );
					if ( !match )
					{
						continue;
					}
					const bool bFull = std::abs( match->second ) > 0.75;
					const auto nB = meshes[ b ] ? static_cast<std::size_t>( meshes[ b ]->m_nSize ) : 0;
					if ( meshes[ b ] && bFull && nB > nA )
					{
						continue; // the finer grid moves; that is the other way round
					}
					const bool bAverage = meshes[ b ] && bFull && nB == nA;
					if ( bAverage && averaged.contains( { std::min( a, b ), std::max( a, b ) } ) )
					{
						continue;
					}
					if ( bAverage )
					{
						averaged.insert( { std::min( a, b ), std::max( a, b ) } );
					}
					for ( const auto &[ i, t ] : EdgePoints( nA, ka ) )
					{
						const double tB = match->first + t * match->second;
						Point_t target;
						if ( !meshes[ b ] )
						{
							target = Lerp( edgesB[ kb ].first, edgesB[ kb ].second, tB );
						}
						else
						{
							target = EdgePosition( *meshes[ b ], EdgePlace( nB, kb, tB ) );
						}
						if ( bAverage )
						{
							target = Lerp( meshA.m_Points[ i ], target, 0.5 );
							// The other grid's point at the same place takes it too.
							const auto [ r, c ] = EdgePlace( nB, kb, tB );
							const std::size_t j = static_cast<std::size_t>( std::lround( r ) ) * nB + static_cast<std::size_t>( std::lround( c ) );
							if ( Length( Subtract( meshes[ b ]->m_Points[ j ], target ) ) > 1e-6 )
							{
								SetPosition( *faces[ b ].m_pDisp, *meshes[ b ], *faces[ b ].m_pFace, j, target );
								bChanged = true;
							}
						}
						if ( Length( Subtract( meshA.m_Points[ i ], target ) ) > 1e-6 )
						{
							SetPosition( *faces[ a ].m_pDisp, meshA, *faces[ a ].m_pFace, i, target );
							bChanged = true;
						}
					}
				}
			}
		}
	}
	return bChanged;
}

//-----------------------------------------------------------------------------
// Purpose: Round the base quadrilaterals rather than smoothing the painted vertex field.
//          Shared control edges refine together; the open rim stays where adjacent unselected
//          geometry expects it. Painted distances and alpha survive another subdivision.
//-----------------------------------------------------------------------------
bool SubdivideDisplacements( const std::vector<DispFace_t> &faces )
{
	struct Quad_t
	{
		std::array<std::size_t, 4> m_Vertices;
		std::size_t m_nFace;
		int m_nRow, m_nColumn, m_nWidth;
	};
	struct Edge_t
	{
		Point_t m_FaceSum{};
		int m_nFaces = 0;
		std::size_t m_nVertex = 0;
	};
	std::vector<std::optional<DispMesh_t>> meshes( faces.size() );
	int nPower = 0;
	for ( std::size_t f = 0; f < faces.size(); ++f )
	{
		if ( faces[ f ].m_pDisp && faces[ f ].m_pFace )
		{
			meshes[ f ] = BuildDisplacement( *faces[ f ].m_pDisp, *faces[ f ].m_pFace );
			if ( meshes[ f ] )
			{
				nPower = std::max( nPower, faces[ f ].m_pDisp->m_nPower );
			}
		}
	}
	if ( nPower == 0 )
	{
		return false;
	}
	const int nWidth = 1 << nPower;
	std::map<std::tuple<long long, long long, long long>, std::size_t> ids;
	std::vector<Point_t> points;
	std::vector<Quad_t> quads;
	for ( std::size_t f = 0; f < faces.size(); ++f )
	{
		if ( !meshes[ f ] )
		{
			continue;
		}
		Quad_t quad{ {}, f, 0, 0, nWidth };
		for ( std::size_t k = 0; k < 4; ++k )
		{
			const Point_t &p = meshes[ f ]->m_Corners[ k ];
			const auto [ it, bNew ] = ids.try_emplace( Key( p ), points.size() );
			if ( bNew )
			{
				points.push_back( p );
			}
			quad.m_Vertices[ k ] = it->second;
		}
		quads.push_back( quad );
	}
	for ( int nLevel = 0; nLevel < nPower; ++nLevel )
	{
		std::map<std::pair<std::size_t, std::size_t>, Edge_t> edges;
		std::vector<Point_t> centres, faceSums( points.size() ), edgeSums( points.size() );
		std::vector<int> counts( points.size(), 0 ), edgeCounts( points.size(), 0 );
		std::vector<bool> boundary( points.size(), false );
		for ( const Quad_t &quad : quads )
		{
			Point_t centre{};
			for ( const std::size_t n : quad.m_Vertices )
			{
				centre = Add( centre, Scale( points[ n ], 0.25 ) );
			}
			centres.push_back( centre );
			for ( std::size_t k = 0; k < 4; ++k )
			{
				const std::size_t u = quad.m_Vertices[ k ], v = quad.m_Vertices[ ( k + 1 ) % 4 ];
				faceSums[ u ] = Add( faceSums[ u ], centre );
				counts[ u ]++;
				Edge_t &edge = edges[ std::minmax( u, v ) ];
				edge.m_FaceSum = Add( edge.m_FaceSum, centre );
				edge.m_nFaces++;
			}
		}
		for ( const auto &[ ends, edge ] : edges )
		{
			const Point_t midpoint = Lerp( points[ ends.first ], points[ ends.second ], 0.5 );
			for ( const std::size_t n : { ends.first, ends.second } )
			{
				edgeSums[ n ] = Add( edgeSums[ n ], midpoint );
				edgeCounts[ n ]++;
				boundary[ n ] = boundary[ n ] || edge.m_nFaces != 2;
			}
		}
		std::vector<Point_t> next = points;
		for ( std::size_t n = 0; n < points.size(); ++n )
		{
			if ( !boundary[ n ] && counts[ n ] > 0 && edgeCounts[ n ] > 0 )
			{
				const double flValence = counts[ n ];
				next[ n ] = Scale( Add( Add( Scale( faceSums[ n ], 1.0 / flValence ), Scale( edgeSums[ n ], 2.0 / edgeCounts[ n ] ) ), Scale( points[ n ], flValence - 3.0 ) ), 1.0 / flValence );
			}
		}
		for ( auto &[ ends, edge ] : edges )
		{
			edge.m_nVertex = next.size();
			next.push_back( edge.m_nFaces == 2 ? Scale( Add( Add( points[ ends.first ], points[ ends.second ] ), edge.m_FaceSum ), 0.25 ) : Lerp( points[ ends.first ], points[ ends.second ], 0.5 ) );
		}
		std::vector<Quad_t> refined;
		refined.reserve( quads.size() * 4 );
		for ( std::size_t q = 0; q < quads.size(); ++q )
		{
			const Quad_t &quad = quads[ q ];
			const auto &v = quad.m_Vertices;
			std::array<std::size_t, 4> e;
			for ( std::size_t k = 0; k < 4; ++k )
			{
				e[ k ] = edges.at( std::minmax( v[ k ], v[ ( k + 1 ) % 4 ] ) ).m_nVertex;
			}
			const std::size_t nCentre = next.size();
			next.push_back( centres[ q ] );
			const int h = quad.m_nWidth / 2, r = quad.m_nRow, c = quad.m_nColumn;
			refined.push_back( { { v[ 0 ], e[ 0 ], nCentre, e[ 3 ] }, quad.m_nFace, r, c, h } );
			refined.push_back( { { e[ 0 ], v[ 1 ], e[ 1 ], nCentre }, quad.m_nFace, r + h, c, h } );
			refined.push_back( { { nCentre, e[ 1 ], v[ 2 ], e[ 2 ] }, quad.m_nFace, r + h, c + h, h } );
			refined.push_back( { { e[ 3 ], nCentre, e[ 2 ], v[ 3 ] }, quad.m_nFace, r, c + h, h } );
		}
		points = std::move( next );
		quads = std::move( refined );
	}
	std::vector<std::vector<Point_t>> surfaces( faces.size() );
	for ( std::size_t f = 0; f < faces.size(); ++f )
	{
		if ( meshes[ f ] )
		{
			surfaces[ f ] = meshes[ f ]->m_Flat;
		}
	}
	for ( const Quad_t &quad : quads )
	{
		const int nSize = meshes[ quad.m_nFace ]->m_nSize;
		const int nStride = nWidth / ( nSize - 1 );
		const std::array<std::pair<int, int>, 4> places{ { { quad.m_nRow, quad.m_nColumn }, { quad.m_nRow + 1, quad.m_nColumn }, { quad.m_nRow + 1, quad.m_nColumn + 1 }, { quad.m_nRow, quad.m_nColumn + 1 } } };
		for ( std::size_t k = 0; k < 4; ++k )
		{
			const auto [ r, c ] = places[ k ];
			if ( r % nStride == 0 && c % nStride == 0 )
			{
				surfaces[ quad.m_nFace ][ ( r / nStride ) * nSize + c / nStride ] = points[ quad.m_Vertices[ k ] ];
			}
		}
	}
	std::vector<std::optional<DispMesh_t>> roundedBases( faces.size() );
	std::map<std::tuple<long long, long long, long long>, Point_t> normalSums;
	bool bChanged = false;
	for ( std::size_t f = 0; f < faces.size(); ++f )
	{
		if ( !meshes[ f ] )
		{
			continue;
		}
		DispInfo_t &disp = *faces[ f ].m_pDisp;
		bChanged = bChanged || !disp.m_bSubdivided;
		for ( std::size_t i = 0; i < surfaces[ f ].size(); ++i )
		{
			const Point_t offset = Subtract( surfaces[ f ][ i ], meshes[ f ]->m_Flat[ i ] );
			const CVec3 stored = ToVec3( offset );
			bChanged = bChanged || Length( Subtract( ToPoint( stored ), ToPoint( disp.m_Offsets[ i ] ) ) ) > 1e-6;
			disp.m_Offsets[ i ] = stored;
		}
		DispInfo_t base = disp;
		std::ranges::fill( base.m_Distances, 0.0f );
		base.m_flElevation = 0;
		roundedBases[ f ] = BuildDisplacement( base, *faces[ f ].m_pFace );
		if ( roundedBases[ f ] )
		{
			for ( std::size_t i = 0; i < roundedBases[ f ]->m_Points.size(); ++i )
			{
				Point_t &sum = normalSums[ Key( roundedBases[ f ]->m_Points[ i ] ) ];
				sum = Add( sum, roundedBases[ f ]->m_Normals[ i ] );
			}
		}
		disp.m_bSubdivided = true;
	}
	for ( std::size_t f = 0; f < faces.size(); ++f )
	{
		if ( !roundedBases[ f ] )
			continue;
		DispInfo_t &disp = *faces[ f ].m_pDisp;
		for ( std::size_t i = 0; i < roundedBases[ f ]->m_Points.size(); ++i )
		{
			const CVec3 normal = ToVec3( Normalized( normalSums.at( Key( roundedBases[ f ]->m_Points[ i ] ) ) ) );
			bChanged = bChanged || Length( Subtract( ToPoint( normal ), ToPoint( disp.m_OffsetNormals[ i ] ) ) ) > 1e-6;
			disp.m_OffsetNormals[ i ] = normal;
		}
	}
	return bChanged;
}

//-----------------------------------------------------------------------------
// Purpose: The faces' solids copied, the displacements pointed at in the copies, and the
//          copies made the edit when the function changed something.
//-----------------------------------------------------------------------------
MapEdit_t EditDisplacements( const CScene &scene, const std::vector<FaceRef_t> &faces, const std::function<bool( std::vector<DispFace_t> & )> &pfnEdit )
{
	MapEdit_t edit;
	std::vector<FaceRef_t> sorted = faces;
	std::ranges::sort( sorted );
	sorted.erase( std::ranges::unique( sorted ).begin(), sorted.end() );
	std::vector<std::pair<std::uint32_t, Solid_t>> solids;
	for ( const FaceRef_t &face : sorted )
	{
		if ( face.m_nSolid >= scene.m_Map.m_Solids.size() || ( !solids.empty() && solids.back().first == face.m_nSolid ) )
		{
			continue;
		}
		solids.emplace_back( face.m_nSolid, scene.m_Map.m_Solids[ face.m_nSolid ] );
	}
	std::vector<DispFace_t> refs;
	for ( const FaceRef_t &face : sorted )
	{
		const auto it = std::ranges::find_if( solids, [ & ]( const auto &entry ) { return entry.first == face.m_nSolid; } );
		if ( it == solids.end() || face.m_nSide >= it->second.m_Sides.size() )
		{
			continue;
		}
		Side_t &side = it->second.m_Sides[ face.m_nSide ];
		DispFace_t ref;
		ref.m_Ref = face;
		ref.m_pSide = &side;
		ref.m_pDisp = side.m_pDisp.get();
		ref.m_pFace = scene.m_Solids[ face.m_nSolid ].FindFace( face.m_nSide );
		refs.push_back( ref );
	}
	if ( refs.empty() || !pfnEdit( refs ) )
	{
		return edit;
	}
	for ( DispFace_t &ref : refs )
	{
		if ( ref.m_pSide->m_pDisp && ref.m_pFace )
		{
			TagTriangles( *ref.m_pSide->m_pDisp, *ref.m_pFace );
		}
	}
	for ( auto &[ nSolid, solid ] : solids )
	{
		solid.m_bChanged = true;
		edit.m_Solids.emplace_back( nSolid, std::move( solid ) );
	}
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<FaceRef_t> DisplacementFaces( const CScene &scene, const std::vector<FaceRef_t> &faces )
{
	std::vector<FaceRef_t> out;
	for ( const FaceRef_t &face : faces )
	{
		if ( face.m_nSolid < scene.m_Map.m_Solids.size() && face.m_nSide < scene.m_Map.m_Solids[ face.m_nSolid ].m_Sides.size() && scene.m_Map.m_Solids[ face.m_nSolid ].m_Sides[ face.m_nSide ].m_pDisp )
		{
			out.push_back( face );
		}
	}
	return out;
}

} // namespace xv::map
