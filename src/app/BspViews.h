//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What a map holds besides its packed files: its entities, each with its keyvalues, and
//          the table of lumps in its header.
//
//===========================================================================//
#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include <QWidget>

#include "CodeEdit.h"
#include "bsp/Stripper.h"

class QCheckBox;
class CCodeEdit;
class CStripperWords;
class QPushButton;
class QTimer;
class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

// Every entity by class and name, a search over their keyvalues, and the picked one's keyvalues;
// with the Stripper tick, as the map's Stripper:Source configs leave them when it loads.
class CEntityView : public QWidget
{
	Q_OBJECT

public:
	// bsp must outlive the view. map is the .bsp's file, for finding its Stripper configs; empty
	// for a config edited without its map (bsp then holds no entities).
	CEntityView( const xv::bsp::CBsp &bsp, const std::filesystem::path &map, QWidget *pParent = nullptr );
	~CEntityView() override;

	// The Stripper configs to preview, in the order they run (found beside the map at first).
	void SetStripperConfigs( std::vector<std::filesystem::path> configs );
	[[nodiscard]] const std::vector<std::filesystem::path> &GetStripperConfigs() const { return m_Configs; }
	void SetStripperShown( bool bShown );
	// What the list holds: the map's entities, or with Stripper on, what the configs make of them.
	[[nodiscard]] const std::vector<xv::bsp::StrippedEntity_t> &GetShown() const { return m_Shown; }

	// The config being edited: the map's own (addons/stripper/maps/<map>.cfg, made on saving), or
	// one picked with Config…. Edits show in the list as they're typed.
	[[nodiscard]] const std::filesystem::path &GetConfigPath() const { return m_ConfigPath; }
	void ShowConfigEditor( bool bShown );
	[[nodiscard]] QString GetConfigText() const;
	void SetConfigText( const QString &szText );
	// Edits another config, previewed after the map's global one when it is the map's own, else
	// by itself; the editor opens on it.
	void EditConfig( const std::filesystem::path &config );
	void ReadConfigData( const std::filesystem::path &config, const QString &szText );
	bool SaveConfigAs();
	// Adds rules at the end of the config, opening the editor.
	void AppendToConfig( const std::string &szText );
	[[nodiscard]] bool IsConfigModified() const { return m_bConfigModified; }
	// Writes the config. False (after saying why) when it can't be written.
	bool SaveConfig();

	// Shows only entities with a key or value holding the text.
	void SetSearch( const QString &szSearch );
	[[nodiscard]] int GetShownCount() const;
	// Picks the entity at its index in the map's entity lump.
	void SelectEntity( int nIndex );

signals:
	void ConfigModifiedChanged( bool bModified );

private:
	void Rebuild();
	void ShowKeyValues( int nShown );
	void ShowListMenu( const QPoint &at );
	void GoToProblem( QTreeWidgetItem *pItem );
	void LoadConfigText();
	void SetConfigModified( bool bModified );
	[[nodiscard]] const CStripperWords &Words();
	[[nodiscard]] CodeCompletion_t CompleteConfig( const QTextCursor &cursor );
	[[nodiscard]] QString DescribeConfig( const QTextCursor &cursor );

	const xv::bsp::CBsp &m_Bsp;
	std::vector<std::filesystem::path> m_Configs;
	std::vector<xv::bsp::StripperConfig_t> m_Parsed;
	std::vector<xv::bsp::StrippedEntity_t> m_Shown;
	QCheckBox *m_pStripper = nullptr;
	QLabel *m_pConfigs = nullptr;
	QLabel *m_pSummary = nullptr;
	QWidget *m_pProblemsPanel = nullptr;
	QTreeWidget *m_pProblems = nullptr; // each config's problems: line, what, which config
	QFileSystemWatcher *m_pWatcher = nullptr;
	QLineEdit *m_pSearch = nullptr;
	std::filesystem::path m_ConfigPath;
	QPushButton *m_pEdit = nullptr;
	QWidget *m_pEditor = nullptr;
	QLabel *m_pConfigPath = nullptr;
	CCodeEdit *m_pConfigText = nullptr;
	std::unique_ptr<CStripperWords> m_pWords; // the map's keys and values, made at the first suggestion
	QTimer *m_pReparse = nullptr;
	bool m_bConfigModified = false;
	bool m_bConfigReadOnly = false;
	QString m_szConfigData;
	bool m_bLoadingText = false;
	QTreeWidget *m_pList = nullptr;
	QTableWidget *m_pKeyValues = nullptr;
};

// The header's 64 lumps: offset, size, version and whether each is compressed.
class CLumpView : public QWidget
{
	Q_OBJECT

public:
	explicit CLumpView( const xv::bsp::CBsp &bsp, QWidget *pParent = nullptr );
};
