//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: An open particle (.pcf) file.  Every change goes through Edit(), which records the
//          elements it changed as one undo step. Elements are only appended while editing (see
//          xv::particles), so element indices stay valid for the life of the document and the
//          views can select by index.
//
//===========================================================================//
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include <QObject>
#include <QString>

#include "dmx/Dmx.h"

class QUndoStack;

class CParticleDocument : public QObject
{
	Q_OBJECT

public:
	CParticleDocument( QString szGamePath, xv::dmx::Document_t document, QObject *pParent = nullptr );

	[[nodiscard]] const QString &GetGamePath() const { return m_szGamePath; }
	[[nodiscard]] QString GetFileName() const;
	[[nodiscard]] const xv::dmx::Document_t &GetDocument() const { return m_Document; }
	[[nodiscard]] QUndoStack *GetUndoStack() const { return m_pUndo; }
	[[nodiscard]] bool IsModified() const;

	// The loose file Save writes to; empty when the file came from a VPK or is new.
	[[nodiscard]] const std::filesystem::path &GetDiskPath() const { return m_DiskPath; }
	void SetDiskPath( std::filesystem::path path ) { m_DiskPath = std::move( path ); }
	void MarkSaved();
	void MarkUnsaved(); // a new document that has never been written
	[[nodiscard]] bool IsNew() const { return m_bNeverSaved; }

	// The file as written: binary, without elements nothing refers to any more. An unedited file
	// comes out byte for byte as it was read.
	[[nodiscard]] std::optional<xv::dmx::Bytes_t> Serialize( std::string *pError = nullptr ) const;

	// Applies change to the document as one undo step named description. Nothing is recorded when
	// it changes nothing. Consecutive edits with the same non-empty mergeKey (typing into one field)
	// become a single step.
	bool Edit( const QString &szDescription, const std::function<void( xv::dmx::Document_t & )> &pfnChange, const QString &szMergeKey = {} );
	// Records a change to how the file is shown rather than to the file (the preview's control
	// points) as an undo step. It is already made; undo and redo make it again. Such steps alone
	// don't make the file modified.
	void AddViewStep( const QString &szDescription, std::function<void()> pfnUndo, std::function<void()> pfnRedo );

signals:
	void Changed();
	void ModifiedChanged( bool bModified );

private:
	friend class CParticleEditCommand;

	void UpdateModified();

	QString m_szGamePath;
	xv::dmx::Document_t m_Document;
	std::filesystem::path m_DiskPath;
	QUndoStack *m_pUndo;
	bool m_bNeverSaved = false;
	bool m_bWasModified = false;
};
