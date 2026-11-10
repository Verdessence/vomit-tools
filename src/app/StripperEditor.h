//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A Stripper:Source config in a tab of its own, opened by itself (any name, not only
//          through its map): its text, checked as it is typed, and what it does to its map's
//          entities when the map is found (beside it as Stripper finds configs, or picked).
//          Without a map, what it adds still shows.
//
//===========================================================================//
#pragma once

#include <filesystem>

#include <QWidget>

#include "EditorTab.h"
#include "bsp/Bsp.h"

class CEntityView;
class QLabel;
class QVBoxLayout;

class CStripperEditor : public QWidget, public IEditorTab
{
	Q_OBJECT

public:
	// map: the .bsp to preview on; empty for none.
	CStripperEditor( const std::filesystem::path &config, const std::filesystem::path &map, QWidget *pParent = nullptr );

	// Previews on another map (empty: none), keeping the text typed so far.
	void SetMap( const std::filesystem::path &map );
	void SetGameData( const QString &szText );
	[[nodiscard]] const std::filesystem::path &GetConfig() const { return m_Config; }
	[[nodiscard]] const std::filesystem::path &GetMap() const { return m_Map; }
	[[nodiscard]] CEntityView *GetView() const { return m_pView; }
	[[nodiscard]] bool IsModified() const;

	[[nodiscard]] QString GetTabTitle() const override;
	[[nodiscard]] QString GetSessionEntry() const override;
	[[nodiscard]] bool IsFromGame() const override { return m_bFromGame; }
	bool ConfirmClose() override;
	bool Save() override;
	bool SaveAs() override;

	// Whether a .cfg is a Stripper config: one under addons/stripper/, or one whose text has
	// Stripper's sections.
	[[nodiscard]] static bool IsStripperConfig( const std::filesystem::path &file, std::string_view szText );

signals:
	void ModifiedChanged( bool bModified );

private:
	void PickMap();

	bool m_bFromGame = false;
	QString m_szGameData;
	std::filesystem::path m_Config;
	std::filesystem::path m_Map;
	xv::bsp::CBsp m_Bsp; // the map's, or none
	CEntityView *m_pView = nullptr;
	QLabel *m_pMapLabel = nullptr;
	QVBoxLayout *m_pLayout = nullptr;
};
