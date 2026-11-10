//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See BrushSelect.h.
//
//===========================================================================//
#include "map/BrushSelect.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <set>

#include "map/BrushModeling.h"

namespace xv::map
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose: Whether a face goes from one corner straight to the other (either way round).
//-----------------------------------------------------------------------------
bool FaceHasEdge( const BrushFace_t &face, std::uint32_t a, std::uint32_t b )
{
	const std::size_t nCount = face.m_Corners.size();
	for ( std::size_t i = 0; i < nCount; i++ )
	{
		const std::uint32_t u = face.m_Corners[ i ], v = face.m_Corners[ ( i + 1 ) % nCount ];
		if ( ( u == a && v == b ) || ( u == b && v == a ) )
			return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: The faces with the edge between corners a and b.
//-----------------------------------------------------------------------------
std::vector<std::uint32_t> FacesBeside( const CBrushMesh &mesh, std::uint32_t a, std::uint32_t b )
{
	std::vector<std::uint32_t> faces;
	for ( std::uint32_t i = 0; i < mesh.m_Faces.size(); i++ )
		if ( FaceHasEdge( mesh.m_Faces[ i ], a, b ) )
			faces.push_back( i );
	return faces;
}

//-----------------------------------------------------------------------------
// Purpose: How many parts of a kind a mesh has.
//-----------------------------------------------------------------------------
std::uint32_t PartCount( const BrushMeshParts_t &mesh, BrushPartKind_t nKind )
{
	switch ( nKind )
	{
		case BrushPartKind_t::BRUSH_PART_VERTEX:
			return static_cast<std::uint32_t>( mesh.m_Mesh.m_Vertices.size() );
		case BrushPartKind_t::BRUSH_PART_EDGE:
			return static_cast<std::uint32_t>( mesh.m_Edges.size() );
		case BrushPartKind_t::BRUSH_PART_FACE:
			return static_cast<std::uint32_t>( mesh.m_Mesh.m_Faces.size() );
	}
	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: Whether two parts of the same kind on one mesh are beside each other: corners joined
//          by an edge, edges sharing a corner, faces sharing an edge.
//-----------------------------------------------------------------------------
bool Adjacent( const CBrushMesh &mesh, const std::vector<std::pair<std::uint32_t, std::uint32_t>> &edges, BrushPartKind_t nKind, std::uint32_t a, std::uint32_t b )
{
	if ( a == b )
		return false;
	switch ( nKind )
	{
		case BrushPartKind_t::BRUSH_PART_VERTEX:
			return std::ranges::binary_search( edges, std::pair{ std::min( a, b ), std::max( a, b ) } );
		case BrushPartKind_t::BRUSH_PART_EDGE:
		{
			const auto &e = edges[ a ], &f = edges[ b ];
			return e.first == f.first || e.first == f.second || e.second == f.first || e.second == f.second;
		}
		case BrushPartKind_t::BRUSH_PART_FACE:
		{
			const auto &corners = mesh.m_Faces[ a ].m_Corners;
			for ( std::size_t i = 0; i < corners.size(); i++ )
				if ( FaceHasEdge( mesh.m_Faces[ b ], corners[ i ], corners[ ( i + 1 ) % corners.size() ] ) )
					return true;
			return false;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: An edge of other lying along the edge (a, b) with some length in common, if one does.
//-----------------------------------------------------------------------------
std::optional<std::uint32_t> AlongEdge( Point_t a, Point_t b, const BrushMeshParts_t &parts )
{
	const CBrushMesh &other = parts.m_Mesh;
	const Point_t along = Subtract( b, a );
	const double flLength = Length( along );
	if ( flLength < 1e-6 )
		return std::nullopt;
	const Point_t unit = Scale( along, 1.0 / flLength );
	const auto &edges = parts.m_Edges;
	for ( std::uint32_t i = 0; i < edges.size(); i++ )
	{
		const Point_t p = other.m_Vertices[ edges[ i ].first ], q = other.m_Vertices[ edges[ i ].second ];
		// On the same line: both ends within a hundredth of a unit of it.
		const auto offLine = [ & ]( Point_t point ) { const Point_t d = Subtract( point, a ); return Length( Subtract( d, Scale( unit, Dot( d, unit ) ) ) ); };
		if ( offLine( p ) > 0.01 || offLine( q ) > 0.01 )
			continue;
		const double flP = Dot( Subtract( p, a ), unit ), flQ = Dot( Subtract( q, a ), unit );
		if ( std::min( std::max( flP, flQ ), flLength ) - std::max( std::min( flP, flQ ), 0.0 ) > 0.01 )
			return i;
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: A face's area (half its corners' cross products).
//-----------------------------------------------------------------------------
double FaceArea( const CBrushMesh &mesh, std::uint32_t nFace )
{
	const auto &corners = mesh.m_Faces[ nFace ].m_Corners;
	Point_t sum{};
	for ( std::size_t i = 0; i < corners.size(); i++ )
		sum = Add( sum, Cross( mesh.m_Vertices[ corners[ i ] ], mesh.m_Vertices[ corners[ ( i + 1 ) % corners.size() ] ] ) );
	return Length( sum ) / 2;
}

//-----------------------------------------------------------------------------
// Purpose: The material of a mesh face's side, lower case; empty for a face with no side.
//-----------------------------------------------------------------------------
std::string FaceMaterial( const CScene &scene, std::uint32_t nObject, const CBrushMesh &mesh, std::uint32_t nFace )
{
	const int nSide = mesh.m_Faces[ nFace ].m_nSide;
	const Solid_t &solid = scene.m_Map.m_Solids[ scene.GetIndex( nObject ) ];
	if ( nSide < 0 || static_cast<std::size_t>( nSide ) >= solid.m_Sides.size() )
		return {};
	std::string szMaterial( solid.m_Sides[ nSide ].m_szMaterial );
	std::ranges::transform( szMaterial, szMaterial.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	return szMaterial;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The solid object's mesh, or none for anything else.
//-----------------------------------------------------------------------------
const BrushMeshParts_t *CMeshCache::Get( std::uint32_t nObject )
{
	auto [ it, bNew ] = m_Meshes.try_emplace( nObject );
	if ( bNew && nObject < m_Scene.GetObjectCount() && m_Scene.IsSolidObject( nObject ) && !m_Scene.IsDeleted( nObject ) )
		it->second = BrushMeshParts_t::FromGeometry( m_Scene.m_Solids[ m_Scene.GetIndex( nObject ) ] );
	return it->second ? &*it->second : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::uint32_t> EdgeIndex( const CBrushMesh &mesh, std::uint32_t nFirst, std::uint32_t nSecond )
{
	const auto edges = mesh.GetEdges();
	const auto it = std::ranges::lower_bound( edges, std::pair{ std::min( nFirst, nSecond ), std::max( nFirst, nSecond ) } );
	if ( it == edges.end() || *it != std::pair{ std::min( nFirst, nSecond ), std::max( nFirst, nSecond ) } )
		return std::nullopt;
	return static_cast<std::uint32_t>( it - edges.begin() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::uint32_t> FacesOfEdge( const CBrushMesh &mesh, std::uint32_t nEdge )
{
	const auto edges = mesh.GetEdges();
	return nEdge < edges.size() ? FacesBeside( mesh, edges[ nEdge ].first, edges[ nEdge ].second ) : std::vector<std::uint32_t>{};
}

//-----------------------------------------------------------------------------
// Purpose: Walk on through four-way corners; fall back on a face's outline.
//-----------------------------------------------------------------------------
std::vector<std::uint32_t> EdgeLoop( const CBrushMesh &mesh, std::uint32_t nEdge, std::optional<std::uint32_t> nFace )
{
	const auto edges = mesh.GetEdges();
	if ( nEdge >= edges.size() )
		return {};
	std::vector<std::vector<std::uint32_t>> atCorner( mesh.m_Vertices.size() );
	for ( std::uint32_t i = 0; i < edges.size(); i++ )
	{
		atCorner[ edges[ i ].first ].push_back( i );
		atCorner[ edges[ i ].second ].push_back( i );
	}
	const auto shareFace = [ & ]( std::uint32_t e, std::uint32_t f ) {
		return std::ranges::any_of( mesh.m_Faces, [ & ]( const BrushFace_t &face ) { return FaceHasEdge( face, edges[ e ].first, edges[ e ].second ) && FaceHasEdge( face, edges[ f ].first, edges[ f ].second ); } );
	};
	std::vector<std::uint32_t> loop{ nEdge };
	std::set<std::uint32_t> seen{ nEdge };
	bool bClosed = false;
	// From the edge's second corner on, then (if that didn't come round) from its first.
	for ( const std::uint32_t nStart : { edges[ nEdge ].second, edges[ nEdge ].first } )
	{
		std::uint32_t nCorner = nStart, nFrom = nEdge;
		while ( !bClosed && atCorner[ nCorner ].size() == 4 )
		{
			std::optional<std::uint32_t> next;
			for ( const std::uint32_t e : atCorner[ nCorner ] )
				if ( e != nFrom && !shareFace( e, nFrom ) )
					next = e;
			if ( !next )
				break;
			if ( *next == nEdge )
			{
				bClosed = true;
				break;
			}
			if ( !seen.insert( *next ).second )
				break;
			loop.push_back( *next );
			nFrom = *next;
			nCorner = edges[ *next ].first == nCorner ? edges[ *next ].second : edges[ *next ].first;
		}
		if ( bClosed )
			break;
	}
	if ( loop.size() > 1 )
		return loop;
	// Round a face beside it.
	const auto beside = FacesOfEdge( mesh, nEdge );
	if ( beside.empty() )
		return loop;
	const std::uint32_t nRound = nFace && std::ranges::find( beside, *nFace ) != beside.end() ? *nFace : beside.front();
	const auto &corners = mesh.m_Faces[ nRound ].m_Corners;
	loop.clear();
	for ( std::size_t i = 0; i < corners.size(); i++ )
		if ( const auto e = EdgeIndex( mesh, corners[ i ], corners[ ( i + 1 ) % corners.size() ] ) )
			loop.push_back( *e );
	return loop;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::uint32_t> EdgeRing( const CBrushMesh &mesh, std::uint32_t nEdge )
{
	std::vector<std::uint32_t> ring;
	for ( const BrushRingEdge_t &entry : FindBrushEdgeRing( mesh, nEdge ) )
		ring.push_back( entry.m_nEdge );
	if ( ring.empty() && nEdge < mesh.GetEdges().size() )
		ring.push_back( nEdge );
	return ring;
}

//-----------------------------------------------------------------------------
// Purpose: The faces holding two of the ring's edges; without a ring, the edge's own two.
//-----------------------------------------------------------------------------
std::vector<std::uint32_t> FaceLoop( const CBrushMesh &mesh, std::uint32_t nEdge )
{
	const auto ring = EdgeRing( mesh, nEdge );
	if ( ring.size() < 2 )
		return FacesOfEdge( mesh, nEdge );
	const auto edges = mesh.GetEdges();
	std::vector<std::uint32_t> faces;
	for ( std::uint32_t i = 0; i < mesh.m_Faces.size(); i++ )
	{
		const auto nHeld = std::ranges::count_if( ring, [ & ]( std::uint32_t e ) { return FaceHasEdge( mesh.m_Faces[ i ], edges[ e ].first, edges[ e ].second ); } );
		if ( nHeld >= 2 )
			faces.push_back( i );
	}
	return faces;
}

//-----------------------------------------------------------------------------
// Purpose: The loop on its brush, then on each flush brush whose edge lies along one of it.
//-----------------------------------------------------------------------------
std::vector<BrushPart_t> SelectLoop( const CScene &scene, std::span<const std::uint32_t> objects, std::uint32_t nObject, std::uint32_t nEdge, std::optional<std::uint32_t> nFace, BrushLoop_t nLoop, BrushPartKind_t nKind )
{
	struct Pending_t
	{
		std::uint32_t m_nObject;
		std::uint32_t m_nEdge;
		std::optional<Point_t> m_FaceNormal; // the box face the loop goes round, to find its like
		std::optional<std::uint32_t> m_nFace;
	};
	std::vector<BrushPart_t> parts;
	std::set<std::uint32_t> done{ nObject };
	std::vector<Pending_t> pending{ { nObject, nEdge, std::nullopt, nFace } };
	CMeshCache meshes( scene );
	for ( std::size_t p = 0; p < pending.size(); p++ )
	{
		const Pending_t item = pending[ p ];
		const BrushMeshParts_t *pParts = meshes.Get( item.m_nObject );
		if ( !pParts )
			continue;
		const CBrushMesh *mesh = &pParts->m_Mesh;
		const auto &edges = pParts->m_Edges;
		if ( item.m_nEdge >= edges.size() )
			continue;
		std::optional<std::uint32_t> nRound = item.m_nFace;
		if ( !nRound && item.m_FaceNormal )
		{
			double flBest = -2;
			for ( const std::uint32_t f : FacesOfEdge( *mesh, item.m_nEdge ) )
				if ( const auto plane = mesh->FacePlane( f ); plane && Dot( plane->m_Normal, *item.m_FaceNormal ) > flBest )
				{
					flBest = Dot( plane->m_Normal, *item.m_FaceNormal );
					nRound = f;
				}
		}
		const std::vector<std::uint32_t> along = nLoop == BrushLoop_t::BRUSH_LOOP_LOOP && nKind != BrushPartKind_t::BRUSH_PART_FACE ? EdgeLoop( *mesh, item.m_nEdge, nRound ) : EdgeRing( *mesh, item.m_nEdge );
		if ( nKind == BrushPartKind_t::BRUSH_PART_FACE )
			for ( const std::uint32_t f : FaceLoop( *mesh, item.m_nEdge ) )
				parts.push_back( { item.m_nObject, nKind, f } );
		else
			for ( const std::uint32_t e : along )
			{
				if ( nKind == BrushPartKind_t::BRUSH_PART_EDGE )
					parts.push_back( { item.m_nObject, nKind, e } );
				else
				{
					parts.push_back( { item.m_nObject, nKind, edges[ e ].first } );
					parts.push_back( { item.m_nObject, nKind, edges[ e ].second } );
				}
			}
		std::optional<Point_t> faceNormal = item.m_FaceNormal;
		if ( nRound )
			if ( const auto plane = mesh->FacePlane( *nRound ) )
				faceNormal = plane->m_Normal;
		// On into flush brushes.
		for ( const std::uint32_t nOther : objects )
		{
			if ( done.contains( nOther ) )
				continue;
			const BrushMeshParts_t *pOther = meshes.Get( nOther );
			if ( !pOther )
				continue;
			for ( const std::uint32_t e : along )
				if ( const auto match = AlongEdge( mesh->m_Vertices[ edges[ e ].first ], mesh->m_Vertices[ edges[ e ].second ], *pOther ) )
				{
					done.insert( nOther );
					pending.push_back( { nOther, *match, faceNormal, std::nullopt } );
					break;
				}
		}
	}
	std::ranges::sort( parts );
	parts.erase( std::ranges::unique( parts ).begin(), parts.end() );
	return parts;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<BrushPart_t> GrowParts( const CScene &scene, std::span<const BrushPart_t> parts )
{
	std::vector<BrushPart_t> result( parts.begin(), parts.end() );
	std::map<std::pair<std::uint32_t, BrushPartKind_t>, std::vector<std::uint32_t>> byMesh;
	for ( const BrushPart_t &part : parts )
		byMesh[ { part.m_nObject, part.m_nKind } ].push_back( part.m_nIndex );
	CMeshCache meshes( scene );
	for ( const auto &[ key, indices ] : byMesh )
	{
		const BrushMeshParts_t *pParts = meshes.Get( key.first );
		if ( !pParts )
			continue;
		const CBrushMesh *mesh = &pParts->m_Mesh;
		const auto &edges = pParts->m_Edges;
		const std::uint32_t nCount = PartCount( *pParts, key.second );
		for ( std::uint32_t i = 0; i < nCount; i++ )
			if ( std::ranges::any_of( indices, [ & ]( std::uint32_t n ) { return n < nCount && Adjacent( *mesh, edges, key.second, n, i ); } ) )
				result.push_back( { key.first, key.second, i } );
	}
	std::ranges::sort( result );
	result.erase( std::ranges::unique( result ).begin(), result.end() );
	return result;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<BrushPart_t> ShrinkParts( const CScene &scene, std::span<const BrushPart_t> parts )
{
	std::vector<BrushPart_t> result;
	std::map<std::pair<std::uint32_t, BrushPartKind_t>, std::vector<std::uint32_t>> byMesh;
	for ( const BrushPart_t &part : parts )
		byMesh[ { part.m_nObject, part.m_nKind } ].push_back( part.m_nIndex );
	CMeshCache meshes( scene );
	for ( auto &[ key, indices ] : byMesh )
	{
		const BrushMeshParts_t *pParts = meshes.Get( key.first );
		if ( !pParts )
			continue;
		const CBrushMesh *mesh = &pParts->m_Mesh;
		const auto &edges = pParts->m_Edges;
		const std::uint32_t nCount = PartCount( *pParts, key.second );
		std::ranges::sort( indices );
		for ( const std::uint32_t n : indices )
		{
			bool bEdge = false;
			for ( std::uint32_t i = 0; i < nCount && !bEdge && n < nCount; i++ )
				bEdge = !std::ranges::binary_search( indices, i ) && Adjacent( *mesh, edges, key.second, n, i );
			if ( !bEdge && n < nCount )
				result.push_back( { key.first, key.second, n } );
		}
	}
	std::ranges::sort( result );
	return result;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<BrushPart_t> LinkedParts( const CScene &scene, std::span<const std::uint32_t> objects, BrushPartKind_t nKind )
{
	std::vector<BrushPart_t> result;
	CMeshCache meshes( scene );
	for ( const std::uint32_t nObject : objects )
		if ( const BrushMeshParts_t *pParts = meshes.Get( nObject ) )
			for ( std::uint32_t i = 0, nCount = PartCount( *pParts, nKind ); i < nCount; i++ )
				result.push_back( { nObject, nKind, i } );
	std::ranges::sort( result );
	result.erase( std::ranges::unique( result ).begin(), result.end() );
	return result;
}

//-----------------------------------------------------------------------------
// Purpose: Compare each candidate with the selected parts of its kind by one measure.
//-----------------------------------------------------------------------------
std::vector<BrushPart_t> SimilarParts( const CScene &scene, std::span<const std::uint32_t> objects, std::span<const BrushPart_t> parts, BrushSimilar_t nSimilar )
{
	// What is measured: a face's plane (normal and distance), area and material; an edge's
	// direction (either way) and length; a corner's place has nothing alike but itself.
	struct Measure_t
	{
		BrushPartKind_t m_nKind = BrushPartKind_t::BRUSH_PART_VERTEX;
		Point_t m_Normal{};
		double m_flDistance = 0;
		double m_flSize = 0;
		std::string m_szMaterial;
	};
	const auto measure = [ & ]( std::uint32_t nObject, const BrushMeshParts_t &parts, BrushPartKind_t nKind, std::uint32_t nIndex ) {
		const CBrushMesh &mesh = parts.m_Mesh;
		Measure_t m;
		m.m_nKind = nKind;
		if ( nKind == BrushPartKind_t::BRUSH_PART_FACE )
		{
			if ( const auto plane = mesh.FacePlane( nIndex ) )
			{
				m.m_Normal = plane->m_Normal;
				m.m_flDistance = plane->m_flDistance;
			}
			m.m_flSize = FaceArea( mesh, nIndex );
			m.m_szMaterial = FaceMaterial( scene, nObject, mesh, nIndex );
		}
		else if ( nKind == BrushPartKind_t::BRUSH_PART_EDGE )
		{
			const auto edge = parts.m_Edges[ nIndex ];
			const Point_t along = Subtract( mesh.m_Vertices[ edge.second ], mesh.m_Vertices[ edge.first ] );
			m.m_flSize = Length( along );
			m.m_Normal = m.m_flSize > 0 ? Scale( along, 1 / m.m_flSize ) : along;
		}
		return m;
	};
	const auto alike = [ & ]( const Measure_t &a, const Measure_t &b ) {
		if ( a.m_nKind != b.m_nKind || a.m_nKind == BrushPartKind_t::BRUSH_PART_VERTEX )
			return false;
		const bool bFace = a.m_nKind == BrushPartKind_t::BRUSH_PART_FACE;
		const double flCos = Dot( a.m_Normal, b.m_Normal );
		switch ( nSimilar )
		{
			case BrushSimilar_t::BRUSH_SIMILAR_NORMAL:
				return ( bFace ? flCos : std::abs( flCos ) ) > std::cos( 1.0 * 3.14159265358979 / 180 );
			case BrushSimilar_t::BRUSH_SIMILAR_COPLANAR:
				return bFace && flCos > 1 - 1e-6 && std::abs( a.m_flDistance - b.m_flDistance ) < 0.01;
			case BrushSimilar_t::BRUSH_SIMILAR_MATERIAL:
				return bFace && a.m_szMaterial == b.m_szMaterial;
			case BrushSimilar_t::BRUSH_SIMILAR_SIZE:
				return std::abs( a.m_flSize - b.m_flSize ) <= 0.01 * std::max( a.m_flSize, b.m_flSize ) + 1e-6;
		}
		return false;
	};
	std::vector<Measure_t> selected;
	CMeshCache meshes( scene );
	for ( const BrushPart_t &part : parts )
		if ( const BrushMeshParts_t *pParts = meshes.Get( part.m_nObject ); pParts && part.m_nIndex < PartCount( *pParts, part.m_nKind ) )
			selected.push_back( measure( part.m_nObject, *pParts, part.m_nKind, part.m_nIndex ) );
	std::vector<BrushPart_t> result( parts.begin(), parts.end() );
	for ( const std::uint32_t nObject : objects )
	{
		const BrushMeshParts_t *pParts = meshes.Get( nObject );
		if ( !pParts )
			continue;
		for ( const BrushPartKind_t nKind : { BrushPartKind_t::BRUSH_PART_EDGE, BrushPartKind_t::BRUSH_PART_FACE } )
		{
			if ( std::ranges::none_of( selected, [ & ]( const Measure_t &m ) { return m.m_nKind == nKind; } ) )
				continue;
			for ( std::uint32_t i = 0, nCount = PartCount( *pParts, nKind ); i < nCount; i++ )
			{
				const Measure_t m = measure( nObject, *pParts, nKind, i );
				if ( std::ranges::any_of( selected, [ & ]( const Measure_t &s ) { return alike( s, m ); } ) )
					result.push_back( { nObject, nKind, i } );
			}
		}
	}
	std::ranges::sort( result );
	result.erase( std::ranges::unique( result ).begin(), result.end() );
	return result;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::string> MaterialsOf( const CScene &scene, std::span<const BrushPart_t> parts )
{
	std::vector<std::string> materials;
	CMeshCache meshes( scene );
	for ( const BrushPart_t &part : parts )
		if ( part.m_nKind == BrushPartKind_t::BRUSH_PART_FACE )
			if ( const BrushMeshParts_t *pParts = meshes.Get( part.m_nObject ); pParts && part.m_nIndex < pParts->m_Mesh.m_Faces.size() )
				if ( std::string szMaterial = FaceMaterial( scene, part.m_nObject, pParts->m_Mesh, part.m_nIndex ); !szMaterial.empty() )
					materials.push_back( std::move( szMaterial ) );
	std::ranges::sort( materials );
	materials.erase( std::ranges::unique( materials ).begin(), materials.end() );
	return materials;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<BrushPart_t> FacesWithMaterials( const CScene &scene, std::span<const std::string> materials, std::vector<std::uint32_t> *pObjects )
{
	std::vector<BrushPart_t> result;
	for ( std::size_t nSolid = 0; nSolid < scene.m_Solids.size(); nSolid++ )
	{
		const std::uint32_t nObject = scene.SolidObject( nSolid );
		if ( !scene.IsSolidObject( nObject ) || scene.IsDeleted( nObject ) )
			continue;
		const auto mesh = std::optional<CBrushMesh>( CBrushMesh::FromGeometry( scene.m_Solids[ nSolid ] ) );
		bool bAny = false;
		for ( std::uint32_t f = 0; f < mesh->m_Faces.size(); f++ )
			if ( std::ranges::find( materials, FaceMaterial( scene, nObject, *mesh, f ) ) != materials.end() )
			{
				result.push_back( { nObject, BrushPartKind_t::BRUSH_PART_FACE, f } );
				bAny = true;
			}
		if ( bAny && pObjects )
			pObjects->push_back( nObject );
	}
	return result;
}

//-----------------------------------------------------------------------------
// Purpose: Blender's falloff curves, over how far through the radius a corner is.
//-----------------------------------------------------------------------------
double FalloffWeight( BrushFalloff_t nFalloff, double flDistance, double flRadius )
{
	if ( flRadius <= 0 || flDistance >= flRadius )
		return flDistance <= 0 ? 1.0 : 0.0;
	const double t = 1 - std::max( flDistance, 0.0 ) / flRadius; // 1 at the selection, 0 at the radius
	switch ( nFalloff )
	{
		case BrushFalloff_t::BRUSH_FALLOFF_SMOOTH:
			return t * t * ( 3 - 2 * t );
		case BrushFalloff_t::BRUSH_FALLOFF_SPHERE:
			return std::sqrt( std::max( 0.0, 2 * t - t * t ) );
		case BrushFalloff_t::BRUSH_FALLOFF_ROOT:
			return std::sqrt( t );
		case BrushFalloff_t::BRUSH_FALLOFF_SHARP:
			return t * t;
		case BrushFalloff_t::BRUSH_FALLOFF_LINEAR:
			return t;
		case BrushFalloff_t::BRUSH_FALLOFF_CONSTANT:
			return 1;
	}
	return t;
}

//-----------------------------------------------------------------------------
// Purpose: The selected corners where the transform (or each part's own) puts them; nearby
//          corners part of the way, by their distance from the nearest selected corner.
//-----------------------------------------------------------------------------
std::vector<BrushCornerMove_t> PartMoves( const CScene &scene, std::span<const BrushPart_t> parts, const Transform_t &transform, const BrushMoveOptions_t &options )
{
	std::vector<BrushCornerMove_t> moves;
	CMeshCache meshes( scene );
	// Each selected corner's place, averaged where parts about their own centres share one.
	std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<Point_t, int>> placed;
	std::vector<Point_t> selected;
	const Point_t shift = Subtract( transform.Apply( transform.m_Pivot ), transform.m_Pivot );
	for ( const BrushPart_t &part : parts )
	{
		const BrushMeshParts_t *pParts = meshes.Get( part.m_nObject );
		if ( !pParts )
			continue;
		const CBrushMesh &mesh = pParts->m_Mesh;
		const Point_t centre = PartPosition( *pParts, part.m_nKind, part.m_nIndex );
		for ( const std::uint32_t n : CornersOf( *pParts, part.m_nKind, part.m_nIndex ) )
		{
			const Point_t from = mesh.m_Vertices[ n ];
			// About its own centre: the same turn and stretch, with the move's shift.
			const Point_t to = options.m_bIndividual ? Add( Add( Subtract( transform.Apply( from ), transform.Apply( centre ) ), centre ), shift ) : transform.Apply( from );
			auto &entry = placed[ { part.m_nObject, n } ];
			if ( entry.second == 0 )
				selected.push_back( from );
			entry.first = Add( entry.first, to );
			entry.second++;
		}
	}
	for ( const auto &[ key, entry ] : placed )
		moves.push_back( { key.first, key.second, Scale( entry.first, 1.0 / entry.second ), !options.m_bIndividual } );
	if ( options.m_flRadius <= 0 || selected.empty() )
		return moves;
	for ( const std::uint32_t nObject : options.m_Nearby )
	{
		const BrushMeshParts_t *pParts = meshes.Get( nObject );
		if ( !pParts )
			continue;
		const CBrushMesh *mesh = &pParts->m_Mesh;
		for ( std::uint32_t n = 0; n < mesh->m_Vertices.size(); n++ )
		{
			if ( placed.contains( { nObject, n } ) )
				continue;
			const Point_t from = mesh->m_Vertices[ n ];
			double flNearest = std::numeric_limits<double>::max();
			for ( const Point_t &point : selected )
				flNearest = std::min( flNearest, Length( Subtract( point, from ) ) );
			const double flWeight = FalloffWeight( options.m_nFalloff, flNearest, options.m_flRadius );
			if ( flWeight <= 0 )
				continue;
			const Point_t full = transform.Apply( from );
			moves.push_back( { nObject, n, Add( from, Scale( Subtract( full, from ), flWeight ) ), flWeight >= 1 && !options.m_bIndividual } );
		}
	}
	return moves;
}

//-----------------------------------------------------------------------------
// Purpose: Vertices along the edge most along the drag; edges along the face it goes into.
//-----------------------------------------------------------------------------
std::vector<BrushCornerMove_t> SlideMoves( const CScene &scene, std::span<const BrushPart_t> parts, Point_t drag )
{
	// Never quite onto the next corner, which would make two corners one.
	constexpr double MAX_SLIDE = 0.99;
	std::vector<BrushCornerMove_t> moves;
	std::set<std::pair<std::uint32_t, std::uint32_t>> moved;
	std::map<std::uint32_t, std::set<std::uint32_t>> selectedCorners;
	CMeshCache meshes( scene );
	for ( const BrushPart_t &part : parts )
		if ( const BrushMeshParts_t *pParts = meshes.Get( part.m_nObject ) )
			for ( const std::uint32_t n : CornersOf( *pParts, part.m_nKind, part.m_nIndex ) )
				selectedCorners[ part.m_nObject ].insert( n );
	const auto slide = [ & ]( std::uint32_t nObject, const CBrushMesh &mesh, std::uint32_t nCorner, std::uint32_t nToward, double t ) {
		if ( !moved.insert( { nObject, nCorner } ).second )
			return;
		t = std::clamp( t, 0.0, MAX_SLIDE );
		moves.push_back( { nObject, nCorner, Lerp( mesh.m_Vertices[ nCorner ], mesh.m_Vertices[ nToward ], t ), false } );
	};
	for ( const BrushPart_t &part : parts )
	{
		const BrushMeshParts_t *pParts = meshes.Get( part.m_nObject );
		if ( !pParts )
			continue;
		const CBrushMesh *mesh = &pParts->m_Mesh;
		const auto &edges = pParts->m_Edges;
		const auto &chosen = selectedCorners[ part.m_nObject ];
		if ( part.m_nKind == BrushPartKind_t::BRUSH_PART_VERTEX && part.m_nIndex < mesh->m_Vertices.size() )
		{
			const std::uint32_t v = part.m_nIndex;
			std::optional<std::uint32_t> best;
			double flBest = 0;
			for ( const auto &[ a, b ] : edges )
			{
				if ( a != v && b != v )
					continue;
				const std::uint32_t other = a == v ? b : a;
				if ( chosen.contains( other ) )
					continue;
				const Point_t along = Subtract( mesh->m_Vertices[ other ], mesh->m_Vertices[ v ] );
				const double flLength = Length( along );
				const double flAlong = flLength > 0 ? Dot( along, drag ) / flLength : 0;
				if ( flAlong > flBest )
				{
					flBest = flAlong;
					best = other;
				}
			}
			if ( best )
				slide( part.m_nObject, *mesh, v, *best, flBest / Length( Subtract( mesh->m_Vertices[ *best ], mesh->m_Vertices[ v ] ) ) );
		}
		else if ( part.m_nKind == BrushPartKind_t::BRUSH_PART_EDGE && part.m_nIndex < edges.size() )
		{
			const auto [ a, b ] = edges[ part.m_nIndex ];
			// For each face beside the edge: where its corners go next round that face.
			struct Side_t
			{
				std::uint32_t m_nFromA, m_nFromB;
				Point_t m_Average;
			};
			std::optional<Side_t> best;
			double flBest = 0;
			for ( const std::uint32_t f : FacesBeside( *mesh, a, b ) )
			{
				const auto &corners = mesh->m_Faces[ f ].m_Corners;
				const std::size_t nCount = corners.size();
				const auto nextFrom = [ & ]( std::uint32_t nCorner, std::uint32_t nNot ) {
					const std::size_t i = std::ranges::find( corners, nCorner ) - corners.begin();
					const std::uint32_t before = corners[ ( i + nCount - 1 ) % nCount ], after = corners[ ( i + 1 ) % nCount ];
					return before == nNot ? after : before;
				};
				const std::uint32_t na = nextFrom( a, b ), nb = nextFrom( b, a );
				const Point_t average = Scale( Add( Subtract( mesh->m_Vertices[ na ], mesh->m_Vertices[ a ] ), Subtract( mesh->m_Vertices[ nb ], mesh->m_Vertices[ b ] ) ), 0.5 );
				const double flLength = Length( average );
				const double flAlong = flLength > 0 ? Dot( average, drag ) / flLength : 0;
				if ( flAlong > flBest )
				{
					flBest = flAlong;
					best = Side_t{ na, nb, average };
				}
			}
			if ( best )
			{
				const double t = Dot( drag, best->m_Average ) / Dot( best->m_Average, best->m_Average );
				slide( part.m_nObject, *mesh, a, best->m_nFromA, t );
				slide( part.m_nObject, *mesh, b, best->m_nFromB, t );
			}
		}
	}
	return moves;
}

} // namespace xv::map
