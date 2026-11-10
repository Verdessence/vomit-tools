//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The Brush Edit tool's selection extras (loops, rings, grow and shrink, linked,
//          similar, by material), its pivots and 3D cursor, proportional editing and slides.
//
//===========================================================================//
#include "MapBrushEdit.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include "map/MapEditor.h"
#include "map/MapView2D.h"
#include "map/MapView3D.h"

using namespace xv::map;

namespace
{
// How near the pointer an edge must be to be picked, in pixels.
constexpr double EDGE_PICK_PIXELS = 12;

//-----------------------------------------------------------------------------
// Purpose: How far a point is from a segment on the screen.
//-----------------------------------------------------------------------------
double SegmentDistance( QPointF at, QPointF a, QPointF b )
{
	const QPointF along = b - a;
	const double flLength = QPointF::dotProduct( along, along );
	const double t = flLength < 1e-9 ? 0 : std::clamp( QPointF::dotProduct( at - a, along ) / flLength, 0.0, 1.0 );
	const QPointF d = at - ( a + along * t );
	return std::sqrt( QPointF::dotProduct( d, d ) );
}

//-----------------------------------------------------------------------------
// Purpose: Parts put together, each once.
//-----------------------------------------------------------------------------
std::vector<BrushPart_t> United( std::vector<BrushPart_t> a, const std::vector<BrushPart_t> &b )
{
	a.insert( a.end(), b.begin(), b.end() );
	std::ranges::sort( a );
	a.erase( std::ranges::unique( a ).begin(), a.end() );
	return a;
}
} // namespace

//-----------------------------------------------------------------------------
// Purpose: The selected brushes, as their objects.
//-----------------------------------------------------------------------------
std::vector<std::uint32_t> CMapBrushEdit::SelectedSolidObjects() const
{
	std::vector<std::uint32_t> objects;
	const auto *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene )
		return objects;
	for ( const auto nSolid : ObjectsOf( *pScene, m_Editor.GetDocument().GetSelection() ).m_Solids )
		objects.push_back( pScene->SolidObject( nSolid ) );
	return objects;
}

//-----------------------------------------------------------------------------
// Purpose: In 3D, the edge of the face under the pointer nearest it; otherwise any selected
//          brush's edge within a few pixels. The face beside it: the one clicked, or the one
//          facing the view most.
//-----------------------------------------------------------------------------
std::optional<CMapBrushEdit::EdgePick_t> CMapBrushEdit::EdgeAt( CMapView &view, QPointF at ) const
{
	const auto *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene )
		return std::nullopt;
	const auto objects = SelectedSolidObjects();
	const auto distanceTo = [ & ]( const BrushMeshParts_t &parts, std::uint32_t e ) {
		const CBrushMesh &mesh = parts.m_Mesh;
		const auto edge = parts.m_Edges[ e ];
		const auto a = Project( view, mesh.m_Vertices[ edge.first ] ), b = Project( view, mesh.m_Vertices[ edge.second ] );
		return a && b ? SegmentDistance( at, *a, *b ) : std::numeric_limits<double>::max();
	};
	if ( auto *p3D = qobject_cast<CMapView3D *>( &view ) )
		if ( const auto hit = p3D->FaceAt( at ) )
		{
			const std::uint32_t nObject = pScene->SolidObject( hit->m_nSolid );
			if ( std::ranges::find( objects, nObject ) != objects.end() )
			{
				const auto pMeshParts = MeshOf( *pScene, hit->m_nSolid );
				const CBrushMesh &mesh = pMeshParts->m_Mesh;
				for ( std::uint32_t f = 0; f < mesh.m_Faces.size(); f++ )
				{
					if ( mesh.m_Faces[ f ].m_nSide != static_cast<int>( hit->m_nSide ) )
						continue;
					const auto &corners = mesh.m_Faces[ f ].m_Corners;
					std::optional<EdgePick_t> best;
					double flBest = std::numeric_limits<double>::max();
					for ( std::size_t i = 0; i < corners.size(); i++ )
						if ( const auto e = EdgeIndex( mesh, corners[ i ], corners[ ( i + 1 ) % corners.size() ] ) )
							if ( const double flDistance = distanceTo( *pMeshParts, *e ); flDistance < flBest )
							{
								flBest = flDistance;
								best = EdgePick_t{ nObject, *e, f };
							}
					if ( best )
						return best;
				}
			}
		}
	std::optional<EdgePick_t> best;
	double flBest = EDGE_PICK_PIXELS;
	const Point_t forward = Camera( view ).GetForward();
	for ( const std::uint32_t nObject : objects )
	{
		const auto pMeshParts = MeshOf( *pScene, pScene->GetIndex( nObject ) );
		const CBrushMesh &mesh = pMeshParts->m_Mesh;
		for ( std::uint32_t e = 0, nCount = static_cast<std::uint32_t>( pMeshParts->m_Edges.size() ); e < nCount; e++ )
			if ( const double flDistance = distanceTo( *pMeshParts, e ); flDistance < flBest )
			{
				flBest = flDistance;
				std::optional<std::uint32_t> nFace;
				double flFacing = 2;
				for ( const std::uint32_t f : FacesOfEdge( mesh, e ) )
					if ( const auto plane = mesh.FacePlane( f ); plane && Dot( plane->m_Normal, forward ) < flFacing )
					{
						flFacing = Dot( plane->m_Normal, forward );
						nFace = f;
					}
				best = EdgePick_t{ nObject, e, nFace };
			}
	}
	return best;
}

