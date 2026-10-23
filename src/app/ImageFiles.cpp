//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See ImageFiles.h.
//
//===========================================================================//
#include "ImageFiles.h"

#include <cstring>

#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include "texture/Vtf.h"

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsVtf( const QString &szName )
{
	return szName.endsWith( u".vtf", Qt::CaseInsensitive );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Image files the skybox editor and the preview read: VTF, plus what stb reads (PNG,
//          JPEG, TGA,
//          BMP, PSD, GIF, HDR, PNM).
//-----------------------------------------------------------------------------
bool IsImageFile( const QString &szName )
{
	const QString szSuffix = QFileInfo{ szName }.suffix().toLower();
	return szSuffix == u"vtf" || xv::texture::IsImageExtension( szSuffix.toStdString() );
}

//-----------------------------------------------------------------------------
// Purpose: name decides VTF or not; the content decides the rest.
//-----------------------------------------------------------------------------
std::optional<xv::texture::CImage> DecodeImageFile( const QString &szName, std::span<const std::byte> data, QString *pError )
{
	std::string szProblem;
	std::optional<xv::texture::CImage> image;
	if ( IsVtf( szName ) )
	{
		if ( const auto vtf = xv::texture::ReadVtf( data, &szProblem ) )
		{
			image = vtf->Decode( 0, 0, 0, &szProblem );
		}
	}
	else
	{
		image = xv::texture::LoadImage( data, &szProblem );
	}
	if ( !image && pError )
	{
		*pError = QString::fromStdString( szProblem );
	}
	return image;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<xv::texture::CImage> ReadImageFile( const QString &szPath, QString *pError )
{
	QFile file{ szPath };
	if ( !file.open( QIODevice::ReadOnly ) )
	{
		if ( pError )
		{
			*pError = file.errorString();
		}
		return std::nullopt;
	}
	const QByteArray data = file.readAll();
	return DecodeImageFile( szPath, { reinterpret_cast<const std::byte *>( data.constData() ), static_cast<std::size_t>( data.size() ) }, pError );
}

//-----------------------------------------------------------------------------
// Purpose: A tooltip-sized description of an image file: size, and for VTFs version, format,
//          mipmaps, flags.
//-----------------------------------------------------------------------------
QString DescribeImageFile( const QString &szName, std::span<const std::byte> data )
{
	if ( IsVtf( szName ) )
	{
		std::string szError;
		const auto vtf = xv::texture::ReadVtf( data, &szError );
		if ( !vtf )
		{
			return QString::fromStdString( szError );
		}
		const auto &info = vtf->m_Info;
		QStringList flags;
		for ( const std::string &szFlag : xv::texture::FlagNames( info.m_nFlags ) )
		{
			flags << QString::fromStdString( szFlag );
		}
		QString szText = QStringLiteral( "%1×%2 · %3 · VTF %4.%5 · %6" ).arg( info.m_nWidth ).arg( info.m_nHeight ).arg( QString::fromUtf8( xv::texture::FormatName( info.m_nFormat ) ) ).arg( info.m_nVersionMajor ).arg( info.m_nVersionMinor ).arg( info.m_nMipCount == 1 ? QStringLiteral( "no mipmaps" ) : QStringLiteral( "%1 mipmaps" ).arg( info.m_nMipCount ) );
		if ( info.m_nFrames > 1 )
		{
			szText += QStringLiteral( " · %1 frames" ).arg( info.m_nFrames );
		}
		if ( info.m_nFaces > 1 )
		{
			szText += QStringLiteral( " · %1 faces" ).arg( info.m_nFaces );
		}
		if ( !flags.isEmpty() )
		{
			szText += QStringLiteral( " · " ) + flags.join( u' ' );
		}
		return szText;
	}
	const auto image = xv::texture::LoadImage( data );
	return image ? QStringLiteral( "%1×%2" ).arg( image->m_nWidth ).arg( image->m_nHeight ) : QStringLiteral( "not an image this app can read" );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QImage ToQImage( const xv::texture::CImage &image )
{
	QImage out( image.m_nWidth, image.m_nHeight, QImage::Format_RGBA8888 );
	for ( int y = 0; y < image.m_nHeight; y++ )
	{
		std::memcpy( out.scanLine( y ), image.Pixel( 0, y ), static_cast<std::size_t>( image.m_nWidth ) * 4 );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::texture::CImage FromQImage( const QImage &image )
{
	const QImage rgba = image.convertToFormat( QImage::Format_RGBA8888 );
	xv::texture::CImage out{ rgba.width(), rgba.height() };
	for ( int y = 0; y < rgba.height(); y++ )
	{
		std::memcpy( out.Pixel( 0, y ), rgba.constScanLine( y ), static_cast<std::size_t>( rgba.width() ) * 4 );
	}
	return out;
}
