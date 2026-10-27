//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapRenderer.h.
//
//===========================================================================//
#include "MapRenderer.h"

#ifdef VOMIT_TOOLS_GPU

#include <algorithm>
#include <cstring>

#include <QMatrix4x4>
#include <rhi/qrhi.h>

#include "map/MapConstants.h"

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
};

struct MaterialUniforms_t
{
	float m_Color[ 4 ];
	float m_Options[ 4 ];
	float m_Size[ 4 ];
	float m_Kind[ 4 ];
	float m_Extra[ 4 ];
};

// map.frag's sky block, as sky.frag's.
struct SkyUniforms_t
{
	float m_FaceRight[ 6 ][ 4 ];
	float m_FaceUp[ 6 ][ 4 ];
	float m_Transform[ 6 ][ 4 ];
	float m_Translate[ 6 ][ 4 ];
};

static_assert( sizeof( xv::map::MeshVertex_t ) == 48, "map.vert reads the vertex layout as it is" );

//-----------------------------------------------------------------------------
// Purpose: A material's uniforms, as map.frag reads them.
//-----------------------------------------------------------------------------
MaterialUniforms_t MaterialUniforms( const xv::map::RasterMaterial_t *pMaterial, bool bSelectionOnly )
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
	u.m_Kind[ 1 ] = shade.m_pBase && !shade.m_pBase->m_Mips.empty() ? 1.0f : 0.0f;
	u.m_Kind[ 2 ] = shade.m_pBase2 && !shade.m_pBase2->m_Mips.empty() ? 1.0f : 0.0f;
	u.m_Kind[ 3 ] = bSelectionOnly ? 1.0f : 0.0f;
	u.m_Extra[ 0 ] = shade.m_bSky ? 1.0f : 0.0f;
	u.m_Extra[ 1 ] = shade.m_bTinted ? 1.0f : 0.0f;
	u.m_Extra[ 2 ] = shade.m_bUnlit ? 1.0f : 0.0f;
	return u;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsSeeThrough( const xv::map::RasterMaterial_t *pMaterial )
{
	return pMaterial && !pMaterial->m_bObjectColor && ( pMaterial->m_Shade.m_bTranslucent || pMaterial->m_Shade.m_bAdditive || pMaterial->m_Shade.m_flAlpha < 1.0f );
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
	m_pOpaque.reset();
	m_pBlended.reset();
	m_pAdditive.reset();
	m_pLines.reset();
	m_Materials.clear();
	m_LinesAll = {};
	m_LinesSelected = {};
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
	for ( auto &pSampler : m_TopSamplers )
	{
		pSampler.reset();
	}
	for ( auto &pSampler : m_Samplers )
	{
		pSampler.reset();
	}
	m_pNearest.reset();
	m_pSkyFaces.reset();
	m_pAlpha.reset();
	m_pNoBindings.reset();
	m_pMaterialsFor = nullptr;
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
		// Two frames' uniforms: the world's, and the 3D skybox's camera's.
		m_nFrameStride = m_pRhi->ubufAligned( sizeof( FrameUniforms_t ) );
		m_pFrame.reset( m_pRhi->newBuffer( QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, m_nFrameStride * 2 ) );
		m_pFrame->create();
		for ( std::size_t i = 0; i < m_Samplers.size(); i++ )
		{
			const auto s = ( i & 1 ) != 0 ? QRhiSampler::ClampToEdge : QRhiSampler::Repeat;
			const auto t = ( i & 2 ) != 0 ? QRhiSampler::ClampToEdge : QRhiSampler::Repeat;
			m_Samplers[ i ].reset( m_pRhi->newSampler( QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear, s, t ) );
			m_Samplers[ i ]->create();
			m_TopSamplers[ i ].reset( m_pRhi->newSampler( QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, s, t ) );
			m_TopSamplers[ i ]->create();
		}
		m_pNearest.reset( m_pRhi->newSampler( QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge ) );
		m_pNearest->create();
	}
	// Pipelines follow the target's pass and sample count.
	m_pOpaque.reset();
	m_pBlended.reset();
	m_pAdditive.reset();
	m_pLines.reset();
}

