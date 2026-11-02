//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Discord's Rich Presence: what the app is doing shown on the user's Discord profile,
//          over Discord's local connection (the discord-ipc socket the desktop app, Vesktop's
//          arRPC and the like listen on; a named pipe on Windows). Off until an application ID
//          is set (MakeDiscordSettings' fields); nothing leaves the machine but to the Discord
//          client.
//
//===========================================================================//
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class QLocalSocket;
class QTimer;
class QWidget;

// What the profile shows: the application's name (from Discord's side), then the details,
// the state, and the time since the start; a picture and a button.
struct DiscordActivity_t
{
	QString m_szDetails;
	QString m_szState;
	qint64 m_nStart = 0;   // seconds since the epoch; 0: no timer
	QString m_szImage;     // an asset key of the application, or an https URL
	QString m_szImageText;
	QString m_szButtonLabel;
	QString m_szButtonUrl;

	friend bool operator==( const DiscordActivity_t &, const DiscordActivity_t & ) = default;
};

// What was set (MakeDiscordSettings' fields), read from QSettings.
struct DiscordSettings_t
{
	bool m_bEnabled = true;
	QString m_szClientId;  // the Discord application's ID; empty: off
	QString m_szDetails;
	QString m_szImage;
	QString m_szButtonLabel;
	QString m_szButtonUrl;

	[[nodiscard]] static DiscordSettings_t Load();
	void Save() const;
};

// A frame of Discord's local protocol: the opcode and the length, little-endian, then the JSON.
[[nodiscard]] QByteArray DiscordFrame( int nOpcode, const QJsonObject &body );
// SET_ACTIVITY's activity: empty fields left out, as Discord rejects empty strings.
[[nodiscard]] QJsonObject DiscordActivityJson( const DiscordActivity_t &activity );
// Where a Discord client listens, in the order tried: discord-ipc-0 to 9 in the runtime
// folder, then Flatpak's and Snap's; on Windows, the pipe names.
[[nodiscard]] QStringList DiscordSocketPaths();
// The verb for an editor's kind ("Maps" is "Mapping"...), from CTabGroups::KindOf.
[[nodiscard]] QString DiscordVerb( const QString &szKind, const QString &szFile );
// The settings' fields (on or off, the application ID, details, picture, button), each saved
// as it is left. Not in Preferences: a local build puts it on its Dev page.
[[nodiscard]] QWidget *MakeDiscordSettings();

class CDiscordPresence : public QObject
{
	Q_OBJECT

public:
	explicit CDiscordPresence( QObject *pParent = nullptr );

	// The settings read again: connects, reconnects (another ID) or lets go.
	void Reload();
	// Shown once connected (and again after a reconnect); sent at most every few seconds.
	void SetActivity( const DiscordActivity_t &activity );
	[[nodiscard]] bool IsReady() const { return m_bReady; }
	[[nodiscard]] const DiscordSettings_t &GetSettings() const { return m_Settings; }

signals:
	void ReadyChanged( bool bReady );

private:
	void TryConnect();
	void Connected();
	void ReadFrames();
	void Disconnected();
	void SendActivity();

	DiscordSettings_t m_Settings;
	QLocalSocket *m_pSocket = nullptr;
	QTimer *m_pRetry = nullptr;
	QTimer *m_pSend = nullptr;
	QStringList m_Paths;   // the ones still to try this round
	QByteArray m_Buffer;
	DiscordActivity_t m_Activity;
	bool m_bReady = false; // the handshake done
	int m_nNonce = 0;
};
