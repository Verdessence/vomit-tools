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
#include <string>
#include <vector>

#include <QDialog>

class CHammerSetupPage;
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
	// Keeps what was changed (the Hammer section's entry saved too, if edited); false when the
	// entry couldn't be written.
	bool Apply();
	// Puts back what was kept at opening or the last Apply, and closes.
	void reject() override;

protected:
	// What Cancel puts back is read again each time the window comes up, so a setting changed
	// elsewhere meanwhile isn't undone.
	void showEvent( QShowEvent *pEvent ) override;

private:
	void AddPage( const QString &szTitle, QWidget *pPage );
	[[nodiscard]] static QByteArray ReadSettingsFile();
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
	QByteArray m_Kept; // vomit-tools.nut as it was at opening or the last Apply
};
