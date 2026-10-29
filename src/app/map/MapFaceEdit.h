//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's Face Edit sheet, shown in the side panel while the Texture Application
//          tool is on: the selected faces' texture scale, shift, rotation and lightmap scale
//          (typed, and applied as typed), Justify, Align, the current texture with Browse and
//          Apply, and the Mode a click in 3D works in. Its Displacement tab, as Hammer's:
//          Create, Destroy, Sew, Subdivide, Noise, Invert Alpha, the power and elevation, and
//          the paint brush the 3D views paint geometry or alpha with.
//
//===========================================================================//
#pragma once

#include <optional>

#include <QWidget>

#include "map/DispEdit.h"
#include "map/Faces.h"

class CMapEditor;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSpinBox;
class QTabWidget;

// What a left click (or drag) on a displacement does while the Displacement tab paints.
enum class DispPaintMode_t
{
	DISP_PAINT_NONE, // the texture tool's own click
	DISP_PAINT_GEOMETRY,
	DISP_PAINT_ALPHA,
};

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

	// The Displacement tab's brush: what painting does, the brush without its centre (the view
	// puts it where the mouse is), whether its axis is the face's normal (else the brush's axis
	// is a world axis), and its radius ([ and ] in the 3D views).
	[[nodiscard]] DispPaintMode_t GetDispPaintMode() const;
	void SetDispPaintMode( DispPaintMode_t nMode );
	[[nodiscard]] xv::map::DispBrush_t GetBrush() const;
	[[nodiscard]] bool IsBrushAlongFaceNormal() const;
	[[nodiscard]] double GetBrushRadius() const;
	void SetBrushRadius( double flRadius );
	[[nodiscard]] int GetDispPower() const;
	void ShowTab( int nTab );
	// Alt+right-click on a face: its normal is the brush's axis for every displacement.
	void SetPaintNormal( const xv::map::Point_t &normal );
	[[nodiscard]] bool IsAutosew() const;
	// The Masks: the selected displacements drawn without their grid, or without their shading.
	[[nodiscard]] bool IsGridMasked() const;
	[[nodiscard]] bool IsSelectMasked() const;

signals:
	// What a click in 3D does changed, or how selected displacements show: the views redraw.
	void PaintModeChanged();

private:
	void BuildDisplacementTab( QWidget *pPage );
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
	QTabWidget *m_pTabs = nullptr;
	QSpinBox *m_pPower = nullptr;
	QDoubleSpinBox *m_pElevation = nullptr;
	QButtonGroup *m_pPaintModes = nullptr;
	QComboBox *m_pEffect = nullptr;
	QComboBox *m_pAxis = nullptr;
	QDoubleSpinBox *m_pRadius = nullptr;
	QDoubleSpinBox *m_pAmount = nullptr;     // Paint Geometry's Distance
	QDoubleSpinBox *m_pAlphaValue = nullptr; // Paint Alpha's Value
	QLabel *m_pAmountLabel = nullptr;
	QCheckBox *m_pSoftEdge = nullptr;
	QCheckBox *m_pAutosew = nullptr;
	std::optional<xv::map::Point_t> m_PaintNormal;
	QDoubleSpinBox *m_pNoiseMin = nullptr;
	QDoubleSpinBox *m_pNoiseMax = nullptr;
	QDoubleSpinBox *m_pScale = nullptr;
	QCheckBox *m_pNoPhysics = nullptr;
	QCheckBox *m_pNoHull = nullptr;
	QCheckBox *m_pNoRay = nullptr;
	QCheckBox *m_pGridMask = nullptr;
	QCheckBox *m_pSelectMask = nullptr;
};
