//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapBrushEdit.h.
//===========================================================================//
#include "MapBrushEdit.h"
#include "map/MapSplineTool.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <unordered_map>
#include <QCheckBox>
#include <QCursor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QUndoStack>
#include <QWheelEvent>
#include "Preferences.h"
#include "map/MapEditor.h"
#include "map/MapConstants.h"
#include "map/MapView2D.h"
#include "map/MapView3D.h"
#include "map/Clip.h"
#include "map/BrushOperations.h"

using namespace xv::map;

//-----------------------------------------------------------------------------
// Purpose: Keep mesh tools beside their selection and conversion settings.
//-----------------------------------------------------------------------------
CMapBrushEdit::CMapBrushEdit( CMapEditor &editor )
	: QWidget( &editor ),
	  m_Editor( editor )
{
	setObjectName( QStringLiteral( "mapBrushEdit" ) );
	auto *pLayout = new QFormLayout( this );
	m_pMode = new QComboBox( this );
	m_pMode->setObjectName( QStringLiteral( "brushPartMode" ) );
	m_pMode->addItems( { tr( "Object" ), tr( "Vertex" ), tr( "Edge" ), tr( "Face" ) } );
	m_pMode->setCurrentIndex( 1 );
	pLayout->addRow( tr( "Selection" ), m_pMode );
	pLayout->setRowVisible( m_pMode, false );
	m_pXray = new QCheckBox( tr( "X-ray in 3D" ), this );
	pLayout->addRow( m_pXray );
	m_pConcave = new QComboBox( this );
	m_pConcave->addItems( { tr( "Split into convex brushes" ), tr( "Keep it as it is" ) } );
	m_pConcave->setCurrentIndex( CPreferences::Instance().GetMapPreferences().m_bBrushKeepConcave ? 1 : 0 );
	pLayout->addRow( tr( "Concave shapes" ), m_pConcave );
	m_pDistance = new QDoubleSpinBox( this );
	m_pDistance->setObjectName( QStringLiteral( "brushOperationAmount" ) );
	m_pDistance->setRange( -16384, 16384 );
	m_pDistance->setDecimals( 3 );
	m_pDistance->setValue( 16 );
	m_pDistance->setEnabled( false );
	pLayout->addRow( tr( "Last distance" ), m_pDistance );
	connect( m_pDistance, &QDoubleSpinBox::valueChanged, this, [ this ] { ModelParametersChanged(); } );
	connect( m_pDistance, &QDoubleSpinBox::editingFinished, this, [ this ] {if(m_bAdjustingLast)AdjustLastOperation(true); } );
	CreateModelControls( *pLayout );
	// Selection extras: loops, rings, grow and shrink, linked, similar, by material.
	auto *pSelect = new QPushButton( tr( "Select" ), this );
	pSelect->setObjectName( QStringLiteral( "brushSelectMenu" ) );
	auto *pSelectMenu = new QMenu( pSelect );
	connect( pSelectMenu, &QMenu::aboutToShow, this, [ this, pSelectMenu ] {
		pSelectMenu->clear();
		FillSelectMenu( *pSelectMenu );
	} );
	pSelect->setMenu( pSelectMenu );
	pLayout->addRow( tr( "Select" ), pSelect );
	m_pPivot = new QComboBox( this );
	m_pPivot->setObjectName( QStringLiteral( "brushPivot" ) );
	m_pPivot->addItems( { tr( "Selection centre" ), tr( "Active part" ), tr( "Each part's centre" ), tr( "3D cursor" ) } );
	m_pPivot->setToolTip( tr( "What the selected parts turn and scale about. The active part is the one picked last; Shift+right click places the 3D cursor." ) );
	connect( m_pPivot, &QComboBox::currentIndexChanged, this, [ this ] { Refresh(); } );
	pLayout->addRow( tr( "Pivot" ), m_pPivot );
	auto *pCursorRow = new QWidget( this );
	auto *pCursorLayout = new QHBoxLayout( pCursorRow );
	pCursorLayout->setContentsMargins( 0, 0, 0, 0 );
	m_pCursorLabel = new QLabel( pCursorRow );
	m_pCursorLabel->setObjectName( QStringLiteral( "brushCursor" ) );
	pCursorLayout->addWidget( m_pCursorLabel, 1 );
	auto *pToSelection = new QPushButton( tr( "To Selection" ), pCursorRow );
	pToSelection->setToolTip( tr( "Put the 3D cursor at the selection's centre" ) );
	connect( pToSelection, &QPushButton::clicked, this, [ this ] {
		const auto pivot = m_pPivot->currentIndex() == static_cast<int>( BrushPivot_t::BRUSH_PIVOT_CURSOR ) ? std::optional<Point_t>{} : GetPivotPoint();
		if ( pivot )
			SetCursor( *pivot );
		else if ( !m_Parts.empty() )
		{
			// With the cursor as the pivot, the selection's own centre.
			const QSignalBlocker blocker( m_pPivot );
			m_pPivot->setCurrentIndex( 0 );
			const auto centre = GetPivotPoint();
			m_pPivot->setCurrentIndex( static_cast<int>( BrushPivot_t::BRUSH_PIVOT_CURSOR ) );
			if ( centre )
				SetCursor( *centre );
		}
	} );
	pCursorLayout->addWidget( pToSelection );
	auto *pResetCursor = new QPushButton( tr( "Reset" ), pCursorRow );
	pResetCursor->setToolTip( tr( "Put the 3D cursor back at the map's origin" ) );
	connect( pResetCursor, &QPushButton::clicked, this, [ this ] { SetCursor( {} ); } );
	pCursorLayout->addWidget( pResetCursor );
	pLayout->addRow( tr( "3D cursor" ), pCursorRow );
	RefreshCursorLabel();
	// Proportional editing: nearby corners follow a move by a falloff.
	m_pProportional = new QCheckBox( tr( "Proportional editing (O)" ), this );
	m_pProportional->setObjectName( QStringLiteral( "brushProportional" ) );
	m_pProportional->setToolTip( tr( "Corners of the selected brushes near the moved parts follow them, less the farther they are. The wheel or Page Up/Down changes the radius during a move." ) );
	pLayout->addRow( m_pProportional );
	m_pRadius = new QDoubleSpinBox( this );
	m_pRadius->setObjectName( QStringLiteral( "brushProportionalRadius" ) );
	m_pRadius->setRange( 1, 65536 );
	m_pRadius->setDecimals( 1 );
	m_pRadius->setValue( 128 );
	m_pRadius->setSuffix( tr( " units" ) );
	pLayout->addRow( tr( "Radius" ), m_pRadius );
	m_pFalloff = new QComboBox( this );
	m_pFalloff->setObjectName( QStringLiteral( "brushFalloff" ) );
	m_pFalloff->addItems( { tr( "Smooth" ), tr( "Sphere" ), tr( "Root" ), tr( "Sharp" ), tr( "Linear" ), tr( "Constant" ) } );
	pLayout->addRow( tr( "Falloff" ), m_pFalloff );
	const auto showProportional = [ this, pLayout ] {
		pLayout->setRowVisible( m_pRadius, m_pProportional->isChecked() );
		pLayout->setRowVisible( m_pFalloff, m_pProportional->isChecked() );
		Refresh();
	};
	connect( m_pProportional, &QCheckBox::toggled, this, showProportional );
	connect( m_pRadius, &QDoubleSpinBox::valueChanged, this, [ this ] { Refresh(); } );
	showProportional();
	connect( m_pMode, &QComboBox::currentIndexChanged, this, [ this ] {
		const int nOperation = m_nOperation;
		Cancel();
		SetParts( {} );
		if ( ( nOperation == 6 && ( GetMode() == 1 || GetMode() == 2 ) ) || ( ( nOperation == 1 || nOperation == 5 ) && GetMode() == 3 ) )
			m_nOperation = nOperation;
		RefreshModelControls();
		emit ToolChanged();
	} );
	connect( m_pXray, &QCheckBox::toggled, this, [ this ] { Refresh(); } );
	connect( m_pConcave, &QComboBox::currentIndexChanged, this, []( int n ) {
		auto preferences = CPreferences::Instance().GetMapPreferences();
		preferences.m_bBrushKeepConcave = n == 1;
		CPreferences::Instance().SetMapPreferences( preferences );
	} );
	connect( &CPreferences::Instance(), &CPreferences::MapPreferencesChanged, this, [ this ] {
		const QSignalBlocker blocker( m_pConcave );
		m_pConcave->setCurrentIndex( CPreferences::Instance().GetMapPreferences().m_bBrushKeepConcave ? 1 : 0 );
	} );
	connect( &editor.GetDocument(), &CMapDocument::Saved, this, [ this ] {
		if ( m_bAdjustingLast && m_pLastOperationCommand && m_Editor.GetDocument().GetUndoStack()->index() == m_nLastUndoIndex )
			m_pLastScene = m_Editor.GetDocument().GetSharedScene();
	} );
	// Parts on brushes no longer selected go (and what was going on with them); the document
	// says the selection changed after every edit, which leaves the others as they are.
	connect( &editor.GetDocument(), &CMapDocument::SelectionChanged, this, [ this ] {
		if ( m_bApplying )
			return;
		const auto &selection = m_Editor.GetDocument().GetSelection();
		std::vector<BrushPart_t> kept = m_Parts;
		std::erase_if( kept, [ & ]( const BrushPart_t &part ) { return !std::ranges::binary_search( selection, part.m_nObject ); } );
		if ( kept.size() == m_Parts.size() && !kept.empty() )
			return;
		Cancel();
		SetParts( std::move( kept ) );
	} );
	connect( &editor.GetDocument(), &CMapDocument::SceneEdited, this, [ this ]( const std::vector<std::uint32_t> &changed ) {if(!m_bApplying && !m_pBase && m_Editor.GetTool()==MapTool_t::MAP_TOOL_BRUSH_EDIT) {m_pDistance->setEnabled(false); FindPartsAgain(changed);} } );
	connect( m_Editor.GetDocument().GetUndoStack(), &QUndoStack::indexChanged, this, [ this ]( int nIndex ) {
		if ( m_bApplying || m_pBase || !m_pLastOperationCommand )
			return;
		auto *pUndo = m_Editor.GetDocument().GetUndoStack();
		if ( pUndo->count() != m_nLastUndoIndex || m_nLastUndoIndex < 1 || pUndo->command( m_nLastUndoIndex - 1 ) != m_pLastOperationCommand )
		{
			m_pLastOperationBase.reset();
			m_pLastOperationCommand = nullptr;
			m_bAdjustingLast = false;
			RefreshModelControls();
			return;
		}
		if ( nIndex != m_nLastUndoIndex )
		{
			m_bAdjustingLast = false;
			m_pDistance->setEnabled( false );
			RefreshModelControls();
			return;
		}
		m_pLastScene = m_Editor.GetDocument().GetSharedScene();
		SetModelParameters( m_LastModelParams );
		m_bAdjustingLast = true;
		m_pDistance->setEnabled( true );
		if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_BRUSH_EDIT && !m_LastSelectedSides.empty() )
		{
			std::vector<BrushPart_t> parts;
			const auto *pScene = m_Editor.GetDocument().GetScene();
			for ( int nSide : m_LastSelectedSides )
				if ( auto face = pScene->FindSide( nSide ) )
				{
					const auto pMeshParts = MeshOf( *pScene, face->m_nSolid );
					const CBrushMesh &mesh = pMeshParts->m_Mesh;
					for ( std::uint32_t i = 0; i < mesh.m_Faces.size(); ++i )
						if ( mesh.m_Faces[ i ].m_nSide == int( face->m_nSide ) )
							parts.push_back( { pScene->SolidObject( face->m_nSolid ), BrushPartKind_t::BRUSH_PART_FACE, i } );
				}
			m_bApplying = true;
			SetParts( std::move( parts ) );
			m_bApplying = false;
		}
		RefreshModelControls();
	} );
}

