//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See PopupBlur.h.
//
//===========================================================================//
#include "PopupBlur.h"

#include <charconv>
#include <fstream>
#include <string>

#include <QApplication>
#include <QEvent>
#include <QPointer>
#include <QStyle>
#include <QWidget>

#include "DebugLog.h"

namespace
{

// The first version with the fix (KDE bug 522547).
constexpr int FIXED_VERSION = 6 * 10000 + 32 * 100;

//-----------------------------------------------------------------------------
// Purpose: The style's blur helper, by its class name; null when it has none yet.
//-----------------------------------------------------------------------------
QObject *FindBlurHelper( const QStyle *pStyle )
{
	for ( QObject *pChild : pStyle->findChildren<QObject *>() )
		if ( QByteArray( pChild->metaObject()->className() ).endsWith( "BlurHelper" ) )
			return pChild;
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Kvantum's blur helper is a child of the style; it watches each popup it blurs through
//          an event filter and turns the blur on or off on Show, Hide, Resize and StyleChange.
//          An application's filters see each event before the widget's own do, so taking the
//          helper's filter off right then means it never acts, however often Kvantum puts it back.
//-----------------------------------------------------------------------------
class CPopupBlurFilter : public QObject
{
public:
	using QObject::QObject;

	bool eventFilter( QObject *pObject, QEvent *pEvent ) override
	{
		switch ( pEvent->type() )
		{
			case QEvent::Polish:
			case QEvent::Show:
			case QEvent::Hide:
			case QEvent::Resize:
			case QEvent::StyleChange:
			case QEvent::WindowActivate:
			case QEvent::WindowDeactivate:
				if ( auto *pWidget = qobject_cast<QWidget *>( pObject ); pWidget && pWidget->isWindow() )
				{
					// Kvantum makes its helper when it first polishes a popup it blurs.
					if ( !m_pHelper )
						m_pHelper = FindBlurHelper( pWidget->style() );
					if ( m_pHelper )
						pWidget->removeEventFilter( m_pHelper );
				}
				break;
			default:
				break;
		}
		return false;
	}

private:
	QPointer<QObject> m_pHelper;
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<int> WindowSystemVersionIn( std::string_view szLine )
{
	constexpr std::string_view LIBRARY = "libKF6WindowSystem.so.";
	const std::size_t nAt = szLine.find( LIBRARY );
	if ( nAt == std::string_view::npos )
		return std::nullopt;
	std::string_view szVersion = szLine.substr( nAt + LIBRARY.size() );
	int parts[ 3 ]{};
	for ( int i = 0; i < 3; i++ )
	{
		const auto [ pEnd, ec ] = std::from_chars( szVersion.data(), szVersion.data() + szVersion.size(), parts[ i ] );
		if ( ec != std::errc{} )
			return i == 0 ? std::nullopt : std::optional{ parts[ 0 ] * 10000 + parts[ 1 ] * 100 + parts[ 2 ] };
		szVersion.remove_prefix( static_cast<std::size_t>( pEnd - szVersion.data() ) );
		if ( szVersion.empty() || szVersion.front() != '.' )
			return parts[ 0 ] * 10000 + parts[ 1 ] * 100 + parts[ 2 ];
		szVersion.remove_prefix( 1 );
	}
	return parts[ 0 ] * 10000 + parts[ 1 ] * 100 + parts[ 2 ];
}

//-----------------------------------------------------------------------------
// Purpose: The style is created by now, and Kvantum loads KWindowSystem with it.
//-----------------------------------------------------------------------------
bool NeedsPopupBlurWorkaround()
{
	if ( QGuiApplication::platformName().compare( u"wayland", Qt::CaseInsensitive ) != 0 )
		return false;
	if ( !QApplication::style() || !QApplication::style()->name().contains( u"kvantum", Qt::CaseInsensitive ) )
		return false;
	std::ifstream maps( "/proc/self/maps" );
	for ( std::string szLine; std::getline( maps, szLine ); )
		if ( const auto nVersion = WindowSystemVersionIn( szLine ) )
			return *nVersion < FIXED_VERSION;
	return false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool InstallPopupBlurWorkaround( QApplication &app )
{
	if ( !NeedsPopupBlurWorkaround() )
		return false;
	app.installEventFilter( new CPopupBlurFilter( &app ) );
	DebugLog() << "Kvantum's popup blur is off: KWindowSystem before 6.32 disconnects from Wayland with it";
	return true;
}
