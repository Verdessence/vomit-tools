//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Autosave and crash recovery for maps: where a map's autosave goes (beside the map,
//          or under the app's data for a new map or one read from the game's files), writing
//          it, and finding the ones a crash left behind.
//
//===========================================================================//
#pragma once

#include <string_view>
#include <vector>

#include <QDateTime>
#include <QString>

// A map on disk autosaves beside itself ("<map>.vmf.autosave"); one from the game's files or a
// new one (szPath empty, szNewKey naming it) autosaves under the app's data folder.
[[nodiscard]] QString MapAutosavePath( const QString &szPath, bool bFromGame, const QString &szNewKey = {} );

// A name for a new map's autosave, unique to this run of the app.
[[nodiscard]] QString NewMapAutosaveKey();

// Writes the text in one go (the file never half written). false, with the reason, on failure.
bool WriteMapAutosave( const QString &szAutosave, std::string_view szText, QString *pError = nullptr );

// An autosave for the map that is newer than the map itself (or whose map is gone): the one to
// offer when the map opens. Empty when there is none.
[[nodiscard]] QString RecoverableAutosave( const QString &szPath, bool bFromGame );

struct OrphanAutosave_t
{
	QString m_szPath;      // the autosave
	QDateTime m_Modified;
};

// New maps' autosaves left by a run that ended without saving or closing them (a crash).
[[nodiscard]] std::vector<OrphanAutosave_t> OrphanNewMapAutosaves();
