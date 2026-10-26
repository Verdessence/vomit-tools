//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Model exchange exports, editable source projects, external compilation and BSP packing.
//===========================================================================//
#pragma once
#include <QDialog>
#include <memory>
#include "SourceTools.h"
#include "model/Export.h"
class QLineEdit;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QPlainTextEdit;
class QPushButton;
class QTemporaryDir;
class CModelWorkflow : public QDialog
{
	Q_OBJECT
public:
	CModelWorkflow( QString szPath, xv::model::Reader_t pfnRead, xv::model::CModel model, xv::model::CStudioFile source, xv::model::Clip_t clip, std::optional<xv::vfs::CGameInfo> game, QWidget *pParent = nullptr );
	~CModelWorkflow() override;
signals:
	void OpenModelRequested( const QString &szPath );

private:
	void Export( int nFormat );
	void ExportSources();
	void Compile();
	void Pack();
	void Finished( bool bSuccess );
	void SaveBuild();
	void UpdateTools();
	[[nodiscard]] xv::model::Export_t Sources();
	[[nodiscard]] xv::model::Clip_t Clip() const;
	[[nodiscard]] xv::vfs::CGameInfo Game() const;
	bool Write( const xv::model::Export_t &files, const QString &szFolder );
	void Log( const QString &szText );
	QString m_szPath, m_szSaved, m_szResultBsp;
	xv::model::Reader_t m_pfnRead;
	xv::model::CModel m_Model;
	xv::model::CStudioFile m_Source;
	xv::model::Clip_t m_Clip;
	std::optional<xv::vfs::CGameInfo> m_Game;
	QLineEdit *m_pGame = nullptr, *m_pCompiler = nullptr, *m_pPacker = nullptr, *m_pQc = nullptr, *m_pMesh = nullptr, *m_pCollision = nullptr, *m_pModelName = nullptr, *m_pMap = nullptr;
	QComboBox *m_pOperation = nullptr, *m_pBodygroup = nullptr, *m_pBodyModel = nullptr, *m_pSequences = nullptr;
	QSpinBox *m_pFirst = nullptr, *m_pLast = nullptr;
	QDoubleSpinBox *m_pFps = nullptr;
	QCheckBox *m_pAnimation = nullptr, *m_pBaseBlend = nullptr;
	QPlainTextEdit *m_pLog = nullptr;
	QPushButton *m_pBuild = nullptr, *m_pPack = nullptr, *m_pSaveBuild = nullptr, *m_pOpen = nullptr;
	std::unique_ptr<QTemporaryDir> m_pWork;
	CSourceToolRunner m_Runner;
	xv::model::Export_t m_BuildFiles;
	bool m_bPacking = false;
};
