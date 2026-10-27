//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See PreferencesDialog.h.
//
//===========================================================================//
#include "PreferencesDialog.h"

#include <utility>
#include <vector>

#include <QButtonGroup>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QRadioButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "DebugLog.h"
#include "FileActions.h"
#include "FileDialogs.h"
#include "Graphics.h"
#include "InterfaceMode.h"
#include "Preferences.h"
#include "SourceTools.h"
#include <QLineEdit>
#include <QSettings>
#include <QFileInfo>

namespace
{

//-----------------------------------------------------------------------------
// Purpose: A line under a setting saying what it does.
//-----------------------------------------------------------------------------
QLabel *Hint( const QString &szText )
{
	auto *pLabel = new QLabel( szText );
	pLabel->setWordWrap( true );
	pLabel->setForegroundRole( QPalette::PlaceholderText );
	return pLabel;
}

//-----------------------------------------------------------------------------
// Purpose: A page's layout: its settings from the top, the rest left empty.
//-----------------------------------------------------------------------------
QVBoxLayout *PageLayout( QWidget *pPage )
{
	auto *pLayout = new QVBoxLayout( pPage );
	pLayout->setContentsMargins( 12, 0, 0, 0 );
	return pLayout;
}

//-----------------------------------------------------------------------------
// Purpose: Pages a local build adds.
//-----------------------------------------------------------------------------
std::vector<std::pair<QString, std::function<QWidget *()>>> &LocalPages()
{
	static std::vector<std::pair<QString, std::function<QWidget *()>>> pages;
	return pages;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CPreferencesDialog::CPreferencesDialog( QWidget *pParent )
	: QDialog( pParent )
{
	setObjectName( QStringLiteral( "preferences" ) );
	setWindowTitle( tr( "Preferences" ) );
	m_pPages = new QListWidget;
	m_pPages->setObjectName( QStringLiteral( "preferencesPages" ) );
	m_pPages->setMaximumWidth( 160 );
	m_pStack = new QStackedWidget;
	connect( m_pPages, &QListWidget::currentRowChanged, m_pStack, &QStackedWidget::setCurrentIndex );

	AddPage( tr( "Interface" ), BuildInterfacePage() );
	AddPage( tr( "Graphics" ), BuildGraphicsPage() );
	AddPage( tr( "Particles" ), BuildParticlesPage() );
	AddPage( tr( "Materials" ), BuildMaterialsPage() );
	AddPage( tr( "Maps" ), BuildMapsPage() );
	AddPage( tr( "Source Tools" ), BuildSourceToolsPage() );
	AddPage( tr( "Advanced" ), BuildAdvancedPage() );
	for ( const auto &[ szTitle, pfnMake ] : LocalPages() )
	{
		if ( QWidget *pPage = pfnMake() )
		{
			AddPage( szTitle, pPage );
		}
	}
	m_pPages->setCurrentRow( 0 );

	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Close );
	connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
	auto *pColumns = new QHBoxLayout;
	pColumns->addWidget( m_pPages );
	pColumns->addWidget( m_pStack, 1 );
	auto *pLayout = new QVBoxLayout( this );
	pLayout->addLayout( pColumns, 1 );
	pLayout->addWidget( pButtons );
	resize( 620, 380 );
}

//-----------------------------------------------------------------------------
// Purpose: Adds a page to Preferences from code a local build brings in.
//-----------------------------------------------------------------------------
void AddLocalPreferencesPage( const QString &szTitle, std::function<QWidget *()> pfnMake )
{
	LocalPages().emplace_back( szTitle, std::move( pfnMake ) );
}

//-----------------------------------------------------------------------------
// Purpose: Turns to a page by its title.
//-----------------------------------------------------------------------------
void CPreferencesDialog::ShowPage( const QString &szTitle )
{
	for ( int i = 0; i < m_pPages->count(); i++ )
	{
		if ( m_pPages->item( i )->text() == szTitle )
		{
			m_pPages->setCurrentRow( i );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferencesDialog::AddPage( const QString &szTitle, QWidget *pPage )
{
	m_pPages->addItem( szTitle );
	m_pStack->addWidget( pPage );
}

//-----------------------------------------------------------------------------
// Purpose: The simple or regular interface.
//-----------------------------------------------------------------------------
QWidget *CPreferencesDialog::BuildInterfacePage()
{
	auto *pPage = new QWidget;
	auto *pSimple = new QCheckBox( tr( "Simple interface" ) );
	pSimple->setObjectName( QStringLiteral( "preferencesSimple" ) );
	pSimple->setChecked( CInterfaceMode::Instance().IsSimple() );
	connect( pSimple, &QCheckBox::toggled, this, []( bool bOn ) { CInterfaceMode::Instance().SetSimple( bOn ); } );
	connect( &CInterfaceMode::Instance(), &CInterfaceMode::Changed, pSimple, &QCheckBox::setChecked );
	QVBoxLayout *pLayout = PageLayout( pPage );
	pLayout->addWidget( pSimple );
	pLayout->addWidget( Hint( tr( "Plain names with a line on what each setting does, easier ways to type values, and advanced "
								  "settings tucked away while they have their usual value. Off: the files' own names and every "
								  "setting. Either way the files are edited the same. View → Simple Interface switches it too." ) ) );
	auto *pSingle = new QCheckBox( tr( "Open files in the window already open" ) );
	pSingle->setObjectName( QStringLiteral( "preferencesSingleWindow" ) );
	pSingle->setChecked( CPreferences::Instance().IsSingleWindow() );
	connect( pSingle, &QCheckBox::toggled, this, []( bool bOn ) { CPreferences::Instance().SetSingleWindow( bOn ); } );
	pLayout->addSpacing( 12 );
	pLayout->addWidget( pSingle );
	pLayout->addWidget( Hint( tr( "Files opened from a file manager or the command line become tabs in the running window "
								  "instead of starting another. Takes effect from the next start." ) ) );
	auto *pManifest = new QCheckBox( tr( "Offer to add saved files to the game's manifest" ) );
	pManifest->setObjectName( QStringLiteral( "preferencesManifestReminder" ) );
	pManifest->setChecked( CPreferences::Instance().IsManifestReminder() );
	connect( pManifest, &QCheckBox::toggled, this, []( bool bOn ) { CPreferences::Instance().SetManifestReminder( bOn ); } );
	pLayout->addSpacing( 12 );
	pLayout->addWidget( pManifest );
	pLayout->addWidget( Hint( tr( "When a soundscape, soundscript or particle file is saved and its manifest doesn't list it, ask "
								  "whether to add it. \"Don't remind me again\" in that question turns this off." ) ) );
	auto *pReopen = new QCheckBox( tr( "Reopen the files that were open at closing" ) );
	pReopen->setObjectName( QStringLiteral( "preferencesReopenSession" ) );
	pReopen->setChecked( CPreferences::Instance().IsReopenSession() );
	connect( pReopen, &QCheckBox::toggled, this, []( bool bOn ) { CPreferences::Instance().SetReopenSession( bOn ); } );
	pLayout->addSpacing( 12 );
	pLayout->addWidget( pReopen );
	pLayout->addWidget( Hint( tr( "At the next start, the tabs come back as they were left, with the folder browser's folder. "
								  "Files opened from the command line or a file manager open instead." ) ) );
#ifdef VOMIT_TOOLS_PORTAL
	auto *pChooser = new QCheckBox( tr( "Use the desktop's file chooser" ) );
	pChooser->setObjectName( QStringLiteral( "preferencesDesktopFileChooser" ) );
	pChooser->setChecked( CPreferences::Instance().IsDesktopFileChooser() );
	connect( pChooser, &QCheckBox::toggled, this, []( bool bOn ) { CPreferences::Instance().SetDesktopFileChooser( bOn ); } );
	pLayout->addSpacing( 12 );
	pLayout->addWidget( pChooser );
	pLayout->addWidget( Hint( tr( "Open and save windows are the file chooser your desktop is set up with (KDE's, GNOME's, a "
								  "terminal file manager's...), asked for through the XDG desktop portal. Off, or with no portal "
								  "running: Qt's own window. Either way it starts in the folder the last one finished in." ) ) );
#endif
	pLayout->addStretch();
	return pPage;
}

//-----------------------------------------------------------------------------
// Purpose: What the previews draw with.
//-----------------------------------------------------------------------------
QWidget *CPreferencesDialog::BuildGraphicsPage()
{
	auto *pPage = new QWidget;
	// Inline choices avoid reusing a popup whose Wayland blur effect can outlive its surface
	// with Kvantum and KWindowSystem versions before 6.32.
	auto *pApi = new QButtonGroup( pPage );
	pApi->setObjectName( QStringLiteral( "preferencesGraphics" ) );
	auto *pApis = new QVBoxLayout;
	pApis->setContentsMargins( 0, 0, 0, 0 );
	for ( GraphicsApi_t nApi : CGraphics::Choices() )
	{
		auto *pChoice = new QRadioButton( GraphicsApiName( nApi ) );
		pApi->addButton( pChoice, static_cast<int>( nApi ) );
		pApis->addWidget( pChoice );
	}
	QAbstractButton *pChosen = pApi->button( static_cast<int>( CPreferences::Instance().GetGraphicsApi() ) );
	( pChosen ? pChosen : pApi->buttons().front() )->setChecked( true );
	const CGraphics &graphics = CGraphics::Instance();
	QString szNow = tr( "Now: %1" ).arg( graphics.GetDescription() );
	if ( !graphics.GetProblem().isEmpty() )
	{
		szNow += u'\n' + tr( "Not used:\n%1" ).arg( graphics.GetProblem() );
	}
	auto *pNow = new QLabel( szNow );
	pNow->setObjectName( QStringLiteral( "preferencesGraphicsNow" ) );
	pNow->setWordWrap( true );
	pNow->setTextInteractionFlags( Qt::TextSelectableByMouse );
	auto *pRestart = Hint( tr( "Takes effect the next time vomit-tools starts." ) );
	pRestart->setVisible( false );
	connect( pApi, &QButtonGroup::idClicked, this, [ pRestart ]( int nApi ) {
		CPreferences::Instance().SetGraphicsApi( static_cast<GraphicsApi_t>( nApi ) );
		pRestart->setVisible( true );
	} );
	auto *pForm = new QFormLayout;
	pForm->setContentsMargins( 0, 0, 0, 0 );
	pForm->addRow( tr( "Graphics Backend" ), pApis );
	QVBoxLayout *pLayout = PageLayout( pPage );
	pLayout->addLayout( pForm );
	pLayout->addWidget( Hint( tr( "The sky, material and particle previews and the particle grid draw on the graphics card "
								  "through the API picked here. Automatic tries Vulkan first, then the platform's usual one. "
								  "Software draws on the CPU: slower, but it works anywhere." ) ) );
	pLayout->addWidget( pNow );
	pLayout->addWidget( pRestart );
	pLayout->addStretch();
	return pPage;
}

//-----------------------------------------------------------------------------
// Purpose: The particle editor.
//-----------------------------------------------------------------------------
QWidget *CPreferencesDialog::BuildParticlesPage()
{
	auto *pPage = new QWidget;
	auto *pAll = new QCheckBox( tr( "Animate every system in the grid preview" ) );
	pAll->setObjectName( QStringLiteral( "preferencesGridAnimation" ) );
	pAll->setChecked( CPreferences::Instance().IsGridAnimatingAll() );
	connect( pAll, &QCheckBox::toggled, this, []( bool bOn ) { CPreferences::Instance().SetGridAnimatingAll( bOn ); } );
	QVBoxLayout *pLayout = PageLayout( pPage );
	pLayout->addWidget( pAll );
	pLayout->addStretch();
	return pPage;
}

//-----------------------------------------------------------------------------
// Purpose: The material editor: a list of parameters or the text.
//-----------------------------------------------------------------------------
QWidget *CPreferencesDialog::BuildMaterialsPage()
{
	auto *pPage = new QWidget;
	auto *pVisual = new QRadioButton( tr( "Edit materials as a list of parameters" ) );
	pVisual->setObjectName( QStringLiteral( "preferencesMaterialVisual" ) );
	auto *pText = new QRadioButton( tr( "Edit materials as text" ) );
	pText->setObjectName( QStringLiteral( "preferencesMaterialText" ) );
	( CPreferences::Instance().IsMaterialVisual() ? pVisual : pText )->setChecked( true );
	connect( pVisual, &QRadioButton::toggled, this, []( bool bOn ) { CPreferences::Instance().SetMaterialVisual( bOn ); } );
	connect( &CPreferences::Instance(), &CPreferences::MaterialEditingChanged, pPage, [ pVisual, pText ] {
		( CPreferences::Instance().IsMaterialVisual() ? pVisual : pText )->setChecked( true );
	} );
	auto *pLive = new QCheckBox( tr( "Redraw the preview at each edit in the list" ) );
	pLive->setObjectName( QStringLiteral( "preferencesMaterialLive" ) );
	pLive->setChecked( CPreferences::Instance().IsMaterialLive() );
	connect( pLive, &QCheckBox::toggled, this, []( bool bOn ) { CPreferences::Instance().SetMaterialLive( bOn ); } );
	connect( &CPreferences::Instance(), &CPreferences::MaterialEditingChanged, pLive, [ pLive ] { pLive->setChecked( CPreferences::Instance().IsMaterialLive() ); } );
	QVBoxLayout *pLayout = PageLayout( pPage );
	pLayout->addWidget( pVisual );
	pLayout->addWidget( pText );
	pLayout->addWidget( Hint( tr( "The list gives each parameter a box that suits it and adds more from a right-click; the text "
								  "is the file as it is, coloured. Either way the same file is edited, and the switch above the "
								  "material changes it too." ) ) );
	pLayout->addSpacing( 12 );
	pLayout->addWidget( pLive );
	pLayout->addWidget( Hint( tr( "Off: the preview waits for Refresh, for heavy materials. Typing in the text always redraws "
								  "it after a pause." ) ) );
	pLayout->addStretch();
	return pPage;
}

//-----------------------------------------------------------------------------
// Purpose: The map viewer: where entities' game data comes from, the 3D views' camera, and the
//          2D views' grid and drawing. Every change applies at once.
//-----------------------------------------------------------------------------
QWidget *CPreferencesDialog::BuildMapsPage()
{
	auto *pPage = new QWidget;
	const MapPreferences_t &current = CPreferences::Instance().GetMapPreferences();
	auto change = []( auto pfnEdit ) {
		MapPreferences_t preferences = CPreferences::Instance().GetMapPreferences();
		pfnEdit( preferences );
		CPreferences::Instance().SetMapPreferences( preferences );
	};

	// Game data: a list of FGDs, in the order they are read.
	auto *pFgds = new QListWidget;
	pFgds->setObjectName( QStringLiteral( "preferencesMapFgds" ) );
	pFgds->addItems( current.m_Fgds );
	pFgds->setMaximumHeight( 110 );
	auto storeFgds = [ pFgds, change ] {
		QStringList fgds;
		for ( int i = 0; i < pFgds->count(); i++ )
		{
			fgds << pFgds->item( i )->text();
		}
		change( [ & ]( MapPreferences_t &preferences ) { preferences.m_Fgds = fgds; } );
	};
	auto *pAdd = new QPushButton( tr( "Add…" ) );
	pAdd->setObjectName( QStringLiteral( "preferencesMapFgdAdd" ) );
	auto *pRemove = new QPushButton( tr( "Remove" ) );
	pRemove->setObjectName( QStringLiteral( "preferencesMapFgdRemove" ) );
	auto *pUp = new QPushButton( tr( "Up" ) );
	auto *pDown = new QPushButton( tr( "Down" ) );
	connect( pAdd, &QPushButton::clicked, this, [ this, pFgds, storeFgds ] {
		for ( const QString &szFile : AskOpenFiles( this, tr( "Add Game Data" ), {}, tr( "Game data (*.fgd)" ) ) )
		{
			pFgds->addItem( QDir::toNativeSeparators( szFile ) );
		}
		storeFgds();
	} );
	connect( pRemove, &QPushButton::clicked, this, [ pFgds, storeFgds ] {
		delete pFgds->currentItem();
		storeFgds();
	} );
	auto move = [ pFgds, storeFgds ]( int nBy ) {
		const int nRow = pFgds->currentRow();
		if ( nRow < 0 || nRow + nBy < 0 || nRow + nBy >= pFgds->count() )
		{
			return;
		}
		QListWidgetItem *pItem = pFgds->takeItem( nRow );
		pFgds->insertItem( nRow + nBy, pItem );
		pFgds->setCurrentItem( pItem );
		storeFgds();
	};
	connect( pUp, &QPushButton::clicked, this, [ move ] { move( -1 ); } );
	connect( pDown, &QPushButton::clicked, this, [ move ] { move( 1 ); } );
	auto *pFgdButtons = new QHBoxLayout;
	for ( QPushButton *pButton : { pAdd, pRemove, pUp, pDown } )
	{
		pFgdButtons->addWidget( pButton );
	}
	pFgdButtons->addStretch();

	// The 3D views.
	auto spin = [ & ]( const QString &szName, double flValue, double flLowest, double flHighest, double flStep, int nDecimals, auto pfnStore ) {
		auto *pSpin = new QDoubleSpinBox;
		pSpin->setObjectName( szName );
		pSpin->setRange( flLowest, flHighest );
		pSpin->setSingleStep( flStep );
		pSpin->setDecimals( nDecimals );
		pSpin->setValue( flValue );
		connect( pSpin, &QDoubleSpinBox::valueChanged, this, [ change, pfnStore ]( double flNew ) { change( [ & ]( MapPreferences_t &preferences ) { pfnStore( preferences, flNew ); } ); } );
		return pSpin;
	};
	auto *pForm = new QFormLayout;
	pForm->addRow( tr( "Field of view" ), spin( QStringLiteral( "preferencesMapFov" ), current.m_flFov, 30, 150, 5, 0, []( MapPreferences_t &p, double fl ) { p.m_flFov = fl; } ) );
	pForm->addRow( tr( "Mouse look speed" ), spin( QStringLiteral( "preferencesMapSensitivity" ), current.m_flSensitivity, 0.05, 5, 0.05, 2, []( MapPreferences_t &p, double fl ) { p.m_flSensitivity = fl; } ) );
	pForm->addRow( tr( "Flying speed (units a second)" ), spin( QStringLiteral( "preferencesMapSpeed" ), current.m_flForwardSpeed, 100, 10000, 100, 0, []( MapPreferences_t &p, double fl ) { p.m_flForwardSpeed = fl; } ) );
	pForm->addRow( tr( "Render distance" ), spin( QStringLiteral( "preferencesMapDistance" ), current.m_flRenderDistance, 1024, 131072, 1024, 0, []( MapPreferences_t &p, double fl ) { p.m_flRenderDistance = fl; } ) );
	auto *pTextures = new QComboBox;
	pTextures->setObjectName( QStringLiteral( "preferencesMapTextureSize" ) );
	for ( const int nSize : { 256, 512, 1024, 2048, 4096 } )
	{
		pTextures->addItem( QString::number( nSize ), nSize );
	}
	pTextures->setCurrentIndex( std::max( 0, pTextures->findData( current.m_nTextureSize ) ) );
	connect( pTextures, &QComboBox::currentIndexChanged, this, [ pTextures, change ] { change( [ & ]( MapPreferences_t &preferences ) { preferences.m_nTextureSize = pTextures->currentData().toInt(); } ); } );
	pForm->addRow( tr( "Largest texture size" ), pTextures );

	// The 2D views, and what a map opens with.
	auto check = [ & ]( const QString &szName, const QString &szText, bool bValue, auto pfnStore ) {
		auto *pCheck = new QCheckBox( szText );
		pCheck->setObjectName( szName );
		pCheck->setChecked( bValue );
		connect( pCheck, &QCheckBox::toggled, this, [ change, pfnStore ]( bool bOn ) { change( [ & ]( MapPreferences_t &preferences ) { pfnStore( preferences, bOn ); } ); } );
		return pCheck;
	};
	auto *pHighlight = new QSpinBox;
	pHighlight->setObjectName( QStringLiteral( "preferencesMapHighlight" ) );
	pHighlight->setRange( 2, 64 );
	pHighlight->setValue( current.m_nHighlightEvery );
	connect( pHighlight, &QSpinBox::valueChanged, this, [ change ]( int nEvery ) { change( [ & ]( MapPreferences_t &preferences ) { preferences.m_nHighlightEvery = nEvery; } ); } );
	pForm->addRow( tr( "Brighter grid line every" ), pHighlight );

	QVBoxLayout *pLayout = PageLayout( pPage );
	pLayout->addWidget( new QLabel( tr( "Game data (FGD files), read in this order:" ) ) );
	pLayout->addWidget( pFgds );
	pLayout->addLayout( pFgdButtons );
	pLayout->addWidget( Hint( tr( "Empty: the files the game's Hammer or Hammer++ configuration names. They are read again when a "
								  "map is opened." ) ) );
	pLayout->addSpacing( 12 );
	pLayout->addLayout( pForm );
	pLayout->addWidget( check( QStringLiteral( "preferencesMapHideSmallGrid" ), tr( "Hide grid lines that come too close together" ), current.m_bHideSmallGrid, []( MapPreferences_t &p, bool b ) { p.m_bHideSmallGrid = b; } ) );
	pLayout->addWidget( check( QStringLiteral( "preferencesMapVertices" ), tr( "Draw brushes' corners in the 2D views" ), current.m_bDrawVertices, []( MapPreferences_t &p, bool b ) { p.m_bDrawVertices = b; } ) );
	pLayout->addWidget( check( QStringLiteral( "preferencesMapModels2D" ), tr( "Draw models in the 2D views" ), current.m_bModels2D, []( MapPreferences_t &p, bool b ) { p.m_bModels2D = b; } ) );
	pLayout->addWidget( check( QStringLiteral( "preferencesMapGroupColors" ), tr( "Draw objects in their visgroup's colour" ), current.m_bGroupColors, []( MapPreferences_t &p, bool b ) { p.m_bGroupColors = b; } ) );
	pLayout->addWidget( check( QStringLiteral( "preferencesMapSkybox3D" ), tr( "Draw the 3D skybox" ), current.m_bSkybox3D, []( MapPreferences_t &p, bool b ) { p.m_bSkybox3D = b; } ) );
	pLayout->addWidget( Hint( tr( "Hammer++'s own settings are the defaults. The views' toolbar buttons change these for one map until "
								  "this page changes again." ) ) );
	pLayout->addStretch();
	return pPage;
}

//-----------------------------------------------------------------------------
// Purpose: For testing: the debug log.
//-----------------------------------------------------------------------------
QWidget *CPreferencesDialog::BuildAdvancedPage()
{
	auto *pPage = new QWidget;
	auto *pLog = new QCheckBox( tr( "Debug log" ) );
	pLog->setObjectName( QStringLiteral( "preferencesDebugLog" ) );
	pLog->setChecked( CPreferences::Instance().IsDebugLog() );
	connect( pLog, &QCheckBox::toggled, this, []( bool bOn ) { CPreferences::Instance().SetDebugLog( bOn ); } );
	auto *pShow = new QPushButton( tr( "Show Log File" ) );
	pShow->setObjectName( QStringLiteral( "preferencesShowLog" ) );
	connect( pShow, &QPushButton::clicked, this, [] { RevealInFileManager( DebugLogPath() ); } );
	QVBoxLayout *pLayout = PageLayout( pPage );
	pLayout->addWidget( pLog );
	pLayout->addWidget( Hint( tr( "Prints what goes on in the background (mounting the game, opening and saving files, "
								  "drawing previews, with how long each took) to the terminal, and writes it to %1, which "
								  "starts afresh at each run." )
			.arg( QDir::toNativeSeparators( DebugLogPath() ) ) ) );
	auto *pRow = new QHBoxLayout;
	pRow->addWidget( pShow );
	pRow->addStretch();
	pLayout->addLayout( pRow );
	pLayout->addStretch();
	return pPage;
}

//-----------------------------------------------------------------------------
// Purpose: Lets users supply stock tools, ++ replacements, or native wrapper executables.
//-----------------------------------------------------------------------------
QWidget *CPreferencesDialog::BuildSourceToolsPage()
{
	auto *pPage = new QWidget;
	auto *pLayout = PageLayout( pPage );
	pLayout->addWidget( Hint( tr( "Leave a path empty to discover tools beside the current game and in PATH. Tool folders named by gameinfo.txt are checked too. Use native executables for your operating system." ) ) );
	for ( auto nTool : { SourceTool_t::SOURCE_TOOL_STUDIOMDL, SourceTool_t::SOURCE_TOOL_BSPZIP } )
	{
		const auto szKey = SourceToolSetting( nTool );
		pLayout->addWidget( new QLabel( nTool == SourceTool_t::SOURCE_TOOL_STUDIOMDL ? tr( "studiomdl / StudioMDL++" ) : tr( "bspzip / BSPZIP++" ) ) );
		auto *pRow = new QHBoxLayout;
		auto *pPath = new QLineEdit( QSettings{}.value( szKey ).toString() );
		pPath->setObjectName( nTool == SourceTool_t::SOURCE_TOOL_STUDIOMDL ? QStringLiteral( "studiomdlPath" ) : QStringLiteral( "bspzipPath" ) );
		pPath->setPlaceholderText( tr( "Automatic discovery" ) );
		auto *pBrowse = new QPushButton( tr( "Browse…" ) );
		pRow->addWidget( pPath, 1 );
		pRow->addWidget( pBrowse );
		pLayout->addLayout( pRow );
		connect( pPath, &QLineEdit::textChanged, pPage, [ szKey ]( const QString &szPath ) { QSettings{}.setValue( szKey, szPath.trimmed() ); } );
		connect( pBrowse, &QPushButton::clicked, pPage, [ this, pPath ] {const auto szFile=AskOpenFile(this,tr("Select Source Tool"),pPath->text());if(!szFile.isEmpty())pPath->setText(szFile); } );
	}
	pLayout->addWidget( Hint( tr( "Model builds run in a temporary game directory. The compiler is supplied by you; this application does not include or download Valve or community compilers." ) ) );
	pLayout->addStretch();
	return pPage;
}
