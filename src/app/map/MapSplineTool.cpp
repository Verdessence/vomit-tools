//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapSplineTool.h.
//
//===========================================================================//
#include "MapSplineTool.h"

#include <algorithm>
#include <cmath>

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "map/MapDocument.h"
#include "map/MapEditor.h"
#include "map/MapView2D.h"
#include "map/MapView3D.h"

using namespace xv::map;

namespace
{

// How near the pointer must be to a point's handle, or the curve, to take it (pixels).
constexpr double HANDLE_REACH = 7.0;
constexpr double CURVE_REACH = 6.0;

//-----------------------------------------------------------------------------
// Purpose: The distance from a point to a segment on the screen.
//-----------------------------------------------------------------------------
double DistanceToSegment( QPointF p, QPointF a, QPointF b )
{
	const QPointF ab = b - a;
	const double flLength = QPointF::dotProduct( ab, ab );
	const double t = flLength > 0.0 ? std::clamp( QPointF::dotProduct( p - a, ab ) / flLength, 0.0, 1.0 ) : 0.0;
	const QPointF nearest = a + ab * t;
	return std::hypot( p.x() - nearest.x(), p.y() - nearest.y() );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The panel: how the pieces are made, and what to do with the path.
//-----------------------------------------------------------------------------
CMapSplineTool::CMapSplineTool( CMapEditor &editor )
	: m_Editor( editor )
{
	setObjectName( QStringLiteral( "mapSpline" ) );
	auto *pLayout = new QVBoxLayout( this );
	auto *pHelp = new QLabel( tr( "Click on surfaces in 3D, or on the grid in 2D, to lay out the path; drag points to move them, click the curve to add one between, "
								  "right-click a point to remove it. Enter makes the pieces. Choose this tool with a spline's piece selected to edit that spline." ) );
	pHelp->setWordWrap( true );
	pLayout->addWidget( pHelp );

	auto *pForm = new QFormLayout;
	const auto spin = [ this ]( double flMin, double flMax, double flValue, double flStep, const QString &szSuffix, const char *pszName ) {
		auto *pSpin = new QDoubleSpinBox;
		pSpin->setObjectName( QLatin1String( pszName ) );
		pSpin->setRange( flMin, flMax );
		pSpin->setDecimals( flStep < 1.0 ? 3 : 1 );
		pSpin->setSingleStep( flStep );
		pSpin->setValue( flValue );
		pSpin->setSuffix( szSuffix );
		connect( pSpin, &QDoubleSpinBox::valueChanged, this, [ this ] { ReadPanel(); } );
		return pSpin;
	};
	const Spline_t defaults;
	m_pWidth = spin( 8, 4096, defaults.m_flWidth, 16, tr( " units" ), "splineWidth" );
	m_pWidth->setToolTip( tr( "How wide the strip is; a point's own width (Object Properties of the spline, later) overrides it" ) );
	pForm->addRow( tr( "Width" ), m_pWidth );
	m_pPieceLength = spin( 16, 4096, defaults.m_flPieceLength, 16, tr( " units" ), "splinePieceLength" );
	m_pPieceLength->setToolTip( tr( "About how long each displacement piece is along the curve: shorter follows tight bends better, longer makes fewer brushes" ) );
	pForm->addRow( tr( "Piece length" ), m_pPieceLength );
	m_pPower = new QSpinBox;
	m_pPower->setObjectName( QStringLiteral( "splinePower" ) );
	m_pPower->setRange( 2, 4 );
	m_pPower->setValue( defaults.m_nPower );
	m_pPower->setToolTip( tr( "The pieces' displacement power: 2 is a 5×5 grid, 3 is 9×9, 4 is 17×17" ) );
	connect( m_pPower, &QSpinBox::valueChanged, this, [ this ] { ReadPanel(); } );
	pForm->addRow( tr( "Power" ), m_pPower );
	m_pThickness = spin( 1, 1024, defaults.m_flThickness, 8, tr( " units" ), "splineThickness" );
	m_pThickness->setToolTip( tr( "How deep the brushes under the surface are (nodraw; only the displacement shows)" ) );
	pForm->addRow( tr( "Thickness" ), m_pThickness );
	auto *pMaterialRow = new QHBoxLayout;
	m_pMaterial = new QLineEdit( QString::fromStdString( defaults.m_szMaterial ) );
	m_pMaterial->setObjectName( QStringLiteral( "splineMaterial" ) );
	connect( m_pMaterial, &QLineEdit::editingFinished, this, [ this ] { ReadPanel(); } );
	auto *pCurrent = new QPushButton( tr( "Current" ) );
	pCurrent->setToolTip( tr( "The texture tool bar's current material" ) );
	connect( pCurrent, &QPushButton::clicked, this, [ this ] {
		const QString szMaterial = m_Editor.GetCurrentMaterial().trimmed();
		if ( !szMaterial.isEmpty() )
		{
			m_pMaterial->setText( szMaterial.toUpper() );
			ReadPanel();
		}
	} );
	pMaterialRow->addWidget( m_pMaterial, 1 );
	pMaterialRow->addWidget( pCurrent );
	pForm->addRow( tr( "Material" ), pMaterialRow );
	m_pScale = spin( 0.01, 16, defaults.m_flTextureScale, 0.05, {}, "splineScale" );
	m_pScale->setToolTip( tr( "The texture's scale along and across the strip" ) );
	pForm->addRow( tr( "Texture scale" ), m_pScale );
	m_pClosed = new QCheckBox( tr( "Loop (the last point joins the first)" ) );
	m_pClosed->setObjectName( QStringLiteral( "splineClosed" ) );
	connect( m_pClosed, &QCheckBox::toggled, this, [ this ] { ReadPanel(); } );
	pForm->addRow( QString{}, m_pClosed );
	pLayout->addLayout( pForm );

	auto *pButtons = new QHBoxLayout;
	m_pBuild = new QPushButton( tr( "Make Pieces" ) );
	m_pBuild->setObjectName( QStringLiteral( "splineBuild" ) );
	m_pBuild->setToolTip( tr( "Make the strip of displacements (Enter)" ) );
	connect( m_pBuild, &QPushButton::clicked, this, [ this ] { Build(); } );
	auto *pCancel = new QPushButton( tr( "New Path" ) );
	pCancel->setToolTip( tr( "Start another path, leaving this one as it is (Escape)" ) );
	connect( pCancel, &QPushButton::clicked, this, [ this ] {
		m_Spline = Spline_t{};
		ReadPanel();
		Refresh();
	} );
	m_pDelete = new QPushButton( tr( "Delete Spline" ) );
	m_pDelete->setObjectName( QStringLiteral( "splineDelete" ) );
	connect( m_pDelete, &QPushButton::clicked, this, &CMapSplineTool::DeleteSpline );
	pButtons->addWidget( m_pBuild );
	pButtons->addWidget( pCancel );
	pButtons->addWidget( m_pDelete );
	pLayout->addLayout( pButtons );
	m_pStatus = new QLabel;
	m_pStatus->setObjectName( QStringLiteral( "splineStatus" ) );
	m_pStatus->setWordWrap( true );
	pLayout->addWidget( m_pStatus );
	pLayout->addStretch();

	connect( &m_Editor.GetDocument(), &CMapDocument::SceneEdited, this, &CMapSplineTool::SceneEdited );
	ReadPanel();
}

//-----------------------------------------------------------------------------
// Purpose: On choosing the tool, a selected piece's spline is taken up for editing.
//-----------------------------------------------------------------------------
void CMapSplineTool::SetActive( bool bActive )
{
	if ( bActive == m_bActive )
	{
		return;
	}
	m_bActive = bActive;
	m_nDragging.reset();
	if ( bActive )
	{
		for ( const std::uint32_t nObject : m_Editor.GetDocument().GetSelection() )
		{
			if ( EditSplineOf( nObject ) )
			{
				break;
			}
		}
		emit m_Editor.StatusMessage( tr( "Spline: click to place points, Enter makes the pieces, Escape starts over." ) );
	}
	UpdateStatus();
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapSplineTool::EditSplineOf( std::uint32_t nObject )
{
	const CScene *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene || !pScene->IsSolidObject( nObject ) )
	{
		return false;
	}
	for ( const std::uint32_t nSolid : ObjectsOf( *pScene, { nObject } ).m_Solids )
	{
		if ( const Spline_t *pSpline = SplineOfSolid( pScene->m_Map, pScene->m_Map.m_Solids[ nSolid ].m_nId ) )
		{
			m_Spline = *pSpline;
			m_nSelected.reset();
			WritePanel();
			UpdateStatus();
			Refresh();
			return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: The panel's values into the path; an edited spline is made again with them.
//-----------------------------------------------------------------------------
void CMapSplineTool::ReadPanel()
{
	if ( m_bPanelUpdating )
	{
		return;
	}
	const Spline_t before = m_Spline;
	m_Spline.m_flWidth = m_pWidth->value();
	m_Spline.m_flPieceLength = m_pPieceLength->value();
	m_Spline.m_nPower = m_pPower->value();
	m_Spline.m_flThickness = m_pThickness->value();
	m_Spline.m_flTextureScale = m_pScale->value();
	m_Spline.m_bClosed = m_pClosed->isChecked();
	const QString szMaterial = m_pMaterial->text().trimmed().toUpper();
	if ( !szMaterial.isEmpty() )
	{
		m_Spline.m_szMaterial = szMaterial.toStdString();
	}
	if ( IsEditing() && !( m_Spline == before ) )
	{
		Rebuild();
	}
	UpdateStatus();
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose: The path's settings onto the panel.
//-----------------------------------------------------------------------------
void CMapSplineTool::WritePanel()
{
	m_bPanelUpdating = true;
	m_pWidth->setValue( m_Spline.m_flWidth );
	m_pPieceLength->setValue( m_Spline.m_flPieceLength );
	m_pPower->setValue( m_Spline.m_nPower );
	m_pThickness->setValue( m_Spline.m_flThickness );
	m_pScale->setValue( m_Spline.m_flTextureScale );
	m_pClosed->setChecked( m_Spline.m_bClosed );
	m_pMaterial->setText( QString::fromStdString( m_Spline.m_szMaterial ) );
	m_bPanelUpdating = false;
}

//-----------------------------------------------------------------------------
// Purpose: What the path is and what Enter will do.
//-----------------------------------------------------------------------------
void CMapSplineTool::UpdateStatus()
{
	m_pDelete->setEnabled( IsEditing() );
	m_pBuild->setText( IsEditing() ? tr( "Make Again" ) : tr( "Make Pieces" ) );
	const CSplineCurve curve( m_Spline );
	if ( m_Spline.m_Points.size() < 2 )
	{
		m_pStatus->setText( m_Spline.m_Points.empty() ? tr( "No points yet." ) : tr( "One point: place another." ) );
		return;
	}
	const auto nPieces = static_cast<int>( std::max( 1.0, std::round( curve.GetLength() / std::max( 1.0, m_Spline.m_flPieceLength ) ) ) );
	m_pStatus->setText( ( IsEditing() ? tr( "Editing spline %1: " ).arg( m_Spline.m_nId ) : QString{} ) +
		tr( "%n point(s), %1 units long, %2 pieces.", nullptr, static_cast<int>( m_Spline.m_Points.size() ) ).arg( std::lround( curve.GetLength() ) ).arg( nPieces ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapSplineTool::AddPoint( const Point_t &point )
{
	m_Spline.m_Points.push_back( { point, 0.0 } );
	m_nSelected = m_Spline.m_Points.size() - 1;
	if ( IsEditing() )
	{
		Rebuild();
	}
	UpdateStatus();
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapSplineTool::InsertPoint( std::size_t nAfter, const Point_t &point )
{
	nAfter = std::min( nAfter, m_Spline.m_Points.size() - 1 );
	m_Spline.m_Points.insert( m_Spline.m_Points.begin() + static_cast<std::ptrdiff_t>( nAfter + 1 ), { point, 0.0 } );
	m_nSelected = nAfter + 1;
	if ( IsEditing() )
	{
		Rebuild();
	}
	UpdateStatus();
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose: Moves a point; an edited spline is made again when the drag ends.
//-----------------------------------------------------------------------------
void CMapSplineTool::MovePoint( std::size_t nPoint, const Point_t &point )
{
	if ( nPoint < m_Spline.m_Points.size() )
	{
		m_Spline.m_Points[ nPoint ].m_Position = point;
		UpdateStatus();
		Refresh();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapSplineTool::RemovePoint( std::size_t nPoint )
{
	if ( nPoint >= m_Spline.m_Points.size() )
	{
		return;
	}
	if ( IsEditing() && m_Spline.m_Points.size() <= 2 )
	{
		emit m_Editor.StatusMessage( tr( "A spline keeps two points at least; Delete Spline takes it out." ) );
		return;
	}
	m_Spline.m_Points.erase( m_Spline.m_Points.begin() + static_cast<std::ptrdiff_t>( nPoint ) );
	m_nSelected.reset();
	if ( IsEditing() )
	{
		Rebuild();
	}
	UpdateStatus();
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose: The edited spline's pieces made again, one undo step.
//-----------------------------------------------------------------------------
void CMapSplineTool::Rebuild()
{
	const CScene *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene || !IsEditing() )
	{
		return;
	}
	std::string szProblem;
	std::vector<std::uint32_t> added;
	auto edit = UpdateSpline( *pScene, m_Spline, &added, &szProblem );
	if ( !edit )
	{
		m_pStatus->setText( QString::fromStdString( szProblem ) );
		emit m_Editor.StatusMessage( QString::fromStdString( szProblem ) );
		return;
	}
	m_Editor.GetDocument().Edit( std::move( *edit ), tr( "Edit Spline" ), added );
}

//-----------------------------------------------------------------------------
// Purpose: A new path's pieces made (then it is edited), or the edited one's again.
//-----------------------------------------------------------------------------
bool CMapSplineTool::Build()
{
	const CScene *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene )
	{
		return false;
	}
	if ( IsEditing() )
	{
		Rebuild();
		return true;
	}
	std::string szProblem;
	std::vector<std::uint32_t> added;
	auto edit = AddSpline( *pScene, m_Spline, &added, &szProblem );
	if ( !edit )
	{
		m_pStatus->setText( QString::fromStdString( szProblem ) );
		emit m_Editor.StatusMessage( QString::fromStdString( szProblem ) );
		return false;
	}
	const Spline_t made = edit->m_Splines->back();
	m_Editor.GetDocument().Edit( std::move( *edit ), tr( "Create Spline" ), added );
	m_Spline = made;
	UpdateStatus();
	Refresh();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: A new path's points dropped; an edited spline put back as the map has it.
//-----------------------------------------------------------------------------
void CMapSplineTool::Cancel()
{
	const CScene *pScene = m_Editor.GetDocument().GetScene();
	if ( IsEditing() && pScene )
	{
		if ( const Spline_t *pSpline = FindSpline( pScene->m_Map, m_Spline.m_nId ) )
		{
			m_Spline = *pSpline;
			WritePanel();
		}
	}
	else
	{
		m_Spline.m_Points.clear();
	}
	m_nSelected.reset();
	m_nDragging.reset();
	UpdateStatus();
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapSplineTool::DeleteSpline()
{
	const CScene *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene || !IsEditing() )
	{
		return;
	}
	MapEdit_t edit = xv::map::DeleteSpline( *pScene, m_Spline.m_nId );
	m_Spline = Spline_t{};
	ReadPanel();
	m_Editor.GetDocument().Edit( std::move( edit ), tr( "Delete Spline" ), std::vector<std::uint32_t>{} );
	UpdateStatus();
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose: After any edit (an undo, say), an edited spline follows the map: it is taken from
//          the map again, or dropped when it is gone.
//-----------------------------------------------------------------------------
void CMapSplineTool::SceneEdited()
{
	if ( !IsEditing() || m_nDragging )
	{
		return;
	}
	const CScene *pScene = m_Editor.GetDocument().GetScene();
	const Spline_t *pSpline = pScene ? FindSpline( pScene->m_Map, m_Spline.m_nId ) : nullptr;
	if ( !pSpline )
	{
		m_Spline.m_nId = 0;
		m_Spline.m_Pieces.clear();
	}
	else if ( !( *pSpline == m_Spline ) )
	{
		m_Spline = *pSpline;
		WritePanel();
	}
	if ( m_nSelected && *m_nSelected >= m_Spline.m_Points.size() )
	{
		m_nSelected.reset();
	}
	UpdateStatus();
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapSplineTool::Refresh()
{
	for ( CMapView *pView : m_Editor.GetViews() )
	{
		pView->update();
		for ( auto *pOverlay : pView->findChildren<CMapOverlay3D *>() )
		{
			pOverlay->update();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: A point on the screen.
//-----------------------------------------------------------------------------
std::optional<QPointF> CMapSplineTool::Project( CMapView &view, const Point_t &point ) const
{
	if ( auto *p2D = qobject_cast<CMapView2D *>( &view ) )
	{
		return p2D->ToWidget( p2D->Flatten( point ) );
	}
	if ( auto *p3D = qobject_cast<CMapView3D *>( &view ) )
	{
		if ( const auto projected = p3D->GetSizedCamera().Project( point ) )
		{
			return QPointF( ( *projected )[ 0 ], ( *projected )[ 1 ] );
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: Where the pointer puts a point: the surface under it in 3D (snapped on the two axes
//          along the surface), the grid in 2D with the hidden axis kept from a point.
//-----------------------------------------------------------------------------
std::optional<Point_t> CMapSplineTool::PointerPoint( CMapView &view, QPointF at, bool bSnap, std::optional<std::size_t> nKeepFrom ) const
{
	const auto &settings = m_Editor.GetSettings();
	const double flGrid = bSnap && settings.m_bSnapToGrid && settings.m_nGridSize > 0 ? settings.m_nGridSize : 0.0;
	const auto snap = [ flGrid ]( double fl ) { return flGrid > 0.0 ? std::round( fl / flGrid ) * flGrid : fl; };
	if ( auto *p2D = qobject_cast<CMapView2D *>( &view ) )
	{
		const QPointF map = p2D->ToMap( at );
		Point_t point{};
		const int nHidden = 3 - p2D->GetAxisIndex( 0 ) - p2D->GetAxisIndex( 1 );
		if ( nKeepFrom && *nKeepFrom < m_Spline.m_Points.size() )
		{
			point = m_Spline.m_Points[ *nKeepFrom ].m_Position;
		}
		else if ( !m_Spline.m_Points.empty() )
		{
			point[ nHidden ] = m_Spline.m_Points.back().m_Position[ nHidden ];
		}
		point[ p2D->GetAxisIndex( 0 ) ] = snap( map.x() );
		point[ p2D->GetAxisIndex( 1 ) ] = snap( map.y() );
		return point;
	}
	auto *p3D = qobject_cast<CMapView3D *>( &view );
	const CScene *pScene = m_Editor.GetDocument().GetScene();
	if ( !p3D || !pScene )
	{
		return std::nullopt;
	}
	const auto hit = p3D->FaceAt( at );
	if ( !hit )
	{
		return std::nullopt;
	}
	const FaceGeometry_t *pFace = pScene->m_Solids[ hit->m_nSolid ].FindFace( hit->m_nSide );
	if ( !pFace )
	{
		return std::nullopt;
	}
	const auto [ from, direction ] = p3D->GetSizedCamera().Ray( at.x(), at.y() );
	const double flDot = Dot( direction, pFace->m_Plane.m_Normal );
	if ( std::abs( flDot ) < 1e-8 )
	{
		return std::nullopt;
	}
	Point_t point = Add( from, Scale( direction, -pFace->m_Plane.Distance( from ) / flDot ) );
	if ( flGrid > 0.0 )
	{
		// Snapped along the surface, then put back on it.
		int nNormal = 0;
		for ( int i = 1; i < 3; i++ )
		{
			if ( std::abs( pFace->m_Plane.m_Normal[ i ] ) > std::abs( pFace->m_Plane.m_Normal[ nNormal ] ) )
			{
				nNormal = i;
			}
		}
		double flRemaining = pFace->m_Plane.m_flDistance;
		for ( int i = 0; i < 3; i++ )
		{
			if ( i != nNormal )
			{
				point[ i ] = snap( point[ i ] );
				flRemaining -= pFace->m_Plane.m_Normal[ i ] * point[ i ];
			}
		}
		point[ nNormal ] = flRemaining / pFace->m_Plane.m_Normal[ nNormal ];
	}
	return point;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::size_t> CMapSplineTool::PointAt( CMapView &view, QPointF at ) const
{
	std::optional<std::size_t> nearest;
	double flNearest = HANDLE_REACH;
	for ( std::size_t i = 0; i < m_Spline.m_Points.size(); i++ )
	{
		if ( const auto screen = Project( view, m_Spline.m_Points[ i ].m_Position ) )
		{
			const double flDistance = std::hypot( screen->x() - at.x(), screen->y() - at.y() );
			if ( flDistance <= flNearest )
			{
				flNearest = flDistance;
				nearest = i;
			}
		}
	}
	return nearest;
}

//-----------------------------------------------------------------------------
// Purpose: The nearest place on the curve's middle under the pointer, and the point it follows.
//-----------------------------------------------------------------------------
std::optional<std::pair<std::size_t, Point_t>> CMapSplineTool::CurveAt( CMapView &view, QPointF at ) const
{
	const CSplineCurve curve( m_Spline );
	if ( !curve.IsValid() )
	{
		return std::nullopt;
	}
	const auto &samples = curve.Samples();
	std::optional<std::size_t> nBest;
	double flBest = CURVE_REACH;
	std::optional<QPointF> previous;
	for ( std::size_t i = 0; i < samples.size(); i++ )
	{
		const auto screen = Project( view, samples[ i ].m_Position );
		if ( screen && previous && DistanceToSegment( at, *previous, *screen ) <= flBest )
		{
			flBest = DistanceToSegment( at, *previous, *screen );
			nBest = i;
		}
		previous = screen;
	}
	if ( !nBest )
	{
		return std::nullopt;
	}
	const double flDistance = samples[ *nBest ].m_flDistance;
	const auto &points = curve.PointDistances();
	std::size_t nAfter = 0;
	while ( nAfter + 1 < points.size() && points[ nAfter + 1 ] < flDistance )
	{
		nAfter++;
	}
	return std::pair{ std::min( nAfter, m_Spline.m_Points.size() - 1 ), samples[ *nBest ].m_Position };
}

//-----------------------------------------------------------------------------
// Purpose: Clicks place, pick, drag, insert and remove points; Enter makes the pieces.
//-----------------------------------------------------------------------------
bool CMapSplineTool::HandleEvent( CMapView &view, QEvent *pEvent )
{
	if ( pEvent->type() == QEvent::KeyPress )
	{
		auto *pKey = static_cast<QKeyEvent *>( pEvent );
		switch ( pKey->key() )
		{
			case Qt::Key_Return:
			case Qt::Key_Enter: Build(); return true;
			case Qt::Key_Escape:
				if ( !IsEditing() && m_Spline.m_Points.empty() )
				{
					m_Editor.SetTool( MapTool_t::MAP_TOOL_SELECT );
				}
				else if ( IsEditing() )
				{
					m_Spline = Spline_t{};
					ReadPanel();
				}
				else
				{
					Cancel();
				}
				return true;
			case Qt::Key_Delete:
			case Qt::Key_Backspace:
				if ( m_nSelected )
				{
					RemovePoint( *m_nSelected );
				}
				else if ( !m_Spline.m_Points.empty() )
				{
					RemovePoint( m_Spline.m_Points.size() - 1 );
				}
				return true;
			default: return false;
		}
	}
	if ( pEvent->type() != QEvent::MouseButtonPress && pEvent->type() != QEvent::MouseMove && pEvent->type() != QEvent::MouseButtonRelease )
	{
		return false;
	}
	auto *pMouse = static_cast<QMouseEvent *>( pEvent );
	const QPointF at = pMouse->position();
	const bool bSnap = !pMouse->modifiers().testFlag( Qt::AltModifier );
	if ( pEvent->type() == QEvent::MouseMove )
	{
		if ( !m_nDragging )
		{
			return false; // the view's own looking round and hovering
		}
		if ( const auto point = PointerPoint( view, at, bSnap, m_nDragging ) )
		{
			m_bMoved = m_bMoved || !( *point == m_Spline.m_Points[ *m_nDragging ].m_Position );
			MovePoint( *m_nDragging, *point );
		}
		return true;
	}
	if ( pEvent->type() == QEvent::MouseButtonRelease )
	{
		if ( !m_nDragging || pMouse->button() != Qt::LeftButton )
		{
			return false;
		}
		m_nDragging.reset();
		if ( m_bMoved && IsEditing() )
		{
			Rebuild();
		}
		return true;
	}
	// A press.
	if ( pMouse->button() == Qt::RightButton )
	{
		if ( const auto nPoint = PointAt( view, at ) )
		{
			RemovePoint( *nPoint );
			return true;
		}
		return false;
	}
	if ( pMouse->button() != Qt::LeftButton )
	{
		return false;
	}
	view.setFocus();
	m_Editor.SetActiveView( &view );
	if ( const auto nPoint = PointAt( view, at ) )
	{
		m_nSelected = nPoint;
		m_nDragging = nPoint;
		m_bMoved = false;
		Refresh();
		return true;
	}
	if ( const auto onCurve = CurveAt( view, at ) )
	{
		InsertPoint( onCurve->first, onCurve->second );
		m_nDragging = m_nSelected;
		m_bMoved = false;
		return true;
	}
	const auto point = PointerPoint( view, at, bSnap, std::nullopt );
	if ( !point )
	{
		emit m_Editor.StatusMessage( tr( "Spline: in 3D, place points on a surface." ) );
		return true;
	}
	AddPoint( *point );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: The path as it will be swept: its middle, its two edges, a line across where each
//          piece ends, and the points (the picked one orange).
//-----------------------------------------------------------------------------
void CMapSplineTool::Paint( CMapView &view, QPainter &painter ) const
{
	if ( !m_bActive )
	{
		return;
	}
	painter.save();
	painter.setRenderHint( QPainter::Antialiasing );
	const CSplineCurve curve( m_Spline );
	const auto polyline = [ & ]( auto &&pfnPoint, const QPen &pen ) {
		painter.setPen( pen );
		std::optional<QPointF> previous;
		for ( const SplineFrame_t &frame : curve.Samples() )
		{
			const auto screen = Project( view, pfnPoint( frame ) );
			if ( screen && previous )
			{
				painter.drawLine( *previous, *screen );
			}
			previous = screen;
		}
	};
	if ( curve.IsValid() )
	{
		const QPen edge( QColor( 255, 255, 255, 150 ), 1.0 );
		polyline( []( const SplineFrame_t &frame ) { return Add( frame.m_Position, Scale( frame.m_Right, -0.5 * frame.m_flWidth ) ); }, edge );
		polyline( []( const SplineFrame_t &frame ) { return Add( frame.m_Position, Scale( frame.m_Right, 0.5 * frame.m_flWidth ) ); }, edge );
		polyline( []( const SplineFrame_t &frame ) { return frame.m_Position; }, QPen( QColor( 255, 230, 60 ), 2.0 ) );
		painter.setPen( QPen( QColor( 255, 230, 60, 190 ), 1.0 ) );
		const auto nPieces = static_cast<std::size_t>( std::max( 1.0, std::round( curve.GetLength() / std::max( 1.0, m_Spline.m_flPieceLength ) ) ) );
		for ( std::size_t k = 0; k <= nPieces; k++ )
		{
			const double flDistance = curve.GetLength() * static_cast<double>( k ) / static_cast<double>( nPieces );
			const auto a = Project( view, curve.PointAt( flDistance, 0.0 ) );
			const auto b = Project( view, curve.PointAt( flDistance, 1.0 ) );
			if ( a && b )
			{
				painter.drawLine( *a, *b );
			}
		}
	}
	for ( std::size_t i = 0; i < m_Spline.m_Points.size(); i++ )
	{
		if ( const auto screen = Project( view, m_Spline.m_Points[ i ].m_Position ) )
		{
			const bool bPicked = m_nSelected && *m_nSelected == i;
			painter.setPen( QPen( Qt::black, 1.0 ) );
			painter.setBrush( bPicked ? QColor( 255, 140, 0 ) : QColor( Qt::white ) );
			painter.drawRect( QRectF( *screen - QPointF( 4, 4 ), QSizeF( 8, 8 ) ) );
		}
	}
	painter.restore();
}
