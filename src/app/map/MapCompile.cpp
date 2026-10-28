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
		const bool bStarted = QProcess::startDetached( szProgram, arguments, QFileInfo( szProgram ).absolutePath() );
		if ( !bStarted )
		{
			Append( tr( "%1 could not be started.\n" ).arg( szProgram ) );
		}
		CommandDone( bStarted );
		return;
	}
	m_pTool->Start( QString::fromStdString( command.m_szProgram ), arguments, m_szDirectory, m_szGameDirectory );
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
