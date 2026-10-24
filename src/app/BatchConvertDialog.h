//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Converting a whole folder: every picture into a VTF with one set of choices, or every
//          VTF into pictures, keeping the sub-folders in the output folder.
//
//===========================================================================//
#pragma once

#include <functional>

#include <QDialog>
#include <QStringList>

#include "texture/Build.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

struct BatchJob_t
{
	QString m_szInput;
	QString m_szOutput;
	bool m_bRecurse = true;
	bool m_bToVtf = true;               // pictures into VTFs; else VTFs into pictures
	QString m_szExtension = QStringLiteral( "png" );// the pictures' type when making pictures
	xv::texture::BuildOptions_t m_Options; // when making VTFs; an animated GIF becomes frames
	// Pictures with see-through parts are made in m_nAlphaFormat when the format has no alpha.
	bool m_bAlphaFormat = true;
	xv::texture::VtfFormat_t m_nAlphaFormat = xv::texture::VtfFormat_t::DXT5;
};

struct BatchResult_t
{
	int m_nDone = 0;
	QStringList m_Failures; // "name: why"
};

// Runs a job. pfnProgress( done, total ) is told as it goes and returns false to stop.
[[nodiscard]] BatchResult_t RunBatch( const BatchJob_t &job, const std::function<bool( int, int )> &pfnProgress = {} );

class CBatchConvertDialog : public QDialog
{
	Q_OBJECT

public:
	// szStart is the folder offered first.
	explicit CBatchConvertDialog( const QString &szStart, QWidget *pParent = nullptr );

	[[nodiscard]] BatchJob_t GetJob() const;

private:
	void Update();

	QLineEdit *m_pInput = nullptr;
	QLineEdit *m_pOutput = nullptr;
	QComboBox *m_pDirection = nullptr;
	QComboBox *m_pExtension = nullptr;
	QCheckBox *m_pRecurse = nullptr;
	QLabel *m_pSettings = nullptr;
	QCheckBox *m_pAlphaFormat = nullptr;
	xv::texture::BuildOptions_t m_Options;
};
