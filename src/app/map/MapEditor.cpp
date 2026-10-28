//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapEditor.h.
//
//===========================================================================//
#include "MapEditor.h"

#include "kv1/Kv1.h"
#include "map/Check.h"
#include "map/MapReports.h"
#include "map/Materials.h"
#include "texture/Vtf.h"
#include "vfs/Vfs.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QAction>
#include <QCryptographicHash>
#include <QDateTime>
#include <QSettings>
#include <QActionGroup>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolBar>
#include <QClipboard>
#include <QComboBox>
#include <QSpinBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCompleter>
#include <QFormLayout>
#include <QLineEdit>
#include <QRandomGenerator>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QDir>
#include <QMessageBox>
#include <QTimer>
#include <QVBoxLayout>

#include "FileDialogs.h"
#include "Preferences.h"
#include "map/MapConstants.h"
#include "map/Compile.h"
#include "map/MapCompileDialog.h"
#include "map/MapFaceEdit.h"
#include "map/MapFilterControl.h"
#include "map/MaterialBrowser.h"
#include "map/MapPanes.h"
#include "map/MapProperties.h"
#include "map/MapResources.h"
#include "map/MapView2D.h"
#include "map/MapView3D.h"
#include "map/Entities.h"
#include "map/Primitives.h"
#include "util/Memory.h"

namespace
{

enum class MapIcon_t
{
	MAP_ICON_SELECT,
	MAP_ICON_MAGNIFY,
	MAP_ICON_CAMERA,
	MAP_ICON_ENTITY,
	MAP_ICON_BLOCK,
	MAP_ICON_TEXTURE,
	MAP_ICON_CLIP,
};

//-----------------------------------------------------------------------------
// Purpose: Our own pictures for the tool bar (Hammer's are Valve's to keep).
//-----------------------------------------------------------------------------
QIcon MakeIcon( MapIcon_t nIcon, const QColor &color )
{
	QPixmap pixmap( 32, 32 );
	pixmap.fill( Qt::transparent );
	QPainter painter( &pixmap );
	painter.setRenderHint( QPainter::Antialiasing );
	painter.setPen( QPen{ color, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin } );
	switch ( nIcon )
	{
		case MapIcon_t::MAP_ICON_SELECT:
		{
			QPainterPath arrow;
			arrow.moveTo( 9, 5 );
			arrow.lineTo( 9, 25 );
			arrow.lineTo( 14, 20 );
			arrow.lineTo( 18, 28 );
			arrow.lineTo( 21, 27 );
			arrow.lineTo( 17, 19 );
			arrow.lineTo( 24, 19 );
			arrow.closeSubpath();
			painter.drawPath( arrow );
			break;
		}
		case MapIcon_t::MAP_ICON_MAGNIFY:
			painter.drawEllipse( QPointF{ 13, 13 }, 7.5, 7.5 );
			painter.drawLine( QPointF{ 18.5, 18.5 }, QPointF{ 27, 27 } );
			break;
		case MapIcon_t::MAP_ICON_CAMERA:
			painter.drawRoundedRect( QRectF{ 4, 10, 18, 13 }, 2, 2 );
			painter.drawPolygon( QPolygonF{ { QPointF{ 22, 14 }, QPointF{ 29, 10 }, QPointF{ 29, 23 }, QPointF{ 22, 19 } } } );
			break;
		case MapIcon_t::MAP_ICON_ENTITY:
			// A marker where the entity goes.
			painter.drawEllipse( QPointF{ 16, 16 }, 8.5, 8.5 );
			painter.drawLine( QPointF{ 16, 3 }, QPointF{ 16, 29 } );
			painter.drawLine( QPointF{ 3, 16 }, QPointF{ 29, 16 } );
			break;
		case MapIcon_t::MAP_ICON_BLOCK:
		{
			// A box seen from a corner.
			const QPointF front[ 4 ] = { { 5, 12 }, { 20, 12 }, { 20, 27 }, { 5, 27 } };
			const QPointF back{ 7, -7 };
			painter.drawPolygon( front, 4 );
			painter.drawLine( front[ 0 ], front[ 0 ] + back );
			painter.drawLine( front[ 1 ], front[ 1 ] + back );
			painter.drawLine( front[ 2 ], front[ 2 ] + back );
			painter.drawLine( front[ 0 ] + back, front[ 1 ] + back );
			painter.drawLine( front[ 1 ] + back, front[ 2 ] + back );
			break;
		}
		case MapIcon_t::MAP_ICON_CLIP:
			// A box with a cut through it.
			painter.drawRect( QRectF{ 6, 8, 20, 16 } );
			painter.setPen( QPen{ color, 2.5, Qt::DashLine, Qt::RoundCap } );
			painter.drawLine( QPointF{ 3, 28 }, QPointF{ 29, 4 } );
			break;
		case MapIcon_t::MAP_ICON_TEXTURE:
			// A face with a texture's checker on it.
			painter.drawRect( QRectF{ 5, 5, 22, 22 } );
			painter.setBrush( color );
			painter.setPen( Qt::NoPen );
			painter.drawRect( QRectF{ 5, 5, 11, 11 } );
			painter.drawRect( QRectF{ 16, 16, 11, 11 } );
			break;
	}
	return QIcon{ pixmap };
}

//-----------------------------------------------------------------------------
// Purpose: A power of two the grid can be, nearest to a map's saved spacing.
//-----------------------------------------------------------------------------
int GridSizeFor( int nSpacing )
{
	int nSize = 1;
	while ( nSize < nSpacing && nSize < mapstyle::GRID_LARGEST )
	{
		nSize *= 2;
	}
	return std::clamp( nSize, mapstyle::GRID_SMALLEST, mapstyle::GRID_LARGEST );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapEditor::CMapEditor( const QString &szPath, bool bFromGame, QWidget *pParent )
	: QWidget( pParent ),
	  m_szPath( szPath ),
	  m_bFromGame( bFromGame )
{
	setObjectName( QStringLiteral( "mapEditor" ) );
	m_pDocument = new CMapDocument( this );
	m_pRenderData = std::make_unique<CMapRenderData>( *m_pDocument );
	m_pResources = new CMapResources( this );
	connect( m_pResources, &CMapResources::Arrived, this, &CMapEditor::ResourcesArrived );
	connect( m_pResources, &CMapResources::Finished, this, &CMapEditor::UpdateGameStatus );
	// Reading textures and models frees as much as it keeps.
	connect( m_pResources, &CMapResources::Finished, this, [] { QTimer::singleShot( 0, QCoreApplication::instance(), [] { xv::ReleaseFreedMemory(); } ); } );
	TakePreferences();
	connect( &CPreferences::Instance(), &CPreferences::MapPreferencesChanged, this, &CMapEditor::PreferencesChanged );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->setSpacing( 0 );
	BuildToolBars( pLayout );

	// Hammer's default panes: the camera top left, then top, front and side.
	m_Panes = {
		new CMapPane( *this, MapViewType_t::MAP_VIEW_TYPE_3D_TEXTURED ),
		new CMapPane( *this, MapViewType_t::MAP_VIEW_TYPE_TOP ),
		new CMapPane( *this, MapViewType_t::MAP_VIEW_TYPE_FRONT ),
		new CMapPane( *this, MapViewType_t::MAP_VIEW_TYPE_SIDE ),
	};
	m_pSplitter = new CMapQuadSplitter( m_Panes );
	m_pSplitter->setObjectName( QStringLiteral( "mapPanes" ) );
	m_pLoading = new QLabel( tr( "Reading the map…" ), m_pSplitter );
	m_pLoading->setObjectName( QStringLiteral( "mapLoading" ) );
	m_pLoading->setAlignment( Qt::AlignCenter );
	m_pLoading->setStyleSheet( QStringLiteral( "QLabel { color: white; background: rgba(0, 0, 0, 160); padding: 12px; border-radius: 4px; }" ) );

	// Hammer docks the Filter Control at the side and opens Object Properties as a window of
	// its own; here both share the side, so the selection's keys are always in view.
	m_pSidePanel = new QSplitter( Qt::Vertical );
	m_pSidePanel->setObjectName( QStringLiteral( "mapSidePanel" ) );
	m_pSidePanel->setMinimumWidth( 260 );
	m_pSidePanel->setChildrenCollapsible( false );
	auto section = [ this ]( const QString &szTitle, QWidget *pContent ) {
		auto *pBox = new QWidget;
		auto *pBoxLayout = new QVBoxLayout( pBox );
		pBoxLayout->setContentsMargins( 4, 4, 4, 4 );
		auto *pTitle = new QLabel( szTitle );
		QFont font = pTitle->font();
		font.setBold( true );
		pTitle->setFont( font );
		pBoxLayout->addWidget( pTitle );
		pBoxLayout->addWidget( pContent, 1 );
		m_pSidePanel->addWidget( pBox );
		return pTitle;
	};
	m_pFilter = new CMapFilterControl( *m_pDocument );
	m_pProperties = new CMapProperties( *m_pDocument );
	m_pFaceEdit = new CMapFaceEdit( *this );
	// Hammer opens Face Edit as a window of its own; here it takes Object Properties' place
	// while the texture tool is on.
	m_pPanelStack = new QStackedWidget;
	m_pPanelStack->addWidget( m_pProperties );
	m_pPanelStack->addWidget( m_pFaceEdit );
	section( tr( "Filter Control" ), m_pFilter );
	m_pPanelTitle = section( tr( "Object Properties" ), m_pPanelStack );
	section( tr( "New Objects" ), BuildNewObjects() );
	m_pSidePanel->setStretchFactor( 0, 2 );
	m_pSidePanel->setStretchFactor( 1, 3 );
	m_pSidePanel->setStretchFactor( 2, 0 );

	auto *pMiddle = new QSplitter( Qt::Horizontal );
	pMiddle->addWidget( m_pSplitter );
	pMiddle->addWidget( m_pSidePanel );
	pMiddle->setStretchFactor( 0, 1 );
	pMiddle->setStretchFactor( 1, 0 );
	pMiddle->setSizes( { 1000, 280 } );

	auto *pCenter = new QHBoxLayout;
	pCenter->setContentsMargins( 0, 0, 0, 0 );
	pCenter->setSpacing( 0 );
	auto *pTools = new QToolBar;
	pTools->setObjectName( QStringLiteral( "mapTools" ) );
	pTools->setOrientation( Qt::Vertical );
	pTools->setIconSize( QSize{ 24, 24 } );
	m_pTools = new QActionGroup( this );
	const QColor iconColor = palette().buttonText().color();
	const struct
	{
		MapTool_t m_nTool;
		MapIcon_t m_nIcon;
		QString m_szName;
		MapAction_t m_nKey;
	} tools[] = {
		{ MapTool_t::MAP_TOOL_SELECT, MapIcon_t::MAP_ICON_SELECT, tr( "Selection Tool" ), MapAction_t::MAP_ACTION_TOOL_SELECT },
		{ MapTool_t::MAP_TOOL_MAGNIFY, MapIcon_t::MAP_ICON_MAGNIFY, tr( "Magnify" ), MapAction_t::MAP_ACTION_TOOL_MAGNIFY },
		{ MapTool_t::MAP_TOOL_CAMERA, MapIcon_t::MAP_ICON_CAMERA, tr( "Camera" ), MapAction_t::MAP_ACTION_TOOL_CAMERA },
		{ MapTool_t::MAP_TOOL_ENTITY, MapIcon_t::MAP_ICON_ENTITY, tr( "Entity Tool" ), MapAction_t::MAP_ACTION_TOOL_ENTITY },
		{ MapTool_t::MAP_TOOL_BLOCK, MapIcon_t::MAP_ICON_BLOCK, tr( "Block Tool" ), MapAction_t::MAP_ACTION_TOOL_BLOCK },
		{ MapTool_t::MAP_TOOL_TEXTURE, MapIcon_t::MAP_ICON_TEXTURE, tr( "Texture Application" ), MapAction_t::MAP_ACTION_TOOL_TEXTURE },
		{ MapTool_t::MAP_TOOL_CLIP, MapIcon_t::MAP_ICON_CLIP, tr( "Clipping Tool" ), MapAction_t::MAP_ACTION_TOOL_CLIP },
	};
	for ( const auto &tool : tools )
	{
		QAction *pAction = pTools->addAction( MakeIcon( tool.m_nIcon, iconColor ), tool.m_szName );
		pAction->setCheckable( true );
		pAction->setChecked( tool.m_nTool == m_nTool );
		pAction->setToolTip( QStringLiteral( "%1 (%2)" ).arg( tool.m_szName, MapActionKey( tool.m_nKey ).toString( QKeySequence::NativeText ) ) );
		pAction->setData( static_cast<int>( tool.m_nTool ) );
		pAction->setObjectName( QStringLiteral( "mapTool%1" ).arg( static_cast<int>( tool.m_nTool ) ) );
		m_pTools->addAction( pAction );
	}
	connect( m_pTools, &QActionGroup::triggered, this, [ this ]( QAction *pAction ) { SetTool( static_cast<MapTool_t>( pAction->data().toInt() ) ); } );
	pCenter->addWidget( pTools );
	pCenter->addWidget( pMiddle, 1 );
	pLayout->addLayout( pCenter, 1 );

	// Hammer's status bar: what is selected, its size, the cursor, the zoom, the grid.
	auto *pStatus = new QWidget;
	pStatus->setObjectName( QStringLiteral( "mapStatus" ) );
	auto *pStatusLayout = new QHBoxLayout( pStatus );
	pStatusLayout->setContentsMargins( 6, 2, 6, 2 );
	auto label = [ & ]( const char *pszName, int nMinimum ) {
		auto *pLabel = new QLabel;
		pLabel->setObjectName( QString::fromLatin1( pszName ) );
		pLabel->setMinimumWidth( nMinimum );
		pLabel->setFrameShape( QFrame::StyledPanel );
		pLabel->setContentsMargins( 4, 0, 4, 0 );
		return pLabel;
	};
	m_pStatusSelection = label( "mapStatusSelection", 200 );
	m_pStatusSize = label( "mapStatusSize", 140 );
	m_pStatusCursor = label( "mapStatusCursor", 120 );
	m_pStatusZoom = label( "mapStatusZoom", 90 );
	m_pStatusGrid = label( "mapStatusGrid", 160 );
	m_pStatusGame = label( "mapStatusGame", 0 );
	// The two that stretch share what room is left and cut their text short rather than widen
	// the window past the screen; their tool tips hold all of it.
	m_pStatusSelection->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Preferred );
	m_pStatusGame->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Preferred );
	pStatusLayout->addWidget( m_pStatusSelection, 1 );
	pStatusLayout->addWidget( m_pStatusGame, 1 );
	pStatusLayout->addWidget( m_pStatusSize );
	pStatusLayout->addWidget( m_pStatusCursor );
	pStatusLayout->addWidget( m_pStatusZoom );
	pStatusLayout->addWidget( m_pStatusGrid );
	pLayout->addWidget( pStatus );

