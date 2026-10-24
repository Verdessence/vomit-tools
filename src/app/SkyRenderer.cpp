//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See SkyRenderer.h.
//
//===========================================================================//
#include "SkyRenderer.h"

#ifdef VOMIT_TOOLS_GPU

#include <cmath>
#include <numbers>

#include <rhi/qrhi.h>

namespace sky = xv::sky;

namespace
{

// The uniform block of sky.frag, std140.
struct SkyUniforms_t
{
	float m_Forward[ 4 ];
	float m_Right[ 4 ];
	float m_Up[ 4 ];
	float m_Size[ 4 ];
	float m_FaceRight[ 6 ][ 4 ];
	float m_FaceUp[ 6 ][ 4 ];
	float m_Transform[ 6 ][ 4 ];
	float m_Translate[ 6 ][ 4 ];
};

//-----------------------------------------------------------------------------
// Purpose: Which of the four samplers a face's transform asks for: repeating in u adds 1, in v 2.
//-----------------------------------------------------------------------------
std::size_t SamplerIndex( const sky::CUvTransform &transform )
{
	return ( transform.m_bRepeatU ? 1u : 0u ) + ( transform.m_bRepeatV ? 2u : 0u );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CSkyRenderer::CSkyRenderer( Source_t pfnSource )
	: m_pfnSource( std::move( pfnSource ) )
{
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CSkyRenderer::~CSkyRenderer() = default;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyRenderer::Release()
{
	m_pPipeline.reset();
	m_pBindings.reset();
	for ( auto &pFace : m_Faces )
	{
		pFace.reset();
	}
	m_pBlack.reset();
	for ( auto &pSampler : m_Samplers )
	{
		pSampler.reset();
	}
	m_pUniforms.reset();
	m_pUploaded = nullptr;
	m_nUploadedGeneration = -1;
	m_pRhi = nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyRenderer::Initialize( QRhi *pRhi, QRhiRenderTarget * )
{
	if ( m_pRhi != pRhi )
	{
		Release();
		m_pRhi = pRhi;
	}
	if ( !m_pUniforms )
	{
		m_pUniforms.reset( m_pRhi->newBuffer( QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof( SkyUniforms_t ) ) );
		m_pUniforms->create();
		for ( std::size_t i = 0; i < m_Samplers.size(); i++ )
		{
			const auto u = ( i & 1 ) != 0 ? QRhiSampler::Repeat : QRhiSampler::ClampToEdge;
			const auto v = ( i & 2 ) != 0 ? QRhiSampler::Repeat : QRhiSampler::ClampToEdge;
			m_Samplers[ i ].reset( m_pRhi->newSampler( QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, u, v ) );
			m_Samplers[ i ]->create();
		}
	}
	// The pipeline follows the target's pass.
	m_pPipeline.reset();
}

//-----------------------------------------------------------------------------
// Purpose: Uploads the cube's faces and binds them, each with the sampler its transform asks for.
//-----------------------------------------------------------------------------
void CSkyRenderer::Upload( const sky::CCube *pCube, QRhiResourceUpdateBatch *pUpdates )
{
	if ( !m_pBlack )
	{
		m_pBlack.reset( m_pRhi->newTexture( QRhiTexture::RGBA8, { 1, 1 } ) );
		m_pBlack->create();
		QImage black( 1, 1, QImage::Format_RGBA8888 );
		black.fill( Qt::black );
		pUpdates->uploadTexture( m_pBlack.get(), black );
	}
	QList<QRhiShaderResourceBinding> bindings{ QRhiShaderResourceBinding::uniformBuffer( 0, QRhiShaderResourceBinding::FragmentStage, m_pUniforms.get() ) };
	for ( sky::Face_t nFace : sky::FACES )
	{
		const std::size_t i = sky::IndexOf( nFace );
		m_Faces[ i ].reset();
		const xv::texture::CImage *pImage = pCube ? &pCube->Face( nFace ) : nullptr;
		if ( pImage && !pImage->Empty() )
		{
			m_Faces[ i ].reset( m_pRhi->newTexture( QRhiTexture::RGBA8, { pImage->m_nWidth, pImage->m_nHeight } ) );
			m_Faces[ i ]->create();
			pUpdates->uploadTexture( m_Faces[ i ].get(), QImage( pImage->m_Pixels.data(), pImage->m_nWidth, pImage->m_nHeight, pImage->m_nWidth * 4, QImage::Format_RGBA8888 ) );
		}
		const sky::CUvTransform transform = pCube ? pCube->m_Transforms[ i ] : sky::CUvTransform{};
		bindings.append( QRhiShaderResourceBinding::sampledTexture( static_cast<int>( i ) + 1, QRhiShaderResourceBinding::FragmentStage, m_Faces[ i ] ? m_Faces[ i ].get() : m_pBlack.get(),
			m_Samplers[ SamplerIndex( transform ) ].get() ) );
	}
	m_pBindings.reset( m_pRhi->newShaderResourceBindings() );
	m_pBindings->setBindings( bindings.begin(), bindings.end() );
	m_pBindings->create();
	m_pPipeline.reset(); // made against the old bindings
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyRenderer::Render( QRhiCommandBuffer *pCommands, QRhiRenderTarget *pTarget )
{
	const Frame_t frame = m_pfnSource ? m_pfnSource() : Frame_t{};
	QRhiResourceUpdateBatch *pUpdates = m_pRhi->nextResourceUpdateBatch();
	if ( !m_pBindings || frame.m_pCube != m_pUploaded || frame.m_nGeneration != m_nUploadedGeneration )
	{
		Upload( frame.m_pCube, pUpdates );
		m_pUploaded = frame.m_pCube;
		m_nUploadedGeneration = frame.m_nGeneration;
	}
	if ( !m_pPipeline )
	{
		m_pPipeline.reset( m_pRhi->newGraphicsPipeline() );
		m_pPipeline->setShaderStages( {
			{ QRhiShaderStage::Vertex, LoadShader( QStringLiteral( ":/shaders/fullscreen.vert.qsb" ) ) },
			{ QRhiShaderStage::Fragment, LoadShader( QStringLiteral( ":/shaders/sky.frag.qsb" ) ) },
		} );
		m_pPipeline->setVertexInputLayout( {} );
		m_pPipeline->setSampleCount( pTarget->sampleCount() );
		m_pPipeline->setShaderResourceBindings( m_pBindings.get() );
		m_pPipeline->setRenderPassDescriptor( pTarget->renderPassDescriptor() );
		m_pPipeline->create();
	}

	// The view as sky::CView has it.
	const QSize size = pTarget->pixelSize();
	const float flTanV = std::tan( frame.m_flFov * 0.5f * std::numbers::pi_v<float> / 180.0f ) * 0.75f;
	const float flTanH = flTanV * static_cast<float>( size.width() ) / static_cast<float>( std::max( size.height(), 1 ) );
	const sky::CVec3 vecForward = sky::Forward( frame.m_Angles );
	const float flYaw = frame.m_Angles.m_flYaw * std::numbers::pi_v<float> / 180.0f;
	const sky::CVec3 vecRight{ std::sin( flYaw ), -std::cos( flYaw ), 0.0f };
	const sky::CVec3 vecUp = sky::Cross( vecRight, vecForward );
	SkyUniforms_t uniforms{};
	const auto put = []( float ( &out )[ 4 ], sky::CVec3 v, float w ) {
		out[ 0 ] = v.m_flX;
		out[ 1 ] = v.m_flY;
		out[ 2 ] = v.m_flZ;
		out[ 3 ] = w;
	};
	put( uniforms.m_Forward, vecForward, flTanH );
	put( uniforms.m_Right, vecRight, flTanV );
	put( uniforms.m_Up, vecUp, 0.0f );
	uniforms.m_Size[ 0 ] = static_cast<float>( size.width() );
	uniforms.m_Size[ 1 ] = static_cast<float>( size.height() );
	uniforms.m_Size[ 2 ] = m_pRhi->isYUpInFramebuffer() ? 1.0f : 0.0f;
	uniforms.m_Size[ 3 ] = frame.m_bEdges ? 1.0f : 0.0f;
	for ( sky::Face_t nFace : sky::FACES )
	{
		const std::size_t i = sky::IndexOf( nFace );
		put( uniforms.m_FaceRight[ i ], sky::Basis( nFace ).m_vecRight, 0.0f );
		put( uniforms.m_FaceUp[ i ], sky::Basis( nFace ).m_vecUp, 0.0f );
		const sky::CUvTransform t = frame.m_pCube ? frame.m_pCube->m_Transforms[ i ] : sky::CUvTransform{};
		uniforms.m_Transform[ i ][ 0 ] = t.m_flA;
		uniforms.m_Transform[ i ][ 1 ] = t.m_flB;
		uniforms.m_Transform[ i ][ 2 ] = t.m_flC;
		uniforms.m_Transform[ i ][ 3 ] = t.m_flD;
		uniforms.m_Translate[ i ][ 0 ] = t.m_flTx;
		uniforms.m_Translate[ i ][ 1 ] = t.m_flTy;
		uniforms.m_Translate[ i ][ 2 ] = m_Faces[ i ] ? 1.0f : 0.0f;
	}
	pUpdates->updateDynamicBuffer( m_pUniforms.get(), 0, sizeof uniforms, &uniforms );

	pCommands->beginPass( pTarget, QColor( 24, 24, 28 ), { 1.0f, 0 }, pUpdates );
	pCommands->setGraphicsPipeline( m_pPipeline.get() );
	pCommands->setViewport( { 0, 0, static_cast<float>( size.width() ), static_cast<float>( size.height() ) } );
	pCommands->setShaderResources( m_pBindings.get() );
	pCommands->draw( 3 );
	pCommands->endPass();
}

#endif
