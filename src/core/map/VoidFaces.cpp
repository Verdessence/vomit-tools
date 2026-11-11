//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See VoidFaces.h.
//
//===========================================================================//
#include "VoidFaces.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <memory>

#include "map/Brushes.h"
#include "map/Faces.h"
#include "map/VmfValues.h"

namespace xv::map
{

namespace
{

constexpr std::string_view NODRAW = "TOOLS/TOOLSNODRAW";

//-----------------------------------------------------------------------------
// Purpose: Whether a material is one of the tool textures (nodraw, skybox, clip...), which are
//          left as they are.
//-----------------------------------------------------------------------------
bool IsToolMaterial( std::string_view szMaterial )
{
	constexpr std::string_view TOOLS = "tools/";
	if ( szMaterial.size() < TOOLS.size() )
		return false;
	for ( std::size_t i = 0; i < TOOLS.size(); i++ )
	{
		if ( std::tolower( static_cast<unsigned char>( szMaterial[ i ] ) ) != TOOLS[ i ] )
			return false;
	}
	return true;
}

// A grid of cells over the map: each solid (touched by a world solid), outside (reached from the
// grid's edge) or neither.
class CVoidGrid
{
public:
	CVoidGrid( const Box_t &bounds, double flCell )
		: m_flCell( flCell )
	{
		for ( int i = 0; i < 3; i++ )
		{
			// Two empty cells round the map, so the fill can go all the way round it.
			m_Origin[ i ] = std::floor( bounds.m_Mins[ i ] / flCell ) * flCell - 2.0 * flCell;
			m_nSize[ i ] = static_cast<int>( std::ceil( ( bounds.m_Maxs[ i ] - m_Origin[ i ] ) / flCell ) ) + 2;
		}
		m_Cells.assign( static_cast<std::size_t>( m_nSize[ 0 ] ) * m_nSize[ 1 ] * m_nSize[ 2 ], CELL_EMPTY );
	}

	// Every cell the solid's shape reaches into: those not wholly in front of one of its planes.
	void AddSolid( const SolidGeometry_t &geometry )
	{
		int nMin[ 3 ], nMax[ 3 ];
		for ( int i = 0; i < 3; i++ )
		{
			nMin[ i ] = std::clamp( static_cast<int>( std::floor( ( geometry.m_Bounds.m_Mins[ i ] - m_Origin[ i ] ) / m_flCell ) ), 0, m_nSize[ i ] - 1 );
			nMax[ i ] = std::clamp( static_cast<int>( std::floor( ( geometry.m_Bounds.m_Maxs[ i ] - m_Origin[ i ] ) / m_flCell ) ), 0, m_nSize[ i ] - 1 );
		}
		for ( int z = nMin[ 2 ]; z <= nMax[ 2 ]; z++ )
			for ( int y = nMin[ 1 ]; y <= nMax[ 1 ]; y++ )
				for ( int x = nMin[ 0 ]; x <= nMax[ 0 ]; x++ )
				{
					const Point_t mins{ m_Origin[ 0 ] + x * m_flCell, m_Origin[ 1 ] + y * m_flCell, m_Origin[ 2 ] + z * m_flCell };
					bool bApart = false;
					for ( const FaceGeometry_t &face : geometry.m_Faces )
					{
						// The cell's corner furthest behind the plane: still in front, so is all of it.
						Point_t nearest;
						for ( int i = 0; i < 3; i++ )
							nearest[ i ] = mins[ i ] + ( face.m_Plane.m_Normal[ i ] < 0.0 ? m_flCell : 0.0 );
						if ( face.m_Plane.Distance( nearest ) > 0.01 )
						{
							bApart = true;
							break;
						}
					}
					if ( !bApart )
						m_Cells[ Index( x, y, z ) ] = CELL_SOLID;
				}
	}

