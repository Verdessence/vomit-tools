//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's Texture Browser: the game's materials as pictures, filtered by words in
//          their names (every word must match) or to those the map uses, at 32 to 512 pixels
//          (Hammer++'s sizes); a double click picks one. See-through materials show over a
//          checkerboard, as Hammer++ shows their alpha. Pictures are made on a thread of its
//          own and kept on disk between runs.
//
//===========================================================================//
#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <QAbstractListModel>
#include <QDialog>
#include <QElapsedTimer>
#include <QIcon>
#include <QImage>
#include <limits>
class QTimer;
#include <QStringList>

#include "vfs/Vfs.h"

class QCheckBox;
class QComboBox;
class QLabel;
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
	// Which rows are sprites (see CMaterialBrowser), in the names' order.
	void SetSprites( std::vector<bool> sprites );
	// A picture made at nPictureSize; one made for the size before a change is dropped.
	void SetPicture( int nRow, const QImage &picture, int nPictureSize );
	// The size rows are shown at; past 128 the pictures are made again, bigger.
	void SetSize( int nSize );
	[[nodiscard]] int GetPictureSize() const { return m_nPictureSize; }
	[[nodiscard]] const QStringList &GetNames() const { return m_Names; }
	// Rows whose pictures are wanted (shown, or asked for and not yet made), oldest first.
	[[nodiscard]] std::vector<int> TakeWanted();

	static constexpr int ROLE_NAME = Qt::UserRole;
	static constexpr int ROLE_SPRITE = Qt::UserRole + 1;

private:
	QTimer *m_pNotify = nullptr;
	int m_nFirstPending = std::numeric_limits<int>::max();
	int m_nLastPending = -1;
	QStringList m_Names;
	std::vector<QIcon> m_Pictures;
	std::vector<bool> m_Sprites;
	mutable std::mutex m_Mutex;
	mutable std::vector<int> m_Wanted;
	int m_nSize = 64;
	std::atomic<int> m_nPictureSize{ 128 };
};

// The material under the mouse in the browser (or, away from the grid, the one picked) drawn
// live, as the material editor's preview draws it: its proxies animating, on a square or a ball,
// with its shader and its base texture's size. Each is read on a thread of the pane's own.
class CMaterialPreview : public QWidget
{
	Q_OBJECT

public:
	explicit CMaterialPreview( const xv::vfs::CGameInfo &game, QWidget *pParent = nullptr );
	~CMaterialPreview() override;

	// A material by its name as the browser shows it ("BRICK/BRICKWALL001A"); empty for none.
	void Show( const QString &szName );
	// What is drawn now: the name read (empty until it is), and the picture.
	[[nodiscard]] const QString &GetShown() const { return m_szShown; }
	[[nodiscard]] QImage GetPicture() const { return m_Picture; }
	[[nodiscard]] bool IsAnimated() const;

	struct Loaded_t;

private:
	void Load();
	void Render();

	xv::vfs::CGameInfo m_Game;
	QString m_szWanted;
	QString m_szShown;
	std::shared_ptr<const Loaded_t> m_pLoaded;
	QImage m_Picture;
	QLabel *m_pPicture = nullptr;
	QLabel *m_pName = nullptr;
	QLabel *m_pAbout = nullptr;
	QComboBox *m_pShape = nullptr;
	QTimer *m_pWait = nullptr;    // a hover settles before it is read
	QTimer *m_pAnimate = nullptr; // the proxies' frames
	QElapsedTimer m_Clock;
	// The reading thread: one material at a time, the newest asked for.
	std::thread m_Worker;
	std::mutex m_Mutex;
	std::condition_variable m_Wake;
	QString m_szQueued;
	bool m_bStop = false;
	std::atomic<int> m_nGeneration{ 0 };
};

class CMaterialBrowser : public QDialog
{
	Q_OBJECT

public:
	// used: the map's materials, for "Only used textures"; szCurrent: picked to start with.
	// bSprites: for a sprite key, which starts on "Only sprites" (materials drawn with the
	// Sprite or SpriteCard shader, and everything under sprites/), the rest a tick away.
	CMaterialBrowser( const xv::vfs::CGameInfo &game, QStringList used, const QString &szCurrent, QWidget *pParent = nullptr, bool bSprites = false );
	~CMaterialBrowser() override;

	// Runs the browser; the material picked (as Hammer writes it), if one was.
	static std::optional<QString> Pick( QWidget *pParent, const xv::vfs::CGameInfo &game, const QStringList &used, const QString &szCurrent );
	// The same for a sprite key: the value as sprite keys write it ("sprites/glow01.vmt").
	static std::optional<QString> PickSprite( QWidget *pParent, const xv::vfs::CGameInfo &game, const QStringList &used, const QString &szCurrent );
	[[nodiscard]] QString GetPicked() const { return m_szPicked; }
	[[nodiscard]] CMaterialList &GetList() { return *m_pList; }
	[[nodiscard]] QListView *GetView() const { return m_pView; }
	[[nodiscard]] CMaterialPreview *GetPreview() const { return m_pPreview; }
	// Waits for the names to be listed (tests).
	[[nodiscard]] bool IsListed() const { return m_bListed; }
	// Where the pictures are kept between runs.
	[[nodiscard]] static QString CacheFolder();

protected:
	bool eventFilter( QObject *pObject, QEvent *pEvent ) override;

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
	QCheckBox *m_pOnlySprites = nullptr;
	CMaterialPreview *m_pPreview = nullptr;
	bool m_bSprites = false;
	std::thread m_Worker;
	std::atomic<bool> m_bStop{ false };
	std::atomic<bool> m_bListed{ false };
};
