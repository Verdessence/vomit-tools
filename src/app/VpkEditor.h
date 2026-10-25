//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A tab for one archive opened from disk (a VPK, GCF, GMA...): its folders and files,
//          a search over their paths, a preview of the picked file, and exporting files out. A
//          map (.bsp) shows its packed files that way, beside its entities and lumps.
//
//===========================================================================//
#pragma once

#include <functional>
#include <memory>

#include <QStringList>
#include <QWidget>

#include "material/Material.h"

namespace xv::vfs
{
class CPackage;
}

class CPackageFilter;
class CPackageModel;
class CSoundPlayer;
class QLabel;
class QLineEdit;
class QTimer;
class QTreeView;

class CVpkEditor : public QWidget
{
	Q_OBJECT

public:
	// pPlayer may be null. pfnGame reads files the archive doesn't have (a material's textures)
	// from the mounted game; it may be empty.
	CVpkEditor( std::unique_ptr<xv::vfs::CPackage> pPackage, CSoundPlayer *pPlayer, xv::material::Reader_t pfnGame, QWidget *pParent = nullptr );
	~CVpkEditor() override;

	[[nodiscard]] const xv::vfs::CPackage &GetPackage() const { return *m_pPackage; }
	[[nodiscard]] QString GetFileName() const;
	// Reads from the archive first, then the game; for editors opened from it.
	[[nodiscard]] const xv::material::Reader_t &GetReader() const { return m_pfnRead; }
	// Which files have an editor: those get an Open in Editor button and open on double-click.
	void SetCanOpen( std::function<bool( const QString &szPath )> pfnCanOpen ) { m_pfnCanOpen = std::move( pfnCanOpen ); }

	// The search: a part of a path ("skybox/"), or a pattern with * and ? ("*.wav").
	void SetSearch( const QString &szSearch );
	[[nodiscard]] QString GetSearch() const;
	// How many files the search lets through (all of them with no search).
	[[nodiscard]] int GetMatchCount() const { return m_nMatches; }
	// Shows a file in the preview and picks it in the tree.
	void Preview( const QString &szPath );
	[[nodiscard]] QString GetPreviewedPath() const { return m_szPreviewed; }
	// The files the picked rows stand for: picked files, plus everything a picked folder holds
	// that the search lets through.
	[[nodiscard]] QStringList GetSelectedFiles() const;
	// Writes files out under folder, each below szStripFolder (an archive folder, "" for none:
	// full paths). Shows progress and, unless bQuiet, what happened. Returns how many were written.
	int ExportTo( const QStringList &files, const QString &szFolder, const QString &szStripFolder = {}, bool bQuiet = false );

signals:
	void StatusMessage( const QString &szText );
	// A file the SetCanOpen test passed was asked to open in its editor.
	void OpenRequested( const QString &szPath );

private:
	void ApplySearch();
	void ExportSelected();
	void ExportAll();
	void ShowContextMenu( const QPoint &at );
	[[nodiscard]] QString AskExportFolder();
	void SetPreviewWidget( QWidget *pView );

	std::unique_ptr<xv::vfs::CPackage> m_pPackage;
	CSoundPlayer *m_pPlayer = nullptr;
	xv::material::Reader_t m_pfnRead; // the archive first, then the game
	std::function<bool( const QString & )> m_pfnCanOpen;
	CPackageModel *m_pModel = nullptr;
	CPackageFilter *m_pFilter = nullptr;
	QTreeView *m_pTree = nullptr;
	QLineEdit *m_pSearch = nullptr;
	QTimer *m_pSearchDelay = nullptr;
	QLabel *m_pCount = nullptr;
	QWidget *m_pPreviewHost = nullptr;
	QString m_szPreviewed;
	int m_nMatches = 0;
};