	// The fill from the grid's edge through every cell no solid touches.
	void FillOutside()
	{
		std::vector<std::uint32_t> pending;
		const auto visit = [ & ]( int x, int y, int z ) {
			const std::size_t n = Index( x, y, z );
			if ( m_Cells[ n ] == CELL_EMPTY )
			{
				m_Cells[ n ] = CELL_OUTSIDE;
				pending.push_back( static_cast<std::uint32_t>( n ) );
			}
		};
		for ( int z = 0; z < m_nSize[ 2 ]; z++ )
			for ( int y = 0; y < m_nSize[ 1 ]; y++ )
				for ( int x = 0; x < m_nSize[ 0 ]; x++ )
					if ( x == 0 || y == 0 || z == 0 || x == m_nSize[ 0 ] - 1 || y == m_nSize[ 1 ] - 1 || z == m_nSize[ 2 ] - 1 )
						visit( x, y, z );
		while ( !pending.empty() )
		{
			const std::size_t n = pending.back();
			pending.pop_back();
			const int x = static_cast<int>( n % m_nSize[ 0 ] );
			const int y = static_cast<int>( n / m_nSize[ 0 ] % m_nSize[ 1 ] );
			const int z = static_cast<int>( n / m_nSize[ 0 ] / m_nSize[ 1 ] );
			if ( x > 0 )
				visit( x - 1, y, z );
			if ( x + 1 < m_nSize[ 0 ] )
				visit( x + 1, y, z );
			if ( y > 0 )
				visit( x, y - 1, z );
			if ( y + 1 < m_nSize[ 1 ] )
				visit( x, y + 1, z );
			if ( z > 0 )
				visit( x, y, z - 1 );
			if ( z + 1 < m_nSize[ 2 ] )
				visit( x, y, z + 1 );
		}
	}

	// Whether a point's cell is touched by a solid (beyond the grid: no).
	[[nodiscard]] bool IsSolid( const Point_t &p ) const
	{
		int n[ 3 ];
		for ( int i = 0; i < 3; i++ )
		{
			n[ i ] = static_cast<int>( std::floor( ( p[ i ] - m_Origin[ i ] ) / m_flCell ) );
			if ( n[ i ] < 0 || n[ i ] >= m_nSize[ i ] )
				return false;
		}
		return m_Cells[ Index( n[ 0 ], n[ 1 ], n[ 2 ] ) ] == CELL_SOLID;
	}

	// Beyond the grid counts as outside.
	[[nodiscard]] bool IsOutside( const Point_t &p ) const
	{
		int n[ 3 ];
		for ( int i = 0; i < 3; i++ )
		{
			n[ i ] = static_cast<int>( std::floor( ( p[ i ] - m_Origin[ i ] ) / m_flCell ) );
			if ( n[ i ] < 0 || n[ i ] >= m_nSize[ i ] )
				return true;
		}
		return m_Cells[ Index( n[ 0 ], n[ 1 ], n[ 2 ] ) ] == CELL_OUTSIDE;
	}

private:
	enum Cell_t : std::uint8_t
	{
		CELL_EMPTY,
		CELL_SOLID,
		CELL_OUTSIDE,
	};

	[[nodiscard]] std::size_t Index( int x, int y, int z ) const { return ( static_cast<std::size_t>( z ) * m_nSize[ 1 ] + y ) * m_nSize[ 0 ] + x; }

	double m_flCell;
	Point_t m_Origin{};
	int m_nSize[ 3 ]{};
	std::vector<Cell_t> m_Cells;
};

// The sealing solids by coarse buckets, to tell whether a point is inside one.
class CSolidBuckets
{
public:
	CSolidBuckets( const std::vector<const SolidGeometry_t *> &solids, const Box_t &bounds )
		: m_Solids( solids )
	{
		for ( int i = 0; i < 3; i++ )
		{
			m_Origin[ i ] = bounds.m_Mins[ i ] - BUCKET;
			m_nSize[ i ] = static_cast<int>( std::ceil( ( bounds.m_Maxs[ i ] - m_Origin[ i ] ) / BUCKET ) ) + 1;
		}
		m_Buckets.resize( static_cast<std::size_t>( m_nSize[ 0 ] ) * m_nSize[ 1 ] * m_nSize[ 2 ] );
		for ( std::uint32_t nSolid = 0; nSolid < solids.size(); nSolid++ )
		{
			const Box_t &box = solids[ nSolid ]->m_Bounds;
			int nMin[ 3 ], nMax[ 3 ];
			for ( int i = 0; i < 3; i++ )
			{
				nMin[ i ] = Cell( box.m_Mins[ i ], i );
				nMax[ i ] = Cell( box.m_Maxs[ i ], i );
			}
			for ( int z = nMin[ 2 ]; z <= nMax[ 2 ]; z++ )
				for ( int y = nMin[ 1 ]; y <= nMax[ 1 ]; y++ )
					for ( int x = nMin[ 0 ]; x <= nMax[ 0 ]; x++ )
						m_Buckets[ ( static_cast<std::size_t>( z ) * m_nSize[ 1 ] + y ) * m_nSize[ 0 ] + x ].push_back( nSolid );
		}
	}

