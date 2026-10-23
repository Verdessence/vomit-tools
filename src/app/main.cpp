//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
//===========================================================================//
#include <QApplication>
#include <QCommandLineParser>
#include <QSettings>

#include "AboutDialog.h"
#include "MainWindow.h"

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int main( int nArgc, char *argv[] )
{
	QApplication app{ nArgc, argv };
	QApplication::setApplicationName( QStringLiteral( "vomit-tools" ) );
	QApplication::setOrganizationName( QStringLiteral( "vomit-tools" ) );
	QApplication::setApplicationVersion( CAboutDialog::VersionText() );

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

	CMainWindow window;
	window.show();

	const QStringList paths = parser.positionalArguments();
	if ( paths.isEmpty() )
	{
		if ( const QString szGame = QSettings{}.value( QStringLiteral( "lastGameInfo" ) ).toString(); !szGame.isEmpty() )
		{
			window.OpenGame( szGame );
		}
	}
	window.OpenPaths( paths );
	for ( const QString &szFile : parser.values( edit ) )
	{
		window.OpenSoundFile( szFile );
	}
	return QApplication::exec();
}
