//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Render.h.
//
//===========================================================================//
#include "Render.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <cstring>
#include <numbers>
#include <thread>

#include "kv1/Kv1.h"
#include "texture/Vtf.h"
#include "vfs/Vfs.h"

namespace xv::particles
{

namespace
{

constexpr float DEGREES = std::numbers::pi_v<float> / 180.0f;
constexpr float NEAR = 1.0f;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
template <typename T>
T ReadAt( std::span<const std::byte> data, std::size_t nOffset )
{
	T value;
	std::memcpy( &value, data.data() + nOffset, sizeof value );
	return value;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string Lower( std::string_view szText )
{
	std::string szOut{ szText };
	std::ranges::transform( szOut, szOut.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
	return szOut;
}

// A four-sided shape on screen, textured by rect. Its corners, in order, show the texture's
// top-left, top-right, bottom-right and bottom-left; between them the texture follows the
// perspective, so a sprite lying on the ground looks like a decal, not a skewed parallelogram.
struct CQuad
{
	std::array<std::array<float, 2>, 4> m_Corners{};
	double m_Inverse[ 9 ]{}; // screen (x, y, 1) -> texture (a, b, w), divided by w
	int m_nMinX = 0, m_nMinY = 0, m_nMaxX = -1, m_nMaxY = -1;
	std::array<float, 4> m_Rect{ 0, 0, 1, 1 };
	std::array<float, 3> m_vecColor{ 1, 1, 1 }; // tint, overbright included
	float m_flAlpha = 1.0f;
	float m_flDepth = 0.0f;
	const texture::CImage *m_pImage = nullptr;
	Sprite_t::Blend_t m_nBlend = Sprite_t::Blend_t::BLEND_ALPHA;
	float m_flAddSelf = 0.0f;

	// Works out the mapping from screen to texture and the screen area covered; false when there is
	// nothing to draw.
	bool Finish( int nWidth, int nHeight )
	{
		if ( m_flAlpha <= 0.0f )
		{
			return false;
		}
		// The unit square to the four corners (Heckbert's projective mapping), then inverted.
		const double flX0 = m_Corners[ 0 ][ 0 ], flY0 = m_Corners[ 0 ][ 1 ];
		const double flX1 = m_Corners[ 1 ][ 0 ], flY1 = m_Corners[ 1 ][ 1 ];
		const double flX2 = m_Corners[ 2 ][ 0 ], flY2 = m_Corners[ 2 ][ 1 ];
		const double flX3 = m_Corners[ 3 ][ 0 ], flY3 = m_Corners[ 3 ][ 1 ];
		const double flSx = flX0 - flX1 + flX2 - flX3;
		const double flSy = flY0 - flY1 + flY2 - flY3;
		double a = 0, b = 0, c = flX0, d = 0, e = 0, f = flY0, g = 0, h = 0;
		if ( std::abs( flSx ) < 1e-9 && std::abs( flSy ) < 1e-9 )
		{
			a = flX1 - flX0;
			b = flX3 - flX0;
			d = flY1 - flY0;
			e = flY3 - flY0;
		}
		else
		{
			const double flDx1 = flX1 - flX2, flDx2 = flX3 - flX2;
			const double flDy1 = flY1 - flY2, flDy2 = flY3 - flY2;
			const double flDen = flDx1 * flDy2 - flDx2 * flDy1;
			if ( std::abs( flDen ) < 1e-12 )
			{
				return false;
			}
			g = ( flSx * flDy2 - flDx2 * flSy ) / flDen;
			h = ( flDx1 * flSy - flSx * flDy1 ) / flDen;
			a = flX1 - flX0 + g * flX1;
			b = flX3 - flX0 + h * flX3;
			d = flY1 - flY0 + g * flY1;
			e = flY3 - flY0 + h * flY3;
		}
		// Inverse of [[a b c] [d e f] [g h 1]] by its adjugate (the scale doesn't matter).
		const double m[ 9 ]{ e - f * h, c * h - b, b * f - c * e,
			f * g - d, a - c * g, c * d - a * f,
			d * h - e * g, b * g - a * h, a * e - b * d };
		if ( std::abs( a * m[ 0 ] + b * m[ 3 ] + c * m[ 6 ] ) < 1e-9 )
		{
			return false;
		}
		std::copy( m, m + 9, m_Inverse );
		float flLowX = m_Corners[ 0 ][ 0 ], flHighX = flLowX, flLowY = m_Corners[ 0 ][ 1 ], flHighY = flLowY;
		for ( const auto &corner : m_Corners )
		{
			flLowX = std::min( flLowX, corner[ 0 ] );
			flHighX = std::max( flHighX, corner[ 0 ] );
			flLowY = std::min( flLowY, corner[ 1 ] );
			flHighY = std::max( flHighY, corner[ 1 ] );
		}
		m_nMinX = std::max( 0, static_cast<int>( std::floor( flLowX ) ) );
		m_nMaxX = std::min( nWidth - 1, static_cast<int>( std::ceil( flHighX ) ) );
		m_nMinY = std::max( 0, static_cast<int>( std::floor( flLowY ) ) );
		m_nMaxY = std::min( nHeight - 1, static_cast<int>( std::ceil( flHighY ) ) );
		return m_nMinX <= m_nMaxX && m_nMinY <= m_nMaxY;
	}
};

//-----------------------------------------------------------------------------
// Purpose: Corners from a centre and the screen vectors along the texture's width and height, all
//          at the
//          centre's depth.
//-----------------------------------------------------------------------------
void Around( ScreenQuad_t &quad, std::array<float, 3> vecCenter, float flUx, float flUy, float flVx, float flVy )
{
	const float x = vecCenter[ 0 ];
	const float y = vecCenter[ 1 ];
	const float d = vecCenter[ 2 ];
	quad.m_Corners = { { { x - ( flUx + flVx ) * 0.5f, y - ( flUy + flVy ) * 0.5f, d },
		{ x + ( flUx - flVx ) * 0.5f, y + ( flUy - flVy ) * 0.5f, d },
		{ x + ( flUx + flVx ) * 0.5f, y + ( flUy + flVy ) * 0.5f, d },
		{ x - ( flUx - flVx ) * 0.5f, y - ( flUy - flVy ) * 0.5f, d } } };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool OnScreen( const ScreenQuad_t &quad, const CCamera &camera )
{
	float flLowX = quad.m_Corners[ 0 ][ 0 ], flHighX = flLowX, flLowY = quad.m_Corners[ 0 ][ 1 ], flHighY = flLowY;
	for ( const auto &vecCorner : quad.m_Corners )
	{
		flLowX = std::min( flLowX, vecCorner[ 0 ] );
		flHighX = std::max( flHighX, vecCorner[ 0 ] );
		flLowY = std::min( flLowY, vecCorner[ 1 ] );
		flHighY = std::max( flHighY, vecCorner[ 1 ] );
	}
	return flHighX >= 0.0f && flHighY >= 0.0f && flLowX < static_cast<float>( camera.m_nWidth ) && flLowY < static_cast<float>( camera.m_nHeight );
}

//-----------------------------------------------------------------------------
// Purpose: Bilinear, clamped at the edges; RGBA 0-1.
//-----------------------------------------------------------------------------
std::array<float, 4> Sample( const texture::CImage &image, float x, float y )
{
	x = std::clamp( x * static_cast<float>( image.m_nWidth ) - 0.5f, 0.0f, static_cast<float>( image.m_nWidth - 1 ) );
	y = std::clamp( y * static_cast<float>( image.m_nHeight ) - 0.5f, 0.0f, static_cast<float>( image.m_nHeight - 1 ) );
	const int nX0 = static_cast<int>( x );
	const int nY0 = static_cast<int>( y );
	const int nX1 = std::min( nX0 + 1, image.m_nWidth - 1 );
	const int nY1 = std::min( nY0 + 1, image.m_nHeight - 1 );
	const float flFx = x - static_cast<float>( nX0 );
	const float flFy = y - static_cast<float>( nY0 );
	const std::uint8_t *a = image.Pixel( nX0, nY0 );
	const std::uint8_t *b = image.Pixel( nX1, nY0 );
	const std::uint8_t *c = image.Pixel( nX0, nY1 );
	const std::uint8_t *d = image.Pixel( nX1, nY1 );
	std::array<float, 4> out{};
	for ( int i = 0; i < 4; i++ )
	{
		const float flTop = static_cast<float>( a[ i ] ) + ( static_cast<float>( b[ i ] ) - static_cast<float>( a[ i ] ) ) * flFx;
		const float flBottom = static_cast<float>( c[ i ] ) + ( static_cast<float>( d[ i ] ) - static_cast<float>( c[ i ] ) ) * flFx;
		out[ static_cast<std::size_t>( i ) ] = ( flTop + ( flBottom - flTop ) * flFy ) / 255.0f;
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void Fill( const CQuad &quad, std::vector<float> &buffer, const std::vector<float> &depth, int nWidth, int nRowStart, int nRowEnd )
{
	const int nY0 = std::max( quad.m_nMinY, nRowStart );
	const int nY1 = std::min( quad.m_nMaxY, nRowEnd - 1 );
	const float flRw = quad.m_Rect[ 2 ] - quad.m_Rect[ 0 ];
	const float flRh = quad.m_Rect[ 3 ] - quad.m_Rect[ 1 ];
	const double *m = quad.m_Inverse;
	for ( int y = nY0; y <= nY1; y++ )
	{
		const double flPy = static_cast<double>( y ) + 0.5;
		for ( int x = quad.m_nMinX; x <= quad.m_nMaxX; x++ )
		{
			// Behind the model (when there is one, depth holds how far it is at each pixel).
			if ( !depth.empty() && quad.m_flDepth > depth[ static_cast<std::size_t>( y ) * static_cast<std::size_t>( nWidth ) + static_cast<std::size_t>( x ) ] )
			{
				continue;
			}
			const double flPx = static_cast<double>( x ) + 0.5;
			const double w = m[ 6 ] * flPx + m[ 7 ] * flPy + m[ 8 ];
			if ( w == 0.0 )
			{
				continue;
			}
			const auto a = static_cast<float>( ( m[ 0 ] * flPx + m[ 1 ] * flPy + m[ 2 ] ) / w );
			const auto b = static_cast<float>( ( m[ 3 ] * flPx + m[ 4 ] * flPy + m[ 5 ] ) / w );
			if ( a < 0.0f || a > 1.0f || b < 0.0f || b > 1.0f )
			{
				continue;
			}
			const auto texel = Sample( *quad.m_pImage, quad.m_Rect[ 0 ] + a * flRw, quad.m_Rect[ 1 ] + b * flRh );
			const float flAlpha = texel[ 3 ] * quad.m_flAlpha;
			if ( flAlpha <= 0.0f )
			{
				continue;
			}
			float *pOut = buffer.data() + ( static_cast<std::size_t>( y ) * static_cast<std::size_t>( nWidth ) + static_cast<std::size_t>( x ) ) * 3;
			for ( int c = 0; c < 3; c++ )
			{
				const float flSrc = texel[ static_cast<std::size_t>( c ) ] * quad.m_vecColor[ static_cast<std::size_t>( c ) ];
				if ( quad.m_nBlend == Sprite_t::Blend_t::BLEND_ADDITIVE )
				{
					pOut[ c ] += flSrc * flAlpha;
				}
				else
				{
					pOut[ c ] = pOut[ c ] * ( 1.0f - flAlpha ) + flSrc * flAlpha + flSrc * quad.m_flAddSelf * flAlpha;
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: A model's triangles into the colour and depth buffers, for the rows of one band:
//          nearest wins, textures follow the perspective and repeat, the mip picked by how many
//          texels a triangle squeezes into each pixel.
//-----------------------------------------------------------------------------
void FillMesh( const ScreenMesh_t &mesh, std::vector<float> &buffer, std::vector<float> &depth, int nWidth, int nHeight, int nRowStart, int nRowEnd )
{
	for ( std::size_t t = 0; t + 2 < mesh.m_Vertices.size(); t += 3 )
	{
		const ScreenVertex_t &a = mesh.m_Vertices[ t ];
		const ScreenVertex_t &b = mesh.m_Vertices[ t + 1 ];
		const ScreenVertex_t &c = mesh.m_Vertices[ t + 2 ];
		const float flArea = ( b.m_vecPosition[ 0 ] - a.m_vecPosition[ 0 ] ) * ( c.m_vecPosition[ 1 ] - a.m_vecPosition[ 1 ] ) - ( b.m_vecPosition[ 1 ] - a.m_vecPosition[ 1 ] ) * ( c.m_vecPosition[ 0 ] - a.m_vecPosition[ 0 ] );
		if ( std::abs( flArea ) < 1e-6f )
		{
			continue;
		}
		const int nY0 = std::max( { nRowStart, 0, static_cast<int>( std::floor( std::min( { a.m_vecPosition[ 1 ], b.m_vecPosition[ 1 ], c.m_vecPosition[ 1 ] } ) ) ) } );
		const int nY1 = std::min( { nRowEnd - 1, nHeight - 1, static_cast<int>( std::ceil( std::max( { a.m_vecPosition[ 1 ], b.m_vecPosition[ 1 ], c.m_vecPosition[ 1 ] } ) ) ) } );
		const int nX0 = std::max( 0, static_cast<int>( std::floor( std::min( { a.m_vecPosition[ 0 ], b.m_vecPosition[ 0 ], c.m_vecPosition[ 0 ] } ) ) ) );
		const int nX1 = std::min( nWidth - 1, static_cast<int>( std::ceil( std::max( { a.m_vecPosition[ 0 ], b.m_vecPosition[ 0 ], c.m_vecPosition[ 0 ] } ) ) ) );
		if ( nY0 > nY1 || nX0 > nX1 )
		{
			continue;
		}
		const texture::CImage *pImage = nullptr;
		if ( mesh.m_pSprite && !mesh.m_pSprite->m_Mips.empty() )
		{
			const auto &base = mesh.m_pSprite->m_Mips.front();
			const float flUvArea = std::abs( ( b.m_Uv[ 0 ] - a.m_Uv[ 0 ] ) * ( c.m_Uv[ 1 ] - a.m_Uv[ 1 ] ) - ( b.m_Uv[ 1 ] - a.m_Uv[ 1 ] ) * ( c.m_Uv[ 0 ] - a.m_Uv[ 0 ] ) ) * static_cast<float>( base.m_nWidth ) * static_cast<float>( base.m_nHeight );
			const int nLevel = std::clamp( static_cast<int>( std::floor( 0.5f * std::log2( std::max( 1.0f, flUvArea / std::abs( flArea ) ) ) ) ), 0, static_cast<int>( mesh.m_pSprite->m_Mips.size() ) - 1 );
			pImage = &mesh.m_pSprite->m_Mips[ static_cast<std::size_t>( nLevel ) ];
		}
		// Interpolated across the screen: 1/depth, and everything else over depth.
		const float flInvA = 1.0f / a.m_vecPosition[ 2 ], flInvB = 1.0f / b.m_vecPosition[ 2 ], flInvC = 1.0f / c.m_vecPosition[ 2 ];
		for ( int y = nY0; y <= nY1; y++ )
		{
			const float flPy = static_cast<float>( y ) + 0.5f;
			for ( int x = nX0; x <= nX1; x++ )
			{
				const float flPx = static_cast<float>( x ) + 0.5f;
				const float w0 = ( ( b.m_vecPosition[ 0 ] - flPx ) * ( c.m_vecPosition[ 1 ] - flPy ) - ( b.m_vecPosition[ 1 ] - flPy ) * ( c.m_vecPosition[ 0 ] - flPx ) ) / flArea;
				const float w1 = ( ( c.m_vecPosition[ 0 ] - flPx ) * ( a.m_vecPosition[ 1 ] - flPy ) - ( c.m_vecPosition[ 1 ] - flPy ) * ( a.m_vecPosition[ 0 ] - flPx ) ) / flArea;
				const float w2 = 1.0f - w0 - w1;
				if ( w0 < 0.0f || w1 < 0.0f || w2 < 0.0f )
				{
					continue;
				}
				const float flInv = w0 * flInvA + w1 * flInvB + w2 * flInvC;
				const float flDepth = 1.0f / flInv;
				float &flNearest = depth[ static_cast<std::size_t>( y ) * static_cast<std::size_t>( nWidth ) + static_cast<std::size_t>( x ) ];
				if ( flDepth >= flNearest )
				{
					continue;
				}
				flNearest = flDepth;
				const auto mix = [ & ]( float fa, float fb, float fc ) { return ( w0 * fa * flInvA + w1 * fb * flInvB + w2 * fc * flInvC ) / flInv; };
				std::array<float, 4> texel{ 0.6f, 0.6f, 0.6f, 1.0f };
				if ( pImage )
				{
					const float u = mix( a.m_Uv[ 0 ], b.m_Uv[ 0 ], c.m_Uv[ 0 ] );
					const float v = mix( a.m_Uv[ 1 ], b.m_Uv[ 1 ], c.m_Uv[ 1 ] );
					texel = Sample( *pImage, u - std::floor( u ), v - std::floor( v ) );
				}
				float *pOut = buffer.data() + ( static_cast<std::size_t>( y ) * static_cast<std::size_t>( nWidth ) + static_cast<std::size_t>( x ) ) * 3;
				for ( std::size_t k = 0; k < 3; k++ )
				{
					pOut[ k ] = texel[ k ] * mix( a.m_vecColor[ k ], b.m_vecColor[ k ], c.m_vecColor[ k ] );
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: The ground plane's grid (z = 0, under the camera's target) as short screen segments:
//          projected
//          end points with depth, a colour, and how strongly to draw it (fading out with distance,
//          so the far
//          lines don't pile up at the horizon).
//-----------------------------------------------------------------------------
void ForEachGridSegment( const CCamera &camera, const std::function<void( std::array<float, 3>, std::array<float, 3>, std::array<float, 3>, float )> &pfnDraw )
{
	const float flSpacing = GridSpacing( camera );
	const int nLines = 8;
	const int nPieces = 16;
	const float flCenterX = std::round( camera.m_vecTarget.m_flX / flSpacing ) * flSpacing;
	const float flCenterY = std::round( camera.m_vecTarget.m_flY / flSpacing ) * flSpacing;
	const float flHalf = flSpacing * static_cast<float>( nLines );
	const CVec3 vecEye = camera.GetEye();
	const CVec3 f = camera.GetForward();
	const auto line = [ & ]( CVec3 vecFrom, CVec3 vecTo, std::array<float, 3> vecColor, float flStrength ) {
		for ( int i = 0; i < nPieces; i++ )
		{
			CVec3 a = vecFrom + ( vecTo - vecFrom ) * ( static_cast<float>( i ) / nPieces );
			CVec3 b = vecFrom + ( vecTo - vecFrom ) * ( static_cast<float>( i + 1 ) / nPieces );
			// Clip to the near plane.
			const float flDa = Dot( a - vecEye, f ) - NEAR * 2.0f;
			const float flDb = Dot( b - vecEye, f ) - NEAR * 2.0f;
			if ( flDa < 0.0f && flDb < 0.0f )
			{
				continue;
			}
			if ( flDa < 0.0f )
			{
				a = a + ( b - a ) * ( flDa / ( flDa - flDb ) );
			}
			else if ( flDb < 0.0f )
			{
				b = b + ( a - b ) * ( flDb / ( flDb - flDa ) );
			}
			const auto pa = camera.Project( a );
			const auto pb = camera.Project( b );
			if ( !pa || !pb )
			{
				continue;
			}
			const CVec3 vecMiddle = ( a + b ) * 0.5f;
			const float flAway = Length( CVec3{ vecMiddle.m_flX - camera.m_vecTarget.m_flX, vecMiddle.m_flY - camera.m_vecTarget.m_flY, 0 } ) / flHalf;
			const float flFade = std::max( 0.0f, 1.0f - flAway );
			if ( flFade > 0.0f )
			{
				pfnDraw( *pa, *pb, vecColor, flStrength * flFade * flFade );
			}
		}
	};
	const std::array<float, 3> vecGrey{ 0.55f, 0.57f, 0.6f };
	for ( int i = -nLines; i <= nLines; i++ )
	{
		const float flOffset = static_cast<float>( i ) * flSpacing;
		const bool bXAxis = flCenterY + flOffset == 0.0f;
		const bool bYAxis = flCenterX + flOffset == 0.0f;
		line( { flCenterX - flHalf, flCenterY + flOffset, 0 }, { flCenterX + flHalf, flCenterY + flOffset, 0 }, bXAxis ? std::array<float, 3>{ 0.8f, 0.3f, 0.3f } : vecGrey, bXAxis ? 0.7f : 0.3f );
		line( { flCenterX + flOffset, flCenterY - flHalf, 0 }, { flCenterX + flOffset, flCenterY + flHalf, 0 }, bYAxis ? std::array<float, 3>{ 0.3f, 0.75f, 0.35f } : vecGrey, bYAxis ? 0.7f : 0.3f );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void DrawGrid( const CCamera &camera, std::vector<float> &buffer )
{
	ForEachGridSegment( camera, [ & ]( std::array<float, 3> a, std::array<float, 3> b, std::array<float, 3> vecColor, float flStrength ) {
		const float flDx = b[ 0 ] - a[ 0 ];
		const float flDy = b[ 1 ] - a[ 1 ];
		const int nSteps = std::min( 4096, static_cast<int>( std::max( std::abs( flDx ), std::abs( flDy ) ) ) + 1 );
		for ( int i = 0; i <= nSteps; i++ )
		{
			const float t = static_cast<float>( i ) / static_cast<float>( nSteps );
			const int x = static_cast<int>( a[ 0 ] + flDx * t );
			const int y = static_cast<int>( a[ 1 ] + flDy * t );
			if ( x < 0 || y < 0 || x >= camera.m_nWidth || y >= camera.m_nHeight )
			{
				continue;
			}
			float *pOut = buffer.data() + ( static_cast<std::size_t>( y ) * static_cast<std::size_t>( camera.m_nWidth ) + static_cast<std::size_t>( x ) ) * 3;
			for ( std::size_t c = 0; c < 3; c++ )
			{
				pOut[ c ] = pOut[ c ] * ( 1.0f - flStrength ) + vecColor[ c ] * flStrength;
			}
		}
	} );
}

//-----------------------------------------------------------------------------
// Purpose: Which part of the texture a particle shows now.
//-----------------------------------------------------------------------------
std::array<float, 4> FrameRect( const Sprite_t &sprite, const CCompiledOperator *pRenderer, const Particle_t &p )
{
	if ( sprite.m_Sheet.m_Sequences.empty() )
	{
		return { 0, 0, 1, 1 };
	}
	const CSheet::Sequence_t *pSequence = sprite.m_Sheet.Sequence( p.m_nSequence );
	if ( !pSequence )
	{
		pSequence = &sprite.m_Sheet.m_Sequences[ static_cast<std::size_t>( std::abs( p.m_nSequence ) ) % sprite.m_Sheet.m_Sequences.size() ];
	}
	if ( pSequence->m_Frames.empty() )
	{
		return { 0, 0, 1, 1 };
	}
	const auto nCount = static_cast<int>( pSequence->m_Frames.size() );
	const float flRate = pRenderer ? pRenderer->Number( "animation rate", 0.1f ) : 0.1f;
	int nIndex = 0;
	if ( pRenderer && pRenderer->Flag( "animation_fit_lifetime" ) )
	{
		nIndex = static_cast<int>( p.m_flAge / std::max( p.m_flLifetime, 0.001f ) * static_cast<float>( nCount ) );
	}
	else if ( !pRenderer || pRenderer->Flag( "use animation rate as FPS" ) )
	{
		nIndex = static_cast<int>( p.m_flAge * flRate );
	}
	else
	{
		// The rate is how many times a second the whole sequence plays.
		nIndex = static_cast<int>( p.m_flAge * flRate * static_cast<float>( nCount ) );
	}
	nIndex = pSequence->m_bClamp ? std::clamp( nIndex, 0, nCount - 1 ) : ( ( nIndex % nCount ) + nCount ) % nCount;
	return pSequence->m_Frames[ static_cast<std::size_t>( nIndex ) ].m_Rect;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const texture::CImage &MipFor( const Sprite_t &sprite, float flTexels, float flPixels )
{
	int nLevel = 0;
	if ( flPixels > 0.0f )
	{
		nLevel = static_cast<int>( std::floor( std::log2( std::max( 1.0f, flTexels / flPixels ) ) ) );
	}
	nLevel = std::clamp( nLevel, 0, static_cast<int>( sprite.m_Mips.size() ) - 1 );
	return sprite.m_Mips[ static_cast<std::size_t>( nLevel ) ];
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: About a dozen lines across the view, at a power of two the game's grid would use.
//-----------------------------------------------------------------------------
float GridSpacing( const CCamera &camera )
{
	return std::exp2( std::max( 2.0f, std::round( std::log2( camera.m_flDistance / 8.0f ) ) ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const CSheet::Sequence_t *CSheet::Sequence( int nNumber ) const
{
	const auto it = std::ranges::find( m_Sequences, nNumber, &Sequence_t::m_nNumber );
	return it == m_Sequences.end() ? nullptr : &*it;
}

//-----------------------------------------------------------------------------
// Purpose: Input is untrusted; nullopt if the data doesn't add up.
//-----------------------------------------------------------------------------
std::optional<CSheet> ReadSheet( std::span<const std::byte> data )
{
	if ( data.size() < 8 )
	{
		return std::nullopt;
	}
	const auto nVersion = ReadAt<std::int32_t>( data, 0 );
	const auto nCount = ReadAt<std::int32_t>( data, 4 );
	if ( nVersion < 0 || nVersion > 1 || nCount < 0 || nCount > 4096 )
	{
		return std::nullopt;
	}
	// Version 1 stores four texture rects per frame (for blending sequences); the first is the image.
	const std::size_t nSamples = nVersion == 0 ? 1 : 4;
	CSheet sheet;
	std::size_t nAt = 8;
	for ( int i = 0; i < nCount; i++ )
	{
		if ( data.size() - nAt < 16 )
		{
			return std::nullopt;
		}
		CSheet::Sequence_t sequence;
		sequence.m_nNumber = ReadAt<std::int32_t>( data, nAt );
		sequence.m_bClamp = ReadAt<std::uint32_t>( data, nAt + 4 ) != 0;
		const auto nFrames = ReadAt<std::int32_t>( data, nAt + 8 );
		sequence.m_flTotalTime = ReadAt<float>( data, nAt + 12 );
		nAt += 16;
		const std::size_t nFrameSize = 4 + 16 * nSamples;
		if ( nFrames < 0 || static_cast<std::size_t>( nFrames ) > ( data.size() - nAt ) / nFrameSize )
		{
			return std::nullopt;
		}
		for ( int f = 0; f < nFrames; f++ )
		{
			CSheet::Frame_t frame;
			frame.m_flDuration = ReadAt<float>( data, nAt );
			for ( std::size_t c = 0; c < 4; c++ )
			{
				frame.m_Rect[ c ] = ReadAt<float>( data, nAt + 4 + c * 4 );
			}
			sequence.m_Frames.push_back( frame );
			nAt += nFrameSize;
		}
		sheet.m_Sequences.push_back( std::move( sequence ) );
	}
	return sheet;
}

//-----------------------------------------------------------------------------
// Purpose: The soft dot drawn for missing textures.
//-----------------------------------------------------------------------------
Sprite_t FallbackSprite()
{
	Sprite_t sprite;
	texture::CImage image( 64, 64 );
	for ( int y = 0; y < 64; y++ )
	{
		for ( int x = 0; x < 64; x++ )
		{
			const float flDx = ( static_cast<float>( x ) + 0.5f ) / 32.0f - 1.0f;
			const float flDy = ( static_cast<float>( y ) + 0.5f ) / 32.0f - 1.0f;
			const float flFalloff = std::max( 0.0f, 1.0f - ( flDx * flDx + flDy * flDy ) );
			std::uint8_t *p = image.Pixel( x, y );
			p[ 0 ] = p[ 1 ] = p[ 2 ] = 255;
			p[ 3 ] = static_cast<std::uint8_t>( std::lround( 255.0f * flFalloff * flFalloff ) );
		}
	}
	sprite.m_Mips.push_back( std::move( image ) );
	while ( sprite.m_Mips.back().m_nWidth > 1 )
	{
		sprite.m_Mips.push_back( texture::HalfSize( sprite.m_Mips.back() ) );
	}
	return sprite;
}

//-----------------------------------------------------------------------------
// Purpose: Reads materials/<material>.vmt and its $basetexture through read.
//-----------------------------------------------------------------------------
Sprite_t LoadSprite( std::string_view szMaterial, const FileReader_t &pfnRead )
{
	Sprite_t sprite = FallbackSprite();
	const std::string szVmtPath = MaterialPath( szMaterial );
	const auto vmt = pfnRead ? pfnRead( szVmtPath ) : std::nullopt;
	if ( !vmt )
	{
		sprite.m_szProblem = szVmtPath + " not found";
		return sprite;
	}
	const auto parsed = kv1::Parse( std::string{ reinterpret_cast<const char *>( vmt->data() ), vmt->size() } );
	const kv1::CNode *pRoot = nullptr;
	for ( const kv1::CNode &node : parsed.m_Document.m_Roots )
	{
		if ( node.IsBlock() )
		{
			pRoot = &node;
			break;
		}
	}
	if ( !pRoot )
	{
		sprite.m_szProblem = szVmtPath + " has no shader block";
		return sprite;
	}
	const std::string szShader = Lower( pRoot->GetKey() );
	std::string szBase;
	for ( const kv1::CNode &child : pRoot->GetChildren() )
	{
		if ( !child.IsPair() )
		{
			continue;
		}
		const std::string szKey = Lower( child.GetKey() );
		const std::string szValue = child.GetValue();
		const float flNumber = std::strtof( szValue.c_str(), nullptr );
		if ( szKey == "$basetexture" )
		{
			szBase = szValue;
		}
		else if ( szKey == "$additive" )
		{
			sprite.m_nBlend = flNumber != 0.0f ? Sprite_t::Blend_t::BLEND_ADDITIVE : sprite.m_nBlend;
		}
		else if ( szKey == "$overbrightfactor" )
		{
			sprite.m_flOverbright = flNumber > 0.0f ? flNumber : 1.0f;
		}
		else if ( szKey == "$addself" )
		{
			sprite.m_flAddSelf = flNumber;
		}
	}
	if ( szShader == "refract" )
	{
		sprite.m_bVisible = false;
		sprite.m_szProblem = "refraction isn't drawn in the preview";
		return sprite;
	}
	if ( szBase.empty() )
	{
		// No texture: a plain white square, like vgui/white.
		texture::CImage white( 4, 4 );
		std::ranges::fill( white.m_Pixels, std::uint8_t{ 255 } );
		sprite.m_Mips = { white, texture::HalfSize( white ), texture::HalfSize( texture::HalfSize( white ) ) };
		return sprite;
	}
	const std::string szVtfPath = "materials/" + vfs::NormalizePath( szBase ) + ".vtf";
	const auto data = pfnRead( szVtfPath );
	if ( !data )
	{
		sprite.m_szProblem = szVtfPath + " not found";
		return sprite;
	}
	std::string szError;
	const auto vtf = texture::ReadVtf( *data, &szError );
	const auto image = vtf ? vtf->Decode( 0, 0, 0, &szError ) : std::nullopt;
	if ( !image )
	{
		sprite.m_szProblem = szVtfPath + ": " + szError;
		return sprite;
	}
	sprite.m_Mips = { *image };
	while ( sprite.m_Mips.back().m_nWidth > 1 || sprite.m_Mips.back().m_nHeight > 1 )
	{
		sprite.m_Mips.push_back( texture::HalfSize( sprite.m_Mips.back() ) );
	}
	if ( const auto sheet = ReadSheet( vtf->Resource( texture::SHEET_RESOURCE ) ) )
	{
		sprite.m_Sheet = *sheet;
	}
	return sprite;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVec3 CCamera::GetForward() const
{
	const float y = m_flYaw * DEGREES;
	const float p = m_flPitch * DEGREES;
	return { std::cos( p ) * std::cos( y ), std::cos( p ) * std::sin( y ), -std::sin( p ) };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVec3 CCamera::GetEye() const
{
	return m_vecTarget - GetForward() * m_flDistance;
}

//-----------------------------------------------------------------------------
// Purpose: Pixels per world unit at a depth.
//-----------------------------------------------------------------------------
float CCamera::Scale( float flDepth ) const
{
	const float flFocal = static_cast<float>( m_nHeight ) * 0.5f / std::tan( m_flFov * 0.5f * DEGREES );
	return flFocal / std::max( flDepth, NEAR );
}

//-----------------------------------------------------------------------------
// Purpose: The direction from the eye through a screen position (pixels).
//-----------------------------------------------------------------------------
CVec3 CCamera::Ray( float x, float y ) const
{
	const CVec3 f = GetForward();
	const CVec3 vecRight = Normalized( Cross( f, { 0, 0, 1 } ) );
	const CVec3 vecUp = Cross( vecRight, f );
	const float s = Scale( 1.0f );
	return Normalized( f + vecRight * ( ( x - static_cast<float>( m_nWidth ) * 0.5f ) / s ) - vecUp * ( ( y - static_cast<float>( m_nHeight ) * 0.5f ) / s ) );
}

//-----------------------------------------------------------------------------
// Purpose: Screen position (pixels) and depth along the view; nullopt behind the camera.
//-----------------------------------------------------------------------------
std::optional<std::array<float, 3>> CCamera::Project( CVec3 vecPoint ) const
{
	const CVec3 f = GetForward();
	const CVec3 vecRight = Normalized( Cross( f, { 0, 0, 1 } ) );
	const CVec3 vecUp = Cross( vecRight, f );
	const CVec3 d = vecPoint - GetEye();
	const float flDepth = Dot( d, f );
	if ( flDepth < NEAR )
	{
		return std::nullopt;
	}
	const float s = Scale( flDepth );
	return std::array<float, 3>{ static_cast<float>( m_nWidth ) * 0.5f + Dot( d, vecRight ) * s, static_cast<float>( m_nHeight ) * 0.5f - Dot( d, vecUp ) * s, flDepth };
}

//-----------------------------------------------------------------------------
// Purpose: A camera looking at everything a system shows in its first seconds (simulated on a
//          copy), from
//          its current direction. Falls back to the system's bounding box when nothing appears.
//-----------------------------------------------------------------------------
CCamera Framed( const CSimulation &simulation, CCamera camera, float flSeconds )
{
	CSimulation copy = simulation;
	// Where visible particles are, sampled at a few moments so effects that start late or end
	// early are seen. Stragglers don't count: the box spans the middle 90% on each axis.
	std::array<std::vector<float>, 3> low;
	std::array<std::vector<float>, 3> high;
	std::function<void( const CSimulation::Instance_t & )> pfnCollect = [ & ]( const CSimulation::Instance_t &instance ) {
		for ( const Particle_t &p : instance.m_Particles )
		{
			if ( p.m_flAlpha < 0.05f )
			{
				continue;
			}
			const float r = std::min( p.m_flRadius, 256.0f );
			const std::array<float, 3> vecAt{ p.m_vecPosition.m_flX, p.m_vecPosition.m_flY, p.m_vecPosition.m_flZ };
			for ( std::size_t nAxis = 0; nAxis < 3; nAxis++ )
			{
				low[ nAxis ].push_back( vecAt[ nAxis ] - r );
				high[ nAxis ].push_back( vecAt[ nAxis ] + r );
			}
		}
		for ( const auto &child : instance.m_Children )
		{
			pfnCollect( child );
		}
	};
	for ( int i = 0; i < 6 && copy.GetRoot().m_pDefinition; i++ )
	{
		copy.Advance( flSeconds / 6.0f );
		pfnCollect( copy.GetRoot() );
	}
	CVec3 a;
	CVec3 b;
	if ( !low[ 0 ].empty() )
	{
		const auto percentile = []( std::vector<float> &values, float flShare ) {
			const auto nAt = static_cast<std::size_t>( flShare * static_cast<float>( values.size() - 1 ) );
			std::nth_element( values.begin(), values.begin() + static_cast<std::ptrdiff_t>( nAt ), values.end() );
			return values[ nAt ];
		};
		a = { percentile( low[ 0 ], 0.05f ), percentile( low[ 1 ], 0.05f ), percentile( low[ 2 ], 0.05f ) };
		b = { percentile( high[ 0 ], 0.95f ), percentile( high[ 1 ], 0.95f ), percentile( high[ 2 ], 0.95f ) };
	}
	else if ( copy.GetRoot().m_pDefinition )
	{
		a = copy.GetRoot().m_pDefinition->m_vecBoundsMin + copy.ControlPoint( 0 );
		b = copy.GetRoot().m_pDefinition->m_vecBoundsMax + copy.ControlPoint( 0 );
	}
	else
	{
		return camera;
	}
	// The model the effect stands on is part of the picture too.
	if ( const model::CModel *pModel = copy.GetModel() )
	{
		const CVec3 vecOrigin = copy.ControlPoint( copy.GetModelControlPoint() );
		const CVec3 vecLow = vecOrigin + pModel->m_vecMin;
		const CVec3 vecHigh = vecOrigin + pModel->m_vecMax;
		a = { std::min( a.m_flX, vecLow.m_flX ), std::min( a.m_flY, vecLow.m_flY ), std::min( a.m_flZ, vecLow.m_flZ ) };
		b = { std::max( b.m_flX, vecHigh.m_flX ), std::max( b.m_flY, vecHigh.m_flY ), std::max( b.m_flZ, vecHigh.m_flZ ) };
	}
	camera.m_vecTarget = ( a + b ) * 0.5f;
	const float flExtent = std::max( 16.0f, Length( b - a ) );
	camera.m_flDistance = std::clamp( flExtent * 0.5f / std::tan( camera.m_flFov * 0.5f * DEGREES ), 24.0f, 20000.0f );
	return camera;
}

//-----------------------------------------------------------------------------
// Purpose: Everything the particles draw, in drawing order (system by system, back to front in
//          each).
//-----------------------------------------------------------------------------
std::vector<ScreenQuad_t> Layout( const CSimulation &simulation, const CCamera &camera, const SpriteLookup_t &pfnSprites )
{
	const CLibrary &library = simulation.GetLibrary();
	std::vector<ScreenQuad_t> quads;
	std::function<void( const CSimulation::Instance_t & )> pfnCollect = [ & ]( const CSimulation::Instance_t &instance ) {
		const CDefinition &definition = *instance.m_pDefinition;
		const auto nIndex = static_cast<std::size_t>( &definition - library.m_Definitions.data() );
		const Sprite_t *pSprite = pfnSprites ? pfnSprites( nIndex ) : nullptr;
		const auto renderers = definition.Of( Category_t::CATEGORY_RENDERERS );
		std::vector<ScreenQuad_t> mine;
		if ( pSprite && pSprite->m_bVisible && !pSprite->m_Mips.empty() )
		{
			for ( const CCompiledOperator *pRenderer : renderers )
			{
				if ( !pRenderer->m_bSimulated )
				{
					continue;
				}
				const bool bTrail = pRenderer->m_szFunction == "render_sprite_trail";
				const bool bRope = pRenderer->m_szFunction == "render_rope";
				const bool bVelocityTurn = pRenderer->m_szFunction == "render_screen_velocity_rotate";
				// 0 faces the camera; 1 stands upright and turns only around Z; 2 (and 3, along the
				// particle's normal, which the preview doesn't track) lies flat on the ground.
				const int nOrientation = pRenderer->m_szFunction == "render_animated_sprites" ? pRenderer->Integer( "orientation_type" ) : 0;
				std::vector<const Particle_t *> ordered;
				for ( const Particle_t &p : instance.m_Particles )
				{
					ordered.push_back( &p );
				}
				if ( bRope )
				{
					std::ranges::sort( ordered, {}, &Particle_t::m_flCreationTime );
				}
				for ( std::size_t i = 0; i < ordered.size(); i++ )
				{
					const Particle_t &p = *ordered[ i ];
					const auto center = camera.Project( p.m_vecPosition );
					if ( !center )
					{
						continue;
					}
					ScreenQuad_t quad;
					quad.m_flDepth = ( *center )[ 2 ];
					quad.m_pSprite = pSprite;
					quad.m_Rect = FrameRect( *pSprite, pRenderer, p );
					quad.m_flAlpha = std::clamp( p.m_flAlpha, 0.0f, 1.0f );
					quad.m_vecColor = { p.m_vecColor[ 0 ] * pSprite->m_flOverbright, p.m_vecColor[ 1 ] * pSprite->m_flOverbright, p.m_vecColor[ 2 ] * pSprite->m_flOverbright };
					quad.m_nBlend = pSprite->m_nBlend;
					quad.m_flAddSelf = pSprite->m_flAddSelf;
					const float flSize = p.m_flRadius * camera.Scale( quad.m_flDepth ); // radius in pixels
					quad.m_flPixels = 2.0f * flSize;

					std::optional<std::array<float, 3>> end;
					if ( bTrail )
					{
						const float flSpeed = Length( p.m_vecVelocity );
						const float flTrailLength = std::clamp( flSpeed * p.m_flTrailLength, pRenderer->Number( "min length" ), pRenderer->Number( "max length", 2000.0f ) );
						if ( flSpeed > 0.0f && flTrailLength > 0.0f )
						{
							end = camera.Project( p.m_vecPosition - p.m_vecVelocity * ( flTrailLength / flSpeed ) );
						}
					}
					else if ( bRope && i + 1 < ordered.size() )
					{
						end = camera.Project( ordered[ i + 1 ]->m_vecPosition );
					}
					// A shape in the world, drawn through its four projected corners.
					const auto worldQuad = [ & ]( CVec3 u, CVec3 v ) {
						// u runs along the texture's width, v down its height; both are half sizes.
						const std::array<CVec3, 4> world{ p.m_vecPosition - u - v, p.m_vecPosition + u - v, p.m_vecPosition + u + v, p.m_vecPosition - u + v };
						for ( std::size_t c = 0; c < 4; c++ )
						{
							const auto corner = camera.Project( world[ c ] );
							if ( !corner )
							{
								return false;
							}
							quad.m_Corners[ c ] = *corner;
						}
						return true;
					};
					if ( end )
					{
						// A strip from here to the end point, as wide as the particle.
						const float flDx = ( *end )[ 0 ] - ( *center )[ 0 ];
						const float flDy = ( *end )[ 1 ] - ( *center )[ 1 ];
						const float flLengthOnScreen = std::hypot( flDx, flDy );
						if ( flLengthOnScreen < 0.5f )
						{
							continue;
						}
						const float flNx = -flDy / flLengthOnScreen * flSize;
						const float flNy = flDx / flLengthOnScreen * flSize;
						const float flHx = ( *center )[ 0 ];
						const float flHy = ( *center )[ 1 ];
						const float flHd = ( *center )[ 2 ];
						const float flEd = ( *end )[ 2 ];
						quad.m_Corners = { { { flHx - flNx, flHy - flNy, flHd }, { flHx + flNx, flHy + flNy, flHd }, { flHx + flNx + flDx, flHy + flNy + flDy, flEd }, { flHx - flNx + flDx, flHy - flNy + flDy, flEd } } };
					}
					else if ( bTrail || bRope )
					{
						continue;
					}
					else if ( nOrientation >= 2 )
					{
						// Lying on the ground plane, turned by its rotation.
						const float c = std::cos( p.m_flRotation ) * p.m_flRadius;
						const float s = std::sin( p.m_flRotation ) * p.m_flRadius;
						if ( !worldQuad( { c, s, 0 }, { s, -c, 0 } ) )
						{
							continue;
						}
					}
					else if ( nOrientation == 1 )
					{
						// Upright, facing the camera as far as turning around Z allows.
						const CVec3 vecForward = camera.GetForward();
						const CVec3 vecAcross = Normalized( CVec3{ -vecForward.m_flY, vecForward.m_flX, 0 } );
						if ( !worldQuad( vecAcross * -p.m_flRadius, CVec3{ 0, 0, -p.m_flRadius } ) )
						{
							continue;
						}
					}
					else
					{
						// Facing the camera, turned by its rotation; or, for the velocity renderer, so
						// the texture's forward (forward_angle from its x axis, -90 being up) points the
						// way the particle moves across the screen.
						float flTurn = p.m_flRotation;
						if ( bVelocityTurn )
						{
							const auto ahead = camera.Project( p.m_vecPosition + p.m_vecVelocity * 0.01f );
							const float flDx = ahead ? ( *ahead )[ 0 ] - ( *center )[ 0 ] : 0.0f;
							const float flDy = ahead ? ( *ahead )[ 1 ] - ( *center )[ 1 ] : 0.0f;
							const float flWay = flDx != 0.0f || flDy != 0.0f ? std::atan2( flDy, flDx ) : 0.0f;
							flTurn = flWay - pRenderer->Number( "forward_angle", -90.0f ) * DEGREES + pRenderer->Number( "rotate_rate(dps)" ) * DEGREES * p.m_flAge;
						}
						const float c = std::cos( flTurn ) * flSize;
						const float s = std::sin( flTurn ) * flSize;
						Around( quad, *center, 2.0f * c, 2.0f * s, -2.0f * s, 2.0f * c );
					}
					if ( OnScreen( quad, camera ) && quad.m_flAlpha > 0.0f )
					{
						mine.push_back( quad );
					}
				}
			}
		}
		if ( definition.m_bSort )
		{
			std::ranges::stable_sort( mine, std::greater{}, &ScreenQuad_t::m_flDepth );
		}
		quads.insert( quads.end(), mine.begin(), mine.end() );
		for ( const CSimulation::Instance_t &child : instance.m_Children )
		{
			pfnCollect( child );
		}
	};
	if ( simulation.GetRoot().m_pDefinition )
	{
		pfnCollect( simulation.GetRoot() );
	}

	return quads;
}

//-----------------------------------------------------------------------------
// Purpose: The simulation's model on the screen, a mesh per texture, lit from over the camera's
//          shoulder with some light all round so the far side isn't black.
//-----------------------------------------------------------------------------
std::vector<ScreenMesh_t> LayoutModel( const CSimulation &simulation, const CCamera &camera, const ModelSpriteLookup_t &pfnSprites )
{
	std::vector<ScreenMesh_t> meshes;
	const model::CModel *pModel = simulation.GetModel();
	if ( !pModel )
	{
		return meshes;
	}
	const CVec3 vecOrigin = simulation.ControlPoint( simulation.GetModelControlPoint() );
	const CVec3 vecForward = camera.GetForward();
	const CVec3 vecRight = Normalized( Cross( vecForward, { 0, 0, 1 } ) );
	const CVec3 vecLight = Normalized( -vecForward + CVec3{ 0, 0, 0.8f } - vecRight * 0.4f );
	std::vector<std::optional<ScreenVertex_t>> projected( pModel->m_Vertices.size() );
	for ( std::size_t i = 0; i < pModel->m_Vertices.size(); i++ )
	{
		const model::Vertex_t &vertex = pModel->m_Vertices[ i ];
		if ( const auto at = camera.Project( vecOrigin + vertex.m_vecPosition ) )
		{
			const float flLight = 0.5f + 0.7f * std::max( 0.0f, Dot( Normalized( vertex.m_vecNormal ), vecLight ) );
			projected[ i ] = ScreenVertex_t{ *at, vertex.m_Uv, { flLight, flLight, flLight } };
		}
	}
	for ( const model::Mesh_t &mesh : pModel->m_Meshes )
	{
		const Sprite_t *pSprite = mesh.m_nMaterial >= 0 && pfnSprites ? pfnSprites( static_cast<std::size_t>( mesh.m_nMaterial ) ) : nullptr;
		auto found = std::ranges::find( meshes, pSprite, &ScreenMesh_t::m_pSprite );
		if ( found == meshes.end() )
		{
			meshes.push_back( { pSprite, {} } );
			found = meshes.end() - 1;
		}
		for ( std::size_t t = 0; t + 2 < mesh.m_Indices.size(); t += 3 )
		{
			const auto &a = projected[ mesh.m_Indices[ t ] ];
			const auto &b = projected[ mesh.m_Indices[ t + 1 ] ];
			const auto &c = projected[ mesh.m_Indices[ t + 2 ] ];
			if ( a && b && c )
			{
				found->m_Vertices.insert( found->m_Vertices.end(), { *a, *b, *c } );
			}
		}
	}
	return meshes;
}

//-----------------------------------------------------------------------------
// Purpose: The ground grid, as thin quads.
//-----------------------------------------------------------------------------
std::vector<ScreenQuad_t> GridQuads( const CCamera &camera )
{
	std::vector<ScreenQuad_t> out;
	ForEachGridSegment( camera, [ & ]( std::array<float, 3> a, std::array<float, 3> b, std::array<float, 3> vecColor, float flStrength ) {
		// A line one pixel wide.
		const float flDx = b[ 0 ] - a[ 0 ];
		const float flDy = b[ 1 ] - a[ 1 ];
		const float flSize = std::hypot( flDx, flDy );
		if ( flSize < 0.01f )
		{
			return;
		}
		const float flNx = -flDy / flSize * 0.6f;
		const float flNy = flDx / flSize * 0.6f;
		ScreenQuad_t quad;
		quad.m_Corners = { { { a[ 0 ] - flNx, a[ 1 ] - flNy, a[ 2 ] }, { a[ 0 ] + flNx, a[ 1 ] + flNy, a[ 2 ] }, { b[ 0 ] + flNx, b[ 1 ] + flNy, b[ 2 ] }, { b[ 0 ] - flNx, b[ 1 ] - flNy, b[ 2 ] } } };
		quad.m_vecColor = vecColor;
		quad.m_flAlpha = flStrength;
		out.push_back( quad );
	} );
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Draws into out (camera.width x camera.height). Rows are split over the available cores.
//-----------------------------------------------------------------------------
void Render( const CSimulation &simulation, const CCamera &camera, const SpriteLookup_t &pfnSprites, texture::CImage &out, const RenderOptions_t &options )
{
	const int nWidth = std::max( 1, camera.m_nWidth );
	const int nHeight = std::max( 1, camera.m_nHeight );
	std::vector<float> buffer( static_cast<std::size_t>( nWidth ) * static_cast<std::size_t>( nHeight ) * 3 );
	for ( std::size_t i = 0; i < buffer.size(); i += 3 )
	{
		buffer[ i ] = options.m_vecBackground[ 0 ];
		buffer[ i + 1 ] = options.m_vecBackground[ 1 ];
		buffer[ i + 2 ] = options.m_vecBackground[ 2 ];
	}
	if ( options.m_bGrid )
	{
		DrawGrid( camera, buffer );
	}

	// The model, if any, first: it fills the depth the particles are tested against.
	const std::vector<ScreenMesh_t> meshes = LayoutModel( simulation, camera, options.m_pfnModelSprites );
	std::vector<float> depth;
	if ( !meshes.empty() )
	{
		depth.assign( static_cast<std::size_t>( nWidth ) * static_cast<std::size_t>( nHeight ), std::numeric_limits<float>::infinity() );
	}

	std::vector<CQuad> quads;
	for ( const ScreenQuad_t &shape : Layout( simulation, camera, pfnSprites ) )
	{
		CQuad quad;
		for ( std::size_t c = 0; c < 4; c++ )
		{
			quad.m_Corners[ c ] = { shape.m_Corners[ c ][ 0 ], shape.m_Corners[ c ][ 1 ] };
		}
		quad.m_Rect = shape.m_Rect;
		quad.m_vecColor = shape.m_vecColor;
		quad.m_flAlpha = shape.m_flAlpha;
		quad.m_flDepth = shape.m_flDepth;
		quad.m_nBlend = shape.m_nBlend;
		quad.m_flAddSelf = shape.m_flAddSelf;
		const float flTexels = ( shape.m_Rect[ 2 ] - shape.m_Rect[ 0 ] ) * static_cast<float>( shape.m_pSprite->m_Mips[ 0 ].m_nWidth );
		quad.m_pImage = &MipFor( *shape.m_pSprite, flTexels, shape.m_flPixels );
		if ( quad.Finish( nWidth, nHeight ) )
		{
			quads.push_back( quad );
		}
	}

	// Rows in bands, one per thread; each band draws every quad that reaches it, in order.
	const int nThreads = std::clamp( static_cast<int>( std::thread::hardware_concurrency() ), 1, 8 );
	const int nBand = ( nHeight + nThreads - 1 ) / nThreads;
	std::vector<std::thread> workers;
	for ( int t = 0; t < nThreads; t++ )
	{
		const int nStart = t * nBand;
		const int nEnd = std::min( nHeight, nStart + nBand );
		if ( nStart >= nEnd )
		{
			break;
		}
		workers.emplace_back( [ &, nStart, nEnd ] {
			for ( const ScreenMesh_t &mesh : meshes )
			{
				FillMesh( mesh, buffer, depth, nWidth, nHeight, nStart, nEnd );
			}
			for ( const CQuad &quad : quads )
			{
				if ( quad.m_nMaxY >= nStart && quad.m_nMinY < nEnd )
				{
					Fill( quad, buffer, depth, nWidth, nStart, nEnd );
				}
			}
		} );
	}
	for ( std::thread &worker : workers )
	{
		worker.join();
	}

	out = texture::CImage( nWidth, nHeight );
	for ( std::size_t i = 0, j = 0; i < buffer.size(); i += 3, j += 4 )
	{
		for ( std::size_t c = 0; c < 3; c++ )
		{
			out.m_Pixels[ j + c ] = static_cast<std::uint8_t>( std::lround( std::clamp( buffer[ i + c ], 0.0f, 1.0f ) * 255.0f ) );
		}
		out.m_Pixels[ j + 3 ] = 255;
	}
}

} // namespace xv::particles
