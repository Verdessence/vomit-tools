//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapBrushEdit.h.
//===========================================================================//
#include "MapBrushEdit.h"

#include <algorithm>
#include <cmath>
#include <QCheckBox>
#include <QCursor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include "Preferences.h"
#include "map/MapEditor.h"
#include "map/MapView2D.h"
#include "map/MapView3D.h"
#include "map/Clip.h"

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
	m_pXray = new QCheckBox( tr( "X-ray in 3D" ), this );
	pLayout->addRow( m_pXray );
	m_pConcave = new QComboBox( this );
	m_pConcave->addItems( { tr( "Split into convex brushes" ), tr( "Keep it as it is" ) } );
	m_pConcave->setCurrentIndex( CPreferences::Instance().GetMapPreferences().m_bBrushKeepConcave ? 1 : 0 );
	pLayout->addRow( tr( "Concave shapes" ), m_pConcave );
	m_pDistance = new QDoubleSpinBox( this );
	m_pDistance->setRange( -16384, 16384 );
	m_pDistance->setDecimals( 3 );
	m_pDistance->setValue( 16 );
	pLayout->addRow( tr( "Distance" ), m_pDistance );
	const std::pair<QString, MapGizmoMode_t> transforms[] = {
		{ tr( "Move Parts" ), MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE },
		{ tr( "Rotate Parts" ), MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE },
		{ tr( "Scale Parts" ), MapGizmoMode_t::MAP_GIZMO_MODE_SCALE }
	};
	for ( const auto &[ szName, nMode ] : transforms )
	{
		auto *pButton = new QPushButton( szName, this );
		pLayout->addRow( pButton );
		connect( pButton, &QPushButton::clicked, this, [ this, nMode ] { BeginTransform( nMode ); } );
	}
	const QStringList names{ tr( "Split Face" ), tr( "Inset Face" ), tr( "Cut Ring on X" ), tr( "Cut Ring on Y" ), tr( "Cut Ring on Z" ) };
	for ( int i = 0; i < names.size(); i++ )
	{
		auto *pButton = new QPushButton( names[ i ], this );
		pLayout->addRow( pButton );
		connect( pButton, &QPushButton::clicked, this, [ this, i ] { ApplyOperation( i ); } );
	}
	connect( m_pMode, &QComboBox::currentIndexChanged, this, [ this ] {Cancel(); SetParts({}); } );
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
	connect( &editor.GetDocument(), &CMapDocument::SelectionChanged, this, [ this ] {if(!m_bApplying) {Cancel(); SetParts({});} } );
	connect( &editor.GetDocument(), &CMapDocument::SceneEdited, this, [ this ] {if(!m_bApplying && !m_pBase) SetParts({}); } );
}

//-----------------------------------------------------------------------------
// Purpose: Select an explicit kind without introducing another keyboard preset.
//-----------------------------------------------------------------------------
void CMapBrushEdit::SetMode( int nMode )
{
	m_pMode->setCurrentIndex( std::clamp( nMode, 0, 3 ) );
}

//-----------------------------------------------------------------------------
// Purpose: Keep Face Edit and brush face picking on the same selection.
//-----------------------------------------------------------------------------
void CMapBrushEdit::SetParts( std::vector<BrushPart_t> parts )
{
	std::ranges::sort( parts );
	parts.erase( std::ranges::unique( parts ).begin(), parts.end() );
	m_Parts = std::move( parts );
	std::vector<FaceRef_t> faces;
	const auto *pScene = m_Editor.GetDocument().GetScene();
	if ( pScene )
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
	const auto *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene || m_pMode->currentIndex() == 0 )
		return result;
	auto *p2D = qobject_cast<CMapView2D *>( &view );
	auto *p3D = qobject_cast<CMapView3D *>( &view );
	const auto nKind = static_cast<BrushPartKind_t>( m_pMode->currentIndex() - 1 );
	const auto objects = ObjectsOf( *pScene, m_Editor.GetDocument().GetSelection() );
	for ( const auto nSolid : objects.m_Solids )
	{
		const auto mesh = CBrushMesh::FromGeometry( pScene->m_Solids[ nSolid ] );
		const auto nCount = nKind == BrushPartKind_t::BRUSH_PART_VERTEX ? mesh.m_Vertices.size() : nKind == BrushPartKind_t::BRUSH_PART_EDGE ? mesh.GetEdges().size()
																																			 : mesh.m_Faces.size();
		for ( std::uint32_t i = 0; i < nCount; i++ )
		{
			const Point_t point = PartPosition( mesh, nKind, i );
			QPointF at;
			if ( p2D )
				at = p2D->ToWidget( p2D->Flatten( point ) );
			else if ( p3D )
			{
				const auto camera = p3D->GetSizedCamera();
				const auto screen = camera.Project( point );
				if ( !screen )
					continue;
				at = { ( *screen )[ 0 ], ( *screen )[ 1 ] };
				if ( !m_pXray->isChecked() )
				{
					const auto hit = p3D->FaceAt( at );
					if ( hit )
					{
						const auto &shape = pScene->m_Solids[ hit->m_nSolid ];
						const auto *pFace = shape.FindFace( hit->m_nSide );
						const auto [ from, dir ] = camera.Ray( at.x(), at.y() );
						if ( pFace )
						{
							const double d = Dot( pFace->m_Plane.m_Normal, dir );
							if ( std::abs( d ) > 1e-8 && -pFace->m_Plane.Distance( from ) / d < Dot( Subtract( point, from ), dir ) - 0.1 )
								continue;
						}
					}
				}
			}
			result.push_back( { { pScene->SolidObject( nSolid ), nKind, i }, point, at } );
		}
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
		const auto mesh = CBrushMesh::FromGeometry( pScene->m_Solids[ pScene->GetIndex( part.m_nObject ) ] );
		for ( const auto n : CornersOf( mesh, part.m_nKind, part.m_nIndex ) )
			bounds.Add( mesh.m_Vertices[ n ] );
	}
	if ( bounds.IsEmpty() )
		return std::nullopt;
	CMapGizmo gizmo;
	gizmo.Place( bounds.GetCenter(), { { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } } );
	gizmo.SetBounds( bounds );
	gizmo.SetMode( m_Editor.GetGizmoMode() );
	return gizmo;
}