//-----------------------------------------------------------------------------
// Purpose: The map's mesh and the entity boxes, uploaded once each.
//-----------------------------------------------------------------------------
void CMapRenderer::UploadMeshes( const MapFrame_t &frame, QRhiResourceUpdateBatch *pUpdates )
{
	const std::vector<const xv::map::CMapMesh *> meshes = frame.MakeScene().m_Meshes;
	m_Meshes.resize( meshes.size() );
	for ( std::size_t i = 0; i < meshes.size(); i++ )
	{
		Mesh_t &mesh = m_Meshes[ i ];
		const xv::map::CMapMesh *pMesh = meshes[ i ];
		if ( mesh.m_pFor == pMesh )
		{
			continue;
		}
		mesh = {};
		mesh.m_pFor = pMesh;
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
		m_LinesAll = {};
		m_LinesSelected = {};
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
	if ( texture.m_Mips.empty() || texture.m_Mips.front().Empty() )
	{
		return m_pWhite.get();
	}
	const xv::texture::CImage &top = texture.m_Mips.front();
	const bool bMips = xv::map::HasMipChain( texture );
	pTexture.reset( m_pRhi->newTexture( QRhiTexture::RGBA8, { top.m_nWidth, top.m_nHeight }, 1, bMips ? QRhiTexture::MipMapped : QRhiTexture::Flags{} ) );
	pTexture->create();
	const int nLevels = bMips ? m_pRhi->mipLevelsForSize( { top.m_nWidth, top.m_nHeight } ) : 1;
	QList<QRhiTextureUploadEntry> entries;
	for ( int nLevel = 0; nLevel < nLevels; nLevel++ )
	{
		const xv::texture::CImage &image = texture.m_Mips[ static_cast<std::size_t>( nLevel ) ];
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
	return pTexture && !xv::map::HasMipChain( *pTexture ) ? m_TopSamplers[ nIndex ].get() : m_Samplers[ nIndex ].get();
}

//-----------------------------------------------------------------------------
// Purpose: Every binding every pipeline declares, so one pipeline draws with any material's.
//-----------------------------------------------------------------------------
void CMapRenderer::MakeBindings( Material_t &material, QRhiTexture *pBase, QRhiTexture *pBase2, const xv::map::RasterTexture_t *pBaseTexture, const xv::map::RasterTexture_t *pBase2Texture )
{
	const auto both = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
	material.m_pBindings.reset( m_pRhi->newShaderResourceBindings() );
	material.m_pBindings->setBindings( {
		QRhiShaderResourceBinding::uniformBufferWithDynamicOffset( 0, both, m_pFrame.get(), sizeof( FrameUniforms_t ) ),
		QRhiShaderResourceBinding::uniformBuffer( 1, both, material.m_pUniforms.get() ),
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
	} );
	material.m_pBindings->create();
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
	if ( m_pSkyUniforms && frame.m_pSky.get() == m_pSkyFor )
	{
		return false;
	}
	m_pSkyFor = frame.m_pSky.get();
	if ( !m_pSkyUniforms )
	{
		m_pSkyUniforms.reset( m_pRhi->newBuffer( QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof( SkyUniforms_t ) ) );
		m_pSkyUniforms->create();
	}
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
		m_SkyFaces[ i ].reset();
		const xv::sky::CUvTransform t = m_pSkyFor ? m_pSkyFor->m_Transforms[ i ] : xv::sky::CUvTransform{};
		uniforms.m_Transform[ i ][ 0 ] = t.m_flA;
		uniforms.m_Transform[ i ][ 1 ] = t.m_flB;
		uniforms.m_Transform[ i ][ 2 ] = t.m_flC;
		uniforms.m_Transform[ i ][ 3 ] = t.m_flD;
		uniforms.m_Translate[ i ][ 0 ] = t.m_flTx;
		uniforms.m_Translate[ i ][ 1 ] = t.m_flTy;
		const xv::texture::CImage *pImage = m_pSkyFor ? &m_pSkyFor->m_Faces[ i ] : nullptr;
		if ( pImage && !pImage->Empty() )
		{
			m_SkyFaces[ i ].reset( m_pRhi->newTexture( QRhiTexture::RGBA8, { pImage->m_nWidth, pImage->m_nHeight } ) );
			m_SkyFaces[ i ]->create();
			pUpdates->uploadTexture( m_SkyFaces[ i ].get(), QImage( pImage->m_Pixels.data(), pImage->m_nWidth, pImage->m_nHeight, pImage->m_nWidth * 4, QImage::Format_RGBA8888 ) );
			uniforms.m_Translate[ i ][ 2 ] = 1.0f;
		}
	}
	uniforms.m_Translate[ 0 ][ 3 ] = m_pSkyFor ? 1.0f : 0.0f;
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
	if ( UploadSky( frame, pUpdates ) )
	{
		// Everything binds the sky's faces.
		m_LinesAll.m_pBindings.reset();
		m_LinesSelected.m_pBindings.reset();
		m_pMaterialsFor = nullptr;
	}
	if ( !m_LinesAll.m_pBindings )
	{
		uniforms( m_LinesAll, MaterialUniforms( nullptr, false ) );
		MakeBindings( m_LinesAll, m_pWhite.get(), m_pWhite.get(), nullptr, nullptr );
		uniforms( m_LinesSelected, MaterialUniforms( nullptr, true ) );
		MakeBindings( m_LinesSelected, m_pWhite.get(), m_pWhite.get(), nullptr, nullptr );
	}
	if ( frame.m_pMaterials.get() == m_pMaterialsFor )
	{
		return;
	}
	m_pMaterialsFor = frame.m_pMaterials.get();
	const std::size_t nCount = frame.m_pMaterials ? frame.m_pMaterials->size() : 0;
	m_Materials.resize( nCount );
	for ( std::size_t i = 0; i < nCount; i++ )
	{
		const xv::map::RasterMaterial_t &source = ( *frame.m_pMaterials )[ i ];
		Material_t &material = m_Materials[ i ];
		uniforms( material, MaterialUniforms( &source, false ) );
		// Textures that are the same as last time stay; new ones go up.
		QRhiTexture *pBase = m_pWhite.get();
		QRhiTexture *pBase2 = m_pWhite.get();
		if ( source.m_Shade.m_pBase )
		{
			pBase = material.m_pBaseFor == source.m_Shade.m_pBase && material.m_pBase ? material.m_pBase.get() : Upload( *source.m_Shade.m_pBase, material.m_pBase, pUpdates );
		}
		if ( source.m_Shade.m_pBase2 )
		{
			pBase2 = material.m_pBase2For == source.m_Shade.m_pBase2 && material.m_pBase2 ? material.m_pBase2.get() : Upload( *source.m_Shade.m_pBase2, material.m_pBase2, pUpdates );
		}
		material.m_pBaseFor = source.m_Shade.m_pBase;
		material.m_pBase2For = source.m_Shade.m_pBase2;
		MakeBindings( material, pBase, pBase2, source.m_Shade.m_pBase, source.m_Shade.m_pBase2 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Opaque, see-through, added and line pipelines, all with the same bindings.
//-----------------------------------------------------------------------------
void CMapRenderer::MakePipelines( QRhiRenderTarget *pTarget )
{
	QRhiVertexInputLayout layout;
	layout.setBindings( { QRhiVertexInputBinding( sizeof( xv::map::MeshVertex_t ) ) } );
	layout.setAttributes( {
		QRhiVertexInputAttribute( 0, 0, QRhiVertexInputAttribute::Float3, 0 ),
		QRhiVertexInputAttribute( 0, 1, QRhiVertexInputAttribute::Float3, 12 ),
		QRhiVertexInputAttribute( 0, 2, QRhiVertexInputAttribute::Float2, 24 ),
		QRhiVertexInputAttribute( 0, 3, QRhiVertexInputAttribute::Float2, 32 ),
		QRhiVertexInputAttribute( 0, 4, QRhiVertexInputAttribute::Float, 40 ),
		QRhiVertexInputAttribute( 0, 5, QRhiVertexInputAttribute::UInt, 44 ),
	} );
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
		pPipeline->setVertexInputLayout( layout );
		pPipeline->setSampleCount( pTarget->sampleCount() );
		pPipeline->setShaderResourceBindings( m_LinesAll.m_pBindings.get() );
		pPipeline->setRenderPassDescriptor( pTarget->renderPassDescriptor() );
		pPipeline->create();
		return pPipeline;
	};
	m_pOpaque = make( 0, false );
	m_pBlended = make( 1, false );
	m_pAdditive = make( 2, false );
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
}

//-----------------------------------------------------------------------------
// Purpose: Opaque batches in view, then see-through ones far to near, then edges.
//-----------------------------------------------------------------------------
void CMapRenderer::Render( QRhiCommandBuffer *pCommands, QRhiRenderTarget *pTarget )
{
	const QSize size = pTarget->pixelSize();
	const MapFrame_t frame = m_pfnSource ? m_pfnSource( size ) : MapFrame_t{};
	QRhiResourceUpdateBatch *pUpdates = m_pRhi->nextResourceUpdateBatch();
	UploadMeshes( frame, pUpdates );
	UploadState( frame, pUpdates );
	UploadMaterials( frame, pUpdates );
	if ( !m_pOpaque )
	{
		MakePipelines( pTarget );
	}

	// The world's frame, and the 3D skybox's when it is drawn: textured views only.
	const xv::map::ShadeOptions_t &shade = frame.m_Options.m_Shade;
	const bool bSky = frame.m_Options.m_SkyboxCamera && shade.m_nMode != xv::map::RasterMode_t::RASTER_MODE_FLAT && shade.m_nMode != xv::map::RasterMode_t::RASTER_MODE_WIREFRAME;
	auto uniformsFor = [ & ]( const xv::map::CViewCamera &camera, bool bSkyPass ) {
		FrameUniforms_t uniforms{};
		const std::array<float, 16> matrix = camera.GetViewProjection();
		const QMatrix4x4 viewProjection = m_pRhi->clipSpaceCorrMatrix() * QMatrix4x4( matrix.data() ).transposed();
		std::memcpy( uniforms.m_ViewProjection, viewProjection.constData(), sizeof uniforms.m_ViewProjection );
		uniforms.m_Eye[ 0 ] = static_cast<float>( camera.m_Eye[ 0 ] );
		uniforms.m_Eye[ 1 ] = static_cast<float>( camera.m_Eye[ 1 ] );
		uniforms.m_Eye[ 2 ] = static_cast<float>( camera.m_Eye[ 2 ] );
		uniforms.m_Mode[ 0 ] = static_cast<float>( static_cast<int>( shade.m_nMode ) );
		uniforms.m_Mode[ 1 ] = shade.m_flSelectionTint;
		uniforms.m_Mode[ 2 ] = shade.m_flLuxelShade;
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
		uniforms.m_Instance[ 0 ] = shade.m_vecInstance.m_flX;
		uniforms.m_Instance[ 1 ] = shade.m_vecInstance.m_flY;
		uniforms.m_Instance[ 2 ] = shade.m_vecInstance.m_flZ;
		uniforms.m_Instance[ 3 ] = shade.m_flInstanceTint;
		uniforms.m_Pass[ 0 ] = bSkyPass ? 1.0f : 0.0f;
		return uniforms;
	};
	const FrameUniforms_t world = uniformsFor( frame.m_Camera, false );
	pUpdates->updateDynamicBuffer( m_pFrame.get(), 0, sizeof world, &world );
	xv::map::CViewCamera skyCamera;
	if ( bSky )
	{
		skyCamera = *frame.m_Options.m_SkyboxCamera;
		skyCamera.m_nWidth = frame.m_Camera.m_nWidth;
		skyCamera.m_nHeight = frame.m_Camera.m_nHeight;
		const FrameUniforms_t sky = uniformsFor( skyCamera, true );
		pUpdates->updateDynamicBuffer( m_pFrame.get(), m_nFrameStride, sizeof sky, &sky );
	}

	// Under a 3D skybox the picture starts see-through: what the skybox draws is opaque, and
	// the world's sky faces show the sky only where it is still see-through.
	const auto &background = frame.m_Options.m_Background;
	pCommands->beginPass( pTarget, QColor( background[ 0 ], background[ 1 ], background[ 2 ], bSky ? 0 : 255 ), { 1.0f, 0 }, pUpdates );
	m_nDrawCalls = 0;
	const xv::map::RasterScene_t scene = frame.MakeScene();
	auto materialOf = [ &scene ]( std::uint32_t n ) -> const xv::map::RasterMaterial_t * { return n < scene.m_Materials.size() ? &scene.m_Materials[ n ] : nullptr; };
	struct Later_t
	{
		std::size_t m_nMesh;
		const xv::map::MeshBatch_t *m_pBatch;
		double m_flDistance;
	};
	QRhiShaderResourceBindings *pBound = nullptr;
	QRhiGraphicsPipeline *pPipeline = nullptr;
	quint32 nFrameOffset = 0;
	auto bind = [ & ]( QRhiShaderResourceBindings *pBindings ) {
		const QRhiCommandBuffer::DynamicOffset offset( 0, nFrameOffset );
		pCommands->setShaderResources( pBindings, 1, &offset );
		pBound = pBindings;
	};
	auto draw = [ & ]( std::size_t nMesh, const xv::map::MeshBatch_t &batch, QRhiGraphicsPipeline *pWanted ) {
		if ( pPipeline != pWanted )
		{
			pCommands->setGraphicsPipeline( pWanted );
			pPipeline = pWanted;
			pBound = nullptr;
		}
		QRhiShaderResourceBindings *pBindings = batch.m_nMaterial < m_Materials.size() ? m_Materials[ batch.m_nMaterial ].m_pBindings.get() : m_LinesAll.m_pBindings.get();
		if ( pBindings != pBound )
		{
			bind( pBindings );
		}
		const QRhiCommandBuffer::VertexInput input( m_Meshes[ nMesh ].m_pVertices.get(), 0 );
		pCommands->setVertexInput( 0, 1, &input, m_Meshes[ nMesh ].m_pIndices.get(), 0, QRhiCommandBuffer::IndexUInt32 );
		pCommands->drawIndexed( batch.m_nIndexCount, 1, batch.m_nFirstIndex );
		m_nDrawCalls++;
	};
	// Every face in view of camera: opaque ones, then see-through ones far to near.
	auto faces = [ & ]( const xv::map::CViewCamera &camera, bool bSkyPass ) {
		std::vector<Later_t> later;
		for ( std::size_t nMesh = 0; nMesh < scene.m_Meshes.size(); nMesh++ )
		{
			const xv::map::CMapMesh &mesh = *scene.m_Meshes[ nMesh ];
			if ( !m_Meshes[ nMesh ].m_pVertices || !m_Meshes[ nMesh ].m_pIndices )
			{
				continue;
			}
			for ( const xv::map::MeshBatch_t &batch : mesh.m_Batches )
			{
				const bool bChunk = batch.m_nChunk < mesh.m_Chunks.size();
				if ( bChunk && !camera.CanSee( mesh.m_Chunks[ batch.m_nChunk ].m_Bounds ) )
				{
					continue;
				}
				const xv::map::RasterMaterial_t *pMaterial = materialOf( batch.m_nMaterial );
				if ( pMaterial && pMaterial->m_bTool && !frame.m_Options.m_bToolTextures )
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
			draw( batch.m_nMesh, *batch.m_pBatch, pMaterial && pMaterial->m_Shade.m_bAdditive ? m_pAdditive.get() : m_pBlended.get() );
		}
	};
	const float flWidth = static_cast<float>( size.width() );
	const float flHeight = static_cast<float>( size.height() );
	if ( bSky )
	{
		// The 3D skybox first, from its own camera, kept behind everything the world draws.
		pCommands->setViewport( { 0, 0, flWidth, flHeight, 0.5f, 1.0f } );
		nFrameOffset = m_nFrameStride;
		faces( skyCamera, true );
		nFrameOffset = 0;
		pBound = nullptr;
	}
	pCommands->setViewport( { 0, 0, flWidth, flHeight, 0.0f, bSky ? 0.5f : 1.0f } );
	if ( shade.m_nMode != xv::map::RasterMode_t::RASTER_MODE_WIREFRAME )
	{
		faces( frame.m_Camera, false );
	}
	// Edges: the wireframe's, then the selection's over everything.
	auto edges = [ & ]( Material_t &material ) {
		pCommands->setGraphicsPipeline( m_pLines.get() );
		bind( material.m_pBindings.get() );
		for ( std::size_t nMesh = 0; nMesh < scene.m_Meshes.size(); nMesh++ )
		{
			const xv::map::CMapMesh &mesh = *scene.m_Meshes[ nMesh ];
			if ( !m_Meshes[ nMesh ].m_pLines || !m_Meshes[ nMesh ].m_pVertices )
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
		edges( m_LinesAll );
	}
	if ( frame.m_Options.m_bSelectionEdges )
	{
		edges( m_LinesSelected );
	}
	if ( bSky )
	{
		pCommands->setGraphicsPipeline( m_pAlpha.get() );
		pCommands->setShaderResources( m_pNoBindings.get() );
		pCommands->setViewport( { 0, 0, flWidth, flHeight } );
		pCommands->draw( 3 );
	}
	pCommands->endPass();
	m_Held = frame;
}

#endif
