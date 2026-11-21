//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Where the app keeps its data (crash logs, the debug log, autosaves) and its cache
//          (pictures, lighting). Qt's own AppDataLocation and CacheLocation put the
//          organisation and the application in the path, both "vomit-tools", so these are
//          the generic folders with the application's name once under them (a test suite's
//          own name, so tests keep out of the app's folders).
//
//===========================================================================//
#pragma once

#include <QString>

// ~/.local/share/vomit-tools on Linux (QStandardPaths' generic data folder, then vomit-tools).
[[nodiscard]] QString AppDataFolder();
// ~/.cache/vomit-tools on Linux.
[[nodiscard]] QString AppCacheFolder();
// What older versions kept a level down (vomit-tools/vomit-tools) moved up into the folders
// above, where nothing of the same name is there yet; the old folder goes once it is empty.
void MoveOldAppFolders();
