//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See PictureCache.h.
//
//===========================================================================//
#include "PictureCache.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>

#include <QFile>
#include <QSaveFile>
#include <QtEndian>

#include <jxl/decode.h>
#include <jxl/encode.h>

namespace
{
constexpr char SOURCE_BOX[ 4 ] = { 'v', 't', 's', 'z' };
constexpr JxlPixelFormat RGBA{ 4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0 };
constexpr int MAX_DIMENSION = 4096; // Cache pictures are thumbnails, even for larger source textures.
constexpr qint64 MAX_FILE_SIZE = 64 * 1024 * 1024;

//-----------------------------------------------------------------------------
// Purpose: Read the RGBA pixels and, for a material, the size of its source texture.
//-----------------------------------------------------------------------------
QImage ReadJxl( const QString &szFile, QSize *pSourceSize )
{
	QFile file( szFile );
	if ( !file.open( QIODevice::ReadOnly ) || file.size() > MAX_FILE_SIZE )
		return {};
	const QByteArray bytes = file.readAll();
	std::unique_ptr<JxlDecoder, decltype( &JxlDecoderDestroy )> pDecoder( JxlDecoderCreate( nullptr ), JxlDecoderDestroy );
	const int nEvents = JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE | ( pSourceSize ? JXL_DEC_BOX | JXL_DEC_BOX_COMPLETE : 0 );
	if ( !pDecoder || JxlDecoderSubscribeEvents( pDecoder.get(), nEvents ) != JXL_DEC_SUCCESS ||
		JxlDecoderSetInput( pDecoder.get(), reinterpret_cast<const std::uint8_t *>( bytes.constData() ), bytes.size() ) != JXL_DEC_SUCCESS )
		return {};
	JxlDecoderCloseInput( pDecoder.get() );
	QImage image;
	std::array<std::uint8_t, 8> source{};
	QSize sourceSize;
	bool bComplete = false;
	while ( true )
	{
		switch ( JxlDecoderProcessInput( pDecoder.get() ) )
		{
			case JXL_DEC_BASIC_INFO:
			{
				JxlBasicInfo info;
				if ( JxlDecoderGetBasicInfo( pDecoder.get(), &info ) != JXL_DEC_SUCCESS || info.have_animation ||
					info.xsize > MAX_DIMENSION || info.ysize > MAX_DIMENSION )
					return {};
				image = QImage( static_cast<int>( info.xsize ), static_cast<int>( info.ysize ), QImage::Format_RGBA8888 );
				break;
			}
			case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
				if ( image.isNull() || JxlDecoderSetImageOutBuffer( pDecoder.get(), &RGBA, image.bits(), image.sizeInBytes() ) != JXL_DEC_SUCCESS )
					return {};
				break;
			case JXL_DEC_BOX:
			{
				JxlBoxType type;
				if ( JxlDecoderGetBoxType( pDecoder.get(), type, JXL_FALSE ) == JXL_DEC_SUCCESS && std::memcmp( type, SOURCE_BOX, sizeof( type ) ) == 0 &&
					JxlDecoderSetBoxBuffer( pDecoder.get(), source.data(), source.size() ) != JXL_DEC_SUCCESS )
					return {};
				break;
			}
			case JXL_DEC_BOX_COMPLETE:
				if ( JxlDecoderReleaseBoxBuffer( pDecoder.get() ) != 0 )
					return {};
				sourceSize = QSize( qFromBigEndian<qint32>( source.data() ), qFromBigEndian<qint32>( source.data() + 4 ) );
				break;
			case JXL_DEC_FULL_IMAGE:
				bComplete = true;
				break;
			case JXL_DEC_SUCCESS:
				if ( pSourceSize )
					*pSourceSize = sourceSize;
				return bComplete ? image : QImage{};
			default:
				return {};
		}
	}
}
} // namespace

//-----------------------------------------------------------------------------
// Purpose: Read the current version, migrating a usable PNG as it is requested.
//-----------------------------------------------------------------------------
QImage ReadCachedPicture( const QString &szStem, QSize *pSourceSize )
{
	const QImage picture = ReadJxl( szStem + QStringLiteral( ".jxl" ), pSourceSize );
	if ( !picture.isNull() && ( !pSourceSize || pSourceSize->isValid() ) )
		return picture;
	const QString szOld = szStem + QStringLiteral( ".png" );
	const QImage legacy( szOld );
	if ( legacy.isNull() )
		return {};
	if ( pSourceSize )
		*pSourceSize = QSize( legacy.text( QStringLiteral( "source-width" ) ).toInt(), legacy.text( QStringLiteral( "source-height" ) ).toInt() );
	if ( WriteCachedPicture( szStem, legacy, pSourceSize ? *pSourceSize : QSize{} ) )
		QFile::remove( szOld );
	return legacy;
}

