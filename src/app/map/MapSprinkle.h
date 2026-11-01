//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The Sprinkle tool's settings (Hammer++'s Entity Sprinkle box): the type from the
//          definition files, the mode, the density, the grid, random yaw and the brush's radius.
//          The 3D view paints with them (map/Sprinkle.h does the placing).
//
//===========================================================================//
#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include <QWidget>

#include "map/Sprinkle.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSlider;

class CMapSprinklePanel : public QWidget
{
	Q_OBJECT

public:
	explicit CMapSprinklePanel( QWidget *pParent = nullptr );

	// Reads the definitions for the game in gameDir (none without a game), keeping the type picked
	// when it is still there.
	void Load( const std::optional<std::filesystem::path> &gameDir );
	[[nodiscard]] const xv::map::SprinkleType_t *GetType() const;
	// The settings as they are now; nSeed makes each stroke's rolls its own.
	[[nodiscard]] xv::map::SprinkleOptions_t GetOptions( std::uint32_t nSeed ) const;
	[[nodiscard]] double GetRadius() const;
	void SetRadius( double flRadius );
	// The editor's grid, for Use Grid.
	void SetEditorGrid( int nGrid ) { m_nEditorGrid = nGrid; }

private:
	void TypeChanged();

	std::vector<xv::map::SprinkleType_t> m_Types;
	std::optional<std::filesystem::path> m_GameDir;
	QComboBox *m_pType = nullptr;
	QComboBox *m_pMode = nullptr;
	QSlider *m_pDensity = nullptr;
	QLabel *m_pDensityValue = nullptr;
	QCheckBox *m_pDefinitionGrid = nullptr;
	QDoubleSpinBox *m_pGridX = nullptr;
	QDoubleSpinBox *m_pGridY = nullptr;
	QCheckBox *m_pRandomYaw = nullptr;
	QDoubleSpinBox *m_pOffset = nullptr;
	QDoubleSpinBox *m_pRadius = nullptr;
	QLabel *m_pNotes = nullptr;
	int m_nEditorGrid = 64;
};
