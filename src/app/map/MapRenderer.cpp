//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapRenderer.h.
//
//===========================================================================//
#include "MapRenderer.h"

#ifdef VOMIT_TOOLS_GPU

#include <algorithm>
#include <iterator>
#include <utility>
#include <cstring>
#include <unordered_map>

#include <QMatrix4x4>
#include <rhi/qrhi.h>

#include "map/Lighting.h"
#include "map/MapConstants.h"
#include "texture/Vtf.h"

namespace
{

// The uniform blocks of map.vert, map.frag and mapline.vert, std140.
struct FrameUniforms_t
{
	float m_ViewProjection[ 16 ];
	float m_Eye[ 4 ];
	float m_Mode[ 4 ];
	float m_Selection[ 4 ];
	float m_SelectionEdge[ 4 ];
	float m_FaceShade[ 4 ];
	float m_State[ 4 ];
	float m_Instance[ 4 ];
	float m_Pass[ 4 ];
	float m_Invalid[ 4 ];
	float m_EyeLow[ 4 ];
	float m_Lighting[ 4 ];
	float m_FogColor[ 4 ];
	float m_FogColor2[ 4 ];
	float m_FogDirection[ 4 ];
	float m_FogRange[ 4 ];
	float m_ViewForward[ 4 ];
};

struct MaterialUniforms_t
{
	float m_Color[ 4 ];
	float m_Options[ 4 ];
	float m_Size[ 4 ];
	float m_Kind[ 4 ];
	float m_Extra[ 4 ];
	float m_SelfIllum[ 4 ];
	float m_UvTransform[ 4 ]; // a b c d
	float m_UvOffset[ 4 ];    // tx ty
	float m_Refract[ 4 ];     // 1: Refract or Water; 1: this is the reflection's pass; 1: reflects; 1: reflects the sky
	float m_RefractTint[ 4 ];
	float m_EnvTint[ 4 ];
	float m_Detail[ 4 ];          // $detailblendmode (-1: no detail), $detailblendfactor, $detailscale u v
	float m_DetailTransform[ 4 ]; // a b c d
	float m_DetailOffset[ 4 ];    // tx ty, and the detail texture's size
};

// map.frag's sky block, as sky.frag's.
struct SkyUniforms_t
{
	float m_FaceRight[ 6 ][ 4 ];
	float m_FaceUp[ 6 ][ 4 ];
	float m_Transform[ 6 ][ 4 ];
	float m_Translate[ 6 ][ 4 ];
};

// mapsky.frag's own block.
struct SkyViewUniforms_t
{
	float m_ClipToWorld[ 16 ];
	float m_Options[ 4 ]; // mat_monitorgamma / 2.2 (0: none), the alpha written
};

static_assert( sizeof( xv::map::MeshVertex_t ) == 48, "map.vert reads the vertex layout as it is" );
static_assert( sizeof( xv::map::LightVertex_t ) == 16, "map.vert reads the light as it is" );

//-----------------------------------------------------------------------------
// Purpose: A material's uniforms, as map.frag reads them (and mapline.vert, for the edges: only
//          the selection's, and whether they are a models' mesh's).
//-----------------------------------------------------------------------------
MaterialUniforms_t MaterialUniforms( const xv::map::RasterMaterial_t *pMaterial, bool bSelectionOnly, bool bModels = false, bool bReflection = false )
{
	MaterialUniforms_t u{};
	const xv::map::ShadeMaterial_t shade = pMaterial ? pMaterial->m_Shade : xv::map::ShadeMaterial_t{};
	u.m_Color[ 0 ] = shade.m_vecColor.m_flX;
	u.m_Color[ 1 ] = shade.m_vecColor.m_flY;
	u.m_Color[ 2 ] = shade.m_vecColor.m_flZ;
	u.m_Color[ 3 ] = shade.m_flAlpha;
	u.m_Options[ 0 ] = shade.m_bAlphaTest ? shade.m_flAlphaReference : -1.0f;
	u.m_Options[ 1 ] = shade.m_bAdditive ? 2.0f : shade.m_bTranslucent ? 1.0f
																	   : 0.0f;
	u.m_Options[ 2 ] = shade.m_bBlend ? 1.0f : 0.0f;
	u.m_Options[ 3 ] = shade.m_bMissing ? 1.0f : 0.0f;
	u.m_Size[ 0 ] = static_cast<float>( shade.m_pBase ? shade.m_pBase->m_nWidth : shade.m_nWidth );
	u.m_Size[ 1 ] = static_cast<float>( shade.m_pBase ? shade.m_pBase->m_nHeight : shade.m_nHeight );
	u.m_Size[ 2 ] = static_cast<float>( shade.m_pBase2 ? shade.m_pBase2->m_nWidth : 1 );
	u.m_Size[ 3 ] = static_cast<float>( shade.m_pBase2 ? shade.m_pBase2->m_nHeight : 1 );
	u.m_Kind[ 0 ] = pMaterial && pMaterial->m_bObjectColor ? 1.0f : 0.0f;
	u.m_Kind[ 1 ] = shade.m_pBase && shade.m_pBase->GetMipCount() > 0 ? 1.0f : 0.0f;
	u.m_Kind[ 2 ] = ( shade.m_pBase2 && shade.m_pBase2->GetMipCount() > 0 ) || bModels ? 1.0f : 0.0f;
	u.m_Kind[ 3 ] = bSelectionOnly ? 1.0f : 0.0f;
	u.m_Extra[ 0 ] = shade.m_bSky ? 1.0f : 0.0f;
	u.m_Extra[ 1 ] = shade.m_bTinted ? 1.0f : 0.0f;
	u.m_Extra[ 2 ] = shade.m_bUnlit ? 1.0f : 0.0f;
	u.m_Extra[ 3 ] = !shade.m_bVertexColor ? 0.0f : !shade.m_bPremultiplied ? 1.0f
		: shade.m_bAddOver                                                  ? 3.0f
																			: 2.0f;
	u.m_SelfIllum[ 0 ] = shade.m_vecSelfIllumTint.m_flX;
	u.m_SelfIllum[ 1 ] = shade.m_vecSelfIllumTint.m_flY;
	u.m_SelfIllum[ 2 ] = shade.m_vecSelfIllumTint.m_flZ;
	u.m_SelfIllum[ 3 ] = shade.m_bSelfIllum ? 1.0f : 0.0f;
	for ( std::size_t i = 0; i < 4; i++ )
		u.m_UvTransform[ i ] = shade.m_UvTransform[ i ];
	u.m_UvOffset[ 0 ] = shade.m_UvTransform[ 4 ];
	u.m_UvOffset[ 1 ] = shade.m_UvTransform[ 5 ];
	u.m_Refract[ 0 ] = shade.m_bRefract ? 1.0f : 0.0f;
	u.m_Refract[ 1 ] = bReflection ? 1.0f : 0.0f;
	u.m_Refract[ 2 ] = shade.m_bEnvMap ? 1.0f : 0.0f;
	u.m_Refract[ 3 ] = shade.m_bReflectsSky ? 1.0f : 0.0f;
	u.m_RefractTint[ 0 ] = shade.m_vecRefractTint.m_flX;
	u.m_RefractTint[ 1 ] = shade.m_vecRefractTint.m_flY;
	u.m_RefractTint[ 2 ] = shade.m_vecRefractTint.m_flZ;
	u.m_EnvTint[ 0 ] = shade.m_vecEnvTint.m_flX;
	u.m_EnvTint[ 1 ] = shade.m_vecEnvTint.m_flY;
	u.m_EnvTint[ 2 ] = shade.m_vecEnvTint.m_flZ;
	u.m_EnvTint[ 3 ] = shade.m_flAddSelf;
	const bool bDetail = shade.m_pDetail && shade.m_pDetail->GetMipCount() > 0 && !shade.m_bMissing;
	u.m_Detail[ 0 ] = bDetail ? static_cast<float>( shade.m_nDetailMode ) : -1.0f;
	u.m_Detail[ 1 ] = shade.m_flDetailBlend;
	u.m_Detail[ 2 ] = shade.m_DetailScale[ 0 ];
	u.m_Detail[ 3 ] = shade.m_DetailScale[ 1 ];
	for ( std::size_t i = 0; i < 4; i++ )
		u.m_DetailTransform[ i ] = shade.m_DetailTransform[ i ];
	u.m_DetailOffset[ 0 ] = shade.m_DetailTransform[ 4 ];
	u.m_DetailOffset[ 1 ] = shade.m_DetailTransform[ 5 ];
	u.m_DetailOffset[ 2 ] = static_cast<float>( bDetail ? shade.m_pDetail->m_nWidth : 1 );
	u.m_DetailOffset[ 3 ] = static_cast<float>( bDetail ? shade.m_pDetail->m_nHeight : 1 );
	return u;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsSeeThrough( const xv::map::RasterMaterial_t *pMaterial )
{
	return pMaterial && !pMaterial->m_bObjectColor && ( pMaterial->m_Shade.m_bTranslucent || pMaterial->m_Shade.m_bAdditive || pMaterial->m_Shade.m_flAlpha < 1.0f || pMaterial->m_Shade.m_bRefract );
}

//-----------------------------------------------------------------------------
// Purpose: map.frag's sky block for the sky uploaded: each face's layout and its transform, as
//          moved when the sky scrolls.
//-----------------------------------------------------------------------------
SkyUniforms_t MakeSkyUniforms( const xv::sky::CCube *pSky, const xv::sky::SkyTransforms_t *pTransforms, const std::array<std::unique_ptr<QRhiTexture>, 6> &faces, bool bEnvAtlas )
{
	SkyUniforms_t uniforms{};
	for ( const xv::sky::Face_t nFace : xv::sky::FACES )
	{
		const std::size_t i = xv::sky::IndexOf( nFace );
		const xv::sky::FaceBasis_t &basis = xv::sky::Basis( nFace );
		uniforms.m_FaceRight[ i ][ 0 ] = basis.m_vecRight.m_flX;
		uniforms.m_FaceRight[ i ][ 1 ] = basis.m_vecRight.m_flY;
		uniforms.m_FaceRight[ i ][ 2 ] = basis.m_vecRight.m_flZ;
		uniforms.m_FaceUp[ i ][ 0 ] = basis.m_vecUp.m_flX;
		uniforms.m_FaceUp[ i ][ 1 ] = basis.m_vecUp.m_flY;
		uniforms.m_FaceUp[ i ][ 2 ] = basis.m_vecUp.m_flZ;
		xv::sky::CUvTransform t;
		if ( pSky )
		{
			t = pTransforms ? ( *pTransforms )[ i ] : pSky->m_Transforms[ i ];
		}
		uniforms.m_Transform[ i ][ 0 ] = t.m_flA;
		uniforms.m_Transform[ i ][ 1 ] = t.m_flB;
		uniforms.m_Transform[ i ][ 2 ] = t.m_flC;
		uniforms.m_Transform[ i ][ 3 ] = t.m_flD;
		uniforms.m_Translate[ i ][ 0 ] = t.m_flTx;
		uniforms.m_Translate[ i ][ 1 ] = t.m_flTy;
		uniforms.m_Translate[ i ][ 2 ] = faces[ i ] ? 1.0f : 0.0f;
	}
	uniforms.m_Translate[ 0 ][ 3 ] = pSky ? 1.0f : 0.0f;
	uniforms.m_Translate[ 1 ][ 3 ] = bEnvAtlas ? 1.0f : 0.0f;
	return uniforms;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapRenderer::CMapRenderer( Source_t pfnSource )
	: m_pfnSource( std::move( pfnSource ) )
{
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapRenderer::~CMapRenderer() = default;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapRenderer::Release()
{
	// Its QRhi goes, and with it any reading still on its way.
	m_pReadback.reset();
	m_pReadbackDone.reset();
	m_pEffectPass.reset();
	m_pPipelinePass = nullptr;
	m_pOpaque.reset();
	m_pBlended.reset();
	m_pAdditive.reset();
	m_pMultiply.reset();
	m_pPremultiplied.reset();
	m_pPremultipliedAdditive.reset();
	m_pLines.reset();
	m_Materials.clear();
	m_LinesAll = {};
	m_LinesSelected = {};
	m_ModelLinesAll = {};
	m_ModelLinesSelected = {};
	m_Meshes.clear();
	m_pFrame.reset();
	m_pState.reset();
	m_pLineColors.reset();
	m_pWhite.reset();
	m_pSkyUniforms.reset();
	for ( auto &pFace : m_SkyFaces )
	{
		pFace.reset();
	}
	m_pSkyFor = nullptr;
	m_pSkyTransformsFor.reset();
	m_pEnvAtlas.reset();
	m_pEnvFor.reset();
	m_pLightmap.reset();
	m_pLightmapFor.reset();
	m_pLightSampler.reset();
	for ( auto &pSampler : m_TopSamplers )
	{
		pSampler.reset();
	}
	for ( auto &pSampler : m_Samplers )
	{
		pSampler.reset();
	}
	for ( auto &pSampler : m_NearestMipSamplers )
	{
		pSampler.reset();
	}
	m_pNearest.reset();
	m_pSkyFaces.reset();
	m_pAlpha.reset();
	m_pAlphaSky.reset();
	m_pSkyBehind.reset();
	m_pSkyBehindBindings.reset();
	m_pSkyView.reset();
	m_pNoBindings.reset();
	m_pMaterialsFor = nullptr;
	m_bRemakeMaterials = true;
	m_pStateFlags = nullptr;
	m_pStateColors = nullptr;
	m_pStateLines = nullptr;
	m_Held = {};
	m_pRhi = nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapRenderer::Initialize( QRhi *pRhi, QRhiRenderTarget * )
{
	if ( m_pRhi != pRhi )
	{
		Release();
		m_pRhi = pRhi;
	}
	if ( !m_pFrame )
	{
		// Four frames' uniforms: the world's and the 3D skybox's camera's, each for meshes as read
		// (which leave out edited objects' old shapes) and the others.
		m_nFrameStride = m_pRhi->ubufAligned( sizeof( FrameUniforms_t ) );
		m_pFrame.reset( m_pRhi->newBuffer( QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, m_nFrameStride * 6 ) );
		m_pFrame->create();
		for ( std::size_t i = 0; i < m_Samplers.size(); i++ )
		{
			const auto s = ( i & 1 ) != 0 ? QRhiSampler::ClampToEdge : QRhiSampler::Repeat;
			const auto t = ( i & 2 ) != 0 ? QRhiSampler::ClampToEdge : QRhiSampler::Repeat;
			m_Samplers[ i ].reset( m_pRhi->newSampler( QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear, s, t ) );
			m_Samplers[ i ]->create();
			m_TopSamplers[ i ].reset( m_pRhi->newSampler( QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, s, t ) );
			m_TopSamplers[ i ]->create();
			m_NearestMipSamplers[ i ].reset( m_pRhi->newSampler( QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Nearest, s, t ) );
			m_NearestMipSamplers[ i ]->create();
		}
		m_pNearest.reset( m_pRhi->newSampler( QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge ) );
		m_pNearest->create();
		// The lighting atlas, as CLighting::Sample reads it: linear, clamped, no mips.
		m_pLightSampler.reset( m_pRhi->newSampler( QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge ) );
		m_pLightSampler->create();
	}
	ResetPipelines();
}

//-----------------------------------------------------------------------------
// Purpose: Pipelines follow the target's pass and sample count: made again for another.
//-----------------------------------------------------------------------------
void CMapRenderer::ResetPipelines()
{
	m_pOpaque.reset();
	m_pBlended.reset();
	m_pAdditive.reset();
	m_pMultiply.reset();
	m_pPremultiplied.reset();
	m_pPremultipliedAdditive.reset();
	m_pLines.reset();
	m_pPipelinePass = nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: The map's mesh and the entity boxes, uploaded once each.
//-----------------------------------------------------------------------------
void CMapRenderer::UploadMeshes( const MapFrame_t &frame, QRhiResourceUpdateBatch *pUpdates )
{
	const std::vector<const xv::map::CMapMesh *> meshes = frame.MakeScene().m_Meshes;
	std::vector<Mesh_t> retained = std::move( m_Meshes );
	m_Meshes.resize( meshes.size() );
	std::unordered_map<const xv::map::CMapMesh *, std::size_t> retainedIndex;
	retainedIndex.reserve( retained.size() );
	for ( std::size_t i = 0; i < retained.size(); ++i )
		retainedIndex.emplace( retained[ i ].m_pFor, i );
	for ( std::size_t i = 0; i < meshes.size(); i++ )
	{
		Mesh_t &mesh = m_Meshes[ i ];
		const xv::map::CMapMesh *pMesh = meshes[ i ];
		const auto it = retainedIndex.find( pMesh );
		if ( it != retainedIndex.end() )
			mesh = std::move( retained[ it->second ] );
		if ( mesh.m_pFor == pMesh )
		{
			continue;
		}
		mesh = {};
		mesh.m_pFor = pMesh;
		m_Stats.m_nUploads++;
		auto upload = [ & ]( std::unique_ptr<QRhiBuffer> &pBuffer, QRhiBuffer::UsageFlags usage, const void *pData, std::size_t nBytes ) {
			if ( nBytes == 0 )
			{
				return;
			}
			pBuffer.reset( m_pRhi->newBuffer( QRhiBuffer::Immutable, usage, static_cast<quint32>( nBytes ) ) );
			pBuffer->create();
			pUpdates->uploadStaticBuffer( pBuffer.get(), pData );
		};
		upload( mesh.m_pVertices, QRhiBuffer::VertexBuffer, pMesh->m_Vertices.data(), pMesh->m_Vertices.size() * sizeof( xv::map::MeshVertex_t ) );
		upload( mesh.m_pIndices, QRhiBuffer::IndexBuffer, pMesh->m_Indices.data(), pMesh->m_Indices.size() * sizeof( std::uint32_t ) );
		upload( mesh.m_pLines, QRhiBuffer::IndexBuffer, pMesh->m_Lines.data(), pMesh->m_Lines.size() * sizeof( std::uint32_t ) );
	}
	// Where each vertex's light is: the frame's, or none (kept from before when the frame has
	// no lighting, so a view switching modes doesn't upload them again).
	for ( std::size_t i = 0; i < meshes.size(); i++ )
	{
		Mesh_t &mesh = m_Meshes[ i ];
		const xv::map::CMapMesh *pMesh = meshes[ i ];
		std::shared_ptr<const std::vector<xv::map::LightVertex_t>> pLights;
		if ( frame.m_pMeshLights )
		{
			if ( const auto it = frame.m_pMeshLights->find( pMesh ); it != frame.m_pMeshLights->end() && it->second && it->second->size() == pMesh->m_Vertices.size() )
			{
				pLights = it->second;
			}
		}
		if ( pMesh->m_Vertices.empty() || ( mesh.m_bLightUp && ( pLights == mesh.m_pLightFor || ( !pLights && !frame.m_pMeshLights ) ) ) )
		{
			continue;
		}
		const std::size_t nBytes = pMesh->m_Vertices.size() * sizeof( xv::map::LightVertex_t );
		mesh.m_pLight.reset( m_pRhi->newBuffer( QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, static_cast<quint32>( nBytes ) ) );
		mesh.m_pLight->create();
		if ( pLights )
		{
			pUpdates->uploadStaticBuffer( mesh.m_pLight.get(), pLights->data() );
		}
		else
		{
			const std::vector<xv::map::LightVertex_t> none( pMesh->m_Vertices.size(), xv::map::LIGHT_VERTEX_NONE );
			pUpdates->uploadStaticBuffer( mesh.m_pLight.get(), none.data() );
		}
		mesh.m_pLightFor = std::move( pLights );
		mesh.m_bLightUp = true;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Half floats as the bake stores them; a card without them, or too small for the
//          atlas, draws the shaded look instead.
//-----------------------------------------------------------------------------
bool CMapRenderer::UploadLighting( const MapFrame_t &frame, QRhiResourceUpdateBatch *pUpdates )
{
	if ( !frame.m_pLighting || frame.m_pLighting == m_pLightmapFor )
	{
		return false;
	}
	m_pLightmapFor = frame.m_pLighting;
	m_pLightmap.reset();
	const xv::map::CLighting &lighting = *m_pLightmapFor;
	const int nMax = m_pRhi->resourceLimit( QRhi::TextureSizeMax );
	if ( lighting.m_nWidth <= 0 || lighting.m_nHeight <= 0 || lighting.m_nWidth > nMax || lighting.m_nHeight > nMax || !m_pRhi->isTextureFormatSupported( QRhiTexture::RGBA16F ) )
	{
		return true;
	}
	m_pLightmap.reset( m_pRhi->newTexture( QRhiTexture::RGBA16F, { lighting.m_nWidth, lighting.m_nHeight } ) );
	m_pLightmap->create();
	QRhiTextureSubresourceUploadDescription description( lighting.m_Texels.data(), static_cast<quint32>( lighting.m_Texels.size() * sizeof( std::uint16_t ) ) );
	description.setSourceSize( { lighting.m_nWidth, lighting.m_nHeight } );
	pUpdates->uploadTexture( m_pLightmap.get(), QRhiTextureUploadDescription( QRhiTextureUploadEntry( 0, 0, description ) ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Each object's flat colour and flags, and its line colour: a texel each.
//-----------------------------------------------------------------------------
void CMapRenderer::UploadState( const MapFrame_t &frame, QRhiResourceUpdateBatch *pUpdates )
{
	const std::size_t nObjects = frame.m_pScene ? frame.m_pScene->GetObjectCount() : 0;
	const int nHeight = std::max( 1, static_cast<int>( ( nObjects + static_cast<std::size_t>( m_nStateWidth ) - 1 ) / static_cast<std::size_t>( m_nStateWidth ) ) );
	if ( !m_pState || m_pState->pixelSize().height() != nHeight )
	{
		m_pState.reset( m_pRhi->newTexture( QRhiTexture::RGBA8, { m_nStateWidth, nHeight } ) );
		m_pState->create();
		m_pLineColors.reset( m_pRhi->newTexture( QRhiTexture::RGBA8, { m_nStateWidth, nHeight } ) );
		m_pLineColors->create();
		m_pStateFlags = nullptr;
		m_pStateLines = nullptr;
		m_Materials.clear(); // bound to the old textures
		m_pMaterialsFor = nullptr;
		m_bRemakeMaterials = true;
		m_LinesAll = {};
		m_LinesSelected = {};
		m_ModelLinesAll = {};
		m_ModelLinesSelected = {};
	}
	if ( frame.m_pFlags.get() != m_pStateFlags || frame.m_pFlatColors.get() != m_pStateColors )
	{
		m_pStateFlags = frame.m_pFlags.get();
		m_pStateColors = frame.m_pFlatColors.get();
		QImage image( m_nStateWidth, nHeight, QImage::Format_RGBA8888 );
		image.fill( Qt::transparent );
		for ( std::size_t n = 0; n < nObjects; n++ )
		{
			std::uint8_t *p = image.bits() + n * 4;
			const auto color = frame.m_pFlatColors && n < frame.m_pFlatColors->size() ? ( *frame.m_pFlatColors )[ n ] : std::array<std::uint8_t, 3>{ 255, 255, 255 };
			p[ 0 ] = color[ 0 ];
			p[ 1 ] = color[ 1 ];
			p[ 2 ] = color[ 2 ];
			p[ 3 ] = frame.m_pFlags && n < frame.m_pFlags->size() ? ( *frame.m_pFlags )[ n ] : static_cast<std::uint8_t>( xv::map::RASTER_OBJECT_SHOWN );
		}
		pUpdates->uploadTexture( m_pState.get(), image );
	}
	if ( frame.m_pLineColors.get() != m_pStateLines )
	{
		m_pStateLines = frame.m_pLineColors.get();
		QImage image( m_nStateWidth, nHeight, QImage::Format_RGBA8888 );
		image.fill( Qt::white );
		for ( std::size_t n = 0; frame.m_pLineColors && n < std::min( nObjects, frame.m_pLineColors->size() ); n++ )
		{
			std::uint8_t *p = image.bits() + n * 4;
			p[ 0 ] = ( *frame.m_pLineColors )[ n ][ 0 ];
			p[ 1 ] = ( *frame.m_pLineColors )[ n ][ 1 ];
			p[ 2 ] = ( *frame.m_pLineColors )[ n ][ 2 ];
			p[ 3 ] = 255;
		}
		pUpdates->uploadTexture( m_pLineColors.get(), image );
	}
}

//-----------------------------------------------------------------------------
// Purpose: A texture with the mips it came with (the VTF's own), uploaded once.
//-----------------------------------------------------------------------------
QRhiTexture *CMapRenderer::Upload( const xv::map::RasterTexture_t &texture, std::unique_ptr<QRhiTexture> &pTexture, QRhiResourceUpdateBatch *pUpdates )
{
	if ( texture.GetMipCount() == 0 || texture.GetTopWidth() <= 0 )
	{
		return m_pWhite.get();
	}
	const QSize top{ texture.GetTopWidth(), texture.GetTopHeight() };
	const bool bMips = xv::map::HasMipChain( texture );
	const int nLevels = bMips ? m_pRhi->mipLevelsForSize( top ) : 1;
	// The VTF's own DXT mips when the card takes them, else the decoded ones (decoded here when
	// nothing else did: a card that can't take DXT is rare).
	QRhiTexture::Format nFormat = QRhiTexture::RGBA8;
	switch ( texture.m_nCompressedFormat )
	{
		case xv::texture::VtfFormat_t::DXT1:
		case xv::texture::VtfFormat_t::DXT1_ONEBITALPHA: nFormat = QRhiTexture::BC1; break;
		case xv::texture::VtfFormat_t::DXT3: nFormat = QRhiTexture::BC2; break;
		case xv::texture::VtfFormat_t::DXT5: nFormat = QRhiTexture::BC3; break;
		default: break;
	}
	const bool bCompressed = !texture.m_Compressed.empty() && nFormat != QRhiTexture::RGBA8 && m_pRhi->isTextureFormatSupported( nFormat, bMips ? QRhiTexture::MipMapped : QRhiTexture::Flags{} );
	std::vector<xv::texture::CImage> decoded;
	if ( !bCompressed && texture.m_Mips.empty() )
	{
		for ( const xv::map::CompressedMip_t &mip : texture.m_Compressed )
		{
			std::optional<xv::texture::CImage> image = xv::texture::DecodeImage( texture.m_nCompressedFormat, mip.m_nWidth, mip.m_nHeight, mip.m_Data );
			if ( !image )
			{
				return m_pWhite.get();
			}
			decoded.push_back( std::move( *image ) );
		}
	}
	pTexture.reset( m_pRhi->newTexture( bCompressed ? nFormat : QRhiTexture::RGBA8, top, 1, bMips ? QRhiTexture::MipMapped : QRhiTexture::Flags{} ) );
	pTexture->create();
	QList<QRhiTextureUploadEntry> entries;
	for ( int nLevel = 0; nLevel < nLevels; nLevel++ )
	{
		const auto i = static_cast<std::size_t>( nLevel );
		if ( bCompressed )
		{
			const xv::map::CompressedMip_t &mip = texture.m_Compressed[ i ];
			QRhiTextureSubresourceUploadDescription description( mip.m_Data.data(), static_cast<quint32>( mip.m_Data.size() ) );
			description.setSourceSize( { mip.m_nWidth, mip.m_nHeight } );
			entries.append( QRhiTextureUploadEntry( 0, nLevel, description ) );
			continue;
		}
		const xv::texture::CImage &image = decoded.empty() ? texture.m_Mips[ i ] : decoded[ i ];
		QRhiTextureSubresourceUploadDescription description( image.m_Pixels.data(), static_cast<quint32>( image.m_Pixels.size() ) );
		description.setSourceSize( { image.m_nWidth, image.m_nHeight } );
		entries.append( QRhiTextureUploadEntry( 0, nLevel, description ) );
	}
	QRhiTextureUploadDescription upload;
	upload.setEntries( entries.begin(), entries.end() );
	pUpdates->uploadTexture( pTexture.get(), upload );
	return pTexture.get();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QRhiSampler *CMapRenderer::SamplerFor( const xv::map::RasterTexture_t *pTexture ) const
{
	const std::size_t nIndex = pTexture ? ( pTexture->m_bClampS ? 1u : 0u ) + ( pTexture->m_bClampT ? 2u : 0u ) : 0u;
	if ( pTexture && !xv::map::HasMipChain( *pTexture ) )
	{
		return m_TopSamplers[ nIndex ].get();
	}
	return m_bMipBlend ? m_Samplers[ nIndex ].get() : m_NearestMipSamplers[ nIndex ].get();
}

//-----------------------------------------------------------------------------
// Purpose: Every binding every pipeline declares, so one pipeline draws with any material's.
//-----------------------------------------------------------------------------
void CMapRenderer::MakeBindings( Material_t &material, QRhiTexture *pBase, QRhiTexture *pBase2, const xv::map::RasterTexture_t *pBaseTexture, const xv::map::RasterTexture_t *pBase2Texture, QRhiTexture *pDetail, const xv::map::RasterTexture_t *pDetailTexture )
{
	if ( !pDetail )
		pDetail = m_pWhite.get();
	const auto both = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
	auto make = [ & ]( QRhiBuffer *pUniforms ) {
		std::unique_ptr<QRhiShaderResourceBindings> pBindings( m_pRhi->newShaderResourceBindings() );
		pBindings->setBindings( {
			QRhiShaderResourceBinding::uniformBufferWithDynamicOffset( 0, both, m_pFrame.get(), sizeof( FrameUniforms_t ) ),
			QRhiShaderResourceBinding::uniformBuffer( 1, both, pUniforms ),
			QRhiShaderResourceBinding::sampledTexture( 2, QRhiShaderResourceBinding::FragmentStage, pBase, SamplerFor( pBaseTexture ) ),
			QRhiShaderResourceBinding::sampledTexture( 3, QRhiShaderResourceBinding::FragmentStage, pBase2, SamplerFor( pBase2Texture ) ),
			QRhiShaderResourceBinding::sampledTexture( 4, both, m_pState.get(), m_pNearest.get() ),
			QRhiShaderResourceBinding::sampledTexture( 5, QRhiShaderResourceBinding::VertexStage, m_pLineColors.get(), m_pNearest.get() ),
			QRhiShaderResourceBinding::uniformBuffer( 6, QRhiShaderResourceBinding::FragmentStage, m_pSkyUniforms.get() ),
			QRhiShaderResourceBinding::sampledTexture( 7, QRhiShaderResourceBinding::FragmentStage, SkyFace( 0 ), SkySampler( 0 ) ),
			QRhiShaderResourceBinding::sampledTexture( 8, QRhiShaderResourceBinding::FragmentStage, SkyFace( 1 ), SkySampler( 1 ) ),
			QRhiShaderResourceBinding::sampledTexture( 9, QRhiShaderResourceBinding::FragmentStage, SkyFace( 2 ), SkySampler( 2 ) ),
			QRhiShaderResourceBinding::sampledTexture( 10, QRhiShaderResourceBinding::FragmentStage, SkyFace( 3 ), SkySampler( 3 ) ),
			QRhiShaderResourceBinding::sampledTexture( 11, QRhiShaderResourceBinding::FragmentStage, SkyFace( 4 ), SkySampler( 4 ) ),
			QRhiShaderResourceBinding::sampledTexture( 12, QRhiShaderResourceBinding::FragmentStage, SkyFace( 5 ), SkySampler( 5 ) ),
			QRhiShaderResourceBinding::sampledTexture( 13, QRhiShaderResourceBinding::FragmentStage, m_pLightmap ? m_pLightmap.get() : m_pWhite.get(), m_pLightSampler.get() ),
			QRhiShaderResourceBinding::sampledTexture( 14, QRhiShaderResourceBinding::FragmentStage, m_pEnvAtlas ? m_pEnvAtlas.get() : m_pWhite.get(), m_TopSamplers[ 3 ].get() ),
			QRhiShaderResourceBinding::sampledTexture( 15, QRhiShaderResourceBinding::FragmentStage, pDetail, SamplerFor( pDetailTexture ) ),
		} );
		pBindings->create();
		return pBindings;
	};
	material.m_pBindings = make( material.m_pUniforms.get() );
	// A refracting material's reflection pass binds the same, with its own uniforms.
	material.m_pReflectBindings = material.m_pReflectUniforms ? make( material.m_pReflectUniforms.get() ) : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: A sky face's texture, or white where there's none.
//-----------------------------------------------------------------------------
QRhiTexture *CMapRenderer::SkyFace( std::size_t nFace ) const
{
	return m_SkyFaces[ nFace ] ? m_SkyFaces[ nFace ].get() : m_pWhite.get();
}

//-----------------------------------------------------------------------------
// Purpose: Repeating or clamped as the face's transform says, like the sky editor's view.
//-----------------------------------------------------------------------------
QRhiSampler *CMapRenderer::SkySampler( std::size_t nFace ) const
{
	const xv::sky::CUvTransform transform = m_pSkyFor ? m_pSkyFor->m_Transforms[ nFace ] : xv::sky::CUvTransform{};
	return m_TopSamplers[ ( transform.m_bRepeatU ? 0u : 1u ) + ( transform.m_bRepeatV ? 0u : 2u ) ].get();
}

//-----------------------------------------------------------------------------
// Purpose: The map's sky faces and their layout, when the sky changes. Returns whether it did
//          (the materials' bindings then need making again).
//-----------------------------------------------------------------------------
bool CMapRenderer::UploadSky( const MapFrame_t &frame, QRhiResourceUpdateBatch *pUpdates )
{
	const bool bMoved = frame.m_pSkyTransforms != m_pSkyTransformsFor;
	m_pSkyTransformsFor = frame.m_pSkyTransforms;
	if ( m_pSkyUniforms && frame.m_pSky.get() == m_pSkyFor && frame.m_pEnvMap == m_pEnvFor )
	{
		if ( bMoved )
		{
			// A scrolling sky: only its faces' transforms change.
			SkyUniforms_t uniforms = MakeSkyUniforms( m_pSkyFor, m_pSkyTransformsFor.get(), m_SkyFaces, m_pEnvAtlas != nullptr );
			pUpdates->updateDynamicBuffer( m_pSkyUniforms.get(), 0, sizeof uniforms, &uniforms );
		}
		return false;
	}
	m_pSkyFor = frame.m_pSky.get();
	if ( frame.m_pEnvMap != m_pEnvFor )
	{
		m_pEnvFor = frame.m_pEnvMap;
		m_pEnvAtlas.reset();
		// Faces of another size than the first are left black, as a missing face is.
		const int nSize = m_pEnvFor ? m_pEnvFor->m_Faces[ 0 ].m_nWidth : 0;
		if ( nSize > 0 )
		{
			QImage atlas( nSize * 3, nSize * 2, QImage::Format_RGBA8888 );
			atlas.fill( Qt::black );
			for ( std::size_t i = 0; i < 6; i++ )
			{
				const xv::texture::CImage &face = m_pEnvFor->m_Faces[ i ];
				if ( face.m_nWidth != nSize || face.m_nHeight != nSize )
					continue;
				const int nX = static_cast<int>( i % 3 ) * nSize;
				const int nY = static_cast<int>( i / 3 ) * nSize;
				for ( int y = 0; y < nSize; y++ )
					std::memcpy( atlas.scanLine( nY + y ) + nX * 4, face.m_Pixels.data() + static_cast<std::size_t>( y ) * nSize * 4, static_cast<std::size_t>( nSize ) * 4 );
			}
			m_pEnvAtlas.reset( m_pRhi->newTexture( QRhiTexture::RGBA8, atlas.size() ) );
			m_pEnvAtlas->create();
			pUpdates->uploadTexture( m_pEnvAtlas.get(), atlas );
		}
	}
	if ( !m_pSkyUniforms )
	{
		m_pSkyUniforms.reset( m_pRhi->newBuffer( QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof( SkyUniforms_t ) ) );
		m_pSkyUniforms->create();
	}
	for ( std::size_t i = 0; i < m_SkyFaces.size(); i++ )
	{
		m_SkyFaces[ i ].reset();
		const xv::texture::CImage *pImage = m_pSkyFor ? &m_pSkyFor->m_Faces[ i ] : nullptr;
		if ( pImage && !pImage->Empty() )
		{
			m_SkyFaces[ i ].reset( m_pRhi->newTexture( QRhiTexture::RGBA8, { pImage->m_nWidth, pImage->m_nHeight } ) );
			m_SkyFaces[ i ]->create();
			pUpdates->uploadTexture( m_SkyFaces[ i ].get(), QImage( pImage->m_Pixels.data(), pImage->m_nWidth, pImage->m_nHeight, pImage->m_nWidth * 4, QImage::Format_RGBA8888 ) );
		}
	}
	SkyUniforms_t uniforms = MakeSkyUniforms( m_pSkyFor, m_pSkyTransformsFor.get(), m_SkyFaces, m_pEnvAtlas != nullptr );
	pUpdates->updateDynamicBuffer( m_pSkyUniforms.get(), 0, sizeof uniforms, &uniforms );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: The materials' textures and uniforms, made again when the materials change.
//-----------------------------------------------------------------------------
void CMapRenderer::UploadMaterials( const MapFrame_t &frame, QRhiResourceUpdateBatch *pUpdates )
{
	if ( !m_pWhite )
	{
		m_pWhite.reset( m_pRhi->newTexture( QRhiTexture::RGBA8, { 1, 1 } ) );
		m_pWhite->create();
		QImage white( 1, 1, QImage::Format_RGBA8888 );
		white.fill( Qt::white );
		pUpdates->uploadTexture( m_pWhite.get(), white );
	}
	auto uniforms = [ & ]( Material_t &material, const MaterialUniforms_t &values ) {
		if ( !material.m_pUniforms )
		{
			material.m_pUniforms.reset( m_pRhi->newBuffer( QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof( MaterialUniforms_t ) ) );
			material.m_pUniforms->create();
		}
		pUpdates->updateDynamicBuffer( material.m_pUniforms.get(), 0, sizeof values, &values );
	};
	const bool bSkyChanged = UploadSky( frame, pUpdates );
	const bool bLightingChanged = UploadLighting( frame, pUpdates );
	if ( bSkyChanged || bLightingChanged )
	{
		// Everything binds the sky's faces and the lighting atlas.
		m_LinesAll.m_pBindings.reset();
		m_LinesSelected.m_pBindings.reset();
		m_ModelLinesAll.m_pBindings.reset();
		m_ModelLinesSelected.m_pBindings.reset();
		m_pMaterialsFor = nullptr;
		m_bRemakeMaterials = true;
		if ( m_pSkyBehindBindings )
		{
			MakeSkyBehindBindings();
		}
	}
	if ( !m_LinesAll.m_pBindings )
	{
		uniforms( m_LinesAll, MaterialUniforms( nullptr, false ) );
		MakeBindings( m_LinesAll, m_pWhite.get(), m_pWhite.get(), nullptr, nullptr );
		uniforms( m_LinesSelected, MaterialUniforms( nullptr, true ) );
		MakeBindings( m_LinesSelected, m_pWhite.get(), m_pWhite.get(), nullptr, nullptr );
		uniforms( m_ModelLinesAll, MaterialUniforms( nullptr, false, true ) );
		MakeBindings( m_ModelLinesAll, m_pWhite.get(), m_pWhite.get(), nullptr, nullptr );
		uniforms( m_ModelLinesSelected, MaterialUniforms( nullptr, true, true ) );
		MakeBindings( m_ModelLinesSelected, m_pWhite.get(), m_pWhite.get(), nullptr, nullptr );
	}
	if ( frame.m_Options.m_Shade.m_bMipBlend != m_bMipBlend )
	{
		// The materials bind their samplers.
		m_bMipBlend = frame.m_Options.m_Shade.m_bMipBlend;
		m_pMaterialsFor = nullptr;
		m_bRemakeMaterials = true;
	}
	if ( frame.m_pMaterials.get() == m_pMaterialsFor )
	{
		return;
	}
	m_pMaterialsFor = frame.m_pMaterials.get();
	const std::size_t nCount = frame.m_pMaterials ? frame.m_pMaterials->size() : 0;
	m_Materials.resize( nCount );
	// Only the materials that changed are made again (animated ones change every few frames).
	const bool bRemake = std::exchange( m_bRemakeMaterials, false );
	for ( std::size_t i = 0; i < nCount; i++ )
	{
		const xv::map::RasterMaterial_t &source = ( *frame.m_pMaterials )[ i ];
		Material_t &material = m_Materials[ i ];
		if ( !bRemake && material.m_bMade && material.m_pBindings && material.m_Source == source )
		{
			continue;
		}
		uniforms( material, MaterialUniforms( &source, false ) );
		if ( source.m_Shade.m_bRefract )
		{
			if ( !material.m_pReflectUniforms )
			{
				material.m_pReflectUniforms.reset( m_pRhi->newBuffer( QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof( MaterialUniforms_t ) ) );
				material.m_pReflectUniforms->create();
			}
			const MaterialUniforms_t reflection = MaterialUniforms( &source, false, false, true );
			pUpdates->updateDynamicBuffer( material.m_pReflectUniforms.get(), 0, sizeof reflection, &reflection );
		}
		else
		{
			material.m_pReflectUniforms.reset();
		}
		// Textures that are the same as last time stay; new ones go up.
		QRhiTexture *pBase = m_pWhite.get();
		QRhiTexture *pBase2 = m_pWhite.get();
		QRhiTexture *pDetail = m_pWhite.get();
		// An animated material's frames (base and detail alike) are kept on the card by texture.
		auto frame = [ & ]( const xv::map::RasterTexture_t *pTexture ) {
			auto found = std::ranges::find( material.m_Frames, pTexture, &std::pair<const xv::map::RasterTexture_t *, std::unique_ptr<QRhiTexture>>::first );
			if ( found == material.m_Frames.end() )
			{
				material.m_Frames.emplace_back( pTexture, nullptr );
				return Upload( *pTexture, material.m_Frames.back().second, pUpdates );
			}
			return found->second ? found->second.get() : m_pWhite.get();
		};
		if ( source.m_bAnimated )
		{
			// Frames of another reading (the textures read again) are let go.
			if ( material.m_pFramesFor != source.m_pFrames )
			{
				material.m_Frames.clear();
				material.m_pFramesFor = source.m_pFrames;
			}
		}
		else
		{
			material.m_Frames.clear();
		}
		if ( source.m_Shade.m_pBase && source.m_bAnimated )
		{
			pBase = frame( source.m_Shade.m_pBase );
		}
		else if ( source.m_Shade.m_pBase )
		{
			pBase = material.m_pBaseFor == source.m_Shade.m_pBase && material.m_pBase ? material.m_pBase.get() : Upload( *source.m_Shade.m_pBase, material.m_pBase, pUpdates );
		}
		if ( source.m_Shade.m_pDetail && source.m_bAnimated )
		{
			pDetail = frame( source.m_Shade.m_pDetail );
		}
		else if ( source.m_Shade.m_pDetail )
		{
			pDetail = material.m_pDetailFor == source.m_Shade.m_pDetail && material.m_pDetail ? material.m_pDetail.get() : Upload( *source.m_Shade.m_pDetail, material.m_pDetail, pUpdates );
		}
		if ( source.m_Shade.m_pBase2 )
		{
			pBase2 = material.m_pBase2For == source.m_Shade.m_pBase2 && material.m_pBase2 ? material.m_pBase2.get() : Upload( *source.m_Shade.m_pBase2, material.m_pBase2, pUpdates );
		}
		if ( !source.m_bAnimated )
		{
			material.m_pBaseFor = source.m_Shade.m_pBase;
		}
		material.m_pBase2For = source.m_Shade.m_pBase2;
		if ( !source.m_bAnimated )
		{
			material.m_pDetailFor = source.m_Shade.m_pDetail;
		}
		MakeBindings( material, pBase, pBase2, source.m_Shade.m_pBase, source.m_Shade.m_pBase2, pDetail, source.m_Shade.m_pDetail );
		material.m_Source = source;
		material.m_bMade = true;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Opaque, see-through, added and line pipelines, all with the same bindings.
//-----------------------------------------------------------------------------
void CMapRenderer::MakePipelines( QRhiRenderTarget *pTarget )
{
	const std::initializer_list<QRhiVertexInputAttribute> attributes{
		QRhiVertexInputAttribute( 0, 0, QRhiVertexInputAttribute::Float3, 0 ),
		QRhiVertexInputAttribute( 0, 1, QRhiVertexInputAttribute::Float3, 12 ),
		QRhiVertexInputAttribute( 0, 2, QRhiVertexInputAttribute::Float2, 24 ),
		QRhiVertexInputAttribute( 0, 3, QRhiVertexInputAttribute::Float2, 32 ),
		QRhiVertexInputAttribute( 0, 4, QRhiVertexInputAttribute::Float, 40 ),
		QRhiVertexInputAttribute( 0, 5, QRhiVertexInputAttribute::UInt, 44 ),
	};
	// Edges read the mesh's vertices alone; faces also where their light is (a buffer of its own).
	QRhiVertexInputLayout lineLayout;
	lineLayout.setBindings( { QRhiVertexInputBinding( sizeof( xv::map::MeshVertex_t ) ) } );
	lineLayout.setAttributes( attributes );
	QRhiVertexInputLayout layout;
	layout.setBindings( { QRhiVertexInputBinding( sizeof( xv::map::MeshVertex_t ) ), QRhiVertexInputBinding( sizeof( xv::map::LightVertex_t ) ) } );
	std::vector<QRhiVertexInputAttribute> faceAttributes( attributes );
	faceAttributes.emplace_back( 1, 6, QRhiVertexInputAttribute::Float4, 0 );
	layout.setAttributes( faceAttributes.begin(), faceAttributes.end() );
	const QShader faceVertex = LoadShader( QStringLiteral( ":/shaders/map.vert.qsb" ) );
	const QShader faceFragment = LoadShader( QStringLiteral( ":/shaders/map.frag.qsb" ) );
	auto make = [ & ]( int nBlend, bool bLines ) {
		std::unique_ptr<QRhiGraphicsPipeline> pPipeline( m_pRhi->newGraphicsPipeline() );
		if ( bLines )
		{
			pPipeline->setShaderStages( { { QRhiShaderStage::Vertex, LoadShader( QStringLiteral( ":/shaders/mapline.vert.qsb" ) ) }, { QRhiShaderStage::Fragment, LoadShader( QStringLiteral( ":/shaders/mapline.frag.qsb" ) ) } } );
			pPipeline->setTopology( QRhiGraphicsPipeline::Lines );
		}
		else
		{
			pPipeline->setShaderStages( { { QRhiShaderStage::Vertex, faceVertex }, { QRhiShaderStage::Fragment, faceFragment } } );
			// Faces are wound clockwise seen from outside. QRhi keeps OpenGL's sense of winding on
			// every backend once clipSpaceCorrMatrix is applied.
			pPipeline->setFrontFace( QRhiGraphicsPipeline::CW );
			pPipeline->setCullMode( QRhiGraphicsPipeline::Back );
		}
		pPipeline->setDepthTest( !bLines );
		pPipeline->setDepthWrite( !bLines && ( nBlend == 0 || nBlend == 3 ) );
		pPipeline->setDepthOp( QRhiGraphicsPipeline::Less );
		if ( nBlend == 3 )
		{
			// Sky faces over a 3D skybox: the skybox where it drew (alpha 1), the sky elsewhere.
			QRhiGraphicsPipeline::TargetBlend blend;
			blend.enable = true;
			blend.srcColor = QRhiGraphicsPipeline::OneMinusDstAlpha;
			blend.dstColor = QRhiGraphicsPipeline::DstAlpha;
			blend.srcAlpha = QRhiGraphicsPipeline::One;
			blend.dstAlpha = QRhiGraphicsPipeline::Zero;
			pPipeline->setTargetBlends( { blend } );
		}
		else if ( nBlend == 4 )
		{
			QRhiGraphicsPipeline::TargetBlend blend;
			blend.enable = true;
			blend.srcColor = QRhiGraphicsPipeline::Zero;
			blend.dstColor = QRhiGraphicsPipeline::SrcColor;
			blend.srcAlpha = QRhiGraphicsPipeline::Zero;
			blend.dstAlpha = QRhiGraphicsPipeline::One;
			pPipeline->setTargetBlends( { blend } );
		}
		else if ( nBlend == 5 || nBlend == 6 )
		{
			// Particles, weighted by their alpha in the shader already (ShadeMaterial_t::m_bPremultiplied).
			QRhiGraphicsPipeline::TargetBlend blend;
			blend.enable = true;
			blend.srcColor = QRhiGraphicsPipeline::One;
			blend.dstColor = nBlend == 6 ? QRhiGraphicsPipeline::One : QRhiGraphicsPipeline::OneMinusSrcAlpha;
			blend.srcAlpha = QRhiGraphicsPipeline::One;
			blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
			pPipeline->setTargetBlends( { blend } );
		}
		else if ( nBlend != 0 )
		{
			QRhiGraphicsPipeline::TargetBlend blend;
			blend.enable = true;
			blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
			blend.dstColor = nBlend == 2 ? QRhiGraphicsPipeline::One : QRhiGraphicsPipeline::OneMinusSrcAlpha;
			blend.srcAlpha = QRhiGraphicsPipeline::One;
			blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
			pPipeline->setTargetBlends( { blend } );
		}
		pPipeline->setVertexInputLayout( bLines ? lineLayout : layout );
		pPipeline->setSampleCount( pTarget->sampleCount() );
		pPipeline->setShaderResourceBindings( m_LinesAll.m_pBindings.get() );
		pPipeline->setRenderPassDescriptor( pTarget->renderPassDescriptor() );
		pPipeline->create();
		return pPipeline;
	};
	m_pOpaque = make( 0, false );
	m_pBlended = make( 1, false );
	m_pAdditive = make( 2, false );
	m_pMultiply = make( 4, false );
	m_pPremultiplied = make( 5, false );
	m_pPremultipliedAdditive = make( 6, false );
	m_pLines = make( 0, true );
	m_pSkyFaces = make( 3, false );

	m_pNoBindings.reset( m_pRhi->newShaderResourceBindings() );
	m_pNoBindings->create();
	m_pAlpha.reset( m_pRhi->newGraphicsPipeline() );
	m_pAlpha->setShaderStages( { { QRhiShaderStage::Vertex, LoadShader( QStringLiteral( ":/shaders/mapalpha.vert.qsb" ) ) }, { QRhiShaderStage::Fragment, LoadShader( QStringLiteral( ":/shaders/mapalpha.frag.qsb" ) ) } } );
	QRhiGraphicsPipeline::TargetBlend alphaOnly;
	alphaOnly.colorWrite = QRhiGraphicsPipeline::A;
	m_pAlpha->setTargetBlends( { alphaOnly } );
	m_pAlpha->setSampleCount( pTarget->sampleCount() );
	m_pAlpha->setShaderResourceBindings( m_pNoBindings.get() );
	m_pAlpha->setRenderPassDescriptor( pTarget->renderPassDescriptor() );
	m_pAlpha->create();
	m_pAlphaSky.reset( m_pRhi->newGraphicsPipeline() );
	m_pAlphaSky->setShaderStages( { { QRhiShaderStage::Vertex, LoadShader( QStringLiteral( ":/shaders/mapalpha.vert.qsb" ) ) }, { QRhiShaderStage::Fragment, LoadShader( QStringLiteral( ":/shaders/mapalphasky.frag.qsb" ) ) } } );
	QRhiGraphicsPipeline::TargetBlend keepMarks = alphaOnly;
	keepMarks.enable = true;
	keepMarks.opAlpha = QRhiGraphicsPipeline::Max;
	m_pAlphaSky->setTargetBlends( { keepMarks } );
	m_pAlphaSky->setSampleCount( pTarget->sampleCount() );
	m_pAlphaSky->setShaderResourceBindings( m_pNoBindings.get() );
	m_pAlphaSky->setRenderPassDescriptor( pTarget->renderPassDescriptor() );
	m_pAlphaSky->create();

	m_pSkyView.reset( m_pRhi->newBuffer( QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof( SkyViewUniforms_t ) ) );
	m_pSkyView->create();
	MakeSkyBehindBindings();
	m_pSkyBehind.reset( m_pRhi->newGraphicsPipeline() );
	m_pSkyBehind->setShaderStages( { { QRhiShaderStage::Vertex, LoadShader( QStringLiteral( ":/shaders/mapsky.vert.qsb" ) ) }, { QRhiShaderStage::Fragment, LoadShader( QStringLiteral( ":/shaders/mapsky.frag.qsb" ) ) } } );
	m_pSkyBehind->setSampleCount( pTarget->sampleCount() );
	m_pSkyBehind->setShaderResourceBindings( m_pSkyBehindBindings.get() );
	m_pSkyBehind->setRenderPassDescriptor( pTarget->renderPassDescriptor() );
	m_pSkyBehind->create();
}

//-----------------------------------------------------------------------------
// Purpose: The 2D sky pass's bindings: its own uniforms and the sky's, as map.frag binds them.
//-----------------------------------------------------------------------------
void CMapRenderer::MakeSkyBehindBindings()
{
	const auto fragment = QRhiShaderResourceBinding::FragmentStage;
	m_pSkyBehindBindings.reset( m_pRhi->newShaderResourceBindings() );
	m_pSkyBehindBindings->setBindings( {
		QRhiShaderResourceBinding::uniformBuffer( 0, fragment, m_pSkyView.get() ),
		QRhiShaderResourceBinding::uniformBuffer( 6, fragment, m_pSkyUniforms.get() ),
		QRhiShaderResourceBinding::sampledTexture( 7, fragment, SkyFace( 0 ), SkySampler( 0 ) ),
		QRhiShaderResourceBinding::sampledTexture( 8, fragment, SkyFace( 1 ), SkySampler( 1 ) ),
		QRhiShaderResourceBinding::sampledTexture( 9, fragment, SkyFace( 2 ), SkySampler( 2 ) ),
		QRhiShaderResourceBinding::sampledTexture( 10, fragment, SkyFace( 3 ), SkySampler( 3 ) ),
		QRhiShaderResourceBinding::sampledTexture( 11, fragment, SkyFace( 4 ), SkySampler( 4 ) ),
		QRhiShaderResourceBinding::sampledTexture( 12, fragment, SkyFace( 5 ), SkySampler( 5 ) ),
	} );
	m_pSkyBehindBindings->create();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapRenderer::SetExposureSink( std::function<void( float )> pfnExposure, std::function<void()> pfnAgain )
{
	m_pfnExposure = std::move( pfnExposure );
	m_pfnAgain = std::move( pfnAgain );
}

//-----------------------------------------------------------------------------
// Purpose: Reads this picture back (into pReadback, for the pass's end) when the HDR lighting
//          preview shows and the last reading is a while ago; measured when it arrives.
//-----------------------------------------------------------------------------
void CMapRenderer::MeasureExposure( const MapFrame_t &frame, QRhiRenderTarget *pTarget, QRhiResourceUpdateBatch *&pReadback )
{
	if ( m_pReadback )
	{
		// Frames go on until it arrives.
		if ( m_pfnAgain )
		{
			m_pfnAgain();
		}
		return;
	}
	if ( frame.m_flExposure <= 0.0f || !m_pfnExposure || pTarget->resourceType() != QRhiResource::TextureRenderTarget )
	{
		return;
	}
	if ( m_SinceMeasured.isValid() && m_SinceMeasured.elapsed() < 150 )
	{
		return;
	}
	const QRhiColorAttachment *pAttachment = static_cast<QRhiTextureRenderTarget *>( pTarget )->description().cbeginColorAttachments();
	QRhiTexture *pTexture = pAttachment ? ( pAttachment->resolveTexture() ? pAttachment->resolveTexture() : pAttachment->texture() ) : nullptr;
	if ( !pTexture || ( pTexture->format() != QRhiTexture::RGBA8 && pTexture->format() != QRhiTexture::BGRA8 ) )
	{
		return;
	}
	m_SinceMeasured.start();
	m_pReadback = std::make_unique<QRhiReadbackResult>();
	const float flExposure = frame.m_flExposure;
	const float flGamma = frame.m_Options.m_Shade.m_flMonitorGamma;
	m_pReadback->completed = [ this, flExposure, flGamma ] {
		// Kept until the next reading: QRhi may still hold it while this runs.
		m_pReadbackDone = std::move( m_pReadback );
		QRhiReadbackResult *pResult = m_pReadbackDone.get();
		const int nWidth = pResult->pixelSize.width();
		const int nHeight = pResult->pixelSize.height();
		if ( nWidth <= 0 || nHeight <= 0 || pResult->data.size() < static_cast<qsizetype>( nWidth ) * nHeight * 4 )
		{
			return;
		}
		auto *pPixels = reinterpret_cast<std::uint8_t *>( pResult->data.data() );
		if ( pResult->format == QRhiTexture::BGRA8 )
		{
			for ( qsizetype i = 0; i + 3 < pResult->data.size(); i += 4 )
			{
				std::swap( pPixels[ i ], pPixels[ i + 2 ] );
			}
		}
		// Every fourth pixel each way is plenty for its brightest 2% and its middle.
		const float flNext = xv::map::NextExposure( xv::map::ShownLuminance( pPixels, nWidth, nHeight, static_cast<std::size_t>( nWidth ) * 4, 4, flGamma ), flExposure );
		if ( m_pfnExposure )
		{
			m_pfnExposure( flNext );
		}
	};
	pReadback = m_pRhi->nextResourceUpdateBatch();
	pReadback->readBackTexture( { pTexture }, m_pReadback.get() );
	if ( m_pfnAgain )
	{
		m_pfnAgain();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Opaque batches in view, then see-through ones far to near, then edges.
//-----------------------------------------------------------------------------
void CMapRenderer::Render( QRhiCommandBuffer *pCommands, QRhiRenderTarget *pTarget )
{
	const QSize size = pTarget->pixelSize();
	QElapsedTimer timer;
	timer.start();
	m_Stats.m_flIntervalMs = m_Interval.isValid() ? static_cast<double>( m_Interval.nsecsElapsed() ) / 1e6 : 0.0;
	m_Interval.start();
	m_Stats.m_nFrames++;
	m_Stats.m_nUploads = 0;
	m_Stats.m_flBindingsMs = 0;
	m_Stats.m_nBindingChanges = 0;
	const MapFrame_t frame = m_pfnSource ? m_pfnSource( size ) : MapFrame_t{};
	m_Stats.m_flSnapshotMs = static_cast<double>( timer.nsecsElapsed() ) / 1e6;
	QRhiResourceUpdateBatch *pUpdates = m_pRhi->nextResourceUpdateBatch();
	UploadMeshes( frame, pUpdates );
	UploadState( frame, pUpdates );
	UploadMaterials( frame, pUpdates );
	// A screen effect the card can draw: the scene into its texture first, the effect from it
	// onto the view after.
	QRhiRenderTarget *pFinal = pTarget;
	const bool bEffect = frame.m_pScreenEffect && frame.m_pScreenEffect->CanRun() && frame.m_pScreenEffect->GetBaked().isValid();
	if ( bEffect )
	{
		if ( !m_pEffectPass )
			m_pEffectPass = std::make_unique<CScreenEffectPass>();
		pTarget = m_pEffectPass->SceneTarget( m_pRhi, pFinal );
	}
	if ( m_pOpaque && m_pPipelinePass != pTarget->renderPassDescriptor() )
	{
		ResetPipelines();
	}
	if ( !m_pOpaque )
	{
		MakePipelines( pTarget );
		m_pPipelinePass = pTarget->renderPassDescriptor();
	}
	m_Stats.m_flUploadMs = static_cast<double>( timer.nsecsElapsed() ) / 1e6 - m_Stats.m_flSnapshotMs;
	const double flRecordFrom = static_cast<double>( timer.nsecsElapsed() ) / 1e6;
	m_Stats.m_nTriangles = 0;

	// The world's frame, and the 3D skybox's when it is drawn: textured views only.
	const xv::map::ShadeOptions_t &shade = frame.m_Options.m_Shade;
	const bool bSky = frame.m_Options.m_SkyboxCamera && shade.m_nMode != xv::map::RasterMode_t::RASTER_MODE_FLAT && shade.m_nMode != xv::map::RasterMode_t::RASTER_MODE_WIREFRAME;
	auto uniformsFor = [ & ]( const xv::map::CViewCamera &camera, bool bSkyPass ) {
		FrameUniforms_t uniforms{};
		// The shaders take positions relative to the eye (see map.vert), so the matrix looks from
		// the origin.
		xv::map::CViewCamera atOrigin = camera;
		atOrigin.m_Eye = {};
		const std::array<float, 16> matrix = atOrigin.GetViewProjection();
		const QMatrix4x4 viewProjection = m_pRhi->clipSpaceCorrMatrix() * QMatrix4x4( matrix.data() ).transposed();
		std::memcpy( uniforms.m_ViewProjection, viewProjection.constData(), sizeof uniforms.m_ViewProjection );
		for ( std::size_t i = 0; i < 3; i++ )
		{
			uniforms.m_Eye[ i ] = static_cast<float>( camera.m_Eye[ i ] );
			uniforms.m_EyeLow[ i ] = static_cast<float>( camera.m_Eye[ i ] - static_cast<double>( uniforms.m_Eye[ i ] ) );
		}
		uniforms.m_Mode[ 0 ] = static_cast<float>( static_cast<int>( shade.m_nMode ) );
		uniforms.m_Mode[ 1 ] = shade.m_flSelectionTint;
		uniforms.m_Mode[ 2 ] = shade.m_flLuxelShade;
		uniforms.m_Mode[ 3 ] = frame.m_Options.m_bSelectionEdges ? 1.0f : 0.0f;
		uniforms.m_Selection[ 0 ] = shade.m_vecSelection.m_flX;
		uniforms.m_Selection[ 1 ] = shade.m_vecSelection.m_flY;
		uniforms.m_Selection[ 2 ] = shade.m_vecSelection.m_flZ;
		for ( std::size_t i = 0; i < 3; i++ )
		{
			uniforms.m_SelectionEdge[ i ] = frame.m_Options.m_SelectionEdge[ i ] / 255.0f;
		}
		uniforms.m_FaceShade[ 0 ] = shade.m_vecFaceShade.m_flX;
		uniforms.m_FaceShade[ 1 ] = shade.m_vecFaceShade.m_flY;
		uniforms.m_FaceShade[ 2 ] = shade.m_vecFaceShade.m_flZ;
		uniforms.m_State[ 0 ] = static_cast<float>( m_nStateWidth );
		uniforms.m_State[ 1 ] = static_cast<float>( shade.m_nAnisotropy );
		uniforms.m_Instance[ 0 ] = shade.m_vecInstance.m_flX;
		uniforms.m_Instance[ 1 ] = shade.m_vecInstance.m_flY;
		uniforms.m_Instance[ 2 ] = shade.m_vecInstance.m_flZ;
		uniforms.m_Instance[ 3 ] = shade.m_flInstanceTint;
		uniforms.m_Pass[ 0 ] = bSkyPass ? 1.0f : 0.0f;
		uniforms.m_Invalid[ 0 ] = shade.m_vecInvalid.m_flX;
		uniforms.m_Invalid[ 1 ] = shade.m_vecInvalid.m_flY;
		uniforms.m_Invalid[ 2 ] = shade.m_vecInvalid.m_flZ;
		uniforms.m_Invalid[ 3 ] = shade.m_flInvalidTint;
		if ( frame.m_pLighting && frame.m_pLighting == m_pLightmapFor && m_pLightmap )
		{
			uniforms.m_Lighting[ 0 ] = static_cast<float>( m_pLightmapFor->m_nWidth );
			uniforms.m_Lighting[ 1 ] = static_cast<float>( m_pLightmapFor->m_nHeight );
		}
		if ( frame.m_pLighting )
		{
			// Vertex-lit props need no atlas; which look decides the curve.
			uniforms.m_Lighting[ 3 ] = shade.m_bLightHdr ? 2.0f : 1.0f;
		}
		uniforms.m_Lighting[ 2 ] = shade.m_flLightScale;
		if ( const xv::map::Fog_t *pFog = shade.m_pFog )
		{
			uniforms.m_FogColor[ 0 ] = pFog->m_vecColor.m_flX;
			uniforms.m_FogColor[ 1 ] = pFog->m_vecColor.m_flY;
			uniforms.m_FogColor[ 2 ] = pFog->m_vecColor.m_flZ;
			uniforms.m_FogColor[ 3 ] = static_cast<float>( pFog->m_flMaxDensity );
			uniforms.m_FogColor2[ 0 ] = pFog->m_vecColor2.m_flX;
			uniforms.m_FogColor2[ 1 ] = pFog->m_vecColor2.m_flY;
			uniforms.m_FogColor2[ 2 ] = pFog->m_vecColor2.m_flZ;
			uniforms.m_FogColor2[ 3 ] = pFog->m_bBlend ? 1.0f : 0.0f;
			for ( std::size_t i = 0; i < 3; i++ )
			{
				uniforms.m_FogDirection[ i ] = static_cast<float>( pFog->m_Direction[ i ] );
			}
			uniforms.m_FogDirection[ 3 ] = pFog->m_bRadial ? 1.0f : 0.0f;
			uniforms.m_FogRange[ 0 ] = static_cast<float>( pFog->m_flStart );
			uniforms.m_FogRange[ 1 ] = static_cast<float>( pFog->m_flEnd );
			uniforms.m_FogRange[ 2 ] = 1.0f;
		}
		uniforms.m_FogRange[ 3 ] = shade.m_bLightOnly ? 1.0f : 0.0f;
		uniforms.m_ViewForward[ 0 ] = shade.m_vecViewForward.m_flX;
		uniforms.m_ViewForward[ 1 ] = shade.m_vecViewForward.m_flY;
		uniforms.m_ViewForward[ 2 ] = shade.m_vecViewForward.m_flZ;
		uniforms.m_ViewForward[ 3 ] = shade.m_flMonitorGamma / 2.2f;
		return uniforms;
	};
	// Slot 1 and 3 are 0 and 2 for meshes as read.
	auto upload = [ & ]( FrameUniforms_t uniforms, quint32 nSlot ) {
		pUpdates->updateDynamicBuffer( m_pFrame.get(), m_nFrameStride * nSlot, sizeof uniforms, &uniforms );
		uniforms.m_Pass[ 1 ] = 1.0f;
		pUpdates->updateDynamicBuffer( m_pFrame.get(), m_nFrameStride * ( nSlot + 1 ), sizeof uniforms, &uniforms );
		uniforms.m_Pass[ 1 ] = 2.0f;
		pUpdates->updateDynamicBuffer( m_pFrame.get(), m_nFrameStride * ( nSlot + 2 ), sizeof uniforms, &uniforms );
	};
	upload( uniformsFor( frame.m_Camera, false ), 0 );
	xv::map::CViewCamera skyCamera;
	if ( bSky )
	{
		skyCamera = *frame.m_Options.m_SkyboxCamera;
		skyCamera.m_nWidth = frame.m_Camera.m_nWidth;
		skyCamera.m_nHeight = frame.m_Camera.m_nHeight;
		upload( uniformsFor( skyCamera, true ), 3 );
	}

	// The 2D sky behind everything, in place of the flat background (xv::map::RenderView's).
	const bool bTextured = shade.m_nMode != xv::map::RasterMode_t::RASTER_MODE_FLAT && shade.m_nMode != xv::map::RasterMode_t::RASTER_MODE_WIREFRAME;
	const bool bSkyBehind = frame.m_Options.m_bSkyBackground && m_pSkyFor && !frame.m_Options.m_bTransparent && bTextured;
	if ( bSkyBehind )
	{
		xv::map::CViewCamera atOrigin = frame.m_Camera;
		atOrigin.m_Eye = {};
		const std::array<float, 16> matrix = atOrigin.GetViewProjection();
		const QMatrix4x4 clipToWorld = ( m_pRhi->clipSpaceCorrMatrix() * QMatrix4x4( matrix.data() ).transposed() ).inverted();
		SkyViewUniforms_t view{};
		std::memcpy( view.m_ClipToWorld, clipToWorld.constData(), sizeof view.m_ClipToWorld );
		const bool bLighting = shade.m_nMode == xv::map::RasterMode_t::RASTER_MODE_LIGHTING;
		view.m_Options[ 0 ] = bLighting ? shade.m_flMonitorGamma / 2.2f : 0.0f;
		view.m_Options[ 1 ] = bLighting && frame.m_pLighting && shade.m_bLightHdr ? xv::map::UNMEASURED_ALPHA / 255.0f : 1.0f;
		pUpdates->updateDynamicBuffer( m_pSkyView.get(), 0, sizeof view, &view );
	}

	// Under a 3D skybox the picture starts see-through: what the skybox draws is opaque, and
	// the world's sky faces show the sky only where it is still see-through.
	const auto &background = frame.m_Options.m_Background;
	QElapsedTimer passClock;
	passClock.start();
	pCommands->beginPass( pTarget, QColor( background[ 0 ], background[ 1 ], background[ 2 ], bSky ? 0 : 255 ), { 1.0f, 0 }, pUpdates );
	m_Stats.m_flPassMs = passClock.nsecsElapsed() / 1e6;
	m_nDrawCalls = 0;
	if ( bSkyBehind )
	{
		// Opaque, so under a 3D skybox the sky faces show it where the skybox drew nothing, as
		// they would show the sky themselves.
		pCommands->setGraphicsPipeline( m_pSkyBehind.get() );
		pCommands->setShaderResources( m_pSkyBehindBindings.get() );
		pCommands->setViewport( { 0, 0, static_cast<float>( size.width() ), static_cast<float>( size.height() ) } );
		pCommands->draw( 3 );
		m_nDrawCalls++;
	}
	const xv::map::RasterScene_t scene = frame.MakeScene();
	auto materialOf = [ &scene ]( std::uint32_t n ) -> const xv::map::RasterMaterial_t * { return n < scene.m_Materials.size() ? &scene.m_Materials[ n ] : nullptr; };
	struct Later_t
	{
		std::size_t m_nMesh;
		const xv::map::MeshBatch_t *m_pBatch;
		double m_flDistance;
	};
	QRhiShaderResourceBindings *pBound = nullptr;
	quint32 nBoundOffset = 0;
	QRhiGraphicsPipeline *pPipeline = nullptr;
	quint32 nFrameOffset = 0; // the pass's slot: the world's or the 3D skybox's
	auto meshPass = [ &scene ]( std::size_t nMesh ) {
		if ( scene.m_AsRead.contains( scene.m_Meshes[ nMesh ] ) )
			return 1u;
		return scene.m_Overridden.contains( scene.m_Meshes[ nMesh ] ) ? 2u : 0u;
	};
	auto bind = [ & ]( QRhiShaderResourceBindings *pBindings, quint32 nPass ) {
		const quint32 nOffset = nFrameOffset + m_nFrameStride * nPass;
		if ( pBindings == pBound && nOffset == nBoundOffset )
		{
			return;
		}
		const QRhiCommandBuffer::DynamicOffset offset( 0, nOffset );
		QElapsedTimer bindingClock;
		bindingClock.start();
		pCommands->setShaderResources( pBindings, 1, &offset );
		m_Stats.m_flBindingsMs += bindingClock.nsecsElapsed() / 1e6;
		++m_Stats.m_nBindingChanges;
		pBound = pBindings;
		nBoundOffset = nOffset;
	};
	auto draw = [ & ]( std::size_t nMesh, const xv::map::MeshBatch_t &batch, QRhiGraphicsPipeline *pWanted, bool bReflection = false ) {
		if ( pPipeline != pWanted )
		{
			pCommands->setGraphicsPipeline( pWanted );
			pPipeline = pWanted;
			pBound = nullptr;
		}
		QRhiShaderResourceBindings *pBindings = batch.m_nMaterial < m_Materials.size() ? ( bReflection ? m_Materials[ batch.m_nMaterial ].m_pReflectBindings : m_Materials[ batch.m_nMaterial ].m_pBindings ).get() : m_LinesAll.m_pBindings.get();
		if ( !pBindings )
			return;
		bind( pBindings, meshPass( nMesh ) );
		const QRhiCommandBuffer::VertexInput inputs[] = { { m_Meshes[ nMesh ].m_pVertices.get(), 0 }, { m_Meshes[ nMesh ].m_pLight.get(), 0 } };
		pCommands->setVertexInput( 0, 2, inputs, m_Meshes[ nMesh ].m_pIndices.get(), 0, QRhiCommandBuffer::IndexUInt32 );
		pCommands->drawIndexed( batch.m_nIndexCount, 1, batch.m_nFirstIndex );
		m_nDrawCalls++;
		m_Stats.m_nTriangles += static_cast<int>( batch.m_nIndexCount / 3 );
	};
	// Every face in view of camera: opaque ones, then see-through ones far to near.
	auto faces = [ & ]( const xv::map::CViewCamera &camera, bool bSkyPass ) {
		std::vector<Later_t> later;
		for ( std::size_t nMesh = 0; nMesh < scene.m_Meshes.size(); nMesh++ )
		{
			const xv::map::CMapMesh &mesh = *scene.m_Meshes[ nMesh ];
			if ( !m_Meshes[ nMesh ].m_pVertices || !m_Meshes[ nMesh ].m_pIndices || !m_Meshes[ nMesh ].m_pLight )
			{
				continue;
			}
			for ( const xv::map::MeshBatch_t &batch : mesh.m_Batches )
			{
				const bool bChunk = batch.m_nChunk < mesh.m_Chunks.size();
				if ( bChunk && ( !camera.CanSee( mesh.m_Chunks[ batch.m_nChunk ].m_Bounds ) || ( bSkyPass && !mesh.m_Chunks[ batch.m_nChunk ].m_Bounds.Intersects( frame.m_Options.m_SkyboxRoom ) ) ) )
				{
					continue;
				}
				const xv::map::RasterMaterial_t *pMaterial = materialOf( batch.m_nMaterial );
				if ( pMaterial && IsToggledOff( *pMaterial, frame.m_Options.m_bToolTextures, frame.m_Options.m_bNodraw ) )
				{
					continue;
				}
				if ( shade.m_nMode != xv::map::RasterMode_t::RASTER_MODE_FLAT && IsSeeThrough( pMaterial ) )
				{
					const xv::map::Point_t centre = bChunk ? mesh.m_Chunks[ batch.m_nChunk ].m_Bounds.GetCenter() : camera.m_Eye;
					later.push_back( { nMesh, &batch, xv::map::Length( xv::map::Subtract( centre, camera.m_Eye ) ) } );
					continue;
				}
				const bool bSkyFace = bSky && !bSkyPass && pMaterial && pMaterial->m_Shade.m_bSky;
				draw( nMesh, batch, bSkyFace ? m_pSkyFaces.get() : m_pOpaque.get() );
			}
		}
		std::ranges::stable_sort( later, std::greater<>{}, &Later_t::m_flDistance );
		for ( const Later_t &batch : later )
		{
			const xv::map::RasterMaterial_t *pMaterial = materialOf( batch.m_pBatch->m_nMaterial );
			if ( pMaterial && pMaterial->m_Shade.m_bRefract )
			{
				// What is behind times the tint, then the reflection added (xv::map::ShadeRefract).
				draw( batch.m_nMesh, *batch.m_pBatch, m_pMultiply.get() );
				draw( batch.m_nMesh, *batch.m_pBatch, m_pAdditive.get(), true );
				continue;
			}
			const bool bAdditive = pMaterial && pMaterial->m_Shade.m_bAdditive;
			if ( pMaterial && pMaterial->m_Shade.m_bPremultiplied )
				draw( batch.m_nMesh, *batch.m_pBatch, bAdditive ? m_pPremultipliedAdditive.get() : m_pPremultiplied.get() );
			else
				draw( batch.m_nMesh, *batch.m_pBatch, bAdditive ? m_pAdditive.get() : m_pBlended.get() );
		}
	};
	const float flWidth = static_cast<float>( size.width() );
	const float flHeight = static_cast<float>( size.height() );
	if ( bSky )
	{
		// The 3D skybox first, from its own camera, kept behind everything the world draws.
		pCommands->setViewport( { 0, 0, flWidth, flHeight, 0.5f, 1.0f } );
		nFrameOffset = m_nFrameStride * 3;
		faces( skyCamera, true );
		nFrameOffset = 0;
		pBound = nullptr;
	}
	pCommands->setViewport( { 0, 0, flWidth, flHeight, 0.0f, bSky ? 0.5f : 1.0f } );
	if ( shade.m_nMode != xv::map::RasterMode_t::RASTER_MODE_WIREFRAME )
	{
		faces( frame.m_Camera, false );
	}
	// Edges: the wireframe's, then broken solids' and the selection's over everything.
	auto edges = [ & ]( Material_t &material, Material_t &models, bool bWireframe ) {
		pCommands->setGraphicsPipeline( m_pLines.get() );
		pBound = nullptr;
		for ( std::size_t nMesh = 0; nMesh < scene.m_Meshes.size(); nMesh++ )
		{
			const xv::map::CMapMesh &mesh = *scene.m_Meshes[ nMesh ];
			const bool bModels = scene.m_Models.contains( &mesh );
			bind( ( bModels ? models : material ).m_pBindings.get(), meshPass( nMesh ) );
			if ( !m_Meshes[ nMesh ].m_pLines || !m_Meshes[ nMesh ].m_pVertices || ( bWireframe && scene.m_NoWireframe.contains( &mesh ) ) )
			{
				continue;
			}
			const QRhiCommandBuffer::VertexInput input( m_Meshes[ nMesh ].m_pVertices.get(), 0 );
			pCommands->setVertexInput( 0, 1, &input, m_Meshes[ nMesh ].m_pLines.get(), 0, QRhiCommandBuffer::IndexUInt32 );
			pCommands->drawIndexed( static_cast<quint32>( mesh.m_Lines.size() ) );
			m_nDrawCalls++;
		}
	};
	if ( shade.m_nMode == xv::map::RasterMode_t::RASTER_MODE_WIREFRAME )
	{
		edges( m_LinesAll, m_ModelLinesAll, true );
	}
	edges( m_LinesSelected, m_ModelLinesSelected, false );
	if ( bSky )
	{
		pCommands->setGraphicsPipeline( frame.m_flExposure > 0.0f ? m_pAlphaSky.get() : m_pAlpha.get() );
		pCommands->setShaderResources( m_pNoBindings.get() );
		pCommands->setViewport( { 0, 0, flWidth, flHeight } );
		pCommands->draw( 3 );
	}
	QRhiResourceUpdateBatch *pReadback = nullptr;
	MeasureExposure( frame, pTarget, pReadback );
	pCommands->endPass( pReadback );
	if ( bEffect )
	{
		m_pEffectPass->Draw( pCommands, pFinal, frame.m_pScreenEffect, frame.m_flScreenTime );
	}
	m_Held = frame;
	m_Stats.m_nDrawCalls = m_nDrawCalls;
	m_Stats.m_flRecordMs = static_cast<double>( timer.nsecsElapsed() ) / 1e6 - flRecordFrom;
	m_Stats.m_flFrameMs = static_cast<double>( timer.nsecsElapsed() ) / 1e6;
	m_Stats.m_flGpuMs = m_pRhi->isFeatureSupported( QRhi::Timestamps ) ? pCommands->lastCompletedGpuTime() * 1000.0 : -1.0;
}

#endif
