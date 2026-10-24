//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See PreferencesDialog.h.
//
//===========================================================================//
#include "PreferencesDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QRadioButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "Graphics.h"
#include "InterfaceMode.h"
#include "Preferences.h"

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
