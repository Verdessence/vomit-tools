//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Qt's own settings file (window layouts, recent files, last folders, the session) as
//          Squirrel tables, vomit-tools.state.nut, like the settings file beside it
//          (vomit-tools.nut). Every key is a
//          slot of one table, "state", by its full name; values keep their types.
//
//===========================================================================//
#pragma once

#include <QSettings>

// The format, registered on first use. main() makes it the default before anything reads
// settings.
[[nodiscard]] QSettings::Format NutSettingsFormat();
// The first run with the format: what Qt's old file held (vomit-tools.conf, or the registry on
// Windows) is copied over, and the old file removed; a state file from before it had its own
// name (vomit-tools.nut, which is the settings' now) is renamed. Nothing happens once the new
// file exists.
void MigrateSettingsToNut();
