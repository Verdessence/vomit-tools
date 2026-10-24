//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: An open texture (.vtf). The file's bytes are the document: an import or a conversion
//          replaces them as one undo step, so an untouched file saves byte for byte as it was read.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

#include <QObject>
#include <QString>

#include "texture/Vtf.h"

class QUndoStack;

class CTextureDocument : public QObject
{
	Q_OBJECT

public:
	// bytes must read as a VTF (see IsValid).
	CTextureDocument( QString szGamePath, std::vector<std::byte> bytes, QObject *pParent = nullptr );

	[[nodiscard]] bool IsValid() const { return m_bValid; }
	[[nodiscard]] const QString &GetGamePath() const { return m_szGamePath; }
	void SetGamePath( QString szGamePath ) { m_szGamePath = std::move( szGamePath ); }
	[[nodiscard]] QString GetFileName() const;
	[[nodiscard]] const xv::texture::CVtf &GetVtf() const { return m_Vtf; }
	[[nodiscard]] const std::vector<std::byte> &GetBytes() const { return m_Vtf.m_Data; }
	[[nodiscard]] QUndoStack *GetUndoStack() const { return m_pUndo; }
	[[nodiscard]] bool IsModified() const;

	// The loose file Save writes to; empty when the file came from a VPK or is new.
	[[nodiscard]] const std::filesystem::path &GetDiskPath() const { return m_DiskPath; }
	void SetDiskPath( std::filesystem::path path ) { m_DiskPath = std::move( path ); }
	void MarkSaved();
	void MarkUnsaved(); // a new document that has never been written
	[[nodiscard]] bool IsNew() const { return m_bNeverSaved; }
	// Where Save As starts for a new texture (beside the pictures it was made from).
	[[nodiscard]] const std::filesystem::path &GetSuggestedPath() const { return m_SuggestedPath; }
	void SetSuggestedPath( std::filesystem::path path ) { m_SuggestedPath = std::move( path ); }

	// Puts a new file in place of the current one as one undo step. False (and nothing changes)
	// when bytes don't read as a VTF.
	bool Replace( std::vector<std::byte> bytes, const QString &szDescription );

signals:
	void Changed();
	void ModifiedChanged( bool bModified );

private:
	friend class CReplaceTextureCommand;
	void Set( const std::vector<std::byte> &bytes );
	void UpdateModified();

	QString m_szGamePath;
	xv::texture::CVtf m_Vtf;
	bool m_bValid = false;
	std::filesystem::path m_DiskPath;
	std::filesystem::path m_SuggestedPath;
	QUndoStack *m_pUndo;
	bool m_bNeverSaved = false;
	bool m_bWasModified = false;
};