//-----------------------------------------------------------------------------
// Purpose: Select an explicit kind without introducing another keyboard preset.
//-----------------------------------------------------------------------------
void CMapBrushEdit::SetMode( int nMode )
{
	const int nOperation = m_nOperation;
	Cancel();
	if ( ( nOperation == 6 && ( nMode == 1 || nMode == 2 ) ) || ( ( nOperation == 1 || nOperation == 5 ) && nMode == 3 ) )
		m_nOperation = nOperation;
	m_pMode->setCurrentIndex( std::clamp( nMode, 0, 3 ) );
	RefreshModelControls();
	emit ToolChanged();
}

//-----------------------------------------------------------------------------
// Purpose: Keep Face Edit and brush face picking on the same selection.
//-----------------------------------------------------------------------------
void CMapBrushEdit::SetParts( std::vector<BrushPart_t> parts )
{
	std::ranges::sort( parts );
	parts.erase( std::ranges::unique( parts ).begin(), parts.end() );
	if ( !m_bApplying )
		m_bAdjustingLast = false;
	m_Parts = std::move( parts );
	m_pPartsScene = m_Parts.empty() ? nullptr : m_Editor.GetDocument().GetSharedScene();
	if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_VERTEX )
	{
		std::vector<VertexRef_t> handles;
		for ( const auto &part : m_Parts )
			if ( part.m_nKind != BrushPartKind_t::BRUSH_PART_FACE )
				handles.push_back( { part.m_nObject, part.m_nKind == BrushPartKind_t::BRUSH_PART_EDGE, part.m_nIndex } );
		m_Editor.SetVertexHandles( std::move( handles ) );
	}
	std::vector<FaceRef_t> faces;
	const auto *pScene = m_Editor.GetDocument().GetScene();
	if ( pScene && !( m_nOperation == 6 && m_pBase && m_bOperationValid ) )
		for ( const auto &part : m_Parts )
			if ( part.m_nKind == BrushPartKind_t::BRUSH_PART_FACE && pScene->IsSolidObject( part.m_nObject ) )
			{
				const auto nSolid = pScene->GetIndex( part.m_nObject );
				if ( part.m_nIndex < pScene->m_Solids[ nSolid ].m_Faces.size() )
					faces.push_back( { nSolid, pScene->m_Solids[ nSolid ].m_Faces[ part.m_nIndex ].m_nSide } );
			}
	if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_BRUSH_EDIT )
		m_Editor.SetSelectedFaces( std::move( faces ) );
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose: Repaint overlays on both software and graphics-card views.
//-----------------------------------------------------------------------------
void CMapBrushEdit::Refresh()
{
	for ( CMapView *pView : m_Editor.GetViews() )
	{
		pView->update();
		for ( auto *pOverlay : pView->findChildren<CMapOverlay3D *>() )
			pOverlay->update();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Project the selected solids' parts, optionally excluding hidden handles.
//-----------------------------------------------------------------------------
std::vector<CMapBrushEdit::Handle_t> CMapBrushEdit::Handles( CMapView &view ) const
{
	std::vector<Handle_t> result;
	const auto *pScene = m_pBase && m_nOperation >= 0 ? m_pBase.get() : m_Editor.GetDocument().GetScene();
	if ( !pScene || ( m_pMode->currentIndex() == 0 && m_Editor.GetTool() != MapTool_t::MAP_TOOL_VERTEX ) )
		return result;
	auto *p2D = qobject_cast<CMapView2D *>( &view );
	auto *p3D = qobject_cast<CMapView3D *>( &view );
	std::vector<BrushPartKind_t> kinds;
	if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_VERTEX )
	{
		if ( m_Editor.GetVertexMode() != MapVertexMode_t::MAP_VERTEX_MODE_EDGES )
			kinds.push_back( BrushPartKind_t::BRUSH_PART_VERTEX );
		if ( m_Editor.GetVertexMode() != MapVertexMode_t::MAP_VERTEX_MODE_VERTICES )
			kinds.push_back( BrushPartKind_t::BRUSH_PART_EDGE );
	}
	else
		kinds.push_back( static_cast<BrushPartKind_t>( m_pMode->currentIndex() - 1 ) );
	const auto objects = ObjectsOf( *pScene, m_Editor.GetDocument().GetSelection() );
	const CViewCamera camera = p3D ? p3D->GetSizedCamera() : CViewCamera{};
	for ( const auto nKind : kinds )
		for ( const auto nSolid : objects.m_Solids )
		{
			const auto pMeshParts = MeshOf( *pScene, nSolid );
			const CBrushMesh &mesh = pMeshParts->m_Mesh;
			const auto nCount = nKind == BrushPartKind_t::BRUSH_PART_VERTEX ? mesh.m_Vertices.size() : nKind == BrushPartKind_t::BRUSH_PART_EDGE ? pMeshParts->m_Edges.size()
																																				 : mesh.m_Faces.size();
			// In 3D without x-ray: the parts only on faces turned away are behind the brush itself
			// (it is convex), with no ray needed to tell.
			std::vector<bool> front;
			if ( p3D && !m_pXray->isChecked() )
			{
				front.assign( nCount, false );
				for ( std::size_t f = 0; f < mesh.m_Faces.size(); f++ )
				{
					const auto plane = mesh.FacePlane( f );
					if ( plane && ( camera.IsOrthographic() ? Dot( plane->m_Normal, camera.GetForward() ) >= 0 : plane->Distance( camera.m_Eye ) <= 0 ) )
						continue;
					const auto &corners = mesh.m_Faces[ f ].m_Corners;
					if ( nKind == BrushPartKind_t::BRUSH_PART_FACE )
						front[ f ] = true;
					for ( std::size_t c = 0; c < corners.size(); c++ )
					{
						if ( nKind == BrushPartKind_t::BRUSH_PART_VERTEX )
							front[ corners[ c ] ] = true;
						else if ( nKind == BrushPartKind_t::BRUSH_PART_EDGE )
						{
							const auto a = corners[ c ], b = corners[ ( c + 1 ) % corners.size() ];
							const auto it = std::ranges::lower_bound( pMeshParts->m_Edges, std::pair{ std::min( a, b ), std::max( a, b ) } );
							if ( it != pMeshParts->m_Edges.end() && *it == std::pair{ std::min( a, b ), std::max( a, b ) } )
								front[ static_cast<std::size_t>( it - pMeshParts->m_Edges.begin() ) ] = true;
						}
					}
				}
			}
			for ( std::uint32_t i = 0; i < nCount; i++ )
			{
				if ( !front.empty() && !front[ i ] )
					continue;
				const Point_t point = PartPosition( *pMeshParts, nKind, i );
				QPointF at;
				if ( p2D )
					at = p2D->ToWidget( p2D->Flatten( point ) );
				else if ( p3D )
				{
					const auto screen = camera.Project( point );
					if ( !screen )
						continue;
					at = { ( *screen )[ 0 ], ( *screen )[ 1 ] };
				}
				result.push_back( { { pScene->SolidObject( nSolid ), nKind, i }, point, at } );
			}
		}
	// Hidden handles: another brush nearer than the handle along its ray.
	if ( p3D && !m_pXray->isChecked() && !result.empty() )
	{
		std::vector<Point_t> points;
		std::vector<std::uint32_t> owners;
		points.reserve( result.size() );
		owners.reserve( result.size() );
		for ( const Handle_t &handle : result )
		{
			points.push_back( handle.m_Position );
			owners.push_back( handle.m_Part.m_nObject );
		}
		const auto hidden = p3D->BrushesHide( points, owners );
		std::size_t nKept = 0;
		for ( std::size_t h = 0; h < result.size(); h++ )
			if ( !hidden[ h ] )
				result[ nKept++ ] = result[ h ];
		result.resize( nKept );
	}
	return result;
}

//-----------------------------------------------------------------------------
// Purpose: Place the part gizmo at the bounds of the selected corners.
//-----------------------------------------------------------------------------
std::optional<CMapGizmo> CMapBrushEdit::Gizmo() const
{
	const auto *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene || m_Parts.empty() )
		return std::nullopt;
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
	if ( bounds.IsEmpty() )
		return std::nullopt;
	CMapGizmo gizmo;
	gizmo.Place( GetPivotPoint().value_or( bounds.GetCenter() ), { { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } } );
	gizmo.SetBounds( bounds );
	gizmo.SetMode( m_Editor.GetGizmoMode() );
	return gizmo;
}

