//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's Face Edit sheet, shown in the side panel while the Texture Application
//          tool is on: the selected faces' texture scale, shift, rotation and lightmap scale
//          (typed, and applied as typed), Justify, Align, the current texture with Browse and
//          Apply, and the Mode a click in 3D works in.
//
//===========================================================================//
#pragma once

#include <QWidget>

#include "map/Faces.h"

class CMapEditor;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QRadioButton;
class QSpinBox;

// What a left click on a face does (Hammer's Mode).
enum class FaceEditMode_t
{
	FACE_EDIT_MODE_LIFT_SELECT, // the face's texture and values into the sheet, and the face selected
	FACE_EDIT_MODE_LIFT,
	FACE_EDIT_MODE_SELECT,
	FACE_EDIT_MODE_APPLY,        // the current texture onto the face
	FACE_EDIT_MODE_APPLY_VALUES, // and the sheet's values with it
};

class CMapFaceEdit : public QWidget
{
	Q_OBJECT

public:
	explicit CMapFaceEdit( CMapEditor &editor, QWidget *pParent = nullptr );

	// The sheet from the selected faces (a field blank where they differ).
	void Refresh();
	[[nodiscard]] FaceEditMode_t GetMode() const;
	// The sheet's values from a face (Lift), and onto one (Apply with values).
	void LiftFrom( const xv::map::Side_t &side );
	void ApplyValuesTo( xv::map::Side_t &side ) const;
	void Justify( xv::map::Justify_t nHow );

private:
	// A field typed: the selected faces take it.
	void FieldChanged( const QString &szName, const std::function<void( xv::map::Side_t & )> &pfnSet );
	void Align( bool bWorld );

	CMapEditor &m_Editor;
	bool m_bFilling = false;
	QDoubleSpinBox *m_pScaleX = nullptr;
	QDoubleSpinBox *m_pScaleY = nullptr;
	QDoubleSpinBox *m_pShiftX = nullptr;
	QDoubleSpinBox *m_pShiftY = nullptr;
	QDoubleSpinBox *m_pRotation = nullptr;
	QSpinBox *m_pLightmap = nullptr;
	QCheckBox *m_pTreatAsOne = nullptr;
	QRadioButton *m_pAlignWorld = nullptr;
	QRadioButton *m_pAlignFace = nullptr;
	QLineEdit *m_pTexture = nullptr;
	QComboBox *m_pMode = nullptr;
};
