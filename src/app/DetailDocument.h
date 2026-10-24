//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Lossless detail file edits, saved state and undo history.
//===========================================================================//
#pragma once

#include <filesystem>
#include <functional>
#include <QObject>
#include <QUndoStack>
#include "kv1/Kv1.h"

class CDetailDocument : public QObject
{
	Q_OBJECT
public:
	CDetailDocument( QString szGamePath, xv::kv1::CParseResult parsed, QObject *pParent = nullptr );
	[[nodiscard]] const xv::kv1::CDocument &GetDocument() const { return m_Document; }
	[[nodiscard]] const auto &GetParseDiagnostics() const { return m_Diagnostics; }
	[[nodiscard]] const QString &GetGamePath() const { return m_szGamePath; }
	[[nodiscard]] QString GetFileName() const;
	[[nodiscard]] std::string GetSerialize() const { return xv::kv1::Write( m_Document ); }
	[[nodiscard]] QUndoStack *GetUndoStack() const { return m_pUndo; }
	[[nodiscard]] bool IsModified() const { return m_bNew || !m_pUndo->isClean(); }
	[[nodiscard]] const std::filesystem::path &GetDiskPath() const { return m_DiskPath; }
	void SetGamePath( QString szPath ) { m_szGamePath = std::move( szPath ); }
	void SetDiskPath( std::filesystem::path path ) { m_DiskPath = std::move( path ); }
	[[nodiscard]] bool IsNew() const { return m_bNew; }
	void MarkSaved();
	void MarkUnsaved();
	// Optional view context follows the edit on redo (true) and undo (false), such as a packed sheet.
	void Edit( const QString &szDescription, const std::function<void( xv::kv1::CDocument & )> &pfnEdit, std::function<void( bool )> pfnContext = {} );
signals:
	void Changed();
	void ModifiedChanged( bool bModified );

private:
	friend class CDetailEditCommand;
	xv::kv1::CDocument m_Document;
	std::vector<xv::kv1::Diagnostic_t> m_Diagnostics;
	QString m_szGamePath;
	std::filesystem::path m_DiskPath;
	QUndoStack *m_pUndo;
	bool m_bNew = false;
};
