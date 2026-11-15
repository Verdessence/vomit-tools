//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The main window's tabs in groups: a strip over the tabs picks a group (maps,
//          textures, sounds... by default, or groups of the user's own), and the tabs show only
//          that group's. A tab moved into a group of the user's stays there across starts.
//          With editor windows on (Preferences), each kind of editor is a window of its own
//          instead, its tabs in it, the main window keeping the preview.
//
//===========================================================================//
#pragma once

#include <functional>
#include <vector>

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QStringList>
#include <QTabWidget>

class QMenuBar;
class QTabBar;
class CEditorWindow;

// The main window's tabs, which say when they come and go. Windowed (SetWindowed), each kind
// of editor's tabs are in a window of their own (CEditorWindow); the functions below, which
// stand in for QTabWidget's of the same names, then count every window's tabs as one list:
// the main window's first (the preview), then each window's in turn. The current tab is the
// window last used's.
class CEditorTabWidget : public QTabWidget
{
	Q_OBJECT

public:
	using QTabWidget::QTabWidget;
	~CEditorTabWidget() override;

	[[nodiscard]] int count() const;
	[[nodiscard]] QWidget *widget( int nIndex ) const;
	[[nodiscard]] int indexOf( const QWidget *pWidget ) const;
	[[nodiscard]] QWidget *currentWidget() const;
	[[nodiscard]] int currentIndex() const;
	void setCurrentWidget( QWidget *pWidget );
	void setCurrentIndex( int nIndex );
	int addTab( QWidget *pWidget, const QString &szText );
	void removeTab( int nIndex );
	[[nodiscard]] QString tabText( int nIndex ) const;
	void setTabText( int nIndex, const QString &szText );
	[[nodiscard]] QString tabToolTip( int nIndex ) const;
	void setTabToolTip( int nIndex, const QString &szTip );
	[[nodiscard]] bool isTabVisible( int nIndex ) const;
	void setTabVisible( int nIndex, bool bVisible );
	void setTabEnabled( int nIndex, bool bEnabled );

	// Each kind's tabs in a window of its own, or all back in this one. The first tab (the
	// preview) stays here.
	void SetWindowed( bool bWindowed );
	[[nodiscard]] bool IsWindowed() const { return m_bWindowed; }
	// The menus every editor window shows: the main window's own.
	void SetMenuSource( QMenuBar *pMenus );
	// The window a tab is in: its kind's, or the main window.
	[[nodiscard]] QWidget *GetWindowOf( const QWidget *pWidget ) const;
	[[nodiscard]] std::vector<CEditorWindow *> GetWindows() const;
	// Every editor window put away without asking (the main window has asked already).
	void CloseWindows();

signals:
	void TabsChanged();

protected:
	void tabInserted( int nIndex ) override;
	void tabRemoved( int nIndex ) override;
	bool eventFilter( QObject *pObject, QEvent *pEvent ) override;

private:
	struct Place_t
	{
		QTabWidget *m_pTabs = nullptr; // this one's own, or a window's
		int m_nIndex = -1;
	};
	[[nodiscard]] Place_t PlaceOf( int nIndex ) const;
	[[nodiscard]] QTabWidget *Own() { return this; }
	[[nodiscard]] const QTabWidget *Own() const { return this; }
	// The window for a kind of editor, made the first time.
	CEditorWindow *WindowFor( const QString &szKind );
	// Moves a tab from one tab widget to another, as it was (text, tip, icon).
	static void MoveTab( QTabWidget *pFrom, int nIndex, QTabWidget *pTo );
	// The window in use changed, or its current tab: the current tab did.
	void Activated( QTabWidget *pTabs );
	void WindowCurrentChanged( CEditorWindow *pWindow );

	bool m_bWindowed = false;
	bool m_bMoving = false;
	std::vector<QPointer<CEditorWindow>> m_Windows; // in the order they were made
	QPointer<QTabWidget> m_pActive;                 // the window last used's tabs; null: this one
	QPointer<QMenuBar> m_pMenuSource;
};

// A kind of editor's window: its tabs, the main window's menus, its own place on screen.
// Closing it closes its tabs (each asking about unsaved changes).
class CEditorWindow : public QMainWindow
{
	Q_OBJECT

public:
	CEditorWindow( const QString &szKind, QWidget *pParent );

	[[nodiscard]] const QString &GetKind() const { return m_szKind; }
	[[nodiscard]] QTabWidget *GetTabs() const { return m_pTabs; }
	// The menus as the main window has them, the same actions.
	void TakeMenus( QMenuBar *pMenus );
	// Closing without asking about the tabs: the main window has.
	void SetClosingQuietly( bool bQuiet ) { m_bQuiet = bQuiet; }

signals:
	void Activated();
	void TabCloseRequested( QWidget *pWidget );
	void TabsChanged();

protected:
	void changeEvent( QEvent *pEvent ) override;
	void closeEvent( QCloseEvent *pEvent ) override;

private:
	QString m_szKind;
	QTabWidget *m_pTabs = nullptr;
	bool m_bQuiet = false;
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
