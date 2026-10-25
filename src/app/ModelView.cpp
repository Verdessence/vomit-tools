//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: See ModelView.h.
//===========================================================================//
#include "ModelView.h"
#include "Graphics.h"
#include "ImageFiles.h"
#include "ParticleRenderer.h"
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
using namespace xv;
namespace
{
class CModelLabels : public QWidget
{
public:
	//-----------------------------------------------------------------------------
	// Purpose: Adds a transparent layer for bone names above either drawing backend.
	//-----------------------------------------------------------------------------
	CModelLabels( QWidget *pParent, std::function<void( QPainter & )> pfnDraw )
		: QWidget( pParent ),
		  m_pfnDraw( std::move( pfnDraw ) )
	{
		setAttribute( Qt::WA_TransparentForMouseEvents );
	}

protected:
	//-----------------------------------------------------------------------------
	// Purpose: Draws the labels without intercepting viewport mouse input.
	//-----------------------------------------------------------------------------
	void paintEvent( QPaintEvent * ) override
	{
		QPainter painter( this );
		m_pfnDraw( painter );
	}

private:
	std::function<void( QPainter & )> m_pfnDraw;
};
} // namespace
//-----------------------------------------------------------------------------
// Purpose: Uses the same solid GPU pipeline and software rasterizer as model particle previews.
//-----------------------------------------------------------------------------
CModelView::CModelView( QWidget *pParent )
	: QWidget( pParent )
{
	setObjectName( QStringLiteral( "modelView" ) );
	setMinimumSize( 240, 200 );
	setToolTip( tr( "Drag to orbit · Right-drag to pan · Wheel to zoom · Double-click to frame" ) );
	installEventFilter( this );
#ifdef VOMIT_TOOLS_GPU
	if ( CGraphics::Instance().IsGpu() )
	{
		auto pRenderer = std::make_unique<CModelRenderer>( [ this ]( QSize size ) {CModelRenderer::Frame_t frame;std::vector<particles::ScreenMesh_t> meshes;Scene(size,meshes,frame.m_Quads);frame.m_Camera=m_Camera;frame.m_pReference=m_pReference;frame.m_pPosed=m_pModel;frame.m_pMaterials=m_pMaterials;return frame; } );
		m_pGpu = new CGpuView( std::move( pRenderer ), this );
		m_pGpu->installEventFilter( this );
	}
#endif
	m_pLabels = new CModelLabels( this, [ this ]( QPainter &p ) {if(!m_bBones||!m_pModel)return;auto camera=m_Camera;camera.m_nWidth=width();camera.m_nHeight=height();p.setPen(QColor(255,210,100));for(const auto &bone:m_pModel->m_Bones)if(auto at=camera.Project(bone.m_ToModel.GetOrigin()))p.drawText(QPointF((*at)[0]+3,(*at)[1]-3),QString::fromStdString(bone.m_szName)); } );
}
//-----------------------------------------------------------------------------
// Purpose: Replaces the scene without resetting the user's camera.
//-----------------------------------------------------------------------------
void CModelView::SetModel( const model::CModel *pModel, const model::CStudioFile *pStudio, const std::vector<particles::Sprite_t> *pSprites )
{
	m_pModel = pModel;
	m_pStudio = pStudio;
	m_pSprites = pSprites;
	Redraw();
}
//-----------------------------------------------------------------------------
// Purpose: Changes inspection layers together to avoid redundant redraws.
//-----------------------------------------------------------------------------
void CModelView::SetOverlays( bool bWire, bool bNormals, bool bBones, bool bHitboxes, bool bAttachments, bool bBounds, bool bPositions, bool bGrid )
{
	m_bWire = bWire;
	m_bNormals = bNormals;
	m_bBones = bBones;
	m_bHitboxes = bHitboxes;
	m_bAttachments = bAttachments;
	m_bBounds = bBounds;
	m_bPositions = bPositions;
	m_bGrid = bGrid;
	Redraw();
}
//-----------------------------------------------------------------------------
// Purpose: Fits the model bounds, including animation-only models' declared view bounds.
//-----------------------------------------------------------------------------
void CModelView::Frame()
{
	CVec3 vecMin{}, vecMax{ 32, 32, 72 };
	if ( m_pStudio )
	{
		vecMin = m_pStudio->m_vecViewMin;
		vecMax = m_pStudio->m_vecViewMax;
	}
	if ( m_pModel && !m_pModel->m_Vertices.empty() )
	{
		vecMin = m_pModel->m_vecMin;
		vecMax = m_pModel->m_vecMax;
	}
	if ( m_pModel && m_pModel->m_Vertices.empty() && !m_pModel->m_Bones.empty() )
	{
		vecMin = vecMax = m_pModel->m_Bones[ 0 ].m_ToModel.GetOrigin();
		for ( const auto &bone : m_pModel->m_Bones )
		{
			const auto p = bone.m_ToModel.GetOrigin();
			vecMin = { std::min( vecMin.m_flX, p.m_flX ), std::min( vecMin.m_flY, p.m_flY ), std::min( vecMin.m_flZ, p.m_flZ ) };
			vecMax = { std::max( vecMax.m_flX, p.m_flX ), std::max( vecMax.m_flY, p.m_flY ), std::max( vecMax.m_flZ, p.m_flZ ) };
		}
	}

	m_Camera.m_vecTarget = ( vecMin + vecMax ) * 0.5f;
	m_Camera.m_flDistance = std::max( 10.0f, Length( vecMax - vecMin ) * 1.15f );
	m_Camera.m_flYaw = 135;
	m_Camera.m_flPitch = 15;
	Redraw();
}
//-----------------------------------------------------------------------------
// Purpose: Builds triangles and depth-tested line quads for either renderer.
//-----------------------------------------------------------------------------
void CModelView::Scene( QSize size, std::vector<particles::ScreenMesh_t> &meshes, std::vector<particles::ScreenQuad_t> &quads )
{
	m_Camera.m_nWidth = std::max( 1, size.width() );
	m_Camera.m_nHeight = std::max( 1, size.height() );
	if ( m_bGrid )
		quads = particles::GridQuads( m_Camera );
	const auto line = [ & ]( CVec3 a, CVec3 b, std::array<float, 3> color ) {
		const auto pa = m_Camera.Project( a ), pb = m_Camera.Project( b );
		if ( !pa || !pb )
			return;
		const auto &x = *pa, &y = *pb;
		const float dx = y[ 0 ] - x[ 0 ], dy = y[ 1 ] - x[ 1 ], length = std::hypot( dx, dy );
		if ( length < 0.01f )
			return;
		const float nx = -dy / length * 0.7f, ny = dx / length * 0.7f;
		particles::ScreenQuad_t q;
		q.m_Corners = { { { x[ 0 ] - nx, x[ 1 ] - ny, x[ 2 ] * 0.998f }, { x[ 0 ] + nx, x[ 1 ] + ny, x[ 2 ] * 0.998f }, { y[ 0 ] + nx, y[ 1 ] + ny, y[ 2 ] * 0.998f }, { y[ 0 ] - nx, y[ 1 ] - ny, y[ 2 ] * 0.998f } } };
		q.m_vecColor = color;
		// Inspection lines must remain visible through the shaded surface.
		for ( auto &corner : q.m_Corners )
			corner[ 2 ] = 1.001f;
		q.m_flDepth = 1.001f;
		quads.push_back( q );
	};
	const auto box = [ & ]( CVec3 vecMin, CVec3 vecMax, const model::CTransform &transform, std::array<float, 3> color ) {
		std::array<CVec3, 8> corners;
		for ( int i = 0; i < 8; i++ )
			corners[ i ] = transform.Apply( { i & 1 ? vecMax.m_flX : vecMin.m_flX, i & 2 ? vecMax.m_flY : vecMin.m_flY, i & 4 ? vecMax.m_flZ : vecMin.m_flZ } );
		for ( int i = 0; i < 8; i++ )
			for ( int j = 0; j < 3; j++ )
				if ( !( i & ( 1 << j ) ) )
					line( corners[ i ], corners[ i | ( 1 << j ) ], color );
	};
	if ( m_pModel )
	{
		meshes = particles::LayoutMesh( *m_pModel, {}, m_Camera, [ this ]( std::size_t i ) -> const particles::Sprite_t * { return m_pSprites && i < m_pSprites->size() ? &( *m_pSprites )[ i ] : nullptr; } );
		if ( m_pMaterials )
		{
			material::PreviewOptions_t options;
			options.m_bAmbientCube = true;
			options.m_nSize = std::max( size.width(), size.height() );
			m_Shading.clear();
			for ( const auto &entry : *m_pMaterials )
				m_Shading.push_back( material::ReadShading( entry.m_Material, entry.m_Textures, options ) );
			const auto vecForward = m_Camera.GetForward(), vecRight = Normalized( Cross( vecForward, { 0, 0, 1 } ) ), vecUp = Normalized( Cross( vecRight, vecForward ) );
			const auto view = [ & ]( CVec3 v ) { return CVec3{ Dot( v, vecRight ), Dot( v, vecUp ), -Dot( v, vecForward ) }; };
			const float flScale = 1.0f / std::max( 1.0f, m_pReference ? Length( m_pReference->m_vecMax - m_pReference->m_vecMin ) : 1 );
			for ( auto &mesh : meshes )
			{
				if ( m_pSprites && mesh.m_pSprite )
				{
					const auto nMaterial = mesh.m_pSprite - m_pSprites->data();
					if ( nMaterial >= 0 && nMaterial < static_cast<std::ptrdiff_t>( m_Shading.size() ) )
						mesh.m_pShading = &m_Shading[ nMaterial ];
				}
				mesh.m_Options = options;
				for ( std::size_t i = 0; i + 2 < mesh.m_Vertices.size(); i += 3 )
				{
					const auto &a = mesh.m_Vertices[ i ], &b = mesh.m_Vertices[ i + 1 ], &c = mesh.m_Vertices[ i + 2 ];
					const auto vecEdge = b.m_Surface.m_vecPosition - a.m_Surface.m_vecPosition, vecOther = c.m_Surface.m_vecPosition - a.m_Surface.m_vecPosition;
					const float du = b.m_Uv[ 0 ] - a.m_Uv[ 0 ], dv = b.m_Uv[ 1 ] - a.m_Uv[ 1 ], eu = c.m_Uv[ 0 ] - a.m_Uv[ 0 ], ev = c.m_Uv[ 1 ] - a.m_Uv[ 1 ], det = du * ev - eu * dv;
					const auto vecTangent = std::abs( det ) > 1e-8f ? Normalized( ( vecEdge * ev - vecOther * dv ) * ( 1.0f / det ) ) : Normalized( Cross( a.m_Surface.m_vecNormal, { 0, 0, 1 } ) );
					const auto vecBitangent = std::abs( det ) > 1e-8f ? Normalized( ( vecOther * du - vecEdge * eu ) * ( 1.0f / det ) ) : Normalized( Cross( a.m_Surface.m_vecNormal, vecTangent ) );
					for ( int k = 0; k < 3; k++ )
					{
						auto &v = mesh.m_Vertices[ i + k ].m_Surface;
						v.m_vecPosition = view( v.m_vecPosition - m_Camera.GetEye() ) * flScale + CVec3{ 0, 0, 3 };
						v.m_vecNormal = view( v.m_vecNormal );
						v.m_vecTangent = view( vecTangent );
						v.m_vecBitangent = view( vecBitangent );
					}
				}
			}
		}

		if ( m_bWire )
			for ( const auto &mesh : m_pModel->m_Meshes )
				for ( std::size_t i = 0; i + 2 < mesh.m_Indices.size(); i += 3 )
					for ( int j = 0; j < 3; j++ )
						line( m_pModel->m_Vertices[ mesh.m_Indices[ i + j ] ].m_vecPosition, m_pModel->m_Vertices[ mesh.m_Indices[ i + ( j + 1 ) % 3 ] ].m_vecPosition, { 0.2f, 0.9f, 0.85f } );
		if ( m_bNormals )
			for ( const auto &v : m_pModel->m_Vertices )
				line( v.m_vecPosition, v.m_vecPosition + v.m_vecNormal * 2, { 0.3f, 0.65f, 1 } );
		if ( m_bBones )
			for ( const auto &bone : m_pModel->m_Bones )
				if ( bone.m_nParent >= 0 && bone.m_nParent < static_cast<int>( m_pModel->m_Bones.size() ) )
					line( bone.m_ToModel.GetOrigin(), m_pModel->m_Bones[ bone.m_nParent ].m_ToModel.GetOrigin(), { 1, 0.75f, 0.2f } );
		if ( m_bHitboxes )
			for ( const auto &hit : m_pModel->m_Hitboxes )
				if ( hit.m_nBone >= 0 && hit.m_nBone < static_cast<int>( m_pModel->m_Bones.size() ) )
				{
					const float f = ( hit.m_nGroup % 7 ) / 7.0f;
					box( hit.m_vecMin, hit.m_vecMax, m_pModel->m_Bones[ hit.m_nBone ].m_ToModel, { 0.3f + f, 1 - f, 0.4f } );
				}
		if ( m_nHitbox >= 0 && m_nHitbox < static_cast<int>( m_pModel->m_Hitboxes.size() ) )
		{
			const auto &hit = m_pModel->m_Hitboxes[ static_cast<std::size_t>( m_nHitbox ) ];
			if ( hit.m_nBone >= 0 && hit.m_nBone < static_cast<int>( m_pModel->m_Bones.size() ) )
				box( hit.m_vecMin, hit.m_vecMax, m_pModel->m_Bones[ static_cast<std::size_t>( hit.m_nBone ) ].m_ToModel, { 1, 1, 1 } );
		}
		if ( m_nAttachment >= 0 && m_nAttachment < static_cast<int>( m_pModel->m_Attachments.size() ) )
		{
			const auto t = m_pModel->AttachmentToModel( static_cast<std::size_t>( m_nAttachment ) );
			const float flSize = std::max( 4.0f, Length( m_pModel->m_vecMax - m_pModel->m_vecMin ) * 0.08f );
			line( t.GetOrigin(), t.Apply( { flSize, 0, 0 } ), { 1, 0.3f, 0.3f } );
			line( t.GetOrigin(), t.Apply( { 0, flSize, 0 } ), { 0.3f, 1, 0.3f } );
			line( t.GetOrigin(), t.Apply( { 0, 0, flSize } ), { 0.3f, 0.6f, 1 } );
		}
		if ( m_bAttachments )
			for ( std::size_t i = 0; i < m_pModel->m_Attachments.size(); i++ )
			{
				const auto t = m_pModel->AttachmentToModel( i );
				line( t.GetOrigin(), t.Apply( { 5, 0, 0 } ), { 1, 0.2f, 0.2f } );
				line( t.GetOrigin(), t.Apply( { 0, 5, 0 } ), { 0.2f, 1, 0.2f } );
				line( t.GetOrigin(), t.Apply( { 0, 0, 5 } ), { 0.2f, 0.5f, 1 } );
			}
		if ( m_bBounds )
			box( m_pModel->m_vecMin, m_pModel->m_vecMax, {}, { 0.8f, 0.8f, 0.8f } );
	}
	if ( m_bPhysics && m_pPhysics )
		for ( const auto &convex : m_pPhysics->m_Convexes )
		{
			model::CTransform transform;
			if ( m_pModel && convex.m_nBone >= 0 && convex.m_nBone < static_cast<int>( m_pModel->m_Bones.size() ) )
				transform = m_pModel->m_Bones[ convex.m_nBone ].m_ToModel;
			for ( std::size_t i = 0; i + 2 < convex.m_Indices.size(); i += 3 )
				for ( int e = 0; e < 3; e++ )
					line( transform.Apply( convex.m_Vertices[ convex.m_Indices[ i + e ] ] ), transform.Apply( convex.m_Vertices[ convex.m_Indices[ i + ( e + 1 ) % 3 ] ] ), { 0.3f, 1, 0.4f } );
		}

	if ( m_pStudio )
	{
		if ( m_bBounds )
		{
			box( m_pStudio->m_vecHullMin, m_pStudio->m_vecHullMax, {}, { 1, 0.4f, 0.2f } );
			box( m_pStudio->m_vecViewMin, m_pStudio->m_vecViewMax, {}, { 0.3f, 0.5f, 1 } );
		}
		if ( m_bPositions )
			for ( const auto &p : { m_pStudio->m_vecEye, m_pStudio->m_vecIllumination } )
			{
				line( p - CVec3{ 2, 0, 0 }, p + CVec3{ 2, 0, 0 }, { 1, 1, 0.2f } );
				line( p - CVec3{ 0, 2, 0 }, p + CVec3{ 0, 2, 0 }, { 1, 1, 0.2f } );
				line( p - CVec3{ 0, 0, 2 }, p + CVec3{ 0, 0, 2 }, { 1, 1, 0.2f } );
			}
	}
}
//-----------------------------------------------------------------------------
// Purpose: Produces a software snapshot for export and regression comparisons.
//-----------------------------------------------------------------------------
QImage CModelView::GetImage( QSize size )
{
	std::vector<particles::ScreenMesh_t> meshes;
	std::vector<particles::ScreenQuad_t> quads;
	Scene( size, meshes, quads );
	texture::CImage image;
	particles::RenderScene( m_Camera, meshes, quads, image, { .m_bGrid = false, .m_pfnModelSprites = {} } );
	return ToQImage( image );
}
//-----------------------------------------------------------------------------
// Purpose: Paints the software fallback.
//-----------------------------------------------------------------------------
void CModelView::paintEvent( QPaintEvent * )
{
	if ( m_pGpu )
		return;
	QPainter p( this );
	p.drawImage( rect(), GetImage( size() ) );
}
//-----------------------------------------------------------------------------
// Purpose: Keeps the RHI child at the full viewport size.
//-----------------------------------------------------------------------------
void CModelView::resizeEvent( QResizeEvent * )
{
	if ( m_pGpu )
		m_pGpu->setGeometry( rect() );
	if ( m_pLabels )
	{
		m_pLabels->setGeometry( rect() );
		m_pLabels->raise();
	}
	Redraw();
}
//-----------------------------------------------------------------------------
// Purpose: Handles orbit, pan and zoom identically over either backend.
//-----------------------------------------------------------------------------
bool CModelView::eventFilter( QObject *, QEvent *pEvent )
{
	if ( pEvent->type() == QEvent::MouseButtonPress )
	{
		m_From = static_cast<QMouseEvent *>( pEvent )->position();
		return true;
	}
	if ( pEvent->type() == QEvent::MouseMove )
	{
		auto *p = static_cast<QMouseEvent *>( pEvent );
		const QPointF delta = p->position() - m_From;
		m_From = p->position();
		if ( p->buttons() & Qt::LeftButton )
		{
			m_Camera.m_flYaw -= delta.x() * 0.4f;
			m_Camera.m_flPitch = std::clamp( m_Camera.m_flPitch + static_cast<float>( delta.y() ) * 0.3f, -89.0f, 89.0f );
		}
		else if ( p->buttons() & Qt::RightButton )
		{
			const auto vecRight = Normalized( Cross( m_Camera.GetForward(), { 0, 0, 1 } ) );
			const auto vecUp = Normalized( Cross( vecRight, m_Camera.GetForward() ) );
			m_Camera.m_vecTarget += ( vecRight * static_cast<float>( -delta.x() ) + vecUp * static_cast<float>( delta.y() ) ) * ( m_Camera.m_flDistance / std::max( 1, height() ) );
		}
		Redraw();
		return true;
	}
	if ( pEvent->type() == QEvent::Wheel )
	{
		auto *p = static_cast<QWheelEvent *>( pEvent );
		m_Camera.m_flDistance = std::clamp( m_Camera.m_flDistance * std::pow( 0.999f, static_cast<float>( p->angleDelta().y() ) ), 0.1f, 100000.0f );
		Redraw();
		return true;
	}
	if ( pEvent->type() == QEvent::MouseButtonDblClick )
	{
		Frame();
		return true;
	}
	return false;
}
//-----------------------------------------------------------------------------
// Purpose: Requests a frame from the selected drawing backend.
//-----------------------------------------------------------------------------
void CModelView::Redraw()
{
	if ( m_pLabels )
		m_pLabels->update();
	if ( m_pGpu )
		m_pGpu->update();
	else
		update();
}

