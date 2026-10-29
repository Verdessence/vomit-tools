//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MaterialPictures.h.
//
//===========================================================================//
#include "MaterialPictures.h"
#include <algorithm>
#include <QCryptographicHash>
#include <QDir>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>
#include "ImageFiles.h"
#include "map/MapResources.h"
#include "map/Materials.h"
#include "texture/Vtf.h"

namespace
{
//-----------------------------------------------------------------------------
// Purpose: A see-through material over a grey checkerboard (eight squares across), its alpha
//          showing; any other without its alpha, which is something else there (a mask). A
//          texture smaller than the picture is blown up in whole pixels.
//-----------------------------------------------------------------------------
QImage Composed( QImage picture, bool bSeeThrough, int nPictureSize )
{
	if ( !bSeeThrough )
	{
		// The colour as it is, the alpha ignored: before scaling, which premultiplies (and so
		// blacks out what the alpha hides).
		picture = picture.convertToFormat( QImage::Format_RGBA8888 );
		picture.reinterpretAsFormat( QImage::Format_RGBX8888 );
	}
	const int nLargest = std::max( picture.width(), picture.height() );
	if ( nLargest > 0 && nLargest < nPictureSize )
	{
		const int nTimes = nPictureSize / nLargest;
		picture = picture.scaled( picture.width() * nTimes, picture.height() * nTimes, Qt::IgnoreAspectRatio, Qt::FastTransformation );
	}
	QImage composed( picture.size(), QImage::Format_RGB32 );
	composed.fill( Qt::black );
	if ( bSeeThrough )
	{
		const int nSquare = std::max( 1, std::max( picture.width(), picture.height() ) / 8 );
		QPainter checker( &composed );
		for ( int y = 0; y < composed.height(); y += nSquare )
		{
			for ( int x = 0; x < composed.width(); x += nSquare )
			{
				checker.fillRect( x, y, nSquare, nSquare, ( ( x / nSquare + y / nSquare ) % 2 ) ? QColor( 102, 102, 102 ) : QColor( 153, 153, 153 ) );
			}
		}
	}
	QPainter painter( &composed );
	painter.drawImage( 0, 0, picture );
	return composed;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Versioned composed pictures, independent for materials from different installs.
//-----------------------------------------------------------------------------
QString MaterialPictureCacheFolder()
{
	return QStandardPaths::writableLocation( QStandardPaths::CacheLocation ) + QStringLiteral( "/material-pictures/v3" );
}

//-----------------------------------------------------------------------------
// Purpose: Read the shared cache, or decode a base texture off the GUI thread and atomically
//          cache its picture with its original dimensions for both consumers.
//-----------------------------------------------------------------------------
MaterialPicture_t LoadMaterialPicture( const xv::vfs::CFileSystem &fileSystem, const QString &szGame, const QString &szName, int nPictureSize )
{
	const QByteArray hash = QCryptographicHash::hash( ( szGame + QLatin1Char( '\n' ) + szName ).toUtf8(), QCryptographicHash::Sha1 ).toHex();
	const QString szFolder = MaterialPictureCacheFolder() + QStringLiteral( "/%1" ).arg( nPictureSize );
	const QString szFile = szFolder + QLatin1Char( '/' ) + QString::fromLatin1( hash ) + QStringLiteral( ".png" );
	MaterialPicture_t result;
	result.m_Image.load( szFile );
	if ( !result.m_Image.isNull() )
	{
		result.m_Size = QSize( result.m_Image.text( QStringLiteral( "source-width" ) ).toInt(), result.m_Image.text( QStringLiteral( "source-height" ) ).toInt() );
		return result;
	}
	const xv::material::Reader_t read = [ &fileSystem ]( std::string_view szPath ) { return fileSystem.Read( szPath ); };
	const auto info = xv::map::ReadMaterialInfo( szName.toStdString(), read );
	if ( const auto bytes = info.m_szTexture.empty() ? std::nullopt : fileSystem.Read( "materials/" + info.m_szTexture + ".vtf" ) )
	{
		if ( const auto vtf = xv::texture::ReadVtf( *bytes ) )
			result.m_Size = QSize( vtf->m_Info.m_nWidth, vtf->m_Info.m_nHeight );
		if ( const auto pTexture = ReadMapTexture( *bytes, nPictureSize, true ); pTexture && !pTexture->m_Mips.empty() )
		{
			result.m_Image = Composed( ToQImage( pTexture->m_Mips.front() ).copy(), info.IsSeeThrough() || info.m_bAlphaTest, nPictureSize );
			result.m_Image.setText( QStringLiteral( "source-width" ), QString::number( result.m_Size.width() ) );
			result.m_Image.setText( QStringLiteral( "source-height" ), QString::number( result.m_Size.height() ) );
			QDir().mkpath( szFolder );
			QSaveFile cache( szFile );
			if ( cache.open( QIODevice::WriteOnly ) && result.m_Image.save( &cache, "PNG" ) )
				cache.commit();
		}
	}
	if ( result.m_Image.isNull() )
	{
		result.m_Image = QImage( 16, 16, QImage::Format_RGB32 );
		result.m_Image.fill( QColor( 96, 96, 96 ) );
	}
	return result;
}

//-----------------------------------------------------------------------------
// Purpose: The sheet owns one loader, created without starting work until it has a game.
//-----------------------------------------------------------------------------
CMaterialPictureLoader::CMaterialPictureLoader( QObject *pParent )
	: QObject( pParent )
{
}

//-----------------------------------------------------------------------------
// Purpose: Finish the current decode before its request receiver is destroyed.
//-----------------------------------------------------------------------------
CMaterialPictureLoader::~CMaterialPictureLoader()
{
	{
		const std::lock_guard lock( m_Mutex );
		m_bStop = true;
	}
	m_Wake.notify_all();
	if ( m_Worker.joinable() )
		m_Worker.join();
}

//-----------------------------------------------------------------------------
// Purpose: Replace a pending request; only its generation may publish back to the sheet.
//-----------------------------------------------------------------------------
void CMaterialPictureLoader::Request( const xv::vfs::CGameInfo &game, const QString &szName )
{
	const std::uint64_t nGeneration = ++m_nGeneration;
	{
		const std::lock_guard lock( m_Mutex );
		m_Job = Job_t{ game, szName, nGeneration };
	}
	if ( !m_Worker.joinable() )
		m_Worker = std::thread( [ this ] { Run(); } );
	m_Wake.notify_one();
}

//-----------------------------------------------------------------------------
// Purpose: Blank or mixed selections discard both queued and already decoding pictures.
//-----------------------------------------------------------------------------
void CMaterialPictureLoader::Cancel()
{
	++m_nGeneration;
	const std::lock_guard lock( m_Mutex );
	m_Job.reset();
}

//-----------------------------------------------------------------------------
// Purpose: Mount once per game and deliver QImages through the receiver's GUI event queue.
//-----------------------------------------------------------------------------
void CMaterialPictureLoader::Run()
{
	std::unique_ptr<xv::vfs::CFileSystem> pFileSystem;
	std::filesystem::path mounted;
	while ( true )
	{
		std::optional<Job_t> job;
		{
			std::unique_lock lock( m_Mutex );
			m_Wake.wait( lock, [ this ] { return m_bStop || m_Job.has_value(); } );
			if ( m_bStop )
				return;
			job = std::move( m_Job );
			m_Job.reset();
		}
		if ( !pFileSystem || mounted != job->m_Game.m_GameDir )
		{
			pFileSystem = std::make_unique<xv::vfs::CFileSystem>();
			pFileSystem->Mount( job->m_Game );
			mounted = job->m_Game.m_GameDir;
		}
		const auto picture = LoadMaterialPicture( *pFileSystem, QString::fromStdString( mounted.string() ), job->m_szName, 128 );
		QMetaObject::invokeMethod( this, [ this, nGeneration = job->m_nGeneration, picture ] {
			if ( nGeneration == m_nGeneration ) emit Ready( picture.m_Image, picture.m_Size ); }, Qt::QueuedConnection );
	}
}
