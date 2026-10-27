//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
//===========================================================================//
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>

#include <QIcon>

#include "AboutDialog.h"
#include "DebugLog.h"
#include "Language.h"
#include "Graphics.h"
#include "MainWindow.h"
#include "PopupBlur.h"
#include "Preferences.h"
#include "PremiumDialog.h"
#include "SingleInstance.h"
#include "Splash.h"
#include "util/Memory.h"

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int main( int nArgc, char *argv[] )
{
	xv::TuneAllocator();
	QApplication app{ nArgc, argv };
	QApplication::setApplicationName( QStringLiteral( "vomit-tools" ) );
	QApplication::setOrganizationName( QStringLiteral( "vomit-tools" ) );
	QApplication::setApplicationVersion( CAboutDialog::VersionText() );
	QApplication::setWindowIcon( QIcon( QStringLiteral( ":/assets/vomit-tools.png" ) ) );
	// Before any popup is made: Kvantum's menu blur crashes the app with KWindowSystem before 6.32.
	InstallPopupBlurWorkaround( app );

	QCommandLineParser parser;
	parser.setApplicationDescription( QStringLiteral( "Soundscape, skybox and particle editors for Source engine games" ) );
	parser.addHelpOption();
	parser.addVersionOption();
	parser.addPositionalArgument( QStringLiteral( "paths" ), QStringLiteral( "Files, folders or a game's gameinfo.txt to open" ), QStringLiteral( "[paths...]" ) );
	const QCommandLineOption edit{ { QStringLiteral( "e" ), QStringLiteral( "edit" ) },
		QStringLiteral( "Open a soundscape or soundscript from the game, e.g. scripts/soundscapes_dust2.txt" ),
		QStringLiteral( "file" ) };
	parser.addOption( edit );
	parser.process( app );

	if ( CPreferences::Instance().IsDebugLog() )
	{
		SetDebugLogging( true );
		DebugLog() << "vomit-tools" << CAboutDialog::VersionText() << "starting; log at" << DebugLogPath();
	}

	// Files go to the window already open, when there is one.
	CSingleInstance instance;
	const bool bSingle = CPreferences::Instance().IsSingleWindow();
	QStringList absolute;
	for ( const QString &szPath : parser.positionalArguments() )
	{
		absolute << QFileInfo{ szPath }.absoluteFilePath();
	}
	if ( bSingle && parser.values( edit ).isEmpty() && instance.SendToRunning( absolute ) )
	{
		return 0;
	}

	// The splash, a step for each thing loaded before the window is ready.
	const QStringList paths = parser.positionalArguments();
	const QString szLastGame = paths.isEmpty() ? QSettings{}.value( QStringLiteral( "lastGameInfo" ) ).toString() : QString{};
	const QStringList edits = parser.values( edit );
	// What was open last time comes back unless files were asked for.
	const QStringList session = paths.isEmpty() && edits.isEmpty() ? CMainWindow::GetLastSession() : QStringList{};
	CSplash splash( 3 + ( szLastGame.isEmpty() ? 0 : 1 ) + static_cast<int>( session.size() + paths.size() + edits.size() ) );
	splash.show();
	splash.SetStep( QStringLiteral( "graphics" ) );
	// What the previews draw with, settled before any of them is made.
	static_cast<void>( CGraphics::Instance() );
	DebugLog() << "drawing with" << GraphicsApiName( CGraphics::Instance().GetActive() ) << CGraphics::Instance().GetDescription();
	splash.SetStep( QStringLiteral( "names and parameters" ) );
	static_cast<void>( ParticleNames() );
	static_cast<void>( MaterialParameters() );
	splash.SetStep( QStringLiteral( "the window" ) );
	CMainWindow window;
	if ( bSingle )
	{
		instance.Listen( [ &window ]( const QStringList &received ) {
			window.OpenPaths( received );
			window.setWindowState( ( window.windowState() & ~Qt::WindowMinimized ) | Qt::WindowActive );
			window.raise();
			window.activateWindow();
		} );
	}

	if ( !szLastGame.isEmpty() )
	{
		splash.SetStep( QFileInfo{ szLastGame }.dir().dirName() );
		window.OpenGame( szLastGame );
	}
	for ( const QString &szEntry : session )
	{
		splash.SetStep( CMainWindow::SessionEntryName( szEntry ) );
		window.RestoreSessionEntry( szEntry );
	}
	window.FinishRestoringSession();
	for ( const QString &szPath : paths )
	{
		splash.SetStep( QFileInfo{ szPath }.fileName() );
		window.OpenPaths( { szPath } );
	}
	for ( const QString &szFile : edits )
	{
		splash.SetStep( QFileInfo{ szFile }.fileName() );
		window.OpenSoundFile( szFile );
	}
	window.show();
	splash.finish( &window );
	// The first start says hello.
	if ( !CPremiumDialog::HasBeenShown() )
	{
		QTimer::singleShot( 600, &window, [ &window ] { CPremiumDialog{ &window }.exec(); } );
	}
	return QApplication::exec();
}
