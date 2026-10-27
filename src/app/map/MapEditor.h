//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The map viewer's tab, laid out as Hammer is: the tool bar down the left, the four
//          view panes, the object panels on the right and Hammer's status bar along the
//          bottom. It views a map; nothing is changed or saved.
//
//===========================================================================//
#pragma once

#include <array>
#include <vector>

#include <QWidget>

#include "EditorTab.h"
#include "map/MapDocument.h"
#include "map/MapView.h"

class CMapPane;
class CMapQuadSplitter;
class QActionGroup;
class QLabel;
class QToolBar;
class QVBoxLayout;

class CMapEditor : public QWidget, public IEditorTab
{
	Q_OBJECT

public:
	// szPath is the file on disk, or its path in the game when bFromGame.
	CMapEditor( const QString &szPath, bool bFromGame, QWidget *pParent = nullptr );
	~CMapEditor() override;

	void Load( CMapDocument::Source_t source );
	[[nodiscard]] CMapDocument &GetDocument() { return *m_pDocument; }
	[[nodiscard]] const CMapDocument &GetDocument() const { return *m_pDocument; }
	[[nodiscard]] const QString &GetPath() const { return m_szPath; }
	[[nodiscard]] const MapViewSettings_t &GetSettings() const { return m_Settings; }
	[[nodiscard]] MapTool_t GetTool() const { return m_nTool; }
	void SetTool( MapTool_t nTool );
	[[nodiscard]] CMapPane *GetPane( int nIndex ) const;
	[[nodiscard]] std::vector<CMapView *> GetViews() const;
	[[nodiscard]] CMapView *GetActiveView() const { return m_pActiveView; }

	// What the views report and ask for.
	bool RunAction( MapAction_t nAction, CMapView *pFrom );
	void SetActiveView( CMapView *pView );
	void ViewTypeChanged( CMapView *pView );
	void ShowCursor( const QString &szAt );
	void ShowZoom( double flZoom );

	// IEditorTab
	[[nodiscard]] QString GetTabTitle() const override;
	[[nodiscard]] QString GetSessionEntry() const override;
	[[nodiscard]] bool IsFromGame() const override { return m_bFromGame; }
	void StopWork() override;
	void GameChanged( const xv::vfs::CGameInfo *pGame ) override;

signals:
	void StatusMessage( const QString &szText );

private:
	void BuildToolBars( QVBoxLayout *pLayout );
	void Loaded();
	void SettingsChanged();
	void UpdateStatus();
	void GoToBrush();
	[[nodiscard]] CMapPane *PaneOf( const CMapView *pView ) const;

	CMapDocument *m_pDocument = nullptr;
	QString m_szPath;
	bool m_bFromGame = false;
	MapViewSettings_t m_Settings;
	MapTool_t m_nTool = MapTool_t::MAP_TOOL_SELECT;
	CMapView *m_pActiveView = nullptr;

	std::array<CMapPane *, 4> m_Panes{};
	CMapQuadSplitter *m_pSplitter = nullptr;
	QWidget *m_pSidePanel = nullptr;
	QLabel *m_pLoading = nullptr;
	QActionGroup *m_pTools = nullptr;
	QActionGroup *m_pModes = nullptr;
	QLabel *m_pStatusSelection = nullptr;
	QLabel *m_pStatusSize = nullptr;
	QLabel *m_pStatusCursor = nullptr;
	QLabel *m_pStatusZoom = nullptr;
	QLabel *m_pStatusGrid = nullptr;
	QLabel *m_pStatusGame = nullptr;
};
