//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Faces.h. Source BSP format facts: planes 20 bytes, texdata 32, texinfo 72
//          (texture axes, lightmap axes, flags, texdata), faces 56, models 48, dispinfo 176,
//          displacement vertices 20; lightmaps as ColorRGBExp32, (width + 1) by (height + 1)
//          luxels from the face's lightmap mins, the flat one first.
//
//===========================================================================//
#include "Faces.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <cstring>
#include <unordered_map>

#include "bsp/Bsp.h"
#include "bsp/Decompile.h"
#include "map/Displacement.h"

namespace xv::bsp
{

namespace
{

using map::Point_t;

constexpr int LUMP_PLANES = 1;
constexpr int LUMP_TEXDATA = 2;
constexpr int LUMP_VERTEXES = 3;
constexpr int LUMP_TEXINFO = 6;
constexpr int LUMP_FACES = 7;
constexpr int LUMP_LIGHTING = 8;
constexpr int LUMP_EDGES = 12;
constexpr int LUMP_SURFEDGES = 13;
constexpr int LUMP_MODELS = 14;
constexpr int LUMP_DISPINFO = 26;
constexpr int LUMP_DISP_VERTS = 33;
constexpr int LUMP_NODES = 5;
constexpr int LUMP_LEAFS = 10;
constexpr int LUMP_LEAF_AMBIENT_INDEX_HDR = 51;
constexpr int LUMP_LEAF_AMBIENT_INDEX = 52;
constexpr int LUMP_LEAF_AMBIENT_LIGHTING_HDR = 55;
constexpr int LUMP_LEAF_AMBIENT_LIGHTING = 56;
constexpr int LUMP_TEXDATA_STRING_DATA = 43;
constexpr int LUMP_TEXDATA_STRING_TABLE = 44;
constexpr int LUMP_LIGHTING_HDR = 53;
constexpr int LUMP_FACES_HDR = 58;
constexpr int LUMP_VISIBILITY = 4;
constexpr int LUMP_LEAFFACES = 16;

constexpr std::size_t PLANE_SIZE = 20;
constexpr std::size_t TEXDATA_SIZE = 32;
constexpr std::size_t TEXINFO_SIZE = 72;
constexpr std::size_t FACE_SIZE = 56;
constexpr std::size_t MODEL_SIZE = 48;
constexpr std::size_t DISPINFO_SIZE = 176;
constexpr std::size_t DISP_VERT_SIZE = 20;
constexpr std::size_t NODE_SIZE = 32;
constexpr std::size_t LEAF_SIZE = 32;
constexpr std::size_t AMBIENT_INDEX_SIZE = 4;
constexpr std::size_t AMBIENT_SAMPLE_SIZE = 28; // six colours, then where in the leaf
constexpr int TREE_DEPTH = 4096;

constexpr std::uint32_t SURF_NODRAW = 0x80;
constexpr std::uint32_t SURF_HINT = 0x100;
constexpr std::uint32_t SURF_SKIP = 0x200;
constexpr std::uint32_t SURF_NOLIGHT = 0x400;
constexpr std::uint8_t NO_STYLE = 255;
constexpr double CHUNK_SIZE = 1024.0;
constexpr int MAX_ATLAS_SIDE = 16384;

//-----------------------------------------------------------------------------
// Purpose: A value at a byte offset of a lump; zero past its end.
//-----------------------------------------------------------------------------
template <typename T>
T ReadAt( const std::vector<std::byte> &bytes, std::size_t nAt )
{
	T value{};
	if ( nAt + sizeof( T ) <= bytes.size() )
	{
		std::memcpy( &value, bytes.data() + nAt, sizeof( T ) );
	}
	return value;
}

Point_t ReadPoint( const std::vector<std::byte> &bytes, std::size_t nAt )
{
	return { ReadAt<float>( bytes, nAt ), ReadAt<float>( bytes, nAt + 4 ), ReadAt<float>( bytes, nAt + 8 ) };
}

// A face as read, before it is drawn.
struct Face_t
{
	int m_nModel = 0;
	std::size_t m_nFace = 0;
	std::vector<Point_t> m_Points; // wound clockwise seen from the front, as the map's meshes are
	map::Plane_t m_Plane;
	int m_nTexInfo = -1;
	int m_nDisp = -1;
	int m_nLightOffset = -1;
	std::array<int, 2> m_LuxelMins{};
	std::array<int, 2> m_LuxelSize{};
	bool m_bLit = false;
	int m_nRect = -1; // its place in the atlas
};

struct Rect_t
{
	int m_nX = 0;
	int m_nY = 0;
	int m_nWidth = 0;
	int m_nHeight = 0;
};

//-----------------------------------------------------------------------------
// Purpose: Rectangles in rows: tallest first, each row as tall as its first. False when they
//          won't fit within the most a texture may be.
//-----------------------------------------------------------------------------
bool PackRects( std::vector<Rect_t> &rects, int nWidth, int &nHeight )
{
	std::vector<std::size_t> order( rects.size() );
	for ( std::size_t i = 0; i < order.size(); i++ )
	{
		order[ i ] = i;
	}
	std::ranges::stable_sort( order, [ & ]( std::size_t a, std::size_t b ) { return rects[ a ].m_nHeight > rects[ b ].m_nHeight; } );
	int nX = 0;
	int nY = 0;
	int nRow = 0;
	for ( const std::size_t i : order )
	{
		Rect_t &rect = rects[ i ];
		if ( rect.m_nWidth > nWidth )
		{
			return false;
		}
		if ( nX + rect.m_nWidth > nWidth )
		{
			nY += nRow;
			nX = 0;
			nRow = 0;
		}
		rect.m_nX = nX;
		rect.m_nY = nY;
		nX += rect.m_nWidth;
		nRow = std::max( nRow, rect.m_nHeight );
	}
	nHeight = std::max( 1, nY + nRow );
	return nHeight <= MAX_ATLAS_SIDE;
}

//-----------------------------------------------------------------------------
// Purpose: A ColorRGBExp32 as linear light over 255.
//-----------------------------------------------------------------------------
CVec3 ReadLight( const std::vector<std::byte> &bytes, std::size_t nAt )
{
	const int nExponent = static_cast<std::int8_t>( ReadAt<std::uint8_t>( bytes, nAt + 3 ) );
	const auto channel = [ & ]( std::size_t k ) { return static_cast<float>( std::ldexp( static_cast<double>( ReadAt<std::uint8_t>( bytes, nAt + k ) ), nExponent ) / 255.0 ); };
	return { channel( 0 ), channel( 1 ), channel( 2 ) };
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Each leaf's cluster and faces gathered by cluster; the visibility sets stay
//          compressed until one is asked for.
//-----------------------------------------------------------------------------
CVisibility::CVisibility( std::vector<std::byte> planes, std::vector<std::byte> nodes, std::vector<std::byte> leafs, const std::vector<std::byte> &leafFaces, std::vector<std::byte> visibility, int nHeadNode, std::size_t nFaces )
	: m_Planes( std::move( planes ) ),
	  m_Nodes( std::move( nodes ) ),
	  m_Leafs( std::move( leafs ) ),
	  m_Visibility( std::move( visibility ) ),
	  m_nHeadNode( nHeadNode ),
	  m_nFaces( nFaces )
{
	if ( m_Visibility.size() < 4 )
		return;
	const int nClusters = ReadAt<std::int32_t>( m_Visibility, 0 );
	if ( nClusters <= 0 || static_cast<std::size_t>( nClusters ) * 8 + 4 > m_Visibility.size() )
		return;
	m_nClusters = nClusters;
	m_ClusterFaces.resize( static_cast<std::size_t>( nClusters ) );
	const std::size_t nLeafs = m_Leafs.size() / LEAF_SIZE;
	for ( std::size_t nLeaf = 0; nLeaf < nLeafs; nLeaf++ )
	{
		const int nCluster = ReadAt<std::int16_t>( m_Leafs, nLeaf * LEAF_SIZE + 4 );
		if ( nCluster < 0 || nCluster >= nClusters )
			continue;
		const std::size_t nFirst = ReadAt<std::uint16_t>( m_Leafs, nLeaf * LEAF_SIZE + 20 );
		const std::size_t nCount = ReadAt<std::uint16_t>( m_Leafs, nLeaf * LEAF_SIZE + 22 );
		std::vector<std::uint32_t> &faces = m_ClusterFaces[ static_cast<std::size_t>( nCluster ) ];
		for ( std::size_t i = nFirst; i < nFirst + nCount && ( i + 1 ) * 2 <= leafFaces.size(); i++ )
			faces.push_back( ReadAt<std::uint16_t>( leafFaces, i * 2 ) );
	}
	for ( std::vector<std::uint32_t> &faces : m_ClusterFaces )
	{
		std::ranges::sort( faces );
		faces.erase( std::unique( faces.begin(), faces.end() ), faces.end() );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Down the tree as CAmbientLighting does; the leaf's cluster.
//-----------------------------------------------------------------------------
int CVisibility::ClusterAt( map::Point_t point ) const
{
	int nNode = m_nHeadNode;
	for ( int nDepth = 0; nNode >= 0; nDepth++ )
	{
		const std::size_t nAt = static_cast<std::size_t>( nNode ) * NODE_SIZE;
		if ( nDepth > TREE_DEPTH || nAt + NODE_SIZE > m_Nodes.size() )
			return -1;
		const std::size_t nPlane = static_cast<std::size_t>( ReadAt<std::int32_t>( m_Nodes, nAt ) );
		if ( ( nPlane + 1 ) * PLANE_SIZE > m_Planes.size() )
			return -1;
		const double flSide = map::Dot( ReadPoint( m_Planes, nPlane * PLANE_SIZE ), point ) - ReadAt<float>( m_Planes, nPlane * PLANE_SIZE + 12 );
		nNode = ReadAt<std::int32_t>( m_Nodes, nAt + ( flSide >= 0.0 ? 4 : 8 ) );
	}
	const std::size_t nLeaf = static_cast<std::size_t>( -1 - nNode );
	if ( ( nLeaf + 1 ) * LEAF_SIZE > m_Leafs.size() )
		return -1;
	return ReadAt<std::int16_t>( m_Leafs, nLeaf * LEAF_SIZE + 4 );
}

//-----------------------------------------------------------------------------
// Purpose: The cluster's set unpacked (a zero byte, then how many zero bytes it stands for),
//          then every face of every cluster in it. A cluster outside the map's (in solid, or
//          the map not vised) sees everything.
//-----------------------------------------------------------------------------
std::vector<bool> CVisibility::VisibleFaces( int nCluster ) const
{
	std::vector<bool> visible( m_nFaces, nCluster < 0 || nCluster >= m_nClusters );
	if ( nCluster < 0 || nCluster >= m_nClusters )
		return visible;
	std::size_t nAt = static_cast<std::size_t>( ReadAt<std::int32_t>( m_Visibility, 4 + static_cast<std::size_t>( nCluster ) * 8 ) );
	for ( int nOther = 0; nOther < m_nClusters && nAt < m_Visibility.size(); )
	{
		const auto nByte = static_cast<std::uint8_t>( m_Visibility[ nAt++ ] );
		if ( nByte == 0 )
		{
			const int nZeros = nAt < m_Visibility.size() ? static_cast<std::uint8_t>( m_Visibility[ nAt++ ] ) : 0;
			nOther += nZeros * 8;
			if ( nZeros == 0 )
				break;
			continue;
		}
		for ( int nBit = 0; nBit < 8 && nOther < m_nClusters; nBit++, nOther++ )
		{
			if ( !( nByte & ( 1u << nBit ) ) )
				continue;
			for ( const std::uint32_t nFace : m_ClusterFaces[ static_cast<std::size_t>( nOther ) ] )
			{
				if ( nFace < visible.size() )
					visible[ nFace ] = true;
			}
		}
	}
	return visible;
}

//-----------------------------------------------------------------------------
// Purpose: Down the world's tree to the leaf the point is in (a node's front child when the
//          point is on or before its plane), then that leaf's sample nearest the point (each
//          sample is at a fraction of the leaf's box, in 255ths).
//-----------------------------------------------------------------------------
std::optional<CAmbientLighting::Cube_t> CAmbientLighting::CubeAt( map::Point_t point ) const
{
	int nNode = m_nHeadNode;
	for ( int nDepth = 0; nNode >= 0; nDepth++ )
	{
		const std::size_t nAt = static_cast<std::size_t>( nNode ) * NODE_SIZE;
		if ( nDepth > TREE_DEPTH || nAt + NODE_SIZE > m_Nodes.size() )
		{
			return std::nullopt;
		}
		const std::size_t nPlane = static_cast<std::size_t>( ReadAt<std::int32_t>( m_Nodes, nAt ) );
		const double flSide = map::Dot( ReadPoint( m_Planes, nPlane * PLANE_SIZE ), point ) - ReadAt<float>( m_Planes, nPlane * PLANE_SIZE + 12 );
		nNode = ReadAt<std::int32_t>( m_Nodes, nAt + ( flSide >= 0.0 ? 4 : 8 ) );
	}
	const std::size_t nLeaf = static_cast<std::size_t>( -1 - nNode );
	if ( ( nLeaf + 1 ) * AMBIENT_INDEX_SIZE > m_Index.size() || ( nLeaf + 1 ) * LEAF_SIZE > m_Leafs.size() )
	{
		return std::nullopt;
	}
	const std::size_t nCount = ReadAt<std::uint16_t>( m_Index, nLeaf * AMBIENT_INDEX_SIZE );
	const std::size_t nFirst = ReadAt<std::uint16_t>( m_Index, nLeaf * AMBIENT_INDEX_SIZE + 2 );
	map::Point_t mins{}, maxs{};
	for ( std::size_t a = 0; a < 3; a++ )
	{
		mins[ a ] = ReadAt<std::int16_t>( m_Leafs, nLeaf * LEAF_SIZE + 8 + a * 2 );
		maxs[ a ] = ReadAt<std::int16_t>( m_Leafs, nLeaf * LEAF_SIZE + 14 + a * 2 );
	}
	std::optional<std::size_t> nBest;
	double flBest = 0.0;
	for ( std::size_t i = nFirst; i < nFirst + nCount && ( i + 1 ) * AMBIENT_SAMPLE_SIZE <= m_Samples.size(); i++ )
	{
		double flDistance = 0.0;
		for ( std::size_t a = 0; a < 3; a++ )
		{
			const double flAt = mins[ a ] + ( maxs[ a ] - mins[ a ] ) * ReadAt<std::uint8_t>( m_Samples, i * AMBIENT_SAMPLE_SIZE + 24 + a ) / 255.0;
			flDistance += ( flAt - point[ a ] ) * ( flAt - point[ a ] );
		}
		if ( !nBest || flDistance < flBest )
		{
			nBest = i;
			flBest = flDistance;
		}
	}
	if ( !nBest )
	{
		return std::nullopt;
	}
	Cube_t cube;
	for ( std::size_t k = 0; k < 6; k++ )
	{
		cube[ k ] = ReadLight( m_Samples, *nBest * AMBIENT_SAMPLE_SIZE + k * 4 );
	}
	return cube;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVec3 CAmbientLighting::LightFor( const Cube_t &cube, CVec3 vecNormal )
{
	const float flX = vecNormal.m_flX * vecNormal.m_flX;
	const float flY = vecNormal.m_flY * vecNormal.m_flY;
	const float flZ = vecNormal.m_flZ * vecNormal.m_flZ;
	return cube[ vecNormal.m_flX >= 0.0f ? 0 : 1 ] * flX + cube[ vecNormal.m_flY >= 0.0f ? 2 : 3 ] * flY + cube[ vecNormal.m_flZ >= 0.0f ? 4 : 5 ] * flZ;
}

namespace
{

constexpr double SURFACE_CELL = 256.0;
constexpr double ON_FACE = 2.0; // how far off a face a point may be and still be on it

int CellOf( double flAt )
{
	return static_cast<int>( std::floor( flAt / SURFACE_CELL ) );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Every lit triangle of the world, filed by the cells (256 units a side) its box
//          reaches into.
//-----------------------------------------------------------------------------
CSurfaceLight::CSurfaceLight( std::shared_ptr<const map::CMapMesh> pWorld, std::shared_ptr<const std::vector<map::LightVertex_t>> pLights, std::shared_ptr<const map::CLighting> pAtlas )
	: m_pWorld( std::move( pWorld ) ),
	  m_pLights( std::move( pLights ) ),
	  m_pAtlas( std::move( pAtlas ) )
{
	if ( !m_pWorld || !m_pLights || !m_pAtlas || m_pLights->size() != m_pWorld->m_Vertices.size() )
	{
		return;
	}
	const std::vector<map::MeshVertex_t> &vertices = m_pWorld->m_Vertices;
	for ( std::size_t i = 0; i + 2 < m_pWorld->m_Indices.size(); i += 3 )
	{
		Triangle_t triangle;
		triangle.m_Corners = { m_pWorld->m_Indices[ i ], m_pWorld->m_Indices[ i + 1 ], m_pWorld->m_Indices[ i + 2 ] };
		if ( std::ranges::any_of( triangle.m_Corners, [ & ]( std::uint32_t n ) { return ( *m_pLights )[ n ][ 3 ] != map::LIGHT_KIND_LIGHTMAP; } ) )
		{
			continue;
		}
		const map::Point_t a = map::ToPoint( vertices[ triangle.m_Corners[ 0 ] ].m_vecPosition );
		const map::Point_t b = map::ToPoint( vertices[ triangle.m_Corners[ 1 ] ].m_vecPosition );
		const map::Point_t c = map::ToPoint( vertices[ triangle.m_Corners[ 2 ] ].m_vecPosition );
		const map::Point_t normal = map::Cross( map::Subtract( c, a ), map::Subtract( b, a ) );
		if ( map::Length( normal ) < 1.0e-6 )
		{
			continue;
		}
		// The map's faces wind clockwise seen from the front.
		triangle.m_Normal = map::Normalized( normal );
		triangle.m_flDistance = map::Dot( triangle.m_Normal, a );
		const auto nTriangle = static_cast<std::uint32_t>( m_Triangles.size() );
		m_Triangles.push_back( triangle );
		map::Box_t box;
		box.Add( a );
		box.Add( b );
		box.Add( c );
		for ( int x = CellOf( box.m_Mins[ 0 ] ); x <= CellOf( box.m_Maxs[ 0 ] ); x++ )
		{
			for ( int y = CellOf( box.m_Mins[ 1 ] ); y <= CellOf( box.m_Maxs[ 1 ] ); y++ )
			{
				for ( int z = CellOf( box.m_Mins[ 2 ] ); z <= CellOf( box.m_Maxs[ 2 ] ); z++ )
				{
					m_Cells[ Cell( x, y, z ) ].push_back( nTriangle );
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::uint64_t CSurfaceLight::Cell( int x, int y, int z )
{
	const auto part = []( int n ) { return static_cast<std::uint64_t>( static_cast<std::uint32_t>( n + ( 1 << 20 ) ) & 0x1FFFFF ); };
	return part( x ) << 42 | part( y ) << 21 | part( z );
}

//-----------------------------------------------------------------------------
// Purpose: How much of each corner a point of a triangle's plane is; nullopt when the point is
//          outside the triangle.
//-----------------------------------------------------------------------------
std::optional<std::array<double, 3>> CSurfaceLight::Weights( const Triangle_t &triangle, map::Point_t point ) const
{
	const std::vector<map::MeshVertex_t> &vertices = m_pWorld->m_Vertices;
	const map::Point_t a = map::ToPoint( vertices[ triangle.m_Corners[ 0 ] ].m_vecPosition );
	const map::Point_t b = map::ToPoint( vertices[ triangle.m_Corners[ 1 ] ].m_vecPosition );
	const map::Point_t c = map::ToPoint( vertices[ triangle.m_Corners[ 2 ] ].m_vecPosition );
	const map::Point_t v0 = map::Subtract( b, a );
	const map::Point_t v1 = map::Subtract( c, a );
	const map::Point_t v2 = map::Subtract( point, a );
	const double d00 = map::Dot( v0, v0 );
	const double d01 = map::Dot( v0, v1 );
	const double d11 = map::Dot( v1, v1 );
	const double d20 = map::Dot( v2, v0 );
	const double d21 = map::Dot( v2, v1 );
	const double flDenominator = d00 * d11 - d01 * d01;
	if ( std::abs( flDenominator ) < 1.0e-9 )
	{
		return std::nullopt;
	}
	const double flV = ( d11 * d20 - d01 * d21 ) / flDenominator;
	const double flW = ( d00 * d21 - d01 * d20 ) / flDenominator;
	const double flU = 1.0 - flV - flW;
	constexpr double SLACK = -1.0e-3;
	if ( flU < SLACK || flV < SLACK || flW < SLACK )
	{
		return std::nullopt;
	}
	return std::array<double, 3>{ flU, flV, flW };
}

//-----------------------------------------------------------------------------
// Purpose: The light at a point of a triangle (on its plane): its corners' places in the
//          atlas blended by where the point is between them. Nullopt when the point is outside.
//-----------------------------------------------------------------------------
std::optional<CVec3> CSurfaceLight::LightAt( const Triangle_t &triangle, map::Point_t point ) const
{
	const auto weights = Weights( triangle, point );
	if ( !weights )
	{
		return std::nullopt;
	}
	const auto [ flU, flV, flW ] = *weights;
	const map::LightVertex_t &la = ( *m_pLights )[ triangle.m_Corners[ 0 ] ];
	const map::LightVertex_t &lb = ( *m_pLights )[ triangle.m_Corners[ 1 ] ];
	const map::LightVertex_t &lc = ( *m_pLights )[ triangle.m_Corners[ 2 ] ];
	const auto light = m_pAtlas->Sample( static_cast<float>( la[ 0 ] * flU + lb[ 0 ] * flV + lc[ 0 ] * flW ), static_cast<float>( la[ 1 ] * flU + lb[ 1 ] * flV + lc[ 1 ] * flW ) );
	return CVec3{ light[ 0 ], light[ 1 ], light[ 2 ] };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<CVec3> CSurfaceLight::On( map::Point_t point, map::Point_t normal ) const
{
	const auto found = m_Cells.find( Cell( CellOf( point[ 0 ] ), CellOf( point[ 1 ] ), CellOf( point[ 2 ] ) ) );
	if ( found == m_Cells.end() )
	{
		return std::nullopt;
	}
	std::optional<CVec3> best;
	double flBest = ON_FACE;
	for ( const std::uint32_t nTriangle : found->second )
	{
		const Triangle_t &triangle = m_Triangles[ nTriangle ];
		const double flOff = std::abs( map::Dot( triangle.m_Normal, point ) - triangle.m_flDistance );
		if ( flOff > flBest || map::Dot( triangle.m_Normal, normal ) < 0.5 )
		{
			continue;
		}
		if ( const auto light = LightAt( triangle, map::Subtract( point, map::Scale( triangle.m_Normal, map::Dot( triangle.m_Normal, point ) - triangle.m_flDistance ) ) ) )
		{
			best = light;
			flBest = flOff;
		}
	}
	return best;
}

//-----------------------------------------------------------------------------
// Purpose: Down the cells under the point, the highest upward face the vertical line through
//          it crosses below it.
//-----------------------------------------------------------------------------
std::optional<CVec3> CSurfaceLight::Below( map::Point_t point, double flReach ) const
{
	const int x = CellOf( point[ 0 ] );
	const int y = CellOf( point[ 1 ] );
	std::optional<CVec3> best;
	double flBestZ = point[ 2 ] - flReach;
	for ( int z = CellOf( point[ 2 ] ); z >= CellOf( point[ 2 ] - flReach ); z-- )
	{
		const auto found = m_Cells.find( Cell( x, y, z ) );
		if ( found != m_Cells.end() )
		{
			for ( const std::uint32_t nTriangle : found->second )
			{
				const Triangle_t &triangle = m_Triangles[ nTriangle ];
				if ( triangle.m_Normal[ 2 ] < 0.3 )
				{
					continue;
				}
				// Where the vertical line meets the triangle's plane.
				const double flZ = ( triangle.m_flDistance - triangle.m_Normal[ 0 ] * point[ 0 ] - triangle.m_Normal[ 1 ] * point[ 1 ] ) / triangle.m_Normal[ 2 ];
				if ( flZ > point[ 2 ] || flZ <= flBestZ )
				{
					continue;
				}
				if ( const auto light = LightAt( triangle, { point[ 0 ], point[ 1 ], flZ } ) )
				{
					best = light;
					flBestZ = flZ;
				}
			}
		}
		if ( best && flBestZ >= static_cast<double>( z ) * SURFACE_CELL )
		{
			break;
		}
	}
	return best;
}

namespace
{

class CCompiledReader
{
public:
	CCompiledReader( std::span<const std::byte> file, const CBsp &bsp, const CompiledOptions_t &options )
		: m_File( file ),
		  m_Bsp( bsp ),
		  m_Options( options )
	{
	}

	bool Run( CompiledMap_t &out, std::string &szError );

private:
	[[nodiscard]] std::vector<std::byte> Lump( int nLump ) const;
	[[nodiscard]] std::uint32_t MaterialOf( int nTexInfo );
	[[nodiscard]] std::uint32_t FlagsOf( int nTexInfo ) const;
	[[nodiscard]] std::optional<Face_t> ReadFace( std::size_t nFace, int nModel ) const;
	void BuildModel( std::span<Face_t> faces, int nModel, const std::vector<Rect_t> &rects, CompiledModel_t &model, CompiledMap_t &out );

	std::span<const std::byte> m_File;
	const CBsp &m_Bsp;
	const CompiledOptions_t &m_Options;
	std::vector<std::byte> m_Planes, m_TexData, m_Vertexes, m_TexInfo, m_Faces, m_LightFaces, m_Lighting, m_Edges, m_SurfEdges, m_Models;
	std::vector<std::byte> m_DispInfo, m_DispVerts, m_StringData, m_StringTable;
	std::vector<std::string> m_Materials;
	std::unordered_map<std::string, std::uint32_t> m_MaterialByName; // lower case
	std::unordered_map<int, std::uint32_t> m_MaterialByTexData;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::byte> CCompiledReader::Lump( int nLump ) const
{
	auto bytes = ReadLump( m_File, m_Bsp.GetLumps()[ static_cast<std::size_t>( nLump ) ] );
	return bytes ? std::move( *bytes ) : std::vector<std::byte>{};
}

//-----------------------------------------------------------------------------
// Purpose: A texinfo's material in the table, by the name the map had (one entry however many
//          cubemap copies the compiler made of it).
//-----------------------------------------------------------------------------
std::uint32_t CCompiledReader::MaterialOf( int nTexInfo )
{
	const int nTexData = nTexInfo >= 0 && static_cast<std::size_t>( nTexInfo ) < m_TexInfo.size() / TEXINFO_SIZE ? ReadAt<std::int32_t>( m_TexInfo, static_cast<std::size_t>( nTexInfo ) * TEXINFO_SIZE + 68 ) : -1;
	if ( const auto found = m_MaterialByTexData.find( nTexData ); found != m_MaterialByTexData.end() )
	{
		return found->second;
	}
	std::string szName = "TOOLS/TOOLSNODRAW";
	if ( nTexData >= 0 && static_cast<std::size_t>( nTexData ) < m_TexData.size() / TEXDATA_SIZE )
	{
		const int nString = ReadAt<std::int32_t>( m_TexData, static_cast<std::size_t>( nTexData ) * TEXDATA_SIZE + 12 );
		if ( nString >= 0 && static_cast<std::size_t>( nString ) < m_StringTable.size() / 4 )
		{
			const int nAt = ReadAt<std::int32_t>( m_StringTable, static_cast<std::size_t>( nString ) * 4 );
			if ( nAt >= 0 && static_cast<std::size_t>( nAt ) < m_StringData.size() )
			{
				const char *pszName = reinterpret_cast<const char *>( m_StringData.data() ) + nAt;
				szName = MapMaterialName( std::string_view( pszName, strnlen( pszName, m_StringData.size() - static_cast<std::size_t>( nAt ) ) ) );
			}
		}
	}
	std::string szKey = szName;
	std::ranges::transform( szKey, szKey.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	auto [ it, bAdded ] = m_MaterialByName.try_emplace( szKey, static_cast<std::uint32_t>( m_Materials.size() ) );
	if ( bAdded )
	{
		m_Materials.push_back( szName );
	}
	m_MaterialByTexData.emplace( nTexData, it->second );
	return it->second;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::uint32_t CCompiledReader::FlagsOf( int nTexInfo ) const
{
	return nTexInfo >= 0 && static_cast<std::size_t>( nTexInfo ) < m_TexInfo.size() / TEXINFO_SIZE ? ReadAt<std::uint32_t>( m_TexInfo, static_cast<std::size_t>( nTexInfo ) * TEXINFO_SIZE + 64 ) : SURF_NODRAW;
}

//-----------------------------------------------------------------------------
// Purpose: A face's corners by way of its edges, wound as the map's meshes wind theirs, and its
//          lightmap from the faces of the light being drawn (the HDR compile's have their own
//          offsets).
//-----------------------------------------------------------------------------
std::optional<Face_t> CCompiledReader::ReadFace( std::size_t nFace, int nModel ) const
{
	const std::size_t nAt = nFace * FACE_SIZE;
	if ( nAt + FACE_SIZE > m_Faces.size() )
	{
		return std::nullopt;
	}
	Face_t face;
	face.m_nModel = nModel;
	face.m_nFace = nFace;
	const std::size_t nPlane = ReadAt<std::uint16_t>( m_Faces, nAt );
	const int nFirstEdge = ReadAt<std::int32_t>( m_Faces, nAt + 4 );
	const int nEdges = ReadAt<std::int16_t>( m_Faces, nAt + 8 );
	face.m_nTexInfo = ReadAt<std::int16_t>( m_Faces, nAt + 10 );
	face.m_nDisp = ReadAt<std::int16_t>( m_Faces, nAt + 12 );
	if ( nPlane >= m_Planes.size() / PLANE_SIZE || nFirstEdge < 0 || nEdges < 3 || static_cast<std::size_t>( nFirstEdge ) + static_cast<std::size_t>( nEdges ) > m_SurfEdges.size() / 4 )
	{
		return std::nullopt;
	}
	// The face names the plane of the pair it faces along.
	face.m_Plane = { ReadPoint( m_Planes, nPlane * PLANE_SIZE ), ReadAt<float>( m_Planes, nPlane * PLANE_SIZE + 12 ) };
	for ( int i = 0; i < nEdges; i++ )
	{
		// A surface edge is an edge's number, negative when the face goes along it backwards.
		const int nSurfEdge = ReadAt<std::int32_t>( m_SurfEdges, ( static_cast<std::size_t>( nFirstEdge ) + static_cast<std::size_t>( i ) ) * 4 );
		const std::size_t nEdge = static_cast<std::size_t>( std::abs( nSurfEdge ) );
		if ( nEdge >= m_Edges.size() / 4 )
		{
			return std::nullopt;
		}
		const std::size_t nVertex = ReadAt<std::uint16_t>( m_Edges, nEdge * 4 + ( nSurfEdge < 0 ? 2 : 0 ) );
		if ( nVertex >= m_Vertexes.size() / 12 )
		{
			return std::nullopt;
		}
		face.m_Points.push_back( ReadPoint( m_Vertexes, nVertex * 12 ) );
	}
	// Turned to go clockwise seen from the front (its corners' turn against the normal).
	Point_t vecTurn{};
	for ( std::size_t i = 0; i < face.m_Points.size(); i++ )
	{
		vecTurn = map::Add( vecTurn, map::Cross( face.m_Points[ i ], face.m_Points[ ( i + 1 ) % face.m_Points.size() ] ) );
	}
	if ( map::Dot( vecTurn, face.m_Plane.m_Normal ) > 0.0 )
	{
		std::ranges::reverse( face.m_Points );
	}

	const std::size_t nLightAt = m_LightFaces.size() == m_Faces.size() ? nAt : ~std::size_t{ 0 };
	if ( nLightAt != ~std::size_t{ 0 } )
	{
		face.m_nLightOffset = ReadAt<std::int32_t>( m_LightFaces, nLightAt + 20 );
		face.m_LuxelMins = { ReadAt<std::int32_t>( m_LightFaces, nLightAt + 28 ), ReadAt<std::int32_t>( m_LightFaces, nLightAt + 32 ) };
		face.m_LuxelSize = { ReadAt<std::int32_t>( m_LightFaces, nLightAt + 36 ), ReadAt<std::int32_t>( m_LightFaces, nLightAt + 40 ) };
		const auto nStyle = ReadAt<std::uint8_t>( m_LightFaces, nLightAt + 16 );
		const std::size_t nLuxels = static_cast<std::size_t>( std::max( face.m_LuxelSize[ 0 ] + 1, 0 ) ) * static_cast<std::size_t>( std::max( face.m_LuxelSize[ 1 ] + 1, 0 ) );
		face.m_bLit = nStyle != NO_STYLE && face.m_nLightOffset >= 0 && ( FlagsOf( face.m_nTexInfo ) & SURF_NOLIGHT ) == 0 && nLuxels > 0 &&
			face.m_LuxelSize[ 0 ] < MAX_ATLAS_SIDE && face.m_LuxelSize[ 1 ] < MAX_ATLAS_SIDE &&
			static_cast<std::size_t>( face.m_nLightOffset ) + nLuxels * 4 <= m_Lighting.size();
	}
	return face;
}

//-----------------------------------------------------------------------------
// Purpose: A model's faces as a mesh: polygons fanned into triangles, displacements as their
//          grids, each face in the area its centre is in, the triangles by material then area.
//          Textures and lightmaps follow the face's flat surface (a displacement's too, as the
//          game maps them).
//-----------------------------------------------------------------------------
void CCompiledReader::BuildModel( std::span<Face_t> faces, int nModel, const std::vector<Rect_t> &rects, CompiledModel_t &model, CompiledMap_t &out )
{
	auto pMesh = std::make_shared<map::CMapMesh>();
	auto pLights = std::make_shared<std::vector<map::LightVertex_t>>();
	map::CMapMesh &mesh = *pMesh;
	struct Triangle_t
	{
		std::uint64_t m_nKey; // material, then chunk
		std::array<std::uint32_t, 3> m_Corners;
		std::uint32_t m_nFace;
	};
	std::vector<Triangle_t> triangles;
	std::unordered_map<std::uint64_t, std::uint32_t> chunkOf;
	for ( Face_t &face : faces )
	{
		const std::size_t nTexInfoAt = static_cast<std::size_t>( std::max( face.m_nTexInfo, 0 ) ) * TEXINFO_SIZE;
		std::array<std::array<double, 4>, 4> axes{}; // texture s, t, then lightmap s, t
		for ( std::size_t a = 0; a < 4; a++ )
		{
			for ( std::size_t k = 0; k < 4; k++ )
			{
				axes[ a ][ k ] = ReadAt<float>( m_TexInfo, nTexInfoAt + a * 16 + k * 4 );
			}
		}
		const auto project = [ & ]( std::size_t a, Point_t p ) { return axes[ a ][ 0 ] * p[ 0 ] + axes[ a ][ 1 ] * p[ 1 ] + axes[ a ][ 2 ] * p[ 2 ] + axes[ a ][ 3 ]; };
		const std::uint32_t nMaterial = MaterialOf( face.m_nTexInfo );
		const Rect_t *pRect = face.m_nRect >= 0 ? &rects[ static_cast<std::size_t>( face.m_nRect ) ] : nullptr;

		const auto nFace = static_cast<std::uint32_t>( mesh.m_Faces.size() );
		const auto nBase = static_cast<std::uint32_t>( mesh.m_Vertices.size() );
		map::MeshFace_t meshFace;
		meshFace.m_nSolid = static_cast<std::uint32_t>( nModel );
		meshFace.m_nSide = nFace;
		meshFace.m_nMaterial = nMaterial;
		meshFace.m_nFirstVertex = nBase;
		const auto addVertex = [ & ]( Point_t p, Point_t normal, Point_t flat, float flAlpha ) {
			map::MeshVertex_t vertex;
			vertex.m_vecPosition = map::ToVec3( p );
			vertex.m_vecNormal = map::ToVec3( normal );
			vertex.m_Texel = { static_cast<float>( project( 0, flat ) ), static_cast<float>( project( 1, flat ) ) };
			// Within the face's own luxels: a corner a little past them (a displacement's, or by
			// rounding) would read its neighbour's light.
			const double flU = std::clamp( project( 2, flat ) - face.m_LuxelMins[ 0 ], 0.0, static_cast<double>( std::max( face.m_LuxelSize[ 0 ], 0 ) ) );
			const double flV = std::clamp( project( 3, flat ) - face.m_LuxelMins[ 1 ], 0.0, static_cast<double>( std::max( face.m_LuxelSize[ 1 ], 0 ) ) );
			vertex.m_Luxel = { static_cast<float>( flU ), static_cast<float>( flV ) };
			vertex.m_flAlpha = flAlpha;
			vertex.m_nObject = static_cast<std::uint32_t>( nModel );
			mesh.m_Vertices.push_back( vertex );
			// A luxel's light is at the middle of its texel.
			pLights->push_back( pRect ? map::LightVertex_t{ static_cast<float>( pRect->m_nX + flU + 0.5 ), static_cast<float>( pRect->m_nY + flV + 0.5 ), 0.0f, map::LIGHT_KIND_LIGHTMAP } : map::LightVertex_t{ 0.0f, 0.0f, 0.0f, map::LIGHT_KIND_FULLBRIGHT } );
		};

		std::vector<std::array<std::uint32_t, 3>> faceTriangles;
		std::optional<map::DispMesh_t> disp;
		if ( face.m_nDisp >= 0 && static_cast<std::size_t>( face.m_nDisp ) < m_DispInfo.size() / DISPINFO_SIZE )
		{
			const std::size_t nDispAt = static_cast<std::size_t>( face.m_nDisp ) * DISPINFO_SIZE;
			map::DispInfo_t info;
			info.m_nPower = ReadAt<std::int32_t>( m_DispInfo, nDispAt + 20 );
			info.m_Start = ReadPoint( m_DispInfo, nDispAt );
			const int nFirstVert = ReadAt<std::int32_t>( m_DispInfo, nDispAt + 12 );
			const std::size_t nSize = info.m_nPower >= 2 && info.m_nPower <= 4 ? ( std::size_t{ 1 } << info.m_nPower ) + 1 : 0;
			if ( nSize == 0 || nFirstVert < 0 || static_cast<std::size_t>( nFirstVert ) + nSize * nSize > m_DispVerts.size() / DISP_VERT_SIZE )
			{
				out.m_Problems.push_back( "face " + std::to_string( face.m_nFace ) + ": its displacement's power or points don't read" );
			}
			else
			{
				for ( std::size_t i = 0; i < nSize * nSize; i++ )
				{
					const std::size_t nVertAt = ( static_cast<std::size_t>( nFirstVert ) + i ) * DISP_VERT_SIZE;
					info.m_Normals.push_back( map::ToVec3( ReadPoint( m_DispVerts, nVertAt ) ) );
					info.m_Distances.push_back( ReadAt<float>( m_DispVerts, nVertAt + 12 ) );
					info.m_Offsets.emplace_back();
					info.m_OffsetNormals.push_back( map::ToVec3( face.m_Plane.m_Normal ) );
					info.m_Alphas.push_back( ReadAt<float>( m_DispVerts, nVertAt + 16 ) );
				}
				info.m_bValid = true;
				map::FaceGeometry_t geometry;
				geometry.m_Plane = face.m_Plane;
				geometry.m_Points = face.m_Points;
				std::string szWhy;
				disp = map::BuildDisplacement( info, geometry, &szWhy );
				if ( !disp )
				{
					out.m_Problems.push_back( "face " + std::to_string( face.m_nFace ) + ": " + szWhy );
				}
			}
		}
		Point_t centre{};
		if ( disp )
		{
			meshFace.m_bDisplacement = true;
			out.m_nDisplacements++;
			for ( std::size_t i = 0; i < disp->m_Points.size(); i++ )
			{
				addVertex( disp->m_Points[ i ], disp->m_Normals[ i ], disp->m_Flat[ i ], disp->m_Alphas[ i ] );
				centre = map::Add( centre, map::Scale( disp->m_Points[ i ], 1.0 / static_cast<double>( disp->m_Points.size() ) ) );
			}
			std::vector<std::pair<std::uint32_t, std::uint32_t>> edges;
			for ( std::size_t i = 0; i + 2 < disp->m_Indices.size(); i += 3 )
			{
				const std::array<std::uint32_t, 3> triangle = { nBase + disp->m_Indices[ i ], nBase + disp->m_Indices[ i + 1 ], nBase + disp->m_Indices[ i + 2 ] };
				faceTriangles.push_back( triangle );
				for ( std::size_t k = 0; k < 3; k++ )
				{
					edges.emplace_back( std::min( triangle[ k ], triangle[ ( k + 1 ) % 3 ] ), std::max( triangle[ k ], triangle[ ( k + 1 ) % 3 ] ) );
				}
			}
			std::ranges::sort( edges );
			edges.erase( std::unique( edges.begin(), edges.end() ), edges.end() );
			for ( const auto &[ a, b ] : edges )
			{
				mesh.m_Lines.insert( mesh.m_Lines.end(), { a, b } );
			}
		}
		else
		{
			for ( const Point_t &p : face.m_Points )
			{
				addVertex( p, face.m_Plane.m_Normal, p, 0.0f );
				centre = map::Add( centre, map::Scale( p, 1.0 / static_cast<double>( face.m_Points.size() ) ) );
			}
			const auto nCount = static_cast<std::uint32_t>( face.m_Points.size() );
			for ( std::uint32_t i = 1; i + 1 < nCount; i++ )
			{
				faceTriangles.push_back( { nBase, nBase + i, nBase + i + 1 } );
			}
			for ( std::uint32_t i = 0; i < nCount; i++ )
			{
				mesh.m_Lines.insert( mesh.m_Lines.end(), { nBase + i, nBase + ( i + 1 ) % nCount } );
			}
		}
		meshFace.m_nVertexCount = static_cast<std::uint32_t>( mesh.m_Vertices.size() ) - nBase;
		mesh.m_Faces.push_back( meshFace );
		model.m_FaceNumbers.push_back( static_cast<std::int32_t>( face.m_nFace ) );

		std::uint64_t nCell = 0;
		for ( std::size_t a = 0; a < 3; a++ )
		{
			const double flCell = std::floor( centre[ a ] / CHUNK_SIZE );
			nCell = ( nCell << 21 ) | ( static_cast<std::uint64_t>( static_cast<std::int64_t>( std::clamp( flCell, -1048576.0, 1048575.0 ) ) + 1048576 ) & 0x1FFFFF );
		}
		auto [ it, bAdded ] = chunkOf.try_emplace( nCell, static_cast<std::uint32_t>( mesh.m_Chunks.size() ) );
		if ( bAdded )
		{
			mesh.m_Chunks.emplace_back();
		}
		for ( std::uint32_t v = nBase; v < mesh.m_Vertices.size(); v++ )
		{
			mesh.m_Chunks[ it->second ].m_Bounds.Add( map::ToPoint( mesh.m_Vertices[ v ].m_vecPosition ) );
		}
		for ( const auto &corners : faceTriangles )
		{
			triangles.push_back( { ( static_cast<std::uint64_t>( nMaterial ) << 32 ) | it->second, corners, nFace } );
		}
	}

	std::ranges::stable_sort( triangles, {}, &Triangle_t::m_nKey );
	mesh.m_Indices.reserve( triangles.size() * 3 );
	mesh.m_TriangleFaces.reserve( triangles.size() );
	for ( const Triangle_t &triangle : triangles )
	{
		const auto nMaterial = static_cast<std::uint32_t>( triangle.m_nKey >> 32 );
		const auto nChunk = static_cast<std::uint32_t>( triangle.m_nKey & 0xFFFFFFFFu );
		if ( mesh.m_Batches.empty() || mesh.m_Batches.back().m_nMaterial != nMaterial || mesh.m_Batches.back().m_nChunk != nChunk )
		{
			mesh.m_Batches.push_back( { nMaterial, nChunk, static_cast<std::uint32_t>( mesh.m_Indices.size() ), 0 } );
		}
		mesh.m_Indices.insert( mesh.m_Indices.end(), triangle.m_Corners.begin(), triangle.m_Corners.end() );
		mesh.m_Batches.back().m_nIndexCount += 3;
		mesh.m_TriangleFaces.push_back( triangle.m_nFace );
	}
	// The whole model is one object.
	mesh.m_SolidIndices = { static_cast<std::uint32_t>( nModel ) };
	mesh.m_SolidFaces = { { 0, static_cast<std::uint32_t>( mesh.m_Faces.size() ) } };
	mesh.m_SolidLines = { { 0, static_cast<std::uint32_t>( mesh.m_Lines.size() / 2 ) } };
	model.m_nFaces = static_cast<int>( mesh.m_Faces.size() );
	model.m_pMesh = std::move( pMesh );
	model.m_pLights = std::move( pLights );
}

//-----------------------------------------------------------------------------
// Purpose: Every model's faces that are drawn (not nodraw, hint or skip), their lightmaps
//          packed into one atlas, then the meshes.
//-----------------------------------------------------------------------------
bool CCompiledReader::Run( CompiledMap_t &out, std::string &szError )
{
	m_Planes = Lump( LUMP_PLANES );
	m_TexData = Lump( LUMP_TEXDATA );
	m_Vertexes = Lump( LUMP_VERTEXES );
	m_TexInfo = Lump( LUMP_TEXINFO );
	m_Faces = Lump( LUMP_FACES );
	m_Edges = Lump( LUMP_EDGES );
	m_SurfEdges = Lump( LUMP_SURFEDGES );
	m_Models = Lump( LUMP_MODELS );
	m_DispInfo = Lump( LUMP_DISPINFO );
	m_DispVerts = Lump( LUMP_DISP_VERTS );
	m_StringData = Lump( LUMP_TEXDATA_STRING_DATA );
	m_StringTable = Lump( LUMP_TEXDATA_STRING_TABLE );
	if ( m_Faces.empty() || m_Models.empty() || m_Planes.empty() || m_Vertexes.empty() || m_Edges.empty() || m_SurfEdges.empty() )
	{
		szError = "the map has no faces to draw (its lumps are missing or cut short)";
		return false;
	}
	// The HDR compile's light has faces of its own (the same faces, their own offsets).
	if ( m_Options.m_bHdr )
	{
		m_Lighting = Lump( LUMP_LIGHTING_HDR );
		m_LightFaces = Lump( LUMP_FACES_HDR );
		if ( m_LightFaces.empty() )
		{
			m_LightFaces = m_Faces;
		}
		out.m_bHdr = !m_Lighting.empty();
	}
	if ( m_Lighting.empty() )
	{
		m_Lighting = Lump( LUMP_LIGHTING );
		m_LightFaces = m_Faces;
		out.m_bHdr = false;
	}

	const std::size_t nModels = m_Models.size() / MODEL_SIZE;
	const std::size_t nFaces = m_Faces.size() / FACE_SIZE;
	std::vector<std::vector<Face_t>> modelFaces( nModels );
	std::vector<Rect_t> rects;
	std::vector<std::pair<int, std::size_t>> lit; // (model, its face) by rect
	out.m_Models.resize( nModels );
	for ( std::size_t nModel = 0; nModel < nModels; nModel++ )
	{
		const std::size_t nAt = nModel * MODEL_SIZE;
		out.m_Models[ nModel ].m_Bounds.Add( ReadPoint( m_Models, nAt ) );
		out.m_Models[ nModel ].m_Bounds.Add( ReadPoint( m_Models, nAt + 12 ) );
		const int nFirst = ReadAt<std::int32_t>( m_Models, nAt + 40 );
		const int nCount = ReadAt<std::int32_t>( m_Models, nAt + 44 );
		for ( int i = 0; i < nCount; i++ )
		{
			const std::size_t nFace = static_cast<std::size_t>( nFirst ) + static_cast<std::size_t>( i );
			if ( nFirst < 0 || nFace >= nFaces )
			{
				break;
			}
			auto face = ReadFace( nFace, static_cast<int>( nModel ) );
			if ( !face || ( FlagsOf( face->m_nTexInfo ) & ( SURF_NODRAW | SURF_HINT | SURF_SKIP ) ) )
			{
				out.m_nHiddenDisplacements += face && face->m_nDisp >= 0 ? 1 : 0;
				continue;
			}
			if ( face->m_bLit )
			{
				face->m_nRect = static_cast<int>( rects.size() );
				rects.push_back( { 0, 0, face->m_LuxelSize[ 0 ] + 1, face->m_LuxelSize[ 1 ] + 1 } );
				lit.emplace_back( static_cast<int>( nModel ), modelFaces[ nModel ].size() );
				out.m_nLitFaces++;
			}
			else
			{
				out.m_nUnlitFaces++;
			}
			modelFaces[ nModel ].push_back( std::move( *face ) );
		}
	}

	// The atlas: wider until it fits.
	int nWidth = std::max( m_Options.m_nAtlasWidth, 64 );
	int nHeight = 1;
	while ( !PackRects( rects, nWidth, nHeight ) )
	{
		if ( nWidth >= MAX_ATLAS_SIDE )
		{
			out.m_Problems.push_back( "the lightmaps don't fit in one texture: the map is drawn unlit" );
			rects.clear();
			for ( auto &faces : modelFaces )
			{
				for ( Face_t &face : faces )
				{
					face.m_nRect = -1;
				}
			}
			nWidth = 1;
			nHeight = 1;
			break;
		}
		nWidth *= 2;
	}
	auto pLighting = std::make_shared<map::CLighting>();
	pLighting->m_nWidth = nWidth;
	pLighting->m_nHeight = nHeight;
	pLighting->m_Texels.assign( static_cast<std::size_t>( nWidth ) * static_cast<std::size_t>( nHeight ) * 4, 0 );
	const std::uint16_t nOne = map::FloatToHalf( 1.0f );
	for ( std::size_t r = 0; r < rects.size(); r++ )
	{
		const Face_t &face = modelFaces[ static_cast<std::size_t>( lit[ r ].first ) ][ lit[ r ].second ];
		const Rect_t &rect = rects[ r ];
		for ( int t = 0; t < rect.m_nHeight; t++ )
		{
			for ( int s = 0; s < rect.m_nWidth; s++ )
			{
				// The face's first lightmap: its first style's (a bump-mapped face's flat one
				// comes first), each luxel's colour times two to its exponent.
				const std::size_t nLuxel = static_cast<std::size_t>( face.m_nLightOffset ) + ( static_cast<std::size_t>( t ) * static_cast<std::size_t>( rect.m_nWidth ) + static_cast<std::size_t>( s ) ) * 4;
				const int nExponent = static_cast<std::int8_t>( m_Lighting[ nLuxel + 3 ] );
				const std::size_t nTexel = ( static_cast<std::size_t>( rect.m_nY + t ) * static_cast<std::size_t>( nWidth ) + static_cast<std::size_t>( rect.m_nX + s ) ) * 4;
				for ( std::size_t k = 0; k < 3; k++ )
				{
					const float flLight = static_cast<float>( std::ldexp( static_cast<double>( static_cast<std::uint8_t>( m_Lighting[ nLuxel + k ] ) ), nExponent ) / 255.0 );
					pLighting->m_Texels[ nTexel + k ] = map::FloatToHalf( flLight );
				}
				pLighting->m_Texels[ nTexel + 3 ] = nOne;
			}
		}
	}
	out.m_pLighting = std::move( pLighting );

	for ( std::size_t nModel = 0; nModel < nModels; nModel++ )
	{
		BuildModel( modelFaces[ nModel ], static_cast<int>( nModel ), rects, out.m_Models[ nModel ], out );
	}
	out.m_Materials = std::move( m_Materials );

	if ( auto pVisibility = std::make_shared<CVisibility>( m_Planes, Lump( LUMP_NODES ), Lump( LUMP_LEAFS ), Lump( LUMP_LEAFFACES ), Lump( LUMP_VISIBILITY ), ReadAt<std::int32_t>( m_Models, 36 ), m_Faces.size() / FACE_SIZE ); pVisibility->IsUsable() )
		out.m_pVisibility = std::move( pVisibility );
	out.m_pAmbient = std::make_shared<const CAmbientLighting>( m_Planes, Lump( LUMP_NODES ), Lump( LUMP_LEAFS ), Lump( out.m_bHdr ? LUMP_LEAF_AMBIENT_INDEX_HDR : LUMP_LEAF_AMBIENT_INDEX ), Lump( out.m_bHdr ? LUMP_LEAF_AMBIENT_LIGHTING_HDR : LUMP_LEAF_AMBIENT_LIGHTING ), ReadAt<std::int32_t>( m_Models, 36 ) );

	const auto numbers = []( std::string_view szText ) {
		std::array<double, 3> values{};
		std::size_t nAt = 0;
		for ( double &flValue : values )
		{
			while ( nAt < szText.size() && szText[ nAt ] == ' ' )
			{
				nAt++;
			}
			const auto [ pEnd, error ] = std::from_chars( szText.data() + nAt, szText.data() + szText.size(), flValue );
			if ( error != std::errc{} )
			{
				break;
			}
			nAt = static_cast<std::size_t>( pEnd - szText.data() );
		}
		return values;
	};
	for ( const Entity_t &entity : m_Bsp.GetEntities() )
	{
		const std::string_view szModel = entity.Get( "model" );
		int nModel = 0;
		if ( szModel.size() < 2 || szModel[ 0 ] != '*' || std::from_chars( szModel.data() + 1, szModel.data() + szModel.size(), nModel ).ec != std::errc{} || nModel <= 0 || static_cast<std::size_t>( nModel ) >= nModels )
		{
			continue;
		}
		ModelPlacement_t placement;
		placement.m_nModel = nModel;
		placement.m_szClass = std::string( entity.Get( "classname" ) );
		placement.m_Origin = numbers( entity.Get( "origin" ) );
		placement.m_Angles = numbers( entity.Get( "angles" ) );
		out.m_Placements.push_back( std::move( placement ) );
	}
	return true;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<CompiledMap_t> ReadCompiledMap( std::span<const std::byte> file, const CompiledOptions_t &options, std::string *pError )
{
	std::string szError;
	const auto bsp = CBsp::Parse( file, &szError );
	if ( !bsp )
	{
		if ( pError )
		{
			*pError = szError;
		}
		return std::nullopt;
	}
	CompiledMap_t out;
	CCompiledReader reader( file, *bsp, options );
	if ( !reader.Run( out, szError ) )
	{
		if ( pError )
		{
			*pError = szError;
		}
		return std::nullopt;
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Cell by cell along the ray (each cell's triangles tried as it is reached), the
//          nearest face it meets.
//-----------------------------------------------------------------------------
std::optional<map::Point_t> CSurfaceLight::Hit( map::Point_t from, map::Point_t direction, double flReach ) const
{
	if ( m_Cells.empty() || map::Length( direction ) < 1.0e-9 )
	{
		return std::nullopt;
	}
	direction = map::Normalized( direction );
	std::array<int, 3> cell{};
	std::array<int, 3> step{};
	std::array<double, 3> next{};
	std::array<double, 3> across{};
	for ( std::size_t a = 0; a < 3; a++ )
	{
		cell[ a ] = CellOf( from[ a ] );
		if ( std::abs( direction[ a ] ) < 1.0e-12 )
		{
			next[ a ] = across[ a ] = std::numeric_limits<double>::infinity();
			continue;
		}
		step[ a ] = direction[ a ] > 0.0 ? 1 : -1;
		const double flEdge = ( cell[ a ] + ( step[ a ] > 0 ? 1 : 0 ) ) * SURFACE_CELL;
		next[ a ] = ( flEdge - from[ a ] ) / direction[ a ];
		across[ a ] = SURFACE_CELL / std::abs( direction[ a ] );
	}
	double flBest = flReach;
	bool bFound = false;
	for ( double flAt = 0.0; flAt <= flReach; )
	{
		if ( const auto found = m_Cells.find( Cell( cell[ 0 ], cell[ 1 ], cell[ 2 ] ) ); found != m_Cells.end() )
		{
			for ( const std::uint32_t nTriangle : found->second )
			{
				const Triangle_t &triangle = m_Triangles[ nTriangle ];
				const double flFacing = map::Dot( triangle.m_Normal, direction );
				if ( std::abs( flFacing ) < 1.0e-9 )
				{
					continue;
				}
				const double flAlong = ( triangle.m_flDistance - map::Dot( triangle.m_Normal, from ) ) / flFacing;
				if ( flAlong < 0.0 || flAlong >= flBest )
				{
					continue;
				}
				if ( Weights( triangle, map::Add( from, map::Scale( direction, flAlong ) ) ) )
				{
					flBest = flAlong;
					bFound = true;
				}
			}
		}
		const std::size_t a = next[ 0 ] < next[ 1 ] ? ( next[ 0 ] < next[ 2 ] ? 0 : 2 ) : ( next[ 1 ] < next[ 2 ] ? 1 : 2 );
		// A hit before the next cell starts can't be beaten further on.
		if ( bFound && flBest <= next[ a ] )
		{
			break;
		}
		flAt = next[ a ];
		cell[ a ] += step[ a ];
		next[ a ] += across[ a ];
	}
	return bFound ? std::optional{ map::Add( from, map::Scale( direction, flBest ) ) } : std::nullopt;
}

} // namespace xv::bsp