//-----------------------------------------------------------------------------
// Purpose: Draw the same part handles above either rendering backend.
//-----------------------------------------------------------------------------
void CMapBrushEdit::Paint( CMapView &view, QPainter &painter ) const
{
	m_Editor.GetSplineTool().Paint( view, painter );
	PaintConstruction( view, painter );
	PaintModel( view, painter );
	PaintModelHandle( view, painter );
	if ( m_Editor.GetTool() != MapTool_t::MAP_TOOL_BRUSH_EDIT && !( m_Editor.GetTool() == MapTool_t::MAP_TOOL_VERTEX && qobject_cast<CMapView3D *>( &view ) ) )
		return;
	painter.save();
	const auto *pScene = m_pBase && m_nOperation >= 0 ? m_pBase.get() : m_Editor.GetDocument().GetScene();
	if ( pScene )
		for ( const auto &part : m_Parts )
		{
			if ( part.m_nObject >= pScene->GetObjectCount() || !pScene->IsSolidObject( part.m_nObject ) )
				continue;
			const auto pMeshParts = MeshOf( *pScene, pScene->GetIndex( part.m_nObject ) );
			const CBrushMesh &mesh = pMeshParts->m_Mesh;
			if ( part.m_nKind == BrushPartKind_t::BRUSH_PART_VERTEX )
				continue;
			QPolygonF polygon;
			for ( const auto n : CornersOf( *pMeshParts, part.m_nKind, part.m_nIndex ) )
			{
				const auto point = mesh.m_Vertices[ n ];
				if ( auto *p2D = qobject_cast<CMapView2D *>( &view ) )
					polygon << p2D->ToWidget( p2D->Flatten( point ) );
				else if ( const auto projected = Camera( view ).Project( point ) )
					polygon << QPointF( ( *projected )[ 0 ], ( *projected )[ 1 ] );
			}
			painter.setPen( QPen( QColor( 255, 80, 70 ), 3 ) );
			painter.setBrush( QColor( 255, 80, 70, 60 ) );
			if ( part.m_nKind == BrushPartKind_t::BRUSH_PART_EDGE )
				painter.drawPolyline( polygon );
			else
				painter.drawPolygon( polygon );
		}

	if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_BRUSH_EDIT && m_nOperation < 0 && qobject_cast<CMapView3D *>( &view ) )
		if ( const auto gizmo = m_DragGizmo ? m_DragGizmo : Gizmo() )
			for ( const auto &line : gizmo->GetLines( Camera( view ), 90 ) )
				if ( const auto projected = Camera( view ).ProjectLine( line.m_From, line.m_To ) )
				{
					const QColor colors[]{ Qt::red, Qt::green, Qt::blue };
					painter.setPen( QPen( colors[ std::clamp( gizmo->GetPartAxis( line.m_nPart ), 0, 2 ) ], 2 ) );
					painter.drawLine( QPointF( projected->first[ 0 ], projected->first[ 1 ] ), QPointF( projected->second[ 0 ], projected->second[ 1 ] ) );
				}
	painter.setPen( Qt::black );
	if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_VERTEX || GetMode() == 1 )
		for ( const auto &handle : Handles( view ) )
		{
			painter.setBrush( std::ranges::find( m_Parts, handle.m_Part ) != m_Parts.end() ? QColor( 255, 80, 70 ) : QColor( 255, 230, 100 ) );
			painter.drawRect( QRectF( handle.m_Screen - QPointF( 3, 3 ), QSizeF( 6, 6 ) ) );
		}
	PaintSelectionExtras( view, painter );
	if ( m_From && m_bBox )
	{
		painter.setBrush( Qt::NoBrush );
		painter.setPen( Qt::white );
		painter.drawRect( QRectF( *m_From, m_At ).normalized() );
	}
	if ( m_Modal )
		for ( const auto &line : m_Modal->GetLockLines( Camera( view ).m_flFar ) )
		{
			painter.setPen( QPen( line.m_nPart == 0 ? mapstyle::AXIS_X : line.m_nPart == 1 ? mapstyle::AXIS_Y
																						   : mapstyle::AXIS_Z,
				1.5 ) );
			if ( auto *p2D = qobject_cast<CMapView2D *>( &view ) )
				painter.drawLine( p2D->ToWidget( p2D->Flatten( line.m_From ) ), p2D->ToWidget( p2D->Flatten( line.m_To ) ) );
			else if ( const auto projected = Camera( view ).ProjectLine( line.m_From, line.m_To ) )
				painter.drawLine( QPointF( projected->first[ 0 ], projected->first[ 1 ] ), QPointF( projected->second[ 0 ], projected->second[ 1 ] ) );
		}

	painter.restore();
}

//-----------------------------------------------------------------------------
// Purpose: Convert concave previews according to the persisted preference.
//-----------------------------------------------------------------------------
BrushConcave_t CMapBrushEdit::GetConcave() const
{
	return m_Editor.GetTool() != MapTool_t::MAP_TOOL_VERTEX && m_pConcave->currentIndex() == 0 ? BrushConcave_t::BRUSH_CONCAVE_SPLIT : BrushConcave_t::BRUSH_CONCAVE_KEEP;
}

//-----------------------------------------------------------------------------
// Purpose: Discard a drag without adding an undo command.
//-----------------------------------------------------------------------------
void CMapBrushEdit::Cancel( bool bKeepTool )
{
	m_bModelHandleDrag = false;
	m_bSlide = false;
	m_SlideMoves.clear();
	m_szModelNumber.clear();
	m_bAdjustingLast = false;
	m_ModelSelectedSides.clear();
	m_PolygonPoints.clear();
	m_PolygonHover.reset();
	m_nPolygonAxis = -1;
	const auto armed = m_ArmedTransform;
	const int nOperation = m_nOperation;
	m_bWaitForClick = false;
	m_ArmedTransform.reset();
	m_nOperation = -1;
	m_pOperationView = nullptr;
	m_bOperationValid = false;
	m_Modal.reset();
	m_DragGizmo.reset();
	m_nGizmoPart = -1;
	if ( m_pBase )
	{
		m_bApplying = true;
		m_Editor.GetDocument().EndPreview();
		m_bApplying = false;
	}
	m_pBase.reset();
	m_From.reset();
	m_bBox = false;
	m_Transform = {};
	if ( bKeepTool )
	{
		m_ArmedTransform = armed;
		m_nOperation = nOperation;
	}
	RefreshModelControls();
	emit ToolChanged();
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose: Commit a complete operation through the document's undo stack.
//-----------------------------------------------------------------------------
void CMapBrushEdit::Commit( MapEdit_t edit, const QString &szName )
{
	if ( edit.IsEmpty() )
		return;
	m_bApplying = true;
	m_Editor.GetDocument().Edit( std::move( edit ), szName );
	m_bApplying = false;
	SetParts( {} );
}

//-----------------------------------------------------------------------------
// Purpose: Apply a transform to each selected corner once, reporting refused operations.
//-----------------------------------------------------------------------------
void CMapBrushEdit::Transform( const Transform_t &transform )
{
	const auto *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene || m_Parts.empty() || !m_Editor.ConfirmVertexMerge( m_Parts, transform ) )
		return;
	const bool bIndividual = m_Editor.GetTool() == MapTool_t::MAP_TOOL_BRUSH_EDIT && GetPivot() == BrushPivot_t::BRUSH_PIVOT_INDIVIDUAL;
	CommitMoves( MovesFor( *pScene, transform ), bIndividual ? nullptr : &transform, m_Editor.GetTool() == MapTool_t::MAP_TOOL_VERTEX ? tr( "Move Vertices" ) : tr( "Transform Brush Parts" ) );
}

//-----------------------------------------------------------------------------
// Purpose: Only scenes held by the document or the tool are cached, by a weak pointer, so a
//          closed map's scene is never kept and a new scene at an old one's address is never
//          taken for it.
//-----------------------------------------------------------------------------
std::shared_ptr<const BrushMeshParts_t> CMapBrushEdit::MeshOf( const CScene &scene, std::uint32_t nSolid ) const
{
	// The base during an operation, the base before a drag and the scene drawn: enough for a
	// frame's questions without holding many snapshots' meshes.
	constexpr std::size_t MAX_SCENES = 3;
	if ( nSolid >= scene.m_Solids.size() )
		return std::make_shared<const BrushMeshParts_t>();
	std::shared_ptr<const CScene> pShared;
	if ( m_pBase && m_pBase.get() == &scene )
		pShared = m_pBase;
	else if ( auto pDocument = m_Editor.GetDocument().GetSharedScene(); pDocument.get() == &scene )
		pShared = std::move( pDocument );
	if ( !pShared )
		return std::make_shared<const BrushMeshParts_t>( BrushMeshParts_t::FromGeometry( scene.m_Solids[ nSolid ] ) );
	std::erase_if( m_MeshCaches, []( const MeshCache_t &cache ) { return cache.m_pScene.expired(); } );
	auto it = std::ranges::find_if( m_MeshCaches, [ & ]( const MeshCache_t &cache ) { return cache.m_pScene.lock() == pShared; } );
	if ( it == m_MeshCaches.end() )
	{
		if ( m_MeshCaches.size() >= MAX_SCENES )
			m_MeshCaches.erase( m_MeshCaches.begin() );
		m_MeshCaches.push_back( { pShared, {} } );
		it = m_MeshCaches.end() - 1;
	}
	auto &pMesh = it->m_Meshes[ nSolid ];
	if ( !pMesh )
		pMesh = std::make_shared<const BrushMeshParts_t>( BrushMeshParts_t::FromGeometry( scene.m_Solids[ nSolid ] ) );
	return pMesh;
}

//-----------------------------------------------------------------------------
// Purpose: Give modal transforms the same view axes in orthographic and perspective panes.
//-----------------------------------------------------------------------------
CViewCamera CMapBrushEdit::Camera( CMapView &view ) const
{
	if ( auto *p3D = qobject_cast<CMapView3D *>( &view ) )
		return p3D->GetSizedCamera();
	CViewCamera camera;
	camera.m_nWidth = view.width();
	camera.m_nHeight = view.height();
	if ( auto *p2D = qobject_cast<CMapView2D *>( &view ) )
	{
		Point_t centre{}, right{}, up{};
		centre[ p2D->GetAxisIndex( 0 ) ] = p2D->GetCenter().x();
		centre[ p2D->GetAxisIndex( 1 ) ] = p2D->GetCenter().y();
		right[ p2D->GetAxisIndex( 0 ) ] = 1;
		up[ p2D->GetAxisIndex( 1 ) ] = 1;
		camera.LookAlongAxis( centre, Scale( Cross( right, up ), -1 ), p2D->GetZoom(), 32768 );
	}
	return camera;
}

