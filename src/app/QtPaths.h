// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>

#include <QDir>
#include <QString>

// Lossless conversions between Qt strings and filesystem paths on every platform.
inline QString toQString(const std::filesystem::path& path) {
	return QDir::toNativeSeparators(QString::fromStdU16String(path.u16string()));
}

inline std::filesystem::path toPath(const QString& path) {
	return std::filesystem::path{path.toStdU16String()};
}
