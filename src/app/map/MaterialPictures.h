//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Material pictures shared by the browser and Face Edit. Only QImages cross worker
//          threads; GUI consumers create their pixmaps. The cache also keeps source dimensions.
//
//===========================================================================//
#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include "vfs/Vfs.h"

struct MaterialPicture_t
{
	QImage m_Image;
	QSize m_Size;
};

[[nodiscard]] QString MaterialPictureCacheFolder();
[[nodiscard]] MaterialPicture_t LoadMaterialPicture( const xv::vfs::CFileSystem &fileSystem, const QString &szGame, const QString &szName, int nPictureSize );

#include <QObject>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

// One consumer's latest request; superseded selections cannot display an old result.
class CMaterialPictureLoader : public QObject
{
	Q_OBJECT
public:
	explicit CMaterialPictureLoader( QObject *pParent = nullptr );
	~CMaterialPictureLoader() override;
	void Request( const xv::vfs::CGameInfo &game, const QString &szName );
	void Cancel();
signals:
	void Ready( const QImage &image, QSize size );

private:
	struct Job_t
	{
		xv::vfs::CGameInfo m_Game;
		QString m_szName;
		std::uint64_t m_nGeneration;
	};
	void Run();
	std::mutex m_Mutex;
	std::condition_variable m_Wake;
	std::optional<Job_t> m_Job;
	std::thread m_Worker;
	bool m_bStop = false;
	std::uint64_t m_nGeneration = 0;
};
