//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See BrushOperations.h.
//===========================================================================//
#include "BrushOperations.h"
#include "map/Clip.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
namespace xv::map
{
namespace
{
//-----------------------------------------------------------------------------
// Purpose: Collect each selected corner once, without mixing different solids' topology.
//-----------------------------------------------------------------------------
std::map<std::uint32_t, std::vector<std::uint32_t>> SelectedCorners( const CScene &scene, std::span<const BrushPart_t> parts )
{
	std::map<std::uint32_t, std::vector<std::uint32_t>> result;
	// Each solid's mesh is built once: a selection can hold a thousand faces of one brush.
	std::map<std::uint32_t, BrushMeshParts_t> meshes;
	for ( const auto &part : parts )
		if ( part.m_nObject < scene.GetObjectCount() && scene.IsSolidObject( part.m_nObject ) && !scene.IsDeleted( part.m_nObject ) )
		{
			const auto nSolid = scene.GetIndex( part.m_nObject );
			auto it = meshes.find( nSolid );
			if ( it == meshes.end() )
				it = meshes.emplace( nSolid, BrushMeshParts_t::FromGeometry( scene.m_Solids[ nSolid ] ) ).first;
			const auto corners = CornersOf( it->second, part.m_nKind, part.m_nIndex );
			auto &selected = result[ nSolid ];
			selected.insert( selected.end(), corners.begin(), corners.end() );
		}
	for ( auto &[ nSolid, selected ] : result )
	{
		std::ranges::sort( selected );
		selected.erase( std::ranges::unique( selected ).begin(), selected.end() );
	}
	return result;
}
//-----------------------------------------------------------------------------
// Purpose: Measure a convex solid by tetrahedra about its interior, independent of winding.
//-----------------------------------------------------------------------------
double Volume( const SolidGeometry_t &shape )
{
	double flVolume = 0;
	const auto centre = shape.m_Bounds.GetCenter();
	for ( const auto &face : shape.m_Faces )
		for ( std::size_t i = 1; i + 1 < face.m_Points.size(); ++i )
			flVolume += std::abs( Dot( Subtract( face.m_Points[ 0 ], centre ), Cross( Subtract( face.m_Points[ i ], centre ), Subtract( face.m_Points[ i + 1 ], centre ) ) ) ) / 6;
	return flVolume;
}
} // namespace
//-----------------------------------------------------------------------------
// Purpose: Ask about a weld only when previously distinct corners would meet after a move.
//-----------------------------------------------------------------------------
bool PartsWouldMerge( const CScene &scene, std::span<const BrushPart_t> parts, const Transform_t &transform )
{
	for ( const auto &[ nSolid, selected ] : SelectedCorners( scene, parts ) )
	{
		const auto mesh = CBrushMesh::FromGeometry( scene.m_Solids[ nSolid ] );
		for ( const auto i : selected )
			for ( std::uint32_t j = 0; j < mesh.m_Vertices.size(); ++j )
				if ( i != j && Length( Subtract( mesh.m_Vertices[ i ], mesh.m_Vertices[ j ] ) ) > 0.001 )
				{
					const auto target = std::ranges::binary_search( selected, j ) ? transform.Apply( mesh.m_Vertices[ j ] ) : mesh.m_Vertices[ j ];
					if ( Length( Subtract( transform.Apply( mesh.m_Vertices[ i ] ), target ) ) <= 0.001 )
						return true;
				}
	}
	return false;
}
//-----------------------------------------------------------------------------
// Purpose: Weld selected corners at their centre or in connected distance clusters, atomically.
//-----------------------------------------------------------------------------
MapEdit_t MergeBrushVertices( const CScene &scene, std::span<const BrushPart_t> parts, bool bAtCenter, double flDistance, BrushConcave_t nConcave, std::string *pProblem )
{
	if ( pProblem )
		pProblem->clear();
	if ( !std::isfinite( flDistance ) || flDistance < 0 )
		return {};
	MapEdit_t edit;
	int nId = scene.m_nNextObjectId, nSide = scene.m_nNextSideId;
	auto nAdded = static_cast<std::uint32_t>( scene.m_Map.m_Solids.size() );
	for ( const auto &[ nSolid, selected ] : SelectedCorners( scene, parts ) )
	{
		if ( selected.size() < 2 )
			continue;
		const auto &source = scene.m_Map.m_Solids[ nSolid ];
		auto mesh = CBrushMesh::FromGeometry( scene.m_Solids[ nSolid ] );
		std::vector<std::size_t> roots( selected.size() );
		std::iota( roots.begin(), roots.end(), 0 );
		auto root = [ & ]( std::size_t i ) {while(roots[i]!=i)i=roots[i];return i; };
		for ( std::size_t i = 0; i < selected.size(); ++i )
			for ( std::size_t j = 0; j < i; ++j )
				if ( bAtCenter || Length( Subtract( mesh.m_Vertices[ selected[ i ] ], mesh.m_Vertices[ selected[ j ] ] ) ) <= flDistance )
					roots[ root( i ) ] = root( j );
		std::map<std::size_t, std::vector<std::uint32_t>> clusters;
		for ( std::size_t i = 0; i < selected.size(); ++i )
			clusters[ root( i ) ].push_back( selected[ i ] );
		bool bChanged = false;
		for ( const auto &[ nRoot, corners ] : clusters )
		{
			if ( corners.size() < 2 )
				continue;
			for ( const auto &face : mesh.m_Faces )
				if ( face.m_nSide >= 0 && source.m_Sides[ face.m_nSide ].m_pDisp && std::ranges::any_of( face.m_Corners, [ & ]( auto n ) { return std::ranges::find( corners, n ) != corners.end(); } ) )
				{
					if ( pProblem )
						*pProblem = "The selected vertices touch a displacement.";
					return {};
				}
			Point_t centre{};
			for ( auto n : corners )
				centre = Add( centre, mesh.m_Vertices[ n ] );
			centre = Scale( centre, 1.0 / corners.size() );
			for ( auto n : corners )
				mesh.m_Vertices[ n ] = centre;
			bChanged = true;
		}
		if ( !bChanged )
			continue;
		auto result = ToSolids( scene.m_Map, source, mesh, nConcave );
		if ( result.m_Solids.empty() )
		{
			if ( pProblem )
				*pProblem = result.m_szProblem;
			return {};
		}
		if ( result.m_Solids.size() > 1 )
		{
			Group_t group;
			group.m_nId = nId++;
			group.m_Editor = source.m_Editor;
			edit.m_Groups.push_back( group );
			for ( auto &piece : result.m_Solids )
			{
				piece.m_Editor.m_nGroup = group.m_nId;
				piece.m_Editor.m_bChanged = true;
			}
		}
		for ( std::size_t i = 0; i < result.m_Solids.size(); ++i )
		{
			auto piece = std::move( result.m_Solids[ i ] );
			if ( i )
				piece = AsNew( std::move( piece ), nId, nSide );
			else
				for ( auto &side : piece.m_Sides )
					if ( !side.m_nId )
						side.m_nId = nSide++;
			piece.m_nEntity = source.m_nEntity;
			edit.m_Solids.emplace_back( i ? nAdded++ : nSolid, std::move( piece ) );
		}
	}
	if ( edit.IsEmpty() && pProblem )
		*pProblem = "Select at least two vertices close enough to merge.";
	return edit;
}
//-----------------------------------------------------------------------------
// Purpose: Accept a brush union only when subtracting it leaves no part of its convex hull.
//-----------------------------------------------------------------------------
MapEdit_t MergeBrushes( const CScene &scene, const std::vector<std::uint32_t> &objects, std::string *pProblem )
{
	if ( pProblem )
		pProblem->clear();
	auto refuse = [ & ]( const char *szReason ) {if(pProblem)*pProblem=szReason;return MapEdit_t{}; };
	const auto solids = ObjectsOf( scene, objects ).m_Solids;
	if ( solids.size() < 2 )
		return refuse( "Select at least two brushes to merge." );
	const auto &first = scene.m_Map.m_Solids[ solids.front() ];
	std::vector<Point_t> points;
	for ( auto n : solids )
	{
		const auto &source = scene.m_Map.m_Solids[ n ];
		if ( source.m_nEntity != first.m_nEntity )
			return refuse( "Brushes belonging to different entities cannot be merged." );
		if ( !scene.m_Solids[ n ].IsValid() )
			return refuse( "Repair invalid brushes before merging them." );
		if ( std::ranges::any_of( source.m_Sides, []( const auto &side ) { return bool( side.m_pDisp ); } ) )
			return refuse( "Brushes with displacements cannot be merged." );
		const auto mesh = CBrushMesh::FromGeometry( scene.m_Solids[ n ] );
		points.insert( points.end(), mesh.m_Vertices.begin(), mesh.m_Vertices.end() );
	}
	std::vector<Plane_t> planes;
	for ( const auto n : solids )
		for ( const auto &face : scene.m_Solids[ n ].m_Faces )
			if ( std::ranges::all_of( points, [ & ]( const Point_t &point ) { return face.m_Plane.Distance( point ) <= 0.001; } ) &&
				std::ranges::none_of( planes, [ & ]( const Plane_t &plane ) { return Dot( plane.m_Normal, face.m_Plane.m_Normal ) > 1 - 1e-8 && std::abs( plane.m_flDistance - face.m_Plane.m_flDistance ) < 0.001; } ) )
				planes.push_back( face.m_Plane );
	if ( planes.size() < 4 )
		return refuse( "The brushes' union is not convex." );
	Solid_t hull = first;
	hull.m_Sides.clear();
	hull.m_bChanged = true;
	for ( const auto &plane : planes )
	{
		Side_t side = SideOnPlane( scene.m_Map, plane, first.m_Sides.front() );
		for ( auto n : solids )
			for ( const auto &face : scene.m_Solids[ n ].m_Faces )
				if ( Dot( face.m_Plane.m_Normal, plane.m_Normal ) > 1 - 1e-8 && std::abs( face.m_Plane.m_flDistance - plane.m_flDistance ) < 0.01 )
				{
					side = scene.m_Map.m_Solids[ n ].m_Sides[ face.m_nSide ];
					side.m_PlusVertices.clear();
				}
		hull.m_Sides.push_back( std::move( side ) );
	}
	const auto shape = BuildSolid( hull );
	if ( !shape.IsValid() )
		return refuse( "The merged brush would be invalid." );
	std::vector<Solid_t> remaining{ hull };
	for ( auto n : solids )
	{
		std::vector<Solid_t> outside;
		for ( const auto &piece : remaining )
		{
			std::optional<Solid_t> inside = piece;
			for ( const auto &face : scene.m_Solids[ n ].m_Faces )
			{
				if ( !inside )
					break;
				auto cut = ClipSolid( scene.m_Map, *inside, face.m_Plane );
				if ( cut.m_Front )
					outside.push_back( std::move( *cut.m_Front ) );
				inside = std::move( cut.m_Back );
			}
		}
		remaining = std::move( outside );
	}
	double flMissing = 0;
	for ( const auto &piece : remaining )
		flMissing += Volume( BuildSolid( piece ) );
	if ( flMissing > std::max( 0.001, Volume( shape ) * 1e-8 ) )
		return refuse( "The brushes' union is not convex; merging would fill a gap." );
	int nSide = scene.m_nNextSideId;
	for ( auto &side : hull.m_Sides )
		if ( !side.m_nId )
			side.m_nId = nSide++;
	MapEdit_t edit;
	edit.m_Solids.emplace_back( solids.front(), std::move( hull ) );
	for ( std::size_t i = 1; i < solids.size(); ++i )
	{
		auto removed = scene.m_Map.m_Solids[ solids[ i ] ];
		removed.m_bDeleted = true;
		removed.m_bChanged = true;
		edit.m_Solids.emplace_back( solids[ i ], std::move( removed ) );
	}
	return edit;
}
//-----------------------------------------------------------------------------
// Purpose: Extrude an ordered convex outline in an orthographic plane into a valid brush.
//-----------------------------------------------------------------------------
std::optional<Solid_t> MakePolygonPrism( const CMap &map, std::span<const Point_t> points, int nAxis, double flDepth, const FaceDefaults_t &faces, std::string *pProblem )
{
	if ( pProblem )
		pProblem->clear();
	auto refuse = [ & ]( const char *szReason ) -> std::optional<Solid_t> {if(pProblem)*pProblem=szReason;return std::nullopt; };
	if ( nAxis < 0 || nAxis > 2 || !std::isfinite( flDepth ) || flDepth <= 0 || points.size() < 3 )
		return refuse( "Choose at least three corners and a positive depth." );
	Point_t axis{};
	axis[ nAxis ] = 1;
	double flSign = 0;
	for ( std::size_t i = 0; i < points.size(); ++i )
	{
		for ( double flValue : points[ i ] )
			if ( !std::isfinite( flValue ) )
				return refuse( "Polygon coordinates must be finite." );
		if ( std::abs( points[ i ][ nAxis ] - points[ 0 ][ nAxis ] ) > 0.001 )
			return refuse( "The polygon's points must lie in one view plane." );
		const auto edge = Subtract( points[ ( i + 1 ) % points.size() ], points[ i ] );
		if ( Length( edge ) < 0.001 )
			return refuse( "The polygon has a repeated corner." );
		for ( const auto &point : points )
		{
			const double flCross = Dot( Cross( edge, Subtract( point, points[ i ] ) ), axis );
			if ( std::abs( flCross ) <= 1e-6 )
				continue;
			if ( flSign && flSign * flCross < 0 )
				return refuse( "The polygon must be convex, with its corners in order." );
			flSign = flCross > 0 ? 1 : -1;
		}
	}
	if ( !flSign )
		return refuse( "The polygon's corners lie on a line." );
	std::vector<std::vector<Point_t>> polygons;
	polygons.emplace_back( points.begin(), points.end() );
	std::vector<Point_t> top;
	for ( auto point : points )
	{
		point[ nAxis ] += flDepth;
		top.push_back( point );
	}
	polygons.push_back( top );
	for ( std::size_t i = 0; i < points.size(); ++i )
	{
		const auto j = ( i + 1 ) % points.size();
		polygons.push_back( { points[ i ], points[ j ], top[ j ], top[ i ] } );
	}
	auto solid = SolidFromFaces( map, polygons, faces );
	if ( !BuildSolid( solid ).IsValid() )
		return refuse( "The polygon would make an invalid brush." );
	return solid;
}
} // namespace xv::map
