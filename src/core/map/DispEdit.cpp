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
	if ( !mesh || brush.m_flRadius <= 0.0 )
	{
		return false;
	}
	const auto nSize = static_cast<std::size_t>( mesh->m_nSize );
	const Point_t axis = Length( brush.m_Axis ) > 1e-9 ? Normalized( brush.m_Axis ) : face.m_Plane.m_Normal;
	bool bChanged = false;
	for ( std::size_t i = 0; i < mesh->m_Points.size(); i++ )
	{
		const Point_t &p = mesh->m_Points[ i ];
		const double w = Weight( Length( Subtract( p, brush.m_Centre ) ), brush.m_flRadius, brush.m_bSoftEdge );
		if ( w <= 0.0 )
		{
			continue;
		}
		Point_t move{};
		switch ( brush.m_nEffect )
		{
			case PaintEffect_t::PAINT_EFFECT_RAISE_LOWER:
				move = Scale( axis, brush.m_flAmount * w );
				break;
			case PaintEffect_t::PAINT_EFFECT_RAISE_TO:
				move = Scale( axis, ( brush.m_flAmount - Dot( p, axis ) ) * w );
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
	if ( !mesh || brush.m_flRadius <= 0.0 )
	{
		return false;
	}
	const auto nSize = static_cast<std::size_t>( mesh->m_nSize );
	const std::vector<float> before = disp.m_Alphas;
	bool bChanged = false;
	for ( std::size_t i = 0; i < mesh->m_Points.size() && i < disp.m_Alphas.size(); i++ )
	{
		const double w = Weight( Length( Subtract( mesh->m_Points[ i ], brush.m_Centre ) ), brush.m_flRadius, brush.m_bSoftEdge );
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
// Purpose: The displacements as one mesh (points at one place are one point), smoothed
//          toward their neighbours; a point on an edge no other triangle shares is the
//          surface's rim and stays, so the terrain still meets what is round it.
//-----------------------------------------------------------------------------
bool SubdivideDisplacements( const std::vector<DispFace_t> &faces, int nPasses )
{
	std::vector<std::optional<DispMesh_t>> meshes( faces.size() );
	std::map<std::tuple<long long, long long, long long>, std::size_t> ids;
	std::vector<Point_t> positions;
	std::vector<std::vector<std::size_t>> pointIds( faces.size() );
	std::map<std::pair<std::size_t, std::size_t>, int> edgeUses;
	std::vector<std::set<std::size_t>> neighbours;
	for ( std::size_t f = 0; f < faces.size(); f++ )
	{
		if ( !faces[ f ].m_pDisp || !faces[ f ].m_pFace )
		{
			continue;
		}
		meshes[ f ] = BuildDisplacement( *faces[ f ].m_pDisp, *faces[ f ].m_pFace );
		if ( !meshes[ f ] )
		{
			continue;
		}
		for ( const Point_t &p : meshes[ f ]->m_Points )
		{
			const auto [ it, bNew ] = ids.try_emplace( Key( p ), positions.size() );
			if ( bNew )
			{
				positions.push_back( p );
				neighbours.emplace_back();
			}
			pointIds[ f ].push_back( it->second );
		}
		const std::vector<std::uint32_t> &indices = meshes[ f ]->m_Indices;
		for ( std::size_t t = 0; t + 2 < indices.size(); t += 3 )
		{
			for ( std::size_t k = 0; k < 3; k++ )
			{
				const std::size_t u = pointIds[ f ][ indices[ t + k ] ];
				const std::size_t v = pointIds[ f ][ indices[ t + ( k + 1 ) % 3 ] ];
				if ( u == v )
				{
					continue;
				}
				edgeUses[ { std::min( u, v ), std::max( u, v ) } ]++;
				neighbours[ u ].insert( v );
				neighbours[ v ].insert( u );
			}
		}
	}
	if ( positions.empty() )
	{
		return false;
	}
	std::vector<bool> fixed( positions.size(), false );
	for ( const auto &[ edge, nUses ] : edgeUses )
	{
		if ( nUses == 1 )
		{
			fixed[ edge.first ] = fixed[ edge.second ] = true;
		}
	}
	const std::vector<Point_t> before = positions;
	for ( int nPass = 0; nPass < nPasses; nPass++ )
	{
		std::vector<Point_t> next = positions;
		for ( std::size_t i = 0; i < positions.size(); i++ )
		{
			if ( fixed[ i ] || neighbours[ i ].empty() )
			{
				continue;
			}
			Point_t average{};
			for ( const std::size_t n : neighbours[ i ] )
			{
				average = Add( average, positions[ n ] );
			}
			average = Scale( average, 1.0 / static_cast<double>( neighbours[ i ].size() ) );
			next[ i ] = Lerp( positions[ i ], average, 0.5 );
		}
		positions = std::move( next );
	}
	bool bChanged = false;
	for ( std::size_t f = 0; f < faces.size(); f++ )
	{
		if ( !meshes[ f ] )
		{
			continue;
		}
		for ( std::size_t i = 0; i < pointIds[ f ].size(); i++ )
		{
			const Point_t &target = positions[ pointIds[ f ][ i ] ];
			if ( Length( Subtract( target, before[ pointIds[ f ][ i ] ] ) ) > 1e-6 )
			{
				SetPosition( *faces[ f ].m_pDisp, *meshes[ f ], *faces[ f ].m_pFace, i, target );
				bChanged = true;
			}
		}
		faces[ f ].m_pDisp->m_bSubdivided = true;
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
