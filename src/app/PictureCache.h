//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Lossless JPEG XL pictures shared by the material and model caches.
//
//===========================================================================//
#pragma once

#include <QImage>

[[nodiscard]] QImage ReadCachedPicture( const QString &szStem, QSize *pSourceSize = nullptr );
[[nodiscard]] bool WriteCachedPicture( const QString &szStem, const QImage &picture, QSize sourceSize = {} );
