//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Pictures saved as JPEG XL (libjxl), AVIF (libavif where the build has it, else a
//          Qt image plugin) or PNG, each with its own options.
//
//===========================================================================//
#pragma once

#include <QImage>
#include <QString>

enum class ImageExportFormat_t
{
	IMAGE_EXPORT_FORMAT_JXL,
	IMAGE_EXPORT_FORMAT_AVIF,
	IMAGE_EXPORT_FORMAT_PNG,
};

struct ImageExportOptions_t
{
	ImageExportFormat_t m_nFormat = ImageExportFormat_t::IMAGE_EXPORT_FORMAT_JXL;
	bool m_bLossless = false;
	int m_nQuality = 90; // 0-100, lossy only (JPEG XL's distance and AVIF's quantiser from it)
	int m_nEffort = 7;   // JPEG XL: 1 (fast) to 9 (small); AVIF: speed 10 (fast) to 0 (small), from it
	int m_nPngLevel = 6; // PNG's compression, 0 (none) to 9
};

// Whether this build can write the format (AVIF needs libavif with an encoder, or Qt's plugin).
[[nodiscard]] bool CanExportImage( ImageExportFormat_t nFormat );
// The format's file extension, without the dot, and its name for a file window's filter.
[[nodiscard]] QString ImageExportSuffix( ImageExportFormat_t nFormat );
[[nodiscard]] QString ImageExportName( ImageExportFormat_t nFormat );
// Writes the picture; false (and why in pError) when it couldn't.
bool ExportImage( const QImage &image, const QString &szPath, const ImageExportOptions_t &options, QString *pError = nullptr );
