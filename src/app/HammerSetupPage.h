//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Preferences > Maps > General's Hammer section: the open game's entry in Hammer++'s
//          (or Hammer's) game configuration, from what is there or what the game's folders
//          suggest, edited and saved so that Hammer opens the game ready to map (its FGDs,
//          compilers, folders). The map viewer reads its game data and default entities from
//          the same entry.
//
//===========================================================================//
#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <QWidget>

#include "map/GameConfig.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;

class CHammerSetupPage : public QWidget
{
	Q_OBJECT

public:
	explicit CHammerSetupPage( QWidget *pParent = nullptr );

	// The game whose entry the page edits (an empty folder: no game is open). pfnMaterials lists
	// the game's .vmt paths ("materials/tools/toolsskybox.vmt") for the cordon texture's choices.
	// The fields are filled again only when the game changes, so unsaved edits survive the
	// window being shown again.
	void SetGame( const std::filesystem::path &gameDir, const QString &szGameName, const std::function<std::vector<std::string>()> &pfnMaterials );

	// Writes the entry into the chosen Hammer's configuration (the file as it was kept beside it
	// once, as .bak). Older settings that named game data and default entities apart from the
	// entry, which the map viewer reads first, then give way to it, or take its values where the
	// viewer would read another configuration. False with pError on failure.
	bool Save( QString *pError );
	// Whether the fields differ from the entry they were last filled with or saved as.
	[[nodiscard]] bool IsModified() const;
	[[nodiscard]] std::filesystem::path GetConfigFile() const;
	[[nodiscard]] xv::map::HammerSetup_t GetSetup() const;

private:
	void Fill();
	void FillFields( const xv::map::HammerSetup_t &setup );
	void FillEntityChoices();
	void UpdatePreferences( const xv::map::HammerSetup_t &setup ) const;
	[[nodiscard]] xv::map::HammerKind_t GetKind() const;

	std::filesystem::path m_GameDir;
	QString m_szGameName;
	QWidget *m_pFields = nullptr; // everything but the status, off without a game
	QComboBox *m_pKind = nullptr;
	QLabel *m_pStatus = nullptr;
	QLabel *m_pSaved = nullptr;
	QLineEdit *m_pName = nullptr;
	QPlainTextEdit *m_pFgds = nullptr;
	std::map<std::string, QLineEdit *> m_Paths; // by the config's key
	QTimer *m_pFgdsEdited = nullptr;            // the entity lists follow the game data a moment after typing
	QComboBox *m_pPointEntity = nullptr;
	QComboBox *m_pSolidEntity = nullptr;
	QDoubleSpinBox *m_pTextureScale = nullptr;
	QSpinBox *m_pLightmapScale = nullptr;
	QComboBox *m_pCordonTexture = nullptr;
	std::string m_szFilled; // the fields' entry as written, when they were last filled
};
