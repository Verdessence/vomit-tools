//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Tools > Set Up Hammer: the open game's entry in Hammer++'s (or Hammer's) game
//          configuration, from what is there or what the game's folders suggest, edited and
//          saved so that Hammer opens the game ready to map (its FGDs, compilers, folders).
//
//===========================================================================//
#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <string>

#include <QDialog>

#include "map/GameConfig.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

class CHammerSetupDialog : public QDialog
{
	Q_OBJECT

public:
	CHammerSetupDialog( const std::filesystem::path &gameDir, const QString &szGameName, QWidget *pParent = nullptr );

	// Writes the entry into the chosen Hammer's configuration (the file as it was kept beside it
	// once, as .bak). False with pError on failure.
	bool Save( QString *pError );
	[[nodiscard]] std::filesystem::path GetConfigFile() const;
	[[nodiscard]] xv::map::HammerSetup_t GetSetup() const;

private:
	void Fill();
	[[nodiscard]] xv::map::HammerKind_t GetKind() const;

	std::filesystem::path m_GameDir;
	QString m_szGameName;
	QComboBox *m_pKind = nullptr;
	QLabel *m_pStatus = nullptr;
	QLineEdit *m_pName = nullptr;
	QPlainTextEdit *m_pFgds = nullptr;
	std::map<std::string, QLineEdit *> m_Paths; // by the config's key
	QLineEdit *m_pPointEntity = nullptr;
	QLineEdit *m_pSolidEntity = nullptr;
	QDoubleSpinBox *m_pTextureScale = nullptr;
	QSpinBox *m_pLightmapScale = nullptr;
	QLineEdit *m_pCordonTexture = nullptr;
	std::function<void( const xv::map::HammerSetup_t & )> m_pfnFill; // puts an entry in the fields
};
