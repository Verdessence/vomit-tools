//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See FileIcons.h. Icons come from the desktop's icon theme, each with fallbacks down
//          to the style's own, so a theme without one name still shows something.
//
//===========================================================================//
#include "FileIcons.h"

#include <array>
#include <initializer_list>

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
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

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The folder, closed; open (the icon's On state) while its row is expanded.
//-----------------------------------------------------------------------------
QIcon FolderIcon()
{
	static const QIcon icon = [] {
		QIcon folder = ThemeIcon( { "folder" }, QStyle::SP_DirClosedIcon );
		const QIcon open = ThemeIcon( { "folder-open", "folder" }, QStyle::SP_DirOpenIcon );
		QIcon both;
		for ( const int nSize : { 16, 22, 24, 32, 48 } )
		{
			both.addPixmap( folder.pixmap( nSize ), QIcon::Normal, QIcon::Off );
			both.addPixmap( open.pixmap( nSize ), QIcon::Normal, QIcon::On );
		}
		return both;
	}();
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
