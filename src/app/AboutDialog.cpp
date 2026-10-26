//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See AboutDialog.h.
//
//===========================================================================//
#include "AboutDialog.h"

#include <algorithm>

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSysInfo>
#include <QTabWidget>
#include <QVBoxLayout>

#include "BuildInfo.h"
#include "Graphics.h"

namespace
{

constexpr auto REPOSITORY = "https://github.com/Verdessence/vomit-tools";

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CompilerName()
{
#if defined( __clang__ )
	return QStringLiteral( "Clang %1.%2.%3" ).arg( __clang_major__ ).arg( __clang_minor__ ).arg( __clang_patchlevel__ );
#elif defined( _MSC_VER )
	return QStringLiteral( "MSVC %1" ).arg( _MSC_FULL_VER );
#elif defined( __GNUC__ )
	return QStringLiteral( "GCC %1.%2.%3" ).arg( __GNUC__ ).arg( __GNUC_MINOR__ ).arg( __GNUC_PATCHLEVEL__ );
#else
	return QStringLiteral( "unknown compiler" );
#endif
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CAboutDialog::CAboutDialog( QWidget *pParent )
	: QDialog( pParent )
{
	setWindowTitle( tr( "About vomit-tools" ) );

	auto *pName = new QLabel( QStringLiteral( "vomit-tools" ) );
	QFont big = pName->font();
	big.setPointSizeF( big.pointSizeF() * 1.8 );
	big.setBold( true );
	pName->setFont( big );

	auto *pVersion = new QLabel( tr( "Version %1" ).arg( VersionText() ) );
	pVersion->setObjectName( QStringLiteral( "version" ) );
	pVersion->setTextInteractionFlags( Qt::TextSelectableByMouse );

	auto *pAbout = new QLabel( tr(
		"<p>Soundscape, skybox and particle editors for Source engine games.</p>"
		"<p>Source code and releases: <a href=\"%1\">%1</a></p>"
		"<p>Free software under the <a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">GNU GPL, version 3 or later</a>. "
		"Built on <a href=\"https://www.qt.io\">Qt</a> (LGPLv3), "
		"<a href=\"https://github.com/craftablescience/sourcepp\">sourcepp</a> (MIT), "
		"<a href=\"https://miniaud.io\">miniaudio</a> (public domain / MIT-0) and "
		"<a href=\"https://github.com/nothings/stb\">stb</a> (public domain / MIT), with the compression "
		"and hashing libraries sourcepp uses. <b>Licenses</b> has every one with its terms.</p>"
		"<p>Not affiliated with Valve. Source and Counter-Strike are trademarks of Valve Corporation.</p>" )
			.arg( QLatin1StringView{ REPOSITORY } ) );
	pAbout->setWordWrap( true );
	pAbout->setOpenExternalLinks( true );
	pAbout->setTextInteractionFlags( Qt::TextBrowserInteraction );

	auto *pDetails = new QLabel( BuildDetails().section( u'\n', 1 ).replace( u'\n', QStringLiteral( " · " ) ) );
	pDetails->setObjectName( QStringLiteral( "details" ) );
	pDetails->setWordWrap( true );
	pDetails->setEnabled( false );
	pDetails->setTextInteractionFlags( Qt::TextSelectableByMouse );

	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Close );
	QPushButton *pCopy = pButtons->addButton( tr( "Copy Build Details" ), QDialogButtonBox::ActionRole );
	pCopy->setToolTip( tr( "Copies the version, Qt, system and compiler, for bug reports" ) );
	connect( pCopy, &QPushButton::clicked, this, [ pCopy ] {
		QApplication::clipboard()->setText( BuildDetails() );
		pCopy->setText( tr( "Copied" ) );
	} );
	QPushButton *pLicenses = pButtons->addButton( tr( "Licenses" ), QDialogButtonBox::ActionRole );
	pLicenses->setObjectName( QStringLiteral( "licenses" ) );
	pLicenses->setToolTip( tr( "The GNU GPL and the licenses of the libraries vomit-tools is built with" ) );
	connect( pLicenses, &QPushButton::clicked, this, [ this ] { ShowLicenses( this ); } );
	QPushButton *pAboutQt = pButtons->addButton( tr( "About Qt" ), QDialogButtonBox::ActionRole );
	connect( pAboutQt, &QPushButton::clicked, qApp, &QApplication::aboutQt );
	connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );

