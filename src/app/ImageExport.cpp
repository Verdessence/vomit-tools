//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See ImageExport.h.
//
//===========================================================================//
#include "ImageExport.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include <QImageWriter>
#include <QSaveFile>

#include <jxl/encode.h>

#if defined( VOMIT_TOOLS_AVIF )
#include <avif/avif.h>
#endif

namespace
{

//-----------------------------------------------------------------------------
// Purpose: The bytes written to the file whole (a failed save leaves the old file as it was).
//-----------------------------------------------------------------------------
bool WriteBytes( const QString &szPath, const std::uint8_t *pData, std::size_t nSize, QString *pError )
{
	QSaveFile file( szPath );
	if ( !file.open( QIODevice::WriteOnly ) || file.write( reinterpret_cast<const char *>( pData ), static_cast<qint64>( nSize ) ) != static_cast<qint64>( nSize ) || !file.commit() )
	{
		if ( pError )
			*pError = file.errorString();
		return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: JPEG XL through libjxl: lossless, or lossy at the distance the quality gives; the
//          effort as asked; alpha kept only when the picture uses it.
//-----------------------------------------------------------------------------
bool ExportJxl( const QImage &source, const QString &szPath, const ImageExportOptions_t &options, QString *pError )
{
	const bool bAlpha = source.hasAlphaChannel();
	const QImage image = source.convertToFormat( bAlpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888 );
	const auto fail = [ pError ]( const char *pszWhat ) {
		if ( pError )
			*pError = QObject::tr( "the JPEG XL encoder failed (%1)" ).arg( QLatin1String( pszWhat ) );
		return false;
	};
	std::unique_ptr<JxlEncoder, decltype( &JxlEncoderDestroy )> pEncoder( JxlEncoderCreate( nullptr ), JxlEncoderDestroy );
	if ( !pEncoder )
		return fail( "create" );
	JxlBasicInfo info;
	JxlEncoderInitBasicInfo( &info );
	info.xsize = static_cast<std::uint32_t>( image.width() );
	info.ysize = static_cast<std::uint32_t>( image.height() );
	info.bits_per_sample = 8;
	info.num_color_channels = 3;
	info.alpha_bits = bAlpha ? 8 : 0;
	info.num_extra_channels = bAlpha ? 1 : 0;
	info.uses_original_profile = options.m_bLossless ? JXL_TRUE : JXL_FALSE;
	JxlColorEncoding color;
	JxlColorEncodingSetToSRGB( &color, JXL_FALSE );
	if ( JxlEncoderSetBasicInfo( pEncoder.get(), &info ) != JXL_ENC_SUCCESS || JxlEncoderSetColorEncoding( pEncoder.get(), &color ) != JXL_ENC_SUCCESS )
		return fail( "basic info" );
	JxlEncoderFrameSettings *pFrame = JxlEncoderFrameSettingsCreate( pEncoder.get(), nullptr );
	if ( !pFrame || JxlEncoderFrameSettingsSetOption( pFrame, JXL_ENC_FRAME_SETTING_EFFORT, std::clamp( options.m_nEffort, 1, 9 ) ) != JXL_ENC_SUCCESS )
		return fail( "effort" );
	if ( options.m_bLossless ? JxlEncoderSetFrameLossless( pFrame, JXL_TRUE ) != JXL_ENC_SUCCESS
							 : JxlEncoderSetFrameDistance( pFrame, JxlEncoderDistanceFromQuality( static_cast<float>( std::clamp( options.m_nQuality, 0, 99 ) ) ) ) != JXL_ENC_SUCCESS )
		return fail( "quality" );
	// Rows of a QImage may be padded; the encoder wants them packed, aligned to the stride given.
	const JxlPixelFormat pixels{ bAlpha ? 4u : 3u, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, static_cast<std::size_t>( image.bytesPerLine() ) };
	if ( JxlEncoderAddImageFrame( pFrame, &pixels, image.constBits(), static_cast<std::size_t>( image.sizeInBytes() ) ) != JXL_ENC_SUCCESS )
		return fail( "frame" );
	JxlEncoderCloseInput( pEncoder.get() );
	std::vector<std::uint8_t> bytes( 1 << 16 );
	std::uint8_t *pNext = bytes.data();
	std::size_t nAvailable = bytes.size();
	JxlEncoderStatus nStatus;
	while ( ( nStatus = JxlEncoderProcessOutput( pEncoder.get(), &pNext, &nAvailable ) ) == JXL_ENC_NEED_MORE_OUTPUT )
	{
		const std::size_t nUsed = static_cast<std::size_t>( pNext - bytes.data() );
		bytes.resize( bytes.size() * 2 );
		pNext = bytes.data() + nUsed;
		nAvailable = bytes.size() - nUsed;
	}
	if ( nStatus != JXL_ENC_SUCCESS )
		return fail( "output" );
	return WriteBytes( szPath, bytes.data(), static_cast<std::size_t>( pNext - bytes.data() ), pError );
}

#if defined( VOMIT_TOOLS_AVIF )
//-----------------------------------------------------------------------------
// Purpose: AVIF through libavif: lossless as full-resolution colour with the identity matrix,
//          else 4:4:4 at the quality asked (paths are thin coloured lines, which 4:2:0 smears);
//          the speed from the effort.
//-----------------------------------------------------------------------------
bool ExportAvif( const QImage &source, const QString &szPath, const ImageExportOptions_t &options, QString *pError )
{
	const QImage image = source.convertToFormat( QImage::Format_RGBA8888 );
	const auto fail = [ pError ]( const char *pszWhat, avifResult nResult ) {
		if ( pError )
			*pError = QObject::tr( "the AVIF encoder failed (%1: %2)" ).arg( QLatin1String( pszWhat ), QLatin1String( avifResultToString( nResult ) ) );
		return false;
	};
	std::unique_ptr<avifImage, decltype( &avifImageDestroy )> pImage( avifImageCreate( static_cast<std::uint32_t>( image.width() ), static_cast<std::uint32_t>( image.height() ), 8, AVIF_PIXEL_FORMAT_YUV444 ), avifImageDestroy );
	if ( !pImage )
		return fail( "image", AVIF_RESULT_OUT_OF_MEMORY );
	if ( options.m_bLossless )
		pImage->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
	avifRGBImage rgb;
	avifRGBImageSetDefaults( &rgb, pImage.get() );
	rgb.format = AVIF_RGB_FORMAT_RGBA;
	rgb.depth = 8;
	rgb.pixels = const_cast<std::uint8_t *>( image.constBits() );
	rgb.rowBytes = static_cast<std::uint32_t>( image.bytesPerLine() );
	if ( const avifResult nResult = avifImageRGBToYUV( pImage.get(), &rgb ); nResult != AVIF_RESULT_OK )
		return fail( "colour", nResult );
	std::unique_ptr<avifEncoder, decltype( &avifEncoderDestroy )> pEncoder( avifEncoderCreate(), avifEncoderDestroy );
	if ( !pEncoder )
		return fail( "encoder", AVIF_RESULT_OUT_OF_MEMORY );
	pEncoder->quality = options.m_bLossless ? AVIF_QUALITY_LOSSLESS : std::clamp( options.m_nQuality, 0, 99 );
	pEncoder->qualityAlpha = options.m_bLossless ? AVIF_QUALITY_LOSSLESS : std::clamp( options.m_nQuality + 10, 0, 99 );
	// Effort 1 (fast) to 9 (small) onto libavif's speed 9 to 1.
	pEncoder->speed = std::clamp( 11 - std::clamp( options.m_nEffort, 1, 9 ) - 1, AVIF_SPEED_SLOWEST, AVIF_SPEED_FASTEST );
	avifRWData output = AVIF_DATA_EMPTY;
	if ( const avifResult nResult = avifEncoderWrite( pEncoder.get(), pImage.get(), &output ); nResult != AVIF_RESULT_OK )
	{
		avifRWDataFree( &output );
		return fail( "write", nResult );
	}
	const bool bWritten = WriteBytes( szPath, output.data, output.size, pError );
	avifRWDataFree( &output );
	return bWritten;
}
#endif

//-----------------------------------------------------------------------------
// Purpose: Through a Qt image plugin (PNG; AVIF without libavif): the quality is all it takes.
//-----------------------------------------------------------------------------
bool ExportWithQt( const QImage &image, const QString &szPath, const char *pszFormat, int nQuality, QString *pError )
{
	QImageWriter writer( szPath, pszFormat );
	writer.setQuality( nQuality );
	if ( writer.write( image ) )
		return true;
	if ( pError )
		*pError = writer.errorString();
	return false;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CanExportImage( ImageExportFormat_t nFormat )
{
	switch ( nFormat )
	{
		case ImageExportFormat_t::IMAGE_EXPORT_FORMAT_JXL:
		case ImageExportFormat_t::IMAGE_EXPORT_FORMAT_PNG:
			return true;
		case ImageExportFormat_t::IMAGE_EXPORT_FORMAT_AVIF:
#if defined( VOMIT_TOOLS_AVIF )
			if ( avifCodecName( AVIF_CODEC_CHOICE_AUTO, AVIF_CODEC_FLAG_CAN_ENCODE ) )
				return true;
#endif
			return QImageWriter::supportedImageFormats().contains( "avif" );
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString ImageExportSuffix( ImageExportFormat_t nFormat )
{
	switch ( nFormat )
	{
		case ImageExportFormat_t::IMAGE_EXPORT_FORMAT_JXL: return QStringLiteral( "jxl" );
		case ImageExportFormat_t::IMAGE_EXPORT_FORMAT_AVIF: return QStringLiteral( "avif" );
		case ImageExportFormat_t::IMAGE_EXPORT_FORMAT_PNG: return QStringLiteral( "png" );
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString ImageExportName( ImageExportFormat_t nFormat )
{
	switch ( nFormat )
	{
		case ImageExportFormat_t::IMAGE_EXPORT_FORMAT_JXL: return QObject::tr( "JPEG XL" );
		case ImageExportFormat_t::IMAGE_EXPORT_FORMAT_AVIF: return QObject::tr( "AVIF" );
		case ImageExportFormat_t::IMAGE_EXPORT_FORMAT_PNG: return QObject::tr( "PNG" );
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool ExportImage( const QImage &image, const QString &szPath, const ImageExportOptions_t &options, QString *pError )
{
	if ( image.isNull() )
	{
		if ( pError )
			*pError = QObject::tr( "there is no picture" );
		return false;
	}
	switch ( options.m_nFormat )
	{
		case ImageExportFormat_t::IMAGE_EXPORT_FORMAT_JXL:
			return ExportJxl( image, szPath, options, pError );
		case ImageExportFormat_t::IMAGE_EXPORT_FORMAT_AVIF:
#if defined( VOMIT_TOOLS_AVIF )
			if ( avifCodecName( AVIF_CODEC_CHOICE_AUTO, AVIF_CODEC_FLAG_CAN_ENCODE ) )
				return ExportAvif( image, szPath, options, pError );
#endif
			return ExportWithQt( image, szPath, "avif", options.m_bLossless ? 100 : options.m_nQuality, pError );
		case ImageExportFormat_t::IMAGE_EXPORT_FORMAT_PNG:
			// Qt's PNG quality is the compression, backwards: 0 is the most.
			return ExportWithQt( image, szPath, "png", ( 9 - std::clamp( options.m_nPngLevel, 0, 9 ) ) * 10, pError );
	}
	return false;
}
