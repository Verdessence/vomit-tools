//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's cordon: what it leaves out, and the sealed map it compiles.
//
//===========================================================================//
#include "Cordon.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>

#include "kv1/Kv1.h"
#include "map/Brushes.h"
#include "map/Clip.h"
#include "map/Write.h"

namespace xv::map
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose: Whether two boxes share any space (touching counts).
//-----------------------------------------------------------------------------
bool Overlap( const Box_t &a, const Box_t &b )
{
	for ( std::size_t i = 0; i < 3; i++ )
	{
		if ( a.m_Maxs[ i ] < b.m_Mins[ i ] || b.m_Maxs[ i ] < a.m_Mins[ i ] )
		{
			return false;
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Whether two boxes share some volume (touching doesn't count).
//-----------------------------------------------------------------------------
bool OverlapInside( const Box_t &a, const Box_t &b )
{
	for ( std::size_t i = 0; i < 3; i++ )
	{
		if ( a.m_Maxs[ i ] <= b.m_Mins[ i ] || b.m_Maxs[ i ] <= a.m_Mins[ i ] )
		{
			return false;
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: What of solid lies outside box, in convex pieces: cut by each of the box's sides in
//          turn, the part outside kept and the part inside going on to the next side.
//-----------------------------------------------------------------------------
std::vector<Solid_t> OutsideBox( const CMap &map, const Solid_t &solid, const Box_t &box )
{
	std::vector<Solid_t> pieces;
	std::optional<Solid_t> inside = solid;
	for ( std::size_t nAxis = 0; nAxis < 3 && inside; nAxis++ )
	{
		for ( const double flSign : { 1.0, -1.0 } )
		{
			if ( !inside )
			{
				break;
			}
			Point_t normal{};
			normal[ nAxis ] = flSign;
			ClipResult_t cut = ClipSolid( map, *inside, { normal, flSign > 0.0 ? box.m_Maxs[ nAxis ] : -box.m_Mins[ nAxis ] } );
			if ( cut.m_Front )
			{
				pieces.push_back( std::move( *cut.m_Front ) );
			}
			inside = std::move( cut.m_Back );
		}
	}
	return pieces;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Hammer++ keeps a list of named cordons; Hammer's one cordon is the first, "cordon".
//-----------------------------------------------------------------------------
MapEdit_t SetCordon( const CScene &scene, const std::optional<Box_t> &box, bool bActive )
{
	MapEdit_t edit;
	std::vector<Cordon_t> cordons;
	if ( box )
	{
		Cordon_t cordon;
		cordon.m_szName = scene.m_Map.Store( "cordon" );
		cordon.m_bActive = true;
		cordon.m_Boxes.push_back( *box );
		cordons.push_back( std::move( cordon ) );
	}
	edit.m_Cordons = std::make_pair( std::move( cordons ), bActive && box.has_value() );
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapEdit_t SetCordons( const CScene &scene, std::vector<Cordon_t> cordons, bool bActive )
{
	MapEdit_t edit;
	for ( Cordon_t &cordon : cordons )
	{
		cordon.m_szName = scene.m_Map.Store( std::string{ cordon.m_szName } );
	}
	edit.m_Cordons = std::make_pair( std::move( cordons ), bActive );
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<Box_t> ActiveCordonBoxes( const CMap &map )
{
	std::vector<Box_t> boxes;
	if ( !map.m_bCordonsActive )
	{
		return boxes;
	}
	for ( const Cordon_t &cordon : map.m_Cordons )
	{
		if ( cordon.m_bActive )
		{
			for ( const Box_t &box : cordon.m_Boxes )
			{
				if ( !box.IsEmpty() )
				{
					boxes.push_back( box );
				}
			}
		}
	}
	return boxes;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Box_t ObjectBounds( const CScene &scene, std::uint32_t nObject )
{
	if ( nObject >= scene.GetObjectCount() )
	{
		return {};
	}
	if ( scene.IsSolidObject( nObject ) )
	{
		return scene.m_Solids[ scene.GetIndex( nObject ) ].m_Bounds;
	}
	const std::size_t nEntity = scene.GetIndex( nObject );
	if ( scene.m_Entities[ nEntity ].m_bPoint )
	{
		return scene.GetEntityBox( nEntity );
	}
	Box_t box;
	for ( const std::uint32_t nSolid : scene.m_Map.m_Entities[ nEntity ].m_Solids )
	{
		if ( nSolid < scene.m_Solids.size() && !scene.m_Map.m_Solids[ nSolid ].m_bDeleted )
		{
			box.Add( scene.m_Solids[ nSolid ].m_Bounds );
		}
	}
	return box;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::uint32_t> ObjectsOutsideCordon( const CScene &scene )
{
	std::vector<std::uint32_t> outside;
	const std::vector<Box_t> boxes = ActiveCordonBoxes( scene.m_Map );
	if ( boxes.empty() )
	{
		return outside;
	}
	auto inside = [ & ]( const Box_t &bounds ) {
		return !bounds.IsEmpty() && std::ranges::any_of( boxes, [ & ]( const Box_t &box ) { return Overlap( bounds, box ); } );
	};
	for ( std::uint32_t n = 0; n < scene.GetObjectCount(); n++ )
	{
		if ( scene.IsDeleted( n ) )
		{
			continue;
		}
		// A brush entity's solids stay or go with the entity.
		if ( scene.IsSolidObject( n ) )
		{
			const int nEntity = scene.m_Map.m_Solids[ scene.GetIndex( n ) ].m_nEntity;
			if ( nEntity >= 0 )
			{
				if ( !inside( ObjectBounds( scene, scene.EntityObject( static_cast<std::size_t>( nEntity ) ) ) ) )
				{
					outside.push_back( n );
				}
				continue;
			}
		}
		if ( !inside( ObjectBounds( scene, n ) ) )
		{
			outside.push_back( n );
		}
	}
	return outside;
}

//-----------------------------------------------------------------------------
// Purpose: The walls sit outside the box, the top and bottom spanning the whole and the sides
//          between them, so nothing inside is covered.
//-----------------------------------------------------------------------------
std::vector<Solid_t> CordonWalls( const CMap &map, const Box_t &box, double flThickness, const FaceDefaults_t &faces )
{
	std::vector<Solid_t> walls;
	const Point_t &n = box.m_Mins;
	const Point_t &x = box.m_Maxs;
	const double t = flThickness;
	const Box_t boxes[ 6 ] = {
		{ { n[ 0 ] - t, n[ 1 ] - t, n[ 2 ] - t }, { x[ 0 ] + t, x[ 1 ] + t, n[ 2 ] } }, // floor
		{ { n[ 0 ] - t, n[ 1 ] - t, x[ 2 ] }, { x[ 0 ] + t, x[ 1 ] + t, x[ 2 ] + t } }, // ceiling
		{ { n[ 0 ] - t, n[ 1 ] - t, n[ 2 ] }, { n[ 0 ], x[ 1 ] + t, x[ 2 ] } },         // -x
		{ { x[ 0 ], n[ 1 ] - t, n[ 2 ] }, { x[ 0 ] + t, x[ 1 ] + t, x[ 2 ] } },         // +x
		{ { n[ 0 ], n[ 1 ] - t, n[ 2 ] }, { x[ 0 ], n[ 1 ], x[ 2 ] } },                 // -y
		{ { n[ 0 ], x[ 1 ], n[ 2 ] }, { x[ 0 ], x[ 1 ] + t, x[ 2 ] } },                 // +y
	};
	for ( const Box_t &wall : boxes )
	{
		walls.push_back( MakeBlock( map, wall, faces ) );
	}
	return walls;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string CordonedMapText( const CScene &scene, std::string_view szMaterial, const std::optional<MapCopy_t> &copy )
{
	const std::vector<Box_t> boxes = ActiveCordonBoxes( scene.m_Map );
	if ( boxes.empty() && !copy )
	{
		return WriteVmf( scene.m_Map );
	}
	std::shared_ptr<CScene> pCordoned = ApplyEdit( scene, DeleteObjects( scene, ObjectsOutsideCordon( scene ) ) );
	FaceDefaults_t faces;
	faces.m_szMaterial = szMaterial;
	std::vector<Solid_t> walls;
	for ( std::size_t i = 0; i < boxes.size(); i++ )
	{
		for ( Solid_t &wall : CordonWalls( pCordoned->m_Map, boxes[ i ], 16.0, faces ) )
		{
			std::vector<Solid_t> pieces{ std::move( wall ) };
			for ( std::size_t j = 0; j < boxes.size(); j++ )
			{
				if ( j == i )
				{
					continue;
				}
				std::vector<Solid_t> outside;
				for ( const Solid_t &piece : pieces )
				{
					if ( !OverlapInside( BuildSolid( piece ).m_Bounds, boxes[ j ] ) )
					{
						outside.push_back( piece );
						continue;
					}
					for ( Solid_t &kept : OutsideBox( pCordoned->m_Map, piece, boxes[ j ] ) )
					{
						outside.push_back( std::move( kept ) );
					}
				}
				pieces = std::move( outside );
			}
			for ( Solid_t &piece : pieces )
			{
				walls.push_back( std::move( piece ) );
			}
		}
	}
	MapEdit_t edit = AddObjects( *pCordoned, std::move( walls ), {}, nullptr );
	if ( copy )
	{
		// The instances' files, as the way to them from where the copy is compiled.
		for ( std::uint32_t nEntity = 0; nEntity < pCordoned->m_Map.m_Entities.size(); nEntity++ )
		{
			const Entity_t &entity = pCordoned->m_Map.m_Entities[ nEntity ];
			const std::string_view szFile = entity.GetValue( "file" );
			if ( entity.m_bDeleted || szFile.empty() || !kv1::EqualsIgnoreCase( entity.GetClassname(), "func_instance" ) )
			{
				continue;
			}
			const std::optional<std::filesystem::path> found = FindInstanceFile( szFile, copy->m_Instances );
			if ( !found )
			{
				continue;
			}
			std::error_code error;
			std::filesystem::path way = std::filesystem::relative( *found, copy->m_CopyFolder, error );
			if ( error || way.empty() )
			{
				way = *found;
			}
			Entity_t moved = entity;
			SetKeyValue( pCordoned->m_Map, moved, "file", way.generic_string() );
			edit.m_Entities.emplace_back( nEntity, std::move( moved ) );
		}
	}
	pCordoned = ApplyEdit( *pCordoned, edit );
	return WriteVmf( pCordoned->m_Map );
}

//-----------------------------------------------------------------------------
// Purpose: Twice a polygon's area (Newell's normal's length).
//-----------------------------------------------------------------------------
static double PolygonArea( const std::vector<Point_t> &points )
{
	Point_t normal{ 0, 0, 0 };
	for ( std::size_t i = 0; i < points.size(); i++ )
		normal = Add( normal, Cross( points[ i ], points[ ( i + 1 ) % points.size() ] ) );
	return Length( normal );
}

//-----------------------------------------------------------------------------
// Purpose: The corners a merged neighbour left in the middle of a straight edge.
//-----------------------------------------------------------------------------
static bool DropCollinear( std::vector<Point_t> &points )
{
	bool bDropped = false;
	for ( std::size_t i = 0; points.size() > 3 && i < points.size(); )
	{
		const Point_t &previous = points[ ( i + points.size() - 1 ) % points.size() ];
		const Point_t &next = points[ ( i + 1 ) % points.size() ];
		const Point_t along = Subtract( next, previous );
		const double flLength = Length( along );
		if ( flLength > 0 && Length( Cross( Subtract( points[ i ], previous ), along ) ) / flLength < 0.001 )
		{
			points.erase( points.begin() + static_cast<std::ptrdiff_t>( i ) );
			bDropped = true;
			continue;
		}
		i++;
	}
	return bDropped;
}

//-----------------------------------------------------------------------------
// Purpose: Sides on one plane by the solid's own faces (Hammer's 0.01 for the distance); the
//          piece with the largest face stays, the rest go, and the one kept loses its precise
//          polygon so its face is the whole cross-section again.
//-----------------------------------------------------------------------------
MapEdit_t MergeCoplanarSides( const CScene &scene, std::size_t *pMerged )
{
	MapEdit_t edit;
	std::map<int, int> movedSides; // side id gone -> side id kept
	for ( std::uint32_t nSolid = 0; nSolid < scene.m_Map.m_Solids.size() && nSolid < scene.m_Solids.size(); nSolid++ )
	{
		const Solid_t &solid = scene.m_Map.m_Solids[ nSolid ];
		const std::vector<FaceGeometry_t> &faces = std::as_const( scene.m_Solids )[ nSolid ].m_Faces;
		if ( solid.m_bDeleted || faces.size() < 2 )
			continue;
		std::vector<int> group( faces.size(), -1 );
		bool bAny = false;
		for ( std::size_t i = 0; i < faces.size(); i++ )
		{
			if ( group[ i ] >= 0 )
				continue;
			group[ i ] = static_cast<int>( i );
			for ( std::size_t j = i + 1; j < faces.size(); j++ )
			{
				if ( group[ j ] < 0 && Dot( faces[ i ].m_Plane.m_Normal, faces[ j ].m_Plane.m_Normal ) > 1.0 - 1e-6 && std::abs( faces[ i ].m_Plane.m_flDistance - faces[ j ].m_Plane.m_flDistance ) < 0.01 )
				{
					group[ j ] = static_cast<int>( i );
					bAny = true;
				}
			}
		}
		if ( !bAny )
			continue;
		bool bDisplaced = false;
		std::map<int, std::vector<std::size_t>> members;
		for ( std::size_t i = 0; i < faces.size(); i++ )
		{
			members[ group[ i ] ].push_back( i );
		}
		for ( const auto &[ nGroup, pieces ] : members )
		{
			for ( const std::size_t nFace : pieces )
				bDisplaced = bDisplaced || ( pieces.size() > 1 && solid.m_Sides[ faces[ nFace ].m_nSide ].m_pDisp );
		}
		if ( bDisplaced )
			continue;
		Solid_t merged = solid;
		std::vector<bool> gone( solid.m_Sides.size(), false );
		for ( const auto &[ nGroup, pieces ] : members )
		{
			if ( pieces.size() < 2 )
				continue;
			const std::size_t nKept = *std::ranges::max_element( pieces, {}, [ & ]( std::size_t nFace ) { return PolygonArea( faces[ nFace ].m_Points ); } );
			const std::uint32_t nKeptSide = faces[ nKept ].m_nSide;
			merged.m_Sides[ nKeptSide ].m_PlusVertices.clear();
			for ( const std::size_t nFace : pieces )
			{
				if ( nFace == nKept )
					continue;
				gone[ faces[ nFace ].m_nSide ] = true;
				movedSides[ solid.m_Sides[ faces[ nFace ].m_nSide ].m_nId ] = solid.m_Sides[ nKeptSide ].m_nId;
			}
		}
		std::vector<Side_t> sides;
		for ( std::size_t i = 0; i < merged.m_Sides.size(); i++ )
		{
			if ( gone[ i ] )
				continue;
			DropCollinear( merged.m_Sides[ i ].m_PlusVertices );
			sides.push_back( std::move( merged.m_Sides[ i ] ) );
		}
		merged.m_Sides = std::move( sides );
		merged.m_bChanged = true;
		edit.m_Solids.emplace_back( nSolid, std::move( merged ) );
	}
	if ( pMerged )
		*pMerged = edit.m_Solids.size();
	if ( movedSides.empty() )
		return edit;
	// Overlays (and cubemaps) name the faces they are on by side id.
	for ( std::uint32_t nEntity = 0; nEntity < scene.m_Map.m_Entities.size(); nEntity++ )
	{
		const Entity_t &entity = scene.m_Map.m_Entities[ nEntity ];
		if ( entity.m_bDeleted )
			continue;
		Entity_t moved = entity;
		bool bMoved = false;
		for ( const char *pszKey : { "sides", "sides2" } )
		{
			const std::string_view szSides = entity.GetValue( pszKey );
			if ( szSides.empty() )
				continue;
			std::vector<int> ids;
			bool bChanged = false;
			for ( std::size_t nAt = 0; nAt < szSides.size(); )
			{
				while ( nAt < szSides.size() && szSides[ nAt ] == ' ' )
					nAt++;
				std::size_t nEnd = szSides.find( ' ', nAt );
				nEnd = nEnd == std::string_view::npos ? szSides.size() : nEnd;
				int nId = 0;
				if ( nEnd > nAt && std::from_chars( szSides.data() + nAt, szSides.data() + nEnd, nId ).ec == std::errc{} )
				{
					if ( const auto found = movedSides.find( nId ); found != movedSides.end() )
					{
						nId = found->second;
						bChanged = true;
					}
					if ( std::ranges::find( ids, nId ) == ids.end() )
						ids.push_back( nId );
				}
				nAt = nEnd;
			}
			if ( !bChanged )
				continue;
			std::string szNew;
			for ( const int nId : ids )
				szNew += ( szNew.empty() ? "" : " " ) + std::to_string( nId );
			SetKeyValue( scene.m_Map, moved, pszKey, szNew );
			bMoved = true;
		}
		if ( bMoved )
			edit.m_Entities.emplace_back( nEntity, std::move( moved ) );
	}
	return edit;
}

} // namespace xv::map
