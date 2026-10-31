//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Raster.h. Rows are split into bands drawn on all cores; each band draws the
//          triangles that reach it, in the same order as every other band.
//
//===========================================================================//
#include "Raster.h"

#include <algorithm>
#include <cmath>

#include "util/Parallel.h"

namespace xv::map
{

namespace
{

// A corner of a triangle in clip space with what it carries.
struct ClipVertex_t
{
	std::array<double, 4> m_Clip{};
	std::array<float, 2> m_Texel{};
	std::array<float, 2> m_Luxel{};
	float m_flBlend = 0.0f;
	CVec3 m_vecNormal;
	CVec3 m_vecWorld;
	std::array<float, 4> m_Light{};
};

// A corner on screen: pixels, depth 0-1, and its values divided by w for perspective.
struct ScreenVertex_t
{
	double m_flX = 0.0;
	double m_flY = 0.0;
	double m_flZ = 0.0;
	double m_flInvW = 0.0;
	std::array<double, 15> m_Over{}; // texel u, v, luxel u, v, blend, normal x, y, z, world x, y, z, light 0-3; each over w
};

// A triangle to draw: which mesh and which of its triangles, and how far away it is.
struct TriangleRef_t
{
	std::uint32_t m_nMesh = 0;
	std::uint32_t m_nTriangle = 0;
	std::uint32_t m_nMaterial = 0;
	float m_flDepth = 0.0f;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::array<double, 4> Transform( const std::array<float, 16> &m, const CVec3 &p )
{
	std::array<double, 4> out{};
	for ( std::size_t r = 0; r < 4; r++ )
	{
		out[ r ] = static_cast<double>( m[ r ] ) * p.m_flX + static_cast<double>( m[ 4 + r ] ) * p.m_flY + static_cast<double>( m[ 8 + r ] ) * p.m_flZ + static_cast<double>( m[ 12 + r ] );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
ClipVertex_t Lerp( const ClipVertex_t &a, const ClipVertex_t &b, double t )
{
	ClipVertex_t v;
	for ( std::size_t i = 0; i < 4; i++ )
	{
		v.m_Clip[ i ] = a.m_Clip[ i ] + ( b.m_Clip[ i ] - a.m_Clip[ i ] ) * t;
	}
	const auto ft = static_cast<float>( t );
	for ( std::size_t i = 0; i < 2; i++ )
	{
		v.m_Texel[ i ] = a.m_Texel[ i ] + ( b.m_Texel[ i ] - a.m_Texel[ i ] ) * ft;
		v.m_Luxel[ i ] = a.m_Luxel[ i ] + ( b.m_Luxel[ i ] - a.m_Luxel[ i ] ) * ft;
	}
	for ( std::size_t i = 0; i < 4; i++ )
	{
		v.m_Light[ i ] = a.m_Light[ i ] + ( b.m_Light[ i ] - a.m_Light[ i ] ) * ft;
	}
	v.m_flBlend = a.m_flBlend + ( b.m_flBlend - a.m_flBlend ) * ft;
	v.m_vecNormal = a.m_vecNormal + ( b.m_vecNormal - a.m_vecNormal ) * ft;
	v.m_vecWorld = a.m_vecWorld + ( b.m_vecWorld - a.m_vecWorld ) * ft;
	return v;
}

//-----------------------------------------------------------------------------
// Purpose: The part of a triangle in front of the near plane (z >= -w): up to four corners.
//-----------------------------------------------------------------------------
int ClipNear( const std::array<ClipVertex_t, 3> &in, std::array<ClipVertex_t, 4> &out )
{
	int nOut = 0;
	for ( std::size_t i = 0; i < 3; i++ )
	{
		const ClipVertex_t &a = in[ i ];
		const ClipVertex_t &b = in[ ( i + 1 ) % 3 ];
		const double flA = a.m_Clip[ 2 ] + a.m_Clip[ 3 ];
		const double flB = b.m_Clip[ 2 ] + b.m_Clip[ 3 ];
		if ( flA >= 0.0 )
		{
			out[ static_cast<std::size_t>( nOut++ ) ] = a;
		}
		if ( ( flA >= 0.0 ) != ( flB >= 0.0 ) )
		{
			out[ static_cast<std::size_t>( nOut++ ) ] = Lerp( a, b, flA / ( flA - flB ) );
		}
	}
	return nOut;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
ScreenVertex_t ToScreen( const ClipVertex_t &v, int nWidth, int nHeight )
{
	ScreenVertex_t s;
	const double flInvW = 1.0 / v.m_Clip[ 3 ];
	s.m_flX = ( v.m_Clip[ 0 ] * flInvW + 1.0 ) * 0.5 * nWidth;
	s.m_flY = ( 1.0 - v.m_Clip[ 1 ] * flInvW ) * 0.5 * nHeight;
	s.m_flZ = v.m_Clip[ 2 ] * flInvW * 0.5 + 0.5;
	s.m_flInvW = flInvW;
	const std::array<double, 15> values{ v.m_Texel[ 0 ], v.m_Texel[ 1 ], v.m_Luxel[ 0 ], v.m_Luxel[ 1 ], v.m_flBlend, v.m_vecNormal.m_flX, v.m_vecNormal.m_flY, v.m_vecNormal.m_flZ, v.m_vecWorld.m_flX, v.m_vecWorld.m_flY, v.m_vecWorld.m_flZ, v.m_Light[ 0 ], v.m_Light[ 1 ], v.m_Light[ 2 ], v.m_Light[ 3 ] };
	for ( std::size_t i = 0; i < values.size(); i++ )
	{
		s.m_Over[ i ] = values[ i ] * flInvW;
	}
	return s;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double Edge( const ScreenVertex_t &a, const ScreenVertex_t &b, double x, double y )
{
	return ( b.m_flX - a.m_flX ) * ( y - a.m_flY ) - ( b.m_flY - a.m_flY ) * ( x - a.m_flX );
}

class CRasterizer
{
public:
	// With a sky mask: the 3D skybox's pass (bSkyPass) marks the pixels it draws in it; the
	// world's pass after it keeps the picture and leaves those pixels to its sky faces.
	CRasterizer( const RasterScene_t &scene, const CViewCamera &camera, const RasterOptions_t &options, texture::CImage &out, std::vector<std::uint8_t> *pSkyMask = nullptr, bool bSkyPass = false )
		: m_Scene( scene ),
		  m_Camera( camera ),
		  m_Options( options ),
		  m_Out( out ),
		  m_pSkyMask( pSkyMask ),
		  m_bSkyPass( bSkyPass )
	{
	}

	void Run();

private:
	[[nodiscard]] const RasterMaterial_t *MaterialOf( std::uint32_t nMaterial ) const
	{
		return nMaterial < m_Scene.m_Materials.size() ? &m_Scene.m_Materials[ nMaterial ] : nullptr;
	}
	[[nodiscard]] std::uint8_t FlagsOf( std::uint32_t nObject ) const
	{
		return nObject < m_Scene.m_ObjectFlags.size() ? m_Scene.m_ObjectFlags[ nObject ] : static_cast<std::uint8_t>( RASTER_OBJECT_SHOWN );
	}
	void Gather();
	bool Corners( const TriangleRef_t &ref, std::array<ClipVertex_t, 3> &corners ) const;
	void DrawTriangle( const TriangleRef_t &ref, int nTop, int nBottom, bool bBlend );
	void FillTriangle( const ScreenVertex_t &a, const ScreenVertex_t &b, const ScreenVertex_t &c, const TriangleRef_t &ref, std::uint32_t nObject, int nTop, int nBottom, bool bBlend );
	// A line on screen: pixel x and y, and depth, at each end.
	struct ScreenLine_t
	{
		std::array<double, 3> m_From{};
		std::array<double, 3> m_To{};
		std::array<std::uint8_t, 3> m_Color{};
		bool m_bOnTop = false; // over everything; else depth tested
	};
	void AddLine( Point_t from, Point_t to, std::array<std::uint8_t, 3> color, bool bOnTop );
	void DrawLines();

	const RasterScene_t &m_Scene;
	const CViewCamera &m_Camera;
	const RasterOptions_t &m_Options;
	texture::CImage &m_Out;
	std::vector<std::uint8_t> *m_pSkyMask = nullptr;
	bool m_bSkyPass = false;
	std::array<float, 16> m_Matrix{};
	std::vector<float> m_Depth;
	std::vector<TriangleRef_t> m_Opaque;
	std::vector<TriangleRef_t> m_Blended;
	std::vector<std::vector<std::uint32_t>> m_OpaqueBands; // per band: indices into m_Opaque
	std::vector<std::vector<std::uint32_t>> m_BlendedBands;
	std::vector<ScreenLine_t> m_Lines; // in drawing order
	int m_nBandHeight = 1;
};

//-----------------------------------------------------------------------------
// Purpose: A triangle's corners in clip space, or false when it faces away and its material
//          doesn't draw both sides.
//-----------------------------------------------------------------------------
bool CRasterizer::Corners( const TriangleRef_t &ref, std::array<ClipVertex_t, 3> &corners ) const
{
	const CMapMesh &mesh = *m_Scene.m_Meshes[ ref.m_nMesh ];
	const std::span<const std::array<float, 4>> lights = ref.m_nMesh < m_Scene.m_MeshLights.size() ? m_Scene.m_MeshLights[ ref.m_nMesh ] : std::span<const std::array<float, 4>>{};
	std::array<Point_t, 3> world;
	for ( std::size_t k = 0; k < 3; k++ )
	{
		const std::uint32_t nVertex = mesh.m_Indices[ ref.m_nTriangle * 3 + k ];
		const MeshVertex_t &vertex = mesh.m_Vertices[ nVertex ];
		corners[ k ].m_Light = nVertex < lights.size() ? lights[ nVertex ] : std::array<float, 4>{};
		world[ k ] = ToPoint( vertex.m_vecPosition );
		corners[ k ].m_Clip = Transform( m_Matrix, vertex.m_vecPosition );
		corners[ k ].m_Texel = vertex.m_Texel;
		corners[ k ].m_Luxel = vertex.m_Luxel;
		corners[ k ].m_flBlend = vertex.m_flAlpha;
		corners[ k ].m_vecNormal = vertex.m_vecNormal;
		corners[ k ].m_vecWorld = vertex.m_vecPosition;
	}
	const RasterMaterial_t *pMaterial = MaterialOf( ref.m_nMaterial );
	if ( pMaterial && pMaterial->m_bNoCull )
	{
		return true;
	}
	// Faces are wound clockwise seen from outside, so this points out of the front.
	const Point_t vecFront = Cross( Subtract( world[ 2 ], world[ 0 ] ), Subtract( world[ 1 ], world[ 0 ] ) );
	const Point_t toEye = m_Camera.IsOrthographic() ? Scale( m_Camera.GetForward(), -1.0 ) : Subtract( m_Camera.m_Eye, world[ 0 ] );
	return Dot( vecFront, toEye ) > 0.0;
}

//-----------------------------------------------------------------------------
// Purpose: Every triangle that may show, sorted into opaque and see-through, and into the
//          bands of rows it reaches.
//-----------------------------------------------------------------------------
void CRasterizer::Gather()
{
	const bool bFaces = m_Options.m_Shade.m_nMode != RasterMode_t::RASTER_MODE_WIREFRAME;
	for ( std::uint32_t nMesh = 0; bFaces && nMesh < m_Scene.m_Meshes.size(); nMesh++ )
	{
		const CMapMesh &mesh = *m_Scene.m_Meshes[ nMesh ];
		for ( const MeshBatch_t &batch : mesh.m_Batches )
		{
			if ( batch.m_nChunk < mesh.m_Chunks.size() && ( !m_Camera.CanSee( mesh.m_Chunks[ batch.m_nChunk ].m_Bounds ) || ( m_bSkyPass && !mesh.m_Chunks[ batch.m_nChunk ].m_Bounds.Intersects( m_Options.m_SkyboxRoom ) ) ) )
			{
				continue;
			}
			const RasterMaterial_t *pMaterial = MaterialOf( batch.m_nMaterial );
			if ( pMaterial && pMaterial->m_bTool && !m_Options.m_bToolTextures )
			{
				continue;
			}
			const bool bBlended = pMaterial && !pMaterial->m_bObjectColor && m_Options.m_Shade.m_nMode != RasterMode_t::RASTER_MODE_FLAT && ( pMaterial->m_Shade.m_bTranslucent || pMaterial->m_Shade.m_bAdditive || pMaterial->m_Shade.m_flAlpha < 1.0f );
			for ( std::uint32_t i = batch.m_nFirstIndex; i < batch.m_nFirstIndex + batch.m_nIndexCount; i += 3 )
			{
				const MeshVertex_t &first = mesh.m_Vertices[ mesh.m_Indices[ i ] ];
				const std::uint8_t nFlags = FlagsOf( first.m_nObject );
				if ( !IsDrawn( m_Scene, &mesh, nFlags ) || ( m_bSkyPass && ( nFlags & RASTER_OBJECT_SKYBOX ) == 0 ) )
				{
					continue;
				}
				TriangleRef_t ref{ nMesh, i / 3, batch.m_nMaterial, 0.0f };
				if ( bBlended )
				{
					const Point_t c = Scale( Add( Add( ToPoint( first.m_vecPosition ), ToPoint( mesh.m_Vertices[ mesh.m_Indices[ i + 1 ] ].m_vecPosition ) ), ToPoint( mesh.m_Vertices[ mesh.m_Indices[ i + 2 ] ].m_vecPosition ) ), 1.0 / 3.0 );
					ref.m_flDepth = static_cast<float>( Dot( Subtract( c, m_Camera.m_Eye ), m_Camera.GetForward() ) );
					m_Blended.push_back( ref );
				}
				else
				{
					m_Opaque.push_back( ref );
				}
			}
		}
	}
	// Far to near, so each see-through face lies over what is behind it.
	std::ranges::stable_sort( m_Blended, std::greater<>{}, &TriangleRef_t::m_flDepth );

	const std::size_t nBands = m_OpaqueBands.size();
	auto bin = [ & ]( const std::vector<TriangleRef_t> &refs, std::vector<std::vector<std::uint32_t>> &bands ) {
		// Each worker fills bands from a run of triangles; runs are joined in order afterwards.
		const std::size_t nGrain = std::max<std::size_t>( 4096, refs.size() / ( ThreadCount() * 4 ) + 1 );
		std::vector<std::vector<std::vector<std::uint32_t>>> runs( ( refs.size() + nGrain - 1 ) / nGrain, std::vector<std::vector<std::uint32_t>>( nBands ) );
		ParallelFor( refs.size(), nGrain, [ & ]( std::size_t nBegin, std::size_t nEnd ) {
			std::vector<std::vector<std::uint32_t>> &run = runs[ nBegin / nGrain ];
			std::array<ClipVertex_t, 3> corners;
			std::array<ClipVertex_t, 4> clipped;
			for ( std::size_t i = nBegin; i < nEnd; i++ )
			{
				if ( !Corners( refs[ i ], corners ) )
				{
					continue;
				}
				const int nCount = ClipNear( corners, clipped );
				if ( nCount < 3 )
				{
					continue;
				}
				double flTop = 1e300;
				double flBottom = -1e300;
				double flLeft = 1e300;
				double flRight = -1e300;
				for ( int k = 0; k < nCount; k++ )
				{
					const ScreenVertex_t s = ToScreen( clipped[ static_cast<std::size_t>( k ) ], m_Out.m_nWidth, m_Out.m_nHeight );
					flTop = std::min( flTop, s.m_flY );
					flBottom = std::max( flBottom, s.m_flY );
					flLeft = std::min( flLeft, s.m_flX );
					flRight = std::max( flRight, s.m_flX );
				}
				if ( flRight < 0.0 || flLeft > m_Out.m_nWidth || flBottom < 0.0 || flTop > m_Out.m_nHeight )
				{
					continue;
				}
				const int nFirst = std::clamp( static_cast<int>( std::floor( flTop ) ) / m_nBandHeight, 0, static_cast<int>( nBands ) - 1 );
				const int nLast = std::clamp( static_cast<int>( std::floor( flBottom ) ) / m_nBandHeight, 0, static_cast<int>( nBands ) - 1 );
				for ( int b = nFirst; b <= nLast; b++ )
				{
					run[ static_cast<std::size_t>( b ) ].push_back( static_cast<std::uint32_t>( i ) );
				}
			}
		} );
		for ( std::size_t b = 0; b < nBands; b++ )
		{
			for ( const std::vector<std::vector<std::uint32_t>> &run : runs )
			{
				bands[ b ].insert( bands[ b ].end(), run[ b ].begin(), run[ b ].end() );
			}
		}
	};
	bin( m_Opaque, m_OpaqueBands );
	bin( m_Blended, m_BlendedBands );
}

//-----------------------------------------------------------------------------
// Purpose: Clips a triangle at the near plane and fills what's left within rows top to bottom.
//-----------------------------------------------------------------------------
void CRasterizer::DrawTriangle( const TriangleRef_t &ref, int nTop, int nBottom, bool bBlend )
{
	std::array<ClipVertex_t, 3> corners;
	if ( !Corners( ref, corners ) )
	{
		return;
	}
	std::array<ClipVertex_t, 4> clipped;
	const int nCount = ClipNear( corners, clipped );
	if ( nCount < 3 )
	{
		return;
	}
	const CMapMesh &mesh = *m_Scene.m_Meshes[ ref.m_nMesh ];
	const std::uint32_t nObject = mesh.m_Vertices[ mesh.m_Indices[ ref.m_nTriangle * 3 ] ].m_nObject;
	std::array<ScreenVertex_t, 4> screen;
	for ( int k = 0; k < nCount; k++ )
	{
		screen[ static_cast<std::size_t>( k ) ] = ToScreen( clipped[ static_cast<std::size_t>( k ) ], m_Out.m_nWidth, m_Out.m_nHeight );
	}
	for ( int k = 1; k + 1 < nCount; k++ )
	{
		FillTriangle( screen[ 0 ], screen[ static_cast<std::size_t>( k ) ], screen[ static_cast<std::size_t>( k + 1 ) ], ref, nObject, nTop, nBottom, bBlend );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Pixels whose centres are inside (with the top-left rule for edges), depth tested.
//-----------------------------------------------------------------------------
void CRasterizer::FillTriangle( const ScreenVertex_t &a, const ScreenVertex_t &bIn, const ScreenVertex_t &cIn, const TriangleRef_t &ref, std::uint32_t nObject, int nTop, int nBottom, bool bBlend )
{
	const ScreenVertex_t *pB = &bIn;
	const ScreenVertex_t *pC = &cIn;
	double flArea = Edge( a, *pB, pC->m_flX, pC->m_flY );
	if ( flArea == 0.0 )
	{
		return;
	}
	if ( flArea < 0.0 )
	{
		std::swap( pB, pC );
		flArea = -flArea;
	}
	const ScreenVertex_t &b = *pB;
	const ScreenVertex_t &c = *pC;
	const int nX0 = std::max( 0, static_cast<int>( std::floor( std::min( { a.m_flX, b.m_flX, c.m_flX } ) ) ) );
	const int nX1 = std::min( m_Out.m_nWidth - 1, static_cast<int>( std::ceil( std::max( { a.m_flX, b.m_flX, c.m_flX } ) ) ) );
	const int nY0 = std::max( nTop, static_cast<int>( std::floor( std::min( { a.m_flY, b.m_flY, c.m_flY } ) ) ) );
	const int nY1 = std::min( nBottom - 1, static_cast<int>( std::ceil( std::max( { a.m_flY, b.m_flY, c.m_flY } ) ) ) );
	if ( nX0 > nX1 || nY0 > nY1 )
	{
		return;
	}
	// D3D's and GL's rule: a pixel on an edge belongs to the triangle only for top and left edges.
	auto owns = []( const ScreenVertex_t &from, const ScreenVertex_t &to ) {
		const double dx = to.m_flX - from.m_flX;
		const double dy = to.m_flY - from.m_flY;
		return ( dy == 0.0 && dx > 0.0 ) || dy < 0.0;
	};
	const bool bOwnA = owns( b, c );
	const bool bOwnB = owns( c, a );
	const bool bOwnC = owns( a, b );

	const RasterMaterial_t *pMaterial = MaterialOf( ref.m_nMaterial );
	static const ShadeMaterial_t FALLBACK;
	const ShadeMaterial_t &material = pMaterial ? pMaterial->m_Shade : FALLBACK;
	const std::uint8_t nFlags = FlagsOf( nObject );
	ShadeInput_t input;
	input.m_bSelected = ( nFlags & RASTER_OBJECT_SELECTED ) != 0;
	input.m_bInstance = ( nFlags & RASTER_OBJECT_INSTANCE ) != 0;
	input.m_bInvalid = ( nFlags & RASTER_OBJECT_INVALID ) != 0;
	if ( nObject < m_Scene.m_ObjectColors.size() )
	{
		const auto &color = m_Scene.m_ObjectColors[ nObject ];
		input.m_vecObjectColor = { color[ 0 ] / 255.0f, color[ 1 ] / 255.0f, color[ 2 ] / 255.0f };
	}
	ShadeOptions_t shade = m_Options.m_Shade;
	if ( pMaterial && pMaterial->m_bObjectColor )
	{
		shade.m_nMode = RasterMode_t::RASTER_MODE_FLAT;
	}
	// The mip level is picked per pixel, as a graphics card picks it: from how fast the texel
	// coordinates change across the screen, in the faster of the two directions. Texel over w
	// and 1/w change by the same amounts at every pixel of the triangle.
	const bool bMips = material.m_pBase && shade.m_nMode != RasterMode_t::RASTER_MODE_FLAT && shade.m_nMode != RasterMode_t::RASTER_MODE_WIREFRAME;
	const auto step = [ flArea ]( const ScreenVertex_t &from, const ScreenVertex_t &to, double dx, double dy ) {
		return ( Edge( from, to, dx, dy ) - Edge( from, to, 0.0, 0.0 ) ) / flArea;
	};
	const std::array<double, 3> stepX{ step( b, c, 1.0, 0.0 ), step( c, a, 1.0, 0.0 ), step( a, b, 1.0, 0.0 ) };
	const std::array<double, 3> stepY{ step( b, c, 0.0, 1.0 ), step( c, a, 0.0, 1.0 ), step( a, b, 0.0, 1.0 ) };
	const auto change = [ &a, &b, &c ]( const std::array<double, 3> &weights, auto pfnValue ) {
		return weights[ 0 ] * pfnValue( a ) + weights[ 1 ] * pfnValue( b ) + weights[ 2 ] * pfnValue( c );
	};
	const auto over = []( std::size_t i ) { return [ i ]( const ScreenVertex_t &v ) { return v.m_Over[ i ]; }; };
	const auto invW = []( const ScreenVertex_t &v ) { return v.m_flInvW; };
	const double flUx = change( stepX, over( 0 ) );
	const double flVx = change( stepX, over( 1 ) );
	const double flWx = change( stepX, invW );
	const double flUy = change( stepY, over( 0 ) );
	const double flVy = change( stepY, over( 1 ) );
	const double flWy = change( stepY, invW );
	for ( int y = nY0; y <= nY1; y++ )
	{
		const double flY = y + 0.5;
		for ( int x = nX0; x <= nX1; x++ )
		{
			const double flX = x + 0.5;
			const double w0 = Edge( b, c, flX, flY );
			const double w1 = Edge( c, a, flX, flY );
			const double w2 = Edge( a, b, flX, flY );
			if ( w0 < 0.0 || w1 < 0.0 || w2 < 0.0 || ( w0 == 0.0 && !bOwnA ) || ( w1 == 0.0 && !bOwnB ) || ( w2 == 0.0 && !bOwnC ) )
			{
				continue;
			}
			const double l0 = w0 / flArea;
			const double l1 = w1 / flArea;
			const double l2 = w2 / flArea;
			const double flZ = l0 * a.m_flZ + l1 * b.m_flZ + l2 * c.m_flZ;
			const std::size_t nPixel = static_cast<std::size_t>( y ) * static_cast<std::size_t>( m_Out.m_nWidth ) + static_cast<std::size_t>( x );
			float &flDepth = m_Depth[ nPixel ];
			if ( !( flZ < flDepth ) || flZ < 0.0 || flZ > 1.0 )
			{
				continue;
			}
			if ( m_pSkyMask && !m_bSkyPass && material.m_bSky && ( *m_pSkyMask )[ nPixel ] )
			{
				// The 3D skybox shows here: the sky face only hides what is behind it.
				flDepth = static_cast<float>( flZ );
				continue;
			}
			const double flInvW = l0 * a.m_flInvW + l1 * b.m_flInvW + l2 * c.m_flInvW;
			std::array<double, 15> values{};
			for ( std::size_t i = 0; i < values.size(); i++ )
			{
				values[ i ] = ( l0 * a.m_Over[ i ] + l1 * b.m_Over[ i ] + l2 * c.m_Over[ i ] ) / flInvW;
			}
			input.m_Texel = { static_cast<float>( values[ 0 ] ), static_cast<float>( values[ 1 ] ) };
			if ( bMips )
			{
				const double flDuDx = ( flUx - values[ 0 ] * flWx ) / flInvW;
				const double flDvDx = ( flVx - values[ 1 ] * flWx ) / flInvW;
				const double flDuDy = ( flUy - values[ 0 ] * flWy ) / flInvW;
				const double flDvDy = ( flVy - values[ 1 ] * flWy ) / flInvW;
				const double flRho = std::max( std::hypot( flDuDx, flDvDx ), std::hypot( flDuDy, flDvDy ) );
				input.m_flLod = flRho > 1.0 ? static_cast<float>( std::log2( flRho ) ) : 0.0f;
				input.m_Gradient = { static_cast<float>( flDuDx ), static_cast<float>( flDvDx ), static_cast<float>( flDuDy ), static_cast<float>( flDvDy ) };
			}
			input.m_Luxel = { static_cast<float>( values[ 2 ] ), static_cast<float>( values[ 3 ] ) };
			input.m_flBlend = static_cast<float>( values[ 4 ] );
			input.m_Light = { static_cast<float>( values[ 11 ] ), static_cast<float>( values[ 12 ] ), static_cast<float>( values[ 13 ] ), static_cast<float>( values[ 14 ] ) };
			input.m_vecNormal = Normalized( CVec3{ static_cast<float>( values[ 5 ] ), static_cast<float>( values[ 6 ] ), static_cast<float>( values[ 7 ] ) } );
			input.m_vecDirection = CVec3{ static_cast<float>( values[ 8 ] - m_Camera.m_Eye[ 0 ] ), static_cast<float>( values[ 9 ] - m_Camera.m_Eye[ 1 ] ), static_cast<float>( values[ 10 ] - m_Camera.m_Eye[ 2 ] ) };
			const std::optional<std::array<float, 4>> color = ShadeFragment( material, input, shade );
			if ( !color )
			{
				continue;
			}
			std::uint8_t *p = m_Out.Pixel( x, y );
			if ( bBlend )
			{
				const float flAlpha = std::clamp( ( *color )[ 3 ], 0.0f, 1.0f );
				for ( std::size_t i = 0; i < 3; i++ )
				{
					const float flSource = std::clamp( ( *color )[ i ], 0.0f, 1.0f ) * 255.0f;
					const float flResult = material.m_bAdditive ? p[ i ] + flSource * flAlpha : p[ i ] + ( flSource - p[ i ] ) * flAlpha;
					p[ i ] = static_cast<std::uint8_t>( std::clamp( flResult + 0.5f, 0.0f, 255.0f ) );
				}
			}
			else
			{
				flDepth = static_cast<float>( flZ );
				for ( std::size_t i = 0; i < 3; i++ )
				{
					p[ i ] = static_cast<std::uint8_t>( std::clamp( ( *color )[ i ] * 255.0f + 0.5f, 0.0f, 255.0f ) );
				}
			}
			p[ 3 ] = 255;
			if ( m_bSkyPass )
			{
				( *m_pSkyMask )[ nPixel ] = 1;
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: A one-pixel line to draw, clipped at the near plane; depth tested unless bOnTop.
//-----------------------------------------------------------------------------
void CRasterizer::AddLine( Point_t from, Point_t to, std::array<std::uint8_t, 3> color, bool bOnTop )
{
	std::array<double, 4> a = Transform( m_Matrix, ToVec3( from ) );
	std::array<double, 4> b = Transform( m_Matrix, ToVec3( to ) );
	const double flA = a[ 2 ] + a[ 3 ];
	const double flB = b[ 2 ] + b[ 3 ];
	if ( flA < 0.0 && flB < 0.0 )
	{
		return;
	}
	if ( flA < 0.0 || flB < 0.0 )
	{
		const double t = flA / ( flA - flB );
		std::array<double, 4> &behind = flA < 0.0 ? a : b;
		std::array<double, 4> cut{};
		for ( std::size_t i = 0; i < 4; i++ )
		{
			cut[ i ] = a[ i ] + ( b[ i ] - a[ i ] ) * t;
		}
		behind = cut;
	}
	auto screen = [ & ]( const std::array<double, 4> &c ) {
		return std::array<double, 3>{ ( c[ 0 ] / c[ 3 ] + 1.0 ) * 0.5 * m_Out.m_nWidth, ( 1.0 - c[ 1 ] / c[ 3 ] ) * 0.5 * m_Out.m_nHeight, c[ 2 ] / c[ 3 ] * 0.5 + 0.5 };
	};
	ScreenLine_t line{ screen( a ), screen( b ), color, bOnTop };
	if ( std::isfinite( line.m_From[ 0 ] ) && std::isfinite( line.m_From[ 1 ] ) && std::isfinite( line.m_To[ 0 ] ) && std::isfinite( line.m_To[ 1 ] ) )
	{
		m_Lines.push_back( line );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The lines added, a pixel a step, in the order they were added. Each band of rows is
//          drawn on its own thread, and each line only where it crosses the band.
//-----------------------------------------------------------------------------
void CRasterizer::DrawLines()
{
	// The part of a line (as 0-1 along it) within x0-x1 and y0-y1, if any.
	auto within = []( const ScreenLine_t &line, double x0, double x1, double y0, double y1 ) -> std::optional<std::array<double, 2>> {
		double t0 = 0.0;
		double t1 = 1.0;
		const double dx = line.m_To[ 0 ] - line.m_From[ 0 ];
		const double dy = line.m_To[ 1 ] - line.m_From[ 1 ];
		for ( const auto &[ flP, flQ ] : { std::pair{ -dx, line.m_From[ 0 ] - x0 }, std::pair{ dx, x1 - line.m_From[ 0 ] }, std::pair{ -dy, line.m_From[ 1 ] - y0 }, std::pair{ dy, y1 - line.m_From[ 1 ] } } )
		{
			if ( flP == 0.0 )
			{
				if ( flQ < 0.0 )
				{
					return std::nullopt;
				}
				continue;
			}
			const double t = flQ / flP;
			( flP < 0.0 ? t0 : t1 ) = flP < 0.0 ? std::max( t0, t ) : std::min( t1, t );
		}
		return t0 <= t1 ? std::optional<std::array<double, 2>>{ { t0, t1 } } : std::nullopt;
	};
	const std::size_t nBands = m_OpaqueBands.size();
	std::vector<std::vector<std::uint32_t>> bands( nBands );
	for ( std::uint32_t i = 0; i < m_Lines.size(); i++ )
	{
		const ScreenLine_t &line = m_Lines[ i ];
		const auto range = within( line, -1.0, m_Out.m_nWidth + 1.0, -1.0, m_Out.m_nHeight + 1.0 );
		if ( !range )
		{
			continue;
		}
		const double flY0 = line.m_From[ 1 ] + ( line.m_To[ 1 ] - line.m_From[ 1 ] ) * ( *range )[ 0 ];
		const double flY1 = line.m_From[ 1 ] + ( line.m_To[ 1 ] - line.m_From[ 1 ] ) * ( *range )[ 1 ];
		const int nFirst = std::clamp( static_cast<int>( std::floor( std::min( flY0, flY1 ) ) ) / m_nBandHeight, 0, static_cast<int>( nBands ) - 1 );
		const int nLast = std::clamp( static_cast<int>( std::floor( std::max( flY0, flY1 ) ) ) / m_nBandHeight, 0, static_cast<int>( nBands ) - 1 );
		for ( int b = nFirst; b <= nLast; b++ )
		{
			bands[ static_cast<std::size_t>( b ) ].push_back( i );
		}
	}
	ParallelFor( nBands, 1, [ & ]( std::size_t nBegin, std::size_t nEnd ) {
		for ( std::size_t band = nBegin; band < nEnd; band++ )
		{
			const int nTop = static_cast<int>( band ) * m_nBandHeight;
			const int nBottom = std::min( m_Out.m_nHeight, nTop + m_nBandHeight );
			for ( const std::uint32_t i : bands[ band ] )
			{
				const ScreenLine_t &line = m_Lines[ i ];
				const std::array<double, 3> &p0 = line.m_From;
				const std::array<double, 3> &p1 = line.m_To;
				const auto range = within( line, -1.0, m_Out.m_nWidth + 1.0, nTop - 1.0, nBottom + 1.0 );
				if ( !range )
				{
					continue;
				}
				// A step a pixel along the whole line, taking only the steps in this band.
				const double flSteps = std::max( 1.0, std::ceil( std::max( std::abs( p1[ 0 ] - p0[ 0 ] ), std::abs( p1[ 1 ] - p0[ 1 ] ) ) ) );
				const auto nFirst = static_cast<long long>( std::floor( ( *range )[ 0 ] * flSteps ) );
				const auto nLast = static_cast<long long>( std::ceil( ( *range )[ 1 ] * flSteps ) );
				for ( long long n = nFirst; n <= nLast; n++ )
				{
					const double t = static_cast<double>( n ) / flSteps;
					const int x = static_cast<int>( std::floor( p0[ 0 ] + ( p1[ 0 ] - p0[ 0 ] ) * t ) );
					const int y = static_cast<int>( std::floor( p0[ 1 ] + ( p1[ 1 ] - p0[ 1 ] ) * t ) );
					if ( x < 0 || y < nTop || x >= m_Out.m_nWidth || y >= nBottom )
					{
						continue;
					}
					const double flZ = p0[ 2 ] + ( p1[ 2 ] - p0[ 2 ] ) * t;
					// Lines sit on faces; a little depth pulls them in front of their own.
					if ( !line.m_bOnTop && flZ - 1e-5 > m_Depth[ static_cast<std::size_t>( y ) * static_cast<std::size_t>( m_Out.m_nWidth ) + static_cast<std::size_t>( x ) ] )
					{
						continue;
					}
					std::uint8_t *p = m_Out.Pixel( x, y );
					p[ 0 ] = line.m_Color[ 0 ];
					p[ 1 ] = line.m_Color[ 1 ];
					p[ 2 ] = line.m_Color[ 2 ];
					p[ 3 ] = 255;
				}
			}
		}
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CRasterizer::Run()
{
	m_Matrix = m_Camera.GetViewProjection();
	const std::size_t nPixels = static_cast<std::size_t>( m_Out.m_nWidth ) * static_cast<std::size_t>( m_Out.m_nHeight );
	// The world's pass after the 3D skybox draws over its picture.
	const bool bOverSky = m_pSkyMask && !m_bSkyPass;
	if ( !bOverSky )
	{
		for ( std::size_t i = 0; i < nPixels; i++ )
		{
			m_Out.m_Pixels[ i * 4 ] = m_Options.m_Background[ 0 ];
			m_Out.m_Pixels[ i * 4 + 1 ] = m_Options.m_Background[ 1 ];
			m_Out.m_Pixels[ i * 4 + 2 ] = m_Options.m_Background[ 2 ];
			m_Out.m_Pixels[ i * 4 + 3 ] = m_Options.m_bTransparent ? 0 : 255;
		}
	}
	m_Depth.assign( nPixels, 1.0f );
	const std::size_t nBands = std::clamp<std::size_t>( ThreadCount() * 4, 1, static_cast<std::size_t>( std::max( m_Out.m_nHeight, 1 ) ) );
	m_nBandHeight = std::max( 1, ( m_Out.m_nHeight + static_cast<int>( nBands ) - 1 ) / static_cast<int>( nBands ) );
	m_OpaqueBands.assign( nBands, {} );
	m_BlendedBands.assign( nBands, {} );
	Gather();
	ParallelFor( nBands, 1, [ & ]( std::size_t nBegin, std::size_t nEnd ) {
		for ( std::size_t band = nBegin; band < nEnd; band++ )
		{
			const int nTop = static_cast<int>( band ) * m_nBandHeight;
			const int nBottom = std::min( m_Out.m_nHeight, nTop + m_nBandHeight );
			for ( const std::uint32_t i : m_OpaqueBands[ band ] )
			{
				DrawTriangle( m_Opaque[ i ], nTop, nBottom, false );
			}
			for ( const std::uint32_t i : m_BlendedBands[ band ] )
			{
				DrawTriangle( m_Blended[ i ], nTop, nBottom, true );
			}
		}
	} );

	if ( m_bSkyPass )
	{
		return;
	}
	// Wireframe: every shown object's edges in its colour, through each other as in Hammer.
	const bool bWireframe = m_Options.m_Shade.m_nMode == RasterMode_t::RASTER_MODE_WIREFRAME;
	for ( const CMapMesh *pMesh : m_Scene.m_Meshes )
	{
		if ( m_Scene.m_NoWireframe.contains( pMesh ) )
		{
			continue;
		}
		for ( std::size_t i = 0; i + 1 < pMesh->m_Lines.size(); i += 2 )
		{
			const MeshVertex_t &a = pMesh->m_Vertices[ pMesh->m_Lines[ i ] ];
			const MeshVertex_t &b = pMesh->m_Vertices[ pMesh->m_Lines[ i + 1 ] ];
			const std::uint8_t nFlags = FlagsOf( a.m_nObject );
			if ( !IsDrawn( m_Scene, pMesh, nFlags ) )
			{
				continue;
			}
			// Selected and broken solids' edges come later, over everything.
			const bool bLater = ( nFlags & RASTER_OBJECT_INVALID ) != 0 || ( m_Options.m_bSelectionEdges && IsSelectionEdge( m_Scene, pMesh, nFlags ) );
			if ( bWireframe && !bLater )
			{
				const std::array<std::uint8_t, 3> color = a.m_nObject < m_Scene.m_LineColors.size() ? m_Scene.m_LineColors[ a.m_nObject ] : std::array<std::uint8_t, 3>{ 255, 255, 255 };
				AddLine( ToPoint( a.m_vecPosition ), ToPoint( b.m_vecPosition ), color, true );
			}
		}
	}
	for ( const RasterLine_t &line : m_Scene.m_Lines )
	{
		if ( !line.m_bOnTop )
		{
			AddLine( line.m_From, line.m_To, line.m_Color, false );
		}
	}
	// Broken solids' edges, then the selection's, over everything (Hammer++'s DrawInvalidSolid
	// and SelectionNoZ).
	for ( const CMapMesh *pMesh : m_Scene.m_Meshes )
	{
		for ( std::size_t i = 0; i + 1 < pMesh->m_Lines.size(); i += 2 )
		{
			const MeshVertex_t &a = pMesh->m_Vertices[ pMesh->m_Lines[ i ] ];
			const std::uint8_t nFlags = FlagsOf( a.m_nObject );
			const bool bSelected = m_Options.m_bSelectionEdges && IsSelectionEdge( m_Scene, pMesh, nFlags );
			if ( IsDrawn( m_Scene, pMesh, nFlags ) && ( nFlags & RASTER_OBJECT_INVALID ) != 0 && !bSelected )
			{
				AddLine( ToPoint( a.m_vecPosition ), ToPoint( pMesh->m_Vertices[ pMesh->m_Lines[ i + 1 ] ].m_vecPosition ), m_Options.m_InvalidEdge, true );
			}
		}
	}
	for ( const CMapMesh *pMesh : m_Scene.m_Meshes )
	{
		for ( std::size_t i = 0; m_Options.m_bSelectionEdges && i + 1 < pMesh->m_Lines.size(); i += 2 )
		{
			const MeshVertex_t &a = pMesh->m_Vertices[ pMesh->m_Lines[ i ] ];
			const std::uint8_t nFlags = FlagsOf( a.m_nObject );
			if ( IsDrawn( m_Scene, pMesh, nFlags ) && IsSelectionEdge( m_Scene, pMesh, nFlags ) )
			{
				AddLine( ToPoint( a.m_vecPosition ), ToPoint( pMesh->m_Vertices[ pMesh->m_Lines[ i + 1 ] ].m_vecPosition ), bWireframe ? std::array<std::uint8_t, 3>{ 255, 0, 0 } : m_Options.m_SelectionEdge, true );
			}
		}
	}
	for ( const RasterLine_t &line : m_Scene.m_Lines )
	{
		if ( line.m_bOnTop )
		{
			AddLine( line.m_From, line.m_To, line.m_Color, true );
		}
	}
	DrawLines();
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void RenderView( const RasterScene_t &scene, const CViewCamera &camera, const RasterOptions_t &options, texture::CImage &out )
{
	if ( out.m_nWidth != camera.m_nWidth || out.m_nHeight != camera.m_nHeight )
	{
		out = texture::CImage{ camera.m_nWidth, camera.m_nHeight };
	}
	if ( out.Empty() )
	{
		return;
	}
	const RasterMode_t nMode = options.m_Shade.m_nMode;
	if ( options.m_SkyboxCamera && nMode != RasterMode_t::RASTER_MODE_FLAT && nMode != RasterMode_t::RASTER_MODE_WIREFRAME )
	{
		std::vector<std::uint8_t> mask( static_cast<std::size_t>( out.m_nWidth ) * static_cast<std::size_t>( out.m_nHeight ), 0 );
		CViewCamera sky = *options.m_SkyboxCamera;
		sky.m_nWidth = camera.m_nWidth;
		sky.m_nHeight = camera.m_nHeight;
		CRasterizer{ scene, sky, options, out, &mask, true }.Run();
		CRasterizer{ scene, camera, options, out, &mask, false }.Run();
		return;
	}
	CRasterizer{ scene, camera, options, out }.Run();
}

} // namespace xv::map
