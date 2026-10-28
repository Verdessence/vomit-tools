//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Clip.h.
//
//===========================================================================//
#include "Clip.h"

#include <algorithm>
#include <cmath>

#include "map/Brushes.h"
#include "map/Faces.h"

namespace xv::map
{

namespace
{

constexpr double ON_PLANE = 0.01; // how far from a plane a corner may be and still be on it

//-----------------------------------------------------------------------------
// Purpose: Where the solid's corners are relative to the plane: any in front, any behind.
//-----------------------------------------------------------------------------
std::pair<bool, bool> Straddles( const SolidGeometry_t &shape, const Plane_t &plane )
{
	bool bFront = false, bBack = false;
	for ( const FaceGeometry_t &face : shape.m_Faces )
	{
		for ( const Point_t &p : face.m_Points )
		{
			const double fl = plane.Distance( p );
			bFront = bFront || fl > ON_PLANE;
			bBack = bBack || fl < -ON_PLANE;
		}
	}
	return { bFront, bBack };
}

//-----------------------------------------------------------------------------
// Purpose: The solid's sides that still have a face, Hammer++'s corners dropped (the planes
//          are what stand), as a solid of its own; nullopt when nothing is left.
//-----------------------------------------------------------------------------
std::optional<Solid_t> Trim( Solid_t solid )
{
	for ( Side_t &side : solid.m_Sides )
	{
		side.m_PlusVertices.clear();
	}
	const SolidGeometry_t shape = BuildSolid( solid );
	if ( ( shape.m_nProblems & ( SOLID_PROBLEM_EMPTY | SOLID_PROBLEM_OPEN ) ) != 0 || shape.m_Faces.size() < 4 )
	{
		return std::nullopt;
	}
	std::vector<Side_t> kept;
	for ( std::uint32_t nSide = 0; nSide < solid.m_Sides.size(); nSide++ )
	{
		if ( shape.FindFace( nSide ) )
		{
			kept.push_back( std::move( solid.m_Sides[ nSide ] ) );
		}
	}
	solid.m_Sides = std::move( kept );
	solid.m_bChanged = true;
	return BuildSolid( solid ).IsValid() ? std::optional{ std::move( solid ) } : std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Plane_t Flipped( const Plane_t &plane )
{
	return { Scale( plane.m_Normal, -1.0 ), -plane.m_flDistance };
}

//-----------------------------------------------------------------------------
// Purpose: The same plane, moved flBy along its normal.
//-----------------------------------------------------------------------------
Plane_t Shifted( const Plane_t &plane, double flBy )
{
	return { plane.m_Normal, plane.m_flDistance + flBy };
}

//-----------------------------------------------------------------------------
// Purpose: A new solid for the map: ids of its own, no span, no old owner.
//-----------------------------------------------------------------------------
Solid_t AsNew( Solid_t solid, int &nNextId, int &nNextSide )
{
	solid.m_Span = {};
	solid.m_nId = nNextId++;
	solid.m_nReadEntity = -2;
	solid.m_bChanged = false;
	solid.m_bDeleted = false;
	for ( Side_t &side : solid.m_Sides )
	{
		side.m_Span = {};
		side.m_nId = nNextSide++;
	}
	return solid;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The plane's own axes give the three points; the texture is world-aligned on the new
//          face, at the scale of the side it is like, as Hammer's clip leaves it.
//-----------------------------------------------------------------------------
Side_t SideOnPlane( const CMap &map, const Plane_t &plane, const Side_t &like )
{
	const Point_t n = Normalized( plane.m_Normal );
	const Point_t p1 = Scale( n, plane.m_flDistance );
	const auto [ u, v ] = FaceAxes( n );
	// (p0 - p1) x (p2 - p1) must run along the normal: u x v does when v = u x n... use u and -v.
	const Point_t a = Scale( u, 64.0 );
	const Point_t b = Scale( v, -64.0 );
	Side_t side;
	side.m_Plane = { Add( p1, a ), p1, Add( p1, b ) };
	if ( const std::optional<Plane_t> check = PlaneFromPoints( side.m_Plane ); check && Dot( check->m_Normal, n ) < 0.0 )
	{
		std::swap( side.m_Plane[ 0 ], side.m_Plane[ 2 ] );
	}
	side.m_bPlaneRead = true;
	side.m_szMaterial = like.m_szMaterial;
	side.m_nMaterial = like.m_nMaterial;
	const auto [ wu, wv ] = WorldAxes( n );
	side.m_UAxis = { wu, 0.0, like.m_UAxis.m_flScale };
	side.m_VAxis = { wv, 0.0, like.m_VAxis.m_flScale };
	side.m_nLightmapScale = like.m_nLightmapScale;
	side.m_nSmoothingGroups = 0;
	static_cast<void>( map );
	return side;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
ClipResult_t ClipSolid( const CMap &map, const Solid_t &solid, const Plane_t &plane )
{
	ClipResult_t result;
	if ( solid.m_Sides.empty() || std::ranges::any_of( solid.m_Sides, []( const Side_t &side ) { return static_cast<bool>( side.m_pDisp ); } ) )
	{
		return result;
	}
	const SolidGeometry_t shape = BuildSolid( solid );
	if ( !shape.IsValid() )
	{
		return result;
	}
	const auto [ bFront, bBack ] = Straddles( shape, plane );
	if ( !bFront || !bBack )
	{
		// Wholly on one side: that part is the solid as it is.
		( bFront ? result.m_Front : result.m_Back ) = solid;
		return result;
	}
	// In front: the part the plane's back side closes off (a side faces out of its solid, so
	// the new side's normal points into the plane's front... away from the kept part).
	Solid_t front = solid;
	front.m_Sides.push_back( SideOnPlane( map, Flipped( plane ), solid.m_Sides.front() ) );
	Solid_t back = solid;
	back.m_Sides.push_back( SideOnPlane( map, plane, solid.m_Sides.front() ) );
	result.m_Front = Trim( std::move( front ) );
	result.m_Back = Trim( std::move( back ) );
	return result;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapEdit_t ClipObjects( const CScene &scene, const std::vector<std::uint32_t> &objects, const Plane_t &plane, ClipKeep_t nKeep, std::vector<std::uint32_t> *pAdded )
{
	MapEdit_t edit;
	const CMap &map = scene.m_Map;
	int nNextId = scene.m_nNextObjectId;
	int nNextSide = scene.m_nNextSideId;
	std::uint32_t nObject = scene.GetObjectCount();
	auto nNewIndex = static_cast<std::uint32_t>( map.m_Solids.size() );
	for ( const std::uint32_t nSolid : ObjectsOf( scene, objects ).m_Solids )
	{
		const Solid_t &solid = map.m_Solids[ nSolid ];
		ClipResult_t cut = ClipSolid( map, solid, plane );
		if ( !cut.m_Front || !cut.m_Back )
		{
			continue; // the plane misses it (or it can't be cut)
		}
		std::optional<Solid_t> &kept = nKeep == ClipKeep_t::CLIP_KEEP_BACK ? cut.m_Back : cut.m_Front;
		std::optional<Solid_t> &other = nKeep == ClipKeep_t::CLIP_KEEP_BACK ? cut.m_Front : cut.m_Back;
		kept->m_bChanged = true;
		for ( Side_t &side : kept->m_Sides )
		{
			side.m_nId = side.m_nId == 0 ? nNextSide++ : side.m_nId;
		}
		edit.m_Solids.emplace_back( nSolid, std::move( *kept ) );
		if ( nKeep == ClipKeep_t::CLIP_KEEP_BOTH )
		{
			Solid_t added = AsNew( std::move( *other ), nNextId, nNextSide );
			added.m_nEntity = solid.m_nEntity;
			edit.m_Solids.emplace_back( nNewIndex++, std::move( added ) );
			if ( pAdded )
			{
				pAdded->push_back( nObject++ );
			}
		}
	}
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose: Each solid a carver overlaps is cut plane by plane: what lies outside a plane is a
//          piece kept, what lies inside goes on to the next plane; what is inside them all is
//          the carved hole.
//-----------------------------------------------------------------------------
MapEdit_t CarveObjects( const CScene &scene, const std::vector<std::uint32_t> &carvers, std::vector<std::uint32_t> *pAdded )
{
	MapEdit_t edit;
	const CMap &map = scene.m_Map;
	int nNextId = scene.m_nNextObjectId;
	int nNextSide = scene.m_nNextSideId;
	std::uint32_t nObject = scene.GetObjectCount();
	auto nNewIndex = static_cast<std::uint32_t>( map.m_Solids.size() );
	const std::vector<std::uint32_t> carving = ObjectsOf( scene, carvers ).m_Solids;
	for ( std::uint32_t nSolid = 0; nSolid < map.m_Solids.size(); nSolid++ )
	{
		const Solid_t &target = map.m_Solids[ nSolid ];
		if ( target.m_bDeleted || std::ranges::find( carving, nSolid ) != carving.end() )
		{
			continue;
		}
		std::vector<Solid_t> pieces{ target };
		bool bCarved = false;
		for ( const std::uint32_t nCarver : carving )
		{
			const SolidGeometry_t &carver = scene.m_Solids[ nCarver ];
			if ( !carver.IsValid() || !carver.m_Bounds.Intersects( scene.m_Solids[ nSolid ].m_Bounds ) )
			{
				continue;
			}
			std::vector<Solid_t> next;
			for ( Solid_t &piece : pieces )
			{
				std::optional<Solid_t> inside = piece;
				for ( const FaceGeometry_t &face : carver.m_Faces )
				{
					if ( !inside )
					{
						break;
					}
					ClipResult_t cut = ClipSolid( map, *inside, face.m_Plane );
					if ( cut.m_Front )
					{
						next.push_back( std::move( *cut.m_Front ) );
						bCarved = bCarved || cut.m_Back.has_value();
					}
					inside = std::move( cut.m_Back );
				}
				// What is left inside every plane of the carver is taken away.
				bCarved = bCarved || inside.has_value();
			}
			pieces = std::move( next );
		}
		if ( !bCarved )
		{
			continue;
		}
		if ( pieces.empty() )
		{
			Solid_t gone = target;
			gone.m_bDeleted = true;
			edit.m_Solids.emplace_back( nSolid, std::move( gone ) );
			continue;
		}
		for ( std::size_t i = 0; i < pieces.size(); i++ )
		{
			if ( i == 0 )
			{
				pieces[ i ].m_bChanged = true;
				for ( Side_t &side : pieces[ i ].m_Sides )
				{
					side.m_nId = side.m_nId == 0 ? nNextSide++ : side.m_nId;
				}
				edit.m_Solids.emplace_back( nSolid, std::move( pieces[ i ] ) );
				continue;
			}
			Solid_t added = AsNew( std::move( pieces[ i ] ), nNextId, nNextSide );
			added.m_nEntity = target.m_nEntity;
			edit.m_Solids.emplace_back( nNewIndex++, std::move( added ) );
			if ( pAdded )
			{
				pAdded->push_back( nObject++ );
			}
		}
	}
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose: Wall i is the solid cut to the slab between face i's plane and that plane moved in
//          by the width, and cut back by the earlier walls' inner planes so walls don't
//          overlap; the walls share a new group, as Hammer's do. A negative width grows the
//          solid outward first and builds the slabs outside the faces.
//-----------------------------------------------------------------------------
MapEdit_t HollowObjects( const CScene &scene, const std::vector<std::uint32_t> &objects, double flWallWidth, std::vector<std::uint32_t> *pAdded )
{
	MapEdit_t edit;
	const CMap &map = scene.m_Map;
	int nNextId = scene.m_nNextObjectId;
	int nNextSide = scene.m_nNextSideId;
	std::uint32_t nObject = scene.GetObjectCount();
	auto nNewIndex = static_cast<std::uint32_t>( map.m_Solids.size() );
	if ( std::abs( flWallWidth ) < 1.0 )
	{
		return edit;
	}
	for ( const std::uint32_t nSolid : ObjectsOf( scene, objects ).m_Solids )
	{
		const Solid_t &solid = map.m_Solids[ nSolid ];
		const SolidGeometry_t &shape = scene.m_Solids[ nSolid ];
		if ( !shape.IsValid() || std::ranges::any_of( solid.m_Sides, []( const Side_t &side ) { return static_cast<bool>( side.m_pDisp ); } ) )
		{
			continue;
		}
		// Outward: the solid every plane of which has moved out, the walls between the old
		// planes and the new.
		Solid_t whole = solid;
		std::vector<Plane_t> outer;
		std::vector<Plane_t> inner;
		for ( const FaceGeometry_t &face : shape.m_Faces )
		{
			const Plane_t &plane = face.m_Plane; // facing out of the solid
			if ( flWallWidth > 0.0 )
			{
				outer.push_back( plane );
				inner.push_back( Shifted( plane, -flWallWidth ) );
			}
			else
			{
				outer.push_back( Shifted( plane, -flWallWidth ) );
				inner.push_back( plane );
			}
		}
		if ( flWallWidth < 0.0 )
		{
			whole.m_Sides.clear();
			for ( std::size_t i = 0; i < outer.size(); i++ )
			{
				whole.m_Sides.push_back( SideOnPlane( map, outer[ i ], solid.m_Sides[ shape.m_Faces[ i ].m_nSide ] ) );
				whole.m_Sides.back().m_UAxis = solid.m_Sides[ shape.m_Faces[ i ].m_nSide ].m_UAxis;
				whole.m_Sides.back().m_VAxis = solid.m_Sides[ shape.m_Faces[ i ].m_nSide ].m_VAxis;
			}
		}
		Group_t group;
		group.m_nId = nNextId++;
		group.m_Editor.m_Color = { 220, 220, 220 };
		group.m_Editor.m_bHasColor = true;
		std::vector<Solid_t> walls;
		for ( std::size_t i = 0; i < outer.size(); i++ )
		{
			// Between the outer plane (kept by the solid's own side) and the inner one: the part
			// outside the inner plane (its front, which faces out).
			std::optional<Solid_t> wall = ClipSolid( map, whole, inner[ i ] ).m_Front;
			for ( std::size_t j = 0; j < i && wall; j++ )
			{
				wall = ClipSolid( map, *wall, inner[ j ] ).m_Back;
			}
			if ( wall )
			{
				wall->m_Editor.m_nGroup = group.m_nId;
				wall->m_Editor.m_bChanged = true;
				walls.push_back( std::move( *wall ) );
			}
		}
		if ( walls.empty() )
		{
			continue;
		}
		edit.m_Groups.push_back( group );
		for ( std::size_t i = 0; i < walls.size(); i++ )
		{
			if ( i == 0 )
			{
				walls[ i ].m_bChanged = true;
				for ( Side_t &side : walls[ i ].m_Sides )
				{
					side.m_nId = side.m_nId == 0 ? nNextSide++ : side.m_nId;
				}
				edit.m_Solids.emplace_back( nSolid, std::move( walls[ i ] ) );
				continue;
			}
			Solid_t added = AsNew( std::move( walls[ i ] ), nNextId, nNextSide );
			added.m_nEntity = solid.m_nEntity;
			edit.m_Solids.emplace_back( nNewIndex++, std::move( added ) );
			if ( pAdded )
			{
				pAdded->push_back( nObject++ );
			}
		}
	}
	return edit;
}

} // namespace xv::map
