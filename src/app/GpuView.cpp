//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See GpuView.h.
//
//===========================================================================//
#include "GpuView.h"

#ifdef VOMIT_TOOLS_GPU

#include <QFile>
#include <rhi/qrhi.h>

#include "Graphics.h"

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QShader LoadShader( const QString &szPath )
{
	QFile file{ szPath };
	return file.open( QIODevice::ReadOnly ) ? QShader::fromSerialized( file.readAll() ) : QShader{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CGpuView::CGpuView( std::unique_ptr<IGpuRenderer> pRenderer, QWidget *pParent )
	: QRhiWidget( pParent ),
	  m_pRenderer( std::move( pRenderer ) )
{
	setApi( CGraphics::Instance().GetWidgetApi() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CGpuView::~CGpuView()
{
	Detach();
}

//-----------------------------------------------------------------------------
// Purpose: The renderer works with this QRhi from now on. Should the QRhi go away first, its
//          resources are released while it still can release them.
//-----------------------------------------------------------------------------
void CGpuView::Attach( QRhi *pRhi )
{
	if ( pRhi == m_pAttached )
	{
		return;
	}
	Detach();
	m_pAttached = pRhi;
	pRhi->addCleanupCallback( this, [ this ]( QRhi * ) {
		m_pRenderer->Release();
		m_pAttached = nullptr;
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CGpuView::Detach()
{
	if ( m_pAttached )
	{
		m_pAttached->removeCleanupCallback( this );
		m_pAttached = nullptr;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CGpuView::initialize( QRhiCommandBuffer * )
{
	Attach( rhi() );
	m_pRenderer->Initialize( rhi(), renderTarget() );
}

//-----------------------------------------------------------------------------
// Purpose: A hidden widget's grabFramebuffer() can bring a new QRhi without initialize(); the
//          renderer starts over with it.
//-----------------------------------------------------------------------------
void CGpuView::render( QRhiCommandBuffer *pCommands )
{
	if ( rhi() != m_pAttached )
	{
		Attach( rhi() );
		m_pRenderer->Initialize( rhi(), renderTarget() );
	}
	m_pRenderer->Render( pCommands, renderTarget() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CGpuView::releaseResources()
{
	m_pRenderer->Release();
	Detach();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
COffscreenGpu::COffscreenGpu( std::unique_ptr<IGpuRenderer> pRenderer )
	: m_pRhi( CGraphics::Instance().CreateOffscreen() ),
	  m_pRenderer( std::move( pRenderer ) )
{
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::unique_ptr<COffscreenGpu> COffscreenGpu::Create( std::unique_ptr<IGpuRenderer> pRenderer )
{
	return std::unique_ptr<COffscreenGpu>( new COffscreenGpu( std::move( pRenderer ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: The renderer's resources and the target go before the QRhi they were made with.
//-----------------------------------------------------------------------------
COffscreenGpu::~COffscreenGpu()
{
	m_pRenderer->Release();
	m_pTarget.reset();
	m_pPass.reset();
	m_pDepth.reset();
	m_pColor.reset();
	m_pRhi.reset();
}

//-----------------------------------------------------------------------------
// Purpose: Draws into a texture and reads it back.
//-----------------------------------------------------------------------------
QImage COffscreenGpu::Render( QSize size )
{
	if ( !m_pRhi || size.isEmpty() )
	{
		return {};
	}
	if ( size != m_Size )
	{
		m_pTarget.reset();
		m_pPass.reset();
		m_pColor.reset( m_pRhi->newTexture( QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource ) );
		m_pDepth.reset( m_pRhi->newRenderBuffer( QRhiRenderBuffer::DepthStencil, size ) );
		if ( !m_pColor->create() || !m_pDepth->create() )
		{
			m_pColor.reset();
			m_Size = {};
			return {};
		}
		QRhiTextureRenderTargetDescription description{ QRhiColorAttachment{ m_pColor.get() } };
		description.setDepthStencilBuffer( m_pDepth.get() );
		m_pTarget.reset( m_pRhi->newTextureRenderTarget( description ) );
		m_pPass.reset( m_pTarget->newCompatibleRenderPassDescriptor() );
		m_pTarget->setRenderPassDescriptor( m_pPass.get() );
		m_pTarget->create();
		m_Size = size;
		m_pRenderer->Initialize( m_pRhi.get(), m_pTarget.get() );
	}
	QRhiCommandBuffer *pCommands = nullptr;
	if ( m_pRhi->beginOffscreenFrame( &pCommands ) != QRhi::FrameOpSuccess )
	{
		return {};
	}
	m_pRenderer->Render( pCommands, m_pTarget.get() );
	QRhiReadbackResult result;
	QRhiResourceUpdateBatch *pUpdates = m_pRhi->nextResourceUpdateBatch();
	pUpdates->readBackTexture( { m_pColor.get() }, &result );
	pCommands->resourceUpdate( pUpdates );
	m_pRhi->endOffscreenFrame();
	QImage image( reinterpret_cast<const uchar *>( result.data.constData() ), result.pixelSize.width(), result.pixelSize.height(), QImage::Format_RGBA8888 );
	if ( !m_pRhi->isYUpInFramebuffer() )
	{
		return image.copy();
	}
	// OpenGL's rows run bottom to top.
#if QT_VERSION >= QT_VERSION_CHECK( 6, 9, 0 )
	return image.flipped( Qt::Vertical );
#else
	return image.mirrored( false, true );
#endif
}

#endif
