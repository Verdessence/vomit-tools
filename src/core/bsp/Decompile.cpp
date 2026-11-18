//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Decompile.h.
//
//===========================================================================//
#include "Decompile.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <regex>
#include <set>
#include <string_view>
#include <unordered_map>

#include "bsp/Bsp.h"
#include "map/Geometry.h"

namespace xv::bsp
{

namespace
{

using map::Plane_t;
using map::Point_t;

constexpr int LUMP_PLANES = 1;
constexpr int LUMP_TEXDATA = 2;
constexpr int LUMP_VERTEXES = 3;
constexpr int LUMP_NODES = 5;
constexpr int LUMP_TEXINFO = 6;
constexpr int LUMP_FACES = 7;
constexpr int LUMP_LEAFS = 10;
constexpr int LUMP_EDGES = 12;
constexpr int LUMP_SURFEDGES = 13;
constexpr int LUMP_MODELS = 14;
constexpr int LUMP_LEAFBRUSHES = 17;
constexpr int LUMP_BRUSHES = 18;
constexpr int LUMP_BRUSHSIDES = 19;
constexpr int LUMP_AREAPORTALS = 21;
constexpr int LUMP_DISPINFO = 26;
constexpr int LUMP_DISP_VERTS = 33;
constexpr int LUMP_GAME = 35;
constexpr int LUMP_CUBEMAPS = 42;
constexpr int LUMP_TEXDATA_STRING_DATA = 43;
constexpr int LUMP_TEXDATA_STRING_TABLE = 44;
constexpr int LUMP_OVERLAYS = 45;
constexpr int LUMP_DISP_TRIS = 48;
constexpr int LUMP_CLIP_PORTAL_VERTS = 50;
constexpr int LUMP_FACES_HDR = 58;

// The size of one record in each lump.
constexpr std::size_t PLANE_SIZE = 20;
constexpr std::size_t TEXDATA_SIZE = 32;
constexpr std::size_t NODE_SIZE = 32;
constexpr std::size_t TEXINFO_SIZE = 72;
constexpr std::size_t FACE_SIZE = 56;
constexpr std::size_t MODEL_SIZE = 48;
constexpr std::size_t BRUSH_SIZE = 12;
constexpr std::size_t BRUSHSIDE_SIZE = 8;
constexpr std::size_t AREAPORTAL_SIZE = 12;
constexpr std::size_t DISPINFO_SIZE = 176;
constexpr std::size_t DISP_VERT_SIZE = 20;
constexpr std::size_t CUBEMAP_SIZE = 16;
constexpr std::size_t OVERLAY_SIZE = 352;
constexpr int OVERLAY_FACES = 64;
constexpr std::size_t STATIC_PROP_NAME_SIZE = 128;
// Every version of a static prop begins with these bytes: where it is, its model, how it
// collides and fades.
constexpr std::size_t STATIC_PROP_BASE_SIZE = 56;
constexpr std::uint32_t STATIC_PROP_LUMP_ID = ( 's' << 24 ) | ( 'p' << 16 ) | ( 'r' << 8 ) | 'p';

constexpr std::uint32_t CONTENTS_AREAPORTAL = 0x8000;
constexpr std::uint32_t CONTENTS_DETAIL = 0x8000000;
constexpr std::uint32_t CONTENTS_LADDER = 0x20000000;

// Brushes are cut out of a square this far across; Source's maps reach 16384 (some games' 32768).
constexpr double WINDING_EXTENT = 131072.0;
constexpr double CLIP_EPSILON = 0.01;
// A corner this near a whole unit is written as that unit: the compiler's planes are single
// floats, and cutting them back into corners leaves 127.9998 where the map had 128.
constexpr double SNAP_EPSILON = 0.01;
// How thick the brush made for a displacement whose own brush the compiler didn't keep is.
constexpr double DISPLACEMENT_THICKNESS = 8.0;
constexpr std::string_view NODRAW = "TOOLS/TOOLSNODRAW";

//-----------------------------------------------------------------------------
// Purpose: A value at a place the caller has checked.
//-----------------------------------------------------------------------------
template <typename T>
T ReadAt( const std::vector<std::byte> &bytes, std::size_t nAt )
{
	T value{};
	std::memcpy( &value, bytes.data() + nAt, sizeof( T ) );
	return value;
}

//-----------------------------------------------------------------------------
// Purpose: Three floats as a point.
//-----------------------------------------------------------------------------
Point_t ReadPoint( const std::vector<std::byte> &bytes, std::size_t nAt )
{
	return { ReadAt<float>( bytes, nAt ), ReadAt<float>( bytes, nAt + 4 ), ReadAt<float>( bytes, nAt + 8 ) };
}

//-----------------------------------------------------------------------------
// Purpose: A number as Hammer writes one: no exponent, no trailing zeros, "-0" as "0".
//-----------------------------------------------------------------------------
std::string Num( double flValue, int nDecimals = 4 )
{
	if ( !std::isfinite( flValue ) )
	{
		return "0";
	}
	char buffer[ 64 ];
	std::snprintf( buffer, sizeof( buffer ), "%.*f", nDecimals, flValue );
	std::string szText = buffer;
	if ( szText.find( '.' ) != std::string::npos )
	{
		while ( szText.back() == '0' )
		{
			szText.pop_back();
		}
		if ( szText.back() == '.' )
		{
			szText.pop_back();
		}
	}
	return szText == "-0" ? "0" : szText;
}

//-----------------------------------------------------------------------------
// Purpose: "x y z".
//-----------------------------------------------------------------------------
std::string Vec( Point_t p, int nDecimals = 4 )
{
	return Num( p[ 0 ], nDecimals ) + " " + Num( p[ 1 ], nDecimals ) + " " + Num( p[ 2 ], nDecimals );
}

//-----------------------------------------------------------------------------
// Purpose: "x y z" read as a point; zeros for what isn't there.
//-----------------------------------------------------------------------------
Point_t ParsePoint( std::string_view szText )
{
	Point_t p{};
	const char *pAt = szText.data();
	const char *pEnd = szText.data() + szText.size();
	for ( double &flValue : p )
	{
		while ( pAt < pEnd && ( *pAt == ' ' || *pAt == '\t' ) )
		{
			pAt++;
		}
		const auto result = std::from_chars( pAt, pEnd, flValue );
		if ( result.ec != std::errc{} )
		{
			break;
		}
		pAt = result.ptr;
	}
	return p;
}

//-----------------------------------------------------------------------------
// Purpose: Whether two names are the same but for their letters' case.
//-----------------------------------------------------------------------------
bool SameName( std::string_view a, std::string_view b )
{
	return a.size() == b.size() && std::equal( a.begin(), a.end(), b.begin(), []( char x, char y ) { return std::tolower( static_cast<unsigned char>( x ) ) == std::tolower( static_cast<unsigned char>( y ) ); } );
}

// A plane of a brush to be built, with what its face is textured with.
struct BrushPlane_t
{
	Plane_t m_Plane;
	int m_nTexInfo = -1;
	int m_nPlaneKey = -1; // the plane lump's pair it came from; -1 for a made one
};

struct Side_t
{
	int m_nId = 0;
	Plane_t m_Plane;
	std::vector<Point_t> m_Polygon;
	std::array<Point_t, 3> m_Points{}; // the three corners its plane is written as
	int m_nTexInfo = -1;
	int m_nPlaneKey = -1;
	int m_nDisp = -1; // its displacement in the lump
};

struct Solid_t
{
	int m_nId = 0;
	std::vector<Side_t> m_Sides;
	std::uint32_t m_nContents = 0;
	Point_t m_vecOrigin{}; // what its entity's origin moved it by
	int m_nOwner = -1;     // the entity (of m_Entities) it is written in; -1 for the world
};

// An entity to be written: one of the lump's, or one made for what the compiler stored
// elsewhere.
struct OutEntity_t
{
	int m_nId = 0;
	std::vector<std::pair<std::string, std::string>> m_Keys;
	std::vector<std::pair<std::string, std::string>> m_Outputs;
	bool m_bWorld = false;
};

// A face of the map, as far as matching it to a brush's side needs.
struct Face_t
{
	int m_nPlaneKey = 0;
	Point_t m_vecNormal{};
	std::vector<Point_t> m_Points;
	int m_nTexInfo = -1;
};

class CDecompiler
{
public:
	CDecompiler( std::span<const std::byte> file, const CBsp &bsp, DecompileReport_t &report )
		: m_File( file ),
		  m_Bsp( bsp ),
		  m_Report( report )
	{
	}

