//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's Run Map window (F9): the Normal tab's choices (how much of BSP, VIS and
//          RAD to run, HDR, the game's parameters), the Expert tab's named command sequences
//          (edited here, seeded from Hammer++'s or Hammer's own files, kept in the settings),
//          and the compile's log with the problems the compilers reported, each a click from
//          the brush or point it names.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <QDialog>
#include <QElapsedTimer>
#include <QString>
#include <QTemporaryDir>

#include "map/Compile.h"
#include "map/MapCompile.h"

class CMapEditor;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;

class CMapCompileDialog : public QDialog
{
	Q_OBJECT

public:
	explicit CMapCompileDialog( CMapEditor &editor );
	~CMapCompileDialog() override;

	// The Normal tab's choices and the Expert tab's sequences, as the dialog has them.
	[[nodiscard]] xv::map::NormalOptions_t GetNormalOptions() const;
	[[nodiscard]] const std::vector<xv::map::CompileSequence_t> &GetSequences() const { return m_Sequences; }
	[[nodiscard]] bool IsExpert() const;
	// The commands the Go button would run, with the variables filled in.
	[[nodiscard]] std::vector<xv::map::ResolvedCommand_t> GetCommands() const;
	[[nodiscard]] xv::map::CompileVariables_t GetVariables() const;
	// Saves the map and runs the commands; false when the map couldn't be saved or a tool is
	// missing (the log says which).
	bool Run();
	[[nodiscard]] CCompileRunner &GetRunner() { return *m_pRunner; }
	[[nodiscard]] QPlainTextEdit *GetLog() const { return m_pLog; }
	[[nodiscard]] QListWidget *GetProblems() const { return m_pProblems; }

	// Where the settings keep the sequences (KeyValues, Hammer++'s shape) and the Normal choices.
	static QString SequencesSetting();

private:
	void LoadSequences();
	void SaveSequences();
	void ShowSequence();
	void ShowCommand();
	void CommandEdited();
	void AddCommand();
	void RemoveCommand();
	void MoveCommand( int nBy );
	void AddSequence();
	void RemoveSequence();
	void Finished( bool bSuccess, const xv::map::CompileReport_t &report );
	void ProblemPicked( int nRow );
	[[nodiscard]] xv::map::CompileSequence_t *CurrentSequence();
	[[nodiscard]] xv::map::CompileCommand_t *CurrentCommand();

	CMapEditor &m_Editor;
	CCompileRunner *m_pRunner = nullptr;
	std::vector<xv::map::CompileSequence_t> m_Sequences;
	xv::map::CompileReport_t m_Report;
	QElapsedTimer m_Clock;
	// With the cordon on, this run compiles a sealed copy of the map, under its own name, in a
	// folder of its own; what the compilers leave there comes back beside the map.
	std::unique_ptr<QTemporaryDir> m_pCordonFolder;
	void BringBackCompiled();
	bool m_bFilling = false;

	QTabWidget *m_pTabs = nullptr;
	QButtonGroup *m_pBsp = nullptr;
	QButtonGroup *m_pVis = nullptr;
	QButtonGroup *m_pRad = nullptr;
	QCheckBox *m_pHdr = nullptr;
	QCheckBox *m_pNoGame = nullptr;
	QLineEdit *m_pGameParameters = nullptr;
	QComboBox *m_pSequence = nullptr;
	QListWidget *m_pCommands = nullptr;
	QLineEdit *m_pRun = nullptr;
	QLineEdit *m_pParms = nullptr;
	QComboBox *m_pSpecial = nullptr;
	QCheckBox *m_pEnsure = nullptr;
	QLineEdit *m_pEnsureFile = nullptr;
	QCheckBox *m_pWait = nullptr;
	QPlainTextEdit *m_pLog = nullptr;
	QListWidget *m_pProblems = nullptr;
	QLabel *m_pStatus = nullptr;
	QPushButton *m_pGo = nullptr;
	QPushButton *m_pCancel = nullptr;
};
