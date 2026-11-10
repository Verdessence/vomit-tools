//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Mouse previews, operation parameters and repeatable undo steps for brush modeling.
//===========================================================================//
#include "MapBrushEdit.h"
#include "map/MapEditor.h"
#include "map/MapView2D.h"
#include "map/MapView3D.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QUndoStack>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
using namespace xv::map;

namespace
{
//-----------------------------------------------------------------------------
// Purpose: Snap a point to the two surface grid directions while retaining its plane.
//-----------------------------------------------------------------------------
Point_t SnapModelPoint( Point_t point, const Plane_t &plane, int nGrid )
{
	int nAxis = 0;
	for ( int i = 1; i < 3; ++i )
		if ( std::abs( plane.m_Normal[ i ] ) > std::abs( plane.m_Normal[ nAxis ] ) )
			nAxis = i;
	if ( nGrid <= 0 || std::abs( plane.m_Normal[ nAxis ] ) < 1e-8 )
		return point;
	double flDistance = plane.m_flDistance;
	for ( int i = 0; i < 3; ++i )
		if ( i != nAxis )
		{
			point[ i ] = std::round( point[ i ] / nGrid ) * nGrid;
			flDistance -= point[ i ] * plane.m_Normal[ i ];
		}
	point[ nAxis ] = flDistance / plane.m_Normal[ nAxis ];
	return point;
}
} // namespace

