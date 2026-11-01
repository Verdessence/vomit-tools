//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapAutosave.h.
//
//===========================================================================//
#include "MapAutosave.h"

#include <atomic>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>

namespace
{

constexpr char SUFFIX[] = ".autosave";
constexpr char NEW_PREFIX[] = "new-";

//-----------------------------------------------------------------------------
// Purpose: The app's own folder for autosaves that have no map file to sit beside.
//-----------------------------------------------------------------------------
QString AutosaveFolder()
{
	return QStandardPaths::writableLocation( QStandardPaths::AppDataLocation ) + QStringLiteral( "/autosave" );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: A game file is named by a hash of its path, so two games' maps of one name differ.
//-----------------------------------------------------------------------------
QString MapAutosavePath( const QString &szPath, bool bFromGame, const QString &szNewKey )
{
	if ( szPath.isEmpty() )
	{
		return AutosaveFolder() + QLatin1Char( '/' ) + szNewKey + QStringLiteral( ".vmf" ) + QLatin1String( SUFFIX );
	}
	if ( !bFromGame )
	{
		return szPath + QLatin1String( SUFFIX );
	}
	const QByteArray hash = QCryptographicHash::hash( szPath.toUtf8(), QCryptographicHash::Sha1 ).toHex().left( 12 );
	return AutosaveFolder() + QStringLiteral( "/game-%1-%2" ).arg( QString::fromLatin1( hash ), QFileInfo{ szPath }.fileName() ) + QLatin1String( SUFFIX );
}

//-----------------------------------------------------------------------------
// Purpose: The process id and a count keep two runs, and two new maps, apart.
//-----------------------------------------------------------------------------
QString NewMapAutosaveKey()
{
	static std::atomic<int> s_nCount{ 0 };
	return QStringLiteral( "%1%2-%3-%4" ).arg( QLatin1String( NEW_PREFIX ) ).arg( QCoreApplication::applicationPid() ).arg( QDateTime::currentMSecsSinceEpoch() ).arg( ++s_nCount );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool WriteMapAutosave( const QString &szAutosave, std::string_view szText, QString *pError )
{
	QDir{}.mkpath( QFileInfo{ szAutosave }.absolutePath() );
	QSaveFile file( szAutosave );
	if ( !file.open( QIODevice::WriteOnly ) || file.write( szText.data(), static_cast<qint64>( szText.size() ) ) != static_cast<qint64>( szText.size() ) || !file.commit() )
	{
		if ( pError )
		{
			*pError = file.errorString();
		}
		return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: An autosave older than the map is from before the map was last saved (by this app,
//          which removes its own, or by Hammer): not offered.
//-----------------------------------------------------------------------------
QString RecoverableAutosave( const QString &szPath, bool bFromGame )
{
	if ( szPath.isEmpty() )
	{
		return {};
	}
	const QString szAutosave = MapAutosavePath( szPath, bFromGame );
	const QFileInfo autosave{ szAutosave };
	if ( !autosave.isFile() )
	{
		return {};
	}
	if ( !bFromGame )
	{
		const QFileInfo map{ szPath };
		if ( map.exists() && map.lastModified() > autosave.lastModified() )
		{
			return {};
		}
	}
	return szAutosave;
}

//-----------------------------------------------------------------------------
// Purpose: A new map's editor holds a lock beside its autosave while it is open; one whose lock
//          can be taken has no editor left (QLockFile sees a dead owner's lock as stale).
//-----------------------------------------------------------------------------
std::vector<OrphanAutosave_t> OrphanNewMapAutosaves()
{
	std::vector<OrphanAutosave_t> orphans;
	const QDir folder{ AutosaveFolder() };
	for ( const QFileInfo &info : folder.entryInfoList( { QLatin1String( NEW_PREFIX ) + QStringLiteral( "*" ) + QLatin1String( SUFFIX ) }, QDir::Files, QDir::Time ) )
	{
		QLockFile lock( info.absoluteFilePath() + QStringLiteral( ".lock" ) );
		lock.setStaleLockTime( 0 );
		if ( !lock.tryLock( 0 ) )
		{
			continue;
		}
		lock.unlock();
		orphans.push_back( { info.absoluteFilePath(), info.lastModified() } );
	}
	return orphans;
}
