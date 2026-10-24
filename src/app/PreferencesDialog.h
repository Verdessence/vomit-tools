//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Edit > Preferences: the app's settings, a page each for the interface, graphics and
//          the particle editor. Changes take effect at once, except the graphics API, which
//          needs the app started again.
//
//===========================================================================//
#pragma once

#include <QDialog>

class QListWidget;
class QStackedWidget;

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
	[[nodiscard]] QWidget *BuildParticlesPage();

	QListWidget *m_pPages = nullptr;
	QStackedWidget *m_pStack = nullptr;
};