//-----------------------------------------------------------------------------
// Purpose: The selected brush under a point of a view.
//-----------------------------------------------------------------------------
std::optional<std::uint32_t> CMapBrushEdit::ObjectAt( CMapView &view, QPointF at ) const
{
	const auto *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene )
		return std::nullopt;
	if ( auto *p3D = qobject_cast<CMapView3D *>( &view ) )
		if ( const auto hit = p3D->FaceAt( at ) )
		{
			const auto objects = SelectedSolidObjects();
			const std::uint32_t nObject = pScene->SolidObject( hit->m_nSolid );
			if ( std::ranges::find( objects, nObject ) != objects.end() )
				return nObject;
		}
	if ( const auto pick = EdgeAt( view, at ) )
		return pick->m_nObject;
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: Alt+click: the loop (or ring) through the nearest edge, in the current kind.
//-----------------------------------------------------------------------------
bool CMapBrushEdit::SelectLoopAt( CMapView &view, QPointF at, bool bRing, bool bAdd )
{
	const auto *pScene = m_Editor.GetDocument().GetScene();
	const int nMode = GetMode();
	if ( !pScene || nMode < 1 )
		return false;
	const auto pick = EdgeAt( view, at );
	if ( !pick )
		return false;
	const auto nKind = static_cast<BrushPartKind_t>( nMode - 1 );
	const auto objects = SelectedSolidObjects();
	auto parts = SelectLoop( *pScene, objects, pick->m_nObject, pick->m_nEdge, pick->m_nFace, bRing ? BrushLoop_t::BRUSH_LOOP_RING : BrushLoop_t::BRUSH_LOOP_LOOP, nKind );
	if ( parts.empty() )
		return false;
	const auto pMeshParts = MeshOf( *pScene, pScene->GetIndex( pick->m_nObject ) );
	if ( nKind == BrushPartKind_t::BRUSH_PART_EDGE )
		m_Active = BrushPart_t{ pick->m_nObject, nKind, pick->m_nEdge };
	else if ( nKind == BrushPartKind_t::BRUSH_PART_FACE && pick->m_nFace )
		m_Active = BrushPart_t{ pick->m_nObject, nKind, *pick->m_nFace };
	else
		m_Active = BrushPart_t{ pick->m_nObject, nKind, pMeshParts->m_Edges[ pick->m_nEdge ].first };
	const std::size_t nCount = parts.size();
	SetParts( bAdd ? United( m_Parts, parts ) : std::move( parts ) );
	emit m_Editor.StatusMessage( ( bRing ? tr( "Ring: %n part(s)", nullptr, static_cast<int>( nCount ) ) : tr( "Loop: %n part(s)", nullptr, static_cast<int>( nCount ) ) ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: The panel's Loop and Ring: through every selected edge.
//-----------------------------------------------------------------------------
void CMapBrushEdit::SelectLoops( bool bRing )
{
	const auto *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene )
		return;
	if ( GetMode() != 2 )
	{
		emit m_Editor.StatusMessage( tr( "Select edges first (or Alt+click an edge in any mode)." ) );
		return;
	}
	const auto objects = SelectedSolidObjects();
	std::vector<BrushPart_t> parts = m_Parts;
	for ( const auto &part : m_Parts )
		if ( part.m_nKind == BrushPartKind_t::BRUSH_PART_EDGE )
			parts = United( std::move( parts ), SelectLoop( *pScene, objects, part.m_nObject, part.m_nIndex, std::nullopt, bRing ? BrushLoop_t::BRUSH_LOOP_RING : BrushLoop_t::BRUSH_LOOP_LOOP, BrushPartKind_t::BRUSH_PART_EDGE ) );
	SetParts( std::move( parts ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapBrushEdit::GrowSelection()
{
	if ( const auto *pScene = m_Editor.GetDocument().GetScene() )
		SetParts( GrowParts( *pScene, m_Parts ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapBrushEdit::ShrinkSelection()
{
	if ( const auto *pScene = m_Editor.GetDocument().GetScene() )
		SetParts( ShrinkParts( *pScene, m_Parts ) );
}

//-----------------------------------------------------------------------------
// Purpose: L (under the pointer), Shift+L (taken away) and Ctrl+L (from the selection).
//-----------------------------------------------------------------------------
void CMapBrushEdit::SelectLinked( std::optional<std::uint32_t> nObject, bool bRemove )
{
	const auto *pScene = m_Editor.GetDocument().GetScene();
	const int nMode = GetMode();
	if ( !pScene || nMode < 1 )
		return;
	std::vector<std::uint32_t> objects;
	if ( nObject )
		objects.push_back( *nObject );
	else
		for ( const auto &part : m_Parts )
			objects.push_back( part.m_nObject );
	const auto linked = LinkedParts( *pScene, objects, static_cast<BrushPartKind_t>( nMode - 1 ) );
	if ( !bRemove )
	{
		SetParts( United( m_Parts, linked ) );
		return;
	}
	auto parts = m_Parts;
	std::erase_if( parts, [ & ]( const BrushPart_t &part ) { return std::ranges::binary_search( linked, part ); } );
	SetParts( std::move( parts ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapBrushEdit::SelectSimilar( BrushSimilar_t nSimilar )
{
	const auto *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene || m_Parts.empty() )
		return;
	auto parts = SimilarParts( *pScene, SelectedSolidObjects(), m_Parts, nSimilar );
	const auto nAdded = static_cast<int>( parts.size() - m_Parts.size() );
	SetParts( std::move( parts ) );
	emit m_Editor.StatusMessage( tr( "%n more part(s) selected", nullptr, nAdded ) );
}

//-----------------------------------------------------------------------------
// Purpose: The selected faces' materials, everywhere shown in the map: their brushes join the
//          object selection so their faces can be picked.
//-----------------------------------------------------------------------------
void CMapBrushEdit::SelectByMaterial()
{
	auto &document = m_Editor.GetDocument();
	const auto *pScene = document.GetScene();
	if ( !pScene )
		return;
	const auto materials = MaterialsOf( *pScene, m_Parts );
	if ( materials.empty() )
	{
		emit m_Editor.StatusMessage( tr( "Select a face to choose its material." ) );
		return;
	}
	std::vector<std::uint32_t> objects;
	auto faces = FacesWithMaterials( *pScene, materials, &objects );
	std::erase_if( faces, [ & ]( const BrushPart_t &part ) { return !document.IsShown( part.m_nObject ); } );
	std::erase_if( objects, [ & ]( std::uint32_t nObject ) { return !document.IsShown( nObject ); } );
	auto selection = document.GetSelection();
	selection.insert( selection.end(), objects.begin(), objects.end() );
	std::ranges::sort( selection );
	selection.erase( std::ranges::unique( selection ).begin(), selection.end() );
	if ( selection != document.GetSelection() )
	{
		// The new brushes keep the faces picked so far.
		const bool bWasApplying = m_bApplying;
		m_bApplying = true;
		document.SetSelection( std::move( selection ) );
		m_bApplying = bWasApplying;
	}
	SetParts( United( m_Parts, faces ) );
	emit m_Editor.StatusMessage( tr( "%n face(s) on %1 brush(es)", nullptr, static_cast<int>( faces.size() ) ).arg( objects.size() ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapBrushEdit::SetProportional( bool bOn )
{
	m_pProportional->setChecked( bOn );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapBrushEdit::IsProportional() const
{
	return m_pProportional->isChecked();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapBrushEdit::SetProportionalRadius( double flRadius )
{
	m_pRadius->setValue( flRadius );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double CMapBrushEdit::GetProportionalRadius() const
{
	return m_pRadius->value();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapBrushEdit::SetFalloff( BrushFalloff_t nFalloff )
{
	m_pFalloff->setCurrentIndex( static_cast<int>( nFalloff ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapBrushEdit::SetPivot( BrushPivot_t nPivot )
{
	m_pPivot->setCurrentIndex( static_cast<int>( nPivot ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
BrushPivot_t CMapBrushEdit::GetPivot() const
{
	return static_cast<BrushPivot_t>( m_pPivot->currentIndex() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapBrushEdit::SetCursor( Point_t cursor )
{
	m_Cursor = cursor;
	m_bCursorPlaced = true;
	RefreshCursorLabel();
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose: Show where the cursor is in the panel.
//-----------------------------------------------------------------------------
void CMapBrushEdit::RefreshCursorLabel()
{
	if ( m_pCursorLabel )
		m_pCursorLabel->setText( QStringLiteral( "%1 %2 %3" ).arg( m_Cursor[ 0 ], 0, 'g', 8 ).arg( m_Cursor[ 1 ], 0, 'g', 8 ).arg( m_Cursor[ 2 ], 0, 'g', 8 ) );
}

//-----------------------------------------------------------------------------
// Purpose: The selection's middle, the active part's centre, or the cursor.
//-----------------------------------------------------------------------------
std::optional<Point_t> CMapBrushEdit::GetPivotPoint() const
{
	const auto *pScene = m_pBase ? m_pBase.get() : m_Editor.GetDocument().GetScene();
	if ( !pScene || m_Parts.empty() )
		return std::nullopt;
	if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_BRUSH_EDIT )
	{
		if ( GetPivot() == BrushPivot_t::BRUSH_PIVOT_CURSOR )
			return m_Cursor;
		if ( GetPivot() == BrushPivot_t::BRUSH_PIVOT_ACTIVE && m_Active && std::ranges::find( m_Parts, *m_Active ) != m_Parts.end() && pScene->IsSolidObject( m_Active->m_nObject ) )
			return PartPosition( *MeshOf( *pScene, pScene->GetIndex( m_Active->m_nObject ) ), m_Active->m_nKind, m_Active->m_nIndex );
	}
	Box_t bounds;
	for ( const auto &part : m_Parts )
	{
		if ( !pScene->IsSolidObject( part.m_nObject ) )
			continue;
		const auto pMeshParts = MeshOf( *pScene, pScene->GetIndex( part.m_nObject ) );
		const CBrushMesh &mesh = pMeshParts->m_Mesh;
		for ( const auto n : CornersOf( *pMeshParts, part.m_nKind, part.m_nIndex ) )
			bounds.Add( mesh.m_Vertices[ n ] );
	}
	return bounds.IsEmpty() ? std::nullopt : std::optional{ bounds.GetCenter() };
}

//-----------------------------------------------------------------------------
// Purpose: The legacy Vertex tool moves by the transform alone, as Hammer.
//-----------------------------------------------------------------------------
std::vector<BrushCornerMove_t> CMapBrushEdit::MovesFor( const CScene &scene, const Transform_t &transform ) const
{
	BrushMoveOptions_t options;
	std::vector<std::uint32_t> nearby;
	if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_BRUSH_EDIT )
	{
		options.m_bIndividual = GetPivot() == BrushPivot_t::BRUSH_PIVOT_INDIVIDUAL;
		if ( IsProportional() )
		{
			for ( const auto nSolid : ObjectsOf( scene, m_Editor.GetDocument().GetSelection() ).m_Solids )
				nearby.push_back( scene.SolidObject( nSolid ) );
			options.m_flRadius = GetProportionalRadius();
			options.m_nFalloff = static_cast<BrushFalloff_t>( m_pFalloff->currentIndex() );
			options.m_Nearby = nearby;
		}
	}
	return PartMoves( scene, m_Parts, transform, options );
}

//-----------------------------------------------------------------------------
// Purpose: Texture lock follows the transform except where each part turns about its own centre.
//-----------------------------------------------------------------------------
MapEdit_t CMapBrushEdit::MoveEdit( const CScene &scene, const Transform_t &transform, std::string *pProblem ) const
{
	const bool bIndividual = m_Editor.GetTool() == MapTool_t::MAP_TOOL_BRUSH_EDIT && GetPivot() == BrushPivot_t::BRUSH_PIVOT_INDIVIDUAL;
	return MoveCorners( scene, MovesFor( scene, transform ), bIndividual ? nullptr : &transform, GetConcave(), nullptr, pProblem );
}

//-----------------------------------------------------------------------------
// Purpose: One undo step; the parts are found again by where their corners went (faces by
//          their side), on their own brush or the pieces a split made of it.
//-----------------------------------------------------------------------------
void CMapBrushEdit::CommitMoves( const std::vector<BrushCornerMove_t> &moves, const Transform_t *pLock, const QString &szName )
{
	const auto *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene || moves.empty() )
		return;
	std::map<std::pair<std::uint32_t, std::uint32_t>, Point_t> placed;
	for ( const auto &move : moves )
		placed.try_emplace( { move.m_nObject, move.m_nCorner }, move.m_To );
	struct Remembered_t
	{
		std::uint32_t m_nSolid;
		BrushPartKind_t m_nKind;
		int m_nSide = 0;
		std::vector<Point_t> m_Points;
	};
	std::vector<Remembered_t> remembered;
	for ( const auto &part : m_Parts )
	{
		if ( !pScene->IsSolidObject( part.m_nObject ) )
			continue;
		const auto nSolid = pScene->GetIndex( part.m_nObject );
		const auto pMeshParts = MeshOf( *pScene, nSolid );
		const CBrushMesh &mesh = pMeshParts->m_Mesh;
		Remembered_t item{ nSolid, part.m_nKind, 0, {} };
		if ( part.m_nKind == BrushPartKind_t::BRUSH_PART_FACE && part.m_nIndex < mesh.m_Faces.size() && mesh.m_Faces[ part.m_nIndex ].m_nSide >= 0 )
			item.m_nSide = pScene->m_Map.m_Solids[ nSolid ].m_Sides[ mesh.m_Faces[ part.m_nIndex ].m_nSide ].m_nId;
		for ( const auto n : CornersOf( *pMeshParts, part.m_nKind, part.m_nIndex ) )
		{
			const auto it = placed.find( { part.m_nObject, n } );
			item.m_Points.push_back( it != placed.end() ? it->second : mesh.m_Vertices[ n ] );
		}
		remembered.push_back( std::move( item ) );
	}
	std::string szProblem;
	auto edit = MoveCorners( *pScene, moves, pLock, GetConcave(), nullptr, &szProblem );
	if ( !szProblem.empty() )
	{
		emit m_Editor.StatusMessage( QString::fromStdString( szProblem ) );
		return;
	}
	const std::size_t nOldSolids = pScene->m_Solids.size();
	std::vector<std::uint32_t> solids;
	for ( const auto &[ nSolid, solid ] : edit.m_Solids )
		solids.push_back( nSolid );
	const auto active = m_Active;
	Commit( std::move( edit ), szName );
	pScene = m_Editor.GetDocument().GetScene();
	std::vector<BrushPart_t> parts;
	for ( const auto nSolid : solids )
	{
		if ( nSolid >= pScene->m_Solids.size() )
			continue;
		const auto pMeshParts = MeshOf( *pScene, nSolid );
		const CBrushMesh &mesh = pMeshParts->m_Mesh;
		for ( const auto &item : remembered )
		{
			if ( item.m_nSolid != nSolid && nSolid < nOldSolids )
				continue;
			const auto nCount = item.m_nKind == BrushPartKind_t::BRUSH_PART_VERTEX ? mesh.m_Vertices.size() : item.m_nKind == BrushPartKind_t::BRUSH_PART_EDGE ? pMeshParts->m_Edges.size()
																																							   : mesh.m_Faces.size();
			for ( std::uint32_t i = 0; i < nCount; i++ )
			{
				bool bMatch = false;
				if ( item.m_nKind == BrushPartKind_t::BRUSH_PART_FACE )
					bMatch = mesh.m_Faces[ i ].m_nSide >= 0 && pScene->m_Map.m_Solids[ nSolid ].m_Sides[ mesh.m_Faces[ i ].m_nSide ].m_nId == item.m_nSide;
				else
				{
					const auto corners = CornersOf( *pMeshParts, item.m_nKind, i );
					bMatch = corners.size() == item.m_Points.size() && std::ranges::all_of( corners, [ & ]( std::uint32_t n ) { return std::ranges::any_of( item.m_Points, [ & ]( const Point_t &p ) { return Length( Subtract( p, mesh.m_Vertices[ n ] ) ) < 0.001; } ); } );
				}
				if ( bMatch )
					parts.push_back( { pScene->SolidObject( nSolid ), item.m_nKind, i } );
			}
		}
	}
	SetParts( std::move( parts ) );
	// The active part keeps its place in the list when its index is unchanged.
	if ( active && std::ranges::find( m_Parts, *active ) != m_Parts.end() )
		m_Active = active;
}

//-----------------------------------------------------------------------------
// Purpose: The drag's world offset on the view's plane through the pivot.
//-----------------------------------------------------------------------------
void CMapBrushEdit::PreviewSlide( CMapView &view, QPointF at )
{
	if ( !m_bSlide || !m_pBase )
		return;
	Point_t drag{};
	if ( auto *p2D = qobject_cast<CMapView2D *>( &view ) )
	{
		const auto d = p2D->ToMap( at ) - p2D->ToMap( m_SlideFrom );
		drag[ p2D->GetAxisIndex( 0 ) ] = d.x();
		drag[ p2D->GetAxisIndex( 1 ) ] = d.y();
	}
	else
	{
		const auto camera = Camera( view );
		const Point_t normal = camera.GetForward();
		const auto onPlane = [ & ]( QPointF point ) {
			const auto [ from, dir ] = camera.Ray( point.x(), point.y() );
			const double d = Dot( dir, normal );
			return std::abs( d ) < 1e-8 ? m_Anchor : Add( from, Scale( dir, Dot( Subtract( m_Anchor, from ), normal ) / d ) );
		};
		drag = Subtract( onPlane( at ), onPlane( m_SlideFrom ) );
	}
	m_SlideMoves = SlideMoves( *m_pBase, m_Parts, drag );
	std::string szProblem;
	const auto edit = MoveCorners( *m_pBase, m_SlideMoves, nullptr, GetConcave(), nullptr, &szProblem );
	if ( szProblem.empty() )
	{
		m_bApplying = true;
		m_Editor.GetDocument().Preview( edit );
		m_bApplying = false;
		emit m_Editor.StatusMessage( tr( "Slide: click or Enter to apply, Escape to cancel." ) );
	}
	else
		emit m_Editor.StatusMessage( QString::fromStdString( szProblem ) );
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose: G during a move: from the move's start, the parts slide instead.
//-----------------------------------------------------------------------------
void CMapBrushEdit::BeginSlide( CMapView &view, QPointF at )
{
	if ( std::ranges::all_of( m_Parts, []( const BrushPart_t &part ) { return part.m_nKind == BrushPartKind_t::BRUSH_PART_FACE; } ) )
	{
		emit m_Editor.StatusMessage( tr( "Vertices and edges slide; faces don't." ) );
		return;
	}
	if ( !m_pBase )
		m_pBase = m_Editor.GetDocument().GetSharedScene();
	m_Modal.reset();
	m_bSlide = true;
	m_SlideFrom = at;
	m_Anchor = GetPivotPoint().value_or( Point_t{} );
	m_pOperationView = &view;
	PreviewSlide( view, at );
}

//-----------------------------------------------------------------------------
// Purpose: Shift+right click: on the surface under the pointer in 3D (else on the view's plane
//          through the cursor), at the pointer in 2D (snapped unless bFree), the depth kept.
//-----------------------------------------------------------------------------
void CMapBrushEdit::PlaceCursor( CMapView &view, QPointF at, bool bFree )
{
	Point_t cursor = m_Cursor;
	if ( auto *p2D = qobject_cast<CMapView2D *>( &view ) )
	{
		QPointF point = p2D->ToMap( at );
		const double flGrid = m_Editor.GetSettings().m_nGridSize;
		if ( m_Editor.GetSettings().m_bSnapToGrid && !bFree && flGrid > 0 )
			point = QPointF( std::round( point.x() / flGrid ) * flGrid, std::round( point.y() / flGrid ) * flGrid );
		cursor[ p2D->GetAxisIndex( 0 ) ] = point.x();
		cursor[ p2D->GetAxisIndex( 1 ) ] = point.y();
	}
	else if ( auto *p3D = qobject_cast<CMapView3D *>( &view ) )
	{
		const auto camera = p3D->GetSizedCamera();
		const auto [ from, dir ] = camera.Ray( at.x(), at.y() );
		std::optional<Plane_t> plane;
		if ( const auto hit = p3D->FaceAt( at ) )
			if ( const auto *pScene = m_Editor.GetDocument().GetScene(); pScene && hit->m_nSolid < pScene->m_Solids.size() )
				if ( const auto *pFace = pScene->m_Solids[ hit->m_nSolid ].FindFace( hit->m_nSide ) )
					plane = pFace->m_Plane;
		if ( !plane )
			plane = Plane_t{ camera.GetForward(), Dot( camera.GetForward(), m_Cursor ) };
		const double d = Dot( plane->m_Normal, dir );
		if ( std::abs( d ) < 1e-9 )
			return;
		cursor = Add( from, Scale( dir, -plane->Distance( from ) / d ) );
	}
	SetCursor( cursor );
	emit m_Editor.StatusMessage( tr( "3D cursor at %1" ).arg( m_pCursorLabel ? m_pCursorLabel->text() : QString{} ) );
}

//-----------------------------------------------------------------------------
// Purpose: The 3D cursor (once placed, or when it is the pivot) and proportional editing's
//          radius round the pivot while something moves.
//-----------------------------------------------------------------------------
void CMapBrushEdit::PaintSelectionExtras( CMapView &view, QPainter &painter ) const
{
	if ( m_Editor.GetTool() != MapTool_t::MAP_TOOL_BRUSH_EDIT )
		return;
	painter.save();
	painter.setRenderHint( QPainter::Antialiasing );
	if ( m_bCursorPlaced || GetPivot() == BrushPivot_t::BRUSH_PIVOT_CURSOR )
		if ( const auto at = Project( view, m_Cursor ) )
		{
			painter.setBrush( Qt::NoBrush );
			painter.setPen( QPen( Qt::white, 2 ) );
			painter.drawEllipse( *at, 7, 7 );
			QPen dashed( QColor( 230, 40, 40 ), 2 );
			dashed.setDashPattern( { 2, 2 } );
			painter.setPen( dashed );
			painter.drawEllipse( *at, 7, 7 );
			painter.setPen( QPen( Qt::black, 1 ) );
			painter.drawLine( *at - QPointF( 12, 0 ), *at - QPointF( 4, 0 ) );
			painter.drawLine( *at + QPointF( 4, 0 ), *at + QPointF( 12, 0 ) );
			painter.drawLine( *at - QPointF( 0, 12 ), *at - QPointF( 0, 4 ) );
			painter.drawLine( *at + QPointF( 0, 4 ), *at + QPointF( 0, 12 ) );
		}
	const bool bMoving = m_Modal || ( m_pBase && m_From && !m_bBox && m_nOperation < 0 );
	if ( IsProportional() && bMoving && !m_bSlide )
		if ( const auto pivot = GetPivotPoint() )
			if ( const auto centre = Project( view, *pivot ) )
			{
				double flPixels = 0;
				if ( auto *p2D = qobject_cast<CMapView2D *>( &view ) )
					flPixels = GetProportionalRadius() * p2D->GetZoom();
				else if ( const auto edge = Project( view, Add( *pivot, Scale( Camera( view ).GetRight(), GetProportionalRadius() ) ) ) )
					flPixels = std::hypot( edge->x() - centre->x(), edge->y() - centre->y() );
				QPen pen( QColor( 255, 255, 255, 200 ), 1 );
				pen.setDashPattern( { 4, 3 } );
				painter.setPen( pen );
				painter.setBrush( Qt::NoBrush );
				painter.drawEllipse( *centre, flPixels, flPixels );
			}
	painter.restore();
}

//-----------------------------------------------------------------------------
// Purpose: Shift+G: what to compare the selection by, for its kind.
//-----------------------------------------------------------------------------
void CMapBrushEdit::ShowSimilarMenu( const QPoint &globalAt )
{
	QMenu menu( this );
	menu.setObjectName( QStringLiteral( "brushSimilarMenu" ) );
	if ( GetMode() == 3 )
	{
		menu.addAction( tr( "Facing the Same Way" ), this, [ this ] { SelectSimilar( BrushSimilar_t::BRUSH_SIMILAR_NORMAL ); } );
		menu.addAction( tr( "On the Same Plane" ), this, [ this ] { SelectSimilar( BrushSimilar_t::BRUSH_SIMILAR_COPLANAR ); } );
		menu.addAction( tr( "Same Material" ), this, [ this ] { SelectSimilar( BrushSimilar_t::BRUSH_SIMILAR_MATERIAL ); } );
		menu.addAction( tr( "Same Area" ), this, [ this ] { SelectSimilar( BrushSimilar_t::BRUSH_SIMILAR_SIZE ); } );
	}
	else if ( GetMode() == 2 )
	{
		menu.addAction( tr( "Same Direction" ), this, [ this ] { SelectSimilar( BrushSimilar_t::BRUSH_SIMILAR_NORMAL ); } );
		menu.addAction( tr( "Same Length" ), this, [ this ] { SelectSimilar( BrushSimilar_t::BRUSH_SIMILAR_SIZE ); } );
	}
	else
		menu.addAction( tr( "Select edges or faces to compare" ) )->setEnabled( false );
	menu.exec( globalAt );
}

//-----------------------------------------------------------------------------
// Purpose: The panel's Select menu, for the current kind (its keys beside each).
//-----------------------------------------------------------------------------
void CMapBrushEdit::FillSelectMenu( QMenu &menu )
{
	const int nMode = GetMode();
	const bool bParts = !m_Parts.empty();
	const auto add = [ & ]( const QString &szText, const QString &szKeys, auto pfn, bool bEnabled ) {
		QAction *pAction = menu.addAction( szKeys.isEmpty() ? szText : szText + u'\t' + szKeys, this, pfn );
		pAction->setEnabled( bEnabled );
		return pAction;
	};
	add( tr( "Edge Loop" ), tr( "Alt+click" ), [ this ] { SelectLoops( false ); }, nMode == 2 && bParts );
	add( tr( "Edge Ring" ), tr( "Ctrl+Alt+click" ), [ this ] { SelectLoops( true ); }, nMode == 2 && bParts );
	menu.addSeparator();
	add( tr( "Grow" ), tr( "Ctrl+Num +" ), [ this ] { GrowSelection(); }, bParts );
	add( tr( "Shrink" ), tr( "Ctrl+Num -" ), [ this ] { ShrinkSelection(); }, bParts );
	add( tr( "Linked" ), tr( "Ctrl+L" ), [ this ] { SelectLinked(); }, bParts );
	menu.addSeparator();
	QMenu *pSimilar = menu.addMenu( tr( "Similar" ) );
	pSimilar->setEnabled( bParts && nMode >= 2 );
	if ( nMode == 3 )
	{
		pSimilar->addAction( tr( "Facing the Same Way" ), this, [ this ] { SelectSimilar( BrushSimilar_t::BRUSH_SIMILAR_NORMAL ); } );
		pSimilar->addAction( tr( "On the Same Plane" ), this, [ this ] { SelectSimilar( BrushSimilar_t::BRUSH_SIMILAR_COPLANAR ); } );
		pSimilar->addAction( tr( "Same Material" ), this, [ this ] { SelectSimilar( BrushSimilar_t::BRUSH_SIMILAR_MATERIAL ); } );
		pSimilar->addAction( tr( "Same Area" ), this, [ this ] { SelectSimilar( BrushSimilar_t::BRUSH_SIMILAR_SIZE ); } );
	}
	else
	{
		pSimilar->addAction( tr( "Same Direction" ), this, [ this ] { SelectSimilar( BrushSimilar_t::BRUSH_SIMILAR_NORMAL ); } );
		pSimilar->addAction( tr( "Same Length" ), this, [ this ] { SelectSimilar( BrushSimilar_t::BRUSH_SIMILAR_SIZE ); } );
	}
	add( tr( "All Faces in These Materials" ), {}, [ this ] { SelectByMaterial(); }, nMode == 3 && bParts );
}
