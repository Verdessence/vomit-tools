//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: One look for every file tree (the game browser, the folder browser, an archive's
//          files): a folder's icon (closed, and open while expanded) and a file's icon and kind
//          by its extension, the Source formats included, so the same file looks the same
//          wherever it is listed.
//
//===========================================================================//
#pragma once

#include <QAbstractFileIconProvider>
#include <QIcon>
#include <QString>
#include <QStringView>

// The folder icon; its On state (the tree draws expanded rows with it) is the open folder.
[[nodiscard]] QIcon FolderIcon();
// A file's icon by its name's extension.
[[nodiscard]] QIcon FileIcon( QStringView szName );
// What a file is, in a word or two ("Material", "Texture", "WAV sound"...), by its extension.
[[nodiscard]] QString FileKind( QStringView szName );
[[nodiscard]] QString FolderKind();

// For QFileSystemModel: the same icons and kinds as the other trees.
class CFileIconProvider : public QAbstractFileIconProvider
{
public:
	[[nodiscard]] QIcon icon( IconType nType ) const override;
	[[nodiscard]] QIcon icon( const QFileInfo &info ) const override;
	[[nodiscard]] QString type( const QFileInfo &info ) const override;
};
