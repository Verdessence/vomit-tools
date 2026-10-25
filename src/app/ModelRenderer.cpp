//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: See ModelRenderer.h.
//===========================================================================//
#include "ModelRenderer.h"
#ifdef VOMIT_TOOLS_GPU
#include <rhi/qrhi.h>
#include <cmath>
#include <numbers>
using namespace xv;
namespace
{
struct MeshUniforms_t
{
	float m_Bones[ 256 ][ 12 ];
	float m_Eye[ 4 ], m_Right[ 4 ], m_Up[ 4 ], m_Forward[ 4 ], m_Projection[ 4 ], m_Depth[ 4 ];
};
//-----------------------------------------------------------------------------
// Purpose: Copies vector components into a std140 vec4.
//-----------------------------------------------------------------------------
void PutVector( float *p, CVec3 v )
{
	p[ 0 ] = v.m_flX;
	p[ 1 ] = v.m_flY;
	p[ 2 ] = v.m_flZ;
}
} // namespace
//-----------------------------------------------------------------------------
// Purpose: Keeps the mesh and its inspection quads in one pass with one depth buffer.
//-----------------------------------------------------------------------------
CModelRenderer::CModelRenderer( Source_t pfnSource )
	: m_pfnSource( std::move( pfnSource ) ),
	  m_Overlays( [ this ]( QSize size, std::vector<ParticlePicture_t> &pictures, std::array<float, 3> &background ) {background={0.13f,0.14f,0.15f};ParticlePicture_t picture;picture.m_Rect=QRect(QPoint{},size);picture.m_Quads=m_Frame.m_Quads;pictures.push_back(std::move(picture)); } )
{}
//-----------------------------------------------------------------------------
// Purpose: Destroys RHI resources before their owning device goes away.
//-----------------------------------------------------------------------------
CModelRenderer::~CModelRenderer() = default;
//-----------------------------------------------------------------------------
// Purpose: Reinitializes pipelines when the device or render target changes.
//-----------------------------------------------------------------------------
void CModelRenderer::Initialize( QRhi *pRhi, QRhiRenderTarget *pTarget )
{
	if ( m_pRhi != pRhi )
	{
		Release();
		m_pRhi = pRhi;
	}
	m_Overlays.Initialize( pRhi, pTarget );
	for ( auto &p : m_Materials )
		p->Initialize( pRhi, pTarget );
	if ( !m_pMeshUniforms )
	{
		m_pMeshUniforms.reset( pRhi->newBuffer( QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof( MeshUniforms_t ) ) );
		m_pMeshUniforms->create();
	}
}
//-----------------------------------------------------------------------------
// Purpose: Drops uploaded images when a skin or material source is replaced.
//-----------------------------------------------------------------------------
void CModelRenderer::ForgetTextures()
{
	m_Materials.clear();
	m_Overlays.ForgetTextures();
}
//-----------------------------------------------------------------------------
// Purpose: Releases resources tied to the graphics device.
//-----------------------------------------------------------------------------
void CModelRenderer::Release()
{
	m_Materials.clear();
	m_Overlays.Release();
	m_pVertices.reset();
	m_pMeshUniforms.reset();
	m_pRhi = nullptr;
}
//-----------------------------------------------------------------------------
// Purpose: Uploads the reference mesh and its current bone palette for vertex-shader skinning.
//-----------------------------------------------------------------------------
void CModelRenderer::Prepare( QRhiRenderTarget *pTarget, QRhiResourceUpdateBatch *pUpdates )
{
	m_Runs.clear();
	const auto *pModel = m_Frame.m_pReference;
	if ( !pModel )
		return;
	MeshUniforms_t u{};
	for ( std::size_t i = 0; i < 256; i++ )
	{
		model::CTransform transform;
		if ( i < pModel->m_Bones.size() && m_Frame.m_pPosed && i < m_Frame.m_pPosed->m_Bones.size() )
			transform = m_Frame.m_pPosed->m_Bones[ i ].m_ToModel * pModel->m_Bones[ i ].m_ToModel.Inverse().value_or( model::CTransform{} );
		for ( int row = 0; row < 3; row++ )
			for ( int col = 0; col < 4; col++ )
				u.m_Bones[ i ][ row * 4 + col ] = transform.m_Values[ row * 4 + col ];
	}
	const auto &camera = m_Frame.m_Camera;
	const auto vecForward = camera.GetForward(), vecRight = Normalized( Cross( vecForward, { 0, 0, 1 } ) ), vecUp = Normalized( Cross( vecRight, vecForward ) );
	PutVector( u.m_Eye, camera.GetEye() );
	PutVector( u.m_Forward, vecForward );
	PutVector( u.m_Right, vecRight );
	PutVector( u.m_Up, vecUp );
	u.m_Projection[ 0 ] = camera.m_nHeight * 0.5f / std::tan( camera.m_flFov * std::numbers::pi_v<float> / 360.0f );
	u.m_Projection[ 1 ] = camera.m_nWidth;
	u.m_Projection[ 2 ] = camera.m_nHeight;
	u.m_Projection[ 3 ] = m_pRhi->isYUpInNDC() ? 1.0f : -1.0f;
	u.m_Depth[ 0 ] = m_pRhi->isClipDepthZeroToOne() ? 1.0f : 0.0f;
	u.m_Depth[ 1 ] = 1.0f / std::max( 1.0f, Length( pModel->m_vecMax - pModel->m_vecMin ) );
	pUpdates->updateDynamicBuffer( m_pMeshUniforms.get(), 0, sizeof( u ), &u );
	const std::size_t nMaterials = m_Frame.m_pMaterials ? m_Frame.m_pMaterials->size() : 0;
	if ( m_Materials.size() != nMaterials + 1 )
	{
		m_Materials.clear();
		for ( std::size_t i = 0; i <= nMaterials; i++ )
		{
			auto p = std::make_unique<CMaterialRenderer>( CMaterialRenderer::Source_t{} );
			p->m_pMeshUniforms = m_pMeshUniforms.get();
			p->Initialize( m_pRhi, pTarget );
			m_Materials.push_back( std::move( p ) );
		}
	}
	for ( std::size_t i = 0; i < m_Materials.size(); i++ )
	{
		CMaterialRenderer::Frame_t frame;
		ModelMaterial_t fallback;
		const auto &material = i < nMaterials ? ( *m_Frame.m_pMaterials )[ i ] : fallback;
		frame.m_pMaterial = &material.m_Material;
		frame.m_pTextures = &material.m_Textures;
		frame.m_Options.m_nSize = std::max( camera.m_nWidth, camera.m_nHeight );
		m_Materials[ i ]->Prepare( frame, pTarget, pUpdates );
	}
	std::vector<float> vertices;
	for ( const auto &mesh : pModel->m_Meshes )
	{
		Run_t run;
		run.m_nMaterial = mesh.m_nMaterial >= 0 && mesh.m_nMaterial < static_cast<int>( nMaterials ) ? mesh.m_nMaterial : nMaterials;
		run.m_nFirst = vertices.size() / 20;
		run.m_nCount = mesh.m_Indices.size();
		for ( std::size_t i = 0; i + 2 < mesh.m_Indices.size(); i += 3 )
		{
			const auto &a = pModel->m_Vertices[ mesh.m_Indices[ i ] ], &b = pModel->m_Vertices[ mesh.m_Indices[ i + 1 ] ], &c = pModel->m_Vertices[ mesh.m_Indices[ i + 2 ] ];
			const auto vecEdge = b.m_vecPosition - a.m_vecPosition, vecOther = c.m_vecPosition - a.m_vecPosition;
			const float du = b.m_Uv[ 0 ] - a.m_Uv[ 0 ], dv = b.m_Uv[ 1 ] - a.m_Uv[ 1 ], eu = c.m_Uv[ 0 ] - a.m_Uv[ 0 ], ev = c.m_Uv[ 1 ] - a.m_Uv[ 1 ], det = du * ev - eu * dv;
			const auto vecTangent = std::abs( det ) > 1e-8f ? Normalized( ( vecEdge * ev - vecOther * dv ) * ( 1.0f / det ) ) : Normalized( Cross( a.m_vecNormal, { 0, 0, 1 } ) );
			const auto vecBitangent = std::abs( det ) > 1e-8f ? Normalized( ( vecOther * du - vecEdge * eu ) * ( 1.0f / det ) ) : Normalized( Cross( a.m_vecNormal, vecTangent ) );
			for ( int k = 0; k < 3; k++ )
			{
				const auto &v = pModel->m_Vertices[ mesh.m_Indices[ i + k ] ];
				vertices.insert( vertices.end(), { v.m_vecPosition.m_flX, v.m_vecPosition.m_flY, v.m_vecPosition.m_flZ, v.m_vecNormal.m_flX, v.m_vecNormal.m_flY, v.m_vecNormal.m_flZ, v.m_Uv[ 0 ], v.m_Uv[ 1 ] } );
				for ( int j = 0; j < 3; j++ )
					vertices.push_back( j < v.m_nBones && v.m_Bones[ j ] >= 0 && v.m_Bones[ j ] < 256 ? v.m_Weights[ j ] : 0 );
				for ( int j = 0; j < 3; j++ )
					vertices.push_back( std::clamp( v.m_Bones[ j ], 0, 255 ) );
				vertices.insert( vertices.end(), { vecTangent.m_flX, vecTangent.m_flY, vecTangent.m_flZ, vecBitangent.m_flX, vecBitangent.m_flY, vecBitangent.m_flZ } );
			}
		}
		m_Runs.push_back( run );
	}
	if ( vertices.empty() )
		return;
	const quint32 nBytes = vertices.size() * sizeof( float );
	if ( !m_pVertices || m_pVertices->size() < nBytes )
	{
		m_pVertices.reset( m_pRhi->newBuffer( QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, nBytes ) );
		m_pVertices->create();
	}
	pUpdates->updateDynamicBuffer( m_pVertices.get(), 0, nBytes, vertices.data() );
}
//-----------------------------------------------------------------------------
// Purpose: Draws skinned materials first and depth-tested inspection quads after them.
//-----------------------------------------------------------------------------
void CModelRenderer::Render( QRhiCommandBuffer *pCommands, QRhiRenderTarget *pTarget )
{
	m_Frame = m_pfnSource( pTarget->pixelSize() );
	m_Overlays.RenderWith( pCommands, pTarget, [ & ]( QRhiResourceUpdateBatch *pUpdates ) { Prepare( pTarget, pUpdates ); }, [ & ] {if(!m_pVertices)return;const QRhiCommandBuffer::VertexInput input{m_pVertices.get(),0};for(const auto &run:m_Runs){auto &material=*m_Materials[run.m_nMaterial];pCommands->setGraphicsPipeline(material.m_pPipeline.get());pCommands->setShaderResources(material.m_pBindings.get());pCommands->setVertexInput(0,1,&input);pCommands->draw(run.m_nCount,1,run.m_nFirst);} } );
}
#endif
