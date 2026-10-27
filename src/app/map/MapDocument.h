//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: One open map: read and built in the background, then edited through an undo stack
//          (each edit makes a new snapshot of the scene, map/Edit.h), saved by patching the text
//          it was read from, and what is shown, hidden and selected (the views' own state).
//
//===========================================================================//
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <QObject>
#include <QString>

#include "map/Edit.h"
#include "map/Scene.h"
#include "vfs/Vfs.h"

class QUndoStack;

// Selection picks whole groups, whole entities with their solids, or single solids (Hammer's
// selection modes).
enum class MapSelectMode_t
{
	MAP_SELECT_MODE_GROUPS,
	MAP_SELECT_MODE_OBJECTS,
	MAP_SELECT_MODE_SOLIDS,
};

class CMapDocument : public QObject
{
	Q_OBJECT

public:
	struct Source_t
	{
		QString m_szName;                         // the file's name, for messages
		std::vector<char> m_Text;                 // the VMF
		std::filesystem::path m_DiskPath;         // empty for a game file
		std::optional<xv::vfs::CGameInfo> m_Game; // what materials, models and FGDs come from
		std::vector<std::filesystem::path> m_Fgds; // empty: the ones the game's Hammer config names
	};

	explicit CMapDocument( QObject *pParent = nullptr );
	~CMapDocument() override;

	// Reads and builds the map on a thread of its own; Loaded or LoadFailed follows.
	void Load( Source_t source );
	void Stop();

	[[nodiscard]] bool IsLoaded() const { return m_pScene != nullptr; }
	[[nodiscard]] const xv::map::CScene *GetScene() const { return m_pScene.get(); }
	[[nodiscard]] std::shared_ptr<const xv::map::CScene> GetSharedScene() const { return m_pScene; }
	[[nodiscard]] const std::optional<xv::vfs::CGameInfo> &GetGame() const { return m_Game; }
	[[nodiscard]] QString GetFgdSource() const { return m_szFgdSource; }
	// Another game, or the same one mounted again (for the next load; resources follow it).
	void SetGame( std::optional<xv::vfs::CGameInfo> game ) { m_Game = std::move( game ); }

	// Edits go through the undo stack: szName names them in Edit > Undo. The selection becomes
	// selectAfter (or stays, when it is null), and goes back with an undo.
	void Edit( xv::map::MapEdit_t edit, const QString &szName, std::optional<std::vector<std::uint32_t>> selectAfter = std::nullopt );
	[[nodiscard]] QUndoStack *GetUndoStack() const { return m_pUndo; }
	[[nodiscard]] bool IsModified() const;
	// Writes the map (the text read, patched with every edit) to path, keeping what was there as
	// Hammer does, in a .vmx beside it. The undo stack is clean afterwards.
	bool Save( const std::filesystem::path &path, QString *pError );
	// An edit's scene, now (the undo stack's commands call it).
	void ApplyEditNow( const xv::map::MapEdit_t &edit );
	// While a drag goes on: edit shown on the scene as it was when the drag began, outside the undo
	// stack. EndPreview puts that scene back (before the drag's edit goes through Edit, or when
	// it is cancelled).
	void Preview( const xv::map::MapEdit_t &edit );
	void EndPreview();
	[[nodiscard]] bool IsPreviewing() const { return m_pPreviewBase != nullptr; }

	// Visibility, per object: hidden by its visgroups (or Hammer's own flags in the file), by
	// an automatic group, or quick-hidden.
	[[nodiscard]] bool IsShown( std::uint32_t nObject ) const;
	// Whether a visgroup's members are all shown (Qt::Checked), none (Qt::Unchecked) or some.
	[[nodiscard]] int GetVisgroupState( int nVisgroupId ) const;
	void SetVisgroupShown( int nVisgroupId, bool bShown );
	// The same for objects of an automatic visgroup.
	[[nodiscard]] int GetAutoState( const std::vector<std::uint32_t> &objects ) const;
	void SetAutoShown( const std::vector<std::uint32_t> &objects, bool bShown );
	void QuickHide( const std::vector<std::uint32_t> &objects );
	void QuickHideAllBut( const std::vector<std::uint32_t> &objects );
	void QuickUnhideAll();
	void ShowAll();
	// The members of a visgroup and of the ones inside it.
	[[nodiscard]] std::vector<std::uint32_t> GetVisgroupMembers( int nVisgroupId ) const;
	// Bumped on every change to what is shown or selected, for views that cache.
	[[nodiscard]] std::uint64_t GetStateGeneration() const { return m_nStateGeneration; }

	// Selection: sorted object numbers.
	[[nodiscard]] const std::vector<std::uint32_t> &GetSelection() const { return m_Selection; }
	[[nodiscard]] bool IsSelected( std::uint32_t nObject ) const;
	void SetSelection( std::vector<std::uint32_t> objects );
	void AddToSelection( const std::vector<std::uint32_t> &objects );
	void ToggleSelection( const std::vector<std::uint32_t> &objects );
	void ClearSelection();
	[[nodiscard]] MapSelectMode_t GetSelectMode() const { return m_nSelectMode; }
	void SetSelectMode( MapSelectMode_t nMode );
	// What clicking nObject selects in the current mode: itself, its entity with its solids, or
	// its whole group.
	[[nodiscard]] std::vector<std::uint32_t> Expand( std::uint32_t nObject ) const;
	[[nodiscard]] xv::map::Box_t GetSelectionBounds() const;
	[[nodiscard]] xv::map::Box_t GetObjectBounds( std::uint32_t nObject ) const;

signals:
	void LoadProgress( int nPercent, const QString &szStage );
	void Loaded();
	void LoadFailed( const QString &szError );
	void SelectionChanged();
	void VisibilityChanged();
	void SelectModeChanged();
	// An edit changed these objects (and the scene is a new snapshot).
	void SceneEdited( const std::vector<std::uint32_t> &changed );
	void ModifiedChanged( bool bModified );

private:
	void Run( Source_t source, int nGeneration );
	void Finish( std::shared_ptr<xv::map::CScene> pScene, QString szFgdSource );
	void Changed( bool bVisibility );

	std::shared_ptr<const xv::map::CScene> m_pScene;
	QUndoStack *m_pUndo = nullptr;
	std::shared_ptr<const xv::map::CScene> m_pPreviewBase; // the scene before the drag
	std::optional<xv::vfs::CGameInfo> m_Game;
	QString m_szFgdSource;

	std::thread m_Worker;
	std::atomic<bool> m_bStop{ false };
	int m_nGeneration = 0;

	std::vector<std::uint8_t> m_UserShown; // per object: its visgroups shown it
	std::vector<std::uint8_t> m_AutoShown;
	std::vector<std::uint8_t> m_QuickHidden;
	std::vector<std::uint32_t> m_Selection;
	MapSelectMode_t m_nSelectMode = MapSelectMode_t::MAP_SELECT_MODE_GROUPS;
	std::uint64_t m_nStateGeneration = 0;
};
