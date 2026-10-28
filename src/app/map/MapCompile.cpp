//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapCompile.h.
//
//===========================================================================//
#include "MapCompile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CCompileRunner::CCompileRunner( QObject *pParent )
	: QObject( pParent )
{
	m_pTool = new CSourceToolRunner( this );
	connect( m_pTool, &CSourceToolRunner::Output, this, &CCompileRunner::Append );
	connect( m_pTool, &CSourceToolRunner::Finished, this, &CCompileRunner::CommandDone );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CCompileRunner::Start( std::vector<xv::map::ResolvedCommand_t> commands, const QString &szDirectory, const QString &szGameDirectory )
{
	if ( m_bRunning )
	{
		return false;
	}
	m_Commands = std::move( commands );
	m_nCurrent = 0;
	m_szDirectory = szDirectory;
	m_szGameDirectory = szGameDirectory;
	m_szLog.clear();
	m_bRunning = true;
	m_bCancelled = false;
	// From the event loop, so the caller's connections are in place for the first lines.
	QTimer::singleShot( 0, this, &CCompileRunner::RunNext );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CCompileRunner::Cancel()
{
	if ( !m_bRunning )
	{
		return;
	}
	m_bCancelled = true;
	if ( m_pTool->IsRunning() )
	{
		m_pTool->Cancel();
	}
	else
	{
		End( false );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The specials are done here and now; a program is started and waited for, unless the
//          sequence doesn't wait for it (the game), which is started and left to itself.
//-----------------------------------------------------------------------------
void CCompileRunner::RunNext()
{
	if ( m_bCancelled )
	{
		End( false );
		return;
	}
	if ( m_nCurrent >= m_Commands.size() )
	{
		End( true );
		return;
	}
	const xv::map::ResolvedCommand_t &command = m_Commands[ m_nCurrent ];
	emit CommandStarted( m_nCurrent, QString::fromStdString( command.m_szShown ) );
	Append( QStringLiteral( "> %1\n" ).arg( QString::fromStdString( command.m_szShown ) ) );
	auto path = [ & ]( const std::string &sz ) { return QDir( m_szDirectory ).absoluteFilePath( QString::fromStdString( sz ) ); };
	switch ( command.m_nSpecial )
	{
		case xv::map::COMPILE_SPECIAL_CHANGE_DIR:
			m_szDirectory = path( command.m_szFrom );
			CommandDone( QDir( m_szDirectory ).exists() );
			return;
		case xv::map::COMPILE_SPECIAL_COPY:
		{
			const QString szTo = path( command.m_szTo );
			QDir().mkpath( QFileInfo( szTo ).absolutePath() );
			if ( QFile::exists( szTo ) )
			{
				QFile::remove( szTo );
			}
			const bool bOk = QFile::copy( path( command.m_szFrom ), szTo );
			if ( !bOk )
			{
				// Hammer's own words for the usual case: the compiler left no map behind.
				Append( tr( "The system cannot find the file specified: %1\n" ).arg( path( command.m_szFrom ) ) );
			}
			CommandDone( bOk );
			return;
		}
		case xv::map::COMPILE_SPECIAL_DELETE:
			CommandDone( !QFile::exists( path( command.m_szFrom ) ) || QFile::remove( path( command.m_szFrom ) ) );
			return;
		case xv::map::COMPILE_SPECIAL_RENAME:
			CommandDone( QFile::rename( path( command.m_szFrom ), path( command.m_szTo ) ) );
			return;
		default:
			break;
	}
	QStringList arguments;
	for ( const std::string &sz : command.m_Arguments )
	{
		arguments << QString::fromStdString( sz );
	}
	if ( !command.m_bWait )
	{
		// The game: its own process, its folder as the working directory (its script wants
		// that), and nothing waited for.
		const QString szProgram = QString::fromStdString( command.m_szProgram );
		if ( !QFileInfo( szProgram ).isFile() )
		{
			Append( tr( "%1 isn't there: the game wasn't started. Choose it in Preferences > Source Tools.\n" ).arg( szProgram ) );
			CommandDone( false );
			return;
		}
		StartGame( szProgram, arguments );
		return;
	}
	m_pTool->Start( QString::fromStdString( command.m_szProgram ), arguments, m_szDirectory, m_szGameDirectory );
}

//-----------------------------------------------------------------------------
// Purpose: The game is a process of its own that outlives the sequence; its lines go into the
//          log as they come (prefixed), so a game that won't start says why there.
//-----------------------------------------------------------------------------
void CCompileRunner::StartGame( const QString &szProgram, const QStringList &arguments )
{
	auto *pGame = new QProcess( this );
	pGame->setProgram( szProgram );
	pGame->setArguments( arguments );
	pGame->setWorkingDirectory( QFileInfo( szProgram ).absolutePath() );
	pGame->setProcessEnvironment( GameEnvironment() );
	pGame->setProcessChannelMode( QProcess::MergedChannels );
	const QString szName = QFileInfo( szProgram ).fileName();
	connect( pGame, &QProcess::readyReadStandardOutput, this, [ this, pGame, szName ] {
		for ( const QByteArray &line : pGame->readAllStandardOutput().split( '\n' ) )
		{
			if ( !line.trimmed().isEmpty() )
			{
				Append( QStringLiteral( "%1: %2\n" ).arg( szName, QString::fromUtf8( line ) ) );
			}
		}
	} );
	connect( pGame, &QProcess::finished, this, [ this, pGame, szName ]( int nCode, QProcess::ExitStatus nStatus ) {
		Append( nStatus == QProcess::NormalExit ? tr( "%1 ended with code %2.\n" ).arg( szName ).arg( nCode ) : tr( "%1 crashed.\n" ).arg( szName ) );
		pGame->deleteLater();
	} );
	connect( pGame, &QProcess::errorOccurred, this, [ this, pGame, szProgram ]( QProcess::ProcessError nError ) {
		if ( nError == QProcess::FailedToStart )
		{
			Append( tr( "%1 could not be started: %2\n" ).arg( szProgram, pGame->errorString() ) );
			pGame->deleteLater();
		}
	} );
	pGame->start();
	const bool bStarted = pGame->waitForStarted( 5000 );
	if ( bStarted )
	{
		Append( tr( "%1 started (process %2); what it prints follows.\n" ).arg( szName ).arg( pGame->processId() ) );
	}
	CommandDone( bStarted );
}

//-----------------------------------------------------------------------------
// Purpose: AppRun sets APPDIR and puts the image's folders first in LD_LIBRARY_PATH, PATH and
//          the Qt variables; the game's own script prepends its libraries, but what it lacks
//          would still come from the image.
//-----------------------------------------------------------------------------
QProcessEnvironment CCompileRunner::GameEnvironment()
{
	QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	const QString szAppDir = environment.value( QStringLiteral( "APPDIR" ) );
	if ( szAppDir.isEmpty() )
	{
		return environment;
	}
	for ( const QString &szKey : environment.keys() )
	{
		const QString szValue = environment.value( szKey );
		if ( szKey == QStringLiteral( "APPDIR" ) || szKey == QStringLiteral( "APPIMAGE" ) || szKey == QStringLiteral( "ARGV0" ) || szKey == QStringLiteral( "OWD" ) )
		{
			environment.remove( szKey );
		}
		else if ( szKey == QStringLiteral( "PATH" ) || szKey == QStringLiteral( "LD_LIBRARY_PATH" ) || szKey.startsWith( QStringLiteral( "QT_" ) ) || szKey.startsWith( QStringLiteral( "QML" ) ) || szKey.startsWith( QStringLiteral( "GTK_" ) ) || szKey.startsWith( QStringLiteral( "GSETTINGS" ) ) || szKey.startsWith( QStringLiteral( "XDG_DATA_DIRS" ) ) || szKey.startsWith( QStringLiteral( "PERLLIB" ) ) || szKey.startsWith( QStringLiteral( "PYTHONPATH" ) ) )
		{
			// The image's entries out of a list; a variable that was only the image's goes.
			QStringList kept;
			for ( const QString &szEntry : szValue.split( QLatin1Char( ':' ) ) )
			{
				if ( !szEntry.startsWith( szAppDir ) )
				{
					kept << szEntry;
				}
			}
			if ( kept.isEmpty() || ( kept.size() == 1 && kept.front().isEmpty() ) )
			{
				environment.remove( szKey );
			}
			else if ( kept.size() != szValue.split( QLatin1Char( ':' ) ).size() )
			{
				environment.insert( szKey, kept.join( QLatin1Char( ':' ) ) );
			}
		}
	}
	return environment;
}

//-----------------------------------------------------------------------------
// Purpose: After a command: the file it had to leave, the leak that stops everything, then the
//          next one.
//-----------------------------------------------------------------------------
void CCompileRunner::CommandDone( bool bSuccess )
{
	if ( !m_bRunning )
	{
		return;
	}
	const xv::map::ResolvedCommand_t &command = m_Commands[ m_nCurrent ];
	if ( bSuccess && !command.m_szEnsure.empty() && !QFile::exists( QDir( m_szDirectory ).absoluteFilePath( QString::fromStdString( command.m_szEnsure ) ) ) )
	{
		Append( tr( "%1 wasn't made.\n" ).arg( QString::fromStdString( command.m_szEnsure ) ) );
		bSuccess = false;
	}
	// VBSP++ exits with an error on a leak, stock vbsp doesn't: the log says either way.
	if ( command.m_nSpecial == xv::map::COMPILE_SPECIAL_RUN && QFileInfo( QString::fromStdString( command.m_szProgram ) ).fileName().contains( QStringLiteral( "vbsp" ), Qt::CaseInsensitive ) && xv::map::ParseCompileOutput( m_szLog.toStdString() ).m_bLeaked )
	{
		if ( m_bStopOnLeak || !bSuccess )
		{
			Append( tr( "The map leaked: the rest of the sequence isn't run.\n" ) );
			End( false );
			return;
		}
	}
	if ( !bSuccess || m_bCancelled )
	{
		End( false );
		return;
	}
	m_nCurrent++;
	RunNext();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CCompileRunner::Append( const QString &szGiven )
{
	// The ++ compilers colour their output for a terminal.
	const QString szText = QString::fromStdString( xv::map::StripAnsi( szGiven.toStdString() ) );
	if ( szText.isEmpty() )
	{
		return;
	}
	m_szLog += szText;
	if ( m_szLog.size() > 8 * 1024 * 1024 )
	{
		m_szLog.remove( 0, m_szLog.size() - 8 * 1024 * 1024 );
	}
	emit Output( szText );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CCompileRunner::End( bool bSuccess )
{
	if ( !m_bRunning )
	{
		return;
	}
	m_bRunning = false;
	xv::map::CompileReport_t report = xv::map::ParseCompileOutput( m_szLog.toStdString() );
	Append( bSuccess ? tr( "Done.\n" ) : m_bCancelled ? tr( "Cancelled.\n" )
													  : tr( "Stopped.\n" ) );
	emit Finished( bSuccess, report );
}