	// Whether p is inside a solid other than nExcept, by its place in the list (on its surface
	// doesn't count).
	[[nodiscard]] bool IsInside( const Point_t &p, std::uint32_t nExcept ) const
	{
		const int x = Cell( p[ 0 ], 0 ), y = Cell( p[ 1 ], 1 ), z = Cell( p[ 2 ], 2 );
		for ( const std::uint32_t nSolid : m_Buckets[ ( static_cast<std::size_t>( z ) * m_nSize[ 1 ] + y ) * m_nSize[ 0 ] + x ] )
		{
			if ( nSolid == nExcept )
				continue;
			const SolidGeometry_t &geometry = *m_Solids[ nSolid ];
			bool bInside = true;
			for ( int i = 0; i < 3 && bInside; i++ )
				bInside = p[ i ] > geometry.m_Bounds.m_Mins[ i ] && p[ i ] < geometry.m_Bounds.m_Maxs[ i ];
			for ( std::size_t f = 0; f < geometry.m_Faces.size() && bInside; f++ )
				bInside = geometry.m_Faces[ f ].m_Plane.Distance( p ) < -0.01;
			if ( bInside )
				return true;
		}
		return false;
	}

private:
	static constexpr double BUCKET = 256.0;

	[[nodiscard]] int Cell( double flValue, int nAxis ) const { return std::clamp( static_cast<int>( std::floor( ( flValue - m_Origin[ nAxis ] ) / BUCKET ) ), 0, m_nSize[ nAxis ] - 1 ); }

