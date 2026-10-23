//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: KeyValues1 text, with parse diagnostics and a lossless round-trip check.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <span>

#include <QString>
#include <QWidget>

class CSoundPlayer;

// Read-only inspectors for files picked in the VFS browser. Editors replace these per format later.

class CKeyValuesView : public QWidget
{
	Q_OBJECT

public:
	CKeyValuesView( const QString &szPath, std::span<const std::byte> bytes, QWidget *pParent = nullptr );
};

// DMX / PCF element graph, plus its keyvalues2 text form.
class CDmxView : public QWidget
{
	Q_OBJECT

public:
	CDmxView( const QString &szPath, std::span<const std::byte> bytes, QWidget *pParent = nullptr );
};

// WAV/MP3: format, a waveform with the loop point, and playback when a player is given.
class CAudioView : public QWidget
{
	Q_OBJECT

public:
	CAudioView( const QString &szPath, const QString &szSource, std::span<const std::byte> bytes, CSoundPlayer *pPlayer, QWidget *pParent = nullptr );
};

// VTFs and image files: the picture, fitted to the view over a checkerboard where it's
// transparent, and its size and format.
class CImageView : public QWidget
{
	Q_OBJECT

public:
	CImageView( const QString &szPath, const QString &szSource, std::span<const std::byte> bytes, QWidget *pParent = nullptr );
};

// Anything without a dedicated view.
class CFileInfoView : public QWidget
{
	Q_OBJECT

public:
	CFileInfoView( const QString &szPath, const QString &szSource, std::size_t nSize, QWidget *pParent = nullptr );
};

// Picks the view for a path, by extension. player may be null.
[[nodiscard]] QWidget *CreateFileView( const QString &szPath, const QString &szSource, std::span<const std::byte> bytes, CSoundPlayer *pPlayer = nullptr, QWidget *pParent = nullptr );