	[[nodiscard]] bool Run( std::string &szOut, std::string &szError );

private:
	[[nodiscard]] std::vector<std::byte> Lump( int nLump ) const;
	[[nodiscard]] std::size_t CountOf( const std::vector<std::byte> &lump, std::size_t nSize ) const { return lump.size() / nSize; }
	[[nodiscard]] Plane_t PlaneAt( std::size_t nPlane ) const;
	[[nodiscard]] std::string MaterialOf( int nTexInfo );
	[[nodiscard]] std::optional<Face_t> FaceAt( int nFace ) const;
	[[nodiscard]] int NextId();

	void AssignBrushesToModels();
	void ReadEntities();
	[[nodiscard]] std::optional<Solid_t> BuildSolid( const std::vector<BrushPlane_t> &planes, Point_t vecOrigin );
	void BuildBrushes();
	void IndexSides();
	[[nodiscard]] Side_t *FindSideForFace( int nFace );
	void AttachDisplacements();
	void AttachAreaportals();
	void AddDetailAndLadders();
	void AddStaticProps();
	void AddOverlays();
	void AddCubemaps();

	void WriteSide( std::string &szOut, const Solid_t &solid, const Side_t &side, std::string_view szIndent );
	void WriteDisplacement( std::string &szOut, const Side_t &side, std::string_view szIndent ) const;
	void WriteSolid( std::string &szOut, const Solid_t &solid, std::string_view szIndent );
	void WriteEntity( std::string &szOut, std::size_t nEntity );

	std::span<const std::byte> m_File;
	const CBsp &m_Bsp;
	DecompileReport_t &m_Report;

	std::vector<std::byte> m_Planes, m_TexData, m_Vertexes, m_Nodes, m_TexInfo, m_Faces, m_Leafs, m_Edges, m_SurfEdges, m_Models;
	std::vector<std::byte> m_LeafBrushes, m_Brushes, m_BrushSides, m_DispInfo, m_DispVerts, m_DispTris, m_StringData, m_StringTable;
	std::size_t m_nLeafSize = 32;

