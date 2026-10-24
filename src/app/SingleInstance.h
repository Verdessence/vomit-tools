//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: One window for every launch: a second launch hands its files to the running one (over
//          a local socket) and quits. Edit > Preferences can turn it off.
//
//===========================================================================//
#pragma once

#include <functional>

#include <QObject>
#include <QStringList>

class QLocalServer;

class CSingleInstance : public QObject
{
	Q_OBJECT

public:
	// szName tells instances apart (tests use their own); empty for the app's, one per user.
	explicit CSingleInstance( QString szName = {}, QObject *pParent = nullptr );

	// Hands paths (absolute) to a running instance. False when none answers.
	[[nodiscard]] bool SendToRunning( const QStringList &paths ) const;
	// Becomes the running instance: pfnOpen gets what later launches hand over.
	bool Listen( std::function<void( const QStringList & )> pfnOpen );

private:
	QString m_szName;
	QLocalServer *m_pServer = nullptr;
};
