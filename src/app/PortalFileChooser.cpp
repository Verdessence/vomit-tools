//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See PortalFileChooser.h. A call to the portal hands back a request object at once;
//          the request's Response signal brings the answer once the user is done.
//
//===========================================================================//
#include "PortalFileChooser.h"

#include <algorithm>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDialog>
#include <QFile>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QUrl>
#include <QWidget>

#include "DebugLog.h"

namespace
{

const QString PORTAL_SERVICE = QStringLiteral( "org.freedesktop.portal.Desktop" );
const QString PORTAL_PATH = QStringLiteral( "/org/freedesktop/portal/desktop" );
const QString FILE_CHOOSER_INTERFACE = QStringLiteral( "org.freedesktop.portal.FileChooser" );
const QString REQUEST_INTERFACE = QStringLiteral( "org.freedesktop.portal.Request" );

// One rule of a portal filter: type 0 with a glob pattern, or 1 with a MIME type.
struct PortalRule_t
{
	uint m_nType = 0;
	QString m_szPattern;
};

// A filter as the portal takes it: the name shown and its rules.
struct PortalFilter_t
{
	QString m_szName;
	QList<PortalRule_t> m_Rules;
};

//-----------------------------------------------------------------------------
// Purpose: D-Bus marshalling for the filter types, (us) and (sa(us)).
//-----------------------------------------------------------------------------
QDBusArgument &operator<<( QDBusArgument &argument, const PortalRule_t &rule )
{
	argument.beginStructure();
	argument << rule.m_nType << rule.m_szPattern;
	argument.endStructure();
	return argument;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const QDBusArgument &operator>>( const QDBusArgument &argument, PortalRule_t &rule )
{
	argument.beginStructure();
	argument >> rule.m_nType >> rule.m_szPattern;
	argument.endStructure();
	return argument;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QDBusArgument &operator<<( QDBusArgument &argument, const PortalFilter_t &filter )
{
	argument.beginStructure();
	argument << filter.m_szName << filter.m_Rules;
	argument.endStructure();
	return argument;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const QDBusArgument &operator>>( const QDBusArgument &argument, PortalFilter_t &filter )
{
	argument.beginStructure();
	argument >> filter.m_szName >> filter.m_Rules;
	argument.endStructure();
	return argument;
}

} // namespace

Q_DECLARE_METATYPE( PortalRule_t )
Q_DECLARE_METATYPE( PortalFilter_t )

namespace
{

//-----------------------------------------------------------------------------
// Purpose: A glob that matches either case: "*.vtf" becomes "*.[vV][tT][fF]". Source files come
//          named both ways, and some choosers (GTK's) match patterns exactly.
//-----------------------------------------------------------------------------
QString AnyCaseGlob( const QString &szPattern )
{
	if ( szPattern.contains( u'[' ) )
	{
		return szPattern;
	}
	QString szGlob;
	for ( const QChar ch : szPattern )
	{
		const QChar chLower = ch.toLower();
		const QChar chUpper = ch.toUpper();
		if ( chLower == chUpper )
		{
			szGlob += ch;
		}
		else
		{
			szGlob += u'[';
			szGlob += chLower;
			szGlob += chUpper;
			szGlob += u']';
		}
	}
	return szGlob;
}

//-----------------------------------------------------------------------------
// Purpose: One of Qt's filters, "Pictures (*.png *.tga)", as the portal's.
//-----------------------------------------------------------------------------
PortalFilter_t ToPortalFilter( const QString &szFilter )
{
	static const QRegularExpression s_Named( QStringLiteral( R"(^(.*?)\s*\(([^()]*)\)\s*$)" ) );
	const QRegularExpressionMatch match = s_Named.match( szFilter );
	PortalFilter_t filter;
	filter.m_szName = match.hasMatch() ? match.captured( 1 ) : szFilter;
	const QString szPatterns = match.hasMatch() ? match.captured( 2 ) : szFilter;
	for ( const QString &szPattern : szPatterns.split( u' ', Qt::SkipEmptyParts ) )
	{
		filter.m_Rules.append( { 0, AnyCaseGlob( szPattern ) } );
	}
	return filter;
}

//-----------------------------------------------------------------------------
// Purpose: The portal's FileChooser version, or 0 when there is no portal to ask. Asked once a
//          run.
//-----------------------------------------------------------------------------
uint PortalVersion()
{
	static const uint s_nVersion = [] {
		QDBusConnection bus = QDBusConnection::sessionBus();
		if ( !bus.isConnected() )
		{
			return 0u;
		}
		QDBusMessage message = QDBusMessage::createMethodCall( PORTAL_SERVICE, PORTAL_PATH, QStringLiteral( "org.freedesktop.DBus.Properties" ), QStringLiteral( "Get" ) );
		message << FILE_CHOOSER_INTERFACE << QStringLiteral( "version" );
		// Starting the portal can take a moment the first time.
		const QDBusMessage reply = bus.call( message, QDBus::Block, 5000 );
		if ( reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty() )
		{
			DebugLog() << "no file chooser portal:" << reply.errorMessage();
			return 0u;
		}
		qDBusRegisterMetaType<PortalRule_t>();
		qDBusRegisterMetaType<PortalFilter_t>();
		qDBusRegisterMetaType<QList<PortalRule_t>>();
		qDBusRegisterMetaType<QList<PortalFilter_t>>();
		const uint nVersion = reply.arguments().front().value<QDBusVariant>().variant().toUInt();
		DebugLog() << "file chooser portal version" << nVersion;
		return nVersion;
	}();
	return s_nVersion;
}

//-----------------------------------------------------------------------------
// Purpose: The window the chooser belongs to, as the portal names windows. Only X11's can be
//          named without Qt's private API; on Wayland the chooser opens on its own.
//-----------------------------------------------------------------------------
QString PortalParent( QWidget *pParent )
{
	if ( pParent && QGuiApplication::platformName() == u"xcb" )
	{
		return QStringLiteral( "x11:%1" ).arg( static_cast<qulonglong>( pParent->window()->winId() ), 0, 16 );
	}
	return {};
}

//-----------------------------------------------------------------------------
// One question to the portal's chooser. While the chooser is open this invisible modal dialog
// stands in for it, holding the rest of the app still the way Qt's own native dialogs do, and it
// closes when the portal answers.
//-----------------------------------------------------------------------------
class CPortalRequest : public QDialog
{
	Q_OBJECT

public:
	explicit CPortalRequest( QWidget *pParent );

	// Opens the chooser and waits for it. False when the portal couldn't show one.
	[[nodiscard]] bool Run( const QString &szMethod, const QString &szTitle, QVariantMap options );

	[[nodiscard]] const QStringList &GetFiles() const { return m_Files; }
	// The name of the filter chosen, where the portal says.
	[[nodiscard]] const QString &GetFilterName() const { return m_szFilterName; }

private Q_SLOTS:
	void Response( uint nResponse, const QVariantMap &results );

private:
	void Listen( bool bOn );

	QString m_szHandle;
	QStringList m_Files;
	QString m_szFilterName;
	bool m_bAnswered = false;
	bool m_bFailed = false;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CPortalRequest::CPortalRequest( QWidget *pParent )
	: QDialog( pParent )
{
	setAttribute( Qt::WA_DontShowOnScreen );
	setModal( true );
}

//-----------------------------------------------------------------------------
// Purpose: Starts or stops listening for the answer to this request.
//-----------------------------------------------------------------------------
void CPortalRequest::Listen( bool bOn )
{
	QDBusConnection bus = QDBusConnection::sessionBus();
	if ( bOn )
	{
		bus.connect( {}, m_szHandle, REQUEST_INTERFACE, QStringLiteral( "Response" ), this, SLOT( Response( uint, QVariantMap ) ) );
	}
	else
	{
		bus.disconnect( {}, m_szHandle, REQUEST_INTERFACE, QStringLiteral( "Response" ), this, SLOT( Response( uint, QVariantMap ) ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Opens the chooser and waits for it.
//-----------------------------------------------------------------------------
bool CPortalRequest::Run( const QString &szMethod, const QString &szTitle, QVariantMap options )
{
	static int s_nRequests = 0;
	QDBusConnection bus = QDBusConnection::sessionBus();
	// The request's path follows from our bus name and the token, so the answer is listened for
	// before the question goes out and can't be missed.
	const QString szToken = QStringLiteral( "vomit_tools%1" ).arg( ++s_nRequests );
	options.insert( QStringLiteral( "handle_token" ), szToken );
	m_szHandle = QStringLiteral( "/org/freedesktop/portal/desktop/request/%1/%2" ).arg( bus.baseService().mid( 1 ).replace( u'.', u'_' ), szToken );
	Listen( true );
	QDBusMessage message = QDBusMessage::createMethodCall( PORTAL_SERVICE, PORTAL_PATH, FILE_CHOOSER_INTERFACE, szMethod );
	message << PortalParent( parentWidget() ) << szTitle << options;
	const QDBusMessage reply = bus.call( message, QDBus::Block, 10000 );
	if ( reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty() )
	{
		DebugLog() << "file chooser portal refused" << szMethod << reply.errorMessage();
		Listen( false );
		return false;
	}
	// Portals before 0.9 name requests their own way.
	if ( const QString szHandle = reply.arguments().front().value<QDBusObjectPath>().path(); szHandle != m_szHandle )
	{
		Listen( false );
		m_szHandle = szHandle;
		Listen( true );
	}
	// A portal that went away mid-question would leave the app held still for good.
	QDBusServiceWatcher watcher( PORTAL_SERVICE, bus, QDBusServiceWatcher::WatchForUnregistration );
	connect( &watcher, &QDBusServiceWatcher::serviceUnregistered, this, [ this ] {
		m_bFailed = true;
		reject();
	} );
	exec();
	Listen( false );
	if ( !m_bAnswered && !m_bFailed )
	{
		// Closed from this side somehow: close the chooser too, so it isn't left open unheard.
		bus.send( QDBusMessage::createMethodCall( PORTAL_SERVICE, m_szHandle, REQUEST_INTERFACE, QStringLiteral( "Close" ) ) );
	}
	return !m_bFailed;
}

//-----------------------------------------------------------------------------
// Purpose: The portal's answer: 0 picked, 1 cancelled, 2 ended some other way.
//-----------------------------------------------------------------------------
void CPortalRequest::Response( uint nResponse, const QVariantMap &results )
{
	m_bAnswered = true;
	m_bFailed = nResponse == 2;
	if ( nResponse == 0 )
	{
		for ( const QString &szUri : results.value( QStringLiteral( "uris" ) ).toStringList() )
		{
			// Places only the chooser can reach (smb://, sftp://...) can't be opened here.
			if ( const QUrl url{ szUri }; url.isLocalFile() )
			{
				m_Files << url.toLocalFile();
			}
		}
		if ( const QVariant filter = results.value( QStringLiteral( "current_filter" ) ); filter.canConvert<QDBusArgument>() )
		{
			PortalFilter_t chosen;
			filter.value<QDBusArgument>() >> chosen;
			m_szFilterName = chosen.m_szName;
		}
	}
	if ( m_bFailed )
	{
		DebugLog() << "file chooser portal ended without an answer";
	}
	done( nResponse == 0 ? Accepted : Rejected );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The desktop's chooser, or nothing when the portal can't show one.
//-----------------------------------------------------------------------------
std::optional<QStringList> AskPortalChooser( QWidget *pParent, FileDialog_t &dialog, const QString &szFolder, const QString &szName )
{
	const uint nVersion = PortalVersion();
	// Folders can be picked from version 3.
	if ( nVersion == 0 || ( dialog.m_nMode == FILE_DIALOG_MODE_FOLDER && nVersion < 3 ) )
	{
		return std::nullopt;
	}
	QVariantMap options;
	options.insert( QStringLiteral( "modal" ), true );
	options.insert( QStringLiteral( "current_folder" ), QFile::encodeName( szFolder ).append( '\0' ) );
	if ( dialog.m_nMode == FILE_DIALOG_MODE_OPEN_MANY )
	{
		options.insert( QStringLiteral( "multiple" ), true );
	}
	if ( dialog.m_nMode == FILE_DIALOG_MODE_FOLDER )
	{
		options.insert( QStringLiteral( "directory" ), true );
	}
	if ( dialog.m_nMode == FILE_DIALOG_MODE_SAVE && !szName.isEmpty() )
	{
		options.insert( QStringLiteral( "current_name" ), szName );
	}
	const QStringList qtFilters = dialog.m_nMode == FILE_DIALOG_MODE_FOLDER ? QStringList{} : dialog.m_szFilters.split( QStringLiteral( ";;" ), Qt::SkipEmptyParts );
	QList<PortalFilter_t> filters;
	for ( const QString &szFilter : qtFilters )
	{
		filters.append( ToPortalFilter( szFilter ) );
	}
	if ( !filters.isEmpty() )
	{
		// As in Qt's window, the first filter is the one shown unless another is asked for.
		const qsizetype nCurrent = std::max<qsizetype>( qtFilters.indexOf( dialog.m_szFilter ), 0 );
		dialog.m_szFilter = qtFilters[ nCurrent ];
		options.insert( QStringLiteral( "filters" ), QVariant::fromValue( filters ) );
		options.insert( QStringLiteral( "current_filter" ), QVariant::fromValue( filters[ nCurrent ] ) );
	}
	CPortalRequest request( pParent );
	if ( !request.Run( dialog.m_nMode == FILE_DIALOG_MODE_SAVE ? QStringLiteral( "SaveFile" ) : QStringLiteral( "OpenFile" ), dialog.m_szTitle, options ) )
	{
		return std::nullopt;
	}
	for ( qsizetype i = 0; i < filters.size(); ++i )
	{
		if ( filters[ i ].m_szName == request.GetFilterName() )
		{
			dialog.m_szFilter = qtFilters[ i ];
			break;
		}
	}
	return request.GetFiles();
}

#include "PortalFileChooser.moc"
