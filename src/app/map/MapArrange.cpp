//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Map > Align: the selected objects lined up on an edge or centre, spaced out evenly
//          (across, up, or on a diagonal), or each dropped onto the floor under it or raised
//          to the ceiling over it.
//
//===========================================================================//
#include "map/MapEditor.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>

#include <QCoreApplication>

#include "map/Edit.h"
#include "map/MapView2D.h"
#include "map/MapView3D.h"
#include "map/Trace.h"

namespace
{

// How far a drop looks for something under (or over) an object.
constexpr double DROP_REACH = 65536.0;
// Rays start this far inside the object's box, so a floor it already rests on is found at once.
constexpr double DROP_INSET = 0.5;
// Rays cast across the bottom (or top) of each object, in a grid this many a side.
constexpr int DROP_RAYS = 5;

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::pair<QString, MapAction_t>> MapAlignActions()
{
	return {
		{ QCoreApplication::translate( "CMapEditor", "Align Left" ), MapAction_t::MAP_ACTION_ALIGN_LEFT },
		{ QCoreApplication::translate( "CMapEditor", "Align Right" ), MapAction_t::MAP_ACTION_ALIGN_RIGHT },
		{ QCoreApplication::translate( "CMapEditor", "Align Top" ), MapAction_t::MAP_ACTION_ALIGN_TOP },
		{ QCoreApplication::translate( "CMapEditor", "Align Bottom" ), MapAction_t::MAP_ACTION_ALIGN_BOTTOM },
		{ QCoreApplication::translate( "CMapEditor", "Centre Horizontally" ), MapAction_t::MAP_ACTION_ALIGN_CENTER_HORIZONTAL },
		{ QCoreApplication::translate( "CMapEditor", "Centre Vertically" ), MapAction_t::MAP_ACTION_ALIGN_CENTER_VERTICAL },
		{ {}, MapAction_t::MAP_ACTION_COUNT },
		{ QCoreApplication::translate( "CMapEditor", "Space Evenly Across" ), MapAction_t::MAP_ACTION_DISTRIBUTE_HORIZONTAL },
		{ QCoreApplication::translate( "CMapEditor", "Space Evenly Up" ), MapAction_t::MAP_ACTION_DISTRIBUTE_VERTICAL },
		{ QCoreApplication::translate( "CMapEditor", "Space Evenly on a Diagonal" ), MapAction_t::MAP_ACTION_DISTRIBUTE_DIAGONAL },
		{ {}, MapAction_t::MAP_ACTION_COUNT },
		{ QCoreApplication::translate( "CMapEditor", "Move to Floor" ), MapAction_t::MAP_ACTION_MOVE_TO_FLOOR },
		{ QCoreApplication::translate( "CMapEditor", "Move to Ceiling" ), MapAction_t::MAP_ACTION_MOVE_TO_CEILING },
	};
}

//-----------------------------------------------------------------------------
// Purpose: Objects mode takes groups apart; an entity always moves with its solids.
//-----------------------------------------------------------------------------
std::vector<std::vector<std::uint32_t>> CMapEditor::SelectionUnits() const
{
	std::vector<std::vector<std::uint32_t>> units;
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	if ( !pScene )
		return units;
	const bool bGroups = m_pDocument->GetSelectMode() == MapSelectMode_t::MAP_SELECT_MODE_GROUPS;
	std::map<int, int> parents; // group id: its parent's
	for ( const xv::map::Group_t &group : pScene->m_Map.m_Groups )
		parents[ group.m_nId ] = group.m_Editor.m_nGroup;
	const auto outermost = [ & ]( int nGroup ) {
		for ( int nDepth = 0; nDepth < 64; nDepth++ )
		{
			const auto it = parents.find( nGroup );
			if ( it == parents.end() || it->second == 0 )
				break;
			nGroup = it->second;
		}
		return nGroup;
	};
	// Keyed by what the unit is: an entity, a group, or a world solid on its own.
	std::map<std::pair<int, std::int64_t>, std::size_t> found;
	for ( const std::uint32_t n : m_pDocument->GetSelection() )
	{
		if ( n >= pScene->GetObjectCount() || pScene->IsDeleted( n ) )
			continue;
		const int nEntity = pScene->GetObjectEntity( n );
		const int nGroup = bGroups ? pScene->GetObjectEditor( n ).m_nGroup : 0;
		std::pair<int, std::int64_t> key;
		std::uint32_t nObject = n;
		if ( nGroup != 0 )
		{
			key = { 1, outermost( nGroup ) };
			if ( nEntity >= 0 )
				nObject = pScene->EntityObject( static_cast<std::size_t>( nEntity ) );
		}
		else if ( nEntity >= 0 )
		{
			key = { 0, nEntity };
			nObject = pScene->EntityObject( static_cast<std::size_t>( nEntity ) );
		}
		else
		{
			key = { 2, n };
		}
		const auto [ it, bAdded ] = found.try_emplace( key, units.size() );
		if ( bAdded )
			units.emplace_back();
		std::vector<std::uint32_t> &unit = units[ it->second ];
		if ( std::ranges::find( unit, nObject ) == unit.end() )
			unit.push_back( nObject );
	}
	return units;
}

//-----------------------------------------------------------------------------
// Purpose: A point entity's box is its class's; a prop's model is usually another size, so its
//          model's bounds are taken where they are loaded and still stand round its origin
//          (they are worked out when the props are placed, which can lag an edit).
//-----------------------------------------------------------------------------
xv::map::Box_t CMapEditor::UnitBounds( const std::vector<std::uint32_t> &unit ) const
{
	xv::map::Box_t box;
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	for ( const std::uint32_t n : unit )
	{
		xv::map::Box_t bounds = m_pDocument->GetObjectBounds( n );
		if ( pScene && !pScene->IsSolidObject( n ) )
		{
			const std::size_t nEntity = pScene->GetIndex( n );
			if ( pScene->m_Entities[ nEntity ].m_bPoint )
			{
				const xv::map::Box_t model = m_pRenderData->GetPropBounds( n );
				const xv::map::Point_t &origin = pScene->m_Entities[ nEntity ].m_Origin;
				const bool bAround = !model.IsEmpty() && origin[ 0 ] >= model.m_Mins[ 0 ] - 1.0 && origin[ 0 ] <= model.m_Maxs[ 0 ] + 1.0 && origin[ 1 ] >= model.m_Mins[ 1 ] - 1.0 && origin[ 1 ] <= model.m_Maxs[ 1 ] + 1.0;
				if ( bAround )
					bounds = model;
			}
		}
		box.Add( bounds );
	}
	return box;
}

//-----------------------------------------------------------------------------
// Purpose: The units don't share objects, so their edits put together are one edit.
//-----------------------------------------------------------------------------
void CMapEditor::MoveUnits( const std::vector<std::vector<std::uint32_t>> &units, const std::vector<xv::map::Point_t> &offsets, const QString &szName )
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	if ( !pScene )
		return;
	xv::map::MapEdit_t edit;
	for ( std::size_t i = 0; i < units.size() && i < offsets.size(); i++ )
	{
		const xv::map::Point_t &offset = offsets[ i ];
		if ( std::abs( offset[ 0 ] ) < 1e-6 && std::abs( offset[ 1 ] ) < 1e-6 && std::abs( offset[ 2 ] ) < 1e-6 )
			continue;
		xv::map::Transform_t move;
		move.m_Offset = offset;
		xv::map::MapEdit_t one = xv::map::TransformObjects( *pScene, units[ i ], move, m_Settings.m_bTextureLock, m_Settings.m_bScaleLock );
		std::ranges::move( one.m_Solids, std::back_inserter( edit.m_Solids ) );
		std::ranges::move( one.m_Entities, std::back_inserter( edit.m_Entities ) );
		std::ranges::move( one.m_SolidTransforms, std::back_inserter( edit.m_SolidTransforms ) );
	}
	if ( edit.IsEmpty() )
	{
		emit StatusMessage( tr( "%1: nothing to move" ).arg( szName ) );
		return;
	}
	m_pDocument->Edit( std::move( edit ), szName, m_pDocument->GetSelection() );
}

