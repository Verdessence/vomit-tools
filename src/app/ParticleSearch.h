//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Finds particle systems across every particle file the game has (and the ones open), by
//          name, material, operator or the systems they spawn. Activating a result opens it.
//
//===========================================================================//
#pragma once

#include <functional>
#include <vector>

#include <QDialog>

#include "particles/Particles.h"

class QLabel;
class QLineEdit;
class QTreeWidget;

class CParticleSearch : public QDialog
{
	Q_OBJECT

public:
	using Source_t = std::function<std::vector<xv::particles::SystemSummary_t>()>;

	// source is asked again each time the dialog is shown, so it stays current.
	explicit CParticleSearch( Source_t pfnSource, QWidget *pParent = nullptr );

	void SetQuery( const QString &szQuery );
	// The systems listed, as (file, system).
	[[nodiscard]] std::vector<std::pair<QString, QString>> GetResults() const;

signals:
	void OpenRequested( const QString &szFile, const QString &szSystem );

protected:
	void showEvent( QShowEvent *pEvent ) override;

private:
	void update();

	Source_t m_pfnSource;
	std::vector<xv::particles::SystemSummary_t> m_Systems;
	QLineEdit *m_pQuery = nullptr;
	QTreeWidget *m_pResults = nullptr;
	QLabel *m_pSummary = nullptr;
};