//-----------------------------------------------------------------------------
// Purpose: Use the same operation names in the palette, status text and undo history.
//-----------------------------------------------------------------------------
QString CMapBrushEdit::OperationName( int nOperation )
{
	if ( nOperation == 0 )
		return tr( "Split Face" );
	if ( nOperation == 1 )
		return tr( "Inset" );
	if ( nOperation >= 2 && nOperation <= 4 )
		return tr( "Cut on %1" ).arg( QChar( 'X' + nOperation - 2 ) );
	const QStringList names{ tr( "Extrude" ), tr( "Bevel" ), tr( "Loop Cut" ), tr( "Knife" ), tr( "Dissolve" ), tr( "Convex Hull" ), tr( "Intersect" ), tr( "Bridge" ), tr( "Mirror" ), tr( "Array" ), tr( "Spin" ), tr( "Flatten" ), tr( "Make Face Axis-Aligned" ), tr( "Inset" ) };
	return nOperation >= 5 && nOperation < 5 + names.size() ? names[ nOperation - 5 ] : tr( "Edit Brush" );
}
//-----------------------------------------------------------------------------
// Purpose: Keep mouse-driven operations optional to type and adjustable after their commit.
//-----------------------------------------------------------------------------
void CMapBrushEdit::CreateModelControls( QFormLayout &layout )
{
	m_pModelControls = new QGroupBox( tr( "Operation / adjust last" ), this );
	m_pModelControls->setObjectName( QStringLiteral( "brushOperationControls" ) );
	auto *pForm = new QFormLayout( m_pModelControls );
	auto number = [ & ]( const char *pszName, double flValue, double flMin = -16384, double flMax = 16384 ) {auto *pSpin=new QDoubleSpinBox(m_pModelControls);pSpin->setObjectName(QString::fromLatin1(pszName));pSpin->setRange(flMin,flMax);pSpin->setDecimals(3);pSpin->setValue(flValue);connect(pSpin,&QDoubleSpinBox::valueChanged,this,[this]{ModelParametersChanged();});connect(pSpin,&QDoubleSpinBox::editingFinished,this,[this]{if(m_bAdjustingLast)AdjustLastOperation(true);});return pSpin; };
	m_pModelDepth = number( "brushInsetDepth", 0 );
	pForm->addRow( tr( "Depth (Ctrl-drag)" ), m_pModelDepth );
	m_pModelCount = new QSpinBox( m_pModelControls );
	m_pModelCount->setObjectName( QStringLiteral( "brushOperationCount" ) );
	m_pModelCount->setRange( 1, 64 );
	m_pModelCount->setValue( 1 );
	pForm->addRow( tr( "Segments / copies" ), m_pModelCount );
	connect( m_pModelCount, &QSpinBox::valueChanged, this, [ this ] { ModelParametersChanged(); } );
	connect( m_pModelCount, &QSpinBox::editingFinished, this, [ this ] {if(m_bAdjustingLast)AdjustLastOperation(true); } );
	m_pModelAngle = number( "brushSpinAngle", 45, -36000, 36000 );
	pForm->addRow( tr( "Angle per copy" ), m_pModelAngle );
	m_pModelSlide = number( "brushLoopSlide", 0, -0.99999999, 0.99999999 );
	m_pModelSlide->setDecimals( 8 );
	pForm->addRow( tr( "Slide" ), m_pModelSlide );
	m_pModelAxis = new QComboBox( m_pModelControls );
	m_pModelAxis->setObjectName( QStringLiteral( "brushOperationAxis" ) );
	m_pModelAxis->addItems( { tr( "X" ), tr( "Y" ), tr( "Z" ), tr( "View plane" ) } );
	m_pModelAxis->setCurrentIndex( 2 );
	pForm->addRow( tr( "Axis" ), m_pModelAxis );
	connect( m_pModelAxis, &QComboBox::currentIndexChanged, this, [ this ] {
		if ( !m_bUpdatingModelControls && m_pModelAxis->currentIndex() == 3 )
			if ( auto *pView = m_Editor.GetActiveView() )
				m_ModelParams.m_Plane.m_Normal = Camera( *pView ).GetForward();
		ModelParametersChanged();
	} );
	for ( int i = 0; i < 3; ++i )
	{
		m_pModelOffsets[ i ] = number( qPrintable( QStringLiteral( "brushCopyOffset%1" ).arg( i ) ), i == 0 ? 64 : 0 );
		pForm->addRow( tr( "Offset %1" ).arg( QChar( 'X' + i ) ), m_pModelOffsets[ i ] );
	}
	auto toggle = [ & ]( const char *pszName, const QString &szText, bool bChecked = false ) {auto *pCheck=new QCheckBox(szText,m_pModelControls);pCheck->setObjectName(QString::fromLatin1(pszName));pCheck->setChecked(bChecked);pForm->addRow(pCheck);connect(pCheck,&QCheckBox::toggled,this,[this]{ModelParametersChanged();});return pCheck; };
	m_pModelNewBrush = toggle( "brushExtrudeNew", tr( "Extrude as a new brush" ) );
	m_pModelRegion = toggle( "brushInsetRegion", tr( "Inset as one coplanar region" ) );
	m_pModelSquare = toggle( "brushInsetSquare", tr( "Square inset corners" ) );
	m_pModelBestPlane = toggle( "brushFlattenBest", tr( "Fit the plane / nearest axis" ), true );
	m_pModelWholeBrush = toggle( "brushBevelAll", tr( "Bevel all edges of the brushes" ) );
	m_pModelCommitControls = new QWidget( m_pModelControls );
	auto *pButtons = new QHBoxLayout( m_pModelCommitControls );
	pButtons->setContentsMargins( 0, 0, 0, 0 );
	m_pModelApply = new QPushButton( tr( "Apply" ), m_pModelCommitControls );
	m_pModelApply->setObjectName( QStringLiteral( "brushOperationApply" ) );
	auto *pCancel = new QPushButton( tr( "Cancel" ), m_pModelCommitControls );
	pCancel->setObjectName( QStringLiteral( "brushOperationCancel" ) );
	pButtons->addWidget( m_pModelApply );
	pButtons->addWidget( pCancel );
	pForm->addRow( m_pModelCommitControls );
	connect( m_pModelApply, &QPushButton::clicked, this, [ this ] { CommitPendingOperation(); } );
	connect( pCancel, &QPushButton::clicked, this, [ this ] { Cancel(); } );
	layout.addRow( m_pModelControls );
	RefreshModelControls();
}
//-----------------------------------------------------------------------------
// Purpose: Read one parameter record for previews, commits and reconstruction of the last edit.
//-----------------------------------------------------------------------------
BrushModelParams_t CMapBrushEdit::GetModelParameters() const
{
	auto params = m_ModelParams;
	params.m_flAmount = m_pDistance->value();
	params.m_flDepth = m_pModelDepth->value();
	params.m_flAngle = m_pModelAngle->value();
	params.m_flSlide = 0;
	params.m_nCount = m_pModelCount->value();
	params.m_nAxis = m_pModelAxis->currentIndex();
	for ( int i = 0; i < 3; ++i )
		params.m_Offset[ i ] = m_pModelOffsets[ i ]->value();
	params.m_bNewBrush = m_pModelNewBrush->isChecked();
	params.m_bRegion = m_pModelRegion->isChecked();
	params.m_bSquareCorners = m_pModelSquare->isChecked();
	params.m_bBestPlane = m_pModelBestPlane->isChecked();
	params.m_bAllEdges = m_pModelWholeBrush->isChecked();
	params.m_nConcave = GetConcave();
	if ( !params.m_bUsePivot )
		if ( auto pivot = m_Editor.GetPivot() )
		{
			params.m_Pivot = *pivot;
			params.m_bUsePivot = true;
		}
	if ( params.m_nAxis == 3 && m_nOperation != 8 && !m_pBase && !m_bAdjustingLast )
		if ( auto *pView = m_Editor.GetActiveView() )
			params.m_Plane.m_Normal = Camera( *pView ).GetForward();
	return params;
}
//-----------------------------------------------------------------------------
// Purpose: Restore operation values without accidentally starting another adjustment.
//-----------------------------------------------------------------------------
void CMapBrushEdit::SetModelParameters( const BrushModelParams_t &params )
{
	m_bUpdatingModelControls = true;
	m_ModelParams = params;
	m_pDistance->setValue( params.m_flAmount );
	m_pModelDepth->setValue( params.m_flDepth );
	m_pModelAngle->setValue( params.m_flAngle );
	m_pModelSlide->setValue( std::clamp( params.m_flSlide, -0.99999999, 0.99999999 ) );
	m_pModelCount->setValue( params.m_nCount );
	m_pModelAxis->setCurrentIndex( std::clamp( params.m_nAxis, 0, 3 ) );
	for ( int i = 0; i < 3; ++i )
		m_pModelOffsets[ i ]->setValue( params.m_Offset[ i ] );
	m_pModelNewBrush->setChecked( params.m_bNewBrush );
	m_pModelRegion->setChecked( params.m_bRegion );
	m_pModelSquare->setChecked( params.m_bSquareCorners );
	m_pModelBestPlane->setChecked( params.m_bBestPlane );
	m_pModelWholeBrush->setChecked( params.m_bAllEdges );
	m_bUpdatingModelControls = false;
	RefreshModelControls();
}
//-----------------------------------------------------------------------------
// Purpose: Show only the parameters meaningful to the active or last operation.
//-----------------------------------------------------------------------------
void CMapBrushEdit::RefreshModelControls()
{
	if ( !m_pModelControls )
		return;
	const int nOperation = m_bAdjustingLast ? m_nLastOperation : m_nOperation;
	auto *pForm = qobject_cast<QFormLayout *>( m_pModelControls->layout() );
	const bool bInset = nOperation == 1 || nOperation == 18, bCount = nOperation == 6 || nOperation == 7 || nOperation == 14 || nOperation == 15, bCopies = nOperation == 14 || nOperation == 15;
	pForm->setRowVisible( m_pModelDepth, bInset );
	pForm->setRowVisible( m_pModelCount, bCount );
	pForm->setRowVisible( m_pModelAngle, nOperation == 15 );
	pForm->setRowVisible( m_pModelSlide, false );
	pForm->setRowVisible( m_pModelAxis, nOperation == 13 || nOperation == 15 || nOperation == 16 || nOperation == 17 );
	for ( auto *pOffset : m_pModelOffsets )
		pForm->setRowVisible( pOffset, bCopies );
	pForm->setRowVisible( m_pModelNewBrush, nOperation == 5 );
	pForm->setRowVisible( m_pModelRegion, bInset );
	pForm->setRowVisible( m_pModelSquare, bInset );
	pForm->setRowVisible( m_pModelBestPlane, nOperation == 16 || nOperation == 17 );
	pForm->setRowVisible( m_pModelWholeBrush, nOperation == 6 );
	m_pModelControls->setVisible( nOperation >= 1 );
	pForm->setRowVisible( m_pModelCommitControls, bool( m_pBase ) || m_nOperation >= 9 );
	m_pModelApply->setEnabled( m_pBase ? m_bOperationValid : ( m_nOperation >= 9 && !m_Editor.GetDocument().GetSelection().empty() ) );
	m_pModelControls->setEnabled( m_nOperation >= 0 || m_bAdjustingLast );
	if ( auto *pFormRoot = qobject_cast<QFormLayout *>( layout() ) )
	{
		pFormRoot->setRowVisible( m_pDistance, bInset || ( nOperation >= 2 && nOperation <= 6 ) || ( nOperation == 16 && !m_pModelBestPlane->isChecked() ) );
		if ( auto *pLabel = qobject_cast<QLabel *>( pFormRoot->labelForField( m_pDistance ) ) )
			pLabel->setText( nOperation >= 2 && nOperation <= 4 ? tr( "Cut position" ) : tr( "Amount / last amount" ) );
	}
	m_pModelControls->setTitle( m_bAdjustingLast ? tr( "Adjust last: %1" ).arg( OperationName( nOperation ) ) : OperationName( nOperation ) );
	if ( m_nOperation < 0 && !m_bAdjustingLast )
		m_pDistance->setEnabled( false );
}
//-----------------------------------------------------------------------------
// Purpose: Rebuild a live preview or replace the last operation when a field is edited.
//-----------------------------------------------------------------------------
void CMapBrushEdit::ModelParametersChanged()
{
	if ( m_bUpdatingModelControls )
		return;
	if ( m_pBase && ( m_nOperation == 1 || m_nOperation >= 5 ) )
	{
		UpdateModelPreview();
		return;
	}
	if ( m_bAdjustingLast )
		AdjustLastOperation();
	RefreshModelControls();
	Refresh();
}
//-----------------------------------------------------------------------------
// Purpose: Retain the original camera anchor and snapshot throughout an operation's gesture.
//-----------------------------------------------------------------------------
void CMapBrushEdit::BeginModelPreview( CMapView &view, QPointF at )
{
	if ( m_pBase )
		return;
	m_bAdjustingLast = false;
	m_szModelNumber.clear();
	m_From = at;
	m_At = at;
	m_LastModelPointer = at;
	m_flModelPointerAmount = 0;
	m_pOperationView = &view;
	if ( const auto gizmo = Gizmo() )
		m_Anchor = gizmo->GetPivot();
	else
		m_Anchor = m_Editor.GetDocument().GetSelectionBounds().GetCenter();
	const auto params = GetModelParameters();
	m_ModelParams.m_bUsePivot = params.m_bUsePivot;
	m_ModelParams.m_Pivot = params.m_Pivot;
	m_ModelParams.m_Plane = params.m_Plane;
	m_OperationObjects = m_Editor.GetDocument().GetSelection();
	m_pBase = m_Editor.GetDocument().GetSharedScene();
	view.setFocus();
	m_Editor.SetActiveView( &view );
	if ( m_nOperation == 8 )
	{
		m_ModelKnifeNormal = Camera( view ).GetForward();
		if ( auto *p3D = qobject_cast<CMapView3D *>( &view ) )
		{
			if ( const auto hit = p3D->FaceAt( at ) )
			{
				if ( const auto *pFace = m_pBase->m_Solids[ hit->m_nSolid ].FindFace( hit->m_nSide ) )
				{
					const auto [ from, direction ] = Camera( view ).Ray( at.x(), at.y() );
					const double d = Dot( direction, pFace->m_Plane.m_Normal );
					if ( std::abs( d ) > 1e-8 )
						m_Anchor = Add( from, Scale( direction, -pFace->m_Plane.Distance( from ) / d ) );
					m_ModelKnifeNormal = pFace->m_Plane.m_Normal;
				}
			}
		}
		if ( qobject_cast<CMapView2D *>( &view ) )
			if ( auto point = PointerPoint( view, at, m_Anchor ) )
				m_Anchor = *point;
		if ( m_Editor.GetSettings().m_bSnapToGrid )
			m_Anchor = SnapModelPoint( m_Anchor, { m_ModelKnifeNormal, Dot( m_ModelKnifeNormal, m_Anchor ) }, m_Editor.GetSettings().m_nGridSize );
		m_ModelEnd = m_Anchor;
		return;
	}
	if ( m_nOperation == 5 || m_nOperation == 6 )
	{
		m_bUpdatingModelControls = true;
		m_pDistance->setValue( 0 );
		m_bUpdatingModelControls = false;
		return;
	}
	UpdateModelPreview();
}
//-----------------------------------------------------------------------------
// Purpose: Render a prospective edit from the original scene without touching undo history.
//-----------------------------------------------------------------------------
void CMapBrushEdit::UpdateModelPreview()
{
	if ( !m_pBase )
		return;
	auto edit = BuildOperation( m_nOperation );
	m_bOperationValid = !edit.IsEmpty();
	m_bApplying = true;
	if ( m_bOperationValid )
		m_Editor.GetDocument().Preview( edit );
	else
		m_Editor.GetDocument().EndPreview();
	m_bApplying = false;
	if ( m_bOperationValid )
		emit m_Editor.StatusMessage( ( m_nOperation >= 9 ? tr( "%1 — select parts; Apply or Enter commits, Escape cancels." ) : tr( "%1 — click or Enter to apply; Escape cancels." ) ).arg( OperationName( m_nOperation ) ) );
	RefreshModelControls();
	Refresh();
}
//-----------------------------------------------------------------------------
// Purpose: Measure a modal width or distance in world units, with precision and optional typed input.
//-----------------------------------------------------------------------------
void CMapBrushEdit::MoveModelPointer( CMapView &view, QPointF at, Qt::KeyboardModifiers modifiers )
{
	if ( !m_pBase || !m_From )
		return;
	m_At += ( at - m_LastModelPointer ) * ( modifiers.testFlag( Qt::ShiftModifier ) ? 0.1 : 1.0 );
	m_LastModelPointer = at;
	if ( m_nOperation == 8 )
	{
		if ( qobject_cast<CMapView2D *>( &view ) )
		{
			if ( auto point = PointerPoint( view, m_At, m_Anchor ) )
				m_ModelEnd = *point;
		}
		else
		{
			const auto [ from, direction ] = Camera( view ).Ray( m_At.x(), m_At.y() );
			const double d = Dot( direction, m_ModelKnifeNormal );
			if ( std::abs( d ) > 1e-8 )
				m_ModelEnd = Add( from, Scale( direction, Dot( Subtract( m_Anchor, from ), m_ModelKnifeNormal ) / d ) );
		}
		const auto &settings = m_Editor.GetSettings();
		if ( settings.m_bSnapToGrid && settings.m_nGridSize > 0 && !modifiers.testFlag( Qt::AltModifier ) )
			m_ModelEnd = SnapModelPoint( m_ModelEnd, { m_ModelKnifeNormal, Dot( m_ModelKnifeNormal, m_Anchor ) }, settings.m_nGridSize );
		const auto normal = Cross( m_ModelKnifeNormal, Subtract( m_ModelEnd, m_Anchor ) );
		if ( Length( normal ) < 1e-6 )
		{
			m_bOperationValid = false;
			Refresh();
			return;
		}
		m_ModelParams.m_Plane = { Normalized( normal ), Dot( Normalized( normal ), m_Anchor ) };
		UpdateModelPreview();
		return;
	}
	if ( m_nOperation != 5 && m_nOperation != 6 )
		return;
	const auto centre = Project( view, m_Anchor );
	if ( !centre )
		return;
	const auto unit = PointerPoint( view, *centre + QPointF( 1, 0 ), m_Anchor );
	double flUnits = unit ? Length( Subtract( *unit, m_Anchor ) ) : 1;
	double flAmount = ( m_At.x() - m_From->x() ) * flUnits;
	if ( m_nOperation == 5 && !m_Parts.empty() )
	{
		const auto &part = m_Parts.front();
		const auto pMeshParts = MeshOf( *m_pBase, m_pBase->GetIndex( part.m_nObject ) );
		const CBrushMesh &mesh = pMeshParts->m_Mesh;
		if ( part.m_nKind == BrushPartKind_t::BRUSH_PART_FACE )
		{
			const auto normal = mesh.FacePlane( part.m_nIndex )->m_Normal;
			const auto end = Project( view, Add( m_Anchor, Scale( normal, 32 ) ) );
			if ( end && QLineF( *centre, *end ).length() > 0.01 )
			{
				const auto delta = *end - *centre;
				flAmount = QPointF::dotProduct( m_At - *m_From, delta ) * 32 / QPointF::dotProduct( delta, delta );
			}
			else
				flAmount = ( m_From->y() - m_At.y() ) * flUnits;
		}
	}
	if ( m_nOperation == 6 )
		flAmount = std::max( 0.0, flAmount );
	const auto &settings = m_Editor.GetSettings();
	if ( settings.m_bSnapToGrid && !modifiers.testFlag( Qt::AltModifier ) && settings.m_nGridSize > 0 )
		flAmount = std::round( flAmount / settings.m_nGridSize ) * settings.m_nGridSize;
	m_flModelPointerAmount = flAmount;
	if ( !m_szModelNumber.isEmpty() )
	{
		bool bOk = false;
		const double flTyped = m_szModelNumber.toDouble( &bOk );
		if ( bOk )
			flAmount = flTyped;
	}
	m_bUpdatingModelControls = true;
	m_pDistance->setValue( flAmount );
	m_bUpdatingModelControls = false;
	UpdateModelPreview();
}
//-----------------------------------------------------------------------------
// Purpose: Choose an actual edge ring from the brush's original topology while hovering.
//-----------------------------------------------------------------------------
bool CMapBrushEdit::PickModelRing( CMapView &view, QPointF at, Qt::KeyboardModifiers )
{
	if ( m_pBase )
		Cancel( true );
	const auto *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene )
		return false;
	std::optional<BrushPart_t> nearest;
	double flBest = 144;
	for ( const auto &handle : Handles( view ) )
	{
		if ( handle.m_Part.m_nKind != BrushPartKind_t::BRUSH_PART_EDGE )
			continue;
		const auto pMeshParts = MeshOf( *pScene, pScene->GetIndex( handle.m_Part.m_nObject ) );
		const CBrushMesh &mesh = pMeshParts->m_Mesh;
		const auto edge = pMeshParts->m_Edges[ handle.m_Part.m_nIndex ];
		auto a = Project( view, mesh.m_Vertices[ edge.first ] ), b = Project( view, mesh.m_Vertices[ edge.second ] );
		if ( !a || !b )
			continue;
		const auto direction = *b - *a;
		const double length = QPointF::dotProduct( direction, direction );
		if ( length < 1e-6 )
			continue;
		const double t = std::clamp( QPointF::dotProduct( at - *a, direction ) / length, 0.0, 1.0 );
		const auto delta = at - ( *a + direction * t );
		const double distance = QPointF::dotProduct( delta, delta );
		if ( distance < flBest )
		{
			flBest = distance;
			nearest = handle.m_Part;
		}
	}
	if ( !nearest )
		return false;
	SetParts( { *nearest } );
	BeginModelPreview( view, at );
	m_bUpdatingModelControls = true;
	m_pModelSlide->setValue( 0 );
	m_bUpdatingModelControls = false;
	UpdateModelPreview();
	return true;
}
//-----------------------------------------------------------------------------
// Purpose: Route each operation's readiness, preview, optional typing, commit and cancellation.
//-----------------------------------------------------------------------------
bool CMapBrushEdit::HandleModelEvent( CMapView &view, QEvent *pEvent )
{
	if ( pEvent->type() == QEvent::KeyPress )
	{
		auto *pKey = static_cast<QKeyEvent *>( pEvent );
		const auto nAction = MapActionFor( *pKey, MapKeyContext_t::MAP_KEY_CONTEXT_BRUSH );
		if ( !m_Modal && !m_From && nAction == MapAction_t::MAP_ACTION_BRUSH_EXTRUDE )
		{
			if ( m_nOperation == 5 )
				Cancel();
			ApplyOperation( 5 );
			if ( m_nOperation == 5 && !m_Parts.empty() )
				BeginModelPreview( view, view.mapFromGlobal( QCursor::pos() ) );
			return true;
		}
		if ( !m_Modal && !m_From && nAction == MapAction_t::MAP_ACTION_BRUSH_KNIFE )
		{
			ApplyOperation( 8 );
			return true;
		}
		if ( m_nOperation == 1 && nAction == MapAction_t::MAP_ACTION_BRUSH_INSET )
		{
			m_pModelRegion->setChecked( !m_pModelRegion->isChecked() );
			return true;
		}
	}
	const bool bHandleTool = m_nOperation == 1 || m_nOperation == 5 || m_nOperation == 6;
	if ( bHandleTool && !m_pOperationView && ( pEvent->type() == QEvent::MouseMove || pEvent->type() == QEvent::MouseButtonPress || pEvent->type() == QEvent::MouseButtonRelease ) )
	{
		if ( pEvent->type() == QEvent::MouseButtonPress && !m_From )
		{
			auto *pMouse = static_cast<QMouseEvent *>( pEvent );
			if ( GetMode() == 1 )
				for ( const auto &handle : Handles( view ) )
					if ( QLineF( handle.m_Screen, pMouse->position() ).length() <= 6 )
						return false;
			if ( pMouse->button() == Qt::LeftButton )
				if ( auto centre = ModelHandleCenter( view ); centre && std::abs( QLineF( *centre, pMouse->position() ).length() - 36 ) <= 7 )
				{
					if ( m_nOperation == 1 )
						PreviewOperation( view, pMouse->position(), pMouse->modifiers() );
					else
						BeginModelPreview( view, pMouse->position() );
					m_bModelHandleDrag = bool( m_pOperationView );
					RefreshModelControls();
					Refresh();
					return true;
				}
		}
		return false;
	}
	if ( m_nOperation >= 9 && pEvent->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent *>( pEvent )->button() == Qt::LeftButton )
	{
		if ( m_pBase )
			Cancel( true );
		return false;
	}
	if ( m_nOperation >= 5 && m_From && !m_pOperationView )
		return false;
	if ( m_nOperation < 5 && m_nOperation != 1 )
		return false;
	if ( m_pOperationView && m_pOperationView != &view )
		return m_nOperation >= 5;
	if ( pEvent->type() == QEvent::KeyPress )
	{
		auto *pKey = static_cast<QKeyEvent *>( pEvent );
		if ( pKey->key() == Qt::Key_Escape )
		{
			if ( m_pBase )
			{
				Cancel( bHandleTool );
			}
			else
				Cancel();
			return true;
		}
		if ( ( pKey->key() == Qt::Key_Return || pKey->key() == Qt::Key_Enter ) && ( m_pBase || m_nOperation >= 9 ) )
		{
			CommitPendingOperation();
			return true;
		}
		if ( ( m_nOperation == 13 || m_nOperation == 15 || m_nOperation == 16 || m_nOperation == 17 ) && ( pKey->key() == Qt::Key_X || pKey->key() == Qt::Key_Y || pKey->key() == Qt::Key_Z ) )
		{
			m_pModelAxis->setCurrentIndex( pKey->key() - Qt::Key_X );
			if ( m_pBase )
				UpdateModelPreview();
			return true;
		}
		if ( m_pBase && ( m_nOperation == 1 || m_nOperation == 5 || m_nOperation == 6 ) )
		{
			bool bNumber = false;
			if ( pKey->key() == Qt::Key_Backspace )
			{
				m_szModelNumber.chop( 1 );
				bNumber = true;
			}
			else
				for ( QChar ch : pKey->text() )
					if ( ch.isDigit() || ch == '-' || ch == '.' )
					{
						if ( ch == '-' && !m_szModelNumber.isEmpty() )
							continue;
						if ( ch == '.' && m_szModelNumber.contains( '.' ) )
							continue;
						m_szModelNumber += ch;
						bNumber = true;
					}
			if ( bNumber )
			{
				bool bOk = false;
				const double value = m_szModelNumber.toDouble( &bOk );
				m_bUpdatingModelControls = true;
				if ( bOk )
					m_pDistance->setValue( value );
				else if ( m_szModelNumber.isEmpty() )
					m_pDistance->setValue( m_flModelPointerAmount );
				m_bUpdatingModelControls = false;
				UpdateModelPreview();
				return true;
			}
		}
		return m_nOperation >= 5 && bool( m_pBase );
	}

	if ( pEvent->type() == QEvent::Wheel && ( ( m_nOperation == 6 && m_pOperationView ) || m_nOperation == 7 ) )
	{
		auto *pWheel = static_cast<QWheelEvent *>( pEvent );
		m_pModelCount->setValue( m_pModelCount->value() + ( pWheel->angleDelta().y() > 0 ? 1 : -1 ) );
		if ( m_pBase && m_nOperation == 7 )
			PickModelRing( view, m_LastModelPointer, pWheel->modifiers() );
		else if ( m_pBase )
			UpdateModelPreview();
		return true;
	}
	if ( pEvent->type() == QEvent::Wheel && m_pBase )
		return true;
	if ( pEvent->type() != QEvent::MouseMove && pEvent->type() != QEvent::MouseButtonPress && pEvent->type() != QEvent::MouseButtonRelease )
		return false;
	auto *pMouse = static_cast<QMouseEvent *>( pEvent );
	if ( pEvent->type() == QEvent::MouseButtonPress && pMouse->button() == Qt::RightButton )
	{
		if ( m_pBase )
		{
			Cancel( bHandleTool );
		}
		else
			Cancel();
		return true;
	}
	if ( pEvent->type() == QEvent::MouseButtonRelease )
	{
		if ( m_bModelHandleDrag && pMouse->button() == Qt::LeftButton )
		{
			if ( m_nOperation == 1 )
				PreviewOperation( view, pMouse->position(), pMouse->modifiers() );
			else
				MoveModelPointer( view, pMouse->position(), pMouse->modifiers() );
			FinishOperation( true );
			return true;
		}
		return bool( m_pBase );
	}
	if ( pEvent->type() == QEvent::MouseMove )
	{
		if ( m_bWaitForClick || ( !m_pBase && pMouse->buttons() != Qt::NoButton ) )
			return false;
		if ( m_nOperation == 7 )
		{
			PickModelRing( view, pMouse->position(), pMouse->modifiers() );
			return true;
		}
		if ( m_pBase )
		{
			if ( m_nOperation == 1 )
				PreviewOperation( view, pMouse->position(), pMouse->modifiers() );
			else
				MoveModelPointer( view, pMouse->position(), pMouse->modifiers() );
			return true;
		}
		if ( m_nOperation == 8 )
			return true;
		if ( ( m_nOperation == 5 || m_nOperation == 9 || m_nOperation == 12 || m_nOperation == 16 || m_nOperation == 17 ) && m_Parts.empty() )
			return false;
		if ( m_nOperation == 6 && m_Parts.empty() && !m_pModelWholeBrush->isChecked() && GetMode() != 0 )
			return false;
		if ( m_Editor.GetDocument().GetSelection().empty() )
			return false;
		BeginModelPreview( view, pMouse->position() );
		return true;
	}
	if ( pMouse->button() != Qt::LeftButton )
		return bool( m_pBase );
	if ( m_pBase )
	{
		if ( m_nOperation == 5 || m_nOperation == 6 || m_nOperation == 8 )
			MoveModelPointer( view, pMouse->position(), pMouse->modifiers() );
		FinishOperation( true );
		return true;
	}
	m_bWaitForClick = false;
	if ( m_nOperation == 7 )
		return PickModelRing( view, pMouse->position(), pMouse->modifiers() );
	if ( m_nOperation == 8 || !m_Parts.empty() || ( ( m_nOperation == 6 || ( m_nOperation >= 10 && m_nOperation <= 15 ) ) && !m_Editor.GetDocument().GetSelection().empty() ) )
	{
		BeginModelPreview( view, pMouse->position() );
		return true;
	}
	return false;
}
//-----------------------------------------------------------------------------
// Purpose: Show a knife line, new ring edges and the current amount above either renderer.
//-----------------------------------------------------------------------------
void CMapBrushEdit::PaintModel( CMapView &view, QPainter &painter ) const
{
	if ( m_Editor.GetTool() != MapTool_t::MAP_TOOL_BRUSH_EDIT || m_nOperation < 2 || !m_pBase )
		return;
	painter.save();
	painter.setPen( QPen( m_nOperation <= 4 ? QColor( 255, 180, 50 ) : QColor( 255, 190, 40 ), 2 ) );
	auto line = [ & ]( Point_t a, Point_t b ) {if(auto *p2D=qobject_cast<CMapView2D *>(&view))painter.drawLine(p2D->ToWidget(p2D->Flatten(a)),p2D->ToWidget(p2D->Flatten(b)));else if(auto projected=Camera(view).ProjectLine(a,b))painter.drawLine(QPointF(projected->first[0],projected->first[1]),QPointF(projected->second[0],projected->second[1])); };
	if ( m_nOperation == 8 )
	{
		line( m_Anchor, m_ModelEnd );
		if ( auto at = Project( view, m_Anchor ) )
			painter.drawEllipse( *at, 4, 4 );
	}
	if ( ( m_nOperation >= 2 && m_nOperation <= 4 ) || m_nOperation == 6 || m_nOperation == 7 )
	{
		const auto *pScene = m_Editor.GetDocument().GetScene();
		for ( auto nSolid : ObjectsOf( *m_pBase, m_Editor.GetDocument().GetSelection() ).m_Solids )
		{
			const auto pOriginal = MeshOf( *m_pBase, nSolid ), pCurrent = MeshOf( *pScene, nSolid );
			const CBrushMesh &original = pOriginal->m_Mesh, &current = pCurrent->m_Mesh;
			const auto &edges = pOriginal->m_Edges;
			for ( auto [ a, b ] : pCurrent->m_Edges )
			{
				const auto pa = current.m_Vertices[ a ], pb = current.m_Vertices[ b ];
				const bool bOld = std::ranges::any_of( edges, [ & ]( auto edge ) {const auto from=original.m_Vertices[edge.first],to=original.m_Vertices[edge.second],direction=Subtract(to,from);for(auto point:{pa,pb})if(Length(Cross(Subtract(point,from),direction))>0.01*Length(direction)||Dot(Subtract(point,from),direction)<-0.01||Dot(Subtract(point,to),direction)>0.01)return false;return true; } );
				if ( !bOld )
					line( pa, pb );
			}
		}
	}
	if ( m_pOperationView == &view )
	{
		QString szLabel = OperationName( m_nOperation );
		if ( m_nOperation == 5 || m_nOperation == 6 )
			szLabel += QStringLiteral( " %1" ).arg( m_pDistance->value() );
		else if ( m_nOperation == 7 || m_nOperation == 14 )
			szLabel += QStringLiteral( " %1" ).arg( m_pModelCount->value() );
		else if ( m_nOperation == 15 )
			szLabel += QStringLiteral( " %1°" ).arg( m_pModelAngle->value() );
		painter.drawText( m_At + QPointF( 12, -12 ), szLabel );
	}
	painter.restore();
}
//-----------------------------------------------------------------------------
// Purpose: Commit geometry and recover generated cap faces by their newly assigned side identities.
//-----------------------------------------------------------------------------
bool CMapBrushEdit::CommitOperationEdit( MapEdit_t edit, const std::vector<int> &sides )
{
	if ( edit.IsEmpty() )
		return false;
	std::vector<std::uint32_t> solidIndices;
	std::vector<std::uint32_t> entityIndices;
	for ( const auto &[ n, solid ] : edit.m_Solids )
		if ( !solid.m_bDeleted )
			solidIndices.push_back( n );
	for ( const auto &[ n, entity ] : edit.m_Entities )
		if ( !entity.m_bDeleted )
			entityIndices.push_back( n );
	m_bApplying = true;
	if ( m_bReplacingOperation )
	{
		if ( !m_Editor.GetDocument().ReplaceLastEdit( std::move( edit ), OperationName( m_nOperation ), m_pLastOperationBase, m_LastObjects ) )
		{
			m_bApplying = false;
			return false;
		}
	}
	else
		m_Editor.GetDocument().Edit( std::move( edit ), OperationName( m_nOperation ) );
	const auto *pScene = m_Editor.GetDocument().GetScene();
	std::vector<BrushPart_t> parts;
	std::vector<std::uint32_t> objects;
	for ( int nSide : sides )
		if ( auto face = pScene->FindSide( nSide ) )
		{
			const auto pMeshParts = MeshOf( *pScene, face->m_nSolid );
			const CBrushMesh &mesh = pMeshParts->m_Mesh;
			for ( std::uint32_t i = 0; i < mesh.m_Faces.size(); ++i )
				if ( mesh.m_Faces[ i ].m_nSide == static_cast<int>( face->m_nSide ) )
				{
					parts.push_back( { pScene->SolidObject( face->m_nSolid ), BrushPartKind_t::BRUSH_PART_FACE, i } );
					objects.push_back( pScene->SolidObject( face->m_nSolid ) );
				}
		}
	if ( objects.empty() )
	{
		for ( auto n : solidIndices )
			objects.push_back( pScene->SolidObject( n ) );
		for ( auto n : entityIndices )
			objects.push_back( pScene->EntityObject( n ) );
	}
	if ( !objects.empty() )
		m_Editor.GetDocument().SetLastEditSelection( objects );
	SetParts( std::move( parts ) );
	m_bApplying = false;
	return true;
}
//-----------------------------------------------------------------------------
// Purpose: Keep the finished tool ready and retain exactly the inputs needed for one-step adjustment.
//-----------------------------------------------------------------------------
void CMapBrushEdit::FinishOperation( bool bCommit )
{
	const int nOperation = m_nOperation;
	const auto parts = m_Parts;
	const auto objects = m_Editor.GetDocument().GetSelection();
	const auto params = GetModelParameters();
	const auto pBase = m_pBase;
	auto edit = bCommit && m_bOperationValid ? BuildOperation( nOperation ) : MapEdit_t{};
	const auto sides = m_ModelSelectedSides;
	const bool bKeepTool = nOperation == 1 || nOperation == 5 || nOperation == 6;
	Cancel( bKeepTool );
	if ( !edit.IsEmpty() )
	{
		m_nOperation = nOperation;
		const bool bCommitted = CommitOperationEdit( std::move( edit ), sides );
		m_nOperation = bKeepTool ? nOperation : -1;
		if ( !bCommitted )
			return;
		m_pLastOperationBase = pBase;
		m_LastParts = parts;
		m_LastObjects = objects;
		m_nLastOperation = nOperation;
		m_LastModelParams = params;
		m_flLastDistance = m_pDistance->value();
		m_pLastScene = m_Editor.GetDocument().GetSharedScene();
		m_nLastUndoIndex = m_Editor.GetDocument().GetUndoStack()->index();
		m_pLastOperationCommand = m_Editor.GetDocument().GetUndoStack()->command( m_nLastUndoIndex - 1 );
		m_LastSelectedSides = sides;
		m_bAdjustingLast = true;
		m_pDistance->setEnabled( true );
	}
	RefreshModelControls();
	if ( bKeepTool )
		emit m_Editor.StatusMessage( tr( "%1: select parts, then drag the circle; release to apply." ).arg( OperationName( nOperation ) ) );
	emit ToolChanged();
}
//-----------------------------------------------------------------------------
// Purpose: Reconstruct the last operation from its original inputs without adding an undo step.
//-----------------------------------------------------------------------------
void CMapBrushEdit::AdjustLastOperation( bool bRestoreInvalid )
{
	auto &document = m_Editor.GetDocument();
	auto *pUndo = document.GetUndoStack();
	if ( m_nLastOperation < 1 || m_pBase || !m_pLastOperationBase || pUndo->index() != m_nLastUndoIndex || m_pLastScene.lock() != document.GetSharedScene() )
	{
		m_pDistance->setEnabled( false );
		m_bAdjustingLast = false;
		RefreshModelControls();
		return;
	}
	const auto params = GetModelParameters();
	if ( params == m_LastModelParams )
		return;
	const auto selected = m_Parts;
	const int nArmed = m_nOperation;
	m_nOperation = m_nLastOperation;
	m_Parts = m_LastParts;
	m_pBase = m_pLastOperationBase;
	m_OperationObjects = m_LastObjects;
	auto edit = BuildOperation( m_nLastOperation );
	const auto sides = m_ModelSelectedSides;
	m_pBase.reset();
	m_Parts = selected;
	if ( edit.IsEmpty() )
	{
		if ( bRestoreInvalid )
			SetModelParameters( m_LastModelParams );
	}
	else
	{
		m_bReplacingOperation = true;
		const bool bReplaced = CommitOperationEdit( std::move( edit ), sides );
		m_bReplacingOperation = false;
		if ( !bReplaced )
		{
			m_nOperation = nArmed;
			m_bAdjustingLast = false;
			RefreshModelControls();
			return;
		}
		m_LastModelParams = params;
		m_LastSelectedSides = sides;
		m_flLastDistance = m_pDistance->value();
	}
	m_nOperation = nArmed;
	m_pLastScene = document.GetSharedScene();
	m_bAdjustingLast = true;
	RefreshModelControls();
	emit ToolChanged();
}

