//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See DetailPreview.h.
//
//===========================================================================//
#include "DetailPreview.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "DebugLog.h"
#include "ImageFiles.h"

using namespace xv;
using xv::detail::NodePath_t;

namespace
{

// Units a texel of a brush texture covers at the usual texture scale.
constexpr float TEXEL_UNITS = 0.25f;

//-----------------------------------------------------------------------------
// Purpose: A picture as a sprite, with its mipmaps.
//-----------------------------------------------------------------------------
particles::Sprite_t SpriteFrom( const QImage &image )
{
	particles::Sprite_t sprite = particles::FallbackSprite();
	if ( image.isNull() )
	{
		return sprite;
	}
	sprite.m_Mips.clear();
	QImage level = image.convertToFormat( QImage::Format_RGBA8888 );
	while ( true )
	{
		sprite.m_Mips.push_back( FromQImage( level ) );
		if ( level.width() <= 4 || level.height() <= 4 )
		{
			break;
		}
		level = level.scaled( level.width() / 2, level.height() / 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation );
	}
	return sprite;
}

//-----------------------------------------------------------------------------
// Purpose: A number of an entry's key, or a fallback.
//-----------------------------------------------------------------------------
std::vector<double> NumbersOf( const kv1::CNode &entry, std::string_view szKey )
{
	const kv1::CNode *pNode = entry.Find( szKey );
	return pNode && pNode->IsPair() ? detail::Numbers( pNode->GetValue() ) : std::vector<double>{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string TextOf( const kv1::CNode &entry, std::string_view szKey )
{
	const kv1::CNode *pNode = entry.Find( szKey );
	return pNode && pNode->IsPair() ? pNode->GetValue() : std::string{};
}

} // namespace

// The picture, and the mouse turning the camera around the ground.
class CDetailView : public QWidget
{
public:
	explicit CDetailView( CDetailPreview *pPreview )
		: m_pPreview( pPreview )
	{
		setMinimumSize( 240, 200 );
		setObjectName( QStringLiteral( "detailPreview3d" ) );
		setToolTip( tr( "Drag to turn around it, right-drag to move, the wheel zooms, a double-click goes back" ) );
	}

protected:
	void paintEvent( QPaintEvent * ) override
	{
		QPainter painter( this );
		painter.fillRect( rect(), QColor( 33, 36, 38 ) );
		painter.drawImage( rect(), m_pPreview->m_Image );
	}
	void resizeEvent( QResizeEvent * ) override { m_pPreview->Redraw(); }
	void mousePressEvent( QMouseEvent *pEvent ) override { m_From = pEvent->position(); }
	void mouseMoveEvent( QMouseEvent *pEvent ) override
	{
		particles::CCamera &camera = m_pPreview->m_Camera;
		const QPointF delta = pEvent->position() - m_From;
		m_From = pEvent->position();
		if ( pEvent->buttons() & Qt::LeftButton )
		{
			camera.m_flYaw -= static_cast<float>( delta.x() ) * 0.4f;
			camera.m_flPitch = std::clamp( camera.m_flPitch + static_cast<float>( delta.y() ) * 0.3f, 2.0f, 89.0f );
		}
		else if ( pEvent->buttons() & Qt::RightButton )
		{
			const CVec3 vecForward = camera.GetForward();
			const CVec3 vecRight = Normalized( Cross( vecForward, { 0, 0, 1 } ) );
			const CVec3 vecAhead = Normalized( CVec3{ vecForward.m_flX, vecForward.m_flY, 0.0f } );
			const float flUnits = camera.m_flDistance / std::max( 1, height() );
			camera.m_vecTarget += ( vecRight * -static_cast<float>( delta.x() ) + vecAhead * static_cast<float>( delta.y() ) ) * flUnits;
		}
		else
		{
			return;
		}
		m_pPreview->Redraw();
	}
	void wheelEvent( QWheelEvent *pEvent ) override
	{
		particles::CCamera &camera = m_pPreview->m_Camera;
		camera.m_flDistance = std::clamp( camera.m_flDistance * ( pEvent->angleDelta().y() > 0 ? 0.85f : 1.18f ), 20.0f, 8000.0f );
		m_pPreview->Redraw();
	}
	void mouseDoubleClickEvent( QMouseEvent * ) override
	{
		particles::CCamera &camera = m_pPreview->m_Camera;
		camera.m_vecTarget = {};
		camera.m_flYaw = 135.0f;
		camera.m_flPitch = 25.0f;
		camera.m_flDistance = 520.0f;
		m_pPreview->Redraw();
	}

private:
	CDetailPreview *m_pPreview;
	QPointF m_From;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CDetailPreview::CDetailPreview( QWidget *pParent )
	: QWidget( pParent )
{
	m_Camera.m_flYaw = 135.0f;
	m_Camera.m_flPitch = 25.0f;
	m_Camera.m_flDistance = 520.0f;
	m_Sheet = particles::FallbackSprite();
	m_pView = new CDetailView( this );

	m_pSeed = new QSpinBox;
	m_pSeed->setObjectName( QStringLiteral( "detailSeed" ) );
	m_pSeed->setRange( 1, 99999 );
	m_pSeed->setPrefix( tr( "seed " ) );
	m_pSeed->setToolTip( tr( "Which random placement; the map compiler's own differs" ) );
	auto *pReroll = new QPushButton( tr( "Scatter Again" ) );
	m_pSize = new QComboBox;
	m_pSize->setObjectName( QStringLiteral( "detailGroundSize" ) );
	for ( const int nSize : { 256, 512, 1024, 2048 } )
	{
		m_pSize->addItem( tr( "%1 units" ).arg( nSize ), nSize );
	}
	m_pSize->setCurrentIndex( 1 );
	m_pSize->setToolTip( tr( "The ground's side" ) );
	m_pBlend = new QComboBox;
	m_pBlend->setObjectName( QStringLiteral( "detailBlend" ) );
	m_pBlend->addItem( tr( "Blend 0 → 1, left to right" ) );
	m_pBlend->addItem( tr( "Blend 0 everywhere" ) );
	m_pBlend->addItem( tr( "Blend 0.5 everywhere" ) );
	m_pBlend->addItem( tr( "Blend 1 everywhere" ) );
	m_pBlend->setToolTip( tr( "The blend alpha painted on a displacement, which picks the group (each group's alpha)" ) );
	auto *pControls = new QHBoxLayout;
	pControls->addWidget( m_pSeed );
	pControls->addWidget( pReroll );
	pControls->addWidget( m_pSize );
	pControls->addWidget( m_pBlend, 1 );
	m_pNotes = new QLabel;
	m_pNotes->setObjectName( QStringLiteral( "detailPreviewNotes" ) );
	m_pNotes->setWordWrap( true );
	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->addWidget( m_pView, 1 );
	pLayout->addLayout( pControls );
	pLayout->addWidget( m_pNotes );

	connect( m_pSeed, &QSpinBox::valueChanged, this, &CDetailPreview::Redraw );
	connect( pReroll, &QPushButton::clicked, this, [ this ] { m_pSeed->setValue( m_pSeed->value() % 99999 + 1 ); } );
	connect( m_pSize, &QComboBox::currentIndexChanged, this, &CDetailPreview::Redraw );
	connect( m_pBlend, &QComboBox::currentIndexChanged, this, &CDetailPreview::Redraw );
}

//-----------------------------------------------------------------------------
// Purpose: What is drawn: the file, the type and the entry picked.
//-----------------------------------------------------------------------------
void CDetailPreview::SetScene( const kv1::CDocument *pDocument, NodePath_t type, NodePath_t selected )
{
	m_pDocument = pDocument;
	m_Type = std::move( type );
	m_Selected = std::move( selected );
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDetailPreview::SetSheet( const QImage &sheet )
{
	m_Sheet = SpriteFrom( sheet );
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: Models and materials read again with the new reader.
//-----------------------------------------------------------------------------
void CDetailPreview::SetReader( material::Reader_t pfnRead )
{
	m_pfnRead = std::move( pfnRead );
	m_Models.clear();
	m_Ground.reset();
	const QString szGround = m_szGround;
	m_szGround.clear();
	SetGround( szGround );
}

//-----------------------------------------------------------------------------
// Purpose: The material the ground is drawn in.
//-----------------------------------------------------------------------------
void CDetailPreview::SetGround( const QString &szMaterial )
{
	if ( szMaterial == m_szGround && m_Ground )
	{
		return;
	}
	m_szGround = szMaterial;
	m_Ground.reset();
	if ( !szMaterial.isEmpty() && m_pfnRead )
	{
		m_Ground = particles::LoadSprite( szMaterial.toStdString(), m_pfnRead );
		if ( !m_Ground->m_szProblem.empty() )
		{
			m_Ground.reset();
		}
	}
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: The ground's texture, for a thumbnail.
//-----------------------------------------------------------------------------
QImage CDetailPreview::GetGroundPicture() const
{
	return m_Ground && !m_Ground->m_Mips.empty() ? ToQImage( m_Ground->m_Mips.front() ) : QImage{};
}

//-----------------------------------------------------------------------------
// Purpose: A model and its materials, read once.
//-----------------------------------------------------------------------------
const CDetailPreview::Model_t &CDetailPreview::ModelFor( const std::string &szPath )
{
	const auto found = m_Models.find( szPath );
	if ( found != m_Models.end() )
	{
		return found->second;
	}
	Model_t loaded;
	if ( m_pfnRead )
	{
		loaded.m_Model = model::LoadModel( szPath, m_pfnRead );
		for ( const std::string &szMaterial : loaded.m_Model ? loaded.m_Model->m_Materials : std::vector<std::string>{} )
		{
			loaded.m_Sprites.push_back( std::make_shared<particles::Sprite_t>( particles::LoadSprite( szMaterial, m_pfnRead ) ) );
		}
	}
	return m_Models.emplace( szPath, std::move( loaded ) ).first->second;
}

//-----------------------------------------------------------------------------
// Purpose: Draws at the next turn of the event loop, once however many ask.
//-----------------------------------------------------------------------------
void CDetailPreview::Redraw()
{
	if ( m_bQueued || !isVisible() )
	{
		return;
	}
	m_bQueued = true;
	QTimer::singleShot( 0, this, [ this ] {
		m_bQueued = false;
		Render();
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDetailPreview::showEvent( QShowEvent *pEvent )
{
	QWidget::showEvent( pEvent );
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: Draws now.
//-----------------------------------------------------------------------------
void CDetailPreview::RenderNow()
{
	m_bQueued = false;
	Render();
}

//-----------------------------------------------------------------------------
// Purpose: The ground, then models and sprites, from the camera.
//-----------------------------------------------------------------------------
void CDetailPreview::Render()
{
	CDebugTimer timer( QStringLiteral( "drawing the 3D detail preview" ) );
	const qreal flRatio = m_pView->devicePixelRatioF();
	m_Camera.m_nWidth = std::max( 64, static_cast<int>( m_pView->width() * flRatio ) );
	m_Camera.m_nHeight = std::max( 64, static_cast<int>( m_pView->height() * flRatio ) );
	detail::ScatterOptions_t options;
	options.m_flSize = static_cast<float>( m_pSize->currentData().toInt() );
	options.m_nSeed = static_cast<std::uint32_t>( m_pSeed->value() );
	static constexpr float FIXED_ALPHAS[] = { 0.0f, 0.0f, 0.5f, 1.0f };
	const int nBlend = std::max( 0, m_pBlend->currentIndex() );
	options.m_flAlphaLow = nBlend == 0 ? 0.0f : FIXED_ALPHAS[ nBlend ];
	options.m_flAlphaHigh = nBlend == 0 ? 1.0f : FIXED_ALPHAS[ nBlend ];

	const CVec3 vecForward = m_Camera.GetForward();
	const CVec3 vecRight = Normalized( Cross( vecForward, { 0, 0, 1 } ) );
	const CVec3 vecUp = Cross( vecRight, vecForward );
	std::vector<particles::ScreenMesh_t> meshes;

	// The ground: a grid of squares so the near ones aren't clipped away.
	{
		particles::ScreenMesh_t ground{ m_Ground ? &*m_Ground : nullptr, {} };
		const float flHalf = options.m_flSize * 0.5f;
		const float flTexture = m_Ground && !m_Ground->m_Mips.empty() ? static_cast<float>( m_Ground->m_Mips.front().m_nWidth ) * TEXEL_UNITS : 128.0f;
		constexpr int CELLS = 24;
		const auto vertex = [ & ]( int i, int j ) -> std::optional<particles::ScreenVertex_t> {
			const float x = -flHalf + options.m_flSize * static_cast<float>( i ) / CELLS;
			const float y = -flHalf + options.m_flSize * static_cast<float>( j ) / CELLS;
			const auto at = m_Camera.Project( { x, y, 0.0f } );
			if ( !at )
			{
				return std::nullopt;
			}
			// Brighter where the blend is higher, so the ramp shows on any ground.
			const float flShade = 0.75f + 0.25f * detail::BlendAlphaAt( x, options );
			return particles::ScreenVertex_t{ *at, { x / flTexture, -y / flTexture }, { flShade, flShade, flShade } };
		};
		for ( int i = 0; i < CELLS; i++ )
		{
			for ( int j = 0; j < CELLS; j++ )
			{
				const auto a = vertex( i, j ), b = vertex( i + 1, j ), c = vertex( i + 1, j + 1 ), d = vertex( i, j + 1 );
				if ( a && b && c && d )
				{
					ground.m_Vertices.insert( ground.m_Vertices.end(), { *a, *b, *c, *a, *c, *d } );
				}
			}
		}
		meshes.push_back( std::move( ground ) );
	}

	const auto types = m_pDocument ? detail::Types( *m_pDocument ) : std::vector<detail::Type_t>{};
	const auto type = std::ranges::find( types, m_Type, &detail::Type_t::m_Path );
	std::vector<detail::Placed_t> placed;
	if ( type != types.end() )
	{
		placed = detail::Scatter( *m_pDocument, *type, options );
	}
	m_nPlaced = placed.size();

	std::vector<particles::ScreenQuad_t> quads;
	int nMissingModels = 0;
	const auto isPicked = [ this ]( const NodePath_t &entry ) { return !m_Selected.empty() && m_Selected == entry; };
	for ( const detail::Placed_t &prop : placed )
	{
		const kv1::CNode *pEntry = detail::Node( *m_pDocument, prop.m_Entry );
		if ( !pEntry )
		{
			continue;
		}
		const float flYaw = prop.m_flYaw * std::numbers::pi_v<float> / 180.0f;
		if ( const std::string szModel = TextOf( *pEntry, "model" ); !szModel.empty() )
		{
			const Model_t &loaded = ModelFor( szModel );
			if ( !loaded.m_Model )
			{
				nMissingModels++;
				continue;
			}
			const model::CTransform toWorld = model::CTransform::Translation( prop.m_vecOrigin ) * model::CTransform::Rotation( 0.0f, prop.m_flYaw, 0.0f );
			for ( particles::ScreenMesh_t &mesh : particles::LayoutMesh( *loaded.m_Model, toWorld, m_Camera, [ &loaded ]( std::size_t i ) { return i < loaded.m_Sprites.size() ? loaded.m_Sprites[ i ].get() : nullptr; } ) )
			{
				if ( isPicked( prop.m_Entry ) )
				{
					for ( particles::ScreenVertex_t &vertex : mesh.m_Vertices )
					{
						vertex.m_vecColor = { vertex.m_vecColor[ 0 ] * 1.2f, vertex.m_vecColor[ 1 ] * 0.95f, vertex.m_vecColor[ 2 ] * 0.45f };
					}
				}
				meshes.push_back( std::move( mesh ) );
			}
			continue;
		}
		// A sprite: its part of the sheet (in the sheet's width), placed by its origin and size.
		const auto rect = NumbersOf( *pEntry, "sprite" );
		const auto size = NumbersOf( *pEntry, "spritesize" );
		if ( rect.size() != 5 || rect[ 4 ] <= 0.0 || size.size() != 4 )
		{
			continue;
		}
		// The sheet's coordinates run 0 to its width both ways, whatever its height.
		const std::array<float, 4> uv{ static_cast<float>( rect[ 0 ] / rect[ 4 ] ), static_cast<float>( rect[ 1 ] / rect[ 4 ] ), static_cast<float>( ( rect[ 0 ] + rect[ 2 ] ) / rect[ 4 ] ),
			static_cast<float>( ( rect[ 1 ] + rect[ 3 ] ) / rect[ 4 ] ) };
		const float flWidth = static_cast<float>( size[ 2 ] ) * prop.m_flScale;
		const float flHeight = static_cast<float>( size[ 3 ] ) * prop.m_flScale;
		const float flLeft = -static_cast<float>( size[ 0 ] ) * flWidth;
		const float flRight = flLeft + flWidth;
		const float flBottom = -static_cast<float>( size[ 1 ] ) * flHeight;
		const float flTop = flBottom + flHeight;
		const auto addQuad = [ & ]( CVec3 vecAcross, CVec3 vecUpward, CVec3 vecOffset ) {
			const CVec3 vecOrigin = prop.m_vecOrigin + vecOffset;
			const CVec3 corners[ 4 ] = { vecOrigin + vecAcross * flLeft + vecUpward * flTop, vecOrigin + vecAcross * flRight + vecUpward * flTop,
				vecOrigin + vecAcross * flRight + vecUpward * flBottom, vecOrigin + vecAcross * flLeft + vecUpward * flBottom };
			particles::ScreenQuad_t quad;
			for ( std::size_t c = 0; c < 4; c++ )
			{
				const auto at = m_Camera.Project( corners[ c ] );
				if ( !at )
				{
					return;
				}
				quad.m_Corners[ c ] = *at;
			}
			const auto centre = m_Camera.Project( vecOrigin + vecUpward * ( ( flTop + flBottom ) * 0.5f ) );
			quad.m_flDepth = centre ? ( *centre )[ 2 ] : quad.m_Corners[ 0 ][ 2 ];
			quad.m_flPixels = std::abs( quad.m_Corners[ 1 ][ 0 ] - quad.m_Corners[ 0 ][ 0 ] ) + std::abs( quad.m_Corners[ 3 ][ 1 ] - quad.m_Corners[ 0 ][ 1 ] );
			quad.m_Rect = uv;
			quad.m_pSprite = &m_Sheet;
			if ( isPicked( prop.m_Entry ) )
			{
				quad.m_vecColor = { 1.0f, 0.82f, 0.4f };
			}
			quads.push_back( quad );
		};
		const CVec3 vecTurned{ std::cos( flYaw ), std::sin( flYaw ), 0.0f };
		const std::string szShape = TextOf( *pEntry, "sprite_shape" );
		if ( szShape == "cross" || szShape == "tri" )
		{
			// Fixed shapes standing on the ground, turned at random.
			const int nSides = szShape == "cross" ? 2 : 3;
			const auto spacing = NumbersOf( *pEntry, "shape_size" );
			const float flOut = spacing.size() == 1 ? static_cast<float>( spacing.front() ) * flWidth : 0.0f;
			for ( int s = 0; s < nSides; s++ )
			{
				const float flAngle = flYaw + std::numbers::pi_v<float> * static_cast<float>( s ) * ( nSides == 2 ? 0.5f : 2.0f / 3.0f );
				const CVec3 vecAcross{ std::cos( flAngle ), std::sin( flAngle ), 0.0f };
				const CVec3 vecOut{ -vecAcross.m_flY, vecAcross.m_flX, 0.0f };
				addQuad( vecAcross, { 0, 0, 1 }, nSides == 3 ? vecOut * flOut : CVec3{} );
			}
			continue;
		}
		const auto orientation = NumbersOf( *pEntry, "detailOrientation" );
		const int nOrientation = orientation.size() == 1 ? static_cast<int>( orientation.front() ) : 0;
		if ( nOrientation == 1 )
		{
			addQuad( vecRight, vecUp, {} ); // faces the camera
		}
		else if ( nOrientation == 2 )
		{
			addQuad( vecRight, { 0, 0, 1 }, {} ); // faces the camera, upright
		}
		else
		{
			addQuad( vecTurned, { 0, 0, 1 }, {} ); // fixed, turned at random
		}
	}
	// Far ones first, so near ones cover them.
	std::ranges::sort( quads, std::greater{}, &particles::ScreenQuad_t::m_flDepth );

	texture::CImage out;
	particles::RenderScene( m_Camera, meshes, quads, out, { .m_bGrid = false, .m_pfnModelSprites = {}, .m_vecBackground = { 0.36f, 0.45f, 0.55f } } );
	m_Image = ToQImage( out );
	m_Image.setDevicePixelRatio( flRatio );
	QStringList notes;
	notes << tr( "%n prop(s) on %1×%1 units", nullptr, static_cast<int>( m_nPlaced ) ).arg( options.m_flSize );
	if ( !m_Ground )
	{
		notes << ( m_szGround.isEmpty() ? tr( "pick a ground material on the left" ) : tr( "%1 can't be read; plain ground" ).arg( m_szGround ) );
	}
	if ( nMissingModels > 0 )
	{
		notes << tr( "%n model(s) can't be read", nullptr, nMissingModels );
	}
	m_pNotes->setText( notes.join( QStringLiteral( " · " ) ) );
	m_pView->update();
}
