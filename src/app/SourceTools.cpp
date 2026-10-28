//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: See SourceTools.h. Tool arguments follow the locally supplied VDC tool documentation.
//===========================================================================//
#include "SourceTools.h"

#include "map/GameConfig.h"
#include <algorithm>
#include "QtPaths.h"
#include "kv1/Kv1.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>
namespace
{
//-----------------------------------------------------------------------------
// Purpose: Expands the game's own bin hints without assuming gameinfo names a compiler directly.
//-----------------------------------------------------------------------------
QString Expand( QString szPath, const xv::vfs::CGameInfo &game )
{
	szPath.replace( QStringLiteral( "|gameinfo_path|" ), QDir::fromNativeSeparators( ToQString( game.m_GameDir ) ) + u'/' );
	szPath.replace( QStringLiteral( "|all_source_engine_paths|" ), QDir::fromNativeSeparators( ToQString( game.m_BaseDir ) ) + u'/' );
	if ( QDir::isRelativePath( szPath ) )
		szPath = QDir( ToQString( game.m_BaseDir ) ).filePath( szPath );
	return QDir::cleanPath( szPath );
}
} // namespace
//-----------------------------------------------------------------------------
// Purpose: Keeps tool paths in Preferences independently of compiler implementation or filename.
//-----------------------------------------------------------------------------
QString SourceToolSetting( SourceTool_t nTool )
{
	return QStringLiteral( "sourceTools/" ) + SourceToolBaseName( nTool );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString SourceToolBaseName( SourceTool_t nTool )
{
	switch ( nTool )
	{
		case SourceTool_t::SOURCE_TOOL_STUDIOMDL: return QStringLiteral( "studiomdl" );
		case SourceTool_t::SOURCE_TOOL_BSPZIP: return QStringLiteral( "bspzip" );
		case SourceTool_t::SOURCE_TOOL_VBSP: return QStringLiteral( "vbsp" );
		case SourceTool_t::SOURCE_TOOL_VVIS: return QStringLiteral( "vvis" );
		case SourceTool_t::SOURCE_TOOL_VRAD: return QStringLiteral( "vrad" );
		case SourceTool_t::SOURCE_TOOL_GAME: return QStringLiteral( "game" );
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString SourceToolLabel( SourceTool_t nTool )
{
	switch ( nTool )
	{
		case SourceTool_t::SOURCE_TOOL_STUDIOMDL: return QObject::tr( "studiomdl / StudioMDL++" );
		case SourceTool_t::SOURCE_TOOL_BSPZIP: return QObject::tr( "bspzip / BSPZIP++" );
		case SourceTool_t::SOURCE_TOOL_VBSP: return QObject::tr( "vbsp / VBSP++" );
		case SourceTool_t::SOURCE_TOOL_VVIS: return QObject::tr( "vvis / VVIS++" );
		case SourceTool_t::SOURCE_TOOL_VRAD: return QObject::tr( "vrad / VRAD++" );
		case SourceTool_t::SOURCE_TOOL_GAME: return QObject::tr( "The game (cstrike.sh, hl2.sh, hl2.exe…)" );
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString BspDirSetting()
{
	return QStringLiteral( "sourceTools/bspDir" );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString FindBspDir( const xv::vfs::CGameInfo &game, const QString &szOverride )
{
	if ( !szOverride.trimmed().isEmpty() )
		return QDir( szOverride.trimmed() ).absolutePath();
	return QDir( ToQString( game.m_GameDir ) ).filePath( QStringLiteral( "maps" ) );
}
//-----------------------------------------------------------------------------
// Purpose: Prefers explicit configuration, then ++ replacements and stock tools near the game.
//-----------------------------------------------------------------------------
QString FindSourceTool( SourceTool_t nTool, const xv::vfs::CGameInfo &game, const QString &szOverride )
{
	if ( !szOverride.trimmed().isEmpty() )
		return QFileInfo( szOverride ).absoluteFilePath();
	QStringList directories;
	for ( const auto &entry : game.m_SearchPaths )
		if ( std::ranges::find( entry.m_Ids, std::string( "gamebin" ) ) != entry.m_Ids.end() )
			directories << Expand( QString::fromStdString( entry.m_szPath ), game );
	// A game can have both bin/ and BIN/ (Counter-Strike: Source does; its Windows compilers are
	// in BIN/x64).
	for ( const auto &root : { game.m_GameDir, game.m_BaseDir } )
		for ( const auto *szSuffix : { "bin/linux64", "bin/linux32", "bin/win64", "bin/x64", "bin", "BIN/x64", "BIN", "" } )
			directories << QDir( ToQString( root ) ).filePath( QString::fromUtf8( szSuffix ) );
	directories.removeDuplicates();
	const QString szBase = SourceToolBaseName( nTool );
	QStringList names{ szBase + QStringLiteral( "++" ), szBase + QStringLiteral( "plusplus" ), szBase };
	if ( nTool == SourceTool_t::SOURCE_TOOL_GAME )
	{
		// The game's own launcher: the mod's script (cstrike.sh), Half-Life 2's, or the exe.
		const QString szGame = QFileInfo( ToQString( game.m_GameDir ) ).fileName();
		names = { szGame + QStringLiteral( ".sh" ), QStringLiteral( "hl2.sh" ), QStringLiteral( "hl2_linux" ), QStringLiteral( "hl2.exe" ), szGame + QStringLiteral( ".exe" ) };
		directories = { ToQString( game.m_BaseDir ) };
	}
#ifdef Q_OS_WIN
	if ( nTool != SourceTool_t::SOURCE_TOOL_GAME )
		for ( QString &szName : names )
			szName += QStringLiteral( ".exe" );
#endif
	for ( const auto &szDirectory : directories )
		for ( const auto &szName : names )
		{
			const auto path = QDir( szDirectory ).filePath( szName );
			if ( QFileInfo( path ).isFile() && QFileInfo( path ).isExecutable() )
				return QFileInfo( path ).absoluteFilePath();
		}
	for ( const auto &szName : names )
		if ( nTool != SourceTool_t::SOURCE_TOOL_GAME )
			if ( const auto path = QStandardPaths::findExecutable( szName ); !path.isEmpty() )
				return path;
	// What the game's Hammer configuration names, if it is really there.
	const xv::map::HammerCompilePaths_t hints = xv::map::FindGameFgds( game.m_GameDir ).m_Compile;
	const std::filesystem::path *pHint = nTool == SourceTool_t::SOURCE_TOOL_VBSP ? &hints.m_Bsp : nTool == SourceTool_t::SOURCE_TOOL_VVIS ? &hints.m_Vis
		: nTool == SourceTool_t::SOURCE_TOOL_VRAD                                                                                         ? &hints.m_Light
		: nTool == SourceTool_t::SOURCE_TOOL_GAME                                                                                         ? &hints.m_GameExe
																																		  : nullptr;
	if ( pHint && !pHint->empty() && QFileInfo( ToQString( *pHint ) ).isFile() )
		return ToQString( *pHint );
	return {};
}
//-----------------------------------------------------------------------------
// Purpose: Writes a temporary gameinfo with absolute original mounts and a writable output mount.
//-----------------------------------------------------------------------------
bool WriteBuildGame( const xv::vfs::CGameInfo &game, const QString &szFolder, QString *pError )
{
	QFile source( ToQString( game.m_File ) );
	if ( !source.open( QIODevice::ReadOnly ) )
	{
		if ( pError )
			*pError = source.errorString();
		return false;
	}
	auto parsed = xv::kv1::Parse( source.readAll().toStdString() );
	if ( parsed.HasErrors() )
	{
		if ( pError )
			*pError = QObject::tr( "The game's gameinfo.txt could not be parsed." );
		return false;
	}
	xv::kv1::CNode *pPaths = nullptr;
	for ( auto &root : parsed.m_Document.m_Roots )
		if ( root.IsBlock() )
			if ( auto *pFs = root.Find( "FileSystem" ) )
				pPaths = pFs->Find( "SearchPaths" );
	if ( !pPaths || !pPaths->IsBlock() )
	{
		if ( pError )
			*pError = QObject::tr( "gameinfo.txt has no SearchPaths." );
		return false;
	}
	for ( std::size_t i = 0; i < pPaths->GetChildren().size(); i++ )
	{
		auto &entry = pPaths->Child( i );
		if ( entry.IsPair() )
			if ( !entry.SetValue( Expand( QString::fromStdString( entry.GetValue() ), game ).toStdString() ) )
			{
				if ( pError )
					*pError = QObject::tr( "A game path cannot be represented in gameinfo.txt." );
				return false;
			}
	}
	pPaths->InsertChild( 0, xv::kv1::CNode::MakePair( "game+mod", QDir::fromNativeSeparators( QFileInfo( szFolder ).absoluteFilePath() ).toStdString() ) );
	if ( !QDir().mkpath( szFolder ) )
	{
		if ( pError )
			*pError = QObject::tr( "Could not create build directory." );
		return false;
	}
	QSaveFile out( QDir( szFolder ).filePath( QStringLiteral( "gameinfo.txt" ) ) );
	const auto text = xv::kv1::Write( parsed.m_Document );
	if ( !out.open( QIODevice::WriteOnly ) || out.write( text.data(), text.size() ) != static_cast<qint64>( text.size() ) || !out.commit() )
	{
		if ( pError )
			*pError = out.errorString();
		return false;
	}
	return true;
}
//-----------------------------------------------------------------------------
// Purpose: Captures a tool's combined output and reports startup, crash and cancellation outcomes.
//-----------------------------------------------------------------------------
CSourceToolRunner::CSourceToolRunner( QObject *pParent )
	: QObject( pParent )
{
	m_Process.setProcessChannelMode( QProcess::MergedChannels );
	connect( &m_Process, &QProcess::readyReadStandardOutput, this, [ this ] { Append( QString::fromLocal8Bit( m_Process.readAllStandardOutput() ) ); } );
	connect( &m_Process, &QProcess::errorOccurred, this, [ this ]( QProcess::ProcessError nError ) {if(nError==QProcess::FailedToStart&&!m_bReported){m_bReported=true;Append(m_Process.errorString()+u'\n');emit Finished(false);} } );
	connect( &m_Process, qOverload<int, QProcess::ExitStatus>( &QProcess::finished ), this, [ this ]( int nCode, QProcess::ExitStatus nStatus ) {Append(QString::fromLocal8Bit(m_Process.readAllStandardOutput()));if(!m_bReported){m_bReported=true;Append(m_bCancelled?tr("Cancelled.\n"):tr("Process exited with code %1.\n").arg(nCode));emit Finished(!m_bCancelled&&nStatus==QProcess::NormalExit&&nCode==0);} } );
}
//-----------------------------------------------------------------------------
// Purpose: Prevents a tool from outliving its owning task window.
//-----------------------------------------------------------------------------
CSourceToolRunner::~CSourceToolRunner()
{
	if ( IsRunning() )
	{
		m_Process.kill();
		m_Process.waitForFinished( 1000 );
	}
}
//-----------------------------------------------------------------------------
// Purpose: Starts a native executable with an argument vector, preserving spaces without shell code.
//-----------------------------------------------------------------------------
bool CSourceToolRunner::Start( const QString &szProgram, const QStringList &arguments, const QString &szDirectory, const QString &szGameDirectory )
{
	if ( IsRunning() )
		return false;
	m_szLog.clear();
	m_bCancelled = false;
	m_bReported = false;
	if ( !QFileInfo( szProgram ).isFile() || !QFileInfo( szProgram ).isExecutable() )
	{
		Append( tr( "%1 isn't an executable file. Choose the tool in Preferences > Source Tools.\n" ).arg( szProgram.isEmpty() ? tr( "(nothing)" ) : szProgram ) );
		m_bReported = true;
		emit Finished( false );
		return false;
	}
	auto env = QProcessEnvironment::systemEnvironment();
	if ( !szGameDirectory.isEmpty() )
		env.insert( QStringLiteral( "VPROJECT" ), szGameDirectory );
#ifndef Q_OS_WIN
	const auto szBin = QFileInfo( szProgram ).absolutePath();
	env.insert( QStringLiteral( "LD_LIBRARY_PATH" ), szBin + u':' + QDir( szBin ).filePath( QStringLiteral( ".." ) ) + u':' + env.value( QStringLiteral( "LD_LIBRARY_PATH" ) ) );
#endif
	m_Process.setProcessEnvironment( env );
	m_Process.setWorkingDirectory( szDirectory );
	m_Process.setProgram( szProgram );
	m_Process.setArguments( arguments );
	Append( szProgram + u' ' + arguments.join( u' ' ) + u'\n' );
	m_Process.start();
	return true;
}
//-----------------------------------------------------------------------------
// Purpose: Requests termination, then kills only this task's child if it does not exit promptly.
//-----------------------------------------------------------------------------
void CSourceToolRunner::Cancel()
{
	if ( !IsRunning() )
		return;
	m_bCancelled = true;
	m_Process.kill();
}
//-----------------------------------------------------------------------------
// Purpose: Retains a readable log while streaming new lines to the task window.
//-----------------------------------------------------------------------------
void CSourceToolRunner::Append( const QString &szText )
{
	if ( szText.isEmpty() )
		return;
	m_szLog += szText;
	if ( m_szLog.size() > 4 * 1024 * 1024 )
		m_szLog.remove( 0, m_szLog.size() - 4 * 1024 * 1024 );
	emit Output( szText );
}
