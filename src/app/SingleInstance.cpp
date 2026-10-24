//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See SingleInstance.h.
//
//===========================================================================//
#include "SingleInstance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>

//-----------------------------------------------------------------------------
// Purpose: szName tells instances apart; empty for the app's, one per user.
//-----------------------------------------------------------------------------
CSingleInstance::CSingleInstance( QString szName, QObject *pParent )
	: QObject( pParent ),
	  m_szName( std::move( szName ) )
{
	if ( m_szName.isEmpty() )
	{
		// Per user: the home folder's hash, so two people on one machine each get their own.
		m_szName = QStringLiteral( "vomit-tools-" ) + QString::fromLatin1( QCryptographicHash::hash( QDir::homePath().toUtf8(), QCryptographicHash::Sha1 ).toHex().left( 12 ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Hands paths to a running instance. False when none answers.
//-----------------------------------------------------------------------------
bool CSingleInstance::SendToRunning( const QStringList &paths ) const
{
	QLocalSocket socket;
	socket.connectToServer( m_szName );
	if ( !socket.waitForConnected( 500 ) )
	{
		return false;
	}
	// One path a line; an empty message just brings the window forward.
	socket.write( paths.join( u'\n' ).toUtf8() );
	socket.flush();
	socket.waitForBytesWritten( 1000 );
	socket.disconnectFromServer();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Becomes the running instance: pfnOpen gets what later launches hand over.
//-----------------------------------------------------------------------------
bool CSingleInstance::Listen( std::function<void( const QStringList & )> pfnOpen )
{
	m_pServer = new QLocalServer( this );
	m_pServer->setSocketOptions( QLocalServer::UserAccessOption );
	if ( !m_pServer->listen( m_szName ) )
	{
		// Left behind by a run that crashed.
		QLocalServer::removeServer( m_szName );
		if ( !m_pServer->listen( m_szName ) )
		{
			return false;
		}
	}
	connect( m_pServer, &QLocalServer::newConnection, this, [ this, pfnOpen ] {
		while ( QLocalSocket *pSocket = m_pServer->nextPendingConnection() )
		{
			connect( pSocket, &QLocalSocket::disconnected, pSocket, [ pSocket, pfnOpen ] {
				const QString szText = QString::fromUtf8( pSocket->readAll() );
				pfnOpen( szText.isEmpty() ? QStringList{} : szText.split( u'\n', Qt::SkipEmptyParts ) );
				pSocket->deleteLater();
			} );
		}
	} );
	return true;
}