//-----------------------------------------------------------------------------
// Purpose: Edges and centres line up with the selection's box; spacing keeps the outermost two
//          where they are and puts the others' centres evenly between them.
//-----------------------------------------------------------------------------
void CMapEditor::AlignSelection( MapAction_t nHow, CMapView *pFrom )
{
	const std::vector<std::vector<std::uint32_t>> units = SelectionUnits();
	if ( units.size() < 2 )
	{
		emit StatusMessage( tr( "Align needs two or more objects selected" ) );
		return;
	}
	std::size_t nAcross = 0;
	std::size_t nUp = 2;
	if ( const auto *p2D = qobject_cast<CMapView2D *>( pFrom ) )
	{
		nAcross = static_cast<std::size_t>( p2D->GetAxisIndex( 0 ) );
		nUp = static_cast<std::size_t>( p2D->GetAxisIndex( 1 ) );
	}
	std::vector<xv::map::Box_t> boxes;
	xv::map::Box_t all;
	for ( const std::vector<std::uint32_t> &unit : units )
	{
		boxes.push_back( UnitBounds( unit ) );
		all.Add( boxes.back() );
	}
	if ( all.IsEmpty() )
		return;
	std::vector<xv::map::Point_t> offsets( units.size(), xv::map::Point_t{} );
	QString szName;
	const auto center = []( const xv::map::Box_t &box, std::size_t a ) { return ( box.m_Mins[ a ] + box.m_Maxs[ a ] ) * 0.5; };
	const auto spread = [ & ]( std::vector<std::size_t> axes ) {
		std::vector<std::size_t> order( units.size() );
		std::iota( order.begin(), order.end(), std::size_t{ 0 } );
		std::ranges::sort( order, [ & ]( std::size_t a, std::size_t b ) {
			for ( const std::size_t nAxis : axes )
				if ( center( boxes[ a ], nAxis ) != center( boxes[ b ], nAxis ) )
					return center( boxes[ a ], nAxis ) < center( boxes[ b ], nAxis );
			return a < b;
		} );
		const std::size_t nFirst = order.front();
		const std::size_t nLast = order.back();
		for ( std::size_t i = 1; i + 1 < order.size(); i++ )
		{
			const double flAt = static_cast<double>( i ) / static_cast<double>( order.size() - 1 );
			for ( const std::size_t nAxis : axes )
			{
				const double flFrom = center( boxes[ nFirst ], nAxis );
				const double flTo = center( boxes[ nLast ], nAxis );
				offsets[ order[ i ] ][ nAxis ] = flFrom + ( flTo - flFrom ) * flAt - center( boxes[ order[ i ] ], nAxis );
			}
		}
	};
	for ( std::size_t i = 0; i < units.size(); i++ )
	{
		const xv::map::Box_t &box = boxes[ i ];
		switch ( nHow )
		{
			case MapAction_t::MAP_ACTION_ALIGN_LEFT: offsets[ i ][ nAcross ] = all.m_Mins[ nAcross ] - box.m_Mins[ nAcross ]; break;
			case MapAction_t::MAP_ACTION_ALIGN_RIGHT: offsets[ i ][ nAcross ] = all.m_Maxs[ nAcross ] - box.m_Maxs[ nAcross ]; break;
			case MapAction_t::MAP_ACTION_ALIGN_TOP: offsets[ i ][ nUp ] = all.m_Maxs[ nUp ] - box.m_Maxs[ nUp ]; break;
			case MapAction_t::MAP_ACTION_ALIGN_BOTTOM: offsets[ i ][ nUp ] = all.m_Mins[ nUp ] - box.m_Mins[ nUp ]; break;
			case MapAction_t::MAP_ACTION_ALIGN_CENTER_HORIZONTAL: offsets[ i ][ nAcross ] = center( all, nAcross ) - center( box, nAcross ); break;
			case MapAction_t::MAP_ACTION_ALIGN_CENTER_VERTICAL: offsets[ i ][ nUp ] = center( all, nUp ) - center( box, nUp ); break;
			default: break;
		}
	}
	switch ( nHow )
	{
		case MapAction_t::MAP_ACTION_DISTRIBUTE_HORIZONTAL: spread( { nAcross } ); break;
		case MapAction_t::MAP_ACTION_DISTRIBUTE_VERTICAL: spread( { nUp } ); break;
		case MapAction_t::MAP_ACTION_DISTRIBUTE_DIAGONAL: spread( { nAcross, nUp } ); break;
		default: break;
	}
	for ( const auto &[ szText, nAction ] : MapAlignActions() )
		if ( nAction == nHow )
			szName = szText;
	MoveUnits( units, offsets, szName );
}

