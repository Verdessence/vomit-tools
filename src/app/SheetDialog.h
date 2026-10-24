//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The sprite sheet editor, mksheet's job with a window: sequences of frames (each a
//          picture, or two for multi-texturing), whether each loops and how long each frame
//          lasts, packed into one texture with the sheet resource particles read. Reads and
//          writes .mks files, and can start from a texture's own sheet by cutting its frames out.
//
//===========================================================================//
#pragma once

#include <map>
#include <string>
#include <vector>

#include <QDialog>

#include "texture/Build.h"
#include "texture/Sheet.h"

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QTreeWidget;

class CSheetDialog : public QDialog
{
	Q_OBJECT

public:
	// vtf, when given, is the texture being edited: its sheet can be cut out to start from.
	explicit CSheetDialog( const xv::texture::CVtf *pVtf, QWidget *pParent = nullptr );

	// The texture: the packed pictures with the sheet resource. Empty, with the reason, when it
	// can't be made.
	[[nodiscard]] std::vector<std::byte> BuildTexture( std::string *pError = nullptr ) const;

	// For tests and the buttons.
	bool LoadMks( const QString &szPath );
	bool SaveMks( const QString &szPath ) const;
	void AddFrames( const QStringList &files );
	void StartFromTexture();
	[[nodiscard]] const xv::texture::CMks &GetMks() const { return m_Mks; }
	[[nodiscard]] QImage GetPreview() const;

private:
	void FillTree();
	void UpdatePreview();
	// The sequence (and frame, or -1) the tree has picked.
	[[nodiscard]] std::pair<int, int> Picked() const;
	void EditPicked();

	const xv::texture::CVtf *m_pVtf;
	xv::texture::CMks m_Mks;
	std::map<std::string, xv::texture::CImage> m_Pictures; // by the name the .mks uses
	xv::texture::BuildOptions_t m_Options;
	QTreeWidget *m_pTree = nullptr;
	QSpinBox *m_pNumber = nullptr;
	QCheckBox *m_pLoop = nullptr;
	QDoubleSpinBox *m_pDuration = nullptr;
	QSpinBox *m_pPadding = nullptr;
	QLabel *m_pPreview = nullptr;
	QLabel *m_pNotes = nullptr;
	QLabel *m_pSettings = nullptr;
	bool m_bFilling = false;
};
