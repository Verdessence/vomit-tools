//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Edit > Preferences: the app's settings, a page each for the interface, graphics and
//          the particle editor. Changes take effect at once, except the graphics API, which
//          needs the app started again.
//
//===========================================================================//
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <QDialog>

#include "Preferences.h"

class CHammerSetupPage;
class CMapKeysPage;
class QListWidget;
class QStackedWidget;
class QTabWidget;

// Adds a page to Preferences from code a local build brings in (a folder kept out of the
// repository); pfnMake gives the page, or null to leave it out.
void AddLocalPreferencesPage( const QString &szTitle, std::function<QWidget *()> pfnMake );

class CPreferencesDialog : public QDialog
{
	Q_OBJECT

public:
	explicit CPreferencesDialog( QWidget *pParent = nullptr );

	// Turns to a page by its title ("Graphics").
	void ShowPage( const QString &szTitle );
	// The open game, for the Hammer section in Maps > General (an empty folder: none).
	void SetGame( const std::filesystem::path &gameDir, const QString &szGameName, const std::function<std::vector<std::string>()> &pfnMaterials );
	[[nodiscard]] CHammerSetupPage *GetHammerPage() const;
	// The changes made since the last Apply take effect (the Hammer section's entry is saved
	// too, if edited); false when the entry couldn't be written.
	bool Apply();
	[[nodiscard]] bool HasChanges() const;
	// Drops the changes not applied, and closes.
	void reject() override;

private:
	void AddPage( const QString &szTitle, QWidget *pPage );
	// A change kept until Apply; a key (the setting's) replaces the one waiting for it.
	void Stage( const QString &szKey, std::function<void()> pfnApply );
	[[nodiscard]] QWidget *BuildInterfacePage();
	[[nodiscard]] QWidget *BuildGraphicsPage();
	[[nodiscard]] QWidget *BuildMaterialsPage();
	[[nodiscard]] QWidget *BuildTextEditorPage();
	[[nodiscard]] QWidget *BuildMapsPage();
	[[nodiscard]] QWidget *BuildAdvancedPage();
	[[nodiscard]] QWidget *BuildSourceToolsPage();
	void BuildMapSections();
	void ResetMapSettings();

	QListWidget *m_pPages = nullptr;
	QTabWidget *m_pInterfaceTabs = nullptr;
	QTabWidget *m_pGraphicsTabs = nullptr;
	QTabWidget *m_pMapTabs = nullptr;
	QStackedWidget *m_pStack = nullptr;
	CHammerSetupPage *m_pHammer = nullptr;
	CMapKeysPage *m_pKeys = nullptr;
	std::vector<std::pair<QString, std::function<void()>>> m_Staged; // waiting for Apply, in order
	int m_nMapEdits = 0;                                             // numbers the map's edits' keys
	std::optional<MapPreferences_t> m_MapShown;                      // a restore's settings, before Apply
};
