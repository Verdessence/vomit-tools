//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See ParticleRenderer.h.
//
//===========================================================================//
#include "ParticleRenderer.h"

#ifdef VOMIT_TOOLS_GPU

#include <algorithm>

#include <rhi/qrhi.h>

namespace particles = xv::particles;

namespace
{

// Per corner: x, y, depth; u, v; r, g, b, a; addSelf.
constexpr int FLOATS_PER_VERTEX = 10;
constexpr quint32 VERTEX_SIZE = FLOATS_PER_VERTEX * sizeof( float );

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

//-----------------------------------------------------------------------------
// Purpose: A picture's place as a scissor, which counts from the bottom-left corner.
//-----------------------------------------------------------------------------
QRhiScissor ScissorFor( const QRect &rect, QSize target )
{
	return { rect.x(), target.height() - rect.y() - rect.height(), rect.width(), rect.height() };
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CParticleRenderer::CParticleRenderer( Source_t pfnSource )
	: m_pfnSource( std::move( pfnSource ) )
{
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CParticleRenderer::~CParticleRenderer() = default;

//-----------------------------------------------------------------------------
// Purpose: Sprites were reloaded: textures made from the old ones must go.
//-----------------------------------------------------------------------------
void CParticleRenderer::ForgetTextures()
{
	m_Textures.clear();
	m_ModelTextures.clear();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleRenderer::Release()
{
	m_Textures.clear();
	m_ModelTextures.clear();
	m_White = {};
	m_ModelWhite = {};
	m_pBlended.reset();
	m_pAdded.reset();
	m_pSolid.reset();
	m_pModelVertices.reset();
	m_pRepeatSampler.reset();
	m_pSampler.reset();
	m_pUniforms.reset();
	m_pIndices.reset();
	m_pVertices.reset();
	m_nIndexCapacity = 0;
	m_pRhi = nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleRenderer::Initialize( QRhi *pRhi, QRhiRenderTarget * )
{
	if ( m_pRhi != pRhi )
	{
		Release();
		m_pRhi = pRhi;
	}
	if ( !m_pUniforms )
	{
		m_pUniforms.reset( m_pRhi->newBuffer( QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16 ) );
		m_pUniforms->create();
		m_pSampler.reset( m_pRhi->newSampler( QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge ) );
		m_pSampler->create();
		m_pRepeatSampler.reset( m_pRhi->newSampler( QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Repeat, QRhiSampler::Repeat ) );
		m_pRepeatSampler->create();
	}
	// The render target may be new (a resize, a different sample count); the pipelines follow it.
	m_pBlended.reset();
	m_pAdded.reset();
	m_pSolid.reset();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::unique_ptr<QRhiGraphicsPipeline> CParticleRenderer::MakePipeline( Pipeline_t nKind, QRhiRenderTarget *pTarget )
{
	std::unique_ptr<QRhiGraphicsPipeline> pPipeline{ m_pRhi->newGraphicsPipeline() };
	const bool bAdditive = nKind == Pipeline_t::PIPELINE_ADDED;
	const bool bSolid = nKind == Pipeline_t::PIPELINE_SOLID;
	// Particles are hidden by the model in front of them but don't hide each other: they're
	// drawn in their own order, back to front.
	pPipeline->setFlags( QRhiGraphicsPipeline::UsesScissor );
	pPipeline->setDepthTest( true );
	pPipeline->setDepthWrite( bSolid );
	pPipeline->setDepthOp( QRhiGraphicsPipeline::LessOrEqual );
	QRhiGraphicsPipeline::TargetBlend blend;
	blend.enable = !bSolid;
	blend.srcColor = QRhiGraphicsPipeline::One;
	blend.dstColor = bAdditive ? QRhiGraphicsPipeline::One : QRhiGraphicsPipeline::OneMinusSrcAlpha;
	blend.srcAlpha = QRhiGraphicsPipeline::One;
	blend.dstAlpha = bAdditive ? QRhiGraphicsPipeline::One : QRhiGraphicsPipeline::OneMinusSrcAlpha;
	pPipeline->setTargetBlends( { blend } );
	pPipeline->setSampleCount( pTarget->sampleCount() );
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
	pPipeline->setRenderPassDescriptor( pTarget->renderPassDescriptor() );
	pPipeline->create();
	return pPipeline;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CParticleRenderer::Texture_t &CParticleRenderer::TextureFor( const particles::Sprite_t *pSprite, QRhiResourceUpdateBatch *pUpdates, bool bRepeat )
{
	Texture_t &white = bRepeat ? m_ModelWhite : m_White;
	auto &cache = bRepeat ? m_ModelTextures : m_Textures;
	if ( !pSprite && white.m_pTexture )
	{
		return white;
	}
	if ( const auto it = cache.find( pSprite ); pSprite && it != cache.end() )
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
		// Plain: white for particles' grid lines, grey for a model without its texture.
		QImage plain( 4, 4, QImage::Format_RGBA8888 );
		plain.fill( bRepeat ? QColor( 153, 153, 153 ) : QColor( Qt::white ) );
		levels = { plain, plain.scaled( 2, 2 ), plain.scaled( 1, 1 ) };
	}
	Texture_t &entry = pSprite ? cache[ pSprite ] : white;
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
		QRhiShaderResourceBinding::sampledTexture( 1, QRhiShaderResourceBinding::FragmentStage, entry.m_pTexture.get(), bRepeat ? m_pRepeatSampler.get() : m_pSampler.get() ),
	} );
	entry.m_pBindings->create();
	return entry;
}

//-----------------------------------------------------------------------------
// Purpose: Every picture in one pass: each picture's model, then its quads, kept to its place.
//-----------------------------------------------------------------------------
void CParticleRenderer::Render( QRhiCommandBuffer *pCommands, QRhiRenderTarget *pTarget )
{
	RenderWith( pCommands, pTarget, {}, {} );
}
//-----------------------------------------------------------------------------
// Purpose: Shares a single render pass with a caller's mesh renderer and its inspection layers.
//-----------------------------------------------------------------------------
void CParticleRenderer::RenderWith( QRhiCommandBuffer *pCommands, QRhiRenderTarget *pTarget, const std::function<void( QRhiResourceUpdateBatch * )> &pfnPrepare, const std::function<void()> &pfnDraw )
{
	const QSize size = pTarget->pixelSize();
	std::vector<ParticlePicture_t> pictures;
	std::array<float, 3> vecBackground{ 0.13f, 0.14f, 0.15f };
	if ( m_pfnSource )
	{
		m_pfnSource( size, pictures, vecBackground );
	}
	QRhiResourceUpdateBatch *pUpdates = m_pRhi->nextResourceUpdateBatch();

	// Textures first (the pipelines need one to describe their bindings), then the vertices, in
	// runs of quads that share a picture, a texture and a blend.
	TextureFor( nullptr, pUpdates );
	struct Run_t
	{
		std::size_t m_nPicture = 0;
		Texture_t *m_pTexture = nullptr;
		bool m_bAdditive = false;
		quint32 m_nFirst = 0; // quads, or vertices for a model
		quint32 m_nCount = 0;
	};
	std::vector<Run_t> runs;
	std::vector<Run_t> meshRuns;
	std::vector<float> data;
	std::vector<float> modelData;
	for ( std::size_t nPicture = 0; nPicture < pictures.size(); nPicture++ )
	{
		const ParticlePicture_t &picture = pictures[ nPicture ];
		const auto x = static_cast<float>( picture.m_Rect.x() );
		const auto y = static_cast<float>( picture.m_Rect.y() );
		bool bNewPicture = true;
		for ( const particles::ScreenQuad_t &quad : picture.m_Quads )
		{
			Texture_t *pTexture = &TextureFor( quad.m_pSprite && quad.m_pSprite->m_bVisible ? quad.m_pSprite : nullptr, pUpdates );
			const bool bAdditive = quad.m_nBlend == particles::Sprite_t::Blend_t::BLEND_ADDITIVE;
			if ( bNewPicture || runs.back().m_pTexture != pTexture || runs.back().m_bAdditive != bAdditive )
			{
				runs.push_back( { nPicture, pTexture, bAdditive, static_cast<quint32>( data.size() / ( 4 * FLOATS_PER_VERTEX ) ), 0 } );
				bNewPicture = false;
			}
			runs.back().m_nCount++;
			const std::array<std::array<float, 2>, 4> uv{ { { quad.m_Rect[ 0 ], quad.m_Rect[ 1 ] }, { quad.m_Rect[ 2 ], quad.m_Rect[ 1 ] }, { quad.m_Rect[ 2 ], quad.m_Rect[ 3 ] }, { quad.m_Rect[ 0 ], quad.m_Rect[ 3 ] } } };
			for ( std::size_t c = 0; c < 4; c++ )
			{
				data.insert( data.end(), { quad.m_Corners[ c ][ 0 ] + x, quad.m_Corners[ c ][ 1 ] + y, quad.m_Corners[ c ][ 2 ], uv[ c ][ 0 ], uv[ c ][ 1 ], quad.m_vecColor[ 0 ], quad.m_vecColor[ 1 ], quad.m_vecColor[ 2 ], quad.m_flAlpha, quad.m_flAddSelf } );
			}
		}
		// The model: its triangles in one buffer, a run per texture.
		for ( const particles::ScreenMesh_t &mesh : picture.m_Meshes )
		{
			if ( mesh.m_Vertices.empty() )
			{
				continue;
			}
			meshRuns.push_back( { nPicture, &TextureFor( mesh.m_pSprite, pUpdates, true ), false, static_cast<quint32>( modelData.size() / FLOATS_PER_VERTEX ), static_cast<quint32>( mesh.m_Vertices.size() ) } );
			for ( const particles::ScreenVertex_t &vertex : mesh.m_Vertices )
			{
				modelData.insert( modelData.end(), { vertex.m_vecPosition[ 0 ] + x, vertex.m_vecPosition[ 1 ] + y, vertex.m_vecPosition[ 2 ], vertex.m_Uv[ 0 ], vertex.m_Uv[ 1 ], vertex.m_vecColor[ 0 ], vertex.m_vecColor[ 1 ], vertex.m_vecColor[ 2 ], 1.0f, 0.0f } );
			}
		}
	}
	if ( !modelData.empty() )
	{
		Reserve( m_pRhi, m_pModelVertices, QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, static_cast<quint32>( modelData.size() * sizeof( float ) ) );
		pUpdates->updateDynamicBuffer( m_pModelVertices.get(), 0, static_cast<quint32>( modelData.size() * sizeof( float ) ), modelData.data() );
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
	const float frame[ 4 ]{ static_cast<float>( size.width() ), static_cast<float>( size.height() ), m_pRhi->isYUpInNDC() ? 1.0f : -1.0f, m_pRhi->isClipDepthZeroToOne() ? 1.0f : 0.0f };
	pUpdates->updateDynamicBuffer( m_pUniforms.get(), 0, sizeof frame, frame );

	if ( !m_pBlended )
	{
		m_pBlended = MakePipeline( Pipeline_t::PIPELINE_BLENDED, pTarget );
		m_pAdded = MakePipeline( Pipeline_t::PIPELINE_ADDED, pTarget );
		m_pSolid = MakePipeline( Pipeline_t::PIPELINE_SOLID, pTarget );
	}

	if ( pfnPrepare )
		pfnPrepare( pUpdates );
	pCommands->beginPass( pTarget, QColor::fromRgbF( vecBackground[ 0 ], vecBackground[ 1 ], vecBackground[ 2 ] ), { 1.0f, 0 }, pUpdates );
	pCommands->setViewport( { 0, 0, static_cast<float>( size.width() ), static_cast<float>( size.height() ) } );
	if ( pfnDraw )
		pfnDraw();
	if ( !meshRuns.empty() )
	{
		pCommands->setGraphicsPipeline( m_pSolid.get() );
		const QRhiCommandBuffer::VertexInput input{ m_pModelVertices.get(), 0 };
		for ( const Run_t &run : meshRuns )
		{
			pCommands->setScissor( ScissorFor( pictures[ run.m_nPicture ].m_Rect, size ) );
			pCommands->setShaderResources( run.m_pTexture->m_pBindings.get() );
			pCommands->setVertexInput( 0, 1, &input );
			pCommands->draw( run.m_nCount, 1, run.m_nFirst );
		}
	}
	for ( const Run_t &run : runs )
	{
		pCommands->setGraphicsPipeline( run.m_bAdditive ? m_pAdded.get() : m_pBlended.get() );
		pCommands->setScissor( ScissorFor( pictures[ run.m_nPicture ].m_Rect, size ) );
		pCommands->setShaderResources( run.m_pTexture->m_pBindings.get() );
		const QRhiCommandBuffer::VertexInput input{ m_pVertices.get(), 0 };
		pCommands->setVertexInput( 0, 1, &input, m_pIndices.get(), 0, QRhiCommandBuffer::IndexUInt32 );
		pCommands->drawIndexed( run.m_nCount * 6, 1, run.m_nFirst * 6 );
	}
	pCommands->endPass();
}

#endif
