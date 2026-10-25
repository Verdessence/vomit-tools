//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: An open compiled model being edited: the bytes of its files (.mdl, .vvd, the .vtx
//          files, .phy, .ani) and the changes made to them. Each edit runs on a copy and becomes
//          one undo step holding only the bytes that differ, so untouched files save byte for
//          byte as they were read. Before saving, the files are read back as the viewer reads
//          them, and a set that no longer reads is refused.
//
//===========================================================================//
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>

#include <QObject>
#include <QString>

#include "model/Edit.h"

class QUndoStack;

class CModelDocument : public QObject
{
	Q_OBJECT

public:
	// szMdlPath is the model's game path ("models/props/crate.mdl"); paths maps each file kind to
	// the game path it was read from (the .ani's name is the model's own).
	CModelDocument( std::string szMdlPath, xv::model::Files_t files, std::map<std::string, std::string> paths, QObject *pParent = nullptr );

	[[nodiscard]] const std::string &GetMdlPath() const { return m_szMdlPath; }
	[[nodiscard]] const xv::model::Files_t &GetFiles() const { return m_Files; }
	[[nodiscard]] const xv::model::Bytes_t &GetMdl() const { return m_Files.at( ".mdl" ); }
	[[nodiscard]] bool Has( const std::string &szKind ) const { return m_Files.contains( szKind ); }
	// The game path a kind of file was read from.
	[[nodiscard]] std::string PathOf( const std::string &szKind ) const;
	// A file of the set by its game path, as it is now; nullopt for other paths.
	[[nodiscard]] std::optional<std::vector<std::byte>> Read( std::string_view szPath ) const;

	[[nodiscard]] QUndoStack *GetUndoStack() const { return m_pUndo; }
	[[nodiscard]] bool IsModified() const;
	// The kinds of file that differ from what was last saved.
	[[nodiscard]] std::vector<std::string> GetModifiedKinds() const;
	void MarkSaved();

	// Runs pfnEdit on a copy of the files; when it succeeds and changes something, the change
	// becomes an undo step named szName. Edits with the same nMerge (not -1) in a row join into one
	// step, as a spin box's steps do. Gives the reason when the edit is refused.
	QString Edit( const QString &szName, const std::function<void( xv::model::Files_t & )> &pfnEdit, int nMerge = -1 );

	// What stops the files being saved: they must still read as a model (and draw, and the hull
	// read, when they did when opened). Empty when they can be saved.
	[[nodiscard]] QString Check() const;

signals:
	void Changed();
	void ModifiedChanged( bool bModified );

private:
	friend class CModelChangeCommand;
	void Apply( const xv::model::Change_t &change, bool bForward );

	std::string m_szMdlPath;
	xv::model::Files_t m_Files, m_Saved;
	std::map<std::string, std::string> m_Paths;
	QUndoStack *m_pUndo;
	bool m_bDrewWhenOpened = false, m_bHullReadWhenOpened = false;
};
