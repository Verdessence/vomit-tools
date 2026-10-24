//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Graphics.h.
//
//===========================================================================//
#include "Graphics.h"

#include <array>
#include <utility>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QStringList>

#include "Preferences.h"

#ifdef VOMIT_TOOLS_GPU
#include <QOffscreenSurface>
#include <rhi/qrhi.h>
#if QT_CONFIG( vulkan )
#include <QVulkanInstance>
#endif
#endif

namespace
{

constexpr auto OVERRIDE_VARIABLE = "VOMIT_TOOLS_GRAPHICS";

constexpr std::array<std::pair<GraphicsApi_t, std::string_view>, 7> KEYS{ {
	{ GraphicsApi_t::GRAPHICS_API_AUTO, "auto" },
	{ GraphicsApi_t::GRAPHICS_API_VULKAN, "vulkan" },
	{ GraphicsApi_t::GRAPHICS_API_OPENGL, "opengl" },
	{ GraphicsApi_t::GRAPHICS_API_DIRECT3D11, "d3d11" },
	{ GraphicsApi_t::GRAPHICS_API_DIRECT3D12, "d3d12" },
	{ GraphicsApi_t::GRAPHICS_API_METAL, "metal" },
	{ GraphicsApi_t::GRAPHICS_API_SOFTWARE, "software" },
} };

//-----------------------------------------------------------------------------
// Purpose: The platform's usual API, which Auto falls back to when Vulkan doesn't work.
//-----------------------------------------------------------------------------
GraphicsApi_t NativeApi()
{
#if defined( Q_OS_WIN )
	return GraphicsApi_t::GRAPHICS_API_DIRECT3D11;
#elif defined( Q_OS_MACOS ) || defined( Q_OS_IOS )
	return GraphicsApi_t::GRAPHICS_API_METAL;
#else
	return GraphicsApi_t::GRAPHICS_API_OPENGL;
#endif
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view GraphicsApiKey( GraphicsApi_t nApi )
{
	for ( const auto &[ nKnown, szKey ] : KEYS )
	{
		if ( nKnown == nApi )
		{
			return szKey;
		}
	}
	return "auto";
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<GraphicsApi_t> GraphicsApiFromKey( std::string_view szKey )
{
	for ( const auto &[ nApi, szKnown ] : KEYS )
	{
		if ( szKnown == szKey )
		{
			return nApi;
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString GraphicsApiName( GraphicsApi_t nApi )
{
	switch ( nApi )
	{
		case GraphicsApi_t::GRAPHICS_API_AUTO: return QCoreApplication::translate( "Graphics", "Automatic (Vulkan where it works)" );
		case GraphicsApi_t::GRAPHICS_API_VULKAN: return QStringLiteral( "Vulkan" );
		case GraphicsApi_t::GRAPHICS_API_OPENGL: return QStringLiteral( "OpenGL" );
		case GraphicsApi_t::GRAPHICS_API_DIRECT3D11: return QStringLiteral( "Direct3D 11" );
		case GraphicsApi_t::GRAPHICS_API_DIRECT3D12: return QStringLiteral( "Direct3D 12" );
		case GraphicsApi_t::GRAPHICS_API_METAL: return QStringLiteral( "Metal" );
		case GraphicsApi_t::GRAPHICS_API_SOFTWARE: return QCoreApplication::translate( "Graphics", "Software (CPU)" );
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose: Made on first use and kept to the end: its Vulkan instance and OpenGL surface must go
//          before the application does, which a post routine sees to.
//-----------------------------------------------------------------------------
CGraphics &CGraphics::Instance()
{
	static CGraphics *pGraphics = [] {
		auto *pMade = new CGraphics;
		qAddPostRoutine( [] {
#ifdef VOMIT_TOOLS_GPU
			Instance().m_pSurface.reset();
			Instance().m_pVulkan.reset();
#endif
		} );
		return pMade;
	}();
	return *pGraphics;
}

//-----------------------------------------------------------------------------
// Purpose: Tries the preference (Auto: Vulkan, then the platform's usual API) and settles on the
//          first that works, or the CPU.
//-----------------------------------------------------------------------------
CGraphics::CGraphics()
{
	GraphicsApi_t nWanted = CPreferences::Instance().GetGraphicsApi();
	if ( const QByteArray szOverride = qgetenv( OVERRIDE_VARIABLE ); !szOverride.isEmpty() )
	{
		nWanted = GraphicsApiFromKey( szOverride.toStdString() ).value_or( nWanted );
	}
	std::vector<GraphicsApi_t> order{ nWanted };
	if ( nWanted == GraphicsApi_t::GRAPHICS_API_AUTO )
	{
		// The offscreen and minimal platforms (tests, headless runs) have no window system to show
		// a graphics card's pictures in.
		const QString szPlatform = QGuiApplication::platformName();
		const bool bHeadless = szPlatform == u"offscreen" || szPlatform == u"minimal";
		order = bHeadless ? std::vector<GraphicsApi_t>{} : std::vector<GraphicsApi_t>{ GraphicsApi_t::GRAPHICS_API_VULKAN, NativeApi() };
	}
	QStringList reasons;
	for ( GraphicsApi_t nApi : order )
	{
		if ( nApi == GraphicsApi_t::GRAPHICS_API_SOFTWARE )
		{
			break;
		}
		QString szWhyNot;
		if ( TryApi( nApi, szWhyNot ) )
		{
			m_nActive = nApi;
			break;
		}
		reasons << QStringLiteral( "%1: %2" ).arg( GraphicsApiName( nApi ), szWhyNot );
	}
	if ( !IsGpu() )
	{
		m_szDescription = GraphicsApiName( GraphicsApi_t::GRAPHICS_API_SOFTWARE );
	}
	if ( !reasons.isEmpty() && ( nWanted != GraphicsApi_t::GRAPHICS_API_AUTO || !IsGpu() ) )
	{
		m_szProblem = reasons.join( u'\n' );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CGraphics::~CGraphics() = default;

//-----------------------------------------------------------------------------
// Purpose: The APIs this platform and build can offer.
//-----------------------------------------------------------------------------
std::vector<GraphicsApi_t> CGraphics::Choices()
{
	std::vector<GraphicsApi_t> choices;
#ifdef VOMIT_TOOLS_GPU
	choices.push_back( GraphicsApi_t::GRAPHICS_API_AUTO );
#if QT_CONFIG( vulkan )
	choices.push_back( GraphicsApi_t::GRAPHICS_API_VULKAN );
#endif
#if QT_CONFIG( opengl )
	choices.push_back( GraphicsApi_t::GRAPHICS_API_OPENGL );
#endif
#if defined( Q_OS_WIN )
	choices.push_back( GraphicsApi_t::GRAPHICS_API_DIRECT3D11 );
	choices.push_back( GraphicsApi_t::GRAPHICS_API_DIRECT3D12 );
#endif
#if defined( Q_OS_MACOS ) || defined( Q_OS_IOS )
	choices.push_back( GraphicsApi_t::GRAPHICS_API_METAL );
#endif
#endif
	choices.push_back( GraphicsApi_t::GRAPHICS_API_SOFTWARE );
	return choices;
}

//-----------------------------------------------------------------------------
// Purpose: Makes a QRhi of the API to see that it works.
//-----------------------------------------------------------------------------
bool CGraphics::TryApi( GraphicsApi_t nApi, QString &szWhyNot )
{
#ifdef VOMIT_TOOLS_GPU
	const GraphicsApi_t nWas = m_nActive;
	m_nActive = nApi;
	const std::unique_ptr<QRhi> pRhi = CreateOffscreen();
	m_nActive = nWas;
	if ( !pRhi )
	{
		szWhyNot = nApi == GraphicsApi_t::GRAPHICS_API_VULKAN && !m_pVulkan ? QCoreApplication::translate( "Graphics", "no Vulkan driver was found" )
																			: QCoreApplication::translate( "Graphics", "the graphics card couldn't be used with it" );
		return false;
	}
	m_szDescription = QCoreApplication::translate( "Graphics", "%1 on %2" ).arg( GraphicsApiName( nApi ), QString::fromUtf8( pRhi->driverInfo().deviceName ) );
	return true;
#else
	Q_UNUSED( nApi );
	szWhyNot = QCoreApplication::translate( "Graphics", "this build draws in software only" );
	return false;
#endif
}

#ifdef VOMIT_TOOLS_GPU

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QRhiWidget::Api CGraphics::GetWidgetApi() const
{
	switch ( m_nActive )
	{
		case GraphicsApi_t::GRAPHICS_API_VULKAN: return QRhiWidget::Api::Vulkan;
		case GraphicsApi_t::GRAPHICS_API_DIRECT3D11: return QRhiWidget::Api::Direct3D11;
		case GraphicsApi_t::GRAPHICS_API_DIRECT3D12: return QRhiWidget::Api::Direct3D12;
		case GraphicsApi_t::GRAPHICS_API_METAL: return QRhiWidget::Api::Metal;
		case GraphicsApi_t::GRAPHICS_API_OPENGL:
		default: return QRhiWidget::Api::OpenGL;
	}
}

//-----------------------------------------------------------------------------
// Purpose: A QRhi of the active API for drawing into textures.
//-----------------------------------------------------------------------------
std::unique_ptr<QRhi> CGraphics::CreateOffscreen()
{
	switch ( m_nActive )
	{
		case GraphicsApi_t::GRAPHICS_API_VULKAN:
		{
#if QT_CONFIG( vulkan )
			if ( !m_pVulkan )
			{
				auto pInstance = std::make_unique<QVulkanInstance>();
				pInstance->setExtensions( QRhiVulkanInitParams::preferredInstanceExtensions() );
				if ( !pInstance->create() )
				{
					return nullptr;
				}
				m_pVulkan = std::move( pInstance );
			}
			QRhiVulkanInitParams params;
			params.inst = m_pVulkan.get();
			return std::unique_ptr<QRhi>( QRhi::create( QRhi::Vulkan, &params ) );
#else
			return nullptr;
#endif
		}
		case GraphicsApi_t::GRAPHICS_API_OPENGL:
		{
#if QT_CONFIG( opengl )
			if ( !m_pSurface )
			{
				m_pSurface.reset( QRhiGles2InitParams::newFallbackSurface() );
			}
			QRhiGles2InitParams params;
			params.fallbackSurface = m_pSurface.get();
			return std::unique_ptr<QRhi>( QRhi::create( QRhi::OpenGLES2, &params ) );
#else
			return nullptr;
#endif
		}
#if defined( Q_OS_WIN )
		case GraphicsApi_t::GRAPHICS_API_DIRECT3D11:
		{
			QRhiD3D11InitParams params;
			return std::unique_ptr<QRhi>( QRhi::create( QRhi::D3D11, &params ) );
		}
		case GraphicsApi_t::GRAPHICS_API_DIRECT3D12:
		{
			QRhiD3D12InitParams params;
			return std::unique_ptr<QRhi>( QRhi::create( QRhi::D3D12, &params ) );
		}
#endif
#if defined( Q_OS_MACOS ) || defined( Q_OS_IOS )
		case GraphicsApi_t::GRAPHICS_API_METAL:
		{
			QRhiMetalInitParams params;
			return std::unique_ptr<QRhi>( QRhi::create( QRhi::Metal, &params ) );
		}
#endif
		default: return nullptr;
	}
}

#endif
