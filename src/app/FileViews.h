// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include <QString>
#include <QWidget>

class SoundPlayer;

// Read-only inspectors for files picked in the VFS browser. Editors replace these per format later.

// KeyValues1 text, with parse diagnostics and a lossless round-trip check.
class KeyValuesView : public QWidget {
	Q_OBJECT

public:
	KeyValuesView(const QString& path, std::span<const std::byte> bytes, QWidget* parent = nullptr);
};

// DMX / PCF element graph, plus its keyvalues2 text form.
class DmxView : public QWidget {
	Q_OBJECT

public:
	DmxView(const QString& path, std::span<const std::byte> bytes, QWidget* parent = nullptr);
};

// WAV/MP3: format, a waveform with the loop point, and playback when a player is given.
class AudioView : public QWidget {
	Q_OBJECT

public:
	AudioView(const QString& path, const QString& source, std::span<const std::byte> bytes, SoundPlayer* player, QWidget* parent = nullptr);
};

// Anything without a dedicated view.
class FileInfoView : public QWidget {
	Q_OBJECT

public:
	FileInfoView(const QString& path, const QString& source, std::size_t size, QWidget* parent = nullptr);
};

// Picks the view for a path, by extension. player may be null.
[[nodiscard]] QWidget* createFileView(const QString& path, const QString& source, std::span<const std::byte> bytes, SoundPlayer* player = nullptr, QWidget* parent = nullptr);
