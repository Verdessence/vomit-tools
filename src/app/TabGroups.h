//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The main window's tabs in groups: a strip over the tabs picks a group (maps,
//          textures, sounds... by default, or groups of the user's own), and the tabs show only
//          that group's. A tab moved into a group of the user's stays there across starts.
//
//===========================================================================//
#pragma once

#include <functional>

#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QTabWidget>

class QTabBar;

// A QTabWidget that says when tabs come and go.
class CEditorTabWidget : public QTabWidget
{
	Q_OBJECT

public:
	using QTabWidget::QTabWidget;

signals:
	void TabsChanged();

protected:
	void tabInserted( int nIndex ) override;
	void tabRemoved( int nIndex ) override;
};

class CTabGroups : public QObject
{
	Q_OBJECT

public:
	// The first tab (the preview) belongs to every group.
	explicit CTabGroups( CEditorTabWidget *pTabs );

	// The strip of groups, to place above the tabs; hidden while there is one group or grouping is off.
	[[nodiscard]] QTabBar *GetBar() const { return m_pBar; }
	// A tab's kind ("Maps", "Textures"...), from its editor's class.
	[[nodiscard]] static QString KindOf( const QWidget *pTab );
	// The editor a widget sits in, as vomit_tools.conf names editors ("map", "sound"...), or
	// empty outside one.
	[[nodiscard]] static QString EditorNameOf( const QWidget *pWidget );
	// Its group: the user's when it has one, else its kind.
	[[nodiscard]] QString GroupOf( const QWidget *pTab ) const;
	// Puts a tab in a group of the user's (empty: back to its kind's).
	void SetUserGroup( QWidget *pTab, const QString &szGroup );
	[[nodiscard]] QStringList GetUserGroups() const;
	// The groups there are now, in the strip's order (after "All").
	[[nodiscard]] QStringList GetGroups() const;
	[[nodiscard]] QString GetCurrentGroup() const { return m_szCurrent; }
	// Shows a group's tabs ("All": every tab), and its last tab used.
	void ShowGroup( const QString &szGroup );
	// Grouping off shows every tab and no strip.
	void SetEnabled( bool bEnabled );
	[[nodiscard]] bool IsEnabled() const { return m_bEnabled; }
	// The menu a tab's right-click shows: its group to change.
	void ShowTabMenu( int nIndex, const QPoint &globalAt );
	// What names a tab across starts (its session entry; empty: not kept), for user groups.
	void SetKeyFunction( std::function<QString( QWidget * )> pfnKey ) { m_pfnKey = std::move( pfnKey ); }

	static constexpr char ALL[] = "All";

private:
	void Refresh();
	void CurrentChanged();
	// The user's groups are kept by tab key; a tab that comes back joins its group again.
	void Remember( QWidget *pTab, const QString &szGroup );
	[[nodiscard]] QString Remembered( QWidget *pTab ) const;

	CEditorTabWidget *m_pTabs = nullptr;
	QTabBar *m_pBar = nullptr;
	bool m_bEnabled = true;
	bool m_bRefreshing = false;
	QString m_szCurrent = QString::fromLatin1( ALL );
	QHash<const QWidget *, QString> m_UserGroups;
	QHash<QString, QPointer<QWidget>> m_LastTab; // per group, the tab last shown in it
	QStringList m_UserGroupOrder;                // as they were made
	std::function<QString( QWidget * )> m_pfnKey;
};
