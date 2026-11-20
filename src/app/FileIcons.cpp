//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See FileIcons.h. File icons come from the desktop's icon theme, with fallbacks down
//          to the style's own, so a theme without one name still shows something.
//
//===========================================================================//
#include "FileIcons.h"

#include <utility>

#include <algorithm>
#include <array>
#include <initializer_list>

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QMimeDatabase>
#include <QStyle>

namespace
{

struct Kind_t
{
	const char *m_pszExtensions; // space separated, lower case
	const char *m_pszKind;
	std::initializer_list<const char *> m_IconNames;
	QStyle::StandardPixmap m_nFallback = QStyle::SP_FileIcon;
};

// The game's own formats first; anything else goes to the system's MIME database.
const std::array KINDS{
	Kind_t{ "vmt", QT_TRANSLATE_NOOP( "FileIcons", "Material" ), { "text-x-generic" } },
	Kind_t{ "vtf", QT_TRANSLATE_NOOP( "FileIcons", "Texture" ), { "image-x-generic" } },
	Kind_t{ "mdl", QT_TRANSLATE_NOOP( "FileIcons", "Model" ), { "application-x-model", "model", "x-office-drawing", "image-x-generic" } },
	Kind_t{ "vvd vtx phy ani", QT_TRANSLATE_NOOP( "FileIcons", "Model data" ), { "application-x-model", "model", "x-office-drawing", "application-octet-stream" } },
	Kind_t{ "pcf", QT_TRANSLATE_NOOP( "FileIcons", "Particles" ), { "applications-graphics", "image-x-generic" } },
	Kind_t{ "bsp", QT_TRANSLATE_NOOP( "FileIcons", "Map" ), { "applications-games", "application-x-executable" }, QStyle::SP_DriveHDIcon },
	Kind_t{ "vmf vmm vmx", QT_TRANSLATE_NOOP( "FileIcons", "Map source" ), { "text-x-generic" } },
	Kind_t{ "vpk", QT_TRANSLATE_NOOP( "FileIcons", "Archive" ), { "package-x-generic", "application-x-archive" } },
	Kind_t{ "nut", QT_TRANSLATE_NOOP( "FileIcons", "Squirrel script" ), { "text-x-script", "text-x-generic" } },
	Kind_t{ "res cfg rad lst vdf manifest", QT_TRANSLATE_NOOP( "FileIcons", "Settings" ), { "text-x-generic" } },
	Kind_t{ "qc qci smd dmx fgd", QT_TRANSLATE_NOOP( "FileIcons", "Source text" ), { "text-x-generic" } },
	Kind_t{ "vcs", QT_TRANSLATE_NOOP( "FileIcons", "Shader" ), { "text-x-script", "application-octet-stream" } },
	Kind_t{ "raw", QT_TRANSLATE_NOOP( "FileIcons", "Colour correction" ), { "image-x-generic" } },
	Kind_t{ "nav ain lmp", QT_TRANSLATE_NOOP( "FileIcons", "Map data" ), { "application-octet-stream" } },
};

//-----------------------------------------------------------------------------
// Purpose: The first of the theme's names it has, else the style's own picture.
//-----------------------------------------------------------------------------
QIcon ThemeIcon( std::initializer_list<const char *> names, QStyle::StandardPixmap nFallback )
{
	for ( const char *pszName : names )
	{
		const QString szName = QString::fromLatin1( pszName );
		if ( QIcon::hasThemeIcon( szName ) )
		{
			return QIcon::fromTheme( szName );
		}
	}
	return QApplication::style()->standardIcon( nFallback );
}

//-----------------------------------------------------------------------------
// Purpose: The table's entry for an extension, or none.
//-----------------------------------------------------------------------------
const Kind_t *KindFor( const QString &szExtension )
{
	for ( const Kind_t &kind : KINDS )
	{
		for ( QStringView szOne : QStringView{ QString::fromLatin1( kind.m_pszExtensions ) }.split( u' ' ) )
		{
			if ( szOne == szExtension )
			{
				return &kind;
			}
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: The extension after the last dot, lower case; empty for none.
//-----------------------------------------------------------------------------
QString ExtensionOf( QStringView szName )
{
	const qsizetype nDot = szName.lastIndexOf( u'.' );
	return nDot <= 0 ? QString{} : szName.mid( nDot + 1 ).toString().toLower();
}

struct Cached_t
{
	QIcon m_Icon;
	QString m_szKind;
};

//-----------------------------------------------------------------------------
// Purpose: An extension's icon and kind, worked out once (trees ask for every row they draw).
//-----------------------------------------------------------------------------
const Cached_t &Lookup( QStringView szName )
{
	static QHash<QString, Cached_t> cache;
	const QString szExtension = ExtensionOf( szName );
	if ( const auto it = cache.constFind( szExtension ); it != cache.constEnd() )
	{
		return *it;
	}
	Cached_t entry;
	if ( const Kind_t *pKind = KindFor( szExtension ) )
	{
		entry.m_Icon = ThemeIcon( pKind->m_IconNames, pKind->m_nFallback );
		entry.m_szKind = QCoreApplication::translate( "FileIcons", pKind->m_pszKind );
	}
	else if ( szExtension.isEmpty() )
	{
		entry.m_Icon = ThemeIcon( { "text-x-generic" }, QStyle::SP_FileIcon );
		entry.m_szKind = QCoreApplication::translate( "FileIcons", "File" );
	}
	else
	{
		const QMimeType mime = QMimeDatabase{}.mimeTypeForFile( QStringLiteral( "x." ) + szExtension, QMimeDatabase::MatchExtension );
		const QByteArray icon = mime.iconName().toLatin1();
		const QByteArray generic = mime.genericIconName().toLatin1();
		entry.m_Icon = mime.isDefault() ? ThemeIcon( { "text-x-generic" }, QStyle::SP_FileIcon ) : ThemeIcon( { icon.constData(), generic.constData() }, QStyle::SP_FileIcon );
		// The MIME database's comments are long ("Microsoft Windows audio"); the extension says it shorter.
		entry.m_szKind = mime.isDefault() ? QCoreApplication::translate( "FileIcons", "%1 file" ).arg( szExtension.toUpper() ) : mime.comment();
	}
	return *cache.insert( szExtension, entry );
}

class CPaletteIconEngine : public QIconEngine
{
public:
	[[nodiscard]] QPixmap pixmap( const QSize &size, QIcon::Mode nMode, QIcon::State nState ) override;
};

//-----------------------------------------------------------------------------
// Purpose: Leave the area outside the icon transparent when Qt requests a pixmap.
//-----------------------------------------------------------------------------
QPixmap CPaletteIconEngine::pixmap( const QSize &size, QIcon::Mode nMode, QIcon::State nState )
{
	QPixmap picture( size );
	picture.fill( Qt::transparent );
	{
		QPainter painter( &picture );
		paint( &painter, QRect( QPoint{}, size ), nMode, nState );
	}
	return picture;
}

class CFolderIconEngine final : public CPaletteIconEngine
{
public:
	[[nodiscard]] QIconEngine *clone() const override;
	void paint( QPainter *pPainter, const QRect &rect, QIcon::Mode nMode, QIcon::State nState ) override;
};

//-----------------------------------------------------------------------------
// Purpose: Qt copies icon engines when an icon is changed.
//-----------------------------------------------------------------------------
QIconEngine *CFolderIconEngine::clone() const
{
	return new CFolderIconEngine;
}

//-----------------------------------------------------------------------------
// Purpose: An outline that stays readable even when the icon theme and palette disagree.
//-----------------------------------------------------------------------------
void CFolderIconEngine::paint( QPainter *pPainter, const QRect &rect, QIcon::Mode nMode, QIcon::State nState )
{
	const QPalette palette = QApplication::palette( "QTreeView" );
	const QPalette::ColorGroup nGroup = nMode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active;
	const QColor foreground = palette.color( nGroup, nMode == QIcon::Selected ? QPalette::HighlightedText : QPalette::Text );
	const qreal flSize = std::min( rect.width(), rect.height() );
	pPainter->save();
	pPainter->setRenderHint( QPainter::Antialiasing );
	pPainter->translate( rect.x() + ( rect.width() - flSize ) / 2, rect.y() + ( rect.height() - flSize ) / 2 );
	pPainter->scale( flSize / 16, flSize / 16 );
	pPainter->setPen( QPen( foreground, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
	QPainterPath folder;
	folder.moveTo( 1.5, 13 );
	folder.lineTo( 1.5, 3.5 );
	folder.lineTo( 6, 3.5 );
	folder.lineTo( 7.5, 5 );
	folder.lineTo( 13, 5 );
	if ( nState == QIcon::On )
	{
		folder.lineTo( 13, 7 );
		folder.moveTo( 1.5, 13 );
		folder.lineTo( 4, 7 );
		folder.lineTo( 14.5, 7 );
		folder.lineTo( 12.5, 13 );
	}
	else
	{
		folder.lineTo( 13, 13 );
	}
	folder.lineTo( 1.5, 13 );
	pPainter->drawPath( folder );
	pPainter->restore();
}

class CForegroundIconEngine final : public CPaletteIconEngine
{
public:
	explicit CForegroundIconEngine( QIcon icon );
	[[nodiscard]] QIconEngine *clone() const override;
	void paint( QPainter *pPainter, const QRect &rect, QIcon::Mode nMode, QIcon::State nState ) override;

private:
	QIcon m_Icon;
};

//-----------------------------------------------------------------------------
// Purpose: Use the desktop icon's shape with the application's foreground colour.
//-----------------------------------------------------------------------------
CForegroundIconEngine::CForegroundIconEngine( QIcon icon )
	: m_Icon( std::move( icon ) )
{}

//-----------------------------------------------------------------------------
// Purpose: Qt copies icon engines when an icon is changed.
//-----------------------------------------------------------------------------
QIconEngine *CForegroundIconEngine::clone() const
{
	return new CForegroundIconEngine( m_Icon );
}

//-----------------------------------------------------------------------------
// Purpose: Match the workspace header even with a desktop icon theme of the opposite tone.
//-----------------------------------------------------------------------------
void CForegroundIconEngine::paint( QPainter *pPainter, const QRect &rect, QIcon::Mode nMode, QIcon::State nState )
{
	const qreal flRatio = pPainter->device()->devicePixelRatioF();
	QPixmap pixmap = m_Icon.pixmap( rect.size(), flRatio, QIcon::Normal, nState );
	if ( pixmap.isNull() )
		return;
	const auto nGroup = nMode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active;
	const QColor color = QApplication::palette().color( nGroup, QPalette::WindowText );
	{
		QPainter tint( &pixmap );
		tint.setCompositionMode( QPainter::CompositionMode_SourceIn );
		tint.fillRect( QRectF( QPointF{}, pixmap.deviceIndependentSize() ), color );
	}
	pPainter->drawPixmap( rect.topLeft() + QPointF( ( rect.width() - pixmap.deviceIndependentSize().width() ) / 2, ( rect.height() - pixmap.deviceIndependentSize().height() ) / 2 ), pixmap );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The folder, closed; open (the icon's On state) while its row is expanded.
//-----------------------------------------------------------------------------
QIcon FolderIcon()
{
	static const QIcon icon( new CFolderIconEngine );
	return icon;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QIcon FileIcon( QStringView szName )
{
	return Lookup( szName ).m_Icon;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString FileKind( QStringView szName )
{
	return Lookup( szName ).m_szKind;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString FolderKind()
{
	return QCoreApplication::translate( "FileIcons", "Folder" );
}

//-----------------------------------------------------------------------------
// Purpose: Palette-aware decoration for monochrome navigation controls.
//-----------------------------------------------------------------------------
QIcon ForegroundIcon( QIcon icon )
{
	return QIcon( new CForegroundIconEngine( std::move( icon ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: QFileSystemModel's icons for places with no file (drives, the computer).
//-----------------------------------------------------------------------------
QIcon CFileIconProvider::icon( IconType nType ) const
{
	return nType == Folder ? FolderIcon() : QApplication::style()->standardIcon( nType == Drive ? QStyle::SP_DriveHDIcon : QStyle::SP_FileIcon );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QIcon CFileIconProvider::icon( const QFileInfo &info ) const
{
	return info.isDir() ? FolderIcon() : FileIcon( info.fileName() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CFileIconProvider::type( const QFileInfo &info ) const
{
	return info.isDir() ? FolderKind() : FileKind( info.fileName() );
}