//-----------------------------------------------------------------------------
// Purpose: Draw the same part handles above either rendering backend.
//-----------------------------------------------------------------------------
void CMapBrushEdit::Paint( CMapView &view, QPainter &painter ) const
{
	if ( m_Editor.GetTool() != MapTool_t::MAP_TOOL_BRUSH_EDIT )
		return;
	painter.save();
	if ( qobject_cast<CMapView3D *>( &view ) )
		if ( const auto gizmo = m_DragGizmo ? m_DragGizmo : Gizmo() )
			for ( const auto &line : gizmo->GetLines( Camera( view ), 90 ) )
				if ( const auto projected = Camera( view ).ProjectLine( line.m_From, line.m_To ) )
				{
					const QColor colors[]{ Qt::red, Qt::green, Qt::blue };
					painter.setPen( QPen( colors[ std::clamp( gizmo->GetPartAxis( line.m_nPart ), 0, 2 ) ], 2 ) );
					painter.drawLine( QPointF( projected->first[ 0 ], projected->first[ 1 ] ), QPointF( projected->second[ 0 ], projected->second[ 1 ] ) );
				}
	painter.setPen( Qt::black );
	for ( const auto &handle : Handles( view ) )
	{
		painter.setBrush( std::ranges::find( m_Parts, handle.m_Part ) != m_Parts.end() ? QColor( 255, 80, 70 ) : QColor( 255, 230, 100 ) );
		painter.drawRect( QRectF( handle.m_Screen - QPointF( 3, 3 ), QSizeF( 6, 6 ) ) );
	}
	if ( m_From && m_bBox )
	{
		painter.setBrush( Qt::NoBrush );
		painter.setPen( Qt::white );
		painter.drawRect( QRectF( *m_From, m_At ).normalized() );
	}
	painter.restore();
}

//-----------------------------------------------------------------------------
// Purpose: Convert concave previews according to the persisted preference.
//-----------------------------------------------------------------------------
BrushConcave_t CMapBrushEdit::GetConcave() const
{
	return m_pConcave->currentIndex() == 0 ? BrushConcave_t::BRUSH_CONCAVE_SPLIT : BrushConcave_t::BRUSH_CONCAVE_KEEP;
}

