//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: An open soundscape or soundscript file.  Every change goes through the undo stack and
//          swaps in a whole new copy of the top-level entry it touches, so undoing restores the
//          file byte for byte, comments and spacing included.
//
//===========================================================================//
#pragma once

#include <filesystem>
#include <functional>
#include <vector>

#include <QObject>
#include <QString>

#include "kv1/Kv1.h"
#include "sound/SoundLint.h"
#include "sound/SoundSchema.h"

class QUndoStack;

class CSoundDocument : public QObject
{
	Q_OBJECT

public:
	CSoundDocument( QString szGamePath, xv::sound::FileKind_t nKind, xv::kv1::CParseResult parsed, QObject *pParent = nullptr );

	[[nodiscard]] const QString &GetGamePath() const { return m_szGamePath; }
	[[nodiscard]] QString GetFileName() const;
	[[nodiscard]] xv::sound::FileKind_t GetKind() const { return m_nKind; }
	[[nodiscard]] const xv::sound::CSchema &GetSchema() const { return *m_pSchema; }
	[[nodiscard]] const xv::kv1::CDocument &GetDocument() const { return m_Document; }
	[[nodiscard]] const std::vector<xv::kv1::Diagnostic_t> &GetParseDiagnostics() const { return m_Diagnostics; }
	[[nodiscard]] QUndoStack *GetUndoStack() const { return m_pUndo; }
	[[nodiscard]] bool IsModified() const;

	// The loose file Save writes to; empty when the file came from a VPK or is new.
	[[nodiscard]] const std::filesystem::path &GetDiskPath() const { return m_DiskPath; }
	void SetDiskPath( std::filesystem::path path ) { m_DiskPath = std::move( path ); }
	// The game or mod folder holding this file's scripts/ (or maps/) folder, for finding its sounds on
	// disk when no game is mounted; empty if it isn't in one.
	[[nodiscard]] std::filesystem::path GetLocalRoot() const;
	[[nodiscard]] std::string GetSerialize() const;
	void MarkSaved();
	void MarkUnsaved(); // a new document that has never been written
	[[nodiscard]] bool IsNew() const { return m_bNeverSaved; }

	[[nodiscard]] const xv::kv1::CNode *Node( const xv::sound::NodePath_t &path ) const;
	// The schema block for the block at path; nullptr for blocks the schema doesn't know.
	[[nodiscard]] const xv::sound::BlockDef_t *BlockDef( const xv::sound::NodePath_t &path ) const;

	void SetKey( const xv::sound::NodePath_t &path, const QString &szKey );
	void SetValue( const xv::sound::NodePath_t &path, const QString &szValue );
	void InsertChild( const xv::sound::NodePath_t &parent, std::size_t nIndex, xv::kv1::CNode child, const QString &szDescription );
	void RemoveNode( const xv::sound::NodePath_t &path );
	void InsertEntry( std::size_t nIndex, xv::kv1::CNode entry, const QString &szDescription );

signals:
	void Changed();
	void ModifiedChanged( bool bModified );

private:
	friend class CReplaceEntryCommand;
	friend class CInsertEntryCommand;
	friend class CRemoveEntryCommand;

	// Applies edit to a copy of top-level entry rootIndex and pushes the swap as one undo step.
	// Edits with the same non-empty merge key in a row become one undo step.
	void EditEntry( std::size_t nRootIndex, const QString &szDescription, const std::function<bool( xv::kv1::CNode &entry )> &pfnEdit, const QString &szMergeKey = {} );

	QString m_szGamePath;
	xv::sound::FileKind_t m_nKind;
	const xv::sound::CSchema *m_pSchema;
	xv::kv1::CDocument m_Document;
	std::vector<xv::kv1::Diagnostic_t> m_Diagnostics;
	std::filesystem::path m_DiskPath;
	QUndoStack *m_pUndo;
	bool m_bNeverSaved = false;
};
