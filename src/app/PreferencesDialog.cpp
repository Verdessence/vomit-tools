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
#include <QColorDialog>
#include <QScrollArea>
#include <QTabWidget>
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
// Purpose: The map viewer, in tabs: where entities' game data comes from and how textures are
//          read, the 3D views' camera and drawing, the 2D views', and the colours. Every change
//          applies at once; the defaults are Hammer++'s.
//-----------------------------------------------------------------------------
QWidget *CPreferencesDialog::BuildMapsPage()
{
	auto *pPage = new QWidget;
	pPage->setObjectName( QStringLiteral( "preferencesMaps" ) );
	const MapPreferences_t &current = CPreferences::Instance().GetMapPreferences();
	auto change = []( auto pfnEdit ) {
		MapPreferences_t preferences = CPreferences::Instance().GetMapPreferences();
		pfnEdit( preferences );
		CPreferences::Instance().SetMapPreferences( preferences );
	};
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
	auto whole = [ & ]( const QString &szName, int nValue, int nLowest, int nHighest, const QString &szSuffix, auto pfnStore ) {
		auto *pSpin = new QSpinBox;
		pSpin->setObjectName( szName );
		pSpin->setRange( nLowest, nHighest );
		pSpin->setValue( nValue );
		pSpin->setSuffix( szSuffix );
		connect( pSpin, &QSpinBox::valueChanged, this, [ change, pfnStore ]( int nNew ) { change( [ & ]( MapPreferences_t &preferences ) { pfnStore( preferences, nNew ); } ); } );
		return pSpin;
	};
	auto check = [ & ]( const QString &szName, const QString &szText, bool bValue, auto pfnStore ) {
		auto *pCheck = new QCheckBox( szText );
		pCheck->setObjectName( szName );
		pCheck->setChecked( bValue );
		connect( pCheck, &QCheckBox::toggled, this, [ change, pfnStore ]( bool bOn ) { change( [ & ]( MapPreferences_t &preferences ) { pfnStore( preferences, bOn ); } ); } );
		return pCheck;
	};
	auto choice = [ & ]( const QString &szName, const std::vector<std::pair<QString, int>> &items, int nValue, auto pfnStore ) {
		auto *pCombo = new QComboBox;
		pCombo->setObjectName( szName );
		for ( const auto &[ szText, nData ] : items )
		{
			pCombo->addItem( szText, nData );
		}
		pCombo->setCurrentIndex( std::max( 0, pCombo->findData( nValue ) ) );
		connect( pCombo, &QComboBox::currentIndexChanged, this, [ pCombo, change, pfnStore ] { change( [ & ]( MapPreferences_t &preferences ) { pfnStore( preferences, pCombo->currentData().toInt() ); } ); } );
		return pCombo;
	};
	// A swatch that opens a colour picker.
	auto colour = [ & ]( const QString &szName, const QColor &value, bool bAlpha, auto pfnStore ) {
		auto *pButton = new QPushButton;
		pButton->setObjectName( szName );
		auto show = [ pButton ]( const QColor &color ) {
			QPixmap swatch( 32, 16 );
			swatch.fill( color );
			pButton->setIcon( QIcon( swatch ) );
			pButton->setIconSize( swatch.size() );
			pButton->setText( color.name( color.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb ) );
			pButton->setProperty( "color", color );
		};
		show( value );
		connect( pButton, &QPushButton::clicked, this, [ this, pButton, show, bAlpha, change, pfnStore ] {
			const QColor picked = QColorDialog::getColor( pButton->property( "color" ).value<QColor>(), this, pButton->toolTip(), bAlpha ? QColorDialog::ShowAlphaChannel : QColorDialog::ColorDialogOptions{} );
			if ( picked.isValid() )
			{
				show( picked );
				change( [ & ]( MapPreferences_t &preferences ) { pfnStore( preferences, picked ); } );
			}
		} );
		return pButton;
	};
	auto tab = []( QTabWidget *pTabs, const QString &szTitle ) {
		auto *pScroll = new QScrollArea;
		pScroll->setWidgetResizable( true );
		pScroll->setFrameShape( QFrame::NoFrame );
		auto *pInside = new QWidget;
		pScroll->setWidget( pInside );
		pTabs->addTab( pScroll, szTitle );
		auto *pLayout = new QVBoxLayout( pInside );
		pLayout->setContentsMargins( 8, 8, 8, 8 );
		return pLayout;
	};
	auto *pTabs = new QTabWidget;
	pTabs->setObjectName( QStringLiteral( "preferencesMapTabs" ) );

	// General: game data, a list of FGDs in the order they are read, and textures.
	{
		QVBoxLayout *pLayout = tab( pTabs, tr( "General" ) );
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
		pLayout->addWidget( new QLabel( tr( "Game data (FGD files), read in this order:" ) ) );
		pLayout->addWidget( pFgds );
		pLayout->addLayout( pFgdButtons );
		pLayout->addWidget( Hint( tr( "Empty: the files the game's Hammer or Hammer++ configuration names. They are read again when a "
									  "map is opened." ) ) );
		pLayout->addSpacing( 12 );
		auto *pForm = new QFormLayout;
		std::vector<std::pair<QString, int>> sizes;
		for ( const int nSize : { 256, 512, 1024, 2048, 4096 } )
		{
			sizes.emplace_back( QString::number( nSize ), nSize );
		}
		pForm->addRow( tr( "Largest texture size" ), choice( QStringLiteral( "preferencesMapTextureSize" ), sizes, current.m_nTextureSize, []( MapPreferences_t &p, int n ) { p.m_nTextureSize = n; } ) );
		pLayout->addLayout( pForm );
		pLayout->addStretch();
	}

	// The 3D views.
	{
		QVBoxLayout *pLayout = tab( pTabs, tr( "3D View" ) );
		auto *pForm = new QFormLayout;
		pForm->addRow( tr( "Field of view" ), spin( QStringLiteral( "preferencesMapFov" ), current.m_flFov, 30, 150, 5, 0, []( MapPreferences_t &p, double fl ) { p.m_flFov = fl; } ) );
		pForm->addRow( tr( "Mouse look speed" ), spin( QStringLiteral( "preferencesMapSensitivity" ), current.m_flSensitivity, 0.05, 5, 0.05, 2, []( MapPreferences_t &p, double fl ) { p.m_flSensitivity = fl; } ) );
		pForm->addRow( tr( "Flying speed (units a second)" ), spin( QStringLiteral( "preferencesMapSpeed" ), current.m_flForwardSpeed, 100, 10000, 100, 0, []( MapPreferences_t &p, double fl ) { p.m_flForwardSpeed = fl; } ) );
		pForm->addRow( tr( "Wheel step (units a notch)" ), spin( QStringLiteral( "preferencesMapWheelStep" ), current.m_flWheelStep, 1, 1024, 8, 0, []( MapPreferences_t &p, double fl ) { p.m_flWheelStep = fl; } ) );
		pForm->addRow( tr( "Right-drag (units a pixel)" ), spin( QStringLiteral( "preferencesMapPanScale" ), current.m_flPanScale, 0.1, 16, 0.25, 2, []( MapPreferences_t &p, double fl ) { p.m_flPanScale = fl; } ) );
		pForm->addRow( tr( "Render distance" ), spin( QStringLiteral( "preferencesMapDistance" ), current.m_flRenderDistance, 1024, 131072, 1024, 0, []( MapPreferences_t &p, double fl ) { p.m_flRenderDistance = fl; } ) );
		pForm->addRow( tr( "Keys 1 and 2 change it by" ), spin( QStringLiteral( "preferencesMapFarStep" ), current.m_flFarStep, 1.05, 4, 0.05, 2, []( MapPreferences_t &p, double fl ) { p.m_flFarStep = fl; } ) );
		pForm->addRow( tr( "Texture filtering" ), choice( QStringLiteral( "preferencesMapTextureFilter" ), { { tr( "Bilinear" ), static_cast<int>( MapTextureFilter_t::MAP_TEXTURE_FILTER_BILINEAR ) }, { tr( "Trilinear" ), static_cast<int>( MapTextureFilter_t::MAP_TEXTURE_FILTER_TRILINEAR ) }, { tr( "Anisotropic 2×" ), static_cast<int>( MapTextureFilter_t::MAP_TEXTURE_FILTER_ANISOTROPIC_2X ) }, { tr( "Anisotropic 4×" ), static_cast<int>( MapTextureFilter_t::MAP_TEXTURE_FILTER_ANISOTROPIC_4X ) }, { tr( "Anisotropic 8×" ), static_cast<int>( MapTextureFilter_t::MAP_TEXTURE_FILTER_ANISOTROPIC_8X ) }, { tr( "Anisotropic 16×" ), static_cast<int>( MapTextureFilter_t::MAP_TEXTURE_FILTER_ANISOTROPIC_16X ) } }, static_cast<int>( current.m_nTextureFilter ), []( MapPreferences_t &p, int n ) { p.m_nTextureFilter = static_cast<MapTextureFilter_t>( n ); } ) );
		pForm->addRow( tr( "Selection tint" ), spin( QStringLiteral( "preferencesMapSelectionTint" ), current.m_flSelectionTint, 0, 1, 0.05, 2, []( MapPreferences_t &p, double fl ) { p.m_flSelectionTint = fl; } ) );
		pForm->addRow( tr( "Broken solids' tint" ), spin( QStringLiteral( "preferencesMapInvalidTint" ), current.m_flInvalidTint, 0, 1, 0.05, 2, []( MapPreferences_t &p, double fl ) { p.m_flInvalidTint = fl; } ) );
		pForm->addRow( tr( "Instances' tint" ), spin( QStringLiteral( "preferencesMapInstanceTint" ), current.m_flInstanceTint, 0, 1, 0.05, 2, []( MapPreferences_t &p, double fl ) { p.m_flInstanceTint = fl; } ) );
		pForm->addRow( tr( "Selected models show" ), choice( QStringLiteral( "preferencesMapSelectedModels" ), { { tr( "Their edges if props (prop_…), else their box" ), static_cast<int>( MapSelectedModels_t::MAP_SELECTED_MODELS_PROPS ) }, { tr( "Their edges" ), static_cast<int>( MapSelectedModels_t::MAP_SELECTED_MODELS_EDGES ) }, { tr( "Their box" ), static_cast<int>( MapSelectedModels_t::MAP_SELECTED_MODELS_BOX ) } }, static_cast<int>( current.m_nSelectedModels ), []( MapPreferences_t &p, int n ) { p.m_nSelectedModels = static_cast<MapSelectedModels_t>( n ); } ) );
		pForm->addRow( tr( "Lightmap grid's darker squares" ), spin( QStringLiteral( "preferencesMapLightmapGridShade" ), current.m_flLightmapGridShade, 0, 1, 0.05, 2, []( MapPreferences_t &p, double fl ) { p.m_flLightmapGridShade = fl; } ) );
		pLayout->addLayout( pForm );
		pLayout->addWidget( check( QStringLiteral( "preferencesMapSkybox3D" ), tr( "Draw the 3D skybox" ), current.m_bSkybox3D, []( MapPreferences_t &p, bool b ) { p.m_bSkybox3D = b; } ) );
		pLayout->addWidget( check( QStringLiteral( "preferencesMapSprites" ), tr( "Draw sprites (else entities' boxes)" ), current.m_bSprites, []( MapPreferences_t &p, bool b ) { p.m_bSprites = b; } ) );
		pLayout->addWidget( check( QStringLiteral( "preferencesMapWireframeModels" ), tr( "Draw models' edges in the wireframe view" ), current.m_bWireframeModels, []( MapPreferences_t &p, bool b ) { p.m_bWireframeModels = b; } ) );
		pLayout->addSpacing( 12 );
		pLayout->addWidget( check( QStringLiteral( "preferencesMapArrowsNudge" ), tr( "Arrow keys move the selection (in the 2D views)" ), current.m_bArrowsNudge, []( MapPreferences_t &p, bool b ) { p.m_bArrowsNudge = b; } ) );
		pLayout->addWidget( check( QStringLiteral( "preferencesMapTextureLock" ), tr( "Texture lock on when a map opens" ), current.m_bTextureLock, []( MapPreferences_t &p, bool b ) { p.m_bTextureLock = b; } ) );
		pLayout->addStretch();
	}

	// The 2D views.
	{
		QVBoxLayout *pLayout = tab( pTabs, tr( "2D Views" ) );
		const QString szPixels = tr( " px" );
		auto *pForm = new QFormLayout;
		pForm->addRow( tr( "Brighter grid line every" ), whole( QStringLiteral( "preferencesMapHighlight" ), current.m_nHighlightEvery, 2, 64, QString{}, []( MapPreferences_t &p, int n ) { p.m_nHighlightEvery = n; } ) );
		pForm->addRow( tr( "Grid lines closer than this are hidden" ), spin( QStringLiteral( "preferencesMapHideGridBelow" ), current.m_flHideGridBelow, 1, 32, 1, 1, []( MapPreferences_t &p, double fl ) { p.m_flHideGridBelow = fl; } ) );
		pForm->addRow( tr( "Corner dots' size" ), whole( QStringLiteral( "preferencesMapVertexSize" ), current.m_nVertexSize, 1, 8, szPixels, []( MapPreferences_t &p, int n ) { p.m_nVertexSize = n; } ) );
		pForm->addRow( tr( "Corners on brushes from" ), whole( QStringLiteral( "preferencesMapVerticesFrom" ), current.m_nVerticesFromPixels, 0, 256, szPixels, []( MapPreferences_t &p, int n ) { p.m_nVerticesFromPixels = n; } ) );
		pForm->addRow( tr( "Handles' size" ), whole( QStringLiteral( "preferencesMapHandleSize" ), current.m_nHandleSize, 3, 16, szPixels, []( MapPreferences_t &p, int n ) { p.m_nHandleSize = n; } ) );
		pForm->addRow( tr( "Centre handles on objects from" ), whole( QStringLiteral( "preferencesMapCenterHandlesFrom" ), current.m_nCenterHandlesFromPixels, 0, 256, szPixels, []( MapPreferences_t &p, int n ) { p.m_nCenterHandlesFromPixels = n; } ) );
		pForm->addRow( tr( "Models drawn from" ), whole( QStringLiteral( "preferencesMapModelsFrom" ), current.m_nModelsFromPixels, 0, 512, szPixels, []( MapPreferences_t &p, int n ) { p.m_nModelsFromPixels = n; } ) );
		pForm->addRow( tr( "Names shown once 16 units are" ), whole( QStringLiteral( "preferencesMapNamesFrom" ), current.m_nNamesFromPixels, 1, 512, szPixels, []( MapPreferences_t &p, int n ) { p.m_nNamesFromPixels = n; } ) );
		pForm->addRow( tr( "The camera's size" ), whole( QStringLiteral( "preferencesMapCameraPixels" ), current.m_nCameraPixels, 8, 256, szPixels, []( MapPreferences_t &p, int n ) { p.m_nCameraPixels = n; } ) );
		pForm->addRow( tr( "Group colours" ), choice( QStringLiteral( "preferencesMapGroupColorsFrom" ), { { tr( "A colour for each group" ), static_cast<int>( MapGroupColors_t::MAP_GROUP_COLORS_RANDOM ) }, { tr( "The colour saved in the map" ), static_cast<int>( MapGroupColors_t::MAP_GROUP_COLORS_SAVED ) }, { tr( "None" ), static_cast<int>( MapGroupColors_t::MAP_GROUP_COLORS_NONE ) } }, static_cast<int>( current.m_nGroupColors ), []( MapPreferences_t &p, int n ) { p.m_nGroupColors = static_cast<MapGroupColors_t>( n ); } ) );
		pForm->addRow( tr( "Wheel zoom (times a notch)" ), spin( QStringLiteral( "preferencesMapWheelZoom" ), current.m_flWheelZoom, 1.05, 4, 0.05, 2, []( MapPreferences_t &p, double fl ) { p.m_flWheelZoom = fl; } ) );
		pForm->addRow( tr( "Keypad + and − zoom (times)" ), spin( QStringLiteral( "preferencesMapKeyZoom" ), current.m_flKeyZoom, 1.05, 8, 0.25, 2, []( MapPreferences_t &p, double fl ) { p.m_flKeyZoom = fl; } ) );
		pForm->addRow( tr( "Arrow keys scroll" ), whole( QStringLiteral( "preferencesMapScrollStep" ), current.m_nScrollStep, 1, 1024, szPixels, []( MapPreferences_t &p, int n ) { p.m_nScrollStep = n; } ) );
		pForm->addRow( tr( "Furthest zoom (pixels a unit)" ), spin( QStringLiteral( "preferencesMapZoomSmallest" ), current.m_flZoomSmallest, 0.001, 1, 0.001, 3, []( MapPreferences_t &p, double fl ) { p.m_flZoomSmallest = fl; } ) );
		pForm->addRow( tr( "Closest zoom (pixels a unit)" ), spin( QStringLiteral( "preferencesMapZoomLargest" ), current.m_flZoomLargest, 1, 1024, 8, 0, []( MapPreferences_t &p, double fl ) { p.m_flZoomLargest = fl; } ) );
		pLayout->addLayout( pForm );
		pLayout->addWidget( check( QStringLiteral( "preferencesMapHideSmallGrid" ), tr( "Hide grid lines that come too close together" ), current.m_bHideSmallGrid, []( MapPreferences_t &p, bool b ) { p.m_bHideSmallGrid = b; } ) );
		pLayout->addWidget( check( QStringLiteral( "preferencesMapVertices" ), tr( "Draw brushes' corners" ), current.m_bDrawVertices, []( MapPreferences_t &p, bool b ) { p.m_bDrawVertices = b; } ) );
		pLayout->addWidget( check( QStringLiteral( "preferencesMapCenterHandles" ), tr( "Draw centre handles" ), current.m_bCenterHandles, []( MapPreferences_t &p, bool b ) { p.m_bCenterHandles = b; } ) );
		pLayout->addWidget( check( QStringLiteral( "preferencesMapModels2D" ), tr( "Draw models" ), current.m_bModels2D, []( MapPreferences_t &p, bool b ) { p.m_bModels2D = b; } ) );
		pLayout->addWidget( check( QStringLiteral( "preferencesMapTexturedModels2D" ), tr( "Draw models textured (else their edges)" ), current.m_bTexturedModels2D, []( MapPreferences_t &p, bool b ) { p.m_bTexturedModels2D = b; } ) );
		pLayout->addWidget( check( QStringLiteral( "preferencesMapGroupColors" ), tr( "Draw objects in their visgroup's and group's colour" ), current.m_bGroupColors, []( MapPreferences_t &p, bool b ) { p.m_bGroupColors = b; } ) );
		pLayout->addWidget( check( QStringLiteral( "preferencesMapInstanceColor2D" ), tr( "Draw instances' contents in the instance colour (Colours), not the func_instance's" ), current.m_bInstanceColor2D, []( MapPreferences_t &p, bool b ) { p.m_bInstanceColor2D = b; } ) );
		pLayout->addStretch();
	}

	// Colours.
	{
		QVBoxLayout *pLayout = tab( pTabs, tr( "Colours" ) );
		auto *pForm = new QFormLayout;
		struct Colour_t
		{
			const char *m_pszName;
			QString m_szLabel;
			QColor MapColors_t::*m_pMember;
			bool m_bAlpha;
		};
		const std::vector<Colour_t> colours = {
			{ "Background", tr( "2D background" ), &MapColors_t::m_Background, false },
			{ "Grid", tr( "Grid" ), &MapColors_t::m_Grid, false },
			{ "GridHighlight", tr( "Brighter grid lines" ), &MapColors_t::m_GridHighlight, false },
			{ "Grid1024", tr( "Grid every 1024 units" ), &MapColors_t::m_Grid1024, false },
			{ "Axis", tr( "Axes" ), &MapColors_t::m_Axis, false },
			{ "Brush", tr( "World brushes" ), &MapColors_t::m_Brush, false },
			{ "Entity", tr( "Entities without a colour of their own" ), &MapColors_t::m_Entity, false },
			{ "Invalid", tr( "Broken solids" ), &MapColors_t::m_Invalid, false },
			{ "Vertex", tr( "Brushes' corners" ), &MapColors_t::m_Vertex, false },
			{ "CenterHandle", tr( "Centre handles" ), &MapColors_t::m_CenterHandle, false },
			{ "Selection", tr( "2D selection" ), &MapColors_t::m_Selection, false },
			{ "SelectionFill", tr( "2D selection's fill" ), &MapColors_t::m_SelectionFill, true },
			{ "ToolSelection", tr( "Selection box and sizes" ), &MapColors_t::m_ToolSelection, false },
			{ "ToolHandle", tr( "Selection box's handles" ), &MapColors_t::m_ToolHandle, false },
			{ "ToolBlock", tr( "New block's box and new entity's marker" ), &MapColors_t::m_ToolBlock, false },
			{ "Camera", tr( "The 3D camera in 2D" ), &MapColors_t::m_Camera, false },
			{ "Instance2D", tr( "Instances' contents in 2D" ), &MapColors_t::m_Instance2D, false },
			{ "Background3D", tr( "3D background" ), &MapColors_t::m_Background3D, false },
			{ "Selection3D", tr( "3D selection's tint" ), &MapColors_t::m_Selection3D, false },
			{ "SelectionEdge3D", tr( "3D selection's edges" ), &MapColors_t::m_SelectionEdge3D, false },
			{ "Instance", tr( "Instances' tint in 3D" ), &MapColors_t::m_Instance, false },
		};
		for ( const Colour_t &entry : colours )
		{
			QPushButton *pButton = colour( QStringLiteral( "preferencesMapColor%1" ).arg( QLatin1String( entry.m_pszName ) ), current.m_Colors.*entry.m_pMember, entry.m_bAlpha, [ pMember = entry.m_pMember ]( MapPreferences_t &p, const QColor &color ) { p.m_Colors.*pMember = color; } );
			pButton->setToolTip( entry.m_szLabel );
			pForm->addRow( entry.m_szLabel, pButton );
		}
		pLayout->addLayout( pForm );
		pLayout->addStretch();
	}

	// Back to Hammer++'s settings, keeping the game data.
	auto *pReset = new QPushButton( tr( "Restore Hammer++'s Settings" ) );
	pReset->setObjectName( QStringLiteral( "preferencesMapReset" ) );
	connect( pReset, &QPushButton::clicked, this, [ this ] {
		MapPreferences_t preferences;
		preferences.m_Fgds = CPreferences::Instance().GetMapPreferences().m_Fgds;
		CPreferences::Instance().SetMapPreferences( preferences );
		const int nTab = m_pMapsPage ? m_pMapsPage->findChild<QTabWidget *>()->currentIndex() : 0;
		QWidget *pOld = m_pMapsPage;
		m_pMapsPage = BuildMapsPage();
		const int nIndex = m_pStack->indexOf( pOld );
		m_pStack->insertWidget( nIndex, m_pMapsPage );
		m_pStack->setCurrentWidget( m_pMapsPage );
		m_pStack->removeWidget( pOld );
		pOld->deleteLater();
		m_pMapsPage->findChild<QTabWidget *>()->setCurrentIndex( nTab );
	} );
	auto *pLayout = new QVBoxLayout( pPage );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->addWidget( pTabs, 1 );
	auto *pRow = new QHBoxLayout;
	pRow->addWidget( Hint( tr( "Hammer++'s own settings are the defaults. The views' toolbar buttons change some of these for one map "
							   "until this page changes again." ) ),
		1 );
	pRow->addWidget( pReset );
	pLayout->addLayout( pRow );
	m_pMapsPage = pPage;
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