//-----------------------------------------------------------------------------
// Purpose: Discard a drag without adding an undo command.
//-----------------------------------------------------------------------------
void CMapBrushEdit::Cancel()
{
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
	if ( !pScene || m_Parts.empty() )
		return;
	struct Remembered_t
	{
		BrushPartKind_t m_nKind;
		int m_nSide = 0;
		std::vector<Point_t> m_Points;
	};
	std::vector<Remembered_t> remembered;
	for ( const auto &part : m_Parts )
	{
		const auto nSolid = pScene->GetIndex( part.m_nObject );
		const auto mesh = CBrushMesh::FromGeometry( pScene->m_Solids[ nSolid ] );
		Remembered_t item{ part.m_nKind, 0, {} };
		if ( part.m_nKind == BrushPartKind_t::BRUSH_PART_FACE )
			item.m_nSide = pScene->m_Map.m_Solids[ nSolid ].m_Sides[ mesh.m_Faces[ part.m_nIndex ].m_nSide ].m_nId;
		for ( const auto n : CornersOf( mesh, part.m_nKind, part.m_nIndex ) )
			item.m_Points.push_back( transform.Apply( mesh.m_Vertices[ n ] ) );
		remembered.push_back( std::move( item ) );
	}
	std::string szProblem;
	auto edit = MoveParts( *pScene, m_Parts, transform, GetConcave(), nullptr, &szProblem );
	if ( !szProblem.empty() )
	{
		emit m_Editor.StatusMessage( QString::fromStdString( szProblem ) );
		return;
	}
	std::vector<std::uint32_t> solids;
	for ( const auto &[ nSolid, solid ] : edit.m_Solids )
		solids.push_back( nSolid );
	Commit( std::move( edit ), tr( "Transform Brush Parts" ) );
	pScene = m_Editor.GetDocument().GetScene();
	std::vector<BrushPart_t> parts;
	for ( const auto nSolid : solids )
	{
		const auto mesh = CBrushMesh::FromGeometry( pScene->m_Solids[ nSolid ] );
		for ( const auto &item : remembered )
		{
			const auto nCount = item.m_nKind == BrushPartKind_t::BRUSH_PART_VERTEX ? mesh.m_Vertices.size() : item.m_nKind == BrushPartKind_t::BRUSH_PART_EDGE ? mesh.GetEdges().size()
																																							   : mesh.m_Faces.size();
			for ( std::uint32_t i = 0; i < nCount; i++ )
			{
				bool bMatch = false;
				if ( item.m_nKind == BrushPartKind_t::BRUSH_PART_FACE )
					bMatch = pScene->m_Map.m_Solids[ nSolid ].m_Sides[ mesh.m_Faces[ i ].m_nSide ].m_nId == item.m_nSide;
				else
				{
					const auto corners = CornersOf( mesh, item.m_nKind, i );
					bMatch = corners.size() == item.m_Points.size() && std::ranges::all_of( corners, [ & ]( std::uint32_t n ) { return std::ranges::any_of( item.m_Points, [ & ]( const Point_t &p ) { return Length( Subtract( p, mesh.m_Vertices[ n ] ) ) < 0.001; } ); } );
				}
				if ( bMatch )
					parts.push_back( { pScene->SolidObject( nSolid ), item.m_nKind, i } );
			}
		}
	}
	SetParts( std::move( parts ) );
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
	auto *pView = m_Editor.GetActiveView();
	const auto *pScene = m_Editor.GetDocument().GetScene();
	if ( !pView || !pScene || m_Parts.empty() )
		return;
	Box_t bounds;
	for ( const auto &part : m_Parts )
	{
		const auto mesh = CBrushMesh::FromGeometry( pScene->m_Solids[ pScene->GetIndex( part.m_nObject ) ] );
		for ( const auto n : CornersOf( mesh, part.m_nKind, part.m_nIndex ) )
			bounds.Add( mesh.m_Vertices[ n ] );
	}
	if ( bounds.IsEmpty() )
		return;
	m_pBase = m_Editor.GetDocument().GetSharedScene();
	const std::array<Point_t, 3> axes{ { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
	m_Modal.emplace( nMode, bounds.GetCenter(), axes, Camera( *pView ), pView->mapFromGlobal( QCursor::pos() ) );
	pView->setFocus();
	PreviewTransform( *pView, Qt::NoModifier );
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
	const auto edit = MoveParts( *m_pBase, m_Parts, m_Transform, GetConcave(), nullptr, &szProblem );
	if ( szProblem.empty() )
	{
		m_bApplying = true;
		m_Editor.GetDocument().Preview( edit );
		m_bApplying = false;
		emit m_Editor.StatusMessage( m_Modal->GetDescription() );
	}
	else
		emit m_Editor.StatusMessage( QString::fromStdString( szProblem ) );
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose: Pick and box-select parts, then preview a snapped drag on the view's plane.
//-----------------------------------------------------------------------------
bool CMapBrushEdit::HandleEvent( CMapView &view, QEvent *pEvent )
{
	if ( m_Editor.GetTool() != MapTool_t::MAP_TOOL_BRUSH_EDIT || m_pMode->currentIndex() == 0 )
		return false;
	if ( m_Modal )
	{
		if ( pEvent->type() == QEvent::KeyPress )
		{
			auto *pKey = static_cast<QKeyEvent *>( pEvent );
			if ( pKey->key() == Qt::Key_Escape )
			{
				Cancel();
				return true;
			}
			if ( pKey->key() == Qt::Key_Enter || pKey->key() == Qt::Key_Return )
			{
				const auto transform = m_Transform;
				Cancel();
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
				Cancel();
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
		if ( pKey->key() == Qt::Key_E && pKey->modifiers() == Qt::AltModifier )
		{
			BeginTransform( MapGizmoMode_t::MAP_GIZMO_MODE_SCALE );
			return true;
		}
		if ( pKey->key() == Qt::Key_F && pKey->modifiers() == Qt::ControlModifier )
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
		if ( pMouse->button() != Qt::LeftButton )
			return false;
		view.setFocus();
		m_Editor.SetActiveView( &view );
		m_From = pMouse->position();
		m_At = *m_From;
		m_bBox = true;
		if ( qobject_cast<CMapView3D *>( &view ) )
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
		const auto handles = Handles( view );
		const Handle_t *pNearest = nullptr;
		double flBest = 100;
		for ( const auto &handle : handles )
		{
			const QPointF d = handle.m_Screen - *m_From;
			const double flDistance = QPointF::dotProduct( d, d );
			if ( flDistance < flBest )
			{
				flBest = flDistance;
				pNearest = &handle;
			}
		}
		if ( pNearest )
		{
			auto parts = m_Parts;
			if ( pMouse->modifiers().testFlag( Qt::ControlModifier ) )
				std::erase( parts, pNearest->m_Part );
			else
			{
				if ( !pMouse->modifiers().testFlag( Qt::ShiftModifier ) && std::ranges::find( parts, pNearest->m_Part ) == parts.end() )
					parts.clear();
				parts.push_back( pNearest->m_Part );
			}
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
		if ( m_pBase && !m_bBox )
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
					const auto mesh = CBrushMesh::FromGeometry( m_pBase->m_Solids[ nSolid ] );
					std::vector<Point_t> targets = mesh.m_Vertices;
					for ( const auto &[ a, b ] : mesh.GetEdges() )
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
			const auto edit = MoveParts( *m_pBase, m_Parts, m_Transform, GetConcave(), nullptr, &szProblem );
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
				if ( pMouse->modifiers().testFlag( Qt::ControlModifier ) )
					std::erase( parts, handle.m_Part );
				else
					parts.push_back( handle.m_Part );
			}
		SetParts( std::move( parts ) );
		Cancel();
	}
	else
	{
		const auto transform = m_Transform;
		Cancel();
		if ( !transform.IsIdentity() )
			Transform( transform );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Apply topology changes to selected solids as a single undoable edit.
//-----------------------------------------------------------------------------
void CMapBrushEdit::ApplyOperation( int nOperation )
{
	const auto *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene || m_Parts.empty() )
		return;
	MapEdit_t edit;
	int nSide = pScene->m_nNextSideId;
	std::map<std::uint32_t, std::vector<BrushPart_t>> objects;
	for ( const auto &part : m_Parts )
		objects[ part.m_nObject ].push_back( part );
	for ( const auto &[ nObject, parts ] : objects )
	{
		const auto nSolid = pScene->GetIndex( nObject );
		const auto &source = pScene->m_Map.m_Solids[ nSolid ];
		if ( std::ranges::any_of( source.m_Sides, []( const Side_t &side ) { return bool( side.m_pDisp ); } ) )
		{
			emit m_Editor.StatusMessage( tr( "This operation cannot modify a displacement brush." ) );
			return;
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
		else if ( nOperation == 1 )
		{
			for ( const auto &part : parts )
				if ( part.m_nKind == BrushPartKind_t::BRUSH_PART_FACE )
					bChanged = InsetFace( mesh, part.m_nIndex, m_pDistance->value() ) || bChanged;
		}
		else if ( nOperation >= 2 && nOperation <= 4 )
		{
			Point_t normal{};
			normal[ nOperation - 2 ] = 1;
			bChanged = CutRing( mesh, { normal, Dot( normal, pScene->m_Solids[ nSolid ].m_Bounds.GetCenter() ) + m_pDistance->value() } );
		}
		if ( !bChanged )
		{
			emit m_Editor.StatusMessage( tr( "The selected parts cannot perform this operation." ) );
			return;
		}
		auto result = ToSolids( pScene->m_Map, source, mesh, GetConcave() );
		if ( result.m_Solids.size() != 1 )
		{
			emit m_Editor.StatusMessage( QString::fromStdString( result.m_szProblem ) );
			return;
		}
		for ( auto &side : result.m_Solids.front().m_Sides )
			if ( side.m_nId == 0 )
				side.m_nId = nSide++;
		edit.m_Solids.emplace_back( nSolid, std::move( result.m_Solids.front() ) );
	}
	Commit( std::move( edit ), tr( "Edit Brush" ) );
}