//-----------------------------------------------------------------------------
// Purpose: The hitbox or attachment an edit panel has picked.
//-----------------------------------------------------------------------------
void CModelView::SetHighlight( int nHitbox, int nAttachment )
{
	if ( nHitbox == m_nHitbox && nAttachment == m_nAttachment )
		return;
	m_nHitbox = nHitbox;
	m_nAttachment = nAttachment;
	Redraw();
}
//-----------------------------------------------------------------------------
// Purpose: Selects the collision hull inspection layer.
//-----------------------------------------------------------------------------
void CModelView::SetPhysics( const model::Physics_t *pPhysics, bool bShow )
{
	m_pPhysics = pPhysics;
	m_bPhysics = bShow;
	Redraw();
}
//-----------------------------------------------------------------------------
// Purpose: Clears GPU texture uploads before a different skin replaces their backing images.
//-----------------------------------------------------------------------------
void CModelView::ForgetTextures()
{
#ifdef VOMIT_TOOLS_GPU
	if ( auto *pView = dynamic_cast<CGpuView *>( m_pGpu ) )
		static_cast<CModelRenderer &>( pView->GetRenderer() ).ForgetTextures();
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Supplies the reference mesh and full material textures to both drawing backends.
//-----------------------------------------------------------------------------
void CModelView::SetResources( const model::CModel *pReference, const std::vector<ModelMaterial_t> *pMaterials )
{
	m_pReference = pReference;
	m_pMaterials = pMaterials;
}

//-----------------------------------------------------------------------------
// Purpose: Draws the same scene into a GPU image for backend comparisons and screenshots.
//-----------------------------------------------------------------------------
QImage CModelView::GetGpuImage( QSize size )
{
#ifdef VOMIT_TOOLS_GPU
	auto pRenderer = std::make_unique<CModelRenderer>( [ this ]( QSize targetSize ) {CModelRenderer::Frame_t frame;std::vector<particles::ScreenMesh_t> meshes;Scene(targetSize,meshes,frame.m_Quads);frame.m_Camera=m_Camera;frame.m_pReference=m_pReference;frame.m_pPosed=m_pModel;frame.m_pMaterials=m_pMaterials;return frame; } );
	auto pGpu = COffscreenGpu::Create( std::move( pRenderer ) );
	if ( pGpu )
		return pGpu->Render( size );
#endif
	return {};
}
