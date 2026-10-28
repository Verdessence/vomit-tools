//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's cordon: what it leaves out, and the sealed map it compiles.
//
//===========================================================================//
#include "Cordon.h"

#include <algorithm>

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
std::string CordonedMapText( const CScene &scene, std::string_view szMaterial )
{
	const std::vector<Box_t> boxes = ActiveCordonBoxes( scene.m_Map );
	if ( boxes.empty() )
	{
		return WriteVmf( scene.m_Map );
	}
	std::shared_ptr<CScene> pCordoned = ApplyEdit( scene, DeleteObjects( scene, ObjectsOutsideCordon( scene ) ) );
	FaceDefaults_t faces;
	faces.m_szMaterial = szMaterial;
	std::vector<Solid_t> walls;
	for ( const Box_t &box : boxes )
	{
		for ( Solid_t &wall : CordonWalls( pCordoned->m_Map, box, 16.0, faces ) )
		{
			walls.push_back( std::move( wall ) );
		}
	}
	pCordoned = ApplyEdit( *pCordoned, AddObjects( *pCordoned, std::move( walls ), {}, nullptr ) );
	return WriteVmf( pCordoned->m_Map );
}

} // namespace xv::map