//-----------------------------------------------------------------------------
// Purpose: Apply a valid brush preview before an explicit Apply or Save request.
//-----------------------------------------------------------------------------
bool CMapBrushEdit::CommitPendingOperation()
{
	if ( !m_pBase && m_nOperation >= 9 )
		if ( auto *pView = m_Editor.GetActiveView() )
			BeginModelPreview( *pView, pView->mapFromGlobal( QCursor::pos() ) );
	if ( !m_pBase || m_nOperation < 1 )
		return true;
	if ( !m_bOperationValid )
	{
		emit m_Editor.StatusMessage( tr( "Complete or cancel the brush operation before saving." ) );
		return false;
	}
	FinishOperation( true );
	return !m_Editor.GetDocument().IsPreviewing();
}

//-----------------------------------------------------------------------------
// Purpose: Anchor the active tool handle to the selected parts without starting a preview.
//-----------------------------------------------------------------------------
std::optional<QPointF> CMapBrushEdit::ModelHandleCenter( CMapView &view ) const
{
	if ( m_nOperation != 1 && m_nOperation != 5 && m_nOperation != 6 )
		return std::nullopt;
	if ( m_pOperationView )
		return Project( view, m_Anchor );
	if ( m_nOperation == 1 || m_nOperation == 5 )
		if ( m_Parts.empty() || std::ranges::any_of( m_Parts, []( const BrushPart_t &part ) { return part.m_nKind != BrushPartKind_t::BRUSH_PART_FACE; } ) )
			return std::nullopt;
	if ( auto gizmo = Gizmo() )
		return Project( view, gizmo->GetPivot() );
	if ( m_nOperation == 6 && m_pModelWholeBrush->isChecked() && !m_Editor.GetDocument().GetSelection().empty() )
		return Project( view, m_Editor.GetDocument().GetSelectionBounds().GetCenter() );
	return std::nullopt;
}
//-----------------------------------------------------------------------------
// Purpose: Keep a scalar tool gizmo visible while clicks elsewhere select geometry.
//-----------------------------------------------------------------------------
void CMapBrushEdit::PaintModelHandle( CMapView &view, QPainter &painter ) const
{
	if ( m_Editor.GetTool() != MapTool_t::MAP_TOOL_BRUSH_EDIT )
		return;
	const auto centre = ModelHandleCenter( view );
	if ( !centre )
		return;
	painter.save();
	painter.setRenderHint( QPainter::Antialiasing );
	painter.setPen( QPen( QColor( 255, 190, 40 ), 2 ) );
	painter.setBrush( Qt::NoBrush );
	painter.drawEllipse( *centre, 36, 36 );
	painter.setBrush( QColor( 255, 190, 40 ) );
	painter.drawEllipse( *centre + QPointF( 36, 0 ), 4, 4 );
	painter.drawText( *centre + QPointF( 44, -8 ), OperationName( m_nOperation ) );
	painter.restore();
}
