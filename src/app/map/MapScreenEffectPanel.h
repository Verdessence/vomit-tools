//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The Screenspace Shaders panel (M16): a screenspace_general material over the 3D
//          views, as the game draws an env_screenoverlay's or a post effect. It is picked from
//          the map's own (env_screenoverlay's OverlayName keys, r_screenoverlay commands,
//          material_modify_control targets), from a list of every such material in the game
//          with the shader each names (or by shader: every .vcs and the materials using it), or
//          from the texture browser; under it, the .vcs in use. Enabled shows it; c0 to c3 can
//          be held at other values; the statistics show what the shader sees now (its constants
//          as its proxies move them), what it reads, its size and what it costs in software, and
//          for Verd's screenspace_font_line its constants unpacked.
//
//===========================================================================//
#pragma once

#include <array>
#include <memory>

#include <QStringList>
#include <QWidget>

#include "map/ScreenShaderCatalog.h"

class CMapEditor;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

class CMapScreenEffectPanel : public QWidget
{
	Q_OBJECT

public:
	explicit CMapScreenEffectPanel( CMapEditor &editor, QWidget *pParent = nullptr );

	// The materials the map names for the screen listed again (after an edit or another map).
	void Refresh();
	// The screen effect materials the map names, in the order found.
	[[nodiscard]] QStringList GetNamed() const { return m_Named; }
	// The game's catalog as the lists show it (null while it is read), and the statistics' text.
	[[nodiscard]] const std::shared_ptr<const ScreenCatalog_t> &GetCatalog() const { return m_pCatalog; }
	void SetCatalog( std::shared_ptr<const ScreenCatalog_t> pCatalog );
	[[nodiscard]] QString GetStatistics() const;
	// The statistics worked out again (the timer does it while they are open).
	void UpdateStatistics();

private:
	void EffectChanged();
	void ConstantsEdited();
	void RequestCatalog();
	// The two lists filled from the catalog and the map's own, as the filter lets through.
	void FillLists();
	void ItemPicked( QTreeWidgetItem *pItem );
	// What running the shader in software costs, measured once a shader and its constants.
	void MeasureSoftware();

	CMapEditor &m_Editor;
	QComboBox *m_pMaterial = nullptr;
	QToolButton *m_pBrowse = nullptr;
	QCheckBox *m_pShown = nullptr;
	QLabel *m_pStatus = nullptr;
	QLineEdit *m_pFilter = nullptr;
	QTabWidget *m_pTabs = nullptr;
	QTreeWidget *m_pMaterials = nullptr;
	QTreeWidget *m_pShaders = nullptr;
	std::array<std::array<QDoubleSpinBox *, 4>, 4> m_Constants{};
	QPushButton *m_pReset = nullptr;
	QLabel *m_pStatistics = nullptr;
	QTimer *m_pStatisticsTimer = nullptr;
	QStringList m_Named;
	std::shared_ptr<const ScreenCatalog_t> m_pCatalog;
	bool m_bCatalogAsked = false;
	// The software cost, and the material and shader it was measured for (constants held
	// here make a new effect each time, but cost the same).
	double m_flSoftwareMs = -1.0;
	QString m_szMeasured;
	bool m_bFilling = false;
};
