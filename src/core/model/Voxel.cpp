//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Bounded surface voxel decomposition for open and overlapping Source render meshes.
//===========================================================================//
#include "Voxel.h"
#include <VHACD.h>
#include <cmath>
#include <memory>
#include <stdexcept>
namespace xv::model
{
//-----------------------------------------------------------------------------
// Purpose: Fits convex pieces to the mesh surface without filling gaps between limbs or wings.
//-----------------------------------------------------------------------------
std::vector<Convex_t> VoxelHulls( std::span<const std::array<double, 3>> points, std::span<const std::array<int, 3>> triangles, const VmfOptions_t &options )
{
	if ( points.size() < 4 || points.size() > 100000 || triangles.size() < 4 || triangles.size() > 100000 ||
		options.m_nVoxelResolution < 100000 || options.m_nVoxelResolution > 8000000 || options.m_nMaxHulls < 1 || options.m_nMaxHulls > 1024 ||
		options.m_nMaxVerticesPerHull < 4 || options.m_nMaxVerticesPerHull > 64 )
		throw std::runtime_error( "Invalid voxel decomposition limits" );
	std::vector<double> coordinates;
	coordinates.reserve( points.size() * 3 );
	for ( const auto &p : points )
		for ( double flValue : p )
		{
			if ( !std::isfinite( flValue ) || std::abs( flValue ) >= 16384 )
				throw std::runtime_error( "Voxel input exceeds model bounds" );
			coordinates.push_back( flValue );
		}
	std::vector<std::uint32_t> indices;
	indices.reserve( triangles.size() * 3 );
	for ( const auto &triangle : triangles )
		for ( int nIndex : triangle )
		{
			if ( nIndex < 0 || nIndex >= static_cast<int>( points.size() ) )
				throw std::runtime_error( "Invalid voxel input index" );
			indices.push_back( static_cast<std::uint32_t>( nIndex ) );
		}
	const auto pfnRelease = []( VHACD::IVHACD *p ) { if ( p ) p->Release(); };
	const std::unique_ptr<VHACD::IVHACD, decltype( pfnRelease )> pDecomposer( VHACD::CreateVHACD(), pfnRelease );
	if ( !pDecomposer )
		throw std::runtime_error( "Cannot create voxel decomposer" );
	VHACD::IVHACD::Parameters parameters;
	parameters.m_asyncACD = false;
	parameters.m_maxConvexHulls = options.m_nMaxHulls;
	parameters.m_resolution = options.m_nVoxelResolution;
	parameters.m_maxNumVerticesPerCH = options.m_nMaxVerticesPerHull;
	parameters.m_minimumVolumePercentErrorAllowed = 0.1;
	parameters.m_maxRecursionDepth = 12;
	parameters.m_minEdgeLength = 1;
	parameters.m_fillMode = VHACD::FillMode::SURFACE_ONLY;
	if ( !pDecomposer->Compute( coordinates.data(), static_cast<std::uint32_t>( points.size() ), indices.data(), static_cast<std::uint32_t>( triangles.size() ), parameters ) )
		throw std::runtime_error( "Voxel decomposition failed" );
	const auto nHulls = pDecomposer->GetNConvexHulls();
	if ( !nHulls || nHulls > static_cast<std::uint32_t>( options.m_nMaxHulls ) )
		throw std::runtime_error( "Voxel decomposition exceeded its hull budget" );
	std::vector<Convex_t> hulls;
	for ( std::uint32_t i = 0; i < nHulls; i++ )
	{
		VHACD::IVHACD::ConvexHull source;
		if ( !pDecomposer->GetConvexHull( i, source ) || source.m_points.size() > 2048 || source.m_triangles.size() > 4096 )
			throw std::runtime_error( "Invalid voxel hull" );
		Convex_t hull;
		for ( const auto &p : source.m_points )
		{
			if ( !std::isfinite( p.mX ) || !std::isfinite( p.mY ) || !std::isfinite( p.mZ ) )
				throw std::runtime_error( "Voxel decomposition produced non-finite coordinates" );
			hull.m_Vertices.push_back( { static_cast<float>( p.mX ), static_cast<float>( p.mY ), static_cast<float>( p.mZ ) } );
		}
		for ( const auto &triangle : source.m_triangles )
			for ( auto nIndex : { triangle.mI0, triangle.mI1, triangle.mI2 } )
			{
				if ( nIndex >= hull.m_Vertices.size() )
					throw std::runtime_error( "Invalid voxel hull triangle" );
				hull.m_Indices.push_back( nIndex );
			}
		hulls.push_back( std::move( hull ) );
	}
	return hulls;
}
} // namespace xv::model
