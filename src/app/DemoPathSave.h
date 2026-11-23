//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The paths page's Save Image…: what the picture holds and how it is saved (JPEG XL,
//          AVIF or PNG, and each one's options), then where.
//
//===========================================================================//
#pragma once

#include <QDialog>

#include "DemoViewer.h"
#include "ImageExport.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QSpinBox;

class CDemoPathSaveDialog : public QDialog
{
	Q_OBJECT
public:
	explicit CDemoPathSaveDialog( QWidget *pParent = nullptr );

	[[nodiscard]] DemoPathPicture_t GetPicture() const;
	[[nodiscard]] ImageExportOptions_t GetOptions() const;
	// The choices kept for next time.
	void Remember() const;

private:
	void UpdateEnabled();

	QComboBox *m_pFormat = nullptr;
	QCheckBox *m_pLossless = nullptr;
	QSlider *m_pQuality = nullptr;
	QSpinBox *m_pQualityValue = nullptr;
	QLabel *m_pEffortLabel = nullptr;
	QSlider *m_pEffort = nullptr;
	QLabel *m_pEffortValue = nullptr;
	QComboBox *m_pScale = nullptr;
	QCheckBox *m_pMap = nullptr;
	QCheckBox *m_pTransparent = nullptr;
	QComboBox *m_pPlayed = nullptr;
	QCheckBox *m_pPlayers = nullptr;
};

// Asks what and where, then saves the paths view's picture; false when cancelled or failed
// (a failure is said in a message box).
bool SaveDemoPathPicture( CDemoPathView &view, QWidget *pParent, const QString &szName );
