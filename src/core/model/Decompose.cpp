//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Bounded render-mesh preparation and convex decomposition through pinned convex decomposition backends.
//===========================================================================//
#include "Vmf.h"
#include "Voxel.h"
#include <coacd.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
namespace xv::model
{
//-----------------------------------------------------------------------------
// Purpose: Prepares render triangles and selects a closed-mesh or surface voxel solver.
//-----------------------------------------------------------------------------
std::vector<Convex_t> DecomposeModel( const CModel &model, const VmfOptions_t &options, std::vector<std::string> *pNotes )
{
	if ( !std::isfinite( options.m_flConcavity ) || options.m_flConcavity < 0.01f || options.m_flConcavity > 1 ||
		options.m_nMaxHulls < 1 || options.m_nMaxHulls > 1024 || options.m_nMaxVerticesPerHull < 4 || options.m_nMaxVerticesPerHull > 64 || options.m_nMaxSides < 4 || options.m_nMaxSides > 128 ||
		options.m_nVoxelResolution < 100000 || options.m_nVoxelResolution > 8000000 )
		throw std::runtime_error( "Invalid decomposition options" );
	coacd::Mesh input;
	std::map<std::array<double, 3>, int> welded;
	std::map<std::pair<int, int>, int> edges;
	std::set<std::array<int, 3>> triangles;
	int nDuplicates = 0, nDegenerate = 0, nInputTriangles = 0;
	for ( const auto &mesh : model.m_Meshes )
	{
		if ( mesh.m_Indices.size() % 3 )
			throw std::runtime_error( "Invalid render triangle list" );
		for ( std::size_t i = 0; i < mesh.m_Indices.size(); i += 3 )
		{
			if ( ++nInputTriangles > 100000 )
				throw std::runtime_error( "Render mesh exceeds decomposition triangle limit" );
			std::array<int, 3> indices;
			for ( int j = 0; j < 3; j++ )
			{
				if ( mesh.m_Indices[ i + j ] >= model.m_Vertices.size() )
					throw std::runtime_error( "Invalid render index" );
				const auto p = model.m_Vertices[ mesh.m_Indices[ i + j ] ].m_vecPosition;
				const std::array<double, 3> point{ p.m_flX, p.m_flY, p.m_flZ };
				for ( double flValue : point )
					if ( !std::isfinite( flValue ) || std::abs( flValue ) >= 16384 )
						throw std::runtime_error( "Render mesh exceeds decomposition bounds" );
				const auto [ it, bNew ] = welded.emplace( point, static_cast<int>( input.vertices.size() ) );
				if ( bNew )
					input.vertices.push_back( point );
				indices[ j ] = it->second;
				if ( input.vertices.size() > 100000 )
					throw std::runtime_error( "Render mesh exceeds decomposition vertex limit" );
			}
			if ( indices[ 0 ] == indices[ 1 ] || indices[ 1 ] == indices[ 2 ] || indices[ 2 ] == indices[ 0 ] )
			{
				nDegenerate++;
				continue;
			}
			const auto &a = input.vertices[ indices[ 0 ] ], &b = input.vertices[ indices[ 1 ] ], &c = input.vertices[ indices[ 2 ] ];
			const std::array<double, 3> cross{ ( b[ 1 ] - a[ 1 ] ) * ( c[ 2 ] - a[ 2 ] ) - ( b[ 2 ] - a[ 2 ] ) * ( c[ 1 ] - a[ 1 ] ),
				( b[ 2 ] - a[ 2 ] ) * ( c[ 0 ] - a[ 0 ] ) - ( b[ 0 ] - a[ 0 ] ) * ( c[ 2 ] - a[ 2 ] ),
				( b[ 0 ] - a[ 0 ] ) * ( c[ 1 ] - a[ 1 ] ) - ( b[ 1 ] - a[ 1 ] ) * ( c[ 0 ] - a[ 0 ] ) };
			if ( cross[ 0 ] * cross[ 0 ] + cross[ 1 ] * cross[ 1 ] + cross[ 2 ] * cross[ 2 ] < 1e-12 )
			{
				nDegenerate++;
				continue;
			}
			auto sorted = indices;
			std::sort( sorted.begin(), sorted.end() );
			if ( !triangles.insert( sorted ).second )
			{
				nDuplicates++;
				continue;
			}
			// CoACD uses the conventional counterclockwise triangle orientation.
			std::swap( indices[ 1 ], indices[ 2 ] );
			input.indices.push_back( indices );
			for ( int j = 0; j < 3; j++ )
				edges[ { indices[ j ], indices[ ( j + 1 ) % 3 ] } ]++;
			if ( input.indices.size() > 100000 || input.vertices.size() > 100000 )
				throw std::runtime_error( "Render mesh exceeds decomposition limit (100,000 triangles/vertices)" );
		}
	}
	if ( input.vertices.size() < 4 || input.indices.size() < 4 )
		throw std::runtime_error( "Render mesh has no closed volume" );
	std::vector<int> remap( input.vertices.size(), -1 );
	std::vector<std::array<double, 3>> referenced;
	for ( auto &triangle : input.indices )
		for ( int &nIndex : triangle )
		{
			if ( remap[ nIndex ] < 0 )
			{
				remap[ nIndex ] = static_cast<int>( referenced.size() );
				referenced.push_back( input.vertices[ nIndex ] );
			}
			nIndex = remap[ nIndex ];
		}
	input.vertices = std::move( referenced );
	const auto &first = input.indices.front();
	const auto &a = input.vertices[ first[ 0 ] ], &b = input.vertices[ first[ 1 ] ], &c = input.vertices[ first[ 2 ] ];
	const std::array<double, 3> normal{ ( b[ 1 ] - a[ 1 ] ) * ( c[ 2 ] - a[ 2 ] ) - ( b[ 2 ] - a[ 2 ] ) * ( c[ 1 ] - a[ 1 ] ),
		( b[ 2 ] - a[ 2 ] ) * ( c[ 0 ] - a[ 0 ] ) - ( b[ 0 ] - a[ 0 ] ) * ( c[ 2 ] - a[ 2 ] ),
		( b[ 0 ] - a[ 0 ] ) * ( c[ 1 ] - a[ 1 ] ) - ( b[ 1 ] - a[ 1 ] ) * ( c[ 0 ] - a[ 0 ] ) };
	const double flLength = std::sqrt( normal[ 0 ] * normal[ 0 ] + normal[ 1 ] * normal[ 1 ] + normal[ 2 ] * normal[ 2 ] );
	if ( std::ranges::all_of( input.vertices, [ & ]( const auto &p ) { return std::abs( ( p[ 0 ] - a[ 0 ] ) * normal[ 0 ] + ( p[ 1 ] - a[ 1 ] ) * normal[ 1 ] + ( p[ 2 ] - a[ 2 ] ) * normal[ 2 ] ) < flLength * 1e-5; } ) )
		throw std::runtime_error( "Flat render mesh has no thickness; image cards need silhouette tracing" );
	bool bManifold = true;
	for ( const auto &[ edge, nCount ] : edges )
		if ( nCount != 1 || !edges.contains( { edge.second, edge.first } ) )
			bManifold = false;
	if ( pNotes && ( nDuplicates || nDegenerate ) )
		pNotes->push_back( "Render mesh preparation: removed " + std::to_string( nDuplicates ) + " duplicate and " + std::to_string( nDegenerate ) + " degenerate triangles." );
	auto limits = options;
	limits.m_nMaxVerticesPerHull = std::min( options.m_nMaxVerticesPerHull, ( options.m_nMaxSides + 4 ) / 2 );
	if ( pNotes && limits.m_nMaxVerticesPerHull < options.m_nMaxVerticesPerHull )
		pNotes->push_back( "Hull vertices limited to " + std::to_string( limits.m_nMaxVerticesPerHull ) + " to fit the brush side budget." );
	const auto pfnVoxel = [ & ] {
		if ( pNotes )
			pNotes->push_back( "Open or overlapping render mesh: V-HACD surface voxel decomposition at " + std::to_string( options.m_nVoxelResolution ) + " voxels. The result is an approximate shell; thin surfaces may be thickened or simplified; the CoACD concavity setting does not apply." );
		return VoxelHulls( input.vertices, input.indices, limits );
	};
	if ( !bManifold )
		return pfnVoxel();
	std::vector<coacd::Mesh> parts;
	try
	{
		// CoACD's logger initializes process-wide state; only serialize calls into that backend.
		static std::mutex mutex;
		const std::lock_guard lock( mutex );
		coacd::set_log_level( "off" );
		parts = coacd::CoACD( input, options.m_flConcavity, options.m_nMaxHulls, "off", 50, 2000, 20, 150, 3, false, true, true, limits.m_nMaxVerticesPerHull );
	}
	catch ( const std::runtime_error &e )
	{
		if ( pNotes )
			pNotes->push_back( std::string( "Closed-mesh solver could not process this mesh: " ) + e.what() );
		return pfnVoxel();
	}
	if ( parts.empty() || parts.size() > static_cast<std::size_t>( options.m_nMaxHulls ) )
		throw std::runtime_error( "Decomposition exceeded its hull budget" );
	if ( pNotes )
		pNotes->push_back( "Visible render mesh decomposed with CoACD; physics collision hulls were not used." );
	std::vector<Convex_t> convexes;
	for ( const auto &part : parts )
	{
		if ( part.vertices.size() > 2048 || part.indices.size() > 4096 )
			throw std::runtime_error( "Decomposition produced an oversized hull" );
		Convex_t convex;
		for ( const auto &p : part.vertices )
		{
			for ( double flValue : p )
				if ( !std::isfinite( flValue ) || std::abs( flValue ) >= 16384 )
					throw std::runtime_error( "Decomposition produced invalid coordinates" );
			convex.m_Vertices.push_back( { static_cast<float>( p[ 0 ] ), static_cast<float>( p[ 1 ] ), static_cast<float>( p[ 2 ] ) } );
		}
		for ( const auto &triangle : part.indices )
			for ( int nIndex : triangle )
			{
				if ( nIndex < 0 || nIndex >= static_cast<int>( part.vertices.size() ) )
					throw std::runtime_error( "Decomposition produced an invalid hull index" );
				convex.m_Indices.push_back( static_cast<std::uint32_t>( nIndex ) );
			}
		convexes.push_back( std::move( convex ) );
	}
	return convexes;
}
} // namespace xv::model
