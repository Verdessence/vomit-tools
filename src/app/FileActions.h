//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What can be done to a file or folder on disk from the file trees: show it in the
//          system's file manager, and the right-click menu (Explorer's own on Windows; elsewhere
//          open, open with the default app, show in the file manager, copy the path, rename and
//          move to the trash).
//
//===========================================================================//
#pragma once

#include <functional>

#include <QPoint>
#include <QString>

class QWidget;

// Opens the folder holding a file (or the folder itself) in the system's file manager, with the
// file selected where the file manager can do that.
void RevealInFileManager( const QString &szPath );
// The right-click menu for a file or folder on disk, at a point on the screen. pfnOpen opens it in
// vomit-tools; it may be empty.
void ShowFileMenu( QWidget *pParent, const QPoint &globalAt, const QString &szPath, const std::function<void()> &pfnOpen );
