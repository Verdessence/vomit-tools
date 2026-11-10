//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Picking a particle system for a key (info_particle_system's effect_name): the game's
//          systems as a list with their files, or as a grid of pictures, filtered by words in
//          their names; the one under the mouse (or, away from them, the one picked) plays beside
//          them, as the particle editor plays it, with what it spawns and what spawns it; each
//          is marked a parent, a child or both. A double click picks one.
//
//===========================================================================//
#pragma once

#include <map>
#include <optional>

#include <QDialog>
#include <QIcon>
#include <QStringList>

class CMapParticles;
class CParticlePreview;
class QLabel;
class QLineEdit;
class QListView;
class QSortFilterProxyModel;
class QStandardItemModel;
class QTimer;
class QToolButton;

class CParticleBrowser : public QDialog
{
	Q_OBJECT

public:
	CParticleBrowser( const CMapParticles &particles, const QString &szCurrent, QWidget *pParent = nullptr );
	~CParticleBrowser() override;

	// Runs the browser; the system picked, if one was.
	static std::optional<QString> Pick( QWidget *pParent, const CMapParticles &particles, const QString &szCurrent );

	[[nodiscard]] QString GetPicked() const { return m_szPicked; }
	[[nodiscard]] QListView *GetView() const { return m_pView; }
	// The system playing beside the list, and the file it is from as shown.
	[[nodiscard]] QString GetPlaying() const { return m_szPlaying; }
	[[nodiscard]] CParticlePreview *GetPreview() const { return m_pPreview; }
	// The grid of pictures rather than the list.
	void SetGrid( bool bGrid );
	[[nodiscard]] bool IsGrid() const;
	// Systems with a picture made (tests).
	[[nodiscard]] int GetPictureCount() const { return static_cast<int>( m_Pictures.size() ); }

	static constexpr int ROLE_NAME = Qt::UserRole + 1;
	// "parent", "child", "parent, child" or empty: whether it spawns systems, is spawned, or both.
	static constexpr int ROLE_FAMILY = Qt::UserRole + 2;

protected:
	bool eventFilter( QObject *pObject, QEvent *pEvent ) override;

private:
	void Play( const QString &szName );
	void ShowPending();
	void MakePictures();
	void PickCurrent();

	const CMapParticles &m_Particles;
	QString m_szPicked;
	QString m_szPlaying;
	QString m_szPending; // under the mouse, waiting for it to rest
	QStandardItemModel *m_pModel = nullptr;
	QSortFilterProxyModel *m_pFilter = nullptr;
	QListView *m_pView = nullptr;
	QLineEdit *m_pWords = nullptr;
	QToolButton *m_pList = nullptr;
	QToolButton *m_pGrid = nullptr;
	CParticlePreview *m_pPreview = nullptr;
	CParticlePreview *m_pPainter = nullptr; // draws the grid's pictures, never shown
	QLabel *m_pName = nullptr;
	QLabel *m_pFile = nullptr;
	QLabel *m_pFamily = nullptr; // what it spawns and what spawns it
	QTimer *m_pWait = nullptr;
	QTimer *m_pPictures = nullptr;
	std::map<QString, QIcon> m_Pictures;
};
