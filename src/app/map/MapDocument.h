//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: One open map for the viewer: read and built in the background, then what is shown,
//          hidden and selected. Nothing here changes the map; hiding and selecting are the
//          view's own state.
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

#include "map/Scene.h"
#include "vfs/Vfs.h"

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

	// Visibility, per object: hidden by its visgroups (or Hammer's own flags in the file), by
	// an automatic group, or quick-hidden.
	[[nodiscard]] bool IsShown( std::uint32_t nObject ) const;
	// Whether a visgroup's members are all shown (Qt::Checked), none (Qt::Unchecked) or some.
	[[nodiscard]] int GetVisgroupState( int nVisgroupId ) const;
	void SetVisgroupShown( int nVisgroupId, bool bShown );
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

private:
	void Run( Source_t source, int nGeneration );
	void Finish( std::shared_ptr<xv::map::CScene> pScene, QString szFgdSource );
	void Changed( bool bVisibility );

	std::shared_ptr<const xv::map::CScene> m_pScene;
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
