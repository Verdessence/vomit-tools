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
#include <QLabel>
#include <QPushButton>
#include <QSysInfo>
#include <QVBoxLayout>

#include "BuildInfo.h"

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
		"<a href=\"https://github.com/nothings/stb\">stb</a> (public domain / MIT).</p>"
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
	QPushButton *pAboutQt = pButtons->addButton( tr( "About Qt" ), QDialogButtonBox::ActionRole );
	connect( pAboutQt, &QPushButton::clicked, qApp, &QApplication::aboutQt );
	connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->setSpacing( 10 );
	pLayout->addWidget( pName );
	pLayout->addWidget( pVersion );
	pLayout->addWidget( pAbout );
	pLayout->addWidget( pDetails );
	pLayout->addSpacing( 6 );
	pLayout->addWidget( pButtons );
	setFixedWidth( std::max( 560, sizeHint().width() ) );
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
// Purpose: The version plus Qt, OS and compiler, for pasting into a bug report.
//-----------------------------------------------------------------------------
QString CAboutDialog::BuildDetails()
{
	QString szQt = QStringLiteral( "Qt %1" ).arg( QString::fromLatin1( qVersion() ) );
	if ( szQt != QStringLiteral( "Qt " QT_VERSION_STR ) )
	{
		szQt += QStringLiteral( " (built with " QT_VERSION_STR ")" );
	}
	return QStringLiteral( "vomit-tools %1\n%2\n%3 %4\n%5" )
		.arg( VersionText(), szQt, QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(), CompilerName() );
}
