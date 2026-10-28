//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's small editing dialogs: Transform (Ctrl+M: turn, scale or move the selection
//          by typed amounts) and Paste Special (Ctrl+Shift+V: several copies, each stepped and
//          turned, grouped, with fresh names). Each answers with a value the editor applies, so
//          the editor's own methods can be driven without the window.
//
//===========================================================================//
#pragma once

#include <array>

#include <QDialog>

#include "map/Edit.h"
#include "map/Geometry.h"

class QButtonGroup;
class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;

// Hammer's Transform dialog: the mode and three values (degrees, factors or units).
enum class TransformMode_t
{
	TRANSFORM_MODE_ROTATE,
	TRANSFORM_MODE_SCALE,
	TRANSFORM_MODE_MOVE,
};

struct TransformValues_t
{
	TransformMode_t m_nMode = TransformMode_t::TRANSFORM_MODE_ROTATE;
	std::array<double, 3> m_Values{};
};

class CMapTransformDialog : public QDialog
{
	Q_OBJECT

public:
	explicit CMapTransformDialog( QWidget *pParent = nullptr );

	[[nodiscard]] TransformValues_t GetValues() const;
	void SetValues( const TransformValues_t &values );

private:
	void FillDefaults();

	QButtonGroup *m_pModes = nullptr;
	std::array<QDoubleSpinBox *, 3> m_Values{};
};

// Hammer's Paste Special dialog; the start is a choice here (where the original was, or the
// 2D view's centre), which the editor turns into a point.
class CMapPasteSpecialDialog : public QDialog
{
	Q_OBJECT

public:
	explicit CMapPasteSpecialDialog( QWidget *pParent = nullptr );

	// m_Start left empty; IsStartAtView says whether the editor should fill it in.
	[[nodiscard]] xv::map::PasteSpecial_t GetValues() const;
	[[nodiscard]] bool IsStartAtView() const;
	void SetValues( const xv::map::PasteSpecial_t &how, bool bStartAtView );

private:
	QSpinBox *m_pCopies = nullptr;
	QButtonGroup *m_pStart = nullptr;
	std::array<QDoubleSpinBox *, 3> m_Offset{};
	std::array<QDoubleSpinBox *, 3> m_Angles{};
	QCheckBox *m_pGroup = nullptr;
	QCheckBox *m_pUniqueNames = nullptr;
};
