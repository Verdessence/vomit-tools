//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Model brush export options and asynchronous VMF generation from a frozen model.
//===========================================================================//
#pragma once
#include <QDialog>
#include "model/Vmf.h"
class QComboBox;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
class QLabel;
class QThread;
class CModelVmfDialog : public QDialog
{
	Q_OBJECT
public:
	CModelVmfDialog( QString szPath, xv::model::Reader_t pfnRead, xv::model::CModel model, const xv::model::Selection_t &selection = {}, QWidget *pParent = nullptr );
	~CModelVmfDialog() override;
	void reject() override;

private:
	void Export();
	QString m_szPath;
	xv::model::Reader_t m_pfnRead;
	xv::model::CModel m_Model;
	xv::model::Selection_t m_Selection;
	QComboBox *m_pSource, *m_pTexture;
	QCheckBox *m_pNodraw, *m_pVertices, *m_pDetail, *m_pReference;
	QSpinBox *m_pHulls, *m_pSides, *m_pHullVertices, *m_pVoxels;
	QDoubleSpinBox *m_pGrid, *m_pConcavity, *m_pOffset[ 3 ];
	QPushButton *m_pExport;
	QLabel *m_pStatus;
	QThread *m_pWorker = nullptr;
};
