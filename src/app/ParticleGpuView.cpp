//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See ParticleGpuView.h.
//
//===========================================================================//
#include "ParticleGpuView.h"

#include <algorithm>
#include <cstring>

#include <QFile>
#include <rhi/qrhi.h>

namespace particles = xv::particles;

namespace
{

// Per corner: x, y, depth; u, v; r, g, b, a; addSelf.
constexpr int FLOATS_PER_VERTEX = 10;
constexpr quint32 VERTEX_SIZE = FLOATS_PER_VERTEX * sizeof( float );

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QShader LoadShader( const QString &szPath )
{
	QFile file{ szPath };
	return file.open( QIODevice::ReadOnly ) ? QShader::fromSerialized( file.readAll() ) : QShader{};
}

//-----------------------------------------------------------------------------
// Purpose: Grows a buffer to hold at least size bytes; true if it was made anew.
//-----------------------------------------------------------------------------
bool Reserve( QRhi *pRhi, std::unique_ptr<QRhiBuffer> &pBuffer, QRhiBuffer::Type nType, QRhiBuffer::UsageFlags usage, quint32 nSize )
{
	if ( pBuffer && pBuffer->size() >= nSize )
	{
		return false;
	}
	pBuffer.reset( pRhi->newBuffer( nType, usage, std::max<quint32>( nSize, 4096 ) ) );
	pBuffer->create();
	return true;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CParticleGpuView::CParticleGpuView( Source_t pfnSource, QWidget *pParent )
	: QRhiWidget( pParent ),
	  m_pfnSource( std::move( pfnSource ) )
{
	setObjectName( QStringLiteral( "particleGpuView" ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CParticleGpuView::~CParticleGpuView() = default;

//-----------------------------------------------------------------------------
// Purpose: Sprites were reloaded: textures made from the old ones must go.
//-----------------------------------------------------------------------------
void CParticleGpuView::ForgetTextures()
{
	m_Textures.clear();
	update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleGpuView::releaseResources()
{
	m_Textures.clear();
	m_White = {};
	m_pBlended.reset();
	m_pAdded.reset();
	m_pSampler.reset();
	m_pUniforms.reset();
	m_pIndices.reset();
	m_pVertices.reset();
	m_nVertexCapacity = 0;
	m_nIndexCapacity = 0;
	m_pRhi = nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleGpuView::initialize( QRhiCommandBuffer * )
{
	if ( m_pRhi != rhi() )
	{
		releaseResources();
		m_pRhi = rhi();
	}
	if ( !m_pUniforms )
	{
		m_pUniforms.reset( m_pRhi->newBuffer( QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16 ) );
		m_pUniforms->create();
		m_pSampler.reset( m_pRhi->newSampler( QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear,
			QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge ) );
		m_pSampler->create();
	}
	// The render target may be new (a resize, a different sample count); the pipelines follow it.
	m_pBlended.reset();
	m_pAdded.reset();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::unique_ptr<QRhiGraphicsPipeline> CParticleGpuView::MakePipeline( bool bAdditive )
{
	std::unique_ptr<QRhiGraphicsPipeline> pPipeline{ m_pRhi->newGraphicsPipeline() };
	QRhiGraphicsPipeline::TargetBlend blend;
	blend.enable = true;
	blend.srcColor = QRhiGraphicsPipeline::One;
	blend.dstColor = bAdditive ? QRhiGraphicsPipeline::One : QRhiGraphicsPipeline::OneMinusSrcAlpha;
	blend.srcAlpha = QRhiGraphicsPipeline::One;
	blend.dstAlpha = bAdditive ? QRhiGraphicsPipeline::One : QRhiGraphicsPipeline::OneMinusSrcAlpha;
	pPipeline->setTargetBlends( { blend } );
	pPipeline->setSampleCount( sampleCount() );
	pPipeline->setShaderStages( {
		{ QRhiShaderStage::Vertex, LoadShader( QStringLiteral( ":/shaders/particle.vert.qsb" ) ) },
		{ QRhiShaderStage::Fragment, LoadShader( QStringLiteral( ":/shaders/particle.frag.qsb" ) ) },
	} );
	QRhiVertexInputLayout layout;
	layout.setBindings( { { VERTEX_SIZE } } );
	layout.setAttributes( {
		{ 0, 0, QRhiVertexInputAttribute::Float3, 0 },
		{ 0, 1, QRhiVertexInputAttribute::Float2, 3 * sizeof( float ) },
		{ 0, 2, QRhiVertexInputAttribute::Float4, 5 * sizeof( float ) },
		{ 0, 3, QRhiVertexInputAttribute::Float, 9 * sizeof( float ) },
	} );
	pPipeline->setVertexInputLayout( layout );
	// Any texture's bindings will do for the layout; they all look alike.
	pPipeline->setShaderResourceBindings( m_White.m_pBindings.get() );
	pPipeline->setRenderPassDescriptor( renderTarget()->renderPassDescriptor() );
	pPipeline->create();
	return pPipeline;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CParticleGpuView::Texture_t &CParticleGpuView::TextureFor( const particles::Sprite_t *pSprite, QRhiResourceUpdateBatch *pUpdates )
{
	if ( !pSprite && m_White.m_pTexture )
	{
		return m_White;
	}
	if ( const auto it = m_Textures.find( pSprite ); pSprite && it != m_Textures.end() )
	{
		return it->second;
	}
	std::vector<QImage> levels;
	if ( pSprite && !pSprite->m_Mips.empty() )
	{
		for ( const auto &mip : pSprite->m_Mips )
		{
			levels.push_back( QImage( mip.m_Pixels.data(), mip.m_nWidth, mip.m_nHeight, mip.m_nWidth * 4, QImage::Format_RGBA8888 ).copy() );
		}
	}
	else
	{
		QImage white( 4, 4, QImage::Format_RGBA8888 );
		white.fill( Qt::white );
		levels = { white, white.scaled( 2, 2 ), white.scaled( 1, 1 ) };
	}
	Texture_t &entry = pSprite ? m_Textures[ pSprite ] : m_White;
	entry.m_pTexture.reset( m_pRhi->newTexture( QRhiTexture::RGBA8, levels.front().size(), 1, QRhiTexture::MipMapped ) );
	entry.m_pTexture->create();
	QList<QRhiTextureUploadEntry> uploads;
	for ( int nLevel = 0; nLevel < static_cast<int>( levels.size() ); nLevel++ )
	{
		uploads.append( { 0, nLevel, QRhiTextureSubresourceUploadDescription( levels[ static_cast<std::size_t>( nLevel ) ] ) } );
	}
	QRhiTextureUploadDescription description;
	description.setEntries( uploads.begin(), uploads.end() );
	pUpdates->uploadTexture( entry.m_pTexture.get(), description );
	entry.m_pBindings.reset( m_pRhi->newShaderResourceBindings() );
	entry.m_pBindings->setBindings( {
		QRhiShaderResourceBinding::uniformBuffer( 0, QRhiShaderResourceBinding::VertexStage, m_pUniforms.get() ),
		QRhiShaderResourceBinding::sampledTexture( 1, QRhiShaderResourceBinding::FragmentStage, entry.m_pTexture.get(), m_pSampler.get() ),
	} );
	entry.m_pBindings->create();
	return entry;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleGpuView::render( QRhiCommandBuffer *pCommands )
{
	const QSize size = renderTarget()->pixelSize();
	std::vector<particles::ScreenQuad_t> quads;
	std::array<float, 3> vecBackground{ 0.13f, 0.14f, 0.15f };
	if ( m_pfnSource )
	{
		m_pfnSource( size.width(), size.height(), quads, vecBackground );
	}
	QRhiResourceUpdateBatch *pUpdates = m_pRhi->nextResourceUpdateBatch();

	// Textures first (the pipelines need one to describe their bindings), then the vertices, in
	// runs of quads that share a texture and a blend.
	TextureFor( nullptr, pUpdates );
	struct Run_t
	{
		Texture_t *m_pTexture = nullptr;
		bool m_bAdditive = false;
		quint32 m_nFirst = 0; // quads
		quint32 m_nCount = 0;
	};
	std::vector<Run_t> runs;
	std::vector<float> data;
	data.reserve( quads.size() * 4 * FLOATS_PER_VERTEX );
	for ( const particles::ScreenQuad_t &quad : quads )
	{
		Texture_t *pTexture = &TextureFor( quad.m_pSprite && quad.m_pSprite->m_bVisible ? quad.m_pSprite : nullptr, pUpdates );
		const bool bAdditive = quad.m_nBlend == particles::Sprite_t::Blend_t::BLEND_ADDITIVE;
		if ( runs.empty() || runs.back().m_pTexture != pTexture || runs.back().m_bAdditive != bAdditive )
		{
			runs.push_back( { pTexture, bAdditive, static_cast<quint32>( data.size() / ( 4 * FLOATS_PER_VERTEX ) ), 0 } );
		}
		runs.back().m_nCount++;
		const std::array<std::array<float, 2>, 4> uv{ { { quad.m_Rect[ 0 ], quad.m_Rect[ 1 ] }, { quad.m_Rect[ 2 ], quad.m_Rect[ 1 ] }, { quad.m_Rect[ 2 ], quad.m_Rect[ 3 ] }, { quad.m_Rect[ 0 ], quad.m_Rect[ 3 ] } } };
		for ( std::size_t c = 0; c < 4; c++ )
		{
			data.insert( data.end(), { quad.m_Corners[ c ][ 0 ], quad.m_Corners[ c ][ 1 ], quad.m_Corners[ c ][ 2 ], uv[ c ][ 0 ], uv[ c ][ 1 ], quad.m_vecColor[ 0 ], quad.m_vecColor[ 1 ], quad.m_vecColor[ 2 ], quad.m_flAlpha, quad.m_flAddSelf } );
		}
	}
	const auto nQuadCount = static_cast<quint32>( data.size() / ( 4 * FLOATS_PER_VERTEX ) );
	if ( nQuadCount > 0 )
	{
		Reserve( m_pRhi, m_pVertices, QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, nQuadCount * 4 * VERTEX_SIZE );
		pUpdates->updateDynamicBuffer( m_pVertices.get(), 0, static_cast<quint32>( data.size() * sizeof( float ) ), data.data() );
		if ( nQuadCount > m_nIndexCapacity )
		{
			// Two triangles a quad; the index buffer only ever grows.
			const quint32 nCapacity = std::max<quint32>( nQuadCount, 1024 ) * 2;
			std::vector<quint32> indices;
			indices.reserve( nCapacity * 6 );
			for ( quint32 q = 0; q < nCapacity; q++ )
			{
				const quint32 v = q * 4;
				indices.insert( indices.end(), { v, v + 1, v + 2, v, v + 2, v + 3 } );
			}
			Reserve( m_pRhi, m_pIndices, QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, static_cast<quint32>( indices.size() * sizeof( quint32 ) ) );
			pUpdates->uploadStaticBuffer( m_pIndices.get(), indices.data() );
			m_nIndexCapacity = nCapacity;
		}
	}
	const float frame[ 4 ]{ static_cast<float>( size.width() ), static_cast<float>( size.height() ), m_pRhi->isYUpInNDC() ? 1.0f : -1.0f, 0.0f };
	pUpdates->updateDynamicBuffer( m_pUniforms.get(), 0, sizeof frame, frame );

	if ( !m_pBlended )
	{
		m_pBlended = MakePipeline( false );
		m_pAdded = MakePipeline( true );
	}

	pCommands->beginPass( renderTarget(), QColor::fromRgbF( vecBackground[ 0 ], vecBackground[ 1 ], vecBackground[ 2 ] ), { 1.0f, 0 }, pUpdates );
	pCommands->setViewport( { 0, 0, static_cast<float>( size.width() ), static_cast<float>( size.height() ) } );
	for ( const Run_t &run : runs )
	{
		pCommands->setGraphicsPipeline( run.m_bAdditive ? m_pAdded.get() : m_pBlended.get() );
		pCommands->setShaderResources( run.m_pTexture->m_pBindings.get() );
		const QRhiCommandBuffer::VertexInput input{ m_pVertices.get(), 0 };
		pCommands->setVertexInput( 0, 1, &input, m_pIndices.get(), 0, QRhiCommandBuffer::IndexUInt32 );
		pCommands->drawIndexed( run.m_nCount * 6, 1, run.m_nFirst * 6 );
	}
	pCommands->endPass();
}