//-----------------------------------------------------------------------------
// Purpose: Keep an exact copy of the preview, including alpha, with an atomic file replacement.
//-----------------------------------------------------------------------------
bool WriteCachedPicture( const QString &szStem, const QImage &picture, QSize sourceSize )
{
	const bool bAlpha = picture.hasAlphaChannel();
	const QImage image = picture.convertToFormat( bAlpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888 );
	const JxlPixelFormat pixels = bAlpha ? RGBA : JxlPixelFormat{ 3, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 4 };
	if ( image.isNull() || image.width() > MAX_DIMENSION || image.height() > MAX_DIMENSION )
		return false;
	std::unique_ptr<JxlEncoder, decltype( &JxlEncoderDestroy )> pEncoder( JxlEncoderCreate( nullptr ), JxlEncoderDestroy );
	if ( !pEncoder )
		return false;
	JxlBasicInfo info;
	JxlEncoderInitBasicInfo( &info );
	info.xsize = static_cast<std::uint32_t>( image.width() );
	info.ysize = static_cast<std::uint32_t>( image.height() );
	info.bits_per_sample = 8;
	info.alpha_bits = bAlpha ? 8 : 0;
	info.num_extra_channels = bAlpha ? 1 : 0;
	info.uses_original_profile = JXL_TRUE; // Keep the stored sRGB values losslessly.
	JxlColorEncoding color;
	JxlColorEncodingSetToSRGB( &color, JXL_FALSE );
	if ( JxlEncoderSetBasicInfo( pEncoder.get(), &info ) != JXL_ENC_SUCCESS ||
		JxlEncoderSetColorEncoding( pEncoder.get(), &color ) != JXL_ENC_SUCCESS )
		return false;

	if ( sourceSize.isValid() )
	{
		std::array<std::uint8_t, 8> source;
		qToBigEndian<qint32>( sourceSize.width(), source.data() );
		qToBigEndian<qint32>( sourceSize.height(), source.data() + 4 );
		if ( JxlEncoderUseBoxes( pEncoder.get() ) != JXL_ENC_SUCCESS ||
			JxlEncoderAddBox( pEncoder.get(), SOURCE_BOX, source.data(), source.size(), JXL_FALSE ) != JXL_ENC_SUCCESS )
			return false;
	}

	JxlEncoderFrameSettings *pFrame = JxlEncoderFrameSettingsCreate( pEncoder.get(), nullptr );
	if ( !pFrame || JxlEncoderSetFrameLossless( pFrame, JXL_TRUE ) != JXL_ENC_SUCCESS ||
		JxlEncoderFrameSettingsSetOption( pFrame, JXL_ENC_FRAME_SETTING_EFFORT, bAlpha ? 3 : 5 ) != JXL_ENC_SUCCESS ||
		JxlEncoderFrameSettingsSetOption( pFrame, JXL_ENC_FRAME_SETTING_KEEP_INVISIBLE, 1 ) != JXL_ENC_SUCCESS ||
		JxlEncoderAddImageFrame( pFrame, &pixels, image.constBits(), image.sizeInBytes() ) != JXL_ENC_SUCCESS )
		return false;
	JxlEncoderCloseInput( pEncoder.get() );

	QSaveFile file( szStem + QStringLiteral( ".jxl" ) );
	if ( !file.open( QIODevice::WriteOnly ) )
		return false;
	std::array<std::uint8_t, 16384> buffer;
	JxlEncoderStatus nStatus;
	do
	{
		std::uint8_t *pNext = buffer.data();
		std::size_t nAvailable = buffer.size();
		nStatus = JxlEncoderProcessOutput( pEncoder.get(), &pNext, &nAvailable );
		const auto nWritten = static_cast<qint64>( buffer.size() - nAvailable );
		if ( file.write( reinterpret_cast<const char *>( buffer.data() ), nWritten ) != nWritten )
			return false;
	}
	while ( nStatus == JXL_ENC_NEED_MORE_OUTPUT );
	return nStatus == JXL_ENC_SUCCESS && file.commit();
}
