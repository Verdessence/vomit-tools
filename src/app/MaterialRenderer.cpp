//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MaterialRenderer.h.
//
//===========================================================================//
#include "MaterialRenderer.h"

#ifdef VOMIT_TOOLS_GPU

#include <algorithm>

#include <rhi/qrhi.h>

namespace material = xv::material;
namespace sky = xv::sky;

namespace
{

constexpr std::size_t CUBE_BINDING = 10;     // after the ten 2D textures
constexpr std::size_t HDR_LIGHT = 9;         // the HDR texture's light values
constexpr std::uint64_t KEEP_PICTURES = 120; // pictures a texture stays unused before it goes

// The uniform block of material.frag, std140: vec4s, in its order.
struct MaterialUniforms_t
{
	float m_Square[ 4 ];
	float m_Options[ 4 ];
	float m_Light[ 4 ];
	float m_Look[ 4 ];
	float m_Color[ 4 ];
	float m_Color2[ 4 ];
	float m_Blending[ 4 ];
	float m_SelfIllum[ 4 ];
	float m_EnvMasks[ 4 ];
	float m_EnvTint[ 4 ];
	float m_EnvSaturation[ 4 ];
	float m_DetailTint[ 4 ];
	float m_Phong[ 4 ];
	float m_PhongFresnel[ 4 ];
	float m_PhongTint[ 4 ];
	float m_Sprite[ 4 ];
	float m_RefractTint[ 4 ];
	float m_Fog[ 4 ];
	float m_Hdr[ 4 ];
	float m_Valid[ 3 ][ 4 ];
	float m_Transform[ 10 ][ 4 ];
	float m_Translate[ 10 ][ 4 ];
	float m_FaceRight[ 6 ][ 4 ];
	float m_FaceUp[ 6 ][ 4 ];
	float m_Faces[ 2 ][ 4 ];
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void Put( float ( &out )[ 4 ], float x, float y, float z, float w )
{
	out[ 0 ] = x;
	out[ 1 ] = y;
	out[ 2 ] = z;
	out[ 3 ] = w;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void Put( float ( &out )[ 4 ], xv::CVec3 v, float w )
{
	Put( out, v.m_flX, v.m_flY, v.m_flZ, w );
}

//-----------------------------------------------------------------------------
// Purpose: A flag for a slot of a vec4 array, four to a vec4.
//-----------------------------------------------------------------------------
template <std::size_t N>
void Flag( float ( &flags )[ N ][ 4 ], std::size_t nIndex, bool bOn )
{
	flags[ nIndex / 4 ][ nIndex % 4 ] = bOn ? 1.0f : 0.0f;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMaterialRenderer::CMaterialRenderer( Source_t pfnSource )
	: m_pfnSource( std::move( pfnSource ) )
{
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMaterialRenderer::~CMaterialRenderer() = default;

//-----------------------------------------------------------------------------
// Purpose: The textures were read again: what was uploaded from the old ones must go.
//-----------------------------------------------------------------------------
void CMaterialRenderer::ForgetTextures()
{
	m_pBindings.reset();
	m_Bound = {};
	m_Textures.clear();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterialRenderer::Release()
{
	m_pPipeline.reset();
	m_pBindings.reset();
	m_Bound = {};
	m_Textures.clear();
	m_pWhite.reset();
	m_pWhiteCube.reset();
	m_Samplers.clear();
	m_pUniforms.reset();
	m_pRhi = nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterialRenderer::Initialize( QRhi *pRhi, QRhiRenderTarget * )
{
	if ( m_pRhi != pRhi )
	{
		Release();
		m_pRhi = pRhi;
	}
	if ( !m_pUniforms )
	{
		m_pUniforms.reset( m_pRhi->newBuffer( QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof( MaterialUniforms_t ) ) );
		m_pUniforms->create();
	}
	// The pipeline follows the target's pass.
	m_pPipeline.reset();
}

//-----------------------------------------------------------------------------
// Purpose: Linear or point, clamped or repeating in s and t.
//-----------------------------------------------------------------------------
QRhiSampler *CMaterialRenderer::SamplerFor( bool bClampS, bool bClampT, bool bPoint )
{
	std::unique_ptr<QRhiSampler> &pSampler = m_Samplers[ { bClampS, bClampT, bPoint } ];
	if ( !pSampler )
	{
		const auto nFilter = bPoint ? QRhiSampler::Nearest : QRhiSampler::Linear;
		pSampler.reset( m_pRhi->newSampler( nFilter, nFilter, QRhiSampler::None, bClampS ? QRhiSampler::ClampToEdge : QRhiSampler::Repeat, bClampT ? QRhiSampler::ClampToEdge : QRhiSampler::Repeat ) );
		pSampler->create();
	}
	return pSampler.get();
}

//-----------------------------------------------------------------------------
// Purpose: A texture made from an image, uploaded once and kept while it is used.
//-----------------------------------------------------------------------------
QRhiTexture *CMaterialRenderer::TextureFor( const xv::texture::CImage *pImage, QRhiResourceUpdateBatch *pUpdates )
{
	Cached_t &cached = m_Textures[ pImage ];
	cached.m_nLastUsed = m_nFrame;
	if ( !cached.m_pTexture )
	{
		cached.m_pTexture.reset( m_pRhi->newTexture( QRhiTexture::RGBA8, { pImage->m_nWidth, pImage->m_nHeight } ) );
		cached.m_pTexture->create();
		pUpdates->uploadTexture( cached.m_pTexture.get(), QImage( pImage->m_Pixels.data(), pImage->m_nWidth, pImage->m_nHeight, pImage->m_nWidth * 4, QImage::Format_RGBA8888 ) );
	}
	return cached.m_pTexture.get();
}

//-----------------------------------------------------------------------------
// Purpose: An HDR texture's light values, as floats.
//-----------------------------------------------------------------------------
QRhiTexture *CMaterialRenderer::LightFor( const xv::texture::CFloatImage *pLight, QRhiResourceUpdateBatch *pUpdates )
{
	Cached_t &cached = m_Textures[ pLight ];
	cached.m_nLastUsed = m_nFrame;
	if ( !cached.m_pTexture )
	{
		cached.m_pTexture.reset( m_pRhi->newTexture( QRhiTexture::RGBA32F, { pLight->m_nWidth, pLight->m_nHeight } ) );
		cached.m_pTexture->create();
		const QRhiTextureSubresourceUploadDescription data( pLight->m_Pixels.data(), static_cast<quint32>( pLight->m_Pixels.size() * sizeof( float ) ) );
		pUpdates->uploadTexture( cached.m_pTexture.get(), QRhiTextureUploadDescription( { 0, 0, data } ) );
	}
	return cached.m_pTexture.get();
}

//-----------------------------------------------------------------------------
// Purpose: A cube map's faces as the layers of one texture, by sky::IndexOf; a missing face is
//          black.
//-----------------------------------------------------------------------------
QRhiTexture *CMaterialRenderer::CubeFor( const sky::CCube *pCube, QRhiResourceUpdateBatch *pUpdates )
{
	Cached_t &cached = m_Textures[ pCube ];
	cached.m_nLastUsed = m_nFrame;
	if ( !cached.m_pTexture )
	{
		QSize size{ 1, 1 };
		for ( const xv::texture::CImage &face : pCube->m_Faces )
		{
			if ( !face.Empty() )
			{
				size = { face.m_nWidth, face.m_nHeight };
				break;
			}
		}
		cached.m_pTexture.reset( m_pRhi->newTextureArray( QRhiTexture::RGBA8, 6, size ) );
		cached.m_pTexture->create();
		QList<QRhiTextureUploadEntry> layers;
		for ( std::size_t i = 0; i < pCube->m_Faces.size(); i++ )
		{
			const xv::texture::CImage &face = pCube->m_Faces[ i ];
			QImage image = face.Empty() ? QImage{} : QImage( face.m_Pixels.data(), face.m_nWidth, face.m_nHeight, face.m_nWidth * 4, QImage::Format_RGBA8888 );
			if ( image.isNull() )
			{
				image = QImage( size, QImage::Format_RGBA8888 );
				image.fill( Qt::black );
			}
			else if ( image.size() != size )
			{
				image = image.scaled( size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation );
			}
			layers.append( { static_cast<int>( i ), 0, QRhiTextureSubresourceUploadDescription( image ) } );
		}
		QRhiTextureUploadDescription description;
		description.setEntries( layers.begin(), layers.end() );
		pUpdates->uploadTexture( cached.m_pTexture.get(), description );
	}
	return cached.m_pTexture.get();
}

//-----------------------------------------------------------------------------
// Purpose: Drops textures not used for a while (an animated texture's frames come and go).
//-----------------------------------------------------------------------------
void CMaterialRenderer::Trim()
{
	std::erase_if( m_Textures, [ this ]( const auto &entry ) { return entry.second.m_nLastUsed + KEEP_PICTURES < m_nFrame; } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterialRenderer::Render( QRhiCommandBuffer *pCommands, QRhiRenderTarget *pTarget )
{
	const Frame_t frame = m_pfnSource ? m_pfnSource() : Frame_t{};
	m_nFrame++;
	QRhiResourceUpdateBatch *pUpdates = m_pRhi->nextResourceUpdateBatch();
	if ( !m_pWhite )
	{
		QImage white( 1, 1, QImage::Format_RGBA8888 );
		white.fill( Qt::white );
		m_pWhite.reset( m_pRhi->newTexture( QRhiTexture::RGBA8, { 1, 1 } ) );
		m_pWhite->create();
		pUpdates->uploadTexture( m_pWhite.get(), white );
		m_pWhiteCube.reset( m_pRhi->newTextureArray( QRhiTexture::RGBA8, 6, { 1, 1 } ) );
		m_pWhiteCube->create();
		QList<QRhiTextureUploadEntry> layers;
		for ( int i = 0; i < 6; i++ )
		{
			layers.append( { i, 0, QRhiTextureSubresourceUploadDescription( white ) } );
		}
		QRhiTextureUploadDescription description;
		description.setEntries( layers.begin(), layers.end() );
		pUpdates->uploadTexture( m_pWhiteCube.get(), description );
	}

	const QSize size = pTarget->pixelSize();
	const material::PreviewOptions_t &options = frame.m_Options;
	MaterialUniforms_t u{};
	Put( u.m_Square, static_cast<float>( frame.m_Corner.x() ), static_cast<float>( frame.m_Corner.y() ), static_cast<float>( options.m_nSize ), static_cast<float>( size.height() ) );
	Put( u.m_Options, options.m_nShape == material::Shape_t::SHAPE_SPHERE ? 1.0f : 0.0f, options.m_flRepeat, static_cast<float>( options.m_nBackdrop ), options.m_flExposure );
	Put( u.m_Light, options.m_vecLight, m_pRhi->isYUpInFramebuffer() ? 1.0f : 0.0f );
	std::array<std::pair<QRhiTexture *, QRhiSampler *>, 11> bound;
	bound.fill( { m_pWhite.get(), SamplerFor( false, false, false ) } );
	bound[ CUBE_BINDING ] = { m_pWhiteCube.get(), SamplerFor( true, true, false ) };
	for ( std::size_t i = 0; i < 10; i++ )
	{
		Put( u.m_Transform[ i ], 1.0f, 0.0f, 0.0f, 1.0f );
		Put( u.m_Translate[ i ], 0.0f, 0.0f, 1.0f, 1.0f );
	}
	for ( sky::Face_t nFace : sky::FACES )
	{
		Put( u.m_FaceRight[ sky::IndexOf( nFace ) ], sky::Basis( nFace ).m_vecRight, 0.0f );
		Put( u.m_FaceUp[ sky::IndexOf( nFace ) ], sky::Basis( nFace ).m_vecUp, 0.0f );
	}

	if ( frame.m_pMaterial && frame.m_pTextures )
	{
		const material::Shading_t s = material::ReadShading( *frame.m_pMaterial, *frame.m_pTextures, options );
		const std::array<const material::Sampler_t *, 9> samplers{ &s.m_Base, &s.m_Base2, &s.m_Detail, &s.m_Bump, &s.m_EnvMask, &s.m_SelfIllumMask, &s.m_PhongExponent, &s.m_RefractTint, &s.m_Hdr };
		for ( std::size_t i = 0; i < samplers.size(); i++ )
		{
			const material::Sampler_t &sampler = *samplers[ i ];
			Flag( u.m_Valid, i, sampler.IsValid() );
			if ( !sampler.IsValid() )
			{
				continue;
			}
			bound[ i ] = { TextureFor( sampler.m_pImage, pUpdates ), SamplerFor( sampler.m_bClampS, sampler.m_bClampT, sampler.m_bPoint ) };
			const sky::CUvTransform &t = sampler.m_Transform;
			Put( u.m_Transform[ i ], t.m_flA, t.m_flB, t.m_flC, t.m_flD );
			Put( u.m_Translate[ i ], t.m_flTx, t.m_flTy, sampler.m_flScaleU, sampler.m_flScaleV );
		}
		// The HDR texture's light values, nearest texel, as Sampler_t::LightAt reads them.
		if ( s.m_Hdr.IsValid() && s.m_Hdr.m_pLight && !s.m_Hdr.m_pLight->Empty() )
		{
			bound[ HDR_LIGHT ] = { LightFor( s.m_Hdr.m_pLight, pUpdates ), SamplerFor( s.m_Hdr.m_bClampS, s.m_Hdr.m_bClampT, true ) };
			std::copy_n( u.m_Transform[ 8 ], 4, u.m_Transform[ HDR_LIGHT ] );
			Put( u.m_Translate[ HDR_LIGHT ], s.m_Hdr.m_Transform.m_flTx, s.m_Hdr.m_Transform.m_flTy, 1.0f, 1.0f );
			Flag( u.m_Valid, HDR_LIGHT, true );
			u.m_Hdr[ 1 ] = 1.0f;
		}
		const bool bCube = s.m_pEnvMap && s.m_pEnvMap->m_Cube;
		if ( bCube )
		{
			bound[ CUBE_BINDING ].first = CubeFor( &*s.m_pEnvMap->m_Cube, pUpdates );
			for ( std::size_t i = 0; i < 6; i++ )
			{
				Flag( u.m_Faces, i, !s.m_pEnvMap->m_Cube->m_Faces[ i ].Empty() );
			}
		}
		const auto on = []( bool b ) { return b ? 1.0f : 0.0f; };
		Put( u.m_Look, static_cast<float>( s.m_nLook ), static_cast<float>( s.m_nDetailMode ), on( s.m_bHalfLambert ), on( s.m_bSsBump ) );
		Put( u.m_Color, s.m_vecColor, s.m_flAlpha );
		Put( u.m_Color2, s.m_vecColor2, on( s.m_szKind == "vertexlitgeneric" ) );
		Put( u.m_Blending, on( s.m_bTranslucent ), on( s.m_bAdditive ), on( s.m_bAlphaTest ), s.m_flAlphaReference );
		Put( u.m_SelfIllum, s.m_vecSelfIllumTint, on( s.m_bSelfIllum ) );
		Put( u.m_EnvMasks, on( s.m_bBaseAlphaEnvMask ), on( s.m_bNormalAlphaEnvMask ), on( s.m_bEnvMap ), on( bCube ) );
		Put( u.m_EnvTint, s.m_vecEnvTint, s.m_flEnvContrast );
		Put( u.m_EnvSaturation, s.m_vecEnvSaturation, s.m_flTintReplace );
		Put( u.m_DetailTint, s.m_vecDetailTint, s.m_flDetailFactor );
		Put( u.m_Phong, on( s.m_bPhong ), s.m_flPhongExponent, s.m_flPhongBoost, on( s.m_bBaseAlphaPhongMask ) );
		Put( u.m_PhongFresnel, s.m_vecPhongFresnel, on( s.m_bHdrCompressed ) );
		Put( u.m_PhongTint, s.m_vecPhongTint, s.m_flOverbright );
		Put( u.m_Sprite, s.m_flAddSelf, on( s.m_bMod2x ), s.m_flRefractAmount, on( s.m_bTwoTextures ) );
		Put( u.m_RefractTint, s.m_vecRefractTint, on( s.m_bBlendTextures ) );
		Put( u.m_Fog, s.m_vecFog, on( s.m_bBlendTint ) );
		u.m_Hdr[ 0 ] = s.m_flHdrScale;
	}
	pUpdates->updateDynamicBuffer( m_pUniforms.get(), 0, sizeof u, &u );

	if ( !m_pBindings || bound != m_Bound )
	{
		QList<QRhiShaderResourceBinding> bindings{ QRhiShaderResourceBinding::uniformBuffer( 0, QRhiShaderResourceBinding::FragmentStage, m_pUniforms.get() ) };
		for ( std::size_t i = 0; i < bound.size(); i++ )
		{
			bindings.append( QRhiShaderResourceBinding::sampledTexture( static_cast<int>( i ) + 1, QRhiShaderResourceBinding::FragmentStage, bound[ i ].first, bound[ i ].second ) );
		}
		m_pBindings.reset( m_pRhi->newShaderResourceBindings() );
		m_pBindings->setBindings( bindings.begin(), bindings.end() );
		m_pBindings->create();
		m_Bound = bound;
	}
	if ( !m_pPipeline )
	{
		// Bindings made later have the same layout, so the pipeline takes them too.
		m_pPipeline.reset( m_pRhi->newGraphicsPipeline() );
		m_pPipeline->setShaderStages( {
			{ QRhiShaderStage::Vertex, LoadShader( QStringLiteral( ":/shaders/fullscreen.vert.qsb" ) ) },
			{ QRhiShaderStage::Fragment, LoadShader( QStringLiteral( ":/shaders/material.frag.qsb" ) ) },
		} );
		m_pPipeline->setVertexInputLayout( {} );
		m_pPipeline->setSampleCount( pTarget->sampleCount() );
		m_pPipeline->setShaderResourceBindings( m_pBindings.get() );
		m_pPipeline->setRenderPassDescriptor( pTarget->renderPassDescriptor() );
		m_pPipeline->create();
	}

	pCommands->beginPass( pTarget, frame.m_Background, { 1.0f, 0 }, pUpdates );
	pCommands->setGraphicsPipeline( m_pPipeline.get() );
	pCommands->setViewport( { 0, 0, static_cast<float>( size.width() ), static_cast<float>( size.height() ) } );
	pCommands->setShaderResources( m_pBindings.get() );
	pCommands->draw( 3 );
	pCommands->endPass();
	Trim();
}

#endif