	std::vector<int> m_BrushModels;          // by brush: the model whose tree holds it; -1 for none
	std::vector<OutEntity_t> m_Entities;     // [ 0 ] is the world
	std::map<int, std::size_t> m_ModelOwner; // by model: the entity that names it
	std::map<int, Point_t> m_ModelOrigin;
	std::vector<Solid_t> m_Solids;
	std::set<int> m_UsedIds;
	int m_nNextId = 1;
	int m_nNextSideId = 1;
	// By plane pair: the sides lying on it, as (solid, side) indices.
	std::unordered_map<int, std::vector<std::pair<std::uint32_t, std::uint32_t>>> m_SidesByPlane;
	std::map<int, int> m_FaceSides; // by face: the id of the side it was found on (0: none)
	std::unordered_map<int, std::string> m_MaterialNames;
};

//-----------------------------------------------------------------------------
// Purpose: A lump's bytes; empty when it is missing or won't unpack.
//-----------------------------------------------------------------------------
std::vector<std::byte> CDecompiler::Lump( int nLump ) const
{
	auto bytes = ReadLump( m_File, m_Bsp.GetLumps()[ static_cast<std::size_t>( nLump ) ] );
	return bytes ? std::move( *bytes ) : std::vector<std::byte>{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Plane_t CDecompiler::PlaneAt( std::size_t nPlane ) const
{
	return { ReadPoint( m_Planes, nPlane * PLANE_SIZE ), ReadAt<float>( m_Planes, nPlane * PLANE_SIZE + 12 ) };
}

//-----------------------------------------------------------------------------
// Purpose: An id no entity of the map has: solids and entities are numbered from one count,
//          and the entities keep the ids Hammer gave them.
//-----------------------------------------------------------------------------
int CDecompiler::NextId()
{
	while ( m_UsedIds.contains( m_nNextId ) )
	{
		m_nNextId++;
	}
	return m_nNextId++;
}

//-----------------------------------------------------------------------------
// Purpose: The material a texinfo names, as the map had it (MapMaterialName).
//-----------------------------------------------------------------------------
std::string CDecompiler::MaterialOf( int nTexInfo )
{
	if ( nTexInfo < 0 || static_cast<std::size_t>( nTexInfo ) >= CountOf( m_TexInfo, TEXINFO_SIZE ) )
	{
		return std::string( NODRAW );
	}
	const int nTexData = ReadAt<std::int32_t>( m_TexInfo, static_cast<std::size_t>( nTexInfo ) * TEXINFO_SIZE + 68 );
	if ( const auto found = m_MaterialNames.find( nTexData ); found != m_MaterialNames.end() )
	{
		return found->second;
	}
	std::string szName( NODRAW );
	if ( nTexData >= 0 && static_cast<std::size_t>( nTexData ) < CountOf( m_TexData, TEXDATA_SIZE ) )
	{
		const int nString = ReadAt<std::int32_t>( m_TexData, static_cast<std::size_t>( nTexData ) * TEXDATA_SIZE + 12 );
		if ( nString >= 0 && static_cast<std::size_t>( nString ) < m_StringTable.size() / 4 )
		{
			const int nAt = ReadAt<std::int32_t>( m_StringTable, static_cast<std::size_t>( nString ) * 4 );
			if ( nAt >= 0 && static_cast<std::size_t>( nAt ) < m_StringData.size() )
			{
				const char *pszName = reinterpret_cast<const char *>( m_StringData.data() ) + nAt;
				szName.assign( pszName, strnlen( pszName, m_StringData.size() - static_cast<std::size_t>( nAt ) ) );
			}
		}
	}
	szName = MapMaterialName( szName );
	m_MaterialNames.emplace( nTexData, szName );
	return szName;
}

//-----------------------------------------------------------------------------
// Purpose: A face's corners, by way of its edges, and the way it faces.
//-----------------------------------------------------------------------------
std::optional<Face_t> CDecompiler::FaceAt( int nFace ) const
{
	if ( nFace < 0 || static_cast<std::size_t>( nFace ) >= CountOf( m_Faces, FACE_SIZE ) )
	{
		return std::nullopt;
	}
	const std::size_t nAt = static_cast<std::size_t>( nFace ) * FACE_SIZE;
	const std::size_t nPlane = ReadAt<std::uint16_t>( m_Faces, nAt );
	const int nFirstEdge = ReadAt<std::int32_t>( m_Faces, nAt + 4 );
	const int nEdges = ReadAt<std::int16_t>( m_Faces, nAt + 8 );
	if ( nPlane >= CountOf( m_Planes, PLANE_SIZE ) || nFirstEdge < 0 || nEdges < 3 || static_cast<std::size_t>( nFirstEdge ) + static_cast<std::size_t>( nEdges ) > m_SurfEdges.size() / 4 )
	{
		return std::nullopt;
	}
	Face_t face;
	face.m_nPlaneKey = static_cast<int>( nPlane >> 1 );
	// The face's plane is the one facing its way already (planes come in opposite pairs, and
	// the face names the one of the pair it faces along).
	face.m_vecNormal = PlaneAt( nPlane ).m_Normal;
	face.m_nTexInfo = ReadAt<std::int16_t>( m_Faces, nAt + 10 );
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
	return face;
}

//-----------------------------------------------------------------------------
// Purpose: Each model (the world is the first, then one for each brush entity) has a tree of
//          its own, whose leaves list the brushes in them: that is the only record of which
//          brushes are a model's.
//-----------------------------------------------------------------------------
void CDecompiler::AssignBrushesToModels()
{
	const std::size_t nBrushes = CountOf( m_Brushes, BRUSH_SIZE );
	const std::size_t nNodes = CountOf( m_Nodes, NODE_SIZE );
	const std::size_t nLeafs = CountOf( m_Leafs, m_nLeafSize );
	m_BrushModels.assign( nBrushes, -1 );
	for ( std::size_t nModel = 0; nModel < CountOf( m_Models, MODEL_SIZE ); nModel++ )
	{
		std::vector<int> stack{ ReadAt<std::int32_t>( m_Models, nModel * MODEL_SIZE + 36 ) };
		std::size_t nVisited = 0;
		while ( !stack.empty() && nVisited++ <= nNodes + nLeafs )
		{
			const int nChild = stack.back();
			stack.pop_back();
			if ( nChild >= 0 )
			{
				if ( static_cast<std::size_t>( nChild ) < nNodes )
				{
					stack.push_back( ReadAt<std::int32_t>( m_Nodes, static_cast<std::size_t>( nChild ) * NODE_SIZE + 4 ) );
					stack.push_back( ReadAt<std::int32_t>( m_Nodes, static_cast<std::size_t>( nChild ) * NODE_SIZE + 8 ) );
				}
				continue;
			}
			// A negative child is a leaf: -1 is leaf 0.
			const std::size_t nLeaf = static_cast<std::size_t>( -1 - nChild );
			if ( nLeaf >= nLeafs )
			{
				continue;
			}
			const std::size_t nFirst = ReadAt<std::uint16_t>( m_Leafs, nLeaf * m_nLeafSize + 24 );
			const std::size_t nCount = ReadAt<std::uint16_t>( m_Leafs, nLeaf * m_nLeafSize + 26 );
			for ( std::size_t i = nFirst; i < nFirst + nCount && i < m_LeafBrushes.size() / 2; i++ )
			{
				const std::size_t nBrush = ReadAt<std::uint16_t>( m_LeafBrushes, i * 2 );
				if ( nBrush < nBrushes && m_BrushModels[ nBrush ] < 0 )
				{
					m_BrushModels[ nBrush ] = static_cast<int>( nModel );
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: The entity lump's entities, as they are written: a brush entity's "model" "*3"
//          (its brushes are written inside it instead), the id Hammer gave it, and its
//          outputs told from its keys. The lump keeps outputs as keys like any other; what
//          gives one away is its value, "target,input,parameter,delay,times" (ESC between
//          them in newer games), ending in two numbers.
//-----------------------------------------------------------------------------
void CDecompiler::ReadEntities()
{
	m_Entities.emplace_back();
	m_Entities[ 0 ].m_bWorld = true;
	const auto isOutput = []( std::string_view szValue ) {
		const char chSeparator = szValue.find( '\x1b' ) != std::string_view::npos ? '\x1b' : ',';
		if ( std::count( szValue.begin(), szValue.end(), chSeparator ) != 4 )
		{
			return false;
		}
		const std::size_t nTimes = szValue.rfind( chSeparator );
		const std::size_t nDelay = szValue.rfind( chSeparator, nTimes - 1 );
		const auto isNumber = []( std::string_view szText ) {
			double flValue = 0.0;
			const auto result = std::from_chars( szText.data(), szText.data() + szText.size(), flValue );
			return result.ec == std::errc{} && result.ptr == szText.data() + szText.size();
		};
		return isNumber( szValue.substr( nTimes + 1 ) ) && isNumber( szValue.substr( nDelay + 1, nTimes - nDelay - 1 ) );
	};
	// The ids Hammer gave are kept where no two entities share one.
	std::map<int, int> idCounts;
	for ( const Entity_t &entity : m_Bsp.GetEntities() )
	{
		idCounts[ std::atoi( std::string( entity.Get( "hammerid" ) ).c_str() ) ]++;
	}
	for ( const auto &[ nId, nCount ] : idCounts )
	{
		if ( nId > 0 && nCount == 1 )
		{
			m_UsedIds.insert( nId );
		}
	}
	bool bWorldRead = false;
	for ( const Entity_t &entity : m_Bsp.GetEntities() )
	{
		const bool bWorld = !bWorldRead && SameName( entity.Get( "classname" ), "worldspawn" );
		if ( !bWorld )
		{
			m_Entities.emplace_back();
		}
		OutEntity_t &out = bWorld ? m_Entities[ 0 ] : m_Entities.back();
		bWorldRead = bWorldRead || bWorld;
		const int nHammerId = std::atoi( std::string( entity.Get( "hammerid" ) ).c_str() );
		out.m_nId = !bWorld && nHammerId > 0 && idCounts[ nHammerId ] == 1 ? nHammerId : 0;
		for ( const auto &[ szKey, szValue ] : entity.m_KeyValues )
		{
			if ( SameName( szKey, "hammerid" ) )
			{
				continue;
			}
			if ( SameName( szKey, "model" ) && szValue.starts_with( '*' ) )
			{
				const int nModel = std::atoi( szValue.c_str() + 1 );
				m_ModelOwner.emplace( nModel, m_Entities.size() - 1 );
				m_ModelOrigin[ nModel ] = ParsePoint( entity.Get( "origin" ) );
				continue;
			}
			if ( !bWorld && isOutput( szValue ) )
			{
				out.m_Outputs.emplace_back( szKey, szValue );
				continue;
			}
			out.m_Keys.emplace_back( szKey, szValue );
		}
	}
	if ( !bWorldRead )
	{
		m_Entities[ 0 ].m_Keys.emplace_back( "classname", "worldspawn" );
	}
	for ( OutEntity_t &entity : m_Entities )
	{
		if ( entity.m_nId == 0 )
		{
			entity.m_nId = NextId();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: A convex solid from its planes: each side is a huge square on its plane cut down
//          by every other plane. Planes that leave no face (the compiler's bevels, a plane
//          given twice) give no side; nullopt when fewer than four are left.
//          vecOrigin moves it: a brush entity with an origin is stored around that origin.
//-----------------------------------------------------------------------------
std::optional<Solid_t> CDecompiler::BuildSolid( const std::vector<BrushPlane_t> &planes, Point_t vecOrigin )
{
	std::vector<BrushPlane_t> moved;
	for ( const BrushPlane_t &plane : planes )
	{
		BrushPlane_t next = plane;
		next.m_Plane.m_flDistance += map::Dot( plane.m_Plane.m_Normal, vecOrigin );
		const bool bRepeated = std::any_of( moved.begin(), moved.end(), [ & ]( const BrushPlane_t &other ) {
			return map::Dot( other.m_Plane.m_Normal, next.m_Plane.m_Normal ) > 1.0 - 1e-6 && std::abs( other.m_Plane.m_flDistance - next.m_Plane.m_flDistance ) < CLIP_EPSILON;
		} );
		if ( !bRepeated && map::Length( next.m_Plane.m_Normal ) > 0.5 )
		{
			moved.push_back( next );
		}
	}
	Solid_t solid;
	solid.m_vecOrigin = vecOrigin;
	std::vector<Point_t> winding;
	std::vector<Point_t> cut;
	for ( std::size_t i = 0; i < moved.size(); i++ )
	{
		map::BaseWinding( moved[ i ].m_Plane, WINDING_EXTENT, winding );
		for ( std::size_t j = 0; j < moved.size() && winding.size() >= 3; j++ )
		{
			if ( j != i )
			{
				map::ClipPolygon( winding, moved[ j ].m_Plane, CLIP_EPSILON, cut );
				winding.swap( cut );
			}
		}
		// Corners the cuts left on top of one another are one corner.
		std::vector<Point_t> polygon;
		for ( Point_t p : winding )
		{
			for ( double &flValue : p )
			{
				if ( std::abs( flValue - std::round( flValue ) ) < SNAP_EPSILON )
				{
					flValue = std::round( flValue );
				}
			}
			if ( polygon.empty() || map::Length( map::Subtract( polygon.back(), p ) ) > CLIP_EPSILON )
			{
				polygon.push_back( p );
			}
		}
		if ( polygon.size() > 1 && map::Length( map::Subtract( polygon.back(), polygon.front() ) ) <= CLIP_EPSILON )
		{
			polygon.pop_back();
		}
		if ( polygon.size() < 3 )
		{
			continue;
		}
		Side_t side;
		side.m_Plane = moved[ i ].m_Plane;
		side.m_Polygon = std::move( polygon );
		side.m_nTexInfo = moved[ i ].m_nTexInfo;
		side.m_nPlaneKey = moved[ i ].m_nPlaneKey;
		// The three corners furthest apart say the plane best, in the order that faces it
		// outwards.
		std::array<Point_t, 3> points{ side.m_Polygon[ 0 ], side.m_Polygon[ 1 ], side.m_Polygon[ 2 ] };
		double flBest = -1.0;
		for ( std::size_t a = 1; a < side.m_Polygon.size(); a++ )
		{
			for ( std::size_t b = a + 1; b < side.m_Polygon.size(); b++ )
			{
				const double flArea = map::Length( map::Cross( map::Subtract( side.m_Polygon[ a ], side.m_Polygon[ 0 ] ), map::Subtract( side.m_Polygon[ b ], side.m_Polygon[ 0 ] ) ) );
				if ( flArea > flBest )
				{
					flBest = flArea;
					points = { side.m_Polygon[ 0 ], side.m_Polygon[ a ], side.m_Polygon[ b ] };
				}
			}
		}
		auto written = map::PlaneFromPoints( points );
		if ( written && map::Dot( written->m_Normal, side.m_Plane.m_Normal ) < 0.0 )
		{
			std::swap( points[ 0 ], points[ 2 ] );
			written = map::PlaneFromPoints( points );
		}
		// A sliver of a side can come out, once its corners are rounded, on the very plane of
		// the side next to it: a map can't have two sides on one plane, and the sliver goes.
		const bool bRepeats = !written || std::any_of( solid.m_Sides.begin(), solid.m_Sides.end(), [ & ]( const Side_t &other ) {
			const auto plane = map::PlaneFromPoints( other.m_Points );
			return plane && map::Dot( plane->m_Normal, written->m_Normal ) > 1.0 - 1e-9 && std::abs( plane->m_flDistance - written->m_flDistance ) < CLIP_EPSILON;
		} );
		if ( bRepeats )
		{
			continue;
		}
		side.m_Points = points;
		solid.m_Sides.push_back( std::move( side ) );
	}
	if ( solid.m_Sides.size() < 4 )
	{
		return std::nullopt;
	}
	for ( Side_t &side : solid.m_Sides )
	{
		side.m_nId = m_nNextSideId++;
	}
	return solid;
}

//-----------------------------------------------------------------------------
// Purpose: Every brush of the lump as a solid, owned by the entity that names its model.
//          Brushes no model's tree holds go to the world.
//-----------------------------------------------------------------------------
void CDecompiler::BuildBrushes()
{
	const std::size_t nBrushes = CountOf( m_Brushes, BRUSH_SIZE );
	const std::size_t nSides = CountOf( m_BrushSides, BRUSHSIDE_SIZE );
	const std::size_t nPlanes = CountOf( m_Planes, PLANE_SIZE );
	int nEmpty = 0;
	for ( std::size_t nBrush = 0; nBrush < nBrushes; nBrush++ )
	{
		const int nFirst = ReadAt<std::int32_t>( m_Brushes, nBrush * BRUSH_SIZE );
		const int nCount = ReadAt<std::int32_t>( m_Brushes, nBrush * BRUSH_SIZE + 4 );
		const std::uint32_t nContents = ReadAt<std::uint32_t>( m_Brushes, nBrush * BRUSH_SIZE + 8 );
		if ( nFirst < 0 || nCount < 4 || static_cast<std::size_t>( nFirst ) + static_cast<std::size_t>( nCount ) > nSides )
		{
			nEmpty++;
			continue;
		}
		std::vector<BrushPlane_t> planes;
		for ( std::size_t i = static_cast<std::size_t>( nFirst ); i < static_cast<std::size_t>( nFirst + nCount ); i++ )
		{
			const std::size_t nPlane = ReadAt<std::uint16_t>( m_BrushSides, i * BRUSHSIDE_SIZE );
			// Bevels are planes the compiler adds for collision; the map never had them.
			const bool bBevel = ReadAt<std::uint8_t>( m_BrushSides, i * BRUSHSIDE_SIZE + 6 ) != 0;
			if ( !bBevel && nPlane < nPlanes )
			{
				planes.push_back( { PlaneAt( nPlane ), ReadAt<std::int16_t>( m_BrushSides, i * BRUSHSIDE_SIZE + 2 ), static_cast<int>( nPlane >> 1 ) } );
			}
		}
		const int nModel = m_BrushModels[ nBrush ];
		const auto owner = m_ModelOwner.find( nModel );
		const Point_t vecOrigin = owner != m_ModelOwner.end() ? m_ModelOrigin[ nModel ] : Point_t{};
		auto solid = BuildSolid( planes, vecOrigin );
		if ( !solid )
		{
			nEmpty++;
			continue;
		}
		solid->m_nContents = nContents;
		solid->m_nOwner = owner != m_ModelOwner.end() ? static_cast<int>( owner->second ) : -1;
		m_Solids.push_back( std::move( *solid ) );
	}
	if ( nEmpty > 0 )
	{
		m_Report.m_Warnings.push_back( std::to_string( nEmpty ) + " brushes had no volume and were left out" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The sides that weren't moved, by the plane they lie on, for finding the side a
//          face of the map was cut from.
//-----------------------------------------------------------------------------
void CDecompiler::IndexSides()
{
	m_SidesByPlane.clear();
	for ( std::uint32_t nSolid = 0; nSolid < m_Solids.size(); nSolid++ )
	{
		const Solid_t &solid = m_Solids[ nSolid ];
		if ( solid.m_vecOrigin != Point_t{} )
		{
			continue;
		}
		for ( std::uint32_t nSide = 0; nSide < solid.m_Sides.size(); nSide++ )
		{
			if ( solid.m_Sides[ nSide ].m_nPlaneKey >= 0 )
			{
				m_SidesByPlane[ solid.m_Sides[ nSide ].m_nPlaneKey ].emplace_back( nSolid, nSide );
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: The brush side a face was cut from: one on the face's plane, facing its way, with
//          the face's middle inside it. Where several brushes' sides overlap there, the one
//          with the face's material is taken. Null when there is none.
//-----------------------------------------------------------------------------
Side_t *CDecompiler::FindSideForFace( int nFace )
{
	const auto face = FaceAt( nFace );
	if ( !face )
	{
		return nullptr;
	}
	const auto sides = m_SidesByPlane.find( face->m_nPlaneKey );
	if ( sides == m_SidesByPlane.end() )
	{
		return nullptr;
	}
	Point_t vecMiddle{};
	for ( const Point_t &p : face->m_Points )
	{
		vecMiddle = map::Add( vecMiddle, p );
	}
	vecMiddle = map::Scale( vecMiddle, 1.0 / static_cast<double>( face->m_Points.size() ) );
	const std::string szMaterial = MaterialOf( face->m_nTexInfo );
	Side_t *pFound = nullptr;
	for ( const auto &[ nSolid, nSide ] : sides->second )
	{
		Side_t &side = m_Solids[ nSolid ].m_Sides[ nSide ];
		if ( map::Dot( side.m_Plane.m_Normal, face->m_vecNormal ) < 0.9 )
		{
			continue;
		}
		bool bInside = true;
		for ( std::size_t i = 0; i < side.m_Polygon.size() && bInside; i++ )
		{
			const Point_t a = side.m_Polygon[ i ];
			const Point_t b = side.m_Polygon[ ( i + 1 ) % side.m_Polygon.size() ];
			// The polygon goes clockwise seen from in front: inside is to the right of each edge.
			bInside = map::Dot( map::Cross( map::Subtract( b, a ), map::Subtract( vecMiddle, a ) ), side.m_Plane.m_Normal ) <= 0.5 * map::Length( map::Subtract( b, a ) );
		}
		if ( !bInside )
		{
			continue;
		}
		if ( SameName( MaterialOf( side.m_nTexInfo ), szMaterial ) )
		{
			return &side;
		}
		pFound = pFound ? pFound : &side;
	}
	return pFound;
}

//-----------------------------------------------------------------------------
// Purpose: Each displacement goes back on the brush side its face came from. The compiler
//          doesn't always keep a displacement's brush; then one is made under the face, its
//          other sides nodraw.
//-----------------------------------------------------------------------------
void CDecompiler::AttachDisplacements()
{
	int nLost = 0;
	for ( std::size_t nDisp = 0; nDisp < CountOf( m_DispInfo, DISPINFO_SIZE ); nDisp++ )
	{
		const std::size_t nAt = nDisp * DISPINFO_SIZE;
		const int nPower = ReadAt<std::int32_t>( m_DispInfo, nAt + 20 );
		const int nFirstVert = ReadAt<std::int32_t>( m_DispInfo, nAt + 12 );
		const int nFace = ReadAt<std::uint16_t>( m_DispInfo, nAt + 36 );
		const std::size_t nVerts = nPower >= 2 && nPower <= 4 ? static_cast<std::size_t>( ( ( 1 << nPower ) + 1 ) * ( ( 1 << nPower ) + 1 ) ) : 0;
		const auto face = FaceAt( nFace );
		if ( nVerts == 0 || nFirstVert < 0 || static_cast<std::size_t>( nFirstVert ) + nVerts > CountOf( m_DispVerts, DISP_VERT_SIZE ) || !face || face->m_Points.size() != 4 )
		{
			nLost++;
			continue;
		}
		Side_t *pSide = FindSideForFace( nFace );
		if ( !pSide || pSide->m_Polygon.size() != 4 || pSide->m_nDisp >= 0 )
		{
			// A slab under the face: the face's plane, one through each edge, and the back.
			Point_t vecMiddle{};
			for ( const Point_t &p : face->m_Points )
			{
				vecMiddle = map::Add( vecMiddle, map::Scale( p, 0.25 ) );
			}
			const Point_t n = face->m_vecNormal;
			std::vector<BrushPlane_t> planes;
			planes.push_back( { { n, map::Dot( n, face->m_Points[ 0 ] ) }, face->m_nTexInfo, -1 } );
			planes.push_back( { { map::Scale( n, -1.0 ), -( map::Dot( n, face->m_Points[ 0 ] ) - DISPLACEMENT_THICKNESS ) }, -1, -1 } );
			for ( std::size_t i = 0; i < 4; i++ )
			{
				const Point_t a = face->m_Points[ i ];
				Point_t vecOut = map::Normalized( map::Cross( map::Subtract( face->m_Points[ ( i + 1 ) % 4 ], a ), n ) );
				if ( map::Dot( vecOut, map::Subtract( vecMiddle, a ) ) > 0.0 )
				{
					vecOut = map::Scale( vecOut, -1.0 );
				}
				planes.push_back( { { vecOut, map::Dot( vecOut, a ) }, -1, -1 } );
			}
			auto solid = BuildSolid( planes, {} );
			if ( !solid || solid->m_Sides[ 0 ].m_Polygon.size() != 4 || map::Dot( solid->m_Sides[ 0 ].m_Plane.m_Normal, n ) < 0.99 )
			{
				nLost++;
				continue;
			}
			m_Solids.push_back( std::move( *solid ) );
			pSide = &m_Solids.back().m_Sides[ 0 ];
		}
		pSide->m_nDisp = static_cast<int>( nDisp );
		m_FaceSides[ nFace ] = pSide->m_nId;
		m_Report.m_nDisplacements++;
	}
	if ( nLost > 0 )
	{
		m_Report.m_Warnings.push_back( std::to_string( nLost ) + " displacements couldn't be put back on a brush" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: An areaportal's brush is compiled into the world, and its entity keeps only a
//          number. The portal lump has that number with the portal's outline: the areaportal
//          brush around the outline is the entity's.
//-----------------------------------------------------------------------------
void CDecompiler::AttachAreaportals()
{
	const std::vector<std::byte> portals = Lump( LUMP_AREAPORTALS );
	const std::vector<std::byte> verts = Lump( LUMP_CLIP_PORTAL_VERTS );
	for ( std::size_t nEntity = 1; nEntity < m_Entities.size(); nEntity++ )
	{
		const OutEntity_t &entity = m_Entities[ nEntity ];
		const auto key = std::find_if( entity.m_Keys.begin(), entity.m_Keys.end(), []( const auto &pair ) { return SameName( pair.first, "portalnumber" ); } );
		if ( key == entity.m_Keys.end() )
		{
			continue;
		}
		const int nNumber = std::atoi( key->second.c_str() );
		std::vector<Point_t> outline;
		for ( std::size_t i = 0; i < CountOf( portals, AREAPORTAL_SIZE ) && outline.empty(); i++ )
		{
			if ( ReadAt<std::uint16_t>( portals, i * AREAPORTAL_SIZE ) != nNumber )
			{
				continue;
			}
			const std::size_t nFirst = ReadAt<std::uint16_t>( portals, i * AREAPORTAL_SIZE + 4 );
			const std::size_t nCount = ReadAt<std::uint16_t>( portals, i * AREAPORTAL_SIZE + 6 );
			for ( std::size_t j = nFirst; j < nFirst + nCount && j < verts.size() / 12; j++ )
			{
				outline.push_back( ReadPoint( verts, j * 12 ) );
			}
		}
		if ( outline.size() < 3 )
		{
			continue;
		}
		for ( Solid_t &solid : m_Solids )
		{
			if ( solid.m_nOwner >= 0 || !( solid.m_nContents & CONTENTS_AREAPORTAL ) )
			{
				continue;
			}
			const bool bAround = std::all_of( outline.begin(), outline.end(), [ & ]( const Point_t &p ) {
				return std::all_of( solid.m_Sides.begin(), solid.m_Sides.end(), [ & ]( const Side_t &side ) { return side.m_Plane.Distance( p ) < 1.0; } );
			} );
			if ( bAround )
			{
				solid.m_nOwner = static_cast<int>( nEntity );
				break;
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: func_detail and (in Counter-Strike) func_ladder brushes are compiled into the
//          world and their entities dropped; the brushes' contents say what they were. Each
//          gets an entity of its own: which brushes shared one isn't recorded.
//-----------------------------------------------------------------------------
void CDecompiler::AddDetailAndLadders()
{
	for ( Solid_t &solid : m_Solids )
	{
		if ( solid.m_nOwner >= 0 || !( solid.m_nContents & ( CONTENTS_DETAIL | CONTENTS_LADDER ) ) )
		{
			continue;
		}
		OutEntity_t entity;
		entity.m_nId = NextId();
		entity.m_Keys.emplace_back( "classname", solid.m_nContents & CONTENTS_LADDER ? "func_ladder" : "func_detail" );
		solid.m_nOwner = static_cast<int>( m_Entities.size() );
		m_Entities.push_back( std::move( entity ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: prop_static entities from the game lump's static props: the model names, then
//          the leaves they are in, then the props. A prop's record grew with each version of
//          the lump, always at its end, so its size is found from the bytes left and only
//          the part every version has is read (with the flags where the size says they are).
//-----------------------------------------------------------------------------
void CDecompiler::AddStaticProps()
{
	const Lump_t &gameLump = m_Bsp.GetLumps()[ LUMP_GAME ];
	const std::vector<std::byte> directory = Lump( LUMP_GAME );
	if ( directory.size() < 4 )
	{
		return;
	}
	const std::size_t nLumps = static_cast<std::size_t>( std::max( ReadAt<std::int32_t>( directory, 0 ), 0 ) );
	for ( std::size_t i = 0; i < nLumps && 4 + ( i + 1 ) * 16 <= directory.size(); i++ )
	{
		const std::size_t nAt = 4 + i * 16;
		if ( ReadAt<std::uint32_t>( directory, nAt ) != STATIC_PROP_LUMP_ID )
		{
			continue;
		}
		const int nVersion = ReadAt<std::uint16_t>( directory, nAt + 6 );
		const std::size_t nOffset = ReadAt<std::uint32_t>( directory, nAt + 8 );
		std::size_t nLength = ReadAt<std::uint32_t>( directory, nAt + 12 );
		// A compressed game lump's length is its size unpacked: it runs to the next one.
		if ( ReadAt<std::uint16_t>( directory, nAt + 4 ) & 1 )
		{
			const std::size_t nNext = 4 + ( i + 2 ) * 16 <= directory.size() ? ReadAt<std::uint32_t>( directory, nAt + 16 + 8 ) : gameLump.m_nOffset + gameLump.m_nLength;
			nLength = nNext > nOffset ? nNext - nOffset : 0;
		}
		if ( nOffset + nLength > m_File.size() )
		{
			continue;
		}
		const auto unpacked = UnpackIfCompressed( m_File.subspan( nOffset, nLength ) );
		if ( !unpacked )
		{
			m_Report.m_Warnings.emplace_back( "the static props wouldn't unpack" );
			return;
		}
		const std::vector<std::byte> &props = *unpacked;
		std::size_t nRead = 0;
		const auto count = [ & ]() -> std::size_t {
			if ( nRead + 4 > props.size() )
			{
				nRead = props.size();
				return 0;
			}
			const int nCount = ReadAt<std::int32_t>( props, nRead );
			nRead += 4;
			return static_cast<std::size_t>( std::max( nCount, 0 ) );
		};
		const std::size_t nNames = count();
		const std::size_t nNamesAt = nRead;
		nRead += nNames * STATIC_PROP_NAME_SIZE;
		const std::size_t nLeaves = nRead <= props.size() ? count() : 0;
		nRead += nLeaves * 2;
		const std::size_t nProps = nRead <= props.size() ? count() : 0;
		if ( nProps == 0 || nRead >= props.size() )
		{
			return;
		}
		const std::size_t nSize = ( props.size() - nRead ) / nProps;
		if ( nSize < STATIC_PROP_BASE_SIZE || nSize * nProps != props.size() - nRead )
		{
			m_Report.m_Warnings.push_back( "the static props (version " + std::to_string( nVersion ) + ") aren't in a layout this reads" );
			return;
		}
		for ( std::size_t nProp = 0; nProp < nProps; nProp++ )
		{
			const std::size_t nPropAt = nRead + nProp * nSize;
			const std::size_t nName = ReadAt<std::uint16_t>( props, nPropAt + 24 );
			if ( nName >= nNames )
			{
				continue;
			}
			const char *pszModel = reinterpret_cast<const char *>( props.data() ) + nNamesAt + nName * STATIC_PROP_NAME_SIZE;
			// The 2013 games' version 10 has no flags byte after "solid": they are 32 bits further on.
			const std::uint32_t nFlags = nVersion == 10 && nSize == 72 ? ReadAt<std::uint32_t>( props, nPropAt + 64 ) : ReadAt<std::uint8_t>( props, nPropAt + 31 );
			OutEntity_t entity;
			entity.m_nId = NextId();
			entity.m_Keys.emplace_back( "classname", "prop_static" );
			entity.m_Keys.emplace_back( "angles", Vec( ReadPoint( props, nPropAt + 12 ) ) );
			entity.m_Keys.emplace_back( "model", std::string( pszModel, strnlen( pszModel, STATIC_PROP_NAME_SIZE ) ) );
			entity.m_Keys.emplace_back( "skin", std::to_string( ReadAt<std::int32_t>( props, nPropAt + 32 ) ) );
			entity.m_Keys.emplace_back( "solid", std::to_string( ReadAt<std::uint8_t>( props, nPropAt + 30 ) ) );
			entity.m_Keys.emplace_back( "fademindist", Num( ReadAt<float>( props, nPropAt + 36 ) ) );
			entity.m_Keys.emplace_back( "fademaxdist", Num( ReadAt<float>( props, nPropAt + 40 ) ) );
			if ( nSize >= 60 )
			{
				entity.m_Keys.emplace_back( "fadescale", Num( ReadAt<float>( props, nPropAt + 56 ) ) );
			}
			entity.m_Keys.emplace_back( "ignorenormals", nFlags & 0x8 ? "1" : "0" );
			entity.m_Keys.emplace_back( "disableshadows", nFlags & 0x10 ? "1" : "0" );
			entity.m_Keys.emplace_back( "screenspacefade", nFlags & 0x20 ? "1" : "0" );
			entity.m_Keys.emplace_back( "disablevertexlighting", nFlags & 0x40 ? "1" : "0" );
			entity.m_Keys.emplace_back( "disableselfshadowing", nFlags & 0x80 ? "1" : "0" );
			entity.m_Keys.emplace_back( "origin", Vec( ReadPoint( props, nPropAt ) ) );
			m_Entities.push_back( std::move( entity ) );
			m_Report.m_nStaticProps++;
		}
		return;
	}
}

//-----------------------------------------------------------------------------
// Purpose: info_overlay entities from the overlay lump. The lump lists the faces an overlay
//          lies on, where the map listed brush sides: each face's side is found again. The
//          U axis is kept in the spare thirds of the first three corners, and whether V
//          runs the other way in the fourth's.
//-----------------------------------------------------------------------------
void CDecompiler::AddOverlays()
{
	const std::vector<std::byte> overlays = Lump( LUMP_OVERLAYS );
	int nLoose = 0;
	for ( std::size_t nOverlay = 0; nOverlay < CountOf( overlays, OVERLAY_SIZE ); nOverlay++ )
	{
		const std::size_t nAt = nOverlay * OVERLAY_SIZE;
		const std::uint16_t nCountAndOrder = ReadAt<std::uint16_t>( overlays, nAt + 6 );
		const int nFaces = std::min<int>( nCountAndOrder & 0x3fff, OVERLAY_FACES );
		std::vector<int> sides;
		for ( int i = 0; i < nFaces; i++ )
		{
			const int nFace = ReadAt<std::int32_t>( overlays, nAt + 8 + static_cast<std::size_t>( i ) * 4 );
			auto found = m_FaceSides.find( nFace );
			if ( found == m_FaceSides.end() )
			{
				const Side_t *pSide = FindSideForFace( nFace );
				found = m_FaceSides.emplace( nFace, pSide ? pSide->m_nId : 0 ).first;
			}
			if ( found->second != 0 && std::find( sides.begin(), sides.end(), found->second ) == sides.end() )
			{
				sides.push_back( found->second );
			}
		}
		// An overlay the compiler found no face under has none listed, and nothing is lost.
		if ( sides.empty() && nFaces > 0 )
		{
			nLoose++;
		}
		const std::size_t nValues = nAt + 8 + OVERLAY_FACES * 4;
		std::array<Point_t, 4> corners;
		for ( std::size_t i = 0; i < 4; i++ )
		{
			corners[ i ] = ReadPoint( overlays, nValues + 16 + i * 12 );
		}
		const Point_t vecOrigin = ReadPoint( overlays, nValues + 64 );
		const Point_t vecNormal = ReadPoint( overlays, nValues + 76 );
		const Point_t vecU{ corners[ 0 ][ 2 ], corners[ 1 ][ 2 ], corners[ 2 ][ 2 ] };
		const Point_t vecV = map::Scale( map::Cross( vecNormal, vecU ), corners[ 3 ][ 2 ] == 1.0 ? -1.0 : 1.0 );
		std::string szSides;
		for ( const int nSide : sides )
		{
			szSides += ( szSides.empty() ? "" : " " ) + std::to_string( nSide );
		}
		OutEntity_t entity;
		entity.m_nId = NextId();
		entity.m_Keys.emplace_back( "classname", "info_overlay" );
		entity.m_Keys.emplace_back( "angles", "0 0 0" );
		entity.m_Keys.emplace_back( "BasisNormal", Vec( vecNormal, 6 ) );
		entity.m_Keys.emplace_back( "BasisOrigin", Vec( vecOrigin ) );
		entity.m_Keys.emplace_back( "BasisU", Vec( vecU, 6 ) );
		entity.m_Keys.emplace_back( "BasisV", Vec( vecV, 6 ) );
		entity.m_Keys.emplace_back( "EndU", Num( ReadAt<float>( overlays, nValues + 4 ), 6 ) );
		entity.m_Keys.emplace_back( "EndV", Num( ReadAt<float>( overlays, nValues + 12 ), 6 ) );
		entity.m_Keys.emplace_back( "fademindist", "-1" );
		entity.m_Keys.emplace_back( "material", MaterialOf( ReadAt<std::int16_t>( overlays, nAt + 4 ) ) );
		entity.m_Keys.emplace_back( "RenderOrder", std::to_string( nCountAndOrder >> 14 ) );
		entity.m_Keys.emplace_back( "sides", szSides );
		entity.m_Keys.emplace_back( "StartU", Num( ReadAt<float>( overlays, nValues ), 6 ) );
		entity.m_Keys.emplace_back( "StartV", Num( ReadAt<float>( overlays, nValues + 8 ), 6 ) );
		for ( std::size_t i = 0; i < 4; i++ )
		{
			entity.m_Keys.emplace_back( "uv" + std::to_string( i ), Vec( { corners[ i ][ 0 ], corners[ i ][ 1 ], 0.0 } ) );
		}
		entity.m_Keys.emplace_back( "origin", Vec( vecOrigin ) );
		m_Entities.push_back( std::move( entity ) );
		m_Report.m_nOverlays++;
	}
	if ( nLoose > 0 )
	{
		m_Report.m_Warnings.push_back( std::to_string( nLoose ) + " overlays' faces weren't found on any brush" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: env_cubemap entities from the cubemap lump: a place and a size each.
//-----------------------------------------------------------------------------
void CDecompiler::AddCubemaps()
{
	const std::vector<std::byte> cubemaps = Lump( LUMP_CUBEMAPS );
	for ( std::size_t i = 0; i < CountOf( cubemaps, CUBEMAP_SIZE ); i++ )
	{
		OutEntity_t entity;
		entity.m_nId = NextId();
		entity.m_Keys.emplace_back( "classname", "env_cubemap" );
		entity.m_Keys.emplace_back( "cubemapsize", std::to_string( ReadAt<std::int32_t>( cubemaps, i * CUBEMAP_SIZE + 12 ) & 0xff ) );
		entity.m_Keys.emplace_back( "origin", std::to_string( ReadAt<std::int32_t>( cubemaps, i * CUBEMAP_SIZE ) ) + " " + std::to_string( ReadAt<std::int32_t>( cubemaps, i * CUBEMAP_SIZE + 4 ) ) + " " + std::to_string( ReadAt<std::int32_t>( cubemaps, i * CUBEMAP_SIZE + 8 ) ) );
		m_Entities.push_back( std::move( entity ) );
		m_Report.m_nCubemaps++;
	}
}

//-----------------------------------------------------------------------------
// Purpose: A displacement as Hammer writes one. The lump's points are in the map's own order
//          (row by row from the start corner), each a direction and a distance along it.
//-----------------------------------------------------------------------------
void CDecompiler::WriteDisplacement( std::string &szOut, const Side_t &side, std::string_view szIndent ) const
{
	const std::size_t nAt = static_cast<std::size_t>( side.m_nDisp ) * DISPINFO_SIZE;
	const int nPower = ReadAt<std::int32_t>( m_DispInfo, nAt + 20 );
	const std::size_t nSize = ( std::size_t{ 1 } << nPower ) + 1;
	const std::size_t nFirstVert = static_cast<std::size_t>( ReadAt<std::int32_t>( m_DispInfo, nAt + 12 ) );
	const int nFirstTri = ReadAt<std::int32_t>( m_DispInfo, nAt + 16 );
	const std::string szIn = std::string( szIndent ) + "\t";
	const std::string szRow = szIn + "\t";
	const auto rows = [ & ]( std::string_view szName, std::size_t nColumns, std::size_t nRows, const auto &pfnValue ) {
		szOut += szIn + std::string( szName ) + "\n" + szIn + "{\n";
		for ( std::size_t nRowAt = 0; nRowAt < nRows; nRowAt++ )
		{
			szOut += szRow + "\"row" + std::to_string( nRowAt ) + "\" \"";
			for ( std::size_t nColumn = 0; nColumn < nColumns; nColumn++ )
			{
				szOut += ( nColumn ? " " : "" ) + pfnValue( nRowAt * nColumns + nColumn );
			}
			szOut += "\"\n";
		}
		szOut += szIn + "}\n";
	};
	szOut += std::string( szIndent ) + "dispinfo\n" + std::string( szIndent ) + "{\n";
	szOut += szIn + "\"power\" \"" + std::to_string( nPower ) + "\"\n";
	szOut += szIn + "\"startposition\" \"[" + Vec( ReadPoint( m_DispInfo, nAt ) ) + "]\"\n";
	szOut += szIn + "\"flags\" \"0\"\n" + szIn + "\"elevation\" \"0\"\n" + szIn + "\"subdiv\" \"0\"\n";
	rows( "normals", nSize, nSize, [ & ]( std::size_t i ) { return Vec( ReadPoint( m_DispVerts, ( nFirstVert + i ) * DISP_VERT_SIZE ), 6 ); } );
	rows( "distances", nSize, nSize, [ & ]( std::size_t i ) { return Num( ReadAt<float>( m_DispVerts, ( nFirstVert + i ) * DISP_VERT_SIZE + 12 ) ); } );
	rows( "offsets", nSize, nSize, []( std::size_t ) { return std::string( "0 0 0" ); } );
	rows( "offset_normals", nSize, nSize, [ & ]( std::size_t ) { return Vec( side.m_Plane.m_Normal, 6 ); } );
	rows( "alphas", nSize, nSize, [ & ]( std::size_t i ) { return Num( ReadAt<float>( m_DispVerts, ( nFirstVert + i ) * DISP_VERT_SIZE + 16 ) ); } );
	const std::size_t nTris = 2 * ( nSize - 1 ) * ( nSize - 1 );
	if ( nFirstTri >= 0 && static_cast<std::size_t>( nFirstTri ) + nTris <= m_DispTris.size() / 2 )
	{
		rows( "triangle_tags", 2 * ( nSize - 1 ), nSize - 1, [ & ]( std::size_t i ) { return std::to_string( ReadAt<std::uint16_t>( m_DispTris, ( static_cast<std::size_t>( nFirstTri ) + i ) * 2 ) ); } );
	}
	szOut += szIn + "allowed_verts\n" + szIn + "{\n" + szRow + "\"10\" \"-1 -1 -1 -1 -1 -1 -1 -1 -1 -1\"\n" + szIn + "}\n";
	szOut += std::string( szIndent ) + "}\n";
}

//-----------------------------------------------------------------------------
// Purpose: A side: the three corners its plane is written as, its material and its texture
//          axes. The
//          compiler stores an axis already divided by its scale, with the shift after it;
//          for a brush moved to its entity's origin the shift was worked out there.
//-----------------------------------------------------------------------------
void CDecompiler::WriteSide( std::string &szOut, const Solid_t &solid, const Side_t &side, std::string_view szIndent )
{
	const std::array<Point_t, 3> &points = side.m_Points;

	// Hammer's axes for a face nothing says how to texture: along the world's axes least
	// like the normal.
	const Point_t n = side.m_Plane.m_Normal;
	const bool bFloor = std::abs( n[ 2 ] ) >= std::abs( n[ 0 ] ) && std::abs( n[ 2 ] ) >= std::abs( n[ 1 ] );
	const bool bEastWest = !bFloor && std::abs( n[ 0 ] ) >= std::abs( n[ 1 ] );
	std::array<Point_t, 2> axes{ bEastWest ? Point_t{ 0, 1, 0 } : Point_t{ 1, 0, 0 }, bFloor ? Point_t{ 0, -1, 0 } : Point_t{ 0, 0, -1 } };
	std::array<double, 2> shifts{ 0.0, 0.0 };
	std::array<double, 2> scales{ 0.25, 0.25 };
	int nLightmapScale = 16;
	if ( side.m_nTexInfo >= 0 && static_cast<std::size_t>( side.m_nTexInfo ) < CountOf( m_TexInfo, TEXINFO_SIZE ) )
	{
		const std::size_t nAt = static_cast<std::size_t>( side.m_nTexInfo ) * TEXINFO_SIZE;
		// The compiler gives every nodraw face one texinfo, whatever way the face looks: axes
		// that lie along the face's normal texture nothing, and Hammer's own are kept.
		const Point_t vecU = ReadPoint( m_TexInfo, nAt );
		const Point_t vecV = ReadPoint( m_TexInfo, nAt + 16 );
		const Point_t vecAcross = map::Cross( vecU, vecV );
		if ( map::Length( vecAcross ) > 1e-9 && std::abs( map::Dot( map::Normalized( vecAcross ), n ) ) > 0.01 )
		{
			for ( std::size_t i = 0; i < 2; i++ )
			{
				const Point_t vecAxis = i == 0 ? vecU : vecV;
				const double flLength = map::Length( vecAxis );
				axes[ i ] = map::Scale( vecAxis, 1.0 / flLength );
				scales[ i ] = 1.0 / flLength;
				shifts[ i ] = ReadAt<float>( m_TexInfo, nAt + i * 16 + 12 ) - map::Dot( solid.m_vecOrigin, vecAxis );
			}
		}
		if ( const double flLuxels = map::Length( ReadPoint( m_TexInfo, nAt + 32 ) ); flLuxels > 1e-6 )
		{
			nLightmapScale = std::max( 1, static_cast<int>( std::lround( 1.0 / flLuxels ) ) );
		}
	}
	const std::string szIn = std::string( szIndent ) + "\t";
	szOut += std::string( szIndent ) + "side\n" + std::string( szIndent ) + "{\n";
	szOut += szIn + "\"id\" \"" + std::to_string( side.m_nId ) + "\"\n";
	szOut += szIn + "\"plane\" \"(" + Vec( points[ 0 ] ) + ") (" + Vec( points[ 1 ] ) + ") (" + Vec( points[ 2 ] ) + ")\"\n";
	szOut += szIn + "\"material\" \"" + MaterialOf( side.m_nTexInfo ) + "\"\n";
	szOut += szIn + "\"uaxis\" \"[" + Vec( axes[ 0 ], 6 ) + " " + Num( shifts[ 0 ] ) + "] " + Num( scales[ 0 ], 6 ) + "\"\n";
	szOut += szIn + "\"vaxis\" \"[" + Vec( axes[ 1 ], 6 ) + " " + Num( shifts[ 1 ] ) + "] " + Num( scales[ 1 ], 6 ) + "\"\n";
	szOut += szIn + "\"rotation\" \"0\"\n";
	szOut += szIn + "\"lightmapscale\" \"" + std::to_string( nLightmapScale ) + "\"\n";
	szOut += szIn + "\"smoothing_groups\" \"0\"\n";
	if ( side.m_nDisp >= 0 )
	{
		WriteDisplacement( szOut, side, szIn );
	}
	szOut += std::string( szIndent ) + "}\n";
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDecompiler::WriteSolid( std::string &szOut, const Solid_t &solid, std::string_view szIndent )
{
	const std::string szIn = std::string( szIndent ) + "\t";
	szOut += std::string( szIndent ) + "solid\n" + std::string( szIndent ) + "{\n";
	szOut += szIn + "\"id\" \"" + std::to_string( solid.m_nId ) + "\"\n";
	for ( const Side_t &side : solid.m_Sides )
	{
		WriteSide( szOut, solid, side, szIn );
	}
	szOut += szIn + "editor\n" + szIn + "{\n" + szIn + "\t\"color\" \"0 180 0\"\n" + szIn + "\t\"visgroupshown\" \"1\"\n" + szIn + "\t\"visgroupautoshown\" \"1\"\n" + szIn + "}\n";
	szOut += std::string( szIndent ) + "}\n";
	m_Report.m_nSolids++;
}

//-----------------------------------------------------------------------------
// Purpose: The world or an entity: its keys, its outputs, then its solids.
//-----------------------------------------------------------------------------
void CDecompiler::WriteEntity( std::string &szOut, std::size_t nEntity )
{
	const OutEntity_t &entity = m_Entities[ nEntity ];
	szOut += entity.m_bWorld ? "world\n{\n" : "entity\n{\n";
	szOut += "\t\"id\" \"" + std::to_string( entity.m_nId ) + "\"\n";
	for ( const auto &[ szKey, szValue ] : entity.m_Keys )
	{
		// A quote can't be written inside a value; the map's text has no way to say one.
		std::string szClean = szValue;
		std::replace( szClean.begin(), szClean.end(), '"', '\'' );
		szOut += "\t\"" + szKey + "\" \"" + szClean + "\"\n";
	}
	if ( !entity.m_Outputs.empty() )
	{
		szOut += "\tconnections\n\t{\n";
		for ( const auto &[ szKey, szValue ] : entity.m_Outputs )
		{
			szOut += "\t\t\"" + szKey + "\" \"" + szValue + "\"\n";
		}
		szOut += "\t}\n";
	}
	const int nOwner = entity.m_bWorld ? -1 : static_cast<int>( nEntity );
	for ( const Solid_t &solid : m_Solids )
	{
		if ( solid.m_nOwner == nOwner )
		{
			WriteSolid( szOut, solid, "\t" );
		}
	}
	if ( !entity.m_bWorld )
	{
		szOut += "\teditor\n\t{\n\t\t\"color\" \"220 30 220\"\n\t\t\"visgroupshown\" \"1\"\n\t\t\"visgroupautoshown\" \"1\"\n\t\t\"logicalpos\" \"[0 0]\"\n\t}\n";
		m_Report.m_nEntities++;
	}
	szOut += "}\n";
}

//-----------------------------------------------------------------------------
// Purpose: The lumps read, the brushes built and given their owners, what the compiler
//          stored elsewhere made into entities again, then the text.
//-----------------------------------------------------------------------------
bool CDecompiler::Run( std::string &szOut, std::string &szError )
{
	m_Planes = Lump( LUMP_PLANES );
	m_Brushes = Lump( LUMP_BRUSHES );
	m_BrushSides = Lump( LUMP_BRUSHSIDES );
	if ( m_Planes.empty() || m_Brushes.empty() || m_BrushSides.empty() )
	{
		szError = "the map has no brushes to read (its plane or brush lumps are missing)";
		return false;
	}
	m_TexData = Lump( LUMP_TEXDATA );
	m_Vertexes = Lump( LUMP_VERTEXES );
	m_Nodes = Lump( LUMP_NODES );
	m_TexInfo = Lump( LUMP_TEXINFO );
	// A map compiled for HDR only has its faces in the HDR lump; the shapes are the same.
	m_Faces = Lump( LUMP_FACES );
	if ( m_Faces.empty() )
	{
		m_Faces = Lump( LUMP_FACES_HDR );
	}
	m_Leafs = Lump( LUMP_LEAFS );
	// The first leaves (version 0) carry their ambient light with them.
	m_nLeafSize = m_Bsp.GetLumps()[ LUMP_LEAFS ].m_nVersion == 0 && m_Bsp.GetVersion() < 20 ? 56 : 32;
	m_Edges = Lump( LUMP_EDGES );
	m_SurfEdges = Lump( LUMP_SURFEDGES );
	m_Models = Lump( LUMP_MODELS );
	m_LeafBrushes = Lump( LUMP_LEAFBRUSHES );
	m_DispInfo = Lump( LUMP_DISPINFO );
	m_DispVerts = Lump( LUMP_DISP_VERTS );
	m_DispTris = Lump( LUMP_DISP_TRIS );
	m_StringData = Lump( LUMP_TEXDATA_STRING_DATA );
	m_StringTable = Lump( LUMP_TEXDATA_STRING_TABLE );

	AssignBrushesToModels();
	ReadEntities();
	BuildBrushes();
	IndexSides();
	AttachDisplacements();
	// The solids made for displacements have sides too, but none an overlay's faces lie on
	// that the displacement's own face doesn't already answer for.
	AddOverlays();
	AttachAreaportals();
	AddDetailAndLadders();
	AddStaticProps();
	AddCubemaps();
	for ( Solid_t &solid : m_Solids )
	{
		solid.m_nId = NextId();
	}

	szOut.reserve( m_Solids.size() * 2048 );
	szOut += "versioninfo\n{\n\t\"editorversion\" \"400\"\n\t\"editorbuild\" \"8000\"\n\t\"mapversion\" \"" + std::to_string( std::max( m_Bsp.GetRevision(), 1 ) ) + "\"\n\t\"formatversion\" \"100\"\n\t\"prefab\" \"0\"\n}\n";
	szOut += "visgroups\n{\n}\n";
	szOut += "viewsettings\n{\n\t\"bSnapToGrid\" \"1\"\n\t\"bShowGrid\" \"1\"\n\t\"bShowLogicalGrid\" \"0\"\n\t\"nGridSpacing\" \"64\"\n\t\"bShow3DGrid\" \"0\"\n}\n";
	for ( std::size_t nEntity = 0; nEntity < m_Entities.size(); nEntity++ )
	{
		WriteEntity( szOut, nEntity );
	}
	szOut += "cameras\n{\n\t\"activecamera\" \"-1\"\n}\n";
	szOut += "cordon\n{\n\t\"mins\" \"(-1024 -1024 -1024)\"\n\t\"maxs\" \"(1024 1024 1024)\"\n\t\"active\" \"0\"\n}\n";
	return true;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::string> DecompileToVmf( std::span<const std::byte> file, DecompileReport_t *pReport, std::string *pError )
{
	std::string szError;
	const auto bsp = CBsp::Parse( file, &szError );
	DecompileReport_t report;
	std::string szOut;
	if ( !bsp || !CDecompiler( file, *bsp, report ).Run( szOut, szError ) )
	{
		if ( pError )
		{
			*pError = szError;
		}
		return std::nullopt;
	}
	if ( !bsp->HasEntities() )
	{
		report.m_Warnings.emplace_back( "the entity list wouldn't unpack: only the brushes are shown" );
	}
	if ( pReport )
	{
		*pReport = std::move( report );
	}
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose: The compiler makes copies of materials for the cubemap each face reflects
//          ("maps/<map>/brick/wall_128_-64_32"), for water's depth ("..._depth_-128") and for
//          blends on plain brushes ("..._wvt_patch"). The copies can stack (a blend's patch,
//          then that patch's cubemap copy): the folder comes off once, the endings until none
//          is left.
//-----------------------------------------------------------------------------
std::string MapMaterialName( std::string_view szStored )
{
	static const std::regex folder( R"(^maps/[^/]+/(.+)$)", std::regex::icase );
	static const std::regex ending( R"(^(.+?)(?:_-?\d+_-?\d+_-?\d+|_depth_-?\d+|_wvt_patch)$)", std::regex::icase );
	std::string szName( szStored );
	std::string szSlashed = szName;
	std::replace( szSlashed.begin(), szSlashed.end(), '\\', '/' );
	std::smatch match;
	if ( std::regex_match( szSlashed, match, folder ) )
	{
		szName = match[ 1 ].str();
		while ( std::regex_match( szName, match, ending ) )
		{
			szName = match[ 1 ].str();
		}
	}
	return szName;
}

} // namespace xv::bsp
