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
#include <memory>
#include <optional>
#include <vector>

#include <QWidget>

#include "EditorTab.h"
#include "map/MapDocument.h"
#include "map/MapRenderData.h"
#include "map/MapView.h"

class CMapFilterControl;
class CMapPane;
class CMapProperties;
class CMapResources;
class CMapQuadSplitter;
class QAction;
class QActionGroup;
class QLabel;
class QSplitter;
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
	[[nodiscard]] CMapRenderData &GetRenderData() { return *m_pRenderData; }
	[[nodiscard]] CMapResources &GetResources() { return *m_pResources; }
	[[nodiscard]] CMapFilterControl &GetFilterControl() { return *m_pFilter; }
	[[nodiscard]] CMapProperties &GetProperties() { return *m_pProperties; }
	// Where the 3D camera is, for the 2D views to draw it (the first 3D pane's).
	[[nodiscard]] std::optional<xv::map::CViewCamera> GetCamera() const;
	// Where the camera was when this map was last closed, unless the file was saved since
	// (Hammer then wrote its own camera into it); the 3D views start there.
	[[nodiscard]] const std::optional<xv::map::CViewCamera> &GetRememberedCamera() const { return m_RememberedCamera; }

	// What the views report and ask for.
	bool RunAction( MapAction_t nAction, CMapView *pFrom );
	void SetActiveView( CMapView *pView );
	void ViewTypeChanged( CMapView *pView );
	void ShowCursor( const QString &szAt );
	void ShowZoom( double flZoom );
	void CameraMoved( CMapView *pView );
	// A 3D view changed how it draws (the card's drawing failed): software needs the textures decoded.
	void RendererChanged();

	// IEditorTab
	[[nodiscard]] QString GetTabTitle() const override;
	[[nodiscard]] QString GetSessionEntry() const override;
	[[nodiscard]] bool IsFromGame() const override { return m_bFromGame; }
	void StopWork() override;
	void GameChanged( const xv::vfs::CGameInfo *pGame ) override;
	[[nodiscard]] QUndoStack *GetUndoStack() const override;
	bool ConfirmClose() override;
	bool Save() override;
	bool SaveAs() override;

signals:
	void StatusMessage( const QString &szText );
	// The tab's title changed (saved under another name, or modified).
	void TitleChanged();

private:
	void BuildToolBars( QVBoxLayout *pLayout );
	void Loaded();
	void SettingsChanged();
	void UpdateStatus();
	void UpdateGameStatus();
	void ResourcesArrived();
	// An edit made a new scene: the views follow, and materials it brought in are read.
	void SceneEdited( const std::vector<std::uint32_t> &changed );
	void GoToBrush();
	void ShowProperties();
	void TakePreferences();
	[[nodiscard]] bool NeedsDecodedTextures() const;
	void StartResources();
	// The camera kept between runs, per map file.
	void RememberCamera() const;
	[[nodiscard]] std::optional<xv::map::CViewCamera> RecallCamera() const;
	[[nodiscard]] QString GetCameraKey() const;
	void PreferencesChanged();
	[[nodiscard]] CMapPane *PaneOf( const CMapView *pView ) const;

	CMapDocument *m_pDocument = nullptr;
	std::unique_ptr<CMapRenderData> m_pRenderData;
	CMapResources *m_pResources = nullptr;
	QString m_szPath;
	bool m_bFromGame = false;
	MapViewSettings_t m_Settings;
	MapTool_t m_nTool = MapTool_t::MAP_TOOL_SELECT;
	std::optional<xv::map::CViewCamera> m_RememberedCamera;
	CMapView *m_pActiveView = nullptr;

	std::array<CMapPane *, 4> m_Panes{};
	CMapQuadSplitter *m_pSplitter = nullptr;
	std::vector<std::pair<QAction *, const bool *>> m_SettingActions; // toggles and what they show
	int m_nTextureSize = 1024;
	std::uint32_t m_nMaterialsRead = 0; // the scene's materials the resources were given
	QSplitter *m_pSidePanel = nullptr;
	CMapFilterControl *m_pFilter = nullptr;
	CMapProperties *m_pProperties = nullptr;
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
