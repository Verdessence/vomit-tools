//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Tools > Building Generator: a template (stairs, spiral stairs, Hammer's arch, a room,
//          a wall with windows, a railing), its numbers and the box it fills, with the brushes
//          it would make drawn from above and from the front as the numbers change.
//
//===========================================================================//
#pragma once

#include <array>
#include <vector>

#include <QDialog>

#include "map/Brushes.h"
#include "map/Generator.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QStackedWidget;

// The brushes' outlines from above (x, y) and from the front (x, z), side by side.
class CBuildingPreview : public QWidget
{
	Q_OBJECT

public:
	explicit CBuildingPreview( QWidget *pParent = nullptr );
	void SetShapes( std::vector<xv::map::SolidGeometry_t> shapes, const xv::map::Box_t &box );

protected:
	void paintEvent( QPaintEvent *pEvent ) override;

private:
	std::vector<xv::map::SolidGeometry_t> m_Shapes;
	xv::map::Box_t m_Box;
};

class CMapBuildingDialog : public QDialog
{
	Q_OBJECT

public:
	CMapBuildingDialog( const xv::map::Box_t &box, QWidget *pParent = nullptr );

	[[nodiscard]] xv::map::BuildingKind_t GetKind() const;
	[[nodiscard]] xv::map::Box_t GetBox() const;
	[[nodiscard]] xv::map::BuildingParams_t GetParams() const;

private:
	void Update();

	QComboBox *m_pKind = nullptr;
	QStackedWidget *m_pPages = nullptr;
	std::array<QDoubleSpinBox *, 3> m_Mins{};
	std::array<QDoubleSpinBox *, 3> m_Maxs{};
	CBuildingPreview *m_pPreview = nullptr;
	QLabel *m_pProblem = nullptr;
	QPushButton *m_pOk = nullptr;
	// The pages' fields.
	QSpinBox *m_pSteps = nullptr;
	QComboBox *m_pDirection = nullptr;
	QCheckBox *m_pSolidUnder = nullptr;
	QSpinBox *m_pSpiralSteps = nullptr;
	QDoubleSpinBox *m_pDegreesPerStep = nullptr;
	QDoubleSpinBox *m_pInnerRadius = nullptr;
	QDoubleSpinBox *m_pSpiralStart = nullptr;
	QCheckBox *m_pSpiralSolidUnder = nullptr;
	QDoubleSpinBox *m_pWallWidth = nullptr;
	QSpinBox *m_pSides = nullptr;
	QDoubleSpinBox *m_pArc = nullptr;
	QDoubleSpinBox *m_pStartAngle = nullptr;
	QDoubleSpinBox *m_pAddHeight = nullptr;
	QDoubleSpinBox *m_pThickness = nullptr;
	std::array<QCheckBox *, 4> m_Doors{};
	QDoubleSpinBox *m_pDoorWidth = nullptr;
	QDoubleSpinBox *m_pDoorHeight = nullptr;
	QSpinBox *m_pColumns = nullptr;
	QSpinBox *m_pRows = nullptr;
	QDoubleSpinBox *m_pWindowWidth = nullptr;
	QDoubleSpinBox *m_pWindowHeight = nullptr;
	QDoubleSpinBox *m_pSill = nullptr;
	QDoubleSpinBox *m_pPostSize = nullptr;
	QDoubleSpinBox *m_pPostSpacing = nullptr;
	QDoubleSpinBox *m_pRailHeight = nullptr;
};
