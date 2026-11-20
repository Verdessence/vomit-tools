//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Lossless filesystem conversions, using Qt's forward-slash paths in the interface.
//
//===========================================================================//
#pragma once

#include <filesystem>

#include <QDir>
#include <QString>

inline QString ToQString( const std::filesystem::path &path )
{
	return QDir::fromNativeSeparators( QString::fromStdU16String( path.u16string() ) );
}

inline std::filesystem::path ToPath( const QString &szPath )
{
	return std::filesystem::path{ szPath.toStdU16String() };
}
