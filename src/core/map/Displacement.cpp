//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Displacement.h. The layout (which corner starts the grid, which way rows run,
//          which diagonal each square takes) follows the facts in the VDC's Displacement and
//          VMF articles and the SDK's builddisp.
//
//===========================================================================//
#include "Displacement.h"

#include <algorithm>
#include <limits>

namespace xv::map
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Box_t DispMesh_t::GetBounds() const
{
	Box_t box;
	for ( const Point_t &p : m_Points )
	{
		box.Add( p );
	}
	return box;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<DispMesh_t> BuildDisplacement( const DispInfo_t &disp, const FaceGeometry_t &face, std::string *pError )
{
	auto fail = [ pError ]( std::string szReason ) -> std::optional<DispMesh_t> {
		if ( pError )
		{
			*pError = std::move( szReason );
		}
		return std::nullopt;
	};
	if ( !disp.m_bValid )
	{
		return fail( "the displacement's power or rows didn't read" );
	}
	if ( face.m_Points.size() != 4 )
	{
		return fail( "a displacement needs a face with four corners; this one has " + std::to_string( face.m_Points.size() ) );
	}
	const auto nSize = static_cast<std::size_t>( disp.GetSize() );
	const std::size_t nCount = nSize * nSize;
	if ( disp.m_Normals.size() != nCount || disp.m_Distances.size() != nCount || disp.m_Offsets.size() != nCount || disp.m_Alphas.size() != nCount )
	{
		return fail( "the displacement's rows don't match its power" );
	}

	DispMesh_t mesh;
	mesh.m_nSize = static_cast<int>( nSize );
	// The grid starts at the corner nearest the start position and follows the face round.
	std::size_t nStart = 0;
	double flNearest = std::numeric_limits<double>::infinity();
	for ( std::size_t i = 0; i < 4; i++ )
	{
		const Point_t d = Subtract( face.m_Points[ i ], disp.m_Start );
		if ( Dot( d, d ) < flNearest )
		{
			flNearest = Dot( d, d );
			nStart = i;
		}
	}
	for ( std::size_t i = 0; i < 4; i++ )
	{
		mesh.m_Corners[ i ] = face.m_Points[ ( i + nStart ) % 4 ];
	}

	const double flStep = 1.0 / static_cast<double>( nSize - 1 );
	const Point_t vecElevation = Scale( face.m_Plane.m_Normal, disp.m_flElevation );
	mesh.m_Points.resize( nCount );
	mesh.m_Flat.resize( nCount );
	mesh.m_Alphas.resize( nCount );
	for ( std::size_t r = 0; r < nSize; r++ )
	{
		// Each row runs between the two edges that leave the first and last corners.
		const Point_t vecRowStart = Lerp( mesh.m_Corners[ 0 ], mesh.m_Corners[ 1 ], static_cast<double>( r ) * flStep );
		const Point_t vecRowEnd = Lerp( mesh.m_Corners[ 3 ], mesh.m_Corners[ 2 ], static_cast<double>( r ) * flStep );
		for ( std::size_t c = 0; c < nSize; c++ )
		{
			const std::size_t i = r * nSize + c;
			const Point_t vecFlat = Lerp( vecRowStart, vecRowEnd, static_cast<double>( c ) * flStep );
			const Point_t vecField = Add( Scale( ToPoint( disp.m_Normals[ i ] ), disp.m_Distances[ i ] ), ToPoint( disp.m_Offsets[ i ] ) );
			mesh.m_Flat[ i ] = vecFlat;
			mesh.m_Points[ i ] = Add( Add( vecFlat, vecElevation ), vecField );
			mesh.m_Alphas[ i ] = std::clamp( disp.m_Alphas[ i ] / 255.0f, 0.0f, 1.0f );
		}
	}

	// Squares alternate their diagonal with the parity of their first point's index.
	const auto nWidth = static_cast<std::uint32_t>( nSize );
	mesh.m_Indices.reserve( ( nSize - 1 ) * ( nSize - 1 ) * 6 );
	for ( std::uint32_t r = 0; r + 1 < nWidth; r++ )
	{
		for ( std::uint32_t c = 0; c + 1 < nWidth; c++ )
		{
			const std::uint32_t q = r * nWidth + c;
			if ( q % 2 == 1 )
			{
				mesh.m_Indices.insert( mesh.m_Indices.end(), { q, q + nWidth, q + 1, q + 1, q + nWidth, q + nWidth + 1 } );
			}
			else
			{
				mesh.m_Indices.insert( mesh.m_Indices.end(), { q, q + nWidth, q + nWidth + 1, q, q + nWidth + 1, q + 1 } );
			}
		}
	}
	// Every triangle of that pattern turns the same way; match the face's winding.
	const Point_t vecTurn = Cross( Subtract( mesh.m_Flat[ mesh.m_Indices[ 1 ] ], mesh.m_Flat[ mesh.m_Indices[ 0 ] ] ), Subtract( mesh.m_Flat[ mesh.m_Indices[ 2 ] ], mesh.m_Flat[ mesh.m_Indices[ 0 ] ] ) );
	if ( Dot( vecTurn, face.m_Plane.m_Normal ) > 0.0 )
	{
		for ( std::size_t i = 0; i < mesh.m_Indices.size(); i += 3 )
		{
			std::swap( mesh.m_Indices[ i + 1 ], mesh.m_Indices[ i + 2 ] );
		}
	}

	mesh.m_Normals.assign( nCount, Point_t{} );
	for ( std::size_t i = 0; i < mesh.m_Indices.size(); i += 3 )
	{
		const Point_t a = mesh.m_Points[ mesh.m_Indices[ i ] ];
		const Point_t b = mesh.m_Points[ mesh.m_Indices[ i + 1 ] ];
		const Point_t c = mesh.m_Points[ mesh.m_Indices[ i + 2 ] ];
		// Clockwise from the front, so this cross product points out of the front; its length
		// weighs bigger triangles more.
		const Point_t vecArea = Cross( Subtract( c, a ), Subtract( b, a ) );
		for ( std::size_t k = 0; k < 3; k++ )
		{
			mesh.m_Normals[ mesh.m_Indices[ i + k ] ] = Add( mesh.m_Normals[ mesh.m_Indices[ i + k ] ], vecArea );
		}
	}
	for ( Point_t &vecNormal : mesh.m_Normals )
	{
		vecNormal = Length( vecNormal ) > 0.0 ? Normalized( vecNormal ) : face.m_Plane.m_Normal;
	}
	return mesh;
}

} // namespace xv::map
