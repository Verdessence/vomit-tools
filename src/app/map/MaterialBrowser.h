//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's Texture Browser: the game's materials as pictures, filtered by words in
//          their names (every word must match) or to those the map uses, at 32, 64 or 128
//          pixels; a double click picks one. Pictures are made on a thread of its own and
//          kept on disk between runs.
//
//===========================================================================//
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <QAbstractListModel>
#include <QDialog>
#include <QIcon>
#include <QStringList>

#include "vfs/Vfs.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListView;
class QSortFilterProxyModel;

// The materials, one row each: the name as Hammer shows it (upper case, without "materials/"
// and ".vmt") and its picture once made.
class CMaterialList : public QAbstractListModel
{
	Q_OBJECT

public:
	explicit CMaterialList( QObject *pParent = nullptr );

	[[nodiscard]] int rowCount( const QModelIndex &parent = {} ) const override;
	[[nodiscard]] QVariant data( const QModelIndex &index, int nRole ) const override;

	void SetNames( QStringList names );
	void SetPicture( int nRow, const QIcon &icon );
	void SetSize( int nSize );
	[[nodiscard]] const QStringList &GetNames() const { return m_Names; }
	// Rows whose pictures are wanted (shown, or asked for and not yet made), oldest first.
	[[nodiscard]] std::vector<int> TakeWanted();

	static constexpr int ROLE_NAME = Qt::UserRole;

private:
	QStringList m_Names;
	std::vector<QIcon> m_Pictures;
	mutable std::mutex m_Mutex;
	mutable std::vector<int> m_Wanted;
	int m_nSize = 64;
};

class CMaterialBrowser : public QDialog
{
	Q_OBJECT

public:
	// used: the map's materials, for "Only used textures"; szCurrent: picked to start with.
	CMaterialBrowser( const xv::vfs::CGameInfo &game, QStringList used, const QString &szCurrent, QWidget *pParent = nullptr );
	~CMaterialBrowser() override;

	// Runs the browser; the material picked (as Hammer writes it), if one was.
	static std::optional<QString> Pick( QWidget *pParent, const xv::vfs::CGameInfo &game, const QStringList &used, const QString &szCurrent );
	[[nodiscard]] QString GetPicked() const { return m_szPicked; }
	[[nodiscard]] CMaterialList &GetList() { return *m_pList; }
	[[nodiscard]] QListView *GetView() const { return m_pView; }
	// Waits for the names to be listed (tests).
	[[nodiscard]] bool IsListed() const { return m_bListed; }
	// Where the pictures are kept between runs.
	[[nodiscard]] static QString CacheFolder();

private:
	void Run();
	void ApplyFilter();
	void PickCurrent();

	xv::vfs::CGameInfo m_Game;
	QStringList m_Used;
	QString m_szPicked;
	CMaterialList *m_pList = nullptr;
	QSortFilterProxyModel *m_pFilter = nullptr;
	QListView *m_pView = nullptr;
	QLineEdit *m_pWords = nullptr;
	QComboBox *m_pSize = nullptr;
	QCheckBox *m_pOnlyUsed = nullptr;
	std::thread m_Worker;
	std::atomic<bool> m_bStop{ false };
	std::atomic<bool> m_bListed{ false };
};