//-----------------------------------------------------------------------------
// Purpose: Start a typed transform about the selection's centre without changing the map.
//-----------------------------------------------------------------------------
void CMapBrushEdit::BeginTransform( MapGizmoMode_t nMode )
{
	Cancel();
	m_ArmedTransform = nMode;
	m_pDistance->setEnabled( false );
	emit ToolChanged();
	emit m_Editor.StatusMessage( tr( "Move into a view to start; click to apply, Escape to cancel." ) );
}

//-----------------------------------------------------------------------------
// Purpose: Anchor a readied transform at the first pointer position inside its view.
//-----------------------------------------------------------------------------
void CMapBrushEdit::StartTransform( CMapView &view, QPointF at )
{
	const auto *pScene = m_Editor.GetDocument().GetScene();
	if ( !m_ArmedTransform || !pScene || m_Parts.empty() )
		return;
	Box_t bounds;
	for ( const auto &part : m_Parts )
	{
		const auto pMeshParts = MeshOf( *pScene, pScene->GetIndex( part.m_nObject ) );
		const CBrushMesh &mesh = pMeshParts->m_Mesh;
		for ( const auto n : CornersOf( *pMeshParts, part.m_nKind, part.m_nIndex ) )
			bounds.Add( mesh.m_Vertices[ n ] );
	}
	if ( bounds.IsEmpty() )
		return;
	m_pBase = m_Editor.GetDocument().GetSharedScene();
	const std::array<Point_t, 3> axes{ { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
	m_pOperationView = &view;
	m_Modal.emplace( *m_ArmedTransform, GetPivotPoint().value_or( bounds.GetCenter() ), axes, Camera( view ), at );
	view.setFocus();
	m_Editor.SetActiveView( &view );
	PreviewTransform( view, Qt::NoModifier );
}

//-----------------------------------------------------------------------------
// Purpose: Recompute a modal preview from its starting snapshot and numeric input.
//-----------------------------------------------------------------------------
void CMapBrushEdit::PreviewTransform( CMapView &view, Qt::KeyboardModifiers modifiers )
{
	if ( !m_Modal || !m_pBase )
		return;
	const auto &settings = m_Editor.GetSettings();
	const bool bFree = modifiers.testFlag( Qt::AltModifier );
	m_Transform = m_Modal->Evaluate( Camera( view ), bFree || !settings.m_bSnapToGrid ? 0 : settings.m_nGridSize, bFree ? 0 : CPreferences::Instance().GetMapPreferences().m_flRotationSnap );
	std::string szProblem;
	const auto edit = MoveEdit( *m_pBase, m_Transform, &szProblem );
	if ( szProblem.empty() )
	{
		m_bApplying = true;
		m_Editor.GetDocument().Preview( edit );
		m_bApplying = false;
		emit m_Editor.StatusMessage( IsProportional() && m_Editor.GetTool() == MapTool_t::MAP_TOOL_BRUSH_EDIT ? m_Modal->GetDescription() + tr( "  (proportional, radius %1)" ).arg( GetProportionalRadius() ) : m_Modal->GetDescription() );
	}
	else
		emit m_Editor.StatusMessage( QString::fromStdString( szProblem ) );
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose: A brush is known by its id across edits (undo can renumber objects); a part on one
//          by its side id (faces) or its corners' places (vertices, edges), within 0.01.
//-----------------------------------------------------------------------------
void CMapBrushEdit::FindPartsAgain( const std::vector<std::uint32_t> &changed )
{
	const std::shared_ptr<const CScene> pOld = std::move( m_pPartsScene );
	const std::shared_ptr<const CScene> pNew = m_Editor.GetDocument().GetSharedScene();
	if ( m_Parts.empty() || !pOld || !pNew )
	{
		SetParts( {} );
		return;
	}
	std::unordered_map<int, std::uint32_t> solidById;
	std::set<int> wanted;
	for ( const BrushPart_t &part : m_Parts )
	{
		if ( pOld->IsSolidObject( part.m_nObject ) )
			wanted.insert( pOld->m_Map.m_Solids[ pOld->GetIndex( part.m_nObject ) ].m_nId );
	}
	for ( std::uint32_t i = 0; i < pNew->m_Map.m_Solids.size() && solidById.size() < wanted.size(); i++ )
	{
		const Solid_t &solid = pNew->m_Map.m_Solids[ i ];
		if ( !solid.m_bDeleted && wanted.contains( solid.m_nId ) )
			solidById.try_emplace( solid.m_nId, i );
	}
	const auto near = []( const Point_t &a, const Point_t &b ) { return Length( Subtract( a, b ) ) < 0.01; };
	std::vector<BrushPart_t> found;
	for ( const BrushPart_t &part : m_Parts )
	{
		if ( !pOld->IsSolidObject( part.m_nObject ) )
			continue;
		const std::uint32_t nOldSolid = pOld->GetIndex( part.m_nObject );
		const auto it = solidById.find( pOld->m_Map.m_Solids[ nOldSolid ].m_nId );
		if ( it == solidById.end() )
			continue;
		const std::uint32_t nNewSolid = it->second;
		const std::uint32_t nObject = pNew->SolidObject( nNewSolid );
		if ( nObject == part.m_nObject && std::ranges::find( changed, nObject ) == changed.end() )
		{
			found.push_back( part );
			continue;
		}
		const auto pOldMesh = MeshOf( *pOld, nOldSolid );
		const auto pNewMesh = MeshOf( *pNew, nNewSolid );
		const CBrushMesh &oldMesh = pOldMesh->m_Mesh, &newMesh = pNewMesh->m_Mesh;
		if ( part.m_nKind == BrushPartKind_t::BRUSH_PART_FACE )
		{
			if ( part.m_nIndex >= oldMesh.m_Faces.size() || oldMesh.m_Faces[ part.m_nIndex ].m_nSide < 0 )
				continue;
			const int nSideId = pOld->m_Map.m_Solids[ nOldSolid ].m_Sides[ oldMesh.m_Faces[ part.m_nIndex ].m_nSide ].m_nId;
			const auto &sides = pNew->m_Map.m_Solids[ nNewSolid ].m_Sides;
			for ( std::uint32_t f = 0; f < newMesh.m_Faces.size(); f++ )
			{
				const int nSide = newMesh.m_Faces[ f ].m_nSide;
				if ( nSide >= 0 && static_cast<std::size_t>( nSide ) < sides.size() && sides[ nSide ].m_nId == nSideId )
				{
					found.push_back( { nObject, part.m_nKind, f } );
					break;
				}
			}
			continue;
		}
		const std::vector<std::uint32_t> oldCorners = CornersOf( *pOldMesh, part.m_nKind, part.m_nIndex );
		std::vector<Point_t> places;
		for ( const std::uint32_t n : oldCorners )
			places.push_back( oldMesh.m_Vertices[ n ] );
		if ( part.m_nKind == BrushPartKind_t::BRUSH_PART_VERTEX && places.size() == 1 )
		{
			for ( std::uint32_t v = 0; v < newMesh.m_Vertices.size(); v++ )
			{
				if ( near( newMesh.m_Vertices[ v ], places[ 0 ] ) )
				{
					found.push_back( { nObject, part.m_nKind, v } );
					break;
				}
			}
		}
		else if ( part.m_nKind == BrushPartKind_t::BRUSH_PART_EDGE && places.size() == 2 )
		{
			const auto &edges = pNewMesh->m_Edges;
			for ( std::uint32_t e = 0; e < edges.size(); e++ )
			{
				const Point_t &a = newMesh.m_Vertices[ edges[ e ].first ], &b = newMesh.m_Vertices[ edges[ e ].second ];
				if ( ( near( a, places[ 0 ] ) && near( b, places[ 1 ] ) ) || ( near( a, places[ 1 ] ) && near( b, places[ 0 ] ) ) )
				{
					found.push_back( { nObject, part.m_nKind, e } );
					break;
				}
			}
		}
	}
	SetParts( std::move( found ) );
}

//-----------------------------------------------------------------------------
// Purpose: Pick and box-select parts, then preview a snapped drag on the view's plane.
//-----------------------------------------------------------------------------
bool CMapBrushEdit::HandleEvent( CMapView &view, QEvent *pEvent )
{
	// The spline tool takes the views' events the way the construction tools here do.
	if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_SPLINE )
		return m_Editor.GetSplineTool().HandleEvent( view, pEvent );
	// The wheel during a proportional move sizes its radius; otherwise the view's own.
	if ( pEvent->type() == QEvent::Wheel )
	{
		auto *pWheel = static_cast<QWheelEvent *>( pEvent );
		if ( m_Editor.GetTool() != MapTool_t::MAP_TOOL_BRUSH_EDIT || !m_Modal || !IsProportional() || pWheel->angleDelta().y() == 0 )
			return false;
		SetProportionalRadius( GetProportionalRadius() * ( pWheel->angleDelta().y() > 0 ? 1.1 : 1 / 1.1 ) );
		PreviewTransform( view, pWheel->modifiers() );
		return true;
	}
	if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_BRUSH_EDIT && pEvent->type() == QEvent::MouseButtonPress && !m_Modal && !m_bSlide && !m_From && m_nOperation < 0 )
	{
		auto *pMouse = static_cast<QMouseEvent *>( pEvent );
		if ( pMouse->button() == Qt::RightButton && pMouse->modifiers().testFlag( Qt::ShiftModifier ) )
		{
			PlaceCursor( view, pMouse->position(), pMouse->modifiers().testFlag( Qt::AltModifier ) );
			return true;
		}
	}
	if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_POLYGON )
		return HandlePolygon( view, pEvent );
	if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_CLIP )
		return HandleClip( view, pEvent );
	if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_BRUSH_EDIT && HandleModelEvent( view, pEvent ) )
		return true;
	const bool bLegacy = m_Editor.GetTool() == MapTool_t::MAP_TOOL_VERTEX;
	if ( bLegacy && !m_Modal && pEvent->type() == QEvent::KeyPress )
	{
		const auto nAction = MapActionFor( *static_cast<QKeyEvent *>( pEvent ), MapKeyContext_t::MAP_KEY_CONTEXT_BRUSH );
		if ( nAction == MapAction_t::MAP_ACTION_BRUSH_SCALE_VERTICES )
		{
			SyncLegacyParts();
			BeginTransform( MapGizmoMode_t::MAP_GIZMO_MODE_SCALE );
			StartTransform( view, view.mapFromGlobal( QCursor::pos() ) );
			return true;
		}
		if ( nAction == MapAction_t::MAP_ACTION_BRUSH_SPLIT_FACE )
		{
			SyncLegacyParts();
			Commit( BuildOperation( 0 ), tr( "Split Face" ) );
			return true;
		}
	}
	if ( bLegacy && !m_Modal && ( !qobject_cast<CMapView3D *>( &view ) || pEvent->type() == QEvent::KeyPress ) )
		return false;
	if ( !bLegacy && ( m_Editor.GetTool() != MapTool_t::MAP_TOOL_BRUSH_EDIT || ( m_pMode->currentIndex() == 0 && m_nOperation < 2 ) ) )
		return false;
	if ( m_pOperationView && m_pOperationView != &view )
	{
		if ( m_nOperation >= 2 && m_nOperation <= 4 && ( pEvent->type() == QEvent::MouseMove || pEvent->type() == QEvent::MouseButtonPress ) )
			Cancel( true );
		else
			return true;
	}
	if ( !m_Modal && !m_From && pEvent->type() == QEvent::KeyPress )
	{
		auto *pKey = static_cast<QKeyEvent *>( pEvent );
		const auto nAction = MapActionFor( *pKey, MapKeyContext_t::MAP_KEY_CONTEXT_BRUSH );
		if ( pKey->key() == Qt::Key_Escape || nAction == MapAction_t::MAP_ACTION_BRUSH_LEAVE )
		{
			if ( m_ArmedTransform || m_nOperation >= 0 )
				Cancel();
			else
				m_Editor.SetTool( MapTool_t::MAP_TOOL_SELECT );
			return true;
		}
		if ( !bLegacy && nAction )
		{
			const QPointF pointer = view.mapFromGlobal( QCursor::pos() );
			switch ( *nAction )
			{
				case MapAction_t::MAP_ACTION_BRUSH_PROPORTIONAL:
					SetProportional( !IsProportional() );
					emit m_Editor.StatusMessage( IsProportional() ? tr( "Proportional editing on" ) : tr( "Proportional editing off" ) );
					return true;
				case MapAction_t::MAP_ACTION_BRUSH_SELECT_LINKED:
				case MapAction_t::MAP_ACTION_BRUSH_DESELECT_LINKED:
					if ( const auto nObject = ObjectAt( view, pointer ) )
						SelectLinked( *nObject, *nAction == MapAction_t::MAP_ACTION_BRUSH_DESELECT_LINKED );
					return true;
				case MapAction_t::MAP_ACTION_BRUSH_SELECT_ALL_LINKED:
					SelectLinked();
					return true;
				case MapAction_t::MAP_ACTION_BRUSH_GROW:
					GrowSelection();
					return true;
				case MapAction_t::MAP_ACTION_BRUSH_SHRINK:
					ShrinkSelection();
					return true;
				case MapAction_t::MAP_ACTION_BRUSH_SELECT_SIMILAR:
					ShowSimilarMenu( QCursor::pos() );
					return true;
				case MapAction_t::MAP_ACTION_BRUSH_MODE_VERTEX:
				case MapAction_t::MAP_ACTION_BRUSH_MODE_EDGE:
				case MapAction_t::MAP_ACTION_BRUSH_MODE_FACE:
					SetMode( 1 + static_cast<int>( *nAction ) - static_cast<int>( MapAction_t::MAP_ACTION_BRUSH_MODE_VERTEX ) );
					return true;
				case MapAction_t::MAP_ACTION_BRUSH_BEVEL:
					ApplyOperation( 6 );
					return true;
				case MapAction_t::MAP_ACTION_BRUSH_LOOP_CUT:
					ApplyOperation( 7 );
					return true;
				default:
					break;
			}
		}
		if ( nAction == MapAction_t::MAP_ACTION_BRUSH_MOVE || nAction == MapAction_t::MAP_ACTION_BRUSH_ROTATE || nAction == MapAction_t::MAP_ACTION_BRUSH_SCALE )
		{
			BeginTransform( nAction == MapAction_t::MAP_ACTION_BRUSH_MOVE ? MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE : nAction == MapAction_t::MAP_ACTION_BRUSH_ROTATE ? MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE
																																									   : MapGizmoMode_t::MAP_GIZMO_MODE_SCALE );
			StartTransform( view, view.mapFromGlobal( QCursor::pos() ) );
			return true;
		}
		if ( nAction == MapAction_t::MAP_ACTION_BRUSH_INSET )
		{
			ApplyOperation( 1 );
			return true;
		}
	}
	if ( !m_Modal && !m_From && pEvent->type() == QEvent::MouseMove )
	{
		auto *pMouse = static_cast<QMouseEvent *>( pEvent );
		if ( m_ArmedTransform && !m_bWaitForClick )
		{
			StartTransform( view, pMouse->position() );
			return true;
		}
		if ( m_nOperation >= 2 && m_nOperation <= 4 && !m_bWaitForClick )
		{
			PreviewOperation( view, pMouse->position(), pMouse->modifiers() );
			return true;
		}
	}
	if ( m_nOperation >= 0 && m_nOperation <= 4 && m_pOperationView )
	{
		if ( pEvent->type() == QEvent::MouseMove )
		{
			auto *pMouse = static_cast<QMouseEvent *>( pEvent );
			PreviewOperation( view, pMouse->position(), pMouse->modifiers() );
			return true;
		}
		if ( pEvent->type() == QEvent::MouseButtonPress )
		{
			auto *pMouse = static_cast<QMouseEvent *>( pEvent );
			if ( pMouse->button() == Qt::LeftButton || pMouse->button() == Qt::RightButton )
			{
				if ( pMouse->button() == Qt::LeftButton && m_nOperation >= 2 )
					PreviewOperation( view, pMouse->position(), pMouse->modifiers() );
				FinishOperation( pMouse->button() == Qt::LeftButton );
				return true;
			}
		}
		if ( pEvent->type() == QEvent::MouseButtonRelease && m_nOperation == 1 )
		{
			auto *pMouse = static_cast<QMouseEvent *>( pEvent );
			if ( pMouse->button() == Qt::LeftButton )
			{
				PreviewOperation( view, pMouse->position(), pMouse->modifiers() );
				FinishOperation( true );
				return true;
			}
		}
		if ( pEvent->type() == QEvent::KeyPress )
		{
			auto *pKey = static_cast<QKeyEvent *>( pEvent );
			if ( pKey->key() == Qt::Key_Escape || pKey->key() == Qt::Key_Return || pKey->key() == Qt::Key_Enter )
			{
				FinishOperation( pKey->key() != Qt::Key_Escape );
				return true;
			}
		}
		return true;
	}
	if ( m_bSlide )
	{
		const auto finish = [ this ]( bool bApply ) {
			const auto moves = m_SlideMoves;
			Cancel( true );
			m_bWaitForClick = true;
			if ( bApply )
				CommitMoves( moves, nullptr, tr( "Slide Brush Parts" ) );
		};
		if ( pEvent->type() == QEvent::MouseMove )
		{
			PreviewSlide( view, static_cast<QMouseEvent *>( pEvent )->position() );
			return true;
		}
		if ( pEvent->type() == QEvent::MouseButtonPress )
		{
			auto *pMouse = static_cast<QMouseEvent *>( pEvent );
			if ( pMouse->button() == Qt::LeftButton )
				PreviewSlide( view, pMouse->position() );
			if ( pMouse->button() == Qt::LeftButton || pMouse->button() == Qt::RightButton )
				finish( pMouse->button() == Qt::LeftButton );
			return true;
		}
		if ( pEvent->type() == QEvent::KeyPress )
		{
			const int nKey = static_cast<QKeyEvent *>( pEvent )->key();
			if ( nKey == Qt::Key_Escape || nKey == Qt::Key_Return || nKey == Qt::Key_Enter )
				finish( nKey != Qt::Key_Escape );
			return true;
		}
		return pEvent->type() != QEvent::MouseButtonRelease;
	}
	if ( m_Modal )
	{
		if ( pEvent->type() == QEvent::KeyPress && !bLegacy )
		{
			auto *pKey = static_cast<QKeyEvent *>( pEvent );
			const auto nAction = MapActionFor( *pKey, MapKeyContext_t::MAP_KEY_CONTEXT_BRUSH );
			// Move's key again: slide instead, from where the move started.
			if ( nAction == MapAction_t::MAP_ACTION_BRUSH_MOVE && m_Modal->GetMode() == MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE )
			{
				BeginSlide( view, view.mapFromGlobal( QCursor::pos() ) );
				return true;
			}
			if ( nAction == MapAction_t::MAP_ACTION_BRUSH_PROPORTIONAL )
			{
				SetProportional( !IsProportional() );
				PreviewTransform( view, pKey->modifiers() );
				return true;
			}
			if ( IsProportional() && ( pKey->key() == Qt::Key_PageUp || pKey->key() == Qt::Key_PageDown ) )
			{
				SetProportionalRadius( GetProportionalRadius() * ( pKey->key() == Qt::Key_PageUp ? 1.1 : 1 / 1.1 ) );
				PreviewTransform( view, pKey->modifiers() );
				return true;
			}
		}
		if ( pEvent->type() == QEvent::KeyPress )
		{
			auto *pKey = static_cast<QKeyEvent *>( pEvent );
			if ( pKey->key() == Qt::Key_Escape )
			{
				Cancel( true );
				m_bWaitForClick = true;
				return true;
			}
			if ( pKey->key() == Qt::Key_Enter || pKey->key() == Qt::Key_Return )
			{
				const auto transform = m_Transform;
				Cancel( true );
				m_bWaitForClick = true;
				if ( !transform.IsIdentity() )
					Transform( transform );
				return true;
			}
			if ( pKey->key() == Qt::Key_X || pKey->key() == Qt::Key_Y || pKey->key() == Qt::Key_Z )
				m_Modal->LockAxis( pKey->key() - Qt::Key_X, pKey->modifiers().testFlag( Qt::ShiftModifier ) );
			else if ( pKey->key() == Qt::Key_Backspace )
				m_Modal->Backspace();
			else
				for ( const QChar ch : pKey->text() )
					static_cast<void>( m_Modal->Type( ch ) );
			PreviewTransform( view, pKey->modifiers() );
			return true;
		}
		if ( pEvent->type() == QEvent::MouseMove )
		{
			auto *pMouse = static_cast<QMouseEvent *>( pEvent );
			m_Modal->MovePointer( pMouse->position(), pMouse->modifiers().testFlag( Qt::ShiftModifier ) );
			PreviewTransform( view, pMouse->modifiers() );
			return true;
		}
		if ( pEvent->type() == QEvent::MouseButtonPress )
		{
			auto *pMouse = static_cast<QMouseEvent *>( pEvent );
			if ( pMouse->button() == Qt::LeftButton || pMouse->button() == Qt::RightButton )
			{
				const auto transform = m_Transform;
				Cancel( true );
				m_bWaitForClick = true;
				if ( pMouse->button() == Qt::LeftButton && !transform.IsIdentity() )
					Transform( transform );
				return true;
			}
		}
		return false;
	}
	if ( pEvent->type() == QEvent::KeyPress )
	{
		auto *pKey = static_cast<QKeyEvent *>( pEvent );
		if ( pKey->key() == Qt::Key_Escape )
		{
			Cancel();
			return true;
		}
		const auto nAction = MapActionFor( *pKey, MapKeyContext_t::MAP_KEY_CONTEXT_BRUSH );
		if ( nAction == MapAction_t::MAP_ACTION_BRUSH_SCALE_VERTICES )
		{
			BeginTransform( MapGizmoMode_t::MAP_GIZMO_MODE_SCALE );
			return true;
		}
		if ( nAction == MapAction_t::MAP_ACTION_BRUSH_SPLIT_FACE )
		{
			ApplyOperation( 0 );
			return true;
		}
		return false;
	}
	if ( pEvent->type() != QEvent::MouseButtonPress && pEvent->type() != QEvent::MouseMove && pEvent->type() != QEvent::MouseButtonRelease )
		return false;
	auto *pMouse = static_cast<QMouseEvent *>( pEvent );
	if ( pEvent->type() == QEvent::MouseButtonPress )
	{
		if ( pMouse->button() == Qt::RightButton && ( m_ArmedTransform || m_nOperation >= 0 ) )
		{
			Cancel();
			return true;
		}
		if ( pMouse->button() != Qt::LeftButton )
			return false;
		if ( m_ArmedTransform && !m_Parts.empty() )
		{
			m_bWaitForClick = false;
			StartTransform( view, pMouse->position() );
			return true;
		}
		if ( m_nOperation >= 2 && m_nOperation <= 4 )
		{
			m_bWaitForClick = false;
			PreviewOperation( view, pMouse->position(), pMouse->modifiers() );
			return true;
		}
		if ( m_nOperation == 0 && m_Parts.size() == 2 )
		{
			Commit( BuildOperation( 0 ), tr( "Split Face" ) );
			return true;
		}
		view.setFocus();
		m_Editor.SetActiveView( &view );
		m_From = pMouse->position();
		m_At = *m_From;
		m_bBox = true;
		if ( !bLegacy && m_nOperation < 0 && qobject_cast<CMapView3D *>( &view ) )
			if ( const auto gizmo = Gizmo() )
			{
				const int nPart = gizmo->HitAt( Camera( view ), 90, m_At, 6 );
				if ( nPart >= 0 )
				{
					m_DragGizmo = gizmo;
					m_nGizmoPart = nPart;
					m_bBox = false;
					m_pBase = m_Editor.GetDocument().GetSharedScene();
					return true;
				}
			}
		if ( !bLegacy && pMouse->modifiers().testFlag( Qt::AltModifier ) && GetMode() >= 1 && SelectLoopAt( view, *m_From, pMouse->modifiers().testFlag( Qt::ControlModifier ), pMouse->modifiers().testFlag( Qt::ShiftModifier ) ) )
		{
			// A drag on from the click moves the loop (Alt: off the grid).
			m_bBox = false;
			m_Anchor = GetPivotPoint().value_or( Point_t{} );
			if ( !m_Parts.empty() )
				m_pBase = m_Editor.GetDocument().GetSharedScene();
			Refresh();
			return true;
		}
		const auto handles = Handles( view );
		const Handle_t *pNearest = nullptr;
		double flBest = 100;
		for ( const auto &handle : handles )
		{
			const QPointF d = handle.m_Screen - *m_From;
			double flDistance = QPointF::dotProduct( d, d );
			if ( !bLegacy && handle.m_Part.m_nKind == BrushPartKind_t::BRUSH_PART_FACE )
				continue;
			if ( !bLegacy && handle.m_Part.m_nKind == BrushPartKind_t::BRUSH_PART_EDGE )
			{
				const auto *pScene = m_Editor.GetDocument().GetScene();
				const auto pMeshParts = MeshOf( *pScene, pScene->GetIndex( handle.m_Part.m_nObject ) );
				const CBrushMesh &mesh = pMeshParts->m_Mesh;
				const auto edge = pMeshParts->m_Edges[ handle.m_Part.m_nIndex ];
				const auto a = Project( view, mesh.m_Vertices[ edge.first ] ), b = Project( view, mesh.m_Vertices[ edge.second ] );
				if ( !a || !b )
					continue;
				const auto along = *b - *a;
				const auto flLength = QPointF::dotProduct( along, along );
				if ( flLength < 1e-6 )
					continue;
				const auto delta = *m_From - ( *a + along * std::clamp( QPointF::dotProduct( *m_From - *a, along ) / flLength, 0.0, 1.0 ) );
				flDistance = QPointF::dotProduct( delta, delta );
			}
			if ( flDistance < flBest )
			{
				flBest = flDistance;
				pNearest = &handle;
			}
		}
		std::optional<Handle_t> faceHandle;
		if ( !pNearest && !bLegacy && GetMode() == 3 )
			if ( auto *p3D = qobject_cast<CMapView3D *>( &view ) )
				if ( const auto hit = p3D->FaceAt( m_At ) )
				{
					const auto *pScene = m_Editor.GetDocument().GetScene();
					const auto nObject = pScene->SolidObject( hit->m_nSolid );
					const auto selected = ObjectsOf( *pScene, m_Editor.GetDocument().GetSelection() );
					if ( std::ranges::find( selected.m_Solids, hit->m_nSolid ) != selected.m_Solids.end() )
					{
						const auto pMeshParts = MeshOf( *pScene, hit->m_nSolid );
						const CBrushMesh &mesh = pMeshParts->m_Mesh;
						for ( std::uint32_t i = 0; i < mesh.m_Faces.size(); ++i )
							if ( mesh.m_Faces[ i ].m_nSide == static_cast<int>( hit->m_nSide ) )
							{
								faceHandle = Handle_t{ { nObject, BrushPartKind_t::BRUSH_PART_FACE, i }, mesh.FaceCenter( i ), m_At };
								pNearest = &*faceHandle;
								break;
							}
					}
				}
		if ( !pNearest && !bLegacy && GetMode() == 3 && qobject_cast<CMapView2D *>( &view ) )
		{
			const auto *pScene = m_Editor.GetDocument().GetScene();
			double flDepth = -std::numeric_limits<double>::infinity();
			for ( const auto &handle : handles )
			{
				const auto pMeshParts = MeshOf( *pScene, pScene->GetIndex( handle.m_Part.m_nObject ) );
				const CBrushMesh &mesh = pMeshParts->m_Mesh;
				QPolygonF polygon;
				for ( auto corner : mesh.m_Faces[ handle.m_Part.m_nIndex ].m_Corners )
					if ( auto point = Project( view, mesh.m_Vertices[ corner ] ) )
						polygon << *point;
				const double depth = -Dot( handle.m_Position, Camera( view ).GetForward() );
				if ( polygon.containsPoint( m_At, Qt::OddEvenFill ) && depth > flDepth )
				{
					faceHandle = handle;
					pNearest = &*faceHandle;
					flDepth = depth;
				}
			}
		}
		if ( pNearest )
		{
			auto parts = m_Parts;
			if ( pMouse->modifiers().testFlag( Qt::ControlModifier ) )
			{
				if ( bLegacy && std::ranges::find( parts, pNearest->m_Part ) == parts.end() )
					parts.push_back( pNearest->m_Part );
				else
					std::erase( parts, pNearest->m_Part );
			}
			else
			{
				if ( !pMouse->modifiers().testFlag( Qt::ShiftModifier ) && std::ranges::find( parts, pNearest->m_Part ) == parts.end() )
					parts.clear();
				parts.push_back( pNearest->m_Part );
			}
			m_Active = pNearest->m_Part;
			SetParts( std::move( parts ) );
			m_bBox = false;
			m_Anchor = pNearest->m_Position;
			if ( !m_Parts.empty() )
				m_pBase = m_Editor.GetDocument().GetSharedScene();
		}
		Refresh();
		return true;
	}
	if ( !m_From )
		return false;
	m_At = pMouse->position();
	if ( pEvent->type() == QEvent::MouseMove )
	{
		if ( m_pBase && !m_bBox && m_nOperation < 0 )
		{
			Point_t delta{};
			if ( auto *p2D = qobject_cast<CMapView2D *>( &view ) )
			{
				const auto d = p2D->ToMap( m_At ) - p2D->ToMap( *m_From );
				delta[ p2D->GetAxisIndex( 0 ) ] = d.x();
				delta[ p2D->GetAxisIndex( 1 ) ] = d.y();
			}
			else if ( auto *p3D = qobject_cast<CMapView3D *>( &view ) )
			{
				const auto camera = p3D->GetSizedCamera();
				const auto normal = camera.GetForward();
				auto point = [ & ]( QPointF at ) {const auto [from,dir]=camera.Ray(at.x(),at.y()); const double d=Dot(dir,normal); return std::abs(d)<1e-8?m_Anchor:Add(from,Scale(dir,Dot(Subtract(m_Anchor,from),normal)/d)); };
				delta = Subtract( point( m_At ), point( *m_From ) );
			}
			const double flGrid = m_Editor.GetSettings().m_nGridSize;
			if ( m_Editor.GetSettings().m_bSnapToGrid && !pMouse->modifiers().testFlag( Qt::AltModifier ) && flGrid > 0 )
				for ( int i = 0; i < 3; i++ )
					if ( std::abs( delta[ i ] ) > 1e-8 )
						delta[ i ] = std::round( ( m_Anchor[ i ] + delta[ i ] ) / flGrid ) * flGrid - m_Anchor[ i ];
			if ( pMouse->modifiers().testFlag( Qt::ControlModifier ) && !m_DragGizmo )
			{
				double flBest = 144;
				for ( std::uint32_t nSolid = 0; nSolid < m_pBase->m_Solids.size(); nSolid++ )
				{
					const auto pMeshParts = MeshOf( *m_pBase, nSolid );
					const CBrushMesh &mesh = pMeshParts->m_Mesh;
					std::vector<Point_t> targets = mesh.m_Vertices;
					for ( const auto &[ a, b ] : pMeshParts->m_Edges )
						targets.push_back( Scale( Add( mesh.m_Vertices[ a ], mesh.m_Vertices[ b ] ), 0.5 ) );
					for ( std::size_t f = 0; f < mesh.m_Faces.size(); f++ )
						targets.push_back( mesh.FaceCenter( f ) );
					for ( const auto &point : targets )
					{
						QPointF at;
						if ( auto *p2D = qobject_cast<CMapView2D *>( &view ) )
							at = p2D->ToWidget( p2D->Flatten( point ) );
						else
						{
							const auto projected = Camera( view ).Project( point );
							if ( !projected )
								continue;
							at = { ( *projected )[ 0 ], ( *projected )[ 1 ] };
						}
						const auto d = at - m_At;
						const double flDistance = QPointF::dotProduct( d, d );
						if ( flDistance < flBest )
						{
							flBest = flDistance;
							delta = Subtract( point, m_Anchor );
						}
					}
				}
			}
			m_Transform = m_DragGizmo ? m_DragGizmo->Drag( Camera( view ), m_nGizmoPart, *m_From, m_At, flGrid, CPreferences::Instance().GetMapPreferences().m_flRotationSnap ) : Transform_t::Translation( delta );
			std::string szProblem;
			const auto edit = MoveEdit( *m_pBase, m_Transform, &szProblem );
			if ( szProblem.empty() )
			{
				m_bApplying = true;
				m_Editor.GetDocument().Preview( edit );
				m_bApplying = false;
			}
			else
				emit m_Editor.StatusMessage( QString::fromStdString( szProblem ) );
		}
		Refresh();
		return true;
	}
	if ( pMouse->button() != Qt::LeftButton )
		return false;
	if ( m_bBox )
	{
		const auto box = QRectF( *m_From, m_At ).normalized();
		auto parts = pMouse->modifiers().testFlag( Qt::ShiftModifier ) || pMouse->modifiers().testFlag( Qt::ControlModifier ) ? m_Parts : std::vector<BrushPart_t>{};
		for ( const auto &handle : Handles( view ) )
			if ( box.contains( handle.m_Screen ) )
			{
				if ( !bLegacy && pMouse->modifiers().testFlag( Qt::ControlModifier ) )
					std::erase( parts, handle.m_Part );
				else
					parts.push_back( handle.m_Part );
			}
		SetParts( std::move( parts ) );
		Cancel( true );
	}
	else
	{
		const auto transform = m_Transform;
		Cancel( true );
		if ( !transform.IsIdentity() )
			Transform( transform );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Apply topology changes to selected solids as a single undoable edit.
//-----------------------------------------------------------------------------
MapEdit_t CMapBrushEdit::BuildOperation( int nOperation )
{
	const auto *pScene = m_pBase ? m_pBase.get() : m_Editor.GetDocument().GetScene();
	if ( !pScene || ( m_Parts.empty() && nOperation < 2 && nOperation != 1 ) )
		return {};
	if ( nOperation == 1 || nOperation >= 5 )
	{
		auto params = GetModelParameters();
		const auto operation = nOperation == 1 ? BrushModelOperation_t::BRUSH_MODEL_OPERATION_INSET : static_cast<BrushModelOperation_t>( nOperation - 5 );
		auto output = ModelBrushes( *pScene, m_pBase ? m_OperationObjects : m_Editor.GetDocument().GetSelection(), m_Parts, operation, params );
		m_ModelSelectedSides = std::move( output.m_SelectedSides );
		if ( !output.m_szProblem.empty() )
			emit m_Editor.StatusMessage( QString::fromStdString( output.m_szProblem ) );
		return std::move( output.m_Edit );
	}
	MapEdit_t edit;
	int nSide = pScene->m_nNextSideId;
	int nId = pScene->m_nNextObjectId;
	auto nAdded = static_cast<std::uint32_t>( pScene->m_Map.m_Solids.size() );
	std::map<std::uint32_t, std::vector<BrushPart_t>> objects;
	for ( const auto &part : m_Parts )
		objects[ part.m_nObject ].push_back( part );
	if ( nOperation >= 2 )
		for ( const auto nSolid : ObjectsOf( *pScene, m_Editor.GetDocument().GetSelection() ).m_Solids )
			objects.try_emplace( pScene->SolidObject( nSolid ) );
	for ( const auto &[ nObject, parts ] : objects )
	{
		const auto nSolid = pScene->GetIndex( nObject );
		const auto &source = pScene->m_Map.m_Solids[ nSolid ];
		if ( nOperation >= 2 && nOperation <= 4 )
		{
			const int nAxis = nOperation - 2;
			const auto &bounds = pScene->m_Solids[ nSolid ].m_Bounds;
			if ( m_pDistance->value() <= bounds.m_Mins[ nAxis ] + 1e-6 || m_pDistance->value() >= bounds.m_Maxs[ nAxis ] - 1e-6 )
				continue;
		}
		if ( std::ranges::any_of( source.m_Sides, []( const Side_t &side ) { return bool( side.m_pDisp ); } ) )
		{
			emit m_Editor.StatusMessage( tr( "This operation cannot modify a displacement brush." ) );
			return {};
		}
		auto mesh = CBrushMesh::FromGeometry( pScene->m_Solids[ nSolid ] );
		bool bChanged = false;
		if ( nOperation == 0 && parts.size() == 2 && parts[ 0 ].m_nKind == BrushPartKind_t::BRUSH_PART_VERTEX && parts[ 1 ].m_nKind == BrushPartKind_t::BRUSH_PART_VERTEX )
		{
			for ( std::uint32_t f = 0; f < mesh.m_Faces.size(); f++ )
				if ( SplitFace( mesh, f, parts[ 0 ].m_nIndex, parts[ 1 ].m_nIndex ) )
				{
					bChanged = true;
					break;
				}
		}
		else if ( nOperation == 0 && parts.size() == 2 && parts[ 0 ].m_nKind == BrushPartKind_t::BRUSH_PART_EDGE && parts[ 1 ].m_nKind == BrushPartKind_t::BRUSH_PART_EDGE )
			bChanged = SplitFaceEdges( mesh, parts[ 0 ].m_nIndex, parts[ 1 ].m_nIndex );
		else if ( nOperation == 1 )
		{
			if ( parts.size() != 1 || parts.front().m_nKind != BrushPartKind_t::BRUSH_PART_FACE )
			{
				emit m_Editor.StatusMessage( tr( "Select one face for inset." ) );
				return {};
			}
			auto pieces = InsetBrush( pScene->m_Map, source, pScene->m_Solids[ nSolid ], parts.front().m_nIndex, m_pDistance->value() );
			if ( pieces.empty() )
			{
				emit m_Editor.StatusMessage( tr( "The inset would collapse the face." ) );
				return {};
			}
			Group_t group;
			group.m_nId = nId++;
			group.m_Editor = source.m_Editor;
			edit.m_Groups.push_back( group );
			for ( std::size_t i = 0; i < pieces.size(); i++ )
			{
				auto piece = AsNew( std::move( pieces[ i ] ), nId, nSide );
				piece.m_nEntity = source.m_nEntity;
				piece.m_Editor.m_nGroup = group.m_nId;
				piece.m_Editor.m_bChanged = true;
				if ( i == 0 )
				{
					piece.m_nId = source.m_nId;
					piece.m_nReadEntity = source.m_nReadEntity;
					piece.m_Span = source.m_Span;
					piece.m_bChanged = true;
				}
				edit.m_Solids.emplace_back( i == 0 ? nSolid : nAdded++, std::move( piece ) );
			}
			continue;
		}
		else if ( nOperation >= 2 && nOperation <= 4 )
		{
			Point_t normal{};
			normal[ nOperation - 2 ] = 1;
			bChanged = CutRing( mesh, { normal, m_pDistance->value() } );
		}
		if ( !bChanged && nOperation >= 2 )
			continue;
		if ( !bChanged )
		{
			emit m_Editor.StatusMessage( tr( "The selected parts cannot perform this operation." ) );
			return {};
		}
		auto result = ToSolids( pScene->m_Map, source, mesh, GetConcave() );
		if ( result.m_Solids.size() != 1 )
		{
			emit m_Editor.StatusMessage( QString::fromStdString( result.m_szProblem ) );
			return {};
		}
		for ( auto &side : result.m_Solids.front().m_Sides )
			if ( side.m_nId == 0 )
				side.m_nId = nSide++;
		edit.m_Solids.emplace_back( nSolid, std::move( result.m_Solids.front() ) );
	}
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose: Ready a topology tool without changing the selected brush.
//-----------------------------------------------------------------------------
void CMapBrushEdit::ApplyOperation( int nOperation )
{
	if ( m_nOperation == nOperation )
	{
		Cancel();
		return;
	}
	Cancel();
	m_ModelParams.m_bUsePivot = false;
	if ( nOperation == 1 && GetMode() != 3 )
		SetMode( 3 );
	else if ( nOperation == 0 && GetMode() != 1 && GetMode() != 2 )
		SetMode( 1 );
	if ( nOperation >= 5 )
	{
		const auto operation = static_cast<BrushModelOperation_t>( nOperation - 5 );
		if ( operation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_EXTRUDE || operation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_ALIGN_FACE )
		{
			if ( GetMode() != 3 )
				SetMode( 3 );
		}
		if ( operation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_LOOP && GetMode() != 2 )
			SetMode( 2 );
		if ( operation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_BEVEL && GetMode() == 3 )
			SetMode( 2 );
	}
	m_nOperation = nOperation;
	m_pDistance->setEnabled( false );
	if ( nOperation >= 5 )
	{
		if ( nOperation == 15 )
		{
			auto params = GetModelParameters();
			params.m_Offset = { 0, 0, 0 };
			if ( params.m_nCount == 1 )
				params.m_nCount = 7;
			SetModelParameters( params );
		}
		m_pDistance->setEnabled( true );
		RefreshModelControls();
		emit ToolChanged();
		emit m_Editor.StatusMessage( ( nOperation == 5 || nOperation == 6 ? tr( "%1: select parts, then drag the circle; release to apply. Escape cancels." ) : tr( "%1: select parts, move into a view to preview, click to apply; Escape cancels." ) ).arg( OperationName( nOperation ) ) );
		Refresh();
		return;
	}
	RefreshModelControls();
	emit ToolChanged();
	emit m_Editor.StatusMessage( nOperation == 0 ? tr( "Select two corners or edges, then click to split; Escape cancels." ) : nOperation == 1 ? tr( "Select a face, then drag its circle to inset; Escape cancels." )
																																			   : tr( "Point at the grid position to cut; click to apply, Escape cancels." ) );
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose: Measure topology edits from a view position and preview against the original map.
//-----------------------------------------------------------------------------
void CMapBrushEdit::PreviewOperation( CMapView &view, QPointF at, Qt::KeyboardModifiers modifiers )
{
	const bool bRing = m_nOperation >= 2;
	if ( !bRing && ( m_Parts.empty() || std::ranges::any_of( m_Parts, []( const BrushPart_t &part ) { return part.m_nKind != BrushPartKind_t::BRUSH_PART_FACE; } ) ) )
		return;
	if ( !m_pOperationView )
	{
		const auto *pScene = m_Editor.GetDocument().GetScene();
		if ( !pScene )
			return;
		Box_t bounds;
		for ( const auto nSolid : ObjectsOf( *pScene, m_Editor.GetDocument().GetSelection() ).m_Solids )
			bounds.Add( pScene->m_Solids[ nSolid ].m_Bounds );
		if ( bounds.IsEmpty() )
			return;
		m_Anchor = bounds.GetCenter();
		if ( !bRing )
			if ( const auto gizmo = Gizmo() )
				m_Anchor = gizmo->GetPivot();
		m_pOperationView = &view;
		m_From = at;
		m_At = at;
		m_LastModelPointer = at;
		m_OperationObjects = m_Editor.GetDocument().GetSelection();
		m_pBase = m_Editor.GetDocument().GetSharedScene();
		view.setFocus();
		m_Editor.SetActiveView( &view );
		if ( !bRing )
		{
			const QSignalBlocker blocker( m_pDistance );
			m_pDistance->setValue( 0 );
			Refresh();
			return;
		}
	}
	if ( bRing )
		m_At = at;
	else
		m_At += ( at - m_LastModelPointer ) * ( modifiers.testFlag( Qt::ShiftModifier ) ? 0.1 : 1.0 );
	m_LastModelPointer = at;
	const auto point = PointerPoint( view, m_At, m_Anchor );
	if ( !point )
		return;
	double flDistance = 0;
	if ( bRing )
	{
		const int nAxis = m_nOperation - 2;
		if ( auto *p2D = qobject_cast<CMapView2D *>( &view ); p2D && nAxis != p2D->GetAxisIndex( 0 ) && nAxis != p2D->GetAxisIndex( 1 ) )
		{
			emit m_Editor.StatusMessage( tr( "Use a view that shows the cut axis." ) );
			return;
		}
		flDistance = ( *point )[ nAxis ];
	}
	else
	{
		const auto from = PointerPoint( view, *m_From, m_Anchor );
		if ( !from )
			return;
		flDistance = Length( Subtract( *point, m_Anchor ) ) - Length( Subtract( *from, m_Anchor ) );
		if ( !modifiers.testFlag( Qt::ControlModifier ) )
			flDistance = std::max( 0.0, flDistance );
	}
	const auto &settings = m_Editor.GetSettings();
	if ( settings.m_bSnapToGrid && !modifiers.testFlag( Qt::AltModifier ) && settings.m_nGridSize > 0 )
		flDistance = std::round( flDistance / settings.m_nGridSize ) * settings.m_nGridSize;
	m_flModelPointerAmount = flDistance;
	const QSignalBlocker blocker( m_pDistance );
	if ( !bRing && modifiers.testFlag( Qt::ControlModifier ) )
	{
		const QSignalBlocker depthBlocker( m_pModelDepth );
		m_pModelDepth->setValue( flDistance );
	}
	else if ( !m_szModelNumber.isEmpty() )
	{
		bool bOk = false;
		const double flTyped = m_szModelNumber.toDouble( &bOk );
		if ( bOk )
			m_pDistance->setValue( flTyped );
	}
	else
		m_pDistance->setValue( flDistance );
	const auto edit = BuildOperation( m_nOperation );
	m_bOperationValid = !edit.IsEmpty();
	m_bApplying = true;
	if ( m_bOperationValid )
		m_Editor.GetDocument().Preview( edit );
	else
		m_Editor.GetDocument().EndPreview();
	m_bApplying = false;
	if ( m_bOperationValid )
		emit m_Editor.StatusMessage( ( bRing ? tr( "Cut at %1 — click or Enter to apply; Escape cancels." ) : tr( "Inset %1 — release to apply; Escape cancels." ) ).arg( flDistance ) );
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose: Expose the selection kind so the tool strip follows panel changes.
//-----------------------------------------------------------------------------
int CMapBrushEdit::GetMode() const
{
	return m_pMode->currentIndex();
}

//-----------------------------------------------------------------------------
// Purpose: Project a world point onto either kind of map pane.
//-----------------------------------------------------------------------------
std::optional<QPointF> CMapBrushEdit::Project( CMapView &view, Point_t point ) const
{
	if ( auto *p2D = qobject_cast<CMapView2D *>( &view ) )
		return p2D->ToWidget( p2D->Flatten( point ) );
	if ( const auto projected = Camera( view ).Project( point ) )
		return QPointF( ( *projected )[ 0 ], ( *projected )[ 1 ] );
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: Place the pointer on a stable view plane, or the brush surface for ring cuts.
//-----------------------------------------------------------------------------
std::optional<Point_t> CMapBrushEdit::PointerPoint( CMapView &view, QPointF at, Point_t anchor ) const
{
	if ( auto *p2D = qobject_cast<CMapView2D *>( &view ) )
	{
		const auto point = p2D->ToMap( at );
		anchor[ p2D->GetAxisIndex( 0 ) ] = point.x();
		anchor[ p2D->GetAxisIndex( 1 ) ] = point.y();
		return anchor;
	}
	const auto camera = Camera( view );
	const auto [ from, direction ] = camera.Ray( at.x(), at.y() );
	Point_t normal = camera.GetForward();
	double flDistance = Dot( normal, anchor );
	if ( m_nOperation >= 2 && m_nOperation <= 4 && m_pBase )
		if ( auto *p3D = qobject_cast<CMapView3D *>( &view ) )
			if ( const auto hit = p3D->FaceAt( at ) )
				if ( hit->m_nSolid < m_pBase->m_Solids.size() )
					if ( const auto *pFace = m_pBase->m_Solids[ hit->m_nSolid ].FindFace( hit->m_nSide ) )
					{
						normal = pFace->m_Plane.m_Normal;
						flDistance = pFace->m_Plane.m_flDistance;
					}
	const double flDot = Dot( direction, normal );
	if ( std::abs( flDot ) < 1e-8 )
		return std::nullopt;
	return Add( from, Scale( direction, ( flDistance - Dot( normal, from ) ) / flDot ) );
}

//-----------------------------------------------------------------------------
// Purpose: Keep the inset's drag circle on the original face while its pieces preview.
//-----------------------------------------------------------------------------
std::optional<QPointF> CMapBrushEdit::InsetCenter( CMapView &view ) const
{
	if ( m_pOperationView )
		return Project( view, m_Anchor );
	if ( m_Parts.empty() || std::ranges::any_of( m_Parts, []( const BrushPart_t &part ) { return part.m_nKind != BrushPartKind_t::BRUSH_PART_FACE; } ) )
		return std::nullopt;
	if ( const auto gizmo = Gizmo() )
		return Project( view, gizmo->GetPivot() );
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: Share legacy selections with the 3D handles without changing the active tool.
//-----------------------------------------------------------------------------
void CMapBrushEdit::SyncLegacyParts()
{
	if ( m_Editor.GetTool() != MapTool_t::MAP_TOOL_VERTEX )
		return;
	m_Parts.clear();
	for ( const auto &handle : m_Editor.GetVertexHandles() )
		m_Parts.push_back( { handle.m_nObject, handle.m_bEdge ? BrushPartKind_t::BRUSH_PART_EDGE : BrushPartKind_t::BRUSH_PART_VERTEX, handle.m_nIndex } );
	Refresh();
}
//-----------------------------------------------------------------------------
// Purpose: Explicit vertex welding is one undoable operation and reports refused shapes.
//-----------------------------------------------------------------------------
void CMapBrushEdit::MergeVertices( bool bAtCenter, double flDistance )
{
	if ( m_Editor.GetTool() != MapTool_t::MAP_TOOL_VERTEX && m_Editor.GetTool() != MapTool_t::MAP_TOOL_BRUSH_EDIT )
	{
		emit m_Editor.StatusMessage( tr( "Select vertices with the Vertex or Brush Edit tool first." ) );
		return;
	}
	Cancel();
	const auto *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene )
		return;
	std::string szProblem;
	// Explicit welds refit their faces under the selected concavity policy.
	const auto nConcave = m_pConcave->currentIndex() == 0 ? BrushConcave_t::BRUSH_CONCAVE_SPLIT : BrushConcave_t::BRUSH_CONCAVE_KEEP;
	auto edit = MergeBrushVertices( *pScene, m_Parts, bAtCenter, flDistance, nConcave, &szProblem );
	if ( edit.IsEmpty() )
	{
		emit m_Editor.StatusMessage( QString::fromStdString( szProblem ) );
		return;
	}
	Commit( std::move( edit ), tr( "Merge Vertices" ) );
}
