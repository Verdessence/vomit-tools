//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The Screen Effect panel (M16): a screenspace_general material over the 3D views, as
//          the game draws an env_screenoverlay's or a post effect: picked from those the map
//          names (env_screenoverlay's OverlayName keys, r_screenoverlay commands) or from the
//          texture browser, shown or not, with how it is drawn (its pixel shader, on the card or
//          in software, or why not) and its constants c0 to c3 to try other values in.
//
//===========================================================================//
#pragma once

#include <array>

#include <QStringList>
#include <QWidget>

class CMapEditor;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QToolButton;

class CMapScreenEffectPanel : public QWidget
{
	Q_OBJECT

public:
	explicit CMapScreenEffectPanel( CMapEditor &editor, QWidget *pParent = nullptr );

	// The materials the map names for the screen listed again (after an edit or another map).
	void Refresh();
	// The screen effect materials the map names, in the order found.
	[[nodiscard]] QStringList GetNamed() const { return m_Named; }

private:
	void EffectChanged();
	void ConstantsEdited();

	CMapEditor &m_Editor;
	QComboBox *m_pMaterial = nullptr;
	QToolButton *m_pBrowse = nullptr;
	QCheckBox *m_pShown = nullptr;
	QLabel *m_pStatus = nullptr;
	std::array<std::array<QDoubleSpinBox *, 4>, 4> m_Constants{};
	QPushButton *m_pReset = nullptr;
	QStringList m_Named;
	bool m_bFilling = false;
};