	// The icon beside the name and version.
	auto *pIcon = new QLabel;
	pIcon->setObjectName( QStringLiteral( "icon" ) );
	const qreal flRatio = devicePixelRatioF();
	QPixmap icon = QPixmap( QStringLiteral( ":/assets/vomit-tools.png" ) ).scaled( QSize( 96, 96 ) * flRatio, Qt::KeepAspectRatio, Qt::SmoothTransformation );
	icon.setDevicePixelRatio( flRatio );
	pIcon->setPixmap( icon );
	auto *pTitles = new QVBoxLayout;
	pTitles->addStretch();
	pTitles->addWidget( pName );
	pTitles->addWidget( pVersion );
	pTitles->addStretch();
	auto *pHeader = new QHBoxLayout;
	pHeader->setSpacing( 14 );
	pHeader->addWidget( pIcon );
	pHeader->addLayout( pTitles, 1 );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->setSpacing( 10 );
	pLayout->addLayout( pHeader );
	pLayout->addWidget( pAbout );
	pLayout->addWidget( pDetails );
	pLayout->addSpacing( 6 );
	pLayout->addWidget( pButtons );
	setFixedWidth( std::max( 560, sizeHint().width() ) );
	FitHeight();
}

//-----------------------------------------------------------------------------
// Purpose: Refits the height whenever the text's measurements can change.
//-----------------------------------------------------------------------------
bool CAboutDialog::event( QEvent *pEvent )
{
	switch ( pEvent->type() )
	{
		case QEvent::Show:
		case QEvent::FontChange:
		case QEvent::StyleChange:
		case QEvent::DevicePixelRatioChange:
			FitHeight();
			break;
		default:
			break;
	}
	return QDialog::event( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose: Makes the dialog exactly as tall as its wrapped text needs at its width. A window's own
//          size doesn't follow heightForWidth, so the layout's guess, made at another width or
//          before the window knew its screen and style, can leave the last lines cut off.
//-----------------------------------------------------------------------------
void CAboutDialog::FitHeight()
{
	layout()->activate();
	setFixedHeight( layout()->totalHeightForWidth( width() ) );
}

//-----------------------------------------------------------------------------
// Purpose: "0.3.0", or "0.3.0 (v0.3.0-2-g1a2b3c4-dirty)" when built from anything but the release
//          tag.
//-----------------------------------------------------------------------------
QString CAboutDialog::VersionText()
{
	const QString szVersion = QString::fromUtf8( buildinfo::version );
	const QString szCommit = QString::fromUtf8( buildinfo::commit );
	// A build of the release tag itself needs no commit: "v0.3.0" is 0.3.0.
	if ( szCommit.isEmpty() || szCommit == u'v' + szVersion )
	{
		return szVersion;
	}
	return QStringLiteral( "%1 (%2)" ).arg( szVersion, szCommit );
}

//-----------------------------------------------------------------------------
// Purpose: The version plus Qt, OS, compiler and what the previews draw with, for pasting into a
//          bug report.
//-----------------------------------------------------------------------------
QString CAboutDialog::BuildDetails()
{
	QString szQt = QStringLiteral( "Qt %1" ).arg( QString::fromLatin1( qVersion() ) );
	if ( szQt != QStringLiteral( "Qt " QT_VERSION_STR ) )
	{
		szQt += QStringLiteral( " (built with " QT_VERSION_STR ")" );
	}
	return QStringLiteral( "vomit-tools %1\n%2\n%3 %4\n%5\n%6" )
		.arg( VersionText(), szQt, QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(), CompilerName(), CGraphics::Instance().GetDescription() );
}

//-----------------------------------------------------------------------------
// Purpose: Built into the program, so every copy carries them.
//-----------------------------------------------------------------------------
QString CAboutDialog::LicenseText( const QString &szName )
{
	QFile file( QStringLiteral( ":/licenses/" ) + szName );
	return file.open( QIODevice::ReadOnly ) ? QString::fromUtf8( file.readAll() ) : QString{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CAboutDialog::ShowLicenses( QWidget *pParent )
{
	QDialog dialog( pParent );
	dialog.setObjectName( QStringLiteral( "licensesDialog" ) );
	dialog.setWindowTitle( tr( "Licenses" ) );
	dialog.resize( 720, 600 );
	auto *pTabs = new QTabWidget;
	const auto page = [ pTabs ]( const QString &szTitle, const QString &szName ) {
		auto *pText = new QPlainTextEdit( LicenseText( szName ) );
		pText->setObjectName( szName );
		pText->setReadOnly( true );
		pText->setFont( QFontDatabase::systemFont( QFontDatabase::FixedFont ) );
		pTabs->addTab( pText, szTitle );
	};
	page( tr( "vomit-tools (GNU GPL 3)" ), QStringLiteral( "LICENSE.txt" ) );
	page( tr( "Third-party libraries" ), QStringLiteral( "THIRD-PARTY-NOTICES.txt" ) );
	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Close );
	connect( pButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
	auto *pLayout = new QVBoxLayout( &dialog );
	pLayout->addWidget( pTabs );
	pLayout->addWidget( pButtons );
	dialog.exec();
}