//-----------------------------------------------------------------------------
// Purpose: Rays go straight down (up) from a grid across each object's bottom (top) to the
//          nearest surface that isn't selected, as the 3D views draw it (hidden objects, and
//          tool textures while they are hidden, are passed through); the object moves by the
//          shortest. An object with nothing under it stays.
//-----------------------------------------------------------------------------
void CMapEditor::DropSelection( bool bUp )
{
	const QString szName = bUp ? tr( "Move to Ceiling" ) : tr( "Move to Floor" );
	const std::vector<std::vector<std::uint32_t>> units = SelectionUnits();
	CMapView3D *p3D = nullptr;
	for ( CMapView *pView : GetViews() )
		if ( ( p3D = qobject_cast<CMapView3D *>( pView ) ) )
			break;
	if ( units.empty() || !p3D )
	{
		emit StatusMessage( units.empty() ? tr( "%1 needs objects selected" ).arg( szName ) : tr( "%1 needs a 3D view open" ).arg( szName ) );
		return;
	}
	const MapFrame_t frame = p3D->MakeFrame( p3D->size() );
	const xv::map::RasterScene_t scene = frame.MakeScene();
	const std::vector<std::uint32_t> &selection = m_pDocument->GetSelection();
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	// What moves: the selection and the solids of selected entities.
	std::vector<std::uint32_t> moving = selection;
	for ( const std::vector<std::uint32_t> &unit : units )
		for ( const std::uint32_t n : unit )
		{
			moving.push_back( n );
			if ( !pScene->IsSolidObject( n ) )
				for ( const std::uint32_t nSolid : pScene->m_Map.m_Entities[ pScene->GetIndex( n ) ].m_Solids )
					moving.push_back( pScene->SolidObject( nSolid ) );
		}
	std::ranges::sort( moving );
	const double flDir = bUp ? 1.0 : -1.0;
	std::vector<xv::map::Point_t> offsets( units.size(), xv::map::Point_t{} );
	std::size_t nMoved = 0;
	for ( std::size_t i = 0; i < units.size(); i++ )
	{
		const xv::map::Box_t box = UnitBounds( units[ i ] );
		if ( box.IsEmpty() )
			continue;
		const double flStart = bUp ? box.m_Maxs[ 2 ] - DROP_INSET : box.m_Mins[ 2 ] + DROP_INSET;
		std::optional<double> flNearest;
		for ( int y = 0; y < DROP_RAYS; y++ )
		{
			for ( int x = 0; x < DROP_RAYS; x++ )
			{
				// Across the face, just inside its edges, so a wall beside it isn't taken for a floor.
				const auto at = [ & ]( std::size_t a, int n ) {
					const double flInset = std::min( 1.0, ( box.m_Maxs[ a ] - box.m_Mins[ a ] ) * 0.25 );
					return box.m_Mins[ a ] + flInset + ( box.m_Maxs[ a ] - box.m_Mins[ a ] - 2.0 * flInset ) * n / ( DROP_RAYS - 1 );
				};
				const xv::map::Point_t from{ at( 0, x ), at( 1, y ), flStart };
				for ( const xv::map::TraceHit_t &hit : xv::map::TraceScene( scene, m_Settings.m_bToolTextures, m_Settings.m_bNodraw, from, { 0, 0, flDir }, DROP_REACH ) )
				{
					if ( std::ranges::binary_search( moving, hit.m_nObject ) )
						continue;
					if ( !flNearest || hit.m_flDistance < *flNearest )
						flNearest = hit.m_flDistance;
					break;
				}
			}
		}
		if ( flNearest )
		{
			offsets[ i ][ 2 ] = flDir * ( *flNearest - DROP_INSET );
			nMoved++;
		}
	}
	if ( nMoved == 0 )
	{
		emit StatusMessage( bUp ? tr( "Nothing is over the selection" ) : tr( "Nothing is under the selection" ) );
		return;
	}
	MoveUnits( units, offsets, szName );
}