	connect( m_pDocument, &CMapDocument::LoadProgress, this, [ this ]( int nPercent, const QString &szStage ) {
		m_pLoading->setText( tr( "Reading the map… %1% (%2)" ).arg( nPercent ).arg( szStage ) );
		m_pLoading->adjustSize();
		m_pLoading->move( ( m_pSplitter->width() - m_pLoading->width() ) / 2, ( m_pSplitter->height() - m_pLoading->height() ) / 2 );
	} );
	connect( m_pDocument, &CMapDocument::Loaded, this, &CMapEditor::Loaded );
	connect( m_pDocument, &CMapDocument::LoadFailed, this, [ this ]( const QString &szError ) {
		m_pLoading->setText( tr( "This map can't be read:\n%1" ).arg( szError ) );
		m_pLoading->adjustSize();
		m_pLoading->move( ( m_pSplitter->width() - m_pLoading->width() ) / 2, ( m_pSplitter->height() - m_pLoading->height() ) / 2 );
	} );
	auto repaint = [ this ] {
		RepaintViews();
		UpdateStatus();
	};
	connect( m_pDocument, &CMapDocument::SelectionChanged, this, repaint );
	connect( m_pDocument, &CMapDocument::VisibilityChanged, this, repaint );
	connect( m_pDocument, &CMapDocument::SceneEdited, this, &CMapEditor::SceneEdited );
	connect( m_pDocument, &CMapDocument::ModifiedChanged, this, &CMapEditor::TitleChanged );
	connect( m_pDocument, &CMapDocument::PointfileChanged, this, &CMapEditor::RepaintTools );
	UpdateStatus();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapEditor::~CMapEditor()
{
	StopWork();
	// The views first, while everything they report to is still here.
	delete m_pSplitter;
	m_pSplitter = nullptr;
	m_Panes = {};
	m_pActiveView = nullptr;
	// Once the map itself is gone (with the tab's children, after this), the memory it took goes
	// back to the system rather than staying with the process.
	QTimer::singleShot( 0, QCoreApplication::instance(), [] { xv::ReleaseFreedMemory(); } );
}

//-----------------------------------------------------------------------------
// Purpose: Every view drawn again (what they show of the map's state changed).
//-----------------------------------------------------------------------------
void CMapEditor::RepaintViews()
{
	for ( CMapView *pView : GetViews() )
	{
		if ( auto *p3D = qobject_cast<CMapView3D *>( pView ) )
		{
			p3D->Redraw();
		}
		pView->update();
	}
}

//-----------------------------------------------------------------------------
// Purpose: The tools' box or marker moved: the 3D views draw only what goes over the picture.
//-----------------------------------------------------------------------------
void CMapEditor::RepaintTools()
{
	for ( CMapView *pView : GetViews() )
	{
		if ( auto *p3D = qobject_cast<CMapView3D *>( pView ) )
		{
			p3D->RedrawLabels();
		}
		else
		{
			pView->update();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Hammer's New Objects and current texture, together: the class the entity tool places
//          and the material new blocks get.
//-----------------------------------------------------------------------------
QWidget *CMapEditor::BuildNewObjects()
{
	auto *pBox = new QWidget;
	auto *pForm = new QFormLayout( pBox );
	pForm->setContentsMargins( 0, 0, 0, 0 );
	m_pEntityClass = new QComboBox;
	m_pEntityClass->setObjectName( QStringLiteral( "mapNewEntityClass" ) );
	m_pEntityClass->setEditable( true );
	m_pEntityClass->setInsertPolicy( QComboBox::NoInsert );
	m_pEntityClass->setToolTip( tr( "The class the entity tool places (%1)" ).arg( MapActionKey( MapAction_t::MAP_ACTION_TOOL_ENTITY ).toString( QKeySequence::NativeText ) ) );
	m_pEntityClass->addItem( QString::fromStdString( m_pDocument->GetDefaults().m_szPointEntity ) );
	m_pMaterial = new QLineEdit( QStringLiteral( "DEV/DEV_MEASUREGENERIC01" ) );
	m_pMaterial->setObjectName( QStringLiteral( "mapNewMaterial" ) );
	m_pMaterial->setToolTip( tr( "The material the block tool gives new blocks (%1)" ).arg( MapActionKey( MapAction_t::MAP_ACTION_TOOL_BLOCK ).toString( QKeySequence::NativeText ) ) );
	// Hammer's Objects box: the block tool's shape, and its Faces box for the round ones.
	m_pPrimitive = new QComboBox;
	m_pPrimitive->setObjectName( QStringLiteral( "mapNewPrimitive" ) );
	m_pPrimitive->addItem( tr( "block" ), static_cast<int>( xv::map::Primitive_t::PRIMITIVE_BLOCK ) );
	m_pPrimitive->addItem( tr( "wedge" ), static_cast<int>( xv::map::Primitive_t::PRIMITIVE_WEDGE ) );
	m_pPrimitive->addItem( tr( "cylinder" ), static_cast<int>( xv::map::Primitive_t::PRIMITIVE_CYLINDER ) );
	m_pPrimitive->addItem( tr( "spike" ), static_cast<int>( xv::map::Primitive_t::PRIMITIVE_SPIKE ) );
	m_pPrimitive->addItem( tr( "sphere" ), static_cast<int>( xv::map::Primitive_t::PRIMITIVE_SPHERE ) );
	m_pPrimitive->setToolTip( tr( "The shape the block tool makes in its box" ) );
	m_pSides = new QSpinBox;
	m_pSides->setObjectName( QStringLiteral( "mapNewSides" ) );
	m_pSides->setRange( 3, 64 );
	m_pSides->setValue( 8 );
	m_pSides->setToolTip( tr( "How many sides a cylinder or spike gets, and how many slices a sphere" ) );
	auto enableSides = [ this ]() {
		const auto nKind = static_cast<xv::map::Primitive_t>( m_pPrimitive->currentData().toInt() );
		m_pSides->setEnabled( nKind != xv::map::Primitive_t::PRIMITIVE_BLOCK && nKind != xv::map::Primitive_t::PRIMITIVE_WEDGE );
	};
	connect( m_pPrimitive, &QComboBox::currentIndexChanged, this, enableSides );
	enableSides();
	pForm->addRow( tr( "Entity" ), m_pEntityClass );
	pForm->addRow( tr( "Objects" ), m_pPrimitive );
	pForm->addRow( tr( "Faces" ), m_pSides );
	pForm->addRow( tr( "Texture" ), m_pMaterial );
	return pBox;
}

//-----------------------------------------------------------------------------
// Purpose: The game data's point classes for the entity tool (the game's default picked), and the
//          map's materials offered as the texture is typed.
//-----------------------------------------------------------------------------
void CMapEditor::FillNewObjects()
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	if ( !pScene )
	{
		return;
	}
	QStringList classes;
	if ( pScene->m_pFgd )
	{
		for ( const auto &[ szLower, fgdClass ] : pScene->m_pFgd->GetClasses() )
		{
			if ( fgdClass.IsPointClass() )
			{
				classes << QString::fromStdString( fgdClass.m_szName );
			}
		}
	}
	const QString szDefault = QString::fromStdString( m_pDocument->GetDefaults().m_szPointEntity );
	const QString szWas = m_pEntityClass->currentText();
	m_pEntityClass->clear();
	m_pEntityClass->addItems( classes );
	m_pEntityClass->completer()->setCaseSensitivity( Qt::CaseInsensitive );
	m_pEntityClass->completer()->setFilterMode( Qt::MatchContains );
	const int nDefault = m_pEntityClass->findText( szDefault, Qt::MatchFixedString );
	m_pEntityClass->setCurrentIndex( nDefault );
	m_pEntityClass->setCurrentText( nDefault >= 0 ? m_pEntityClass->itemText( nDefault ) : szDefault.isEmpty() ? szWas
																											   : szDefault );

	QStringList materials;
	for ( const std::string &szMaterial : pScene->m_Materials )
	{
		materials << QString::fromStdString( szMaterial ).toUpper();
	}
	auto *pCompleter = new QCompleter( materials, m_pMaterial );
	pCompleter->setCaseSensitivity( Qt::CaseInsensitive );
	pCompleter->setFilterMode( Qt::MatchContains );
	delete m_pMaterial->completer();
	m_pMaterial->setCompleter( pCompleter );
}

//-----------------------------------------------------------------------------
// Purpose: The bar across the top: Hammer's selection modes and view toggles.
//-----------------------------------------------------------------------------
void CMapEditor::BuildToolBars( QVBoxLayout *pLayout )
{
	auto *pBar = new QToolBar;
	pBar->setObjectName( QStringLiteral( "mapToolBar" ) );
	pBar->setToolButtonStyle( Qt::ToolButtonTextOnly );
	m_pModes = new QActionGroup( this );
	const struct
	{
		MapSelectMode_t m_nMode;
		QString m_szName;
		QString m_szTip;
	} modes[] = {
		{ MapSelectMode_t::MAP_SELECT_MODE_GROUPS, tr( "Groups" ), tr( "Clicking picks whole groups" ) },
		{ MapSelectMode_t::MAP_SELECT_MODE_OBJECTS, tr( "Objects" ), tr( "Clicking picks an entity with its solids, or a world solid" ) },
		{ MapSelectMode_t::MAP_SELECT_MODE_SOLIDS, tr( "Solids" ), tr( "Clicking picks single solids, even inside entities" ) },
	};
	for ( const auto &mode : modes )
	{
		QAction *pAction = pBar->addAction( mode.m_szName );
		pAction->setCheckable( true );
		pAction->setToolTip( mode.m_szTip );
		pAction->setChecked( mode.m_nMode == m_pDocument->GetSelectMode() );
		pAction->setData( static_cast<int>( mode.m_nMode ) );
		pAction->setObjectName( QStringLiteral( "mapSelectMode%1" ).arg( static_cast<int>( mode.m_nMode ) ) );
		m_pModes->addAction( pAction );
	}
	connect( m_pModes, &QActionGroup::triggered, this, [ this ]( QAction *pAction ) { m_pDocument->SetSelectMode( static_cast<MapSelectMode_t>( pAction->data().toInt() ) ); } );
	pBar->addSeparator();
	auto command = [ this, pBar ]( const QString &szName, MapAction_t nAction, bool bCheckable = false, bool bChecked = false ) {
		QAction *pAction = pBar->addAction( szName );
		pAction->setToolTip( QStringLiteral( "%1 (%2)" ).arg( szName, MapActionKey( nAction ).toString( QKeySequence::NativeText ) ) );
		pAction->setCheckable( bCheckable );
		pAction->setChecked( bChecked );
		connect( pAction, &QAction::triggered, this, [ this, nAction ] { RunAction( nAction, m_pActiveView ); } );
		return pAction;
	};
	command( tr( "Smaller Grid" ), MapAction_t::MAP_ACTION_GRID_SMALLER );
	command( tr( "Bigger Grid" ), MapAction_t::MAP_ACTION_GRID_LARGER );
	// Toggles keep their buttons in step with the settings, however they are changed.
	auto toggle = [ & ]( const QString &szName, MapAction_t nAction, const bool &bSetting ) {
		QAction *pAction = command( szName, nAction, true, bSetting );
		m_SettingActions.emplace_back( pAction, &bSetting );
		return pAction;
	};
	toggle( tr( "Grid" ), MapAction_t::MAP_ACTION_TOGGLE_GRID, m_Settings.m_bShowGrid )->setObjectName( QStringLiteral( "mapToggleGrid" ) );
	toggle( tr( "Snap to Grid" ), MapAction_t::MAP_ACTION_TOGGLE_SNAP, m_Settings.m_bSnapToGrid )->setObjectName( QStringLiteral( "mapToggleSnap" ) );
	toggle( tr( "Texture Lock" ), MapAction_t::MAP_ACTION_TOGGLE_TEXTURE_LOCK, m_Settings.m_bTextureLock )->setObjectName( QStringLiteral( "mapToggleTextureLock" ) );
	toggle( tr( "Scale Lock" ), MapAction_t::MAP_ACTION_TOGGLE_SCALE_LOCK, m_Settings.m_bScaleLock )->setObjectName( QStringLiteral( "mapToggleScaleLock" ) );
	pBar->addSeparator();
	toggle( tr( "Models in 2D" ), MapAction_t::MAP_ACTION_TOGGLE_MODELS_2D, m_Settings.m_bModels2D )->setObjectName( QStringLiteral( "mapToggleModels2D" ) );
	toggle( tr( "Entity Names" ), MapAction_t::MAP_ACTION_TOGGLE_NAMES, m_Settings.m_bNames );
	toggle( tr( "Helpers" ), MapAction_t::MAP_ACTION_TOGGLE_HELPERS, m_Settings.m_bHelpers );
	toggle( tr( "Tool Textures" ), MapAction_t::MAP_ACTION_TOGGLE_TOOL_TEXTURES, m_Settings.m_bToolTextures );
	toggle( tr( "3D Skybox" ), MapAction_t::MAP_ACTION_TOGGLE_SKYBOX_3D, m_Settings.m_bSkybox3D )->setObjectName( QStringLiteral( "mapToggleSkybox3D" ) );
	pBar->addSeparator();
	command( tr( "Center on Selection" ), MapAction_t::MAP_ACTION_CENTER_2D );
	command( tr( "Go to Brush…" ), MapAction_t::MAP_ACTION_GO_TO_BRUSH );
	command( tr( "Autosize Views" ), MapAction_t::MAP_ACTION_AUTOSIZE );
	pBar->addSeparator();
	command( tr( "Run Map…" ), MapAction_t::MAP_ACTION_RUN_MAP )->setObjectName( QStringLiteral( "mapRunMap" ) );
	pLayout->addWidget( pBar );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::Load( CMapDocument::Source_t source )
{
	m_pLoading->show();
	m_pLoading->raise();
	m_pDocument->Load( std::move( source ) );
}

//-----------------------------------------------------------------------------
// Purpose: The map is built: the views draw it and the grid takes the map's own settings.
//-----------------------------------------------------------------------------
void CMapEditor::Loaded()
{
	const xv::map::CScene &scene = *m_pDocument->GetScene();
	m_RememberedCamera = RecallCamera();
	m_Settings.m_nGridSize = GridSizeFor( scene.m_Map.m_ViewSettings.m_nGridSpacing );
	m_Settings.m_bShowGrid = scene.m_Map.m_ViewSettings.m_bShowGrid;
	m_Settings.m_bSnapToGrid = scene.m_Map.m_ViewSettings.m_bSnapToGrid;
	m_pLoading->hide();
	for ( CMapView *pView : GetViews() )
	{
		if ( auto *p2D = qobject_cast<CMapView2D *>( pView ) )
		{
			p2D->Rebuild();
			p2D->Fit();
		}
		else if ( auto *p3D = qobject_cast<CMapView3D *>( pView ) )
		{
			p3D->ResetCamera();
		}
		pView->update();
	}
	if ( auto *pGrid = findChild<QAction *>( QStringLiteral( "mapToggleGrid" ) ) )
	{
		pGrid->setChecked( m_Settings.m_bShowGrid );
	}
	StartResources();
	UpdateGameStatus();
	FillNewObjects();
	std::size_t nErrors = 0;
	for ( const xv::map::Diagnostic_t &diagnostic : scene.m_Map.m_Diagnostics )
	{
		nErrors += diagnostic.m_nSeverity == xv::map::Diagnostic_t::Severity_t::SEVERITY_ERROR ? 1 : 0;
	}
	emit StatusMessage( tr( "%1: %2 solids, %3 entities%4" ).arg( GetTabTitle() ).arg( scene.m_Map.m_Solids.size() ).arg( scene.m_Map.m_Entities.size() ).arg( nErrors ? tr( ", %n problem(s) reading it", nullptr, static_cast<int>( nErrors ) ) : QString{} ) );
	UpdateStatus();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::SetTool( MapTool_t nTool )
{
	m_nTool = nTool;
	const bool bTexture = nTool == MapTool_t::MAP_TOOL_TEXTURE;
	m_pPanelStack->setCurrentWidget( bTexture ? static_cast<QWidget *>( m_pFaceEdit ) : m_pProperties );
	m_pPanelTitle->setText( bTexture ? tr( "Face Edit" ) : tr( "Object Properties" ) );
	if ( !bTexture && !m_SelectedFaces.empty() )
	{
		SetSelectedFaces( {} );
	}
	if ( bTexture )
	{
		m_pFaceEdit->Refresh();
	}
	// What a tool was making goes with it.
	if ( nTool != MapTool_t::MAP_TOOL_BLOCK )
	{
		SetNewBox( std::nullopt );
	}
	if ( nTool != MapTool_t::MAP_TOOL_ENTITY )
	{
		SetEntityMarker( std::nullopt );
	}
	if ( nTool != MapTool_t::MAP_TOOL_CLIP )
	{
		SetClipLine( std::nullopt );
	}
	for ( QAction *pAction : m_pTools->actions() )
	{
		pAction->setChecked( pAction->data().toInt() == static_cast<int>( nTool ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapPane *CMapEditor::GetPane( int nIndex ) const
{
	return m_Panes[ static_cast<std::size_t>( nIndex ) ];
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<CMapView *> CMapEditor::GetViews() const
{
	std::vector<CMapView *> views;
	for ( CMapPane *pPane : m_Panes )
	{
		if ( pPane && pPane->GetView() )
		{
			views.push_back( pPane->GetView() );
		}
	}
	return views;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapPane *CMapEditor::PaneOf( const CMapView *pView ) const
{
	for ( CMapPane *pPane : m_Panes )
	{
		if ( pPane->GetView() == pView )
		{
			return pPane;
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::SetActiveView( CMapView *pView )
{
	m_pActiveView = pView;
	if ( auto *p2D = qobject_cast<CMapView2D *>( pView ) )
	{
		ShowZoom( p2D->GetZoom() );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::ViewTypeChanged( CMapView *pView )
{
	if ( CMapPane *pPane = PaneOf( pView ) )
	{
		pPane->UpdateCaption();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::ShowCursor( const QString &szAt )
{
	m_pStatusCursor->setText( szAt );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::ShowZoom( double flZoom )
{
	m_pStatusZoom->setText( tr( "Zoom: %1" ).arg( flZoom, 0, 'f', flZoom < 1.0 ? 3 : 2 ) );
}

//-----------------------------------------------------------------------------
// Purpose: The selection as Hammer's status bar puts it, and its size.
//-----------------------------------------------------------------------------
void CMapEditor::UpdateStatus()
{
	m_pStatusGrid->setText( tr( "Snap: %1 Grid: %2" ).arg( m_Settings.m_bSnapToGrid ? tr( "On" ) : tr( "Off" ) ).arg( m_Settings.m_nGridSize ) );
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	const std::vector<std::uint32_t> &selection = m_pDocument->GetSelection();
	if ( !pScene || selection.empty() )
	{
		m_pStatusSelection->clear();
		m_pStatusSize->clear();
		return;
	}
	// One object, or one entity with its solids, is named; more are counted.
	std::vector<int> entities;
	bool bWorld = false;
	for ( const std::uint32_t n : selection )
	{
		const int nEntity = pScene->GetObjectEntity( n );
		if ( nEntity < 0 )
		{
			bWorld = true;
		}
		else if ( std::ranges::find( entities, nEntity ) == entities.end() )
		{
			entities.push_back( nEntity );
		}
	}
	if ( !bWorld && entities.size() == 1 )
	{
		const xv::map::Entity_t &entity = pScene->m_Map.m_Entities[ static_cast<std::size_t>( entities.front() ) ];
		const std::string_view szClass = entity.GetClassname();
		const std::string_view szName = entity.GetValue( "targetname" );
		QString szText = QString::fromUtf8( szClass.data(), static_cast<qsizetype>( szClass.size() ) );
		if ( !szName.empty() )
		{
			szText += QStringLiteral( " - " ) + QString::fromUtf8( szName.data(), static_cast<qsizetype>( szName.size() ) );
		}
		m_pStatusSelection->setText( szText );
	}
	else if ( bWorld && entities.empty() && selection.size() == 1 )
	{
		m_pStatusSelection->setText( tr( "solid (id %1)" ).arg( pScene->GetObjectId( selection.front() ) ) );
	}
	else
	{
		m_pStatusSelection->setText( tr( "%n object(s) selected", nullptr, static_cast<int>( selection.size() ) ) );
	}
	m_pStatusSelection->setToolTip( m_pStatusSelection->text() );
	const xv::map::Box_t bounds = m_pDocument->GetSelectionBounds();
	if ( !bounds.IsEmpty() )
	{
		const xv::map::Point_t size = bounds.GetSize();
		m_pStatusSize->setText( QStringLiteral( "%1w %2l %3h" ).arg( std::lround( size[ 0 ] ) ).arg( std::lround( size[ 1 ] ) ).arg( std::lround( size[ 2 ] ) ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::SettingsChanged()
{
	for ( const auto &[ pAction, pSetting ] : m_SettingActions )
	{
		pAction->setChecked( *pSetting );
	}
	for ( CMapView *pView : GetViews() )
	{
		if ( auto *p3D = qobject_cast<CMapView3D *>( pView ) )
		{
			p3D->Redraw();
		}
		pView->update();
	}
	UpdateStatus();
}

//-----------------------------------------------------------------------------
// Purpose: Preferences > Maps: the 2D views' options and the 3D skybox (the views' own toggles
//          start over from them), and how big textures are read.
//-----------------------------------------------------------------------------
void CMapEditor::TakePreferences()
{
	const MapPreferences_t &preferences = CPreferences::Instance().GetMapPreferences();
	m_Settings.m_bDrawVertices = preferences.m_bDrawVertices;
	m_Settings.m_bCenterHandles = preferences.m_bCenterHandles;
	m_Settings.m_bTextureLock = preferences.m_bTextureLock;
	m_Settings.m_bScaleLock = preferences.m_bScaleLock;
	m_Settings.m_bModels2D = preferences.m_bModels2D;
	m_Settings.m_bUseGroupColors = preferences.m_bGroupColors;
	m_Settings.m_bSkybox3D = preferences.m_bSkybox3D;
	m_Settings.m_bHideSmallGrid = preferences.m_bHideSmallGrid;
	m_Settings.m_nHighlightEvery = preferences.m_nHighlightEvery;
	m_nTextureSize = preferences.m_nTextureSize;
}

//-----------------------------------------------------------------------------
// Purpose: The preferences changed while the map is open.
//-----------------------------------------------------------------------------
void CMapEditor::PreferencesChanged()
{
	const int nTextureSize = m_nTextureSize;
	TakePreferences();
	for ( CMapView *pView : GetViews() )
	{
		if ( auto *p3D = qobject_cast<CMapView3D *>( pView ) )
		{
			p3D->ApplyPreferences();
		}
	}
	if ( nTextureSize != m_nTextureSize && m_pDocument->IsLoaded() )
	{
		StartResources();
	}
	SettingsChanged();
}

//-----------------------------------------------------------------------------
// Purpose: Hammer's Go to Brush Number: selects the solid (or entity) with that id.
//-----------------------------------------------------------------------------
void CMapEditor::GoToBrush()
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	if ( !pScene )
	{
		return;
	}
	bool bOk = false;
	const int nId = QInputDialog::getInt( this, tr( "Go to Brush Number" ), tr( "Solid or entity id:" ), 0, 0, std::numeric_limits<int>::max(), 1, &bOk );
	if ( !bOk )
	{
		return;
	}
	GoToObjectId( nId );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::SetClipLine( std::optional<ClipLine_t> line )
{
	m_ClipLine = line;
	m_pClipPreview.reset();
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	if ( const std::optional<xv::map::Plane_t> plane = GetClipPlane(); plane && pScene && !m_pDocument->GetSelection().empty() )
	{
		// What the cut would leave, for the views to outline in white.
		const xv::map::MapEdit_t edit = xv::map::ClipObjects( *pScene, m_pDocument->GetSelection(), *plane, m_nClipMode, nullptr );
		if ( !edit.IsEmpty() )
		{
			m_pClipPreview = xv::map::ApplyEdit( *pScene, edit );
		}
	}
	RepaintTools();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::SetClipMode( xv::map::ClipKeep_t nMode )
{
	m_nClipMode = nMode;
	SetClipLine( m_ClipLine );
	emit StatusMessage( nMode == xv::map::ClipKeep_t::CLIP_KEEP_FRONT ? tr( "Clip: keeping the front" ) : nMode == xv::map::ClipKeep_t::CLIP_KEEP_BACK ? tr( "Clip: keeping the back" )
																																					   : tr( "Clip: keeping both (split)" ) );
}

//-----------------------------------------------------------------------------
// Purpose: The plane through the line, standing on the view's plane: its normal is the line's
//          direction turned a quarter in that plane (to the line's left as drawn), so the
//          front is to the left of a line drawn left to right... from the line's start toward
//          its end, the front lies to the left.
//-----------------------------------------------------------------------------
std::optional<xv::map::Plane_t> CMapEditor::GetClipPlane() const
{
	if ( !m_ClipLine )
	{
		return std::nullopt;
	}
	xv::map::Point_t axis{};
	axis[ static_cast<std::size_t>( std::clamp( m_ClipLine->m_nAxis, 0, 2 ) ) ] = 1.0;
	const xv::map::Point_t along = xv::map::Subtract( m_ClipLine->m_To, m_ClipLine->m_From );
	if ( xv::map::Length( along ) < 1e-6 )
	{
		return std::nullopt;
	}
	const xv::map::Point_t normal = xv::map::Normalized( xv::map::Cross( axis, along ) );
	return xv::map::Plane_t{ normal, xv::map::Dot( normal, m_ClipLine->m_From ) };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::ApplyClip()
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	const std::optional<xv::map::Plane_t> plane = GetClipPlane();
	if ( !pScene || !plane || m_pDocument->GetSelection().empty() )
	{
		return;
	}
	std::vector<std::uint32_t> after = m_pDocument->GetSelection();
	std::vector<std::uint32_t> added;
	xv::map::MapEdit_t edit = xv::map::ClipObjects( *pScene, m_pDocument->GetSelection(), *plane, m_nClipMode, &added );
	if ( edit.IsEmpty() )
	{
		emit StatusMessage( tr( "The clip line misses the selection" ) );
		return;
	}
	after.insert( after.end(), added.begin(), added.end() );
	SetClipLine( std::nullopt );
	m_pDocument->Edit( std::move( edit ), m_nClipMode == xv::map::ClipKeep_t::CLIP_KEEP_BOTH ? tr( "Split" ) : tr( "Clip" ), std::move( after ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::Carve()
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	if ( !pScene || m_pDocument->GetSelection().empty() )
	{
		return;
	}
	std::vector<std::uint32_t> added;
	xv::map::MapEdit_t edit = xv::map::CarveObjects( *pScene, m_pDocument->GetSelection(), &added );
	if ( edit.IsEmpty() )
	{
		emit StatusMessage( tr( "The selection overlaps nothing to carve" ) );
		return;
	}
	m_pDocument->Edit( std::move( edit ), tr( "Carve" ), m_pDocument->GetSelection() );
}

//-----------------------------------------------------------------------------
// Purpose: Hammer's dialog: one number, the wall's width (negative builds outward).
//-----------------------------------------------------------------------------
void CMapEditor::MakeHollow()
{
	if ( m_pDocument->GetSelection().empty() )
	{
		return;
	}
	bool bOk = false;
	const int nWidth = QInputDialog::getInt( this, tr( "Make Hollow" ), tr( "Wall width (a negative width builds the walls outside):" ), 32, -4096, 4096, 1, &bOk );
	if ( bOk )
	{
		MakeHollow( nWidth );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::MakeHollow( double flWallWidth )
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	if ( !pScene || m_pDocument->GetSelection().empty() )
	{
		return;
	}
	std::vector<std::uint32_t> after = m_pDocument->GetSelection();
	std::vector<std::uint32_t> added;
	xv::map::MapEdit_t edit = xv::map::HollowObjects( *pScene, m_pDocument->GetSelection(), flWallWidth, &added );
	if ( edit.IsEmpty() )
	{
		return;
	}
	after.insert( after.end(), added.begin(), added.end() );
	m_pDocument->Edit( std::move( edit ), tr( "Make Hollow" ), std::move( after ) );
}

//-----------------------------------------------------------------------------
// Purpose: Hammer asks which class with Object Properties; a list of the game data's brush
//          classes does here, the configuration's default (func_detail) first.
//-----------------------------------------------------------------------------
void CMapEditor::TieToEntity()
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	const std::vector<std::uint32_t> &selection = m_pDocument->GetSelection();
	if ( !pScene || selection.empty() )
	{
		return;
	}
	QStringList classes;
	for ( const auto &[ szLower, fgdClass ] : pScene->m_pFgd ? pScene->m_pFgd->GetClasses() : std::map<std::string, xv::map::FgdClass_t>{} )
	{
		if ( fgdClass.m_nKind == xv::map::FgdClassKind_t::FGD_CLASS_KIND_SOLID )
		{
			classes << QString::fromStdString( fgdClass.m_szName );
		}
	}
	const QString szDefault = QString::fromStdString( m_pDocument->GetDefaults().m_szSolidEntity );
	classes.removeAll( szDefault );
	classes.prepend( szDefault );
	bool bOk = false;
	const QString szClass = QInputDialog::getItem( this, tr( "Tie to Entity" ), tr( "Class:" ), classes, 0, true, &bOk ).trimmed();
	if ( bOk && !szClass.isEmpty() )
	{
		TieToEntity( szClass );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::TieToEntity( const QString &szClass )
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	const std::vector<std::uint32_t> &selection = m_pDocument->GetSelection();
	if ( !pScene || selection.empty() )
	{
		return;
	}
	std::vector<std::uint32_t> added;
	xv::map::MapEdit_t edit = xv::map::TieToEntity( *pScene, selection, szClass.toStdString(), &added );
	if ( edit.IsEmpty() )
	{
		emit StatusMessage( tr( "Select brushes to tie to an entity" ) );
		return;
	}
	m_pDocument->Edit( std::move( edit ), tr( "Tie to Entity" ), std::move( added ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::MoveToWorld()
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	if ( !pScene || m_pDocument->GetSelection().empty() )
	{
		return;
	}
	xv::map::MapEdit_t edit = xv::map::MoveToWorld( *pScene, m_pDocument->GetSelection() );
	std::vector<std::uint32_t> keep;
	for ( const auto &[ nSolid, solid ] : edit.m_Solids )
	{
		keep.push_back( pScene->SolidObject( nSolid ) );
	}
	if ( !edit.IsEmpty() )
	{
		m_pDocument->Edit( std::move( edit ), tr( "Move to World" ), std::move( keep ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::GroupSelection( bool bGroup )
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	if ( !pScene || m_pDocument->GetSelection().empty() )
	{
		return;
	}
	xv::map::MapEdit_t edit = bGroup ? xv::map::GroupObjects( *pScene, m_pDocument->GetSelection(), nullptr ) : xv::map::UngroupObjects( *pScene, m_pDocument->GetSelection() );
	if ( !edit.IsEmpty() )
	{
		m_pDocument->Edit( std::move( edit ), bGroup ? tr( "Group" ) : tr( "Ungroup" ), m_pDocument->GetSelection() );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapEditor::GoToObjectId( int nId )
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	for ( std::uint32_t n = 0; pScene && n < pScene->GetObjectCount(); n++ )
	{
		if ( pScene->GetObjectId( n ) == nId && !pScene->IsDeleted( n ) )
		{
			m_pDocument->SetSelection( m_pDocument->Expand( n ) );
			RunAction( MapAction_t::MAP_ACTION_CENTER_2D, m_pActiveView );
			RunAction( MapAction_t::MAP_ACTION_CENTER_3D, m_pActiveView );
			return true;
		}
	}
	emit StatusMessage( tr( "No solid or entity has the id %1." ).arg( nId ) );
	return false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::CenterViewsOn( const xv::map::Point_t &point )
{
	xv::map::Box_t box;
	box.Add( xv::map::Subtract( point, { 64, 64, 64 } ) );
	box.Add( xv::map::Add( point, { 64, 64, 64 } ) );
	for ( CMapView *pView : GetViews() )
	{
		pView->CenterOn( box );
	}
}

//-----------------------------------------------------------------------------
// Purpose: One compile window per map, kept while the map is open.
//-----------------------------------------------------------------------------
void CMapEditor::RunMap()
{
	if ( !m_pDocument->IsLoaded() )
	{
		return;
	}
	if ( !m_pCompileDialog )
	{
		m_pCompileDialog = new CMapCompileDialog( *this );
		m_pCompileDialog->setAttribute( Qt::WA_DeleteOnClose );
	}
	m_pCompileDialog->show();
	m_pCompileDialog->raise();
	m_pCompileDialog->activateWindow();
}

//-----------------------------------------------------------------------------
// Purpose: The .lin beside the map, or one chosen.
//-----------------------------------------------------------------------------
void CMapEditor::LoadPointfile()
{
	const QFileInfo map( m_szPath );
	const QString szDefault = m_szPath.isEmpty() || m_bFromGame ? QString{} : map.absolutePath() + QLatin1Char( '/' ) + map.completeBaseName() + QStringLiteral( ".lin" );
	const QString szFile = AskOpenFile( this, tr( "Load Pointfile" ), szDefault, tr( "Pointfiles (*.lin);;All files (*)" ) );
	if ( szFile.isEmpty() )
	{
		return;
	}
	QFile file( szFile );
	if ( !file.open( QIODevice::ReadOnly ) )
	{
		emit StatusMessage( tr( "Could not read %1" ).arg( QDir::toNativeSeparators( szFile ) ) );
		return;
	}
	const std::vector<xv::map::Point_t> points = xv::map::ReadPointfile( file.readAll().toStdString() );
	if ( points.size() < 2 )
	{
		emit StatusMessage( tr( "%1 holds no line" ).arg( QDir::toNativeSeparators( szFile ) ) );
		return;
	}
	m_pDocument->SetPointfile( points );
	GoToLeak();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::UnloadPointfile()
{
	m_pDocument->SetPointfile( std::nullopt );
}

//-----------------------------------------------------------------------------
// Purpose: The line's end is the entity vbsp named; the hole is somewhere along it.
//-----------------------------------------------------------------------------
void CMapEditor::GoToLeak()
{
	const std::optional<std::vector<xv::map::Point_t>> &points = m_pDocument->GetPointfile();
	if ( !points || points->empty() )
	{
		emit StatusMessage( tr( "No pointfile is loaded: compile the map, or Map > Load Pointfile" ) );
		return;
	}
	CenterViewsOn( points->back() );
}

//-----------------------------------------------------------------------------
// Purpose: The commands that act on more than one view.
//-----------------------------------------------------------------------------
bool CMapEditor::RunAction( MapAction_t nAction, CMapView *pFrom )
{
	CMapPane *pPane = PaneOf( pFrom );
	auto switchTo = [ & ]( MapViewType_t nType ) {
		if ( pPane )
		{
			pPane->SetType( nType );
			pPane->GetView()->setFocus();
		}
		return true;
	};
	auto toggle = [ this ]( bool &bSetting ) {
		bSetting = !bSetting;
		SettingsChanged();
		return true;
	};
	const std::vector<std::uint32_t> &selection = m_pDocument->GetSelection();
	switch ( nAction )
	{
		case MapAction_t::MAP_ACTION_VIEW_TOP: return switchTo( MapViewType_t::MAP_VIEW_TYPE_TOP );
		case MapAction_t::MAP_ACTION_VIEW_SIDE: return switchTo( MapViewType_t::MAP_VIEW_TYPE_SIDE );
		case MapAction_t::MAP_ACTION_VIEW_FRONT: return switchTo( MapViewType_t::MAP_VIEW_TYPE_FRONT );
		case MapAction_t::MAP_ACTION_VIEW_TEXTURED: return switchTo( MapViewType_t::MAP_VIEW_TYPE_3D_TEXTURED );
		case MapAction_t::MAP_ACTION_VIEW_TEXTURED_SHADED: return switchTo( MapViewType_t::MAP_VIEW_TYPE_3D_TEXTURED_SHADED );
		case MapAction_t::MAP_ACTION_NEXT_PANE:
		case MapAction_t::MAP_ACTION_PREVIOUS_PANE:
		{
			const int nFrom = pPane ? m_pSplitter->IndexOf( pPane ) : 0;
			const int nTo = ( nFrom + ( nAction == MapAction_t::MAP_ACTION_NEXT_PANE ? 1 : 3 ) ) % 4;
			if ( m_pSplitter->GetMaximized() >= 0 )
			{
				m_pSplitter->SetMaximized( nTo );
			}
			m_Panes[ static_cast<std::size_t>( nTo ) ]->GetView()->setFocus();
			return true;
		}
		case MapAction_t::MAP_ACTION_GRID_SMALLER:
			m_Settings.m_nGridSize = std::max( mapstyle::GRID_SMALLEST, m_Settings.m_nGridSize / 2 );
			SettingsChanged();
			return true;
		case MapAction_t::MAP_ACTION_GRID_LARGER:
			m_Settings.m_nGridSize = std::min( mapstyle::GRID_LARGEST, m_Settings.m_nGridSize * 2 );
			SettingsChanged();
			return true;
		case MapAction_t::MAP_ACTION_TOGGLE_GRID:
			toggle( m_Settings.m_bShowGrid );
			if ( auto *pGrid = findChild<QAction *>( QStringLiteral( "mapToggleGrid" ) ) )
			{
				pGrid->setChecked( m_Settings.m_bShowGrid );
			}
			return true;
		case MapAction_t::MAP_ACTION_MAXIMIZE:
			m_pSplitter->SetMaximized( m_pSplitter->GetMaximized() >= 0 || !pPane ? -1 : m_pSplitter->IndexOf( pPane ) );
			if ( pFrom )
			{
				pFrom->setFocus();
			}
			return true;
		case MapAction_t::MAP_ACTION_AUTOSIZE:
			m_pSplitter->Autosize();
			return true;
		case MapAction_t::MAP_ACTION_CENTER_2D:
		case MapAction_t::MAP_ACTION_CENTER_3D:
		{
			const xv::map::Box_t bounds = m_pDocument->GetSelectionBounds();
			for ( CMapView *pView : GetViews() )
			{
				if ( Is2DView( pView->GetType() ) == ( nAction == MapAction_t::MAP_ACTION_CENTER_2D ) )
				{
					pView->CenterOn( bounds );
				}
			}
			return true;
		}
		case MapAction_t::MAP_ACTION_ESCAPE:
			// A box or marker being made goes first, then the selection.
			if ( m_NewBox || m_EntityMarker || m_ClipLine )
			{
				SetNewBox( std::nullopt );
				SetEntityMarker( std::nullopt );
				SetClipLine( std::nullopt );
				return true;
			}
			m_pDocument->ClearSelection();
			return true;
		case MapAction_t::MAP_ACTION_CLEAR_SELECTION:
			m_pDocument->ClearSelection();
			return true;
		case MapAction_t::MAP_ACTION_ENTER:
			if ( m_nTool == MapTool_t::MAP_TOOL_BLOCK && m_NewBox )
			{
				CreateBlock();
				return true;
			}
			if ( m_nTool == MapTool_t::MAP_TOOL_ENTITY && m_EntityMarker )
			{
				PlaceEntity( *m_EntityMarker );
				return true;
			}
			if ( m_nTool == MapTool_t::MAP_TOOL_CLIP && m_ClipLine )
			{
				ApplyClip();
				return true;
			}
			return false;
		case MapAction_t::MAP_ACTION_QUICK_HIDE:
			m_pDocument->QuickHide( selection );
			return true;
		case MapAction_t::MAP_ACTION_QUICK_HIDE_OTHERS:
			m_pDocument->QuickHideAllBut( selection );
			return true;
		case MapAction_t::MAP_ACTION_QUICK_UNHIDE:
			m_pDocument->QuickUnhideAll();
			return true;
		case MapAction_t::MAP_ACTION_TOGGLE_SNAP: return toggle( m_Settings.m_bSnapToGrid );
		case MapAction_t::MAP_ACTION_TOGGLE_TEXTURE_LOCK: return toggle( m_Settings.m_bTextureLock );
		case MapAction_t::MAP_ACTION_TOGGLE_SCALE_LOCK: return toggle( m_Settings.m_bScaleLock );
		case MapAction_t::MAP_ACTION_EDIT_MODE:
			SetEditMode( m_nEditMode == MapEditMode_t::MAP_EDIT_MODE_RESIZE ? MapEditMode_t::MAP_EDIT_MODE_ROTATE : m_nEditMode == MapEditMode_t::MAP_EDIT_MODE_ROTATE ? MapEditMode_t::MAP_EDIT_MODE_SHEAR
																																									   : MapEditMode_t::MAP_EDIT_MODE_RESIZE );
			return true;
		case MapAction_t::MAP_ACTION_DELETE:
			if ( !selection.empty() && m_pDocument->GetScene() )
			{
				m_pDocument->Edit( xv::map::DeleteObjects( *m_pDocument->GetScene(), selection ), tr( "Delete" ), std::vector<std::uint32_t>{} );
			}
			return true;
		case MapAction_t::MAP_ACTION_COPY:
		case MapAction_t::MAP_ACTION_CUT:
			if ( !selection.empty() && m_pDocument->GetScene() )
			{
				QGuiApplication::clipboard()->setText( QString::fromStdString( xv::map::ObjectsToText( *m_pDocument->GetScene(), selection ) ) );
				if ( nAction == MapAction_t::MAP_ACTION_CUT )
				{
					m_pDocument->Edit( xv::map::DeleteObjects( *m_pDocument->GetScene(), selection ), tr( "Cut" ), std::vector<std::uint32_t>{} );
				}
			}
			return true;
		case MapAction_t::MAP_ACTION_PASTE:
			Paste( pFrom );
			return true;
		case MapAction_t::MAP_ACTION_PASTE_SPECIAL:
			ShowPasteSpecialDialog();
			return true;
		case MapAction_t::MAP_ACTION_REPLACE_TEXTURES:
			ShowReplaceTextures();
			return true;
		case MapAction_t::MAP_ACTION_FIND_ENTITIES:
			ShowFindEntities();
			return true;
		case MapAction_t::MAP_ACTION_ENTITY_REPORT:
			ShowEntityReport();
			return true;
		case MapAction_t::MAP_ACTION_CHECK_PROBLEMS:
			ShowCheckForProblems();
			return true;
		case MapAction_t::MAP_ACTION_TRANSFORM:
			ShowTransformDialog();
			return true;
		case MapAction_t::MAP_ACTION_FLIP_HORIZONTAL:
		case MapAction_t::MAP_ACTION_FLIP_VERTICAL:
		{
			const bool bVertical = nAction == MapAction_t::MAP_ACTION_FLIP_VERTICAL;
			const auto *p2D = qobject_cast<CMapView2D *>( pFrom ? pFrom : m_pActiveView );
			FlipSelection( p2D ? static_cast<std::size_t>( p2D->GetAxisIndex( bVertical ? 1 : 0 ) ) : bVertical ? 2
																												: 0 );
			return true;
		}
		case MapAction_t::MAP_ACTION_SELECT_ALL:
		{
			std::vector<std::uint32_t> all;
			for ( std::uint32_t n = 0; m_pDocument->GetScene() && n < m_pDocument->GetScene()->GetObjectCount(); n++ )
			{
				if ( m_pDocument->IsShown( n ) )
				{
					all.push_back( n );
				}
			}
			m_pDocument->SetSelection( std::move( all ) );
			return true;
		}
		case MapAction_t::MAP_ACTION_SNAP_SELECTION:
			SnapSelection();
			return true;
		case MapAction_t::MAP_ACTION_TOGGLE_HELPERS: return toggle( m_Settings.m_bHelpers );
		case MapAction_t::MAP_ACTION_TOGGLE_MODELS_2D: return toggle( m_Settings.m_bModels2D );
		case MapAction_t::MAP_ACTION_TOGGLE_NAMES: return toggle( m_Settings.m_bNames );
		case MapAction_t::MAP_ACTION_TOGGLE_TOOL_TEXTURES: return toggle( m_Settings.m_bToolTextures );
		case MapAction_t::MAP_ACTION_TOGGLE_SKYBOX_3D: return toggle( m_Settings.m_bSkybox3D );
		case MapAction_t::MAP_ACTION_GO_TO_BRUSH:
			GoToBrush();
			return true;
		case MapAction_t::MAP_ACTION_PROPERTIES:
			ShowProperties();
			return true;
		case MapAction_t::MAP_ACTION_TOOL_SELECT:
			SetTool( MapTool_t::MAP_TOOL_SELECT );
			return true;
		case MapAction_t::MAP_ACTION_TOOL_MAGNIFY:
			SetTool( MapTool_t::MAP_TOOL_MAGNIFY );
			return true;
		case MapAction_t::MAP_ACTION_TOOL_CAMERA:
			SetTool( MapTool_t::MAP_TOOL_CAMERA );
			return true;
		case MapAction_t::MAP_ACTION_GIZMO_TRANSLATE: SetGizmoMode( MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE ); return true;
		case MapAction_t::MAP_ACTION_GIZMO_ROTATE: SetGizmoMode( MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE ); return true;
		case MapAction_t::MAP_ACTION_GIZMO_SCALE: SetGizmoMode( MapGizmoMode_t::MAP_GIZMO_MODE_SCALE ); return true;
		case MapAction_t::MAP_ACTION_GIZMO_ORIENTATION: SetGizmoLocal( !m_bGizmoLocal ); return true;
		case MapAction_t::MAP_ACTION_TOOL_ENTITY:
			SetTool( MapTool_t::MAP_TOOL_ENTITY );
			return true;
		case MapAction_t::MAP_ACTION_TOOL_BLOCK:
			SetTool( MapTool_t::MAP_TOOL_BLOCK );
			return true;
		case MapAction_t::MAP_ACTION_TOOL_TEXTURE:
			SetTool( MapTool_t::MAP_TOOL_TEXTURE );
			return true;
		case MapAction_t::MAP_ACTION_TOOL_CLIP:
			// Shift+X again goes round Hammer's modes: keep the front, the back, both.
			if ( m_nTool == MapTool_t::MAP_TOOL_CLIP )
			{
				SetClipMode( m_nClipMode == xv::map::ClipKeep_t::CLIP_KEEP_FRONT ? xv::map::ClipKeep_t::CLIP_KEEP_BACK : m_nClipMode == xv::map::ClipKeep_t::CLIP_KEEP_BACK ? xv::map::ClipKeep_t::CLIP_KEEP_BOTH
																																											: xv::map::ClipKeep_t::CLIP_KEEP_FRONT );
			}
			else
			{
				SetTool( MapTool_t::MAP_TOOL_CLIP );
			}
			return true;
		case MapAction_t::MAP_ACTION_CARVE:
			Carve();
			return true;
		case MapAction_t::MAP_ACTION_HOLLOW:
			MakeHollow();
			return true;
		case MapAction_t::MAP_ACTION_APPLY_TEXTURE:
			ApplyCurrentTexture( false );
			return true;
		case MapAction_t::MAP_ACTION_RUN_MAP:
			RunMap();
			return true;
		case MapAction_t::MAP_ACTION_TIE_TO_ENTITY:
			TieToEntity();
			return true;
		case MapAction_t::MAP_ACTION_MOVE_TO_WORLD:
			MoveToWorld();
			return true;
		case MapAction_t::MAP_ACTION_GROUP:
			GroupSelection( true );
			return true;
		case MapAction_t::MAP_ACTION_UNGROUP:
			GroupSelection( false );
			return true;
		case MapAction_t::MAP_ACTION_TOGGLE_GROUP_IGNORE:
			// Hammer's "ignore groups": Objects mode, and back to Groups.
			m_pDocument->SetSelectMode( m_pDocument->GetSelectMode() == MapSelectMode_t::MAP_SELECT_MODE_GROUPS ? MapSelectMode_t::MAP_SELECT_MODE_OBJECTS : MapSelectMode_t::MAP_SELECT_MODE_GROUPS );
			for ( QAction *pAction : m_pModes->actions() )
			{
				pAction->setChecked( pAction->data().toInt() == static_cast<int>( m_pDocument->GetSelectMode() ) );
			}
			return true;
		case MapAction_t::MAP_ACTION_ZOOM_IN_ALL:
		case MapAction_t::MAP_ACTION_ZOOM_OUT_ALL:
			for ( CMapView *pView : GetViews() )
			{
				if ( auto *p2D = qobject_cast<CMapView2D *>( pView ) )
				{
					p2D->Zoom( nAction == MapAction_t::MAP_ACTION_ZOOM_IN_ALL ? CPreferences::Instance().GetMapPreferences().m_flKeyZoom : 1.0 / CPreferences::Instance().GetMapPreferences().m_flKeyZoom );
				}
			}
			return true;
		default:
			return false;
	}
}

//-----------------------------------------------------------------------------
// Purpose: The selection moved (or copied, the copies selected) by offset, with the texture lock.
//-----------------------------------------------------------------------------
void CMapEditor::MoveSelection( xv::map::Point_t offset, bool bClone )
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	const std::vector<std::uint32_t> &selection = m_pDocument->GetSelection();
	if ( !pScene || selection.empty() )
	{
		return;
	}
	xv::map::Transform_t move;
	move.m_Offset = offset;
	if ( bClone )
	{
		std::vector<std::uint32_t> added;
		xv::map::MapEdit_t edit = xv::map::CopyObjects( *pScene, selection, move, m_Settings.m_bTextureLock, &added );
		m_pDocument->Edit( std::move( edit ), tr( "Clone" ), std::move( added ) );
		return;
	}
	TransformSelection( move, tr( "Move" ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::TransformSelection( const xv::map::Transform_t &transform, const QString &szName )
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	const std::vector<std::uint32_t> &selection = m_pDocument->GetSelection();
	if ( !pScene || selection.empty() || transform.IsIdentity() )
	{
		return;
	}
	m_pDocument->Edit( xv::map::TransformObjects( *pScene, selection, transform, m_Settings.m_bTextureLock, m_Settings.m_bScaleLock ), szName, selection );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::SetGizmoShown( bool bShown )
{
	m_bGizmo = bShown;
	RepaintTools();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::SetGizmoMode( MapGizmoMode_t nMode )
{
	m_nGizmoMode = nMode;
	RepaintTools();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::SetGizmoLocal( bool bLocal )
{
	m_bGizmoLocal = bLocal;
	RepaintTools();
}

//-----------------------------------------------------------------------------
// Purpose: Hammer's cycle of what the selection's handles do; the views draw the handles anew.
//-----------------------------------------------------------------------------
void CMapEditor::SetEditMode( MapEditMode_t nMode )
{
	if ( nMode == m_nEditMode )
	{
		return;
	}
	m_nEditMode = nMode;
	for ( CMapView *pView : GetViews() )
	{
		pView->update();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::SetNewBox( std::optional<xv::map::Box_t> box )
{
	if ( box == m_NewBox )
	{
		return;
	}
	m_NewBox = box;
	RepaintTools();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::pair<double, double> CMapEditor::GetNewBoxDepth( int nAxis ) const
{
	const auto a = static_cast<std::size_t>( nAxis );
	if ( m_NewBox )
	{
		return { m_NewBox->m_Mins[ a ], m_NewBox->m_Maxs[ a ] };
	}
	const xv::map::Box_t bounds = m_pDocument->GetSelectionBounds();
	if ( !bounds.IsEmpty() && bounds.m_Maxs[ a ] > bounds.m_Mins[ a ] )
	{
		return { bounds.m_Mins[ a ], bounds.m_Maxs[ a ] };
	}
	return { m_LastBox.m_Mins[ a ], m_LastBox.m_Maxs[ a ] };
}

//-----------------------------------------------------------------------------
// Purpose: Each new solid gets an editor colour of its own, as Hammer gives one: no red, green
//          and blue from 100 up (what the maps it saved hold).
//-----------------------------------------------------------------------------
void CMapEditor::CreateBlock()
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	if ( !pScene || !m_NewBox )
	{
		return;
	}
	const xv::map::Box_t box = *m_NewBox;
	for ( std::size_t a = 0; a < 3; a++ )
	{
		if ( box.m_Maxs[ a ] - box.m_Mins[ a ] <= 0.0 )
		{
			emit StatusMessage( tr( "The box is flat: drag its sides apart in another view first" ) );
			return;
		}
	}
	const xv::map::HammerDefaults_t &defaults = m_pDocument->GetDefaults();
	// Hammer writes material names in capitals.
	const std::string szMaterial = GetCurrentMaterial().trimmed().toUpper().toStdString();
	xv::map::FaceDefaults_t faces;
	if ( !szMaterial.empty() )
	{
		faces.m_szMaterial = szMaterial;
	}
	faces.m_flTextureScale = defaults.m_flTextureScale;
	faces.m_nLightmapScale = defaults.m_nLightmapScale;
	QRandomGenerator *pRandom = QRandomGenerator::global();
	faces.m_Color = { 0, static_cast<std::uint8_t>( pRandom->bounded( 100, 256 ) ), static_cast<std::uint8_t>( pRandom->bounded( 100, 256 ) ) };
	std::vector<std::uint32_t> added;
	const auto nKind = static_cast<xv::map::Primitive_t>( m_pPrimitive->currentData().toInt() );
	xv::map::MapEdit_t edit = xv::map::AddObjects( *pScene, { xv::map::MakePrimitive( pScene->m_Map, nKind, box, faces, m_pSides->value() ) }, {}, &added );
	m_LastBox = box;
	SetNewBox( std::nullopt );
	m_pDocument->Edit( std::move( edit ), tr( "Create %1" ).arg( m_pPrimitive->currentText() ), std::move( added ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::SetEntityMarker( std::optional<xv::map::Point_t> at )
{
	if ( at == m_EntityMarker )
	{
		return;
	}
	m_EntityMarker = at;
	RepaintTools();
}

//-----------------------------------------------------------------------------
// Purpose: Only point classes: a brush entity is made from brushes (Tie to Entity).
//-----------------------------------------------------------------------------
void CMapEditor::PlaceEntity( const xv::map::Point_t &origin )
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	const std::string szClass = GetEntityClass().trimmed().toStdString();
	if ( !pScene )
	{
		return;
	}
	if ( szClass.empty() )
	{
		emit StatusMessage( tr( "Pick a class for the entity tool to place" ) );
		return;
	}
	const xv::map::FgdClass_t *pClass = pScene->m_pFgd ? pScene->m_pFgd->Find( szClass ) : nullptr;
	if ( pClass && !pClass->IsPointClass() )
	{
		emit StatusMessage( tr( "%1 is a brush entity: make its brushes, then tie them to it" ).arg( QString::fromStdString( pClass->m_szName ) ) );
		return;
	}
	std::vector<std::uint32_t> added;
	xv::map::MapEdit_t edit = xv::map::AddObjects( *pScene, {}, { xv::map::MakePointEntity( pScene->m_Map, pScene->m_pFgd.get(), szClass, origin ) }, &added );
	m_pDocument->Edit( std::move( edit ), tr( "Create Entity" ), std::move( added ) );
}

//-----------------------------------------------------------------------------
// Purpose: The origin goes out from the surface by as much as the class's box reaches behind it,
//          so the box rests on the surface; on a floor, wall or ceiling the grid places it along
//          the surface.
//-----------------------------------------------------------------------------
void CMapEditor::PlaceEntityOn( const xv::map::Point_t &point, const xv::map::Point_t &normal )
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	if ( !pScene )
	{
		return;
	}
	const std::string szClass = GetEntityClass().trimmed().toStdString();
	xv::map::Entity_t probe;
	probe.m_Keys.push_back( { "classname", szClass, {} } );
	const xv::map::Box_t box = xv::map::DescribeEntity( probe, pScene->m_pFgd.get() ).m_Box;
	double flBehind = std::numeric_limits<double>::max();
	for ( int nCorner = 0; nCorner < 8; nCorner++ )
	{
		const xv::map::Point_t corner{ ( nCorner & 1 ) ? box.m_Maxs[ 0 ] : box.m_Mins[ 0 ], ( nCorner & 2 ) ? box.m_Maxs[ 1 ] : box.m_Mins[ 1 ], ( nCorner & 4 ) ? box.m_Maxs[ 2 ] : box.m_Mins[ 2 ] };
		flBehind = std::min( flBehind, xv::map::Dot( corner, normal ) );
	}
	xv::map::Point_t origin = xv::map::Subtract( point, xv::map::Scale( normal, flBehind ) );
	std::size_t nFacing = 0;
	for ( std::size_t a = 1; a < 3; a++ )
	{
		nFacing = std::abs( normal[ a ] ) > std::abs( normal[ nFacing ] ) ? a : nFacing;
	}
	if ( m_Settings.m_bSnapToGrid && std::abs( normal[ nFacing ] ) > 0.999 )
	{
		const auto flGrid = static_cast<double>( m_Settings.m_nGridSize );
		for ( std::size_t a = 0; a < 3; a++ )
		{
			if ( a != nFacing )
			{
				origin[ a ] = std::round( origin[ a ] / flGrid ) * flGrid;
			}
		}
	}
	PlaceEntity( origin );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CMapEditor::GetCurrentMaterial() const
{
	return m_pMaterial ? m_pMaterial->text() : QString{};
}

//-----------------------------------------------------------------------------
// Purpose: Hammer writes material names in capitals; so does this.
//-----------------------------------------------------------------------------
void CMapEditor::SetCurrentMaterial( const QString &szMaterial )
{
	const QString szClean = szMaterial.trimmed().toUpper();
	if ( szClean.isEmpty() || szClean == m_pMaterial->text() )
	{
		return;
	}
	m_pMaterial->setText( szClean );
	m_pFaceEdit->Refresh();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::BrowseMaterial()
{
	const std::optional<xv::vfs::CGameInfo> &game = m_pDocument->GetGame();
	if ( !game )
	{
		emit StatusMessage( tr( "Open a game to browse its textures" ) );
		return;
	}
	QStringList used;
	for ( const std::string &szMaterial : m_pDocument->GetScene() ? m_pDocument->GetScene()->m_Materials : std::vector<std::string>{} )
	{
		used << QString::fromStdString( szMaterial );
	}
	if ( const std::optional<QString> szPicked = CMaterialBrowser::Pick( this, *game, used, GetCurrentMaterial() ) )
	{
		SetCurrentMaterial( *szPicked );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::SetSelectedFaces( std::vector<xv::map::FaceRef_t> faces )
{
	std::ranges::sort( faces );
	faces.erase( std::ranges::unique( faces ).begin(), faces.end() );
	m_SelectedFaces = std::move( faces );
	m_pFaceEdit->Refresh();
	RepaintTools();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::ToggleFace( const xv::map::FaceRef_t &face )
{
	std::vector<xv::map::FaceRef_t> faces = m_SelectedFaces;
	if ( const auto it = std::ranges::find( faces, face ); it != faces.end() )
	{
		faces.erase( it );
	}
	else
	{
		faces.push_back( face );
	}
	SetSelectedFaces( std::move( faces ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::EditSelectedFaces( const QString &szName, const std::function<void( xv::map::Side_t &, const xv::map::Point_t &, const std::vector<xv::map::Point_t> & )> &pfnEdit )
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	if ( !pScene || m_SelectedFaces.empty() )
	{
		return;
	}
	xv::map::MapEdit_t edit = xv::map::EditFaces( *pScene, m_SelectedFaces, [ & ]( xv::map::Side_t &side, const std::vector<xv::map::Point_t> &points ) {
		const std::optional<xv::map::Plane_t> plane = xv::map::PlaneFromPoints( side.m_Plane );
		pfnEdit( side, plane ? plane->m_Normal : xv::map::Point_t{ 0, 0, 1 }, points );
	} );
	if ( !edit.IsEmpty() )
	{
		m_pDocument->Edit( std::move( edit ), szName );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::ApplyCurrentTexture( bool bValues )
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	const std::string szMaterial = GetCurrentMaterial().trimmed().toUpper().toStdString();
	if ( !pScene || szMaterial.empty() )
	{
		return;
	}
	const std::vector<xv::map::FaceRef_t> faces = !m_SelectedFaces.empty() ? m_SelectedFaces : xv::map::FacesOf( *pScene, m_pDocument->GetSelection() );
	xv::map::MapEdit_t edit = xv::map::EditFaces( *pScene, faces, [ & ]( xv::map::Side_t &side, const std::vector<xv::map::Point_t> & ) {
		xv::map::SetMaterial( pScene->m_Map, side, szMaterial );
		if ( bValues )
		{
			m_pFaceEdit->ApplyValuesTo( side );
		}
	} );
	if ( !edit.IsEmpty() )
	{
		m_pDocument->Edit( std::move( edit ), tr( "Apply Texture" ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Hammer's modes: Ctrl adds or removes the face, Shift takes the whole brush
//          (Ctrl+Shift adds it); Lift puts the face's texture and values in the sheet.
//-----------------------------------------------------------------------------
void CMapEditor::ClickFace( const xv::map::FaceRef_t &face, Qt::KeyboardModifiers modifiers )
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	if ( !pScene || face.m_nSolid >= pScene->m_Map.m_Solids.size() )
	{
		return;
	}
	const FaceEditMode_t nMode = m_pFaceEdit->GetMode();
	if ( nMode == FaceEditMode_t::FACE_EDIT_MODE_APPLY || nMode == FaceEditMode_t::FACE_EDIT_MODE_APPLY_VALUES )
	{
		ApplyToFace( face, nMode == FaceEditMode_t::FACE_EDIT_MODE_APPLY_VALUES );
		return;
	}
	if ( nMode != FaceEditMode_t::FACE_EDIT_MODE_SELECT )
	{
		m_pFaceEdit->LiftFrom( pScene->m_Map.m_Solids[ face.m_nSolid ].m_Sides[ face.m_nSide ] );
	}
	if ( nMode == FaceEditMode_t::FACE_EDIT_MODE_LIFT )
	{
		return;
	}
	std::vector<xv::map::FaceRef_t> picked{ face };
	if ( modifiers.testFlag( Qt::ShiftModifier ) )
	{
		picked = xv::map::FacesOf( *pScene, { pScene->SolidObject( face.m_nSolid ) } );
	}
	if ( modifiers.testFlag( Qt::ControlModifier ) )
	{
		std::vector<xv::map::FaceRef_t> faces = m_SelectedFaces;
		for ( const xv::map::FaceRef_t &one : picked )
		{
			if ( const auto it = std::ranges::find( faces, one ); it != faces.end() && picked.size() == 1 )
			{
				faces.erase( it );
			}
			else if ( it == faces.end() )
			{
				faces.push_back( one );
			}
		}
		SetSelectedFaces( std::move( faces ) );
		return;
	}
	SetSelectedFaces( std::move( picked ) );
}

//-----------------------------------------------------------------------------
// Purpose: The right button's apply: the current texture onto the face under it.
//-----------------------------------------------------------------------------
void CMapEditor::ApplyToFace( const xv::map::FaceRef_t &face, bool bValues )
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	const std::string szMaterial = GetCurrentMaterial().trimmed().toUpper().toStdString();
	if ( !pScene || szMaterial.empty() )
	{
		return;
	}
	xv::map::MapEdit_t edit = xv::map::EditFaces( *pScene, { face }, [ & ]( xv::map::Side_t &side, const std::vector<xv::map::Point_t> & ) {
		xv::map::SetMaterial( pScene->m_Map, side, szMaterial );
		if ( bValues )
		{
			m_pFaceEdit->ApplyValuesTo( side );
		}
	} );
	if ( !edit.IsEmpty() )
	{
		m_pDocument->Edit( std::move( edit ), tr( "Apply Texture" ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::array<int, 2> CMapEditor::GetMaterialSize( std::uint32_t nMaterial ) const
{
	const MapFrame_t frame = m_pRenderData->GetShared();
	if ( frame.m_pMaterials && nMaterial < frame.m_pMaterials->size() )
	{
		const xv::map::ShadeMaterial_t &shade = ( *frame.m_pMaterials )[ nMaterial ].m_Shade;
		const int nWidth = shade.m_pBase ? shade.m_pBase->m_nWidth : shade.m_nWidth;
		const int nHeight = shade.m_pBase ? shade.m_pBase->m_nHeight : shade.m_nHeight;
		if ( nWidth > 0 && nHeight > 0 )
		{
			return { nWidth, nHeight };
		}
	}
	return { 128, 128 };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CMapEditor::GetEntityClass() const
{
	return m_pEntityClass->currentText();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::ShowTransformDialog()
{
	if ( m_pDocument->GetSelection().empty() )
	{
		emit StatusMessage( tr( "Select something to transform first" ) );
		return;
	}
	CMapTransformDialog dialog( this );
	if ( dialog.exec() == QDialog::Accepted )
	{
		ApplyTransformValues( dialog.GetValues() );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Turns go x, then y, then z, as Paste Special's do.
//-----------------------------------------------------------------------------
void CMapEditor::ApplyTransformValues( const TransformValues_t &values )
{
	const xv::map::Box_t bounds = m_pDocument->GetSelectionBounds();
	if ( bounds.IsEmpty() )
	{
		return;
	}
	const xv::map::Point_t centre = bounds.GetCenter();
	const std::array<double, 3> &v = values.m_Values;
	switch ( values.m_nMode )
	{
		case TransformMode_t::TRANSFORM_MODE_ROTATE:
		{
			xv::map::Transform_t turn = xv::map::Transform_t::Rotation( centre, 2, v[ 2 ] ) * xv::map::Transform_t::Rotation( centre, 1, v[ 1 ] ) * xv::map::Transform_t::Rotation( centre, 0, v[ 0 ] );
			turn.m_Pivot = centre;
			TransformSelection( turn, tr( "Rotate" ) );
			break;
		}
		case TransformMode_t::TRANSFORM_MODE_SCALE:
			if ( v[ 0 ] != 0.0 && v[ 1 ] != 0.0 && v[ 2 ] != 0.0 )
			{
				TransformSelection( xv::map::Transform_t::Scaling( centre, { v[ 0 ], v[ 1 ], v[ 2 ] } ), tr( "Scale" ) );
			}
			else
			{
				emit StatusMessage( tr( "A scale of 0 would flatten the selection" ) );
			}
			break;
		case TransformMode_t::TRANSFORM_MODE_MOVE:
			TransformSelection( xv::map::Transform_t::Translation( { v[ 0 ], v[ 1 ], v[ 2 ] } ), tr( "Move" ) );
			break;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::FlipSelection( std::size_t nAxis )
{
	const xv::map::Box_t bounds = m_pDocument->GetSelectionBounds();
	if ( bounds.IsEmpty() || nAxis > 2 )
	{
		return;
	}
	xv::map::Point_t factors{ 1, 1, 1 };
	factors[ nAxis ] = -1;
	TransformSelection( xv::map::Transform_t::Scaling( bounds.GetCenter(), factors ), nAxis == 2 ? tr( "Flip Vertical" ) : tr( "Flip Horizontal" ) );
}

//-----------------------------------------------------------------------------
// Purpose: The dialog remembers its last answers for the session.
//-----------------------------------------------------------------------------
void CMapEditor::ShowPasteSpecialDialog()
{
	if ( QGuiApplication::clipboard()->text().isEmpty() )
	{
		emit StatusMessage( tr( "Nothing on the clipboard to paste into a map" ) );
		return;
	}
	static xv::map::PasteSpecial_t s_Last;
	static bool s_bLastAtView = false;
	CMapPasteSpecialDialog dialog( this );
	dialog.SetValues( s_Last, s_bLastAtView );
	if ( dialog.exec() == QDialog::Accepted )
	{
		s_Last = dialog.GetValues();
		s_bLastAtView = dialog.IsStartAtView();
		PasteSpecial( s_Last, s_bLastAtView );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Starting at the view's centre keeps the objects' own place along the view's depth axis.
//-----------------------------------------------------------------------------
void CMapEditor::PasteSpecial( xv::map::PasteSpecial_t how, bool bStartAtView )
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	const std::string szText = QGuiApplication::clipboard()->text().toStdString();
	if ( !pScene || szText.empty() )
	{
		return;
	}
	if ( bStartAtView )
	{
		const xv::map::Box_t bounds = xv::map::BoundsOfText( szText );
		xv::map::Point_t start = bounds.IsEmpty() ? xv::map::Point_t{} : bounds.GetCenter();
		if ( const auto *p2D = qobject_cast<CMapView2D *>( m_pActiveView ) )
		{
			start[ static_cast<std::size_t>( p2D->GetAxisIndex( 0 ) ) ] = p2D->GetCenter().x();
			start[ static_cast<std::size_t>( p2D->GetAxisIndex( 1 ) ) ] = p2D->GetCenter().y();
		}
		how.m_Start = start;
	}
	std::vector<std::uint32_t> added;
	xv::map::MapEdit_t edit = xv::map::PasteSpecial( *pScene, szText, how, &added );
	if ( edit.IsEmpty() )
	{
		emit StatusMessage( tr( "Nothing on the clipboard to paste into a map" ) );
		return;
	}
	m_pDocument->Edit( std::move( edit ), tr( "Paste Special" ), std::move( added ) );
}

//-----------------------------------------------------------------------------
// Purpose: The dialog remembers its last answers for the session.
//-----------------------------------------------------------------------------
void CMapEditor::ShowReplaceTextures()
{
	static xv::map::ReplaceTextures_t s_Last;
	static bool s_bMarkOnly = false, s_bSelectedOnly = false, s_bHiddenToo = false;
	CMapReplaceTexturesDialog dialog( *this );
	if ( s_Last.m_szFind.empty() && !m_SelectedFaces.empty() && m_pDocument->GetScene() )
	{
		// Starts from the face under the texture tool, as Hammer's does from the current texture.
		const xv::map::CScene &scene = *m_pDocument->GetScene();
		const xv::map::FaceRef_t &face = m_SelectedFaces.front();
		if ( face.m_nSolid < scene.m_Map.m_Solids.size() && face.m_nSide < scene.m_Map.m_Solids[ face.m_nSolid ].m_Sides.size() )
		{
			s_Last.m_szFind = std::string{ scene.m_Map.m_Solids[ face.m_nSolid ].m_Sides[ face.m_nSide ].m_szMaterial };
		}
	}
	dialog.SetValues( s_Last, s_bMarkOnly, s_bSelectedOnly, s_bHiddenToo );
	if ( dialog.exec() != QDialog::Accepted )
	{
		return;
	}
	s_Last = dialog.GetValues();
	s_bMarkOnly = dialog.IsMarkOnly();
	s_bSelectedOnly = dialog.IsSelectedOnly();
	s_bHiddenToo = dialog.IsHiddenToo();
	const std::size_t nFaces = ReplaceTextures( s_Last, s_bMarkOnly, s_bSelectedOnly, s_bHiddenToo );
	emit StatusMessage( s_bMarkOnly ? tr( "%n face(s) marked", nullptr, static_cast<int>( nFaces ) ) : tr( "%n face(s) replaced", nullptr, static_cast<int>( nFaces ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: Sizes for rescaling come from the materials the map has loaded, else from the game's
//          files (the VMT's base texture's VTF header).
//-----------------------------------------------------------------------------
std::size_t CMapEditor::ReplaceTextures( const xv::map::ReplaceTextures_t &how, bool bMarkOnly, bool bSelectedOnly, bool bHiddenToo )
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	if ( !pScene || how.m_szFind.empty() )
	{
		return 0;
	}
	std::vector<std::uint32_t> objects;
	if ( bSelectedOnly )
	{
		objects = m_pDocument->GetSelection();
		if ( objects.empty() )
		{
			return 0;
		}
	}
	else
	{
		for ( std::uint32_t n = 0; n < pScene->GetObjectCount(); n++ )
		{
			if ( !pScene->IsDeleted( n ) && ( bHiddenToo || m_pDocument->IsShown( n ) ) )
			{
				objects.push_back( n );
			}
		}
	}
	const bool bPartial = how.m_nHow != xv::map::ReplaceHow_t::REPLACE_EXACT;
	std::vector<xv::map::FaceRef_t> faces = xv::map::FindFaces( *pScene, objects, how.m_szFind, bPartial );
	if ( bMarkOnly )
	{
		SetSelectedFaces( faces );
		if ( !faces.empty() )
		{
			SetTool( MapTool_t::MAP_TOOL_TEXTURE );
		}
		return faces.size();
	}
	std::optional<xv::vfs::CFileSystem> files;
	if ( how.m_bRescale && m_pDocument->GetGame() )
	{
		files.emplace();
		files->Mount( *m_pDocument->GetGame() );
	}
	auto size = [ & ]( std::string_view szName ) -> std::array<int, 2> {
		for ( std::size_t i = 0; i < pScene->m_Materials.size(); i++ )
		{
			if ( xv::kv1::EqualsIgnoreCase( pScene->m_Materials[ i ], szName ) )
			{
				return GetMaterialSize( static_cast<std::uint32_t>( i ) );
			}
		}
		if ( files )
		{
			const xv::material::Reader_t read = [ & ]( std::string_view szPath ) { return files->Read( szPath ); };
			const xv::map::MaterialInfo_t info = xv::map::ReadMaterialInfo( szName, read );
			if ( info.m_bFound && !info.m_szTexture.empty() )
			{
				if ( const auto data = files->Read( "materials/" + info.m_szTexture + ".vtf" ) )
				{
					if ( const std::optional<xv::texture::CVtf> vtf = xv::texture::ReadVtf( *data ) )
					{
						return { vtf->m_Info.m_nWidth, vtf->m_Info.m_nHeight };
					}
				}
			}
		}
		return { 0, 0 };
	};
	std::size_t nChanged = 0;
	xv::map::MapEdit_t edit = xv::map::ReplaceTextures( *pScene, faces, how, how.m_bRescale ? size : std::function<std::array<int, 2>( std::string_view )>{}, &nChanged );
	if ( !edit.IsEmpty() )
	{
		m_pDocument->Edit( std::move( edit ), tr( "Replace Textures" ) );
	}
	return nChanged;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapEntityReport *CMapEditor::ShowEntityReport()
{
	if ( !m_pEntityReport )
	{
		m_pEntityReport = new CMapEntityReport( *this );
	}
	m_pEntityReport->show();
	m_pEntityReport->raise();
	m_pEntityReport->activateWindow();
	return m_pEntityReport;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapCheckDialog *CMapEditor::ShowCheckForProblems()
{
	if ( !m_pCheck )
	{
		m_pCheck = new CMapCheckDialog( *this );
	}
	else
	{
		m_pCheck->Run();
	}
	m_pCheck->show();
	m_pCheck->raise();
	m_pCheck->activateWindow();
	return m_pCheck;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::ShowFindEntities()
{
	bool bOk = false;
	const QString szName = QInputDialog::getText( this, tr( "Find Entities" ), tr( "Entity name (targetname):" ), QLineEdit::Normal, QString(), &bOk ).trimmed();
	if ( bOk && !szName.isEmpty() && !FindEntitiesByName( szName ) )
	{
		emit StatusMessage( tr( "No entity is named %1" ).arg( szName ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapEditor::FindEntitiesByName( const QString &szName )
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	if ( !pScene )
	{
		return false;
	}
	std::vector<std::uint32_t> found = xv::map::FindEntities( *pScene, szName.toStdString() );
	if ( found.empty() )
	{
		return false;
	}
	m_pDocument->SetSelection( std::move( found ) );
	const xv::map::Box_t bounds = m_pDocument->GetSelectionBounds();
	if ( !bounds.IsEmpty() )
	{
		CenterViewsOn( bounds.GetCenter() );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: What Edit > Copy put on the clipboard (or VMF text from anywhere, Hammer's too), a grid
//          step along the view's axes from where it was, selected.
//-----------------------------------------------------------------------------
void CMapEditor::Paste( CMapView *pFrom )
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	const std::string szText = QGuiApplication::clipboard()->text().toStdString();
	if ( !pScene || szText.empty() )
	{
		return;
	}
	xv::map::Transform_t move;
	const auto nGrid = static_cast<double>( m_Settings.m_nGridSize );
	const auto *p2D = qobject_cast<CMapView2D *>( pFrom );
	move.m_Offset[ p2D ? static_cast<std::size_t>( p2D->GetAxisIndex( 0 ) ) : 0 ] = nGrid;
	move.m_Offset[ p2D ? static_cast<std::size_t>( p2D->GetAxisIndex( 1 ) ) : 1 ] = nGrid;
	std::vector<std::uint32_t> added;
	xv::map::MapEdit_t edit = xv::map::ObjectsFromText( *pScene, szText, move, &added );
	if ( edit.IsEmpty() )
	{
		emit StatusMessage( tr( "Nothing on the clipboard to paste into a map" ) );
		return;
	}
	m_pDocument->Edit( std::move( edit ), tr( "Paste" ), std::move( added ) );
}

//-----------------------------------------------------------------------------
// Purpose: Ctrl+B: the selection moved so its box's lowest corner is on the grid.
//-----------------------------------------------------------------------------
void CMapEditor::SnapSelection()
{
	const xv::map::Box_t bounds = m_pDocument->GetSelectionBounds();
	if ( bounds.IsEmpty() )
	{
		return;
	}
	const auto nGrid = static_cast<double>( m_Settings.m_nGridSize );
	xv::map::Point_t offset{};
	for ( std::size_t a = 0; a < 3; a++ )
	{
		offset[ a ] = std::round( bounds.m_Mins[ a ] / nGrid ) * nGrid - bounds.m_Mins[ a ];
	}
	if ( offset != xv::map::Point_t{} )
	{
		MoveSelection( offset, false );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CMapEditor::GetTabTitle() const
{
	return ( m_pDocument->IsModified() ? QStringLiteral( "*" ) : QString{} ) + GetFileName();
}

//-----------------------------------------------------------------------------
// Purpose: A new map is "Untitled" until it is saved.
//-----------------------------------------------------------------------------
QString CMapEditor::GetFileName() const
{
	return m_szPath.isEmpty() ? tr( "Untitled" ) : QFileInfo{ m_szPath }.fileName();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QUndoStack *CMapEditor::GetUndoStack() const
{
	return m_pDocument->GetUndoStack();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapEditor::ConfirmClose()
{
	if ( !m_pDocument->IsModified() )
	{
		return true;
	}
	const auto nAnswer = QMessageBox::question( this, tr( "Save Changes" ), tr( "%1 has changes that aren't saved. Save them?" ).arg( GetFileName() ), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save );
	return nAnswer == QMessageBox::Discard || ( nAnswer == QMessageBox::Save && Save() );
}

//-----------------------------------------------------------------------------
// Purpose: Over the file it was read from; a map from the game's files, or a new one, is saved
//          as a file of its own (Save As).
//-----------------------------------------------------------------------------
bool CMapEditor::Save()
{
	if ( m_bFromGame || m_szPath.isEmpty() || !m_pDocument->IsLoaded() )
	{
		return m_pDocument->IsLoaded() && SaveAs();
	}
	QString szError;
	if ( !m_pDocument->Save( std::filesystem::path{ m_szPath.toStdString() }, &szError ) )
	{
		QMessageBox::critical( this, tr( "Save failed" ), tr( "Could not write %1:\n%2" ).arg( QDir::toNativeSeparators( m_szPath ), szError ) );
		return false;
	}
	emit StatusMessage( tr( "Saved %1" ).arg( QDir::toNativeSeparators( m_szPath ) ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapEditor::SaveAs()
{
	if ( !m_pDocument->IsLoaded() )
	{
		return false;
	}
	// A bare name starts in the folder the last file window finished in.
	const QString szStart = m_szPath.isEmpty() ? QStringLiteral( "untitled.vmf" ) : m_bFromGame ? QFileInfo{ m_szPath }.fileName()
																								: m_szPath;
	const QString szFile = AskSaveFile( this, tr( "Save Map As" ), szStart, tr( "Hammer maps (*.vmf)" ) );
	if ( szFile.isEmpty() )
	{
		return false;
	}
	QString szError;
	if ( !m_pDocument->Save( std::filesystem::path{ szFile.toStdString() }, &szError ) )
	{
		QMessageBox::critical( this, tr( "Save failed" ), tr( "Could not write %1:\n%2" ).arg( QDir::toNativeSeparators( szFile ), szError ) );
		return false;
	}
	m_szPath = QFileInfo{ szFile }.absoluteFilePath();
	m_bFromGame = false;
	emit TitleChanged();
	emit StatusMessage( tr( "Saved %1" ).arg( QDir::toNativeSeparators( m_szPath ) ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::SceneEdited( const std::vector<std::uint32_t> &changed )
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	// Materials the edit brought in: read into the places kept for them, or everything again
	// when those are used up.
	std::vector<std::uint32_t> added;
	for ( std::uint32_t n = m_nMaterialsRead; pScene && n < pScene->m_Materials.size(); n++ )
	{
		added.push_back( n );
	}
	if ( pScene )
	{
		m_nMaterialsRead = static_cast<std::uint32_t>( pScene->m_Materials.size() );
	}
	// Another sky (Map Properties): everything is read again, the sky with it.
	const bool bSkyChanged = pScene && std::string_view{ m_szSkyRead } != pScene->m_Map.m_World.GetValue( "skyname" );
	if ( bSkyChanged || ( !added.empty() && !m_pResources->LoadMaterials( m_pDocument->GetSharedScene(), added ) ) )
	{
		StartResources();
	}
	// Entities moved, added or deleted: their sprites and props follow.
	else if ( pScene && std::ranges::any_of( changed, [ pScene ]( std::uint32_t n ) { return n < pScene->GetObjectCount() && !pScene->IsSolidObject( n ); } ) )
	{
		m_pResources->Refresh( m_pDocument->GetSharedScene() );
	}
	for ( CMapView *pView : GetViews() )
	{
		if ( auto *p2D = qobject_cast<CMapView2D *>( pView ) )
		{
			p2D->ObjectsChanged( changed );
		}
		else if ( auto *p3D = qobject_cast<CMapView3D *>( pView ) )
		{
			p3D->Redraw();
		}
		pView->update();
	}
	m_pProperties->Refresh();
	m_pFaceEdit->Refresh();
	UpdateStatus();
}

//-----------------------------------------------------------------------------
// Purpose: Hammer opens the world's properties as a window; so does this.
//-----------------------------------------------------------------------------
void CMapEditor::ShowMapProperties()
{
	if ( !m_pDocument->IsLoaded() )
	{
		return;
	}
	auto *pDialog = new QDialog( this );
	pDialog->setObjectName( QStringLiteral( "mapPropertiesDialog" ) );
	pDialog->setWindowTitle( tr( "Map Properties" ) );
	pDialog->setAttribute( Qt::WA_DeleteOnClose );
	pDialog->resize( 420, 520 );
	auto *pLayout = new QVBoxLayout( pDialog );
	auto *pPanel = new CMapProperties( *m_pDocument );
	pPanel->setObjectName( QStringLiteral( "mapWorldProperties" ) );
	pPanel->SetWorld( true );
	pLayout->addWidget( pPanel, 1 );
	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Close );
	connect( pButtons, &QDialogButtonBox::rejected, pDialog, &QDialog::close );
	pLayout->addWidget( pButtons );
	connect( m_pDocument, &CMapDocument::SceneEdited, pPanel, &CMapProperties::Refresh );
	pDialog->show();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CMapEditor::GetSessionEntry() const
{
	if ( m_szPath.isEmpty() )
	{
		return {}; // a new map has nothing to open again
	}
	return QStringLiteral( "map\t%1\t%2" ).arg( m_bFromGame ? QStringLiteral( "game" ) : QStringLiteral( "disk" ), m_szPath );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::StopWork()
{
	RememberCamera();
	m_pDocument->Stop();
	m_pResources->Stop();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::GameChanged( const xv::vfs::CGameInfo *pGame )
{
	m_pDocument->SetGame( pGame ? std::optional<xv::vfs::CGameInfo>{ *pGame } : std::nullopt );
	if ( m_pDocument->IsLoaded() )
	{
		StartResources();
	}
}

//-----------------------------------------------------------------------------
// Purpose: More textures: the 3D views draw with them.
//-----------------------------------------------------------------------------
void CMapEditor::ResourcesArrived()
{
	m_pRenderData->SetMaterials( m_pResources->GetMaterials(), m_pResources->GetTextures(), m_pResources->GetEntityMaterial() );
	m_pRenderData->SetSky( m_pResources->GetSky() );
	m_pRenderData->SetProps( m_pResources->GetProps(), m_pResources->GetPropObjects() );
	m_pRenderData->SetSprites( m_pResources->GetSprites() );
	m_pRenderData->SetOverlays( m_pResources->GetOverlays() );
	for ( CMapView *pView : GetViews() )
	{
		if ( auto *p3D = qobject_cast<CMapView3D *>( pView ) )
		{
			p3D->Redraw();
		}
		else
		{
			// The props' models, once they come.
			pView->update();
		}
	}
	UpdateGameStatus();
}

//-----------------------------------------------------------------------------
// Purpose: Where entities' looks come from, and how far the textures have got.
//-----------------------------------------------------------------------------
void CMapEditor::UpdateGameStatus()
{
	const QString szFgd = m_pDocument->GetFgdSource();
	QString szText = szFgd.isEmpty() ? tr( "No game data: entities show as boxes" ) : tr( "Entities from %1" ).arg( szFgd );
	if ( !m_pDocument->GetGame() )
	{
		szText += tr( "; open the game to see textures" );
	}
	else if ( !m_pResources->IsFinished() )
	{
		szText += tr( "; textures %1 of %2" ).arg( m_pResources->GetLoaded() ).arg( m_pResources->GetTotal() );
	}
	else if ( m_pResources->GetModelsLoaded() < m_pResources->GetModelsTotal() )
	{
		// The others show as boxes.
		szText += tr( "; %1 of %2 models read" ).arg( m_pResources->GetModelsLoaded() ).arg( m_pResources->GetModelsTotal() );
	}
	m_pStatusGame->setText( szText );
	m_pStatusGame->setToolTip( szText );
}

//-----------------------------------------------------------------------------
// Purpose: Alt+Enter: Object Properties, brought out if the side was dragged shut.
//-----------------------------------------------------------------------------
void CMapEditor::ShowProperties()
{
	if ( auto *pMiddle = qobject_cast<QSplitter *>( m_pSidePanel->parentWidget() ) )
	{
		QList<int> sizes = pMiddle->sizes();
		if ( sizes.size() == 2 && sizes[ 1 ] == 0 )
		{
			const int nSide = std::max( m_pSidePanel->minimumWidth(), 280 );
			pMiddle->setSizes( { std::max( sizes[ 0 ] - nSide, 0 ), nSide } );
		}
	}
	m_pProperties->FocusKeys();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<xv::map::CViewCamera> CMapEditor::GetCamera() const
{
	for ( CMapView *pView : GetViews() )
	{
		if ( const auto *p3D = qobject_cast<const CMapView3D *>( pView ) )
		{
			// With the view's shape, for the 2D views' drawing of what it sees.
			xv::map::CViewCamera camera = p3D->GetCamera();
			camera.m_nWidth = std::max( 1, p3D->width() );
			camera.m_nHeight = std::max( 1, p3D->height() );
			return camera;
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: The settings key a map's camera is kept under: its file, on disk or in the game.
//-----------------------------------------------------------------------------
QString CMapEditor::GetCameraKey() const
{
	const QString szWhere = m_bFromGame ? QStringLiteral( "game:" ) + m_szPath : QStringLiteral( "disk:" ) + QFileInfo( m_szPath ).absoluteFilePath();
	return QStringLiteral( "mapCameras/" ) + QString::fromLatin1( QCryptographicHash::hash( szWhere.toUtf8(), QCryptographicHash::Md5 ).toHex() );
}

//-----------------------------------------------------------------------------
// Purpose: Keeps where the 3D camera is, as Hammer++ keeps it in the map when it saves; here it
//          goes in the settings, so a map that isn't saved keeps one too. Only the most recent
//          maps' are kept, and none for a map that has no file yet.
//-----------------------------------------------------------------------------
void CMapEditor::RememberCamera() const
{
	const std::optional<xv::map::CViewCamera> camera = GetCamera();
	if ( !camera || !m_pDocument->IsLoaded() || m_szPath.isEmpty() )
	{
		return;
	}
	constexpr int MAX_MAPS = 64;
	QSettings settings;
	const QVariantList entry{ camera->m_Eye[ 0 ], camera->m_Eye[ 1 ], camera->m_Eye[ 2 ], camera->m_flYaw, camera->m_flPitch, QDateTime::currentMSecsSinceEpoch() };
	settings.setValue( GetCameraKey(), entry );
	settings.beginGroup( QStringLiteral( "mapCameras" ) );
	QStringList keys = settings.childKeys();
	if ( keys.size() > MAX_MAPS )
	{
		std::ranges::sort( keys, [ & ]( const QString &a, const QString &b ) { return settings.value( a ).toList().value( 5 ).toLongLong() < settings.value( b ).toList().value( 5 ).toLongLong(); } );
		for ( qsizetype i = 0; i < keys.size() - MAX_MAPS; i++ )
		{
			settings.remove( keys[ i ] );
		}
	}
	settings.endGroup();
}

//-----------------------------------------------------------------------------
// Purpose: The camera kept for this map, if it is newer than the file on disk.
//-----------------------------------------------------------------------------
std::optional<xv::map::CViewCamera> CMapEditor::RecallCamera() const
{
	const QVariantList entry = m_szPath.isEmpty() ? QVariantList{} : QSettings{}.value( GetCameraKey() ).toList();
	if ( entry.size() < 6 )
	{
		return std::nullopt;
	}
	if ( !m_bFromGame )
	{
		const QDateTime modified = QFileInfo( m_szPath ).lastModified();
		if ( modified.isValid() && modified.toMSecsSinceEpoch() > entry[ 5 ].toLongLong() )
		{
			return std::nullopt;
		}
	}
	xv::map::CViewCamera camera;
	camera.m_Eye = { entry[ 0 ].toDouble(), entry[ 1 ].toDouble(), entry[ 2 ].toDouble() };
	camera.m_flYaw = entry[ 3 ].toDouble();
	camera.m_flPitch = std::clamp( entry[ 4 ].toDouble(), -89.0, 89.0 );
	return camera;
}

//-----------------------------------------------------------------------------
// Purpose: Whether any 3D view draws in software, which needs the textures decoded (the card
//          takes the VTFs' DXT mips as they are, at a fraction of the memory). Tests that compare
//          the two set VOMIT_TOOLS_DECODE_TEXTURES.
//-----------------------------------------------------------------------------
bool CMapEditor::NeedsDecodedTextures() const
{
	if ( qEnvironmentVariableIsSet( "VOMIT_TOOLS_DECODE_TEXTURES" ) )
	{
		return true;
	}
	for ( CMapView *pView : GetViews() )
	{
		if ( const auto *p3D = qobject_cast<const CMapView3D *>( pView ); p3D && !p3D->IsUsingGpu() )
		{
			return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::StartResources()
{
	if ( const xv::map::CScene *pScene = m_pDocument->GetScene() )
	{
		m_szSkyRead = std::string{ pScene->m_Map.m_World.GetValue( "skyname" ) };
	}
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	m_nMaterialsRead = pScene ? static_cast<std::uint32_t>( pScene->m_Materials.size() ) : 0;
	m_pResources->Start( m_pDocument->GetSharedScene(), m_pDocument->GetGame(), m_nTextureSize, NeedsDecodedTextures() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::RendererChanged()
{
	if ( m_pDocument->IsLoaded() && !m_pResources->IsDecoded() && NeedsDecodedTextures() )
	{
		StartResources();
	}
}

//-----------------------------------------------------------------------------
// Purpose: The 2D views draw the camera, so they follow it.
//-----------------------------------------------------------------------------
void CMapEditor::CameraMoved( CMapView *pView )
{
	for ( CMapView *pOther : GetViews() )
	{
		if ( pOther != pView && Is2DView( pOther->GetType() ) )
		{
			pOther->update();
		}
	}
}
