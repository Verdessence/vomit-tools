//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Material pictures shared by the browser and Face Edit. Only QImages cross worker
//          threads; GUI consumers create their pixmaps. The cache also keeps source dimensions.
//
//===========================================================================//
#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include "vfs/Vfs.h"

struct MaterialPicture_t
{
	QImage m_Image;
	QSize m_Size;
};

[[nodiscard]] QString MaterialPictureCacheFolder();
[[nodiscard]] MaterialPicture_t LoadMaterialPicture( const xv::vfs::CFileSystem &fileSystem, const QString &szGame, const QString &szName, int nPictureSize );