	const std::vector<const SolidGeometry_t *> &m_Solids;
	Point_t m_Origin{};
	int m_nSize[ 3 ]{};
	std::vector<std::vector<std::uint32_t>> m_Buckets;
};

//-----------------------------------------------------------------------------
// Purpose: Whether a solid seals: a world one with a shape (Hammer++'s off-plane vertices or
//          not: vbsp builds it from its planes).
//-----------------------------------------------------------------------------
bool Seals( const Solid_t &solid, const SolidGeometry_t &geometry )
{
	constexpr std::uint32_t SHAPELESS = SOLID_PROBLEM_OPEN | SOLID_PROBLEM_EMPTY | SOLID_PROBLEM_TOO_FEW_SIDES;
	if ( solid.m_bDeleted || solid.m_nEntity >= 0 || ( geometry.m_nProblems & SHAPELESS ) != 0 || geometry.m_Faces.size() < 4 )
		return false;
	// A brush of tool textures only (clip, trigger, hint, skip...) doesn't seal, unless one of
	// them is one that does.
	for ( const Side_t &side : solid.m_Sides )
	{
		if ( !IsToolMaterial( side.m_szMaterial ) )
			return true;
		std::string szName{ side.m_szMaterial.substr( 6 ) };
		std::ranges::transform( szName, szName.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
		if ( szName == "toolsnodraw" || szName == "toolsskybox" || szName == "toolsskybox2d" || szName == "toolsblack" || szName == "toolsinvisible" )
			return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Whether an entity's origin tells the inside from the void, as vbsp's leak check
//          does: every point entity but a func_instance's (its contents are placed, and looked
//          at, one by one) and info_overlay (projected onto faces wherever its origin is).
//-----------------------------------------------------------------------------
bool MarksInside( const Entity_t &entity )
{
	if ( entity.m_bDeleted || !entity.m_Solids.empty() || !entity.HasKey( "origin" ) )
		return false;
	const std::string_view szClass = entity.GetClassname();
	return !szClass.starts_with( "func_instance" ) && szClass != "info_overlay";
}

// What seals the map and what marks its inside, its instances' placed in it.
struct Sealing_t
{
	std::vector<const SolidGeometry_t *> m_Solids;
	std::vector<std::int64_t> m_Own;           // by solid: its index in the map, -1 for an instance's
	std::vector<std::unique_ptr<SolidGeometry_t>> m_Placed; // the instances' solids where they sit
	std::vector<std::pair<std::string, Point_t>> m_Marks; // entities' classes and origins
};

//-----------------------------------------------------------------------------
// Purpose: A solid's shape moved by an instance's placement (turns and moves only).
//-----------------------------------------------------------------------------
std::unique_ptr<SolidGeometry_t> Placed( const SolidGeometry_t &geometry, const Transform_t &transform )
{
	auto pPlaced = std::make_unique<SolidGeometry_t>( geometry );
	pPlaced->m_Bounds = Box_t{};
	for ( FaceGeometry_t &face : pPlaced->m_Faces )
	{
		for ( Point_t &p : face.m_Points )
		{
			p = transform.Apply( p );
			for ( int i = 0; i < 3; i++ )
			{
				pPlaced->m_Bounds.m_Mins[ i ] = std::min( pPlaced->m_Bounds.m_Mins[ i ], p[ i ] );
				pPlaced->m_Bounds.m_Maxs[ i ] = std::max( pPlaced->m_Bounds.m_Maxs[ i ], p[ i ] );
			}
		}
		face.m_Plane.m_Normal = Normalized( transform.Rotate( face.m_Plane.m_Normal ) );
		if ( !face.m_Points.empty() )
			face.m_Plane.m_flDistance = Dot( face.m_Plane.m_Normal, face.m_Points.front() );
	}
	return pPlaced;
}

//-----------------------------------------------------------------------------
// Purpose: A scene's sealing solids and inside marks into sealing, placed by transform; its own
//          instances' after them.
//-----------------------------------------------------------------------------
void Gather( const CScene &scene, const Transform_t &transform, bool bOwn, int nDepth, Sealing_t &sealing )
{
	const CMap &map = scene.m_Map;
	for ( std::uint32_t n = 0; n < map.m_Solids.size(); n++ )
	{
		if ( !Seals( map.m_Solids[ n ], scene.m_Solids[ n ] ) )
			continue;
		if ( bOwn )
		{
			sealing.m_Solids.push_back( &scene.m_Solids[ n ] );
			sealing.m_Own.push_back( n );
		}
		else
		{
			sealing.m_Placed.push_back( Placed( scene.m_Solids[ n ], transform ) );
			sealing.m_Solids.push_back( sealing.m_Placed.back().get() );
			sealing.m_Own.push_back( -1 );
		}
	}
	for ( const Entity_t &entity : map.m_Entities )
	{
		if ( !MarksInside( entity ) )
			continue;
		if ( const std::optional<Point_t> origin = ParsePoint( entity.GetValue( "origin" ) ) )
			sealing.m_Marks.emplace_back( std::string{ entity.GetClassname() }, transform.Apply( *origin ) );
	}
	if ( nDepth >= 8 )
		return;
	for ( const PlacedInstance_t &instance : scene.m_Instances )
	{
		if ( instance.m_pScene )
			Gather( *instance.m_pScene, transform * instance.m_ToParent, false, nDepth + 1, sealing );
	}
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The grid filled from outside; then each face followed out along its normal, from its
//          middle and from near each corner: facing the void when every one of them gets to an
//          outside cell before any other solid.
//-----------------------------------------------------------------------------
VoidFaces_t FindVoidFaces( const CScene &scene, double flCellSize )
{
	VoidFaces_t found;
	const CMap &map = scene.m_Map;
	Sealing_t sealing;
	Gather( scene, Transform_t{}, true, 0, sealing );
	Box_t bounds;
	for ( const SolidGeometry_t *pSolid : sealing.m_Solids )
	{
		for ( int i = 0; i < 3; i++ )
		{
			bounds.m_Mins[ i ] = std::min( bounds.m_Mins[ i ], pSolid->m_Bounds.m_Mins[ i ] );
			bounds.m_Maxs[ i ] = std::max( bounds.m_Maxs[ i ], pSolid->m_Bounds.m_Maxs[ i ] );
		}
	}
	if ( !( bounds.m_Mins[ 0 ] <= bounds.m_Maxs[ 0 ] ) )
		return found;
	if ( flCellSize <= 0.0 )
	{
		double flExtent = 0.0;
		for ( int i = 0; i < 3; i++ )
			flExtent = std::max( flExtent, bounds.m_Maxs[ i ] - bounds.m_Mins[ i ] );
		flCellSize = std::max( 16.0, std::ceil( flExtent / 256.0 / 8.0 ) * 8.0 );
	}
	found.m_flCellSize = flCellSize;
	CVoidGrid grid( bounds, flCellSize );
	for ( const SolidGeometry_t *pSolid : sealing.m_Solids )
		grid.AddSolid( *pSolid );
	grid.FillOutside();

	// An entity the outside reaches: the map leaks, and its inside would be taken for the void.
	// With no entity inside at all, there is nothing to tell the inside by either.
	bool bMarked = false;
	for ( const auto &[ szClass, origin ] : sealing.m_Marks )
	{
		if ( grid.IsOutside( origin ) )
		{
			found.m_bLeaks = true;
			found.m_szLeakEntity = szClass;
			found.m_LeakAt = origin;
			return found;
		}
		bMarked = true;
	}
	if ( !bMarked )
	{
		found.m_bNothingInside = true;
		return found;
	}

	// From each sample on a face, out along its normal a unit at a time: void when an outside cell
	// is reached before any other solid (a brush right against the face, or a thin one beyond it
	// in the cells the face's own solid touches).
	const CSolidBuckets buckets( sealing.m_Solids, bounds );
	const double flFurthest = flCellSize * 2.0;
	const auto seesVoid = [ & ]( const Point_t &start, const Point_t &normal, std::uint32_t nSealer ) {
		for ( double t = 0.5; t <= flFurthest; t += 1.0 )
		{
			const Point_t p = Add( start, Scale( normal, t ) );
			if ( buckets.IsInside( p, nSealer ) )
				return false;
			if ( grid.IsOutside( p ) )
				return true;
			if ( !grid.IsSolid( p ) )
				return false; // a cell the outside never reached: inside the map
		}
		return false;
	};
	for ( std::uint32_t nSealer = 0; nSealer < sealing.m_Solids.size(); nSealer++ )
	{
		if ( sealing.m_Own[ nSealer ] < 0 )
			continue;
		const auto n = static_cast<std::uint32_t>( sealing.m_Own[ nSealer ] );
		const Solid_t &solid = map.m_Solids[ n ];
		for ( const FaceGeometry_t &face : sealing.m_Solids[ nSealer ]->m_Faces )
		{
			const Side_t &side = solid.m_Sides[ face.m_nSide ];
			if ( side.m_pDisp || IsToolMaterial( side.m_szMaterial ) || face.m_Points.size() < 3 )
				continue;
			Point_t middle{};
			for ( const Point_t &p : face.m_Points )
				middle = Add( middle, p );
			middle = Scale( middle, 1.0 / static_cast<double>( face.m_Points.size() ) );
			bool bVoid = seesVoid( middle, face.m_Plane.m_Normal, nSealer );
			for ( std::size_t i = 0; bVoid && i < face.m_Points.size(); i++ )
				bVoid = seesVoid( Lerp( face.m_Points[ i ], middle, 0.1 ), face.m_Plane.m_Normal, nSealer );
			if ( bVoid )
				found.m_Faces.push_back( { n, face.m_nSide } );
		}
	}
	return found;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapEdit_t NodrawFaces( const CScene &scene, const std::vector<FaceRef_t> &faces )
{
	ReplaceTextures_t how;
	how.m_nHow = ReplaceHow_t::REPLACE_EXACT;
	how.m_szReplace = std::string{ NODRAW };
	return ReplaceTextures( scene, faces, how, {}, nullptr );
}

} // namespace xv::map
