//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Edit > Preferences: the app's settings, a page each for the interface, graphics and
//          the particle editor. Changes take effect at once, except the graphics API, which
//          needs the app started again.
//
//===========================================================================//
#pragma once

#include <functional>

#include <QDialog>

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

private:
	void AddPage( const QString &szTitle, QWidget *pPage );
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
};
