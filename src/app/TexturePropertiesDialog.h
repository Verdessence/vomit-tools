//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A texture's header changed without touching its pictures: version, first frame, bump
//          scale, reflectivity, the thumbnail and the extra resources. (The flags are ticked in
//          the texture editor's side panel.)
//
//===========================================================================//
#pragma once

#include <QDialog>

#include "texture/Vtf.h"

class QComboBox;
class QDoubleSpinBox;
class QListWidget;
class QSpinBox;
class QTreeWidget;

class CTexturePropertiesDialog : public QDialog
{
	Q_OBJECT

public:
	CTexturePropertiesDialog( const xv::texture::CVtf &vtf, QWidget *pParent = nullptr );

	[[nodiscard]] xv::texture::VtfHeaderEdit_t GetEdit() const;

	// For tests.
	[[nodiscard]] QComboBox *GetVersionBox() const { return m_pVersion; }

private:
	void FillResources();
	void SetResource( xv::texture::VtfExtraResource_t resource );
	void EditResource( std::array<std::uint8_t, 3> tag );

	const xv::texture::CVtf &m_Vtf;
	xv::texture::VtfHeaderEdit_t m_Initial;
	QComboBox *m_pVersion = nullptr;
	QSpinBox *m_pFirstFrame = nullptr;
	QDoubleSpinBox *m_pBumpScale = nullptr;
	QDoubleSpinBox *m_pReflectivity[ 3 ] = {};
	QComboBox *m_pLowRes = nullptr;
	QListWidget *m_pResources = nullptr;
	std::vector<xv::texture::VtfExtraResource_t> m_Resources; // as edited
};
