//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See DiscordPresence.h.
//
//===========================================================================//
#include "DiscordPresence.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QSettings>
#include <QTimer>
#include <QtEndian>

#include "DebugLog.h"
#include "Preferences.h"

namespace
{

// The protocol's opcodes.
constexpr int OPCODE_HANDSHAKE = 0;
constexpr int OPCODE_FRAME = 1;
constexpr int OPCODE_CLOSE = 2;
constexpr int OPCODE_PING = 3;
constexpr int OPCODE_PONG = 4;

// How long before trying the sockets again when no Discord client is listening.
constexpr int RETRY_MS = 15000;
// Discord takes a few changes of activity in twenty seconds; tab switches come faster.
constexpr int SEND_MS = 1500;

} // namespace

//-----------------------------------------------------------------------------
// Purpose: A button that leads nowhere much and a picture under the key "icon" until the user
//          says otherwise.
//-----------------------------------------------------------------------------
DiscordSettings_t DiscordSettings_t::Load()
{
	QSettings settings;
	DiscordSettings_t out;
	out.m_bEnabled = settings.value( QStringLiteral( "discord/enabled" ), true ).toBool();
	out.m_szClientId = settings.value( QStringLiteral( "discord/clientId" ) ).toString().trimmed();
	out.m_szDetails = settings.value( QStringLiteral( "discord/details" ), QStringLiteral( "Source modding tools" ) ).toString();
	out.m_szImage = settings.value( QStringLiteral( "discord/image" ), QStringLiteral( "icon" ) ).toString().trimmed();
	out.m_szButtonLabel = settings.value( QStringLiteral( "discord/buttonLabel" ), QStringLiteral( "BUTTON THAT DOES NOTHING" ) ).toString();
	out.m_szButtonUrl = settings.value( QStringLiteral( "discord/buttonUrl" ), QStringLiteral( "http://localhost/" ) ).toString().trimmed();
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void DiscordSettings_t::Save() const
{
	QSettings settings;
	settings.setValue( QStringLiteral( "discord/enabled" ), m_bEnabled );
	settings.setValue( QStringLiteral( "discord/clientId" ), m_szClientId );
	settings.setValue( QStringLiteral( "discord/details" ), m_szDetails );
	settings.setValue( QStringLiteral( "discord/image" ), m_szImage );
	settings.setValue( QStringLiteral( "discord/buttonLabel" ), m_szButtonLabel );
	settings.setValue( QStringLiteral( "discord/buttonUrl" ), m_szButtonUrl );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QByteArray DiscordFrame( int nOpcode, const QJsonObject &body )
{
	const QByteArray json = QJsonDocument( body ).toJson( QJsonDocument::Compact );
	QByteArray frame( 8, '\0' );
	qToLittleEndian<quint32>( static_cast<quint32>( nOpcode ), frame.data() );
	qToLittleEndian<quint32>( static_cast<quint32>( json.size() ), frame.data() + 4 );
	return frame + json;
}

//-----------------------------------------------------------------------------
// Purpose: Discord wants two characters at least in a text and a button with both a label
//          and an http(s) address; anything short of that is left out rather than refused.
//-----------------------------------------------------------------------------
QJsonObject DiscordActivityJson( const DiscordActivity_t &activity )
{
	const auto text = []( const QString &sz ) { return sz.trimmed().size() >= 2 ? sz.left( 128 ) : QString{}; };
	QJsonObject json;
	if ( const QString sz = text( activity.m_szDetails ); !sz.isEmpty() )
		json.insert( QStringLiteral( "details" ), sz );
	if ( const QString sz = text( activity.m_szState ); !sz.isEmpty() )
		json.insert( QStringLiteral( "state" ), sz );
	if ( activity.m_nStart > 0 )
		json.insert( QStringLiteral( "timestamps" ), QJsonObject{ { QStringLiteral( "start" ), activity.m_nStart } } );
	if ( !activity.m_szImage.isEmpty() )
	{
		QJsonObject assets{ { QStringLiteral( "large_image" ), activity.m_szImage } };
		if ( const QString sz = text( activity.m_szImageText ); !sz.isEmpty() )
			assets.insert( QStringLiteral( "large_text" ), sz );
		json.insert( QStringLiteral( "assets" ), assets );
	}
	const bool bUrl = activity.m_szButtonUrl.startsWith( QStringLiteral( "http://" ) ) || activity.m_szButtonUrl.startsWith( QStringLiteral( "https://" ) );
	if ( !activity.m_szButtonLabel.trimmed().isEmpty() && bUrl )
	{
		json.insert( QStringLiteral( "buttons" ), QJsonArray{ QJsonObject{ { QStringLiteral( "label" ), activity.m_szButtonLabel.left( 32 ) }, { QStringLiteral( "url" ), activity.m_szButtonUrl } } } );
	}
	return json;
}

//-----------------------------------------------------------------------------
// Purpose: The desktop client and Vesktop's arRPC listen in the runtime folder; the Flatpak and
//          Snap builds in folders of their own inside it.
//-----------------------------------------------------------------------------
QStringList DiscordSocketPaths()
{
	QStringList paths;
#if defined( Q_OS_WIN )
	for ( int n = 0; n < 10; n++ )
		paths << QStringLiteral( "discord-ipc-%1" ).arg( n );
#else
	QStringList folders;
	for ( const char *pszVariable : { "XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP" } )
	{
		const QString szFolder = qEnvironmentVariable( pszVariable );
		if ( !szFolder.isEmpty() && !folders.contains( szFolder ) )
			folders << szFolder;
	}
	if ( !folders.contains( QStringLiteral( "/tmp" ) ) )
		folders << QStringLiteral( "/tmp" );
	for ( const QString &szFolder : folders )
	{
		for ( const QString &szInside : { QString{}, QStringLiteral( "app/com.discordapp.Discord/" ), QStringLiteral( "app/dev.vencord.Vesktop/" ), QStringLiteral( "snap.discord/" ) } )
		{
			for ( int n = 0; n < 10; n++ )
				paths << QStringLiteral( "%1/%2discord-ipc-%3" ).arg( szFolder, szInside ).arg( n );
		}
	}
#endif
	return paths;
}

//-----------------------------------------------------------------------------
// Purpose: An archive tab is a BSP's (its entities, lumps and Stripper config) or a VPK's.
//-----------------------------------------------------------------------------
QString DiscordVerb( const QString &szKind, const QString &szFile )
{
	if ( szKind == QLatin1String( "Maps" ) )
		return QStringLiteral( "Mapping" );
	if ( szKind == QLatin1String( "Models" ) )
		return QStringLiteral( "Modelling" );
	if ( szKind == QLatin1String( "Textures" ) )
		return QStringLiteral( "Materialing" );
	if ( szKind == QLatin1String( "Particles" ) )
		return QStringLiteral( "Particling" );
	if ( szKind == QLatin1String( "Sounds" ) )
		return QStringLiteral( "Soundscaping" );
	if ( szKind == QLatin1String( "Skies" ) )
		return QStringLiteral( "Skyboxing" );
	if ( szKind == QLatin1String( "Archives" ) && szFile.endsWith( QStringLiteral( ".bsp" ), Qt::CaseInsensitive ) )
		return QStringLiteral( "Stripping" );
	return QStringLiteral( "Viewing" );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CDiscordPresence::CDiscordPresence( QObject *pParent )
	: QObject( pParent )
{
	m_pSocket = new QLocalSocket( this );
	m_pRetry = new QTimer( this );
	m_pRetry->setSingleShot( true );
	m_pSend = new QTimer( this );
	m_pSend->setSingleShot( true );
	connect( m_pSocket, &QLocalSocket::connected, this, &CDiscordPresence::Connected );
	connect( m_pSocket, &QLocalSocket::readyRead, this, &CDiscordPresence::ReadFrames );
	connect( m_pSocket, &QLocalSocket::disconnected, this, &CDiscordPresence::Disconnected );
	connect( m_pSocket, &QLocalSocket::errorOccurred, this, [ this ] {
		// Not listening here: the next place, or another round later.
		if ( m_pSocket->state() == QLocalSocket::UnconnectedState && !m_bReady )
			TryConnect();
	} );
	connect( m_pRetry, &QTimer::timeout, this, &CDiscordPresence::TryConnect );
	connect( m_pSend, &QTimer::timeout, this, &CDiscordPresence::SendActivity );
	Reload();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDiscordPresence::Reload()
{
	const DiscordSettings_t settings = DiscordSettings_t::Load();
	const bool bOtherApp = settings.m_szClientId != m_Settings.m_szClientId;
	m_Settings = settings;
	if ( !m_Settings.m_bEnabled || m_Settings.m_szClientId.isEmpty() || bOtherApp )
	{
		m_pRetry->stop();
		m_Paths.clear();
		m_pSocket->abort();
		if ( m_bReady )
		{
			m_bReady = false;
			emit ReadyChanged( false );
		}
	}
	if ( m_Settings.m_bEnabled && !m_Settings.m_szClientId.isEmpty() && m_pSocket->state() == QLocalSocket::UnconnectedState )
	{
		TryConnect();
	}
	else if ( m_bReady && !m_pSend->isActive() )
	{
		m_pSend->start( SEND_MS ); // the details or the button changed
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDiscordPresence::SetActivity( const DiscordActivity_t &activity )
{
	if ( activity == m_Activity )
		return;
	m_Activity = activity;
	if ( m_bReady && !m_pSend->isActive() )
		m_pSend->start( SEND_MS );
}

//-----------------------------------------------------------------------------
// Purpose: The next place a client may listen; after the last, a wait before the next round.
//-----------------------------------------------------------------------------
void CDiscordPresence::TryConnect()
{
	if ( !m_Settings.m_bEnabled || m_Settings.m_szClientId.isEmpty() )
		return;
	if ( m_Paths.isEmpty() )
	{
		if ( m_pRetry->isActive() )
			return;
		m_Paths = DiscordSocketPaths();
	}
	const QString szPath = m_Paths.takeFirst();
	if ( m_Paths.isEmpty() )
	{
		m_pRetry->start( RETRY_MS );
	}
	m_Buffer.clear();
	m_pSocket->abort();
	m_pSocket->connectToServer( szPath );
}

//-----------------------------------------------------------------------------
// Purpose: The handshake names the application; the client answers READY.
//-----------------------------------------------------------------------------
void CDiscordPresence::Connected()
{
	m_Paths.clear();
	m_pRetry->stop();
	DebugLog() << "Discord: connected to" << m_pSocket->serverName();
	m_pSocket->write( DiscordFrame( OPCODE_HANDSHAKE, QJsonObject{ { QStringLiteral( "v" ), 1 }, { QStringLiteral( "client_id" ), m_Settings.m_szClientId } } ) );
}

//-----------------------------------------------------------------------------
// Purpose: Whole frames as they arrive: READY starts the presence, a ping is answered, a close
//          (an unknown application ID, say) ends the connection.
//-----------------------------------------------------------------------------
void CDiscordPresence::ReadFrames()
{
	m_Buffer += m_pSocket->readAll();
	while ( m_Buffer.size() >= 8 )
	{
		const auto nOpcode = static_cast<int>( qFromLittleEndian<quint32>( m_Buffer.constData() ) );
		const auto nLength = static_cast<qsizetype>( qFromLittleEndian<quint32>( m_Buffer.constData() + 4 ) );
		if ( m_Buffer.size() < 8 + nLength )
			return;
		const QByteArray json = m_Buffer.mid( 8, nLength );
		m_Buffer.remove( 0, 8 + nLength );
		const QJsonObject body = QJsonDocument::fromJson( json ).object();
		if ( nOpcode == OPCODE_FRAME && body.value( QStringLiteral( "evt" ) ).toString() == QLatin1String( "READY" ) )
		{
			m_bReady = true;
			emit ReadyChanged( true );
			SendActivity();
		}
		else if ( nOpcode == OPCODE_FRAME && body.value( QStringLiteral( "evt" ) ).toString() == QLatin1String( "ERROR" ) )
		{
			DebugLog() << "Discord:" << QString::fromUtf8( json );
		}
		else if ( nOpcode == OPCODE_PING )
		{
			m_pSocket->write( DiscordFrame( OPCODE_PONG, body ) );
		}
		else if ( nOpcode == OPCODE_CLOSE )
		{
			DebugLog() << "Discord: closed:" << QString::fromUtf8( json );
			m_pSocket->disconnectFromServer();
			return;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Discord closed (or went away): tried again later, from the first place.
//-----------------------------------------------------------------------------
void CDiscordPresence::Disconnected()
{
	if ( m_bReady )
	{
		m_bReady = false;
		emit ReadyChanged( false );
	}
	if ( m_Settings.m_bEnabled && !m_Settings.m_szClientId.isEmpty() && !m_pRetry->isActive() )
	{
		m_Paths.clear();
		m_pRetry->start( RETRY_MS );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The activity with the user's details, picture and button.
//-----------------------------------------------------------------------------
void CDiscordPresence::SendActivity()
{
	if ( !m_bReady )
		return;
	DiscordActivity_t activity = m_Activity;
	activity.m_szDetails = m_Settings.m_szDetails;
	activity.m_szImage = m_Settings.m_szImage;
	activity.m_szButtonLabel = m_Settings.m_szButtonLabel;
	activity.m_szButtonUrl = m_Settings.m_szButtonUrl;
	const QJsonObject args{ { QStringLiteral( "pid" ), static_cast<qint64>( QCoreApplication::applicationPid() ) }, { QStringLiteral( "activity" ), DiscordActivityJson( activity ) } };
	m_pSocket->write( DiscordFrame( OPCODE_FRAME, QJsonObject{ { QStringLiteral( "cmd" ), QStringLiteral( "SET_ACTIVITY" ) }, { QStringLiteral( "args" ), args }, { QStringLiteral( "nonce" ), QString::number( ++m_nNonce ) } } ) );
}

//-----------------------------------------------------------------------------
// Purpose: Each field is kept when it is left; the presence reloads.
//-----------------------------------------------------------------------------
QWidget *MakeDiscordSettings()
{
	auto *pPage = new QWidget;
	auto *pLayout = new QVBoxLayout( pPage );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	auto hint = []( const QString &szText ) {
		auto *pLabel = new QLabel( szText );
		pLabel->setWordWrap( true );
		return pLabel;
	};
	pLayout->addWidget( hint( QCoreApplication::translate( "CDiscordPresence", "Shows what you are doing on your Discord profile, through the Discord app running on this computer (Vesktop's arRPC too). It needs the ID of an application you made at discord.com/developers: its name is the title shown, its Rich Presence art the picture." ) ) );
	const DiscordSettings_t settings = DiscordSettings_t::Load();
	auto *pEnabled = new QCheckBox( QCoreApplication::translate( "CDiscordPresence", "Show my activity on Discord" ) );
	pEnabled->setObjectName( QStringLiteral( "discordEnabled" ) );
	pEnabled->setChecked( settings.m_bEnabled );
	pLayout->addWidget( pEnabled );
	auto *pForm = new QFormLayout;
	auto field = [ & ]( const char *pszName, const QString &szValue, const QString &szPlaceholder ) {
		auto *pEdit = new QLineEdit( szValue );
		pEdit->setObjectName( QLatin1String( pszName ) );
		pEdit->setPlaceholderText( szPlaceholder );
		return pEdit;
	};
	auto *pId = field( "discordClientId", settings.m_szClientId, QCoreApplication::translate( "CDiscordPresence", "Off until given" ) );
	auto *pDetails = field( "discordDetails", settings.m_szDetails, QString{} );
	auto *pImage = field( "discordImage", settings.m_szImage, QCoreApplication::translate( "CDiscordPresence", "An art asset's key, or an https address" ) );
	auto *pLabel = field( "discordButtonLabel", settings.m_szButtonLabel, QCoreApplication::translate( "CDiscordPresence", "No button" ) );
	auto *pUrl = field( "discordButtonUrl", settings.m_szButtonUrl, QStringLiteral( "https://" ) );
	pForm->addRow( QCoreApplication::translate( "CDiscordPresence", "Application ID" ), pId );
	pForm->addRow( QCoreApplication::translate( "CDiscordPresence", "Details" ), pDetails );
	pForm->addRow( QCoreApplication::translate( "CDiscordPresence", "Picture" ), pImage );
	pForm->addRow( QCoreApplication::translate( "CDiscordPresence", "Button" ), pLabel );
	pForm->addRow( QCoreApplication::translate( "CDiscordPresence", "Button opens" ), pUrl );
	pLayout->addLayout( pForm );
	pLayout->addWidget( hint( QCoreApplication::translate( "CDiscordPresence", "Under the details: what you are doing and the tab's file (Mapping dr_dust.vmf), and the time since the app started. Only others see the button." ) ) );
	auto save = [ = ] {
		DiscordSettings_t changed;
		changed.m_bEnabled = pEnabled->isChecked();
		changed.m_szClientId = pId->text().trimmed();
		changed.m_szDetails = pDetails->text();
		changed.m_szImage = pImage->text().trimmed();
		changed.m_szButtonLabel = pLabel->text();
		changed.m_szButtonUrl = pUrl->text().trimmed();
		changed.Save();
		CPreferences::Instance().NotifyDiscordChanged();
	};
	QObject::connect( pEnabled, &QCheckBox::toggled, pPage, save );
	for ( QLineEdit *pEdit : { pId, pDetails, pImage, pLabel, pUrl } )
		QObject::connect( pEdit, &QLineEdit::editingFinished, pPage, save );
	return pPage;
}
