//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What the main window asks of an editor tab (its title, how to reopen it, closing
//          it, its undo stack) without knowing the editor's class. Tabs that implement it need
//          no case of their own in the main window.
//
//===========================================================================//
#pragma once

#include <QString>
#include <QWidget>

class QUndoStack;

namespace xv::vfs
{
struct CGameInfo;
} // namespace xv::vfs

class IEditorTab
{
public:
	virtual ~IEditorTab() = default;

	// The name for the window's title ("map.vmf").
	[[nodiscard]] virtual QString GetTabTitle() const = 0;
	// How to reopen it at the next start: "<kind>\t<disk|game>\t<path>"; empty not to.
	[[nodiscard]] virtual QString GetSessionEntry() const = 0;
	// Whether it was opened from the game's files, so it closes when another game opens.
	[[nodiscard]] virtual bool IsFromGame() const = 0;
	// Asks about unsaved changes; false keeps the tab open.
	virtual bool ConfirmClose() { return true; }
	[[nodiscard]] virtual QUndoStack *GetUndoStack() const { return nullptr; }
	// Stops background work, before the tab goes away.
	virtual void StopWork() {}
	// The game's files were mounted again, or another game opened (pGame null: none).
	virtual void GameChanged( const xv::vfs::CGameInfo *pGame ) { (void)pGame; }
};

[[nodiscard]] inline IEditorTab *AsEditorTab( QWidget *pWidget )
{
	return dynamic_cast<IEditorTab *>( pWidget );
}
