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
#include <QCursor>
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

#include "Preferences.h"
#include "map/MapBrushEdit.h"
#include "map/MapConstants.h"
#include "map/MapDocument.h"
#include "map/MapEditor.h"
#include "map/MapKeys.h"
#include "map/MapView2D.h"
#include "map/MapView3D.h"
#include "map/MaterialBrowser.h"
#include "map/Trace.h"

using namespace xv::map;

namespace
{

// How near the pointer must be to a point's handle, or the curve, to take it (pixels).
constexpr double HANDLE_REACH = 7.0;
constexpr double CURVE_REACH = 6.0;
// The most changes of a new path Ctrl+Z goes back through.
constexpr std::size_t UNDO_DEPTH = 256;
// The pieces' outlines and the middle line, as the block tool draws a new block's.
const QColor OUTLINE{ 255, 255, 255 };
const QColor MIDDLE{ 255, 230, 60 };

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

//-----------------------------------------------------------------------------
// Purpose: A vector laid flat on the ground (a handle runs across it).
//-----------------------------------------------------------------------------
Point_t Flat( Point_t v )
{
	v[ 2 ] = 0.0;
	return v;
}

//-----------------------------------------------------------------------------
// Purpose: How many segments a spline's curve has, as map/Spline counts them.
//-----------------------------------------------------------------------------
std::size_t SegmentsOf( const Spline_t &spline )
{
	const std::size_t nPoints = spline.m_Points.size();
	if ( nPoints < 2 )
	{
		return 0;
	}
	return spline.m_bClosed && nPoints >= 3 ? nPoints : nPoints - 1;
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
	auto *pBrowse = new QPushButton( tr( "Browse…" ) );
	pBrowse->setObjectName( QStringLiteral( "splineBrowseMaterial" ) );
	pBrowse->setToolTip( tr( "Pick the material in the texture browser" ) );
	connect( pBrowse, &QPushButton::clicked, this, &CMapSplineTool::BrowseMaterial );
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
	pMaterialRow->addWidget( pBrowse );
	pMaterialRow->addWidget( pCurrent );
	pForm->addRow( tr( "Material" ), pMaterialRow );
	m_pScale = spin( 0.01, 16, defaults.m_flTextureScale, 0.05, {}, "splineScale" );
	m_pScale->setToolTip( tr( "The texture's scale along and across the strip" ) );
	pForm->addRow( tr( "Texture scale" ), m_pScale );
	m_pClosed = new QCheckBox( tr( "Loop (the last point joins the first)" ) );
	m_pClosed->setObjectName( QStringLiteral( "splineClosed" ) );
	connect( m_pClosed, &QCheckBox::toggled, this, [ this ] { ReadPanel(); } );
	pForm->addRow( QString{}, m_pClosed );
	m_pGroup = new QCheckBox( tr( "Group the pieces" ) );
	m_pGroup->setObjectName( QStringLiteral( "splineGroup" ) );
	m_pGroup->setToolTip( tr( "Keep the pieces in one group, so a click selects the whole strip" ) );
	m_pGroup->setChecked( defaults.m_bGroup );
	connect( m_pGroup, &QCheckBox::toggled, this, [ this ] { ReadPanel(); } );
	pForm->addRow( QString{}, m_pGroup );
	m_pAutoHandle = new QPushButton( tr( "Automatic Handle" ) );
	m_pAutoHandle->setObjectName( QStringLiteral( "splineAutoHandle" ) );
	m_pAutoHandle->setToolTip( tr( "The picked point's curve back to the spline's own (or double-click a handle's end)" ) );
	connect( m_pAutoHandle, &QPushButton::clicked, this, [ this ] {
		if ( m_nSelected )
		{
			SetHandle( *m_nSelected, std::nullopt );
		}
	} );
	pForm->addRow( tr( "Handle" ), m_pAutoHandle );
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
		m_Undo.clear();
		m_Redo.clear();
		ClearPicked();
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
	m_DraggingHandle.reset();
	if ( m_Modal )
	{
		EndModal( false );
	}
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
			m_Undo.clear();
			m_Redo.clear();
			ClearPicked();
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
	m_Spline.m_bGroup = m_pGroup->isChecked();
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
	m_pGroup->setChecked( m_Spline.m_bGroup );
	m_pMaterial->setText( QString::fromStdString( m_Spline.m_szMaterial ) );
	m_bPanelUpdating = false;
}

//-----------------------------------------------------------------------------
// Purpose: The texture browser, as the texture tool bar's Browse opens it.
//-----------------------------------------------------------------------------
void CMapSplineTool::BrowseMaterial()
{
	const std::optional<xv::vfs::CGameInfo> &game = m_Editor.GetDocument().GetGame();
	if ( !game )
	{
		emit m_Editor.StatusMessage( tr( "Open a game to browse its textures" ) );
		return;
	}
	QStringList used;
	if ( const CScene *pScene = m_Editor.GetDocument().GetScene() )
	{
		for ( const std::string &szMaterial : pScene->m_Materials )
		{
			used << QString::fromStdString( szMaterial );
		}
	}
	if ( const std::optional<QString> szPicked = CMaterialBrowser::Pick( this, *game, used, m_pMaterial->text().trimmed() ) )
	{
		m_pMaterial->setText( szPicked->toUpper() );
		ReadPanel();
	}
}

//-----------------------------------------------------------------------------
// Purpose: What the path is and what Enter will do.
//-----------------------------------------------------------------------------
void CMapSplineTool::UpdateStatus()
{
	m_pDelete->setEnabled( IsEditing() );
	m_pAutoHandle->setEnabled( m_nSelected && *m_nSelected < m_Spline.m_Points.size() && m_Spline.m_Points[ *m_nSelected ].m_Handle );
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
// Purpose: A new path's points kept before they change, for Ctrl+Z; an edited spline undoes
//          through the map instead.
//-----------------------------------------------------------------------------
void CMapSplineTool::Remember()
{
	if ( IsEditing() )
	{
		return;
	}
	m_Undo.push_back( m_Spline.m_Points );
	if ( m_Undo.size() > UNDO_DEPTH )
	{
		m_Undo.erase( m_Undo.begin() );
	}
	m_Redo.clear();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapSplineTool::Undo()
{
	if ( IsEditing() || m_Undo.empty() || m_Modal )
	{
		return false;
	}
	m_Redo.push_back( std::move( m_Spline.m_Points ) );
	m_Spline.m_Points = std::move( m_Undo.back() );
	m_Undo.pop_back();
	ClearPicked();
	UpdateStatus();
	Refresh();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapSplineTool::Redo()
{
	if ( IsEditing() || m_Redo.empty() || m_Modal )
	{
		return false;
	}
	m_Undo.push_back( std::move( m_Spline.m_Points ) );
	m_Spline.m_Points = std::move( m_Redo.back() );
	m_Redo.pop_back();
	ClearPicked();
	UpdateStatus();
	Refresh();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapSplineTool::ClearPicked()
{
	m_Picked.clear();
	m_nSelected.reset();
}

//-----------------------------------------------------------------------------
// Purpose: A point picked alone, or added to (or, picked already, taken from) the others.
//-----------------------------------------------------------------------------
void CMapSplineTool::Pick( std::size_t nPoint, bool bAdd )
{
	if ( nPoint >= m_Spline.m_Points.size() )
	{
		return;
	}
	if ( !bAdd )
	{
		m_Picked = { nPoint };
		m_nSelected = nPoint;
	}
	else if ( m_Picked.erase( nPoint ) > 0 )
	{
		if ( m_nSelected == nPoint )
		{
			m_nSelected = m_Picked.empty() ? std::nullopt : std::optional<std::size_t>{ *m_Picked.rbegin() };
		}
	}
	else
	{
		m_Picked.insert( nPoint );
		m_nSelected = nPoint;
	}
	UpdateStatus();
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapSplineTool::AddPoint( const Point_t &point )
{
	Remember();
	m_Spline.m_Points.push_back( { point, 0.0, std::nullopt } );
	m_Picked = { m_Spline.m_Points.size() - 1 };
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
	Remember();
	nAfter = std::min( nAfter, m_Spline.m_Points.size() - 1 );
	m_Spline.m_Points.insert( m_Spline.m_Points.begin() + static_cast<std::ptrdiff_t>( nAfter + 1 ), { point, 0.0, std::nullopt } );
	m_Picked = { nAfter + 1 };
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
	Remember();
	m_Spline.m_Points.erase( m_Spline.m_Points.begin() + static_cast<std::ptrdiff_t>( nPoint ) );
	ClearPicked();
	if ( IsEditing() )
	{
		Rebuild();
	}
	UpdateStatus();
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose: The picked points moved, turned or scaled together, their handles turned with them.
//-----------------------------------------------------------------------------
void CMapSplineTool::TransformPicked( const Transform_t &transform )
{
	if ( m_Picked.empty() || transform.IsIdentity() )
	{
		return;
	}
	Remember();
	for ( const std::size_t i : m_Picked )
	{
		if ( i >= m_Spline.m_Points.size() )
		{
			continue;
		}
		SplinePoint_t &point = m_Spline.m_Points[ i ];
		point.m_Position = transform.Apply( point.m_Position );
		if ( point.m_Handle )
		{
			point.m_Handle = Flat( transform.Rotate( *point.m_Handle ) );
		}
	}
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
void CMapSplineTool::SetHandle( std::size_t nPoint, std::optional<Point_t> handle )
{
	if ( nPoint >= m_Spline.m_Points.size() )
	{
		return;
	}
	if ( handle )
	{
		handle = Flat( *handle );
	}
	if ( m_Spline.m_Points[ nPoint ].m_Handle == handle )
	{
		return;
	}
	Remember();
	m_Spline.m_Points[ nPoint ].m_Handle = handle;
	if ( IsEditing() )
	{
		Rebuild();
	}
	UpdateStatus();
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose: A point's handle: its own, or a third of the curve's tangent as it leaves the point
//          (the end of a Bezier's control arm), flat on the ground.
//-----------------------------------------------------------------------------
Point_t CMapSplineTool::HandleOf( std::size_t nPoint ) const
{
	if ( nPoint >= m_Spline.m_Points.size() )
	{
		return {};
	}
	if ( const auto &handle = m_Spline.m_Points[ nPoint ].m_Handle )
	{
		return *handle;
	}
	const std::size_t nSegments = SegmentsOf( m_Spline );
	if ( nSegments == 0 )
	{
		return {};
	}
	constexpr double flStep = 1e-3;
	Point_t tangent;
	if ( nPoint < nSegments )
	{
		tangent = Subtract( EvaluateSpline( m_Spline, nPoint, flStep ), EvaluateSpline( m_Spline, nPoint, 0.0 ) );
	}
	else
	{
		tangent = Subtract( EvaluateSpline( m_Spline, nPoint - 1, 1.0 ), EvaluateSpline( m_Spline, nPoint - 1, 1.0 - flStep ) );
	}
	return Flat( Scale( tangent, 1.0 / ( 3.0 * flStep ) ) );
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
	// From here on the map's undo takes the spline back.
	m_Undo.clear();
	m_Redo.clear();
	UpdateStatus();
	Refresh();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: A new path's points dropped (Ctrl+Z brings them back); an edited spline put back as
//          the map has it.
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
	else if ( !m_Spline.m_Points.empty() )
	{
		Remember();
		m_Spline.m_Points.clear();
	}
	ClearPicked();
	m_nDragging.reset();
	m_DraggingHandle.reset();
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
	ClearPicked();
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
	if ( !IsEditing() || m_nDragging || m_DraggingHandle || m_Modal )
	{
		return;
	}
	const CScene *pScene = m_Editor.GetDocument().GetScene();
	const Spline_t *pSpline = pScene ? FindSpline( pScene->m_Map, m_Spline.m_nId ) : nullptr;
	if ( !pSpline )
	{
		m_Spline.m_nId = 0;
		m_Spline.m_nGroup = 0;
		m_Spline.m_Pieces.clear();
	}
	else if ( !( *pSpline == m_Spline ) )
	{
		m_Spline = *pSpline;
		WritePanel();
	}
	std::erase_if( m_Picked, [ this ]( std::size_t i ) { return i >= m_Spline.m_Points.size(); } );
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
// Purpose: Where the pointer puts a point: the surface under it in 3D, the grid in 2D with the
//          hidden axis kept from a point. On a flat face the point snaps along the face; on a
//          displacement (or a model) it snaps across the ground and is traced down onto it, so
//          it lands on the surface drawn rather than the brush face under it.
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
	const auto surface = p3D->SurfaceAt( at );
	if ( !surface )
	{
		return std::nullopt;
	}
	const auto hit = p3D->FaceAt( at );
	const bool bFlat = hit && hit->m_nSolid < pScene->m_Map.m_Solids.size() && hit->m_nSide < pScene->m_Map.m_Solids[ hit->m_nSolid ].m_Sides.size() && !pScene->m_Map.m_Solids[ hit->m_nSolid ].m_Sides[ hit->m_nSide ].m_pDisp;
	const FaceGeometry_t *pFace = bFlat ? pScene->m_Solids[ hit->m_nSolid ].FindFace( hit->m_nSide ) : nullptr;
	if ( pFace )
	{
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
	Point_t point = surface->first;
	if ( flGrid <= 0.0 )
	{
		return point;
	}
	// Snapped across the ground, then straight down onto whatever is drawn there; of what the
	// line passes through, the surface nearest the height clicked.
	const Point_t snapped{ snap( point[ 0 ] ), snap( point[ 1 ] ), point[ 2 ] };
	const MapFrame_t frame = p3D->MakeFrame( p3D->size() );
	const RasterScene_t scene = frame.MakeScene();
	constexpr double flAbove = 1024.0;
	const Point_t from{ snapped[ 0 ], snapped[ 1 ], point[ 2 ] + flAbove };
	std::optional<double> flBest;
	for ( const TraceHit_t &down : TraceScene( scene, settings.m_bToolTextures, settings.m_bNodraw, from, { 0, 0, -1 }, 2.0 * flAbove ) )
	{
		const double flHeight = from[ 2 ] - down.m_flDistance;
		if ( !flBest || std::abs( flHeight - point[ 2 ] ) < std::abs( *flBest - point[ 2 ] ) )
		{
			flBest = flHeight;
		}
	}
	return flBest ? Point_t{ snapped[ 0 ], snapped[ 1 ], *flBest } : point;
}

//-----------------------------------------------------------------------------
// Purpose: The pointer on the level of a height: in 3D where its ray meets that level, in 2D on
//          the view's grid (a handle only takes the part across the ground).
//-----------------------------------------------------------------------------
std::optional<Point_t> CMapSplineTool::GroundPoint( CMapView &view, QPointF at, double flHeight ) const
{
	if ( auto *p2D = qobject_cast<CMapView2D *>( &view ) )
	{
		const QPointF map = p2D->ToMap( at );
		Point_t point{ 0, 0, flHeight };
		point[ p2D->GetAxisIndex( 0 ) ] = map.x();
		point[ p2D->GetAxisIndex( 1 ) ] = map.y();
		return point;
	}
	auto *p3D = qobject_cast<CMapView3D *>( &view );
	if ( !p3D )
	{
		return std::nullopt;
	}
	const auto [ from, direction ] = p3D->GetSizedCamera().Ray( at.x(), at.y() );
	if ( std::abs( direction[ 2 ] ) < 1e-6 )
	{
		return std::nullopt;
	}
	const double t = ( flHeight - from[ 2 ] ) / direction[ 2 ];
	if ( t <= 0.0 )
	{
		return std::nullopt;
	}
	return Add( from, Scale( direction, t ) );
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
// Purpose: The ends of the picked point's handle, ahead of it and behind it.
//-----------------------------------------------------------------------------
std::optional<std::pair<std::size_t, int>> CMapSplineTool::HandleEndAt( CMapView &view, QPointF at ) const
{
	if ( !m_nSelected || *m_nSelected >= m_Spline.m_Points.size() || m_Spline.m_Points.size() < 2 )
	{
		return std::nullopt;
	}
	const Point_t &point = m_Spline.m_Points[ *m_nSelected ].m_Position;
	const Point_t handle = HandleOf( *m_nSelected );
	for ( const int nSide : { 1, -1 } )
	{
		if ( const auto screen = Project( view, Add( point, Scale( handle, nSide ) ) ) )
		{
			if ( std::hypot( screen->x() - at.x(), screen->y() - at.y() ) <= HANDLE_REACH )
			{
				return std::pair{ *m_nSelected, nSide };
			}
		}
	}
	return std::nullopt;
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
// Purpose: The keys the tool answers before the window's shortcuts: the views pass them on.
//-----------------------------------------------------------------------------
bool CMapSplineTool::WantsKey( const QKeyEvent &key ) const
{
	if ( !m_bActive )
	{
		return false;
	}
	if ( m_Modal )
	{
		return true;
	}
	if ( key.matches( QKeySequence::Undo ) )
	{
		return !IsEditing() && !m_Undo.empty();
	}
	if ( key.matches( QKeySequence::Redo ) || ( key.key() == Qt::Key_Y && key.modifiers() == Qt::ControlModifier ) )
	{
		return !IsEditing() && !m_Redo.empty();
	}
	if ( key.matches( QKeySequence::Cut ) )
	{
		return !m_Picked.empty();
	}
	if ( m_Picked.empty() )
	{
		return false;
	}
	const auto nAction = MapActionFor( key, MapKeyContext_t::MAP_KEY_CONTEXT_BRUSH );
	return nAction == MapAction_t::MAP_ACTION_BRUSH_MOVE || nAction == MapAction_t::MAP_ACTION_BRUSH_ROTATE || nAction == MapAction_t::MAP_ACTION_BRUSH_SCALE;
}

//-----------------------------------------------------------------------------
// Purpose: A typed transform of the picked points about their middle, from where the pointer
//          is (the keys and locks as Brush Edit's).
//-----------------------------------------------------------------------------
void CMapSplineTool::BeginModal( CMapView &view, MapGizmoMode_t nMode )
{
	if ( m_Picked.empty() || m_Modal )
	{
		return;
	}
	Point_t pivot{};
	std::size_t nCount = 0;
	for ( const std::size_t i : m_Picked )
	{
		if ( i < m_Spline.m_Points.size() )
		{
			pivot = Add( pivot, m_Spline.m_Points[ i ].m_Position );
			nCount++;
		}
	}
	if ( nCount == 0 )
	{
		return;
	}
	pivot = Scale( pivot, 1.0 / static_cast<double>( nCount ) );
	const std::array<Point_t, 3> axes{ { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
	m_ModalFrom = m_Spline.m_Points;
	m_pModalView = &view;
	m_Modal.emplace( nMode, pivot, axes, m_Editor.GetBrushEdit().Camera( view ), view.mapFromGlobal( QCursor::pos() ) );
	view.setFocus();
	m_Editor.SetActiveView( &view );
	PreviewModal( view, Qt::NoModifier );
}

//-----------------------------------------------------------------------------
// Purpose: The picked points where the typed transform puts them now (the map waits for the
//          end).
//-----------------------------------------------------------------------------
void CMapSplineTool::PreviewModal( CMapView &view, Qt::KeyboardModifiers modifiers )
{
	if ( !m_Modal )
	{
		return;
	}
	const auto &settings = m_Editor.GetSettings();
	const bool bFree = modifiers.testFlag( Qt::AltModifier );
	m_ModalTransform = m_Modal->Evaluate( m_Editor.GetBrushEdit().Camera( view ), bFree || !settings.m_bSnapToGrid ? 0 : settings.m_nGridSize, bFree ? 0 : CPreferences::Instance().GetMapPreferences().m_flRotationSnap );
	m_Spline.m_Points = m_ModalFrom;
	for ( const std::size_t i : m_Picked )
	{
		if ( i >= m_Spline.m_Points.size() )
		{
			continue;
		}
		SplinePoint_t &point = m_Spline.m_Points[ i ];
		point.m_Position = m_ModalTransform.Apply( point.m_Position );
		if ( point.m_Handle )
		{
			point.m_Handle = Flat( m_ModalTransform.Rotate( *point.m_Handle ) );
		}
	}
	emit m_Editor.StatusMessage( m_Modal->GetDescription() );
	UpdateStatus();
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose: The typed transform kept (one rebuild of an edited spline, one Ctrl+Z step of a new
//          path) or the points put back.
//-----------------------------------------------------------------------------
void CMapSplineTool::EndModal( bool bApply )
{
	if ( !m_Modal )
	{
		return;
	}
	m_Modal.reset();
	m_pModalView = nullptr;
	const bool bChanged = !( m_Spline.m_Points == m_ModalFrom );
	if ( !bApply || !bChanged )
	{
		m_Spline.m_Points = m_ModalFrom;
	}
	else if ( IsEditing() )
	{
		Rebuild();
	}
	else
	{
		m_Undo.push_back( m_ModalFrom );
		m_Redo.clear();
	}
	m_ModalFrom.clear();
	UpdateStatus();
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose: While a transform is typed: X/Y/Z lock an axis (Shift: the plane across it), numbers
//          set the amount, the pointer drags it; Enter or a click applies, Escape or a right
//          click cancels.
//-----------------------------------------------------------------------------
bool CMapSplineTool::HandleModalEvent( CMapView &view, QEvent *pEvent )
{
	if ( pEvent->type() == QEvent::KeyPress )
	{
		auto *pKey = static_cast<QKeyEvent *>( pEvent );
		switch ( pKey->key() )
		{
			case Qt::Key_Escape: EndModal( false ); return true;
			case Qt::Key_Return:
			case Qt::Key_Enter: EndModal( true ); return true;
			case Qt::Key_X:
			case Qt::Key_Y:
			case Qt::Key_Z: m_Modal->LockAxis( pKey->key() - Qt::Key_X, pKey->modifiers().testFlag( Qt::ShiftModifier ) ); break;
			case Qt::Key_Backspace: m_Modal->Backspace(); break;
			default:
				for ( const QChar ch : pKey->text() )
				{
					static_cast<void>( m_Modal->Type( ch ) );
				}
				break;
		}
		PreviewModal( view, pKey->modifiers() );
		return true;
	}
	if ( pEvent->type() == QEvent::MouseMove )
	{
		auto *pMouse = static_cast<QMouseEvent *>( pEvent );
		m_Modal->MovePointer( pMouse->position(), pMouse->modifiers().testFlag( Qt::ShiftModifier ) );
		PreviewModal( view, pMouse->modifiers() );
		return true;
	}
	if ( pEvent->type() == QEvent::MouseButtonPress )
	{
		auto *pMouse = static_cast<QMouseEvent *>( pEvent );
		if ( pMouse->button() == Qt::LeftButton || pMouse->button() == Qt::RightButton )
		{
			EndModal( pMouse->button() == Qt::LeftButton );
			return true;
		}
	}
	return pEvent->type() == QEvent::MouseButtonRelease || pEvent->type() == QEvent::MouseButtonDblClick;
}

//-----------------------------------------------------------------------------
// Purpose: Clicks place, pick, drag, insert and remove points and drag the picked one's
//          handle; Enter makes the pieces.
//-----------------------------------------------------------------------------
bool CMapSplineTool::HandleEvent( CMapView &view, QEvent *pEvent )
{
	if ( m_Modal )
	{
		return HandleModalEvent( view, pEvent );
	}
	if ( pEvent->type() == QEvent::KeyPress )
	{
		auto *pKey = static_cast<QKeyEvent *>( pEvent );
		if ( pKey->matches( QKeySequence::Undo ) )
		{
			return Undo();
		}
		if ( pKey->matches( QKeySequence::Redo ) || ( pKey->key() == Qt::Key_Y && pKey->modifiers() == Qt::ControlModifier ) )
		{
			return Redo();
		}
		const bool bCut = pKey->matches( QKeySequence::Cut );
		if ( !m_Picked.empty() && !bCut )
		{
			const auto nAction = MapActionFor( *pKey, MapKeyContext_t::MAP_KEY_CONTEXT_BRUSH );
			if ( nAction == MapAction_t::MAP_ACTION_BRUSH_MOVE || nAction == MapAction_t::MAP_ACTION_BRUSH_ROTATE || nAction == MapAction_t::MAP_ACTION_BRUSH_SCALE )
			{
				BeginModal( view, nAction == MapAction_t::MAP_ACTION_BRUSH_MOVE ? MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE : nAction == MapAction_t::MAP_ACTION_BRUSH_ROTATE ? MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE
																																											 : MapGizmoMode_t::MAP_GIZMO_MODE_SCALE );
				return true;
			}
		}
		if ( bCut || pKey->key() == Qt::Key_Delete || pKey->key() == Qt::Key_Backspace )
		{
			if ( !m_Picked.empty() )
			{
				// The picked points, the last first so the others keep their places.
				const std::vector<std::size_t> picked( m_Picked.rbegin(), m_Picked.rend() );
				if ( IsEditing() && m_Spline.m_Points.size() < picked.size() + 2 )
				{
					emit m_Editor.StatusMessage( tr( "A spline keeps two points at least; Delete Spline takes it out." ) );
					return true;
				}
				Remember();
				for ( const std::size_t i : picked )
				{
					if ( i < m_Spline.m_Points.size() )
					{
						m_Spline.m_Points.erase( m_Spline.m_Points.begin() + static_cast<std::ptrdiff_t>( i ) );
					}
				}
				ClearPicked();
				if ( IsEditing() )
				{
					Rebuild();
				}
				UpdateStatus();
				Refresh();
			}
			else if ( !bCut && !m_Spline.m_Points.empty() )
			{
				RemovePoint( m_Spline.m_Points.size() - 1 );
			}
			return true;
		}
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
					m_Undo.clear();
					m_Redo.clear();
					ClearPicked();
					ReadPanel();
				}
				else
				{
					Cancel();
				}
				return true;
			default: return false;
		}
	}
	if ( pEvent->type() == QEvent::MouseButtonDblClick )
	{
		auto *pMouse = static_cast<QMouseEvent *>( pEvent );
		if ( const auto end = HandleEndAt( view, pMouse->position() ); end && pMouse->button() == Qt::LeftButton )
		{
			m_DraggingHandle.reset();
			SetHandle( end->first, std::nullopt );
			return true;
		}
		return false;
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
		if ( m_DraggingHandle )
		{
			const auto [ nPoint, nSide ] = *m_DraggingHandle;
			const Point_t &position = m_Spline.m_Points[ nPoint ].m_Position;
			if ( const auto ground = GroundPoint( view, at, position[ 2 ] ) )
			{
				const Point_t handle = Flat( Scale( Subtract( *ground, position ), nSide ) );
				if ( !m_bMoved )
				{
					Remember();
				}
				m_bMoved = true;
				m_Spline.m_Points[ nPoint ].m_Handle = handle;
				UpdateStatus();
				Refresh();
			}
			return true;
		}
		if ( !m_nDragging )
		{
			return false; // the view's own looking round and hovering
		}
		if ( const auto point = PointerPoint( view, at, bSnap, m_nDragging ) )
		{
			if ( !( *point == m_Spline.m_Points[ *m_nDragging ].m_Position ) )
			{
				if ( !m_bMoved )
				{
					Remember();
				}
				m_bMoved = true;
			}
			MovePoint( *m_nDragging, *point );
		}
		return true;
	}
	if ( pEvent->type() == QEvent::MouseButtonRelease )
	{
		if ( ( !m_nDragging && !m_DraggingHandle ) || pMouse->button() != Qt::LeftButton )
		{
			return false;
		}
		m_nDragging.reset();
		m_DraggingHandle.reset();
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
	if ( const auto end = HandleEndAt( view, at ) )
	{
		m_DraggingHandle = end;
		m_bMoved = false;
		return true;
	}
	if ( const auto nPoint = PointAt( view, at ) )
	{
		if ( pMouse->modifiers().testFlag( Qt::ShiftModifier ) )
		{
			Pick( *nPoint, true );
			return true;
		}
		if ( !m_Picked.contains( *nPoint ) || m_Picked.size() == 1 )
		{
			Pick( *nPoint, false );
		}
		m_nSelected = nPoint;
		m_nDragging = nPoint;
		m_bMoved = false;
		UpdateStatus();
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
// Purpose: The path as it will be swept: each piece's outline (its two edges and its ends) in
//          white as the block tool draws a new block, the middle line, the points (picked ones
//          orange) and the last picked point's handle; a typed transform's locked axis.
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
		const QPen outline( OUTLINE, 1.5 );
		polyline( []( const SplineFrame_t &frame ) { return Add( frame.m_Position, Scale( frame.m_Right, -0.5 * frame.m_flWidth ) ); }, outline );
		polyline( []( const SplineFrame_t &frame ) { return Add( frame.m_Position, Scale( frame.m_Right, 0.5 * frame.m_flWidth ) ); }, outline );
		painter.setPen( outline );
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
		polyline( []( const SplineFrame_t &frame ) { return frame.m_Position; }, QPen( MIDDLE, 2.0 ) );
	}
	// The last picked point's handle: a line through it with a dot at each end, bright when the
	// point has a handle of its own.
	if ( m_nSelected && *m_nSelected < m_Spline.m_Points.size() && m_Spline.m_Points.size() >= 2 )
	{
		const Point_t &position = m_Spline.m_Points[ *m_nSelected ].m_Position;
		const Point_t handle = HandleOf( *m_nSelected );
		const bool bOwn = m_Spline.m_Points[ *m_nSelected ].m_Handle.has_value();
		const QColor colour = bOwn ? QColor( 80, 200, 255 ) : QColor( 80, 200, 255, 120 );
		const auto ahead = Project( view, Add( position, handle ) );
		const auto behind = Project( view, Subtract( position, handle ) );
		painter.setPen( QPen( colour, 1.5 ) );
		if ( ahead && behind )
		{
			painter.drawLine( *behind, *ahead );
		}
		painter.setBrush( colour );
		for ( const auto &end : { ahead, behind } )
		{
			if ( end )
			{
				painter.drawEllipse( *end, 3.5, 3.5 );
			}
		}
		painter.setBrush( Qt::NoBrush );
	}
	for ( std::size_t i = 0; i < m_Spline.m_Points.size(); i++ )
	{
		if ( const auto screen = Project( view, m_Spline.m_Points[ i ].m_Position ) )
		{
			const bool bPicked = m_Picked.contains( i );
			painter.setPen( QPen( Qt::black, 1.0 ) );
			painter.setBrush( bPicked ? QColor( 255, 140, 0 ) : QColor( Qt::white ) );
			painter.drawRect( QRectF( *screen - QPointF( 4, 4 ), QSizeF( 8, 8 ) ) );
		}
	}
	if ( m_Modal && m_pModalView == &view )
	{
		const CViewCamera camera = m_Editor.GetBrushEdit().Camera( view );
		for ( const MapGizmoLine_t &line : m_Modal->GetLockLines( camera.m_flFar ) )
		{
			const QColor colour = line.m_nPart == 0 ? mapstyle::AXIS_X : line.m_nPart == 1 ? mapstyle::AXIS_Y
																						   : mapstyle::AXIS_Z;
			painter.setPen( QPen( colour, 1.0 ) );
			if ( auto *p3D = qobject_cast<CMapView3D *>( &view ) )
			{
				if ( const auto projected = p3D->GetSizedCamera().ProjectLine( line.m_From, line.m_To ) )
				{
					painter.drawLine( QPointF( projected->first[ 0 ], projected->first[ 1 ] ), QPointF( projected->second[ 0 ], projected->second[ 1 ] ) );
				}
			}
			else if ( const auto a = Project( view, line.m_From ), b = Project( view, line.m_To ); a && b )
			{
				painter.drawLine( *a, *b );
			}
		}
	}
	painter.restore();
}
