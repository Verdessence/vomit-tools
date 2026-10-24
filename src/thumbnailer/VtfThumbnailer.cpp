//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: VTF thumbnails for KDE's file manager (Dolphin) and file dialogs: a KIO thumbnail
//          plugin that draws a texture's first image, the smallest mipmap still big enough.
//
//===========================================================================//
#include <algorithm>
#include <cstring>

#include <QFile>
#include <QImage>
#include <QUrl>

#include <KIO/ThumbnailCreator>
#include <KPluginFactory>

#include "texture/Vtf.h"

class CVtfThumbnailer : public KIO::ThumbnailCreator
{
	Q_OBJECT

public:
	CVtfThumbnailer( QObject *pParent, const QVariantList &args )
		: KIO::ThumbnailCreator( pParent, args )
	{}

	KIO::ThumbnailResult create( const KIO::ThumbnailRequest &request ) override
	{
		QFile file{ request.url().toLocalFile() };
		if ( !file.open( QIODevice::ReadOnly ) || file.size() > 512 * 1024 * 1024 )
		{
			return KIO::ThumbnailResult::fail();
		}
		const QByteArray data = file.readAll();
		const auto vtf = xv::texture::ReadVtf( std::span{ reinterpret_cast<const std::byte *>( data.constData() ), static_cast<std::size_t>( data.size() ) } );
		if ( !vtf )
		{
			return KIO::ThumbnailResult::fail();
		}
		const xv::texture::VtfInfo_t &info = vtf->m_Info;
		const int nWanted = std::max( request.targetSize().width(), request.targetSize().height() );
		int nMip = 0;
		while ( nMip + 1 < info.m_nMipCount && std::max( info.m_nWidth, info.m_nHeight ) >> ( nMip + 1 ) >= nWanted )
		{
			nMip++;
		}
		const auto image = vtf->Decode( nMip );
		if ( !image )
		{
			return KIO::ThumbnailResult::fail();
		}
		QImage out( image->m_nWidth, image->m_nHeight, QImage::Format_RGBA8888 );
		for ( int y = 0; y < image->m_nHeight; y++ )
		{
			std::memcpy( out.scanLine( y ), image->Pixel( 0, y ), static_cast<std::size_t>( image->m_nWidth ) * 4 );
		}
		return KIO::ThumbnailResult::pass( out.scaled( request.targetSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation ) );
	}
};

K_PLUGIN_CLASS_WITH_JSON( CVtfThumbnailer, "vomit-tools-thumbnail.json" )

#include "VtfThumbnailer.moc"
