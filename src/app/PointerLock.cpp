//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See PointerLock.h. The Wayland part binds zwp_pointer_constraints_v1 and
//          zwp_relative_pointer_manager_v1 through Qt's client extension support, with the
//          protocol code made at build time from src/app/wayland/*.xml.
//
//===========================================================================//
#include "PointerLock.h"

#include <QGuiApplication>
#include <QWidget>
#include <QWindow>

#ifdef VOMIT_TOOLS_WAYLAND
#include <QtGui/qguiapplication_platform.h>
#include <QtWaylandClient/QWaylandClientExtension>
#include <qpa/qplatformnativeinterface.h>

#include "qwayland-pointer-constraints-unstable-v1.h"
#include "qwayland-relative-pointer-unstable-v1.h"
#endif

#include "DebugLog.h"

#ifdef VOMIT_TOOLS_WAYLAND

namespace
{

class CPointerConstraints : public QWaylandClientExtensionTemplate<CPointerConstraints>, public QtWayland::zwp_pointer_constraints_v1
{
public:
	CPointerConstraints()
		: QWaylandClientExtensionTemplate( 1 )
	{
	}
};

class CRelativePointerManager : public QWaylandClientExtensionTemplate<CRelativePointerManager>, public QtWayland::zwp_relative_pointer_manager_v1
{
public:
	CRelativePointerManager()
		: QWaylandClientExtensionTemplate( 1 )
	{
	}
};

// The two globals, bound once the compositor lists them (Qt asks for the list as the program
// starts), only on Wayland.
struct Globals_t
{
	std::unique_ptr<CPointerConstraints> m_pConstraints;
	std::unique_ptr<CRelativePointerManager> m_pRelative;
};

Globals_t &Globals()
{
	static Globals_t globals;
	static bool bMade = false;
	if ( !bMade && QGuiApplication::platformName() == QStringLiteral( "wayland" ) )
	{
		bMade = true;
		globals.m_pConstraints = std::make_unique<CPointerConstraints>();
		globals.m_pRelative = std::make_unique<CRelativePointerManager>();
		qAddPostRoutine( [] {
			Globals().m_pConstraints.reset();
			Globals().m_pRelative.reset();
		} );
	}
	return globals;
}

// The movement while pinned, which the compositor sends here rather than as pointer motion.
class CRelativePointer : public QtWayland::zwp_relative_pointer_v1
{
public:
	explicit CRelativePointer( std::function<void( QPointF )> pfnMoved )
		: m_pfnMoved( std::move( pfnMoved ) )
	{
	}
	~CRelativePointer() override
	{
		if ( isInitialized() )
		{
			destroy();
		}
	}

protected:
	void zwp_relative_pointer_v1_relative_motion( uint32_t, uint32_t, wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t, wl_fixed_t ) override
	{
		m_pfnMoved( QPointF( wl_fixed_to_double( dx ), wl_fixed_to_double( dy ) ) );
	}

private:
	std::function<void( QPointF )> m_pfnMoved;
};

class CLockedPointer : public QtWayland::zwp_locked_pointer_v1
{
public:
	~CLockedPointer() override
	{
		if ( isInitialized() )
		{
			destroy();
		}
	}
	[[nodiscard]] bool IsLocked() const { return m_bLocked; }

protected:
	void zwp_locked_pointer_v1_locked() override { m_bLocked = true; }
	void zwp_locked_pointer_v1_unlocked() override { m_bLocked = false; }

private:
	bool m_bLocked = false;
};

} // namespace

struct CPointerLock::Impl_t
{
	std::unique_ptr<CRelativePointer> m_pRelative;
	std::unique_ptr<CLockedPointer> m_pLocked;
	QWidget *m_pWidget = nullptr;
};

#else

struct CPointerLock::Impl_t
{
};

#endif

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CPointerLock::CPointerLock( QObject *pParent )
	: QObject( pParent ),
	  m_pImpl( std::make_unique<Impl_t>() )
{
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CPointerLock::~CPointerLock() = default;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CPointerLock::IsAvailable()
{
#ifdef VOMIT_TOOLS_WAYLAND
	const Globals_t &globals = Globals();
	return globals.m_pConstraints && globals.m_pConstraints->isActive() && globals.m_pRelative && globals.m_pRelative->isActive();
#else
	return false;
#endif
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CPointerLock::Lock( QWidget *pWidget, std::function<void( QPointF )> pfnMoved )
{
#ifdef VOMIT_TOOLS_WAYLAND
	if ( !IsAvailable() || IsActive() || !pWidget || !pWidget->window()->windowHandle() )
	{
		return false;
	}
	auto *pApp = qApp->nativeInterface<QNativeInterface::QWaylandApplication>();
	QPlatformNativeInterface *pNative = QGuiApplication::platformNativeInterface();
	wl_pointer *pPointer = pApp ? pApp->pointer() : nullptr;
	auto *pSurface = pNative ? static_cast<wl_surface *>( pNative->nativeResourceForWindow( "surface", pWidget->window()->windowHandle() ) ) : nullptr;
	if ( !pPointer || !pSurface )
	{
		DebugLog() << "pointer lock:" << ( pPointer ? "the window has no surface" : "the seat has no pointer" );
		return false;
	}
	Globals_t &globals = Globals();
	m_pImpl->m_pWidget = pWidget;
	m_pImpl->m_pRelative = std::make_unique<CRelativePointer>( std::move( pfnMoved ) );
	m_pImpl->m_pRelative->init( globals.m_pRelative->get_relative_pointer( pPointer ) );
	m_pImpl->m_pLocked = std::make_unique<CLockedPointer>();
	m_pImpl->m_pLocked->init( globals.m_pConstraints->lock_pointer( pSurface, pPointer, nullptr, QtWayland::zwp_pointer_constraints_v1::lifetime_persistent ) );
	return true;
#else
	static_cast<void>( pWidget );
	static_cast<void>( pfnMoved );
	return false;
#endif
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPointerLock::Unlock( QPoint at )
{
#ifdef VOMIT_TOOLS_WAYLAND
	if ( !IsActive() )
	{
		return;
	}
	// Where the cursor shows again: the hint is in the window's own coordinates, and takes
	// effect with the window's next frame.
	const QPointF inWindow = m_pImpl->m_pWidget->mapTo( m_pImpl->m_pWidget->window(), at );
	m_pImpl->m_pLocked->set_cursor_position_hint( wl_fixed_from_double( inWindow.x() ), wl_fixed_from_double( inWindow.y() ) );
	m_pImpl->m_pLocked.reset();
	m_pImpl->m_pRelative.reset();
	m_pImpl->m_pWidget->update();
	m_pImpl->m_pWidget = nullptr;
#else
	static_cast<void>( at );
#endif
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CPointerLock::IsActive() const
{
#ifdef VOMIT_TOOLS_WAYLAND
	return m_pImpl->m_pLocked != nullptr;
#else
	return false;
#endif
}
