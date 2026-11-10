//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See StateFormat.h.
//
//===========================================================================//
#include "StateFormat.h"

#include <climits>
#include <cmath>

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QIODevice>
#include <QRegularExpression>
#include <QVariant>

#include "conf/Conf.h"

namespace
{

// Strings that would read as something else start with this, doubled.
constexpr QChar MARK = u'@';
constexpr QLatin1StringView BYTES = QLatin1StringView( "@ByteArray(" );
constexpr QLatin1StringView VARIANT = QLatin1StringView( "@Variant(" );

//-----------------------------------------------------------------------------
// Purpose: Text as a Squirrel string, always quoted (so "12" stays text): verbatim when it holds
//          backslashes and no quotes or line breaks, as Windows paths do.
//-----------------------------------------------------------------------------
QString Quoted( const QString &szText )
{
	if ( szText.contains( u'\\' ) && !szText.contains( u'"' ) && !szText.contains( u'\n' ) && !szText.contains( u'\r' ) )
	{
		return QStringLiteral( "@\"" ) + szText + u'"';
	}
	QString szOut = QStringLiteral( "\"" );
	for ( const QChar c : szText )
	{
		switch ( c.unicode() )
		{
			case '"': szOut += QStringLiteral( "\\\"" ); break;
			case '\\': szOut += QStringLiteral( "\\\\" ); break;
			case '\n': szOut += QStringLiteral( "\\n" ); break;
			case '\r': szOut += QStringLiteral( "\\r" ); break;
			case '\t': szOut += QStringLiteral( "\\t" ); break;
			default: szOut += c; break;
		}
	}
	return szOut + u'"';
}

//-----------------------------------------------------------------------------
// Purpose: A slot's name: bare when it is a Squirrel name, else ["..."].
//-----------------------------------------------------------------------------
QString SlotName( const QString &szKey )
{
	static const QRegularExpression IDENTIFIER( QStringLiteral( "^[A-Za-z_][A-Za-z0-9_]*$" ) );
	static const QStringList RESERVED{ QStringLiteral( "true" ), QStringLiteral( "false" ), QStringLiteral( "null" ), QStringLiteral( "local" ), QStringLiteral( "const" ) };
	return IDENTIFIER.match( szKey ).hasMatch() && !RESERVED.contains( szKey ) ? szKey : u'[' + Quoted( szKey ) + u']';
}

//-----------------------------------------------------------------------------
// Purpose: A value as Squirrel. Text, numbers, booleans, lists and maps are written as they
//          are; bytes and other types (rectangles, colours, dates) as marked strings, the way
//          Qt's own ini files mark them.
//-----------------------------------------------------------------------------
QString Write( const QVariant &value )
{
	switch ( value.typeId() )
	{
		case QMetaType::UnknownType: return QStringLiteral( "null" );
		case QMetaType::Bool: return value.toBool() ? QStringLiteral( "true" ) : QStringLiteral( "false" );
		case QMetaType::Int:
		case QMetaType::LongLong:
		case QMetaType::UInt:
		case QMetaType::ULongLong:
		case QMetaType::Short:
		case QMetaType::UShort: return value.toString();
		case QMetaType::Double:
		case QMetaType::Float:
		{
			const double fl = value.toDouble();
			if ( std::isfinite( fl ) )
			{
				// A point or an exponent, so it reads back as a double rather than a whole number.
				QString szNumber = QString::number( fl, 'g', 17 );
				if ( !szNumber.contains( u'.' ) && !szNumber.contains( u'e' ) )
				{
					szNumber += QStringLiteral( ".0" );
				}
				return szNumber;
			}
			break;
		}
		case QMetaType::QString:
		{
			const QString szText = value.toString();
			return Quoted( szText.startsWith( MARK ) ? MARK + szText : szText );
		}
		case QMetaType::QByteArray: return Quoted( BYTES + QString::fromLatin1( value.toByteArray().toBase64() ) + u')' );
		case QMetaType::QStringList:
		case QMetaType::QVariantList:
		{
			const QVariantList items = value.toList();
			if ( items.isEmpty() )
			{
				return QStringLiteral( "[]" );
			}
			QStringList written;
			for ( const QVariant &item : items )
			{
				written << Write( item );
			}
			return QStringLiteral( "[ " ) + written.join( QStringLiteral( ", " ) ) + QStringLiteral( " ]" );
		}
		case QMetaType::QVariantMap:
		case QMetaType::QVariantHash:
		{
			const QVariantMap map = value.toMap();
			if ( map.isEmpty() )
			{
				return QStringLiteral( "{}" );
			}
			QStringList written;
			for ( auto it = map.begin(); it != map.end(); ++it )
			{
				written << SlotName( it.key() ) + QStringLiteral( " = " ) + Write( it.value() );
			}
			return QStringLiteral( "{ " ) + written.join( QStringLiteral( ", " ) ) + QStringLiteral( " }" );
		}
		default: break;
	}
	QByteArray data;
	{
		QDataStream stream( &data, QIODevice::WriteOnly );
		stream.setVersion( QDataStream::Qt_6_0 );
		stream << value;
	}
	return Quoted( VARIANT + QString::fromLatin1( data.toBase64() ) + u')' );
}

//-----------------------------------------------------------------------------
// Purpose: A marked string back to what it stood for.
//-----------------------------------------------------------------------------
QVariant ReadString( const QString &szText )
{
	if ( szText.startsWith( BYTES ) && szText.endsWith( u')' ) )
	{
		return QByteArray::fromBase64( szText.mid( BYTES.size(), szText.size() - BYTES.size() - 1 ).toLatin1() );
	}
	if ( szText.startsWith( VARIANT ) && szText.endsWith( u')' ) )
	{
		QByteArray data = QByteArray::fromBase64( szText.mid( VARIANT.size(), szText.size() - VARIANT.size() - 1 ).toLatin1() );
		QDataStream stream( &data, QIODevice::ReadOnly );
		stream.setVersion( QDataStream::Qt_6_0 );
		QVariant value;
		stream >> value;
		return value;
	}
	if ( szText.startsWith( QString{ MARK } + MARK ) )
	{
		return szText.mid( 1 );
	}
	return szText;
}

//-----------------------------------------------------------------------------
// Purpose: A value from the file, its type as written. A list of text is a QStringList, as
//          Qt's ini files give one.
//-----------------------------------------------------------------------------
QVariant Read( const xv::conf::Node_t &node )
{
	using Kind_t = xv::conf::Node_t::Kind_t;
	switch ( node.m_nKind )
	{
		case Kind_t::KIND_NULL: return {};
		case Kind_t::KIND_BOOL: return node.m_szText == "true";
		case Kind_t::KIND_NUMBER:
		{
			const QString szNumber = QString::fromStdString( node.m_szText );
			if ( szNumber.contains( u'.' ) || ( !szNumber.startsWith( QStringLiteral( "0x" ) ) && szNumber.contains( u'e', Qt::CaseInsensitive ) ) )
			{
				return szNumber.toDouble();
			}
			bool bOk = false;
			const qlonglong n = szNumber.toLongLong( &bOk, 0 );
			if ( !bOk )
			{
				return szNumber.toULongLong( nullptr, 0 );
			}
			return n >= INT_MIN && n <= INT_MAX ? QVariant( static_cast<int>( n ) ) : QVariant( n );
		}
		case Kind_t::KIND_STRING:
		case Kind_t::KIND_IDENTIFIER: return ReadString( QString::fromStdString( node.m_szText ) );
		case Kind_t::KIND_ARRAY:
		{
			QVariantList items;
			bool bText = true;
			for ( const xv::conf::Node_t &item : node.m_Items )
			{
				items << Read( item );
				bText = bText && items.back().typeId() == QMetaType::QString;
			}
			return bText ? QVariant( QVariant( items ).toStringList() ) : QVariant( items );
		}
		case Kind_t::KIND_TABLE:
		{
			QVariantMap map;
			for ( const xv::conf::Node_t::Slot_t &slot : node.m_Slots )
			{
				map.insert( QString::fromStdString( slot.m_szName ), Read( slot.m_Value ) );
			}
			return map;
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose: The file's "state" table, key by key. What can't be read is left out, rather than
//          the whole file refused (Qt would then write over it with nothing).
//-----------------------------------------------------------------------------
bool ReadNut( QIODevice &device, QSettings::SettingsMap &map )
{
	const xv::conf::CConfFile file( device.readAll().toStdString() );
	for ( const xv::conf::Statement_t &statement : file.Statements() )
	{
		if ( statement.m_nKind != xv::conf::Statement_t::Kind_t::KIND_ASSIGN || statement.m_Path.size() != 1 || statement.m_Path.front() != "state" ||
			statement.m_Value.m_nKind != xv::conf::Node_t::Kind_t::KIND_TABLE )
		{
			continue;
		}
		for ( const xv::conf::Node_t::Slot_t &slot : statement.m_Value.m_Slots )
		{
			map.insert( QString::fromStdString( slot.m_szName ), Read( slot.m_Value ) );
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Every key as a slot of "state", one a line, in Qt's order (sorted).
//-----------------------------------------------------------------------------
bool WriteNut( QIODevice &device, const QSettings::SettingsMap &map )
{
	QString szText = QStringLiteral( "// vomit-tools state: window layouts, recent files, last folders and the open session,\n"
									 "// written by the app as it runs. Settings are in vomit-tools.nut beside this.\n"
									 "state <- {\n" );
	for ( auto it = map.begin(); it != map.end(); ++it )
	{
		szText += u'\t' + SlotName( it.key() ) + QStringLiteral( " = " ) + Write( it.value() ) + QStringLiteral( ",\n" );
	}
	szText += QStringLiteral( "}\n" );
	const QByteArray data = szText.toUtf8();
	return device.write( data ) == data.size();
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Registered once; Qt keeps the functions. Its files are <app>.state.nut, so they stand
//          apart from the settings file (<app>.nut) in the same folder.
//-----------------------------------------------------------------------------
QSettings::Format NutSettingsFormat()
{
	static const QSettings::Format FORMAT = QSettings::registerFormat( QStringLiteral( "state.nut" ), ReadNut, WriteNut );
	return FORMAT;
}

//-----------------------------------------------------------------------------
// Purpose: Copies Qt's old file over the first time the new one is missing.
//-----------------------------------------------------------------------------
void MigrateSettingsToNut()
{
	QSettings settings( NutSettingsFormat(), QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName() );
	if ( QFileInfo::exists( settings.fileName() ) )
	{
		return;
	}
	// The state file as it was first named, <app>.nut: that name is the settings file's now.
	const QFileInfo info( settings.fileName() );
	const QString szEarlier = info.absoluteDir().filePath( QCoreApplication::applicationName() + QStringLiteral( ".nut" ) );
	if ( QFile earlier( szEarlier ); earlier.open( QIODevice::ReadOnly ) && earlier.read( 4096 ).contains( "\nstate <- {" ) )
	{
		earlier.close();
		if ( QFile::rename( szEarlier, info.absoluteFilePath() ) )
		{
			settings.sync(); // reads what was renamed in
			return;
		}
	}
	QSettings old( QSettings::NativeFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName() );
	const QStringList keys = old.allKeys();
	if ( keys.isEmpty() )
	{
		return;
	}
	for ( const QString &szKey : keys )
	{
		settings.setValue( szKey, old.value( szKey ) );
	}
	settings.sync();
	// The registry (Windows) is left as it is; a file that was moved goes, so there's one.
	if ( settings.status() == QSettings::NoError && QFileInfo( old.fileName() ).isFile() )
	{
		QFile::remove( old.fileName() );
	}
}
