//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Generating a manifest from the game's files: soundscapes_manifest.txt,
//          game_sounds_manifest.txt, particles_manifest.txt or a map's <map>_particles.txt. Every
//          file that could be listed is offered, ticked when it's listed (or a map needs it), with
//          the manifest it makes beside them. What stays listed keeps its place and comments.
//
//===========================================================================//
#pragma once

#include <string>
#include <vector>

#include <QDialog>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTreeWidget;
class QTreeWidgetItem;

class CManifestDialog : public QDialog
{
	Q_OBJECT

public:
	enum class Format_t
	{
		FORMAT_SOUNDSCAPES,
		FORMAT_SOUNDSCRIPTS,
		FORMAT_PARTICLES // particles_manifest.txt and <map>_particles.txt
	};

	struct File_t
	{
		QString m_szPath;        // "scripts/soundscapes_dust.txt"
		bool m_bExists = true;   // the game has it
		bool m_bSuggested = false; // ticked although the manifest doesn't list it
		QString m_szWhy;         // why it's suggested, shown beside it
	};

	// szCurrent is the manifest's text now (empty when there's none yet).
	CManifestDialog( Format_t nFormat, const QString &szManifestPath, const std::string &szCurrent, std::vector<File_t> files, QWidget *pParent = nullptr );

	// The manifest as it would be written.
	[[nodiscard]] std::string GetText() const;
	// For tests.
	void SetListed( const QString &szPath, bool bListed );
	void SetPrecached( const QString &szPath, bool bPrecached );
	[[nodiscard]] int GetRowCount() const;
	// Shown above the files: what couldn't be worked out (systems no file defines...).
	void SetNotes( const QString &szNotes );

private:
	enum Column_t
	{
		COLUMN_FILE,
		COLUMN_PRECACHE,
		COLUMN_STATUS,
		COLUMN_COUNT
	};

	[[nodiscard]] QTreeWidgetItem *ItemFor( const QString &szPath ) const;
	void UpdatePreview();

	Format_t m_nFormat;
	std::string m_szCurrent;
	QTreeWidget *m_pFiles = nullptr;
	QLineEdit *m_pFilter = nullptr;
	QPlainTextEdit *m_pPreview = nullptr;
	QLabel *m_pCount = nullptr;
	QLabel *m_pNotes = nullptr;
};
