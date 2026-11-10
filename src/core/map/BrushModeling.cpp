//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See BrushModeling.h.
//===========================================================================//
#include "BrushModeling.h"
#include "map/BrushSelect.h"
#include "map/Clip.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <ranges>
#include <set>
namespace xv::map
{
namespace
{
constexpr double EPSILON = 0.001;
//-----------------------------------------------------------------------------
// Purpose: Keep generated faces on their own planes with the source face's material.
//-----------------------------------------------------------------------------
Side_t NewSide( const CMap &map, const Plane_t &plane, const Side_t &like )
{
	return SideOnPlane( map, plane, like );
}
//-----------------------------------------------------------------------------
// Purpose: Construct a convex hull while preserving a matching exterior face's alignment.
//-----------------------------------------------------------------------------
std::optional<Solid_t> Hull( const CMap &map, const Solid_t &source, const std::vector<Point_t> &points, const Side_t *pLike = nullptr )
{
	const auto planes = ConvexHull( points );
	if ( !planes || source.m_Sides.empty() )
		return std::nullopt;
	Solid_t solid = source;
	solid.m_Sides.clear();
	solid.m_bChanged = true;
	const auto original = BuildSolid( source );
	for ( const auto &plane : *planes )
	{
		auto side = NewSide( map, plane, pLike ? *pLike : source.m_Sides.front() );
		for ( const auto &face : original.m_Faces )
			if ( !pLike && Dot( face.m_Plane.m_Normal, plane.m_Normal ) > 1 - 1e-8 && std::abs( face.m_Plane.m_flDistance - plane.m_flDistance ) < 0.01 )
			{
				side = source.m_Sides[ face.m_nSide ];
				side.m_PlusVertices.clear();
				break;
			}
		solid.m_Sides.push_back( std::move( side ) );
	}
	if ( !BuildSolid( solid ).IsValid() )
		return std::nullopt;
	return solid;
}
//-----------------------------------------------------------------------------
// Purpose: Carry a translated cap's texels with it instead of resetting its world alignment.
//-----------------------------------------------------------------------------
void ContinueCap( Solid_t &solid, const Side_t &like, const Plane_t &original, double flDistance )
{
	const auto delta = Scale( original.m_Normal, flDistance - original.m_flDistance );
	for ( const auto &face : BuildSolid( solid ).m_Faces )
		if ( Dot( face.m_Plane.m_Normal, original.m_Normal ) > 0.999 && std::abs( face.m_Plane.m_flDistance - flDistance ) < 0.01 )
		{
			auto &side = solid.m_Sides[ face.m_nSide ];
			side.m_UAxis = like.m_UAxis;
			side.m_VAxis = like.m_VAxis;
			side.m_UAxis.m_flShift -= Dot( side.m_UAxis.m_Axis, delta ) / ( side.m_UAxis.m_flScale != 0 ? side.m_UAxis.m_flScale : 1 );
			side.m_VAxis.m_flShift -= Dot( side.m_VAxis.m_Axis, delta ) / ( side.m_VAxis.m_flScale != 0 ? side.m_VAxis.m_flScale : 1 );
			side.m_szMaterial = like.m_szMaterial;
			side.m_nMaterial = like.m_nMaterial;
			side.m_nLightmapScale = like.m_nLightmapScale;
			side.m_Span = {};
		}
}
//-----------------------------------------------------------------------------
// Purpose: Fit a projected face's texture coordinates to its transformed tangent basis.
//-----------------------------------------------------------------------------
void ProjectTexture( Side_t &side, const CBrushMesh &before, const CBrushMesh &after, const BrushFace_t &face )
{
	if ( face.m_Corners.size() < 3 )
		return;
	const auto a = face.m_Corners[ 0 ];
	std::uint32_t b = a, c = a;
	for ( std::size_t i = 1; i < face.m_Corners.size() && b == a; ++i )
		if ( Length( Subtract( after.m_Vertices[ face.m_Corners[ i ] ], after.m_Vertices[ a ] ) ) > 1e-6 )
			b = face.m_Corners[ i ];
	for ( auto n : face.m_Corners )
		if ( Length( Cross( Subtract( after.m_Vertices[ b ], after.m_Vertices[ a ] ), Subtract( after.m_Vertices[ n ], after.m_Vertices[ a ] ) ) ) > 1e-6 )
		{
			c = n;
			break;
		}
	if ( b == a || c == a )
		return;
	const auto u = Subtract( after.m_Vertices[ b ], after.m_Vertices[ a ] ), v = Subtract( after.m_Vertices[ c ], after.m_Vertices[ a ] );
	const double uu = Dot( u, u ), uv = Dot( u, v ), vv = Dot( v, v ), det = uu * vv - uv * uv;
	if ( std::abs( det ) < 1e-10 )
		return;
	for ( auto *pAxis : { &side.m_UAxis, &side.m_VAxis } )
	{
		const double flScale = pAxis->m_flScale != 0 ? pAxis->m_flScale : 1;
		const double flA = Dot( pAxis->m_Axis, before.m_Vertices[ a ] ) / flScale + pAxis->m_flShift, flU = Dot( pAxis->m_Axis, Subtract( before.m_Vertices[ b ], before.m_Vertices[ a ] ) ), flV = Dot( pAxis->m_Axis, Subtract( before.m_Vertices[ c ], before.m_Vertices[ a ] ) );
		pAxis->m_Axis = Add( Scale( u, ( flU * vv - flV * uv ) / det ), Scale( v, ( flV * uu - flU * uv ) / det ) );
		pAxis->m_flShift = flA - Dot( pAxis->m_Axis, after.m_Vertices[ a ] ) / flScale;
	}
}
//-----------------------------------------------------------------------------
// Purpose: Build a prism from a face with the cap's texture continued onto its translated copy.
//-----------------------------------------------------------------------------
std::optional<Solid_t> Prism( const CMap &map, const Solid_t &source, const FaceGeometry_t &face, double flDistance )
{
	std::vector<std::vector<Point_t>> polygons{ face.m_Points };
	std::vector<Point_t> top;
	for ( auto point : face.m_Points )
		top.push_back( Add( point, Scale( face.m_Plane.m_Normal, flDistance ) ) );
	polygons.push_back( top );
	for ( std::size_t i = 0; i < top.size(); ++i )
	{
		const auto j = ( i + 1 ) % top.size();
		polygons.push_back( { face.m_Points[ i ], face.m_Points[ j ], top[ j ], top[ i ] } );
	}
	const auto &like = source.m_Sides[ face.m_nSide ];
	FaceDefaults_t defaults;
	defaults.m_szMaterial = like.m_szMaterial;
	defaults.m_flTextureScale = like.m_UAxis.m_flScale;
	defaults.m_nLightmapScale = like.m_nLightmapScale;
	auto solid = SolidFromFaces( map, polygons, defaults );
	const auto shape = BuildSolid( solid );
	if ( !shape.IsValid() )
		return std::nullopt;
	for ( const auto &cap : shape.m_Faces )
		if ( Dot( cap.m_Plane.m_Normal, face.m_Plane.m_Normal ) > 0.999 )
			ContinueCap( solid, like, face.m_Plane, cap.m_Plane.m_flDistance );
	return solid;
}
//-----------------------------------------------------------------------------
// Purpose: Clip precise face polygons while retaining subdivisions and untouched displacements.
//-----------------------------------------------------------------------------
std::optional<Solid_t> ClipBack( const CMap &map, const Solid_t &source, const Plane_t &plane, const Side_t &like, std::string &szProblem )
{
	const auto shape = BuildSolid( source );
	bool bFront = false, bBack = false;
	for ( const auto &face : shape.m_Faces )
		for ( auto point : face.m_Points )
		{
			bFront |= plane.Distance( point ) > EPSILON;
			bBack |= plane.Distance( point ) < -EPSILON;
		}
	if ( !bFront )
		return source;
	if ( !bBack )
		return std::nullopt;
	CBrushMesh mesh;
	std::vector<Point_t> cap;
	auto index = [ & ]( Point_t point ) {for(std::uint32_t i=0;i<mesh.m_Vertices.size();++i)if(Length(Subtract(point,mesh.m_Vertices[i]))<1e-5)return i;mesh.m_Vertices.push_back(point);return static_cast<std::uint32_t>(mesh.m_Vertices.size()-1); };
	for ( const auto &face : shape.m_Faces )
	{
		const auto polygon = ClipPolygon( face.m_Points, plane, 1e-6 );
		if ( source.m_Sides[ face.m_nSide ].m_pDisp && std::ranges::any_of( face.m_Points, [ & ]( Point_t point ) { return plane.Distance( point ) > EPSILON; } ) )
		{
			szProblem = "The cut would change a displacement's corners.";
			return std::nullopt;
		}
		if ( polygon.size() < 3 )
			continue;
		BrushFace_t output;
		output.m_nSide = face.m_nSide;
		for ( auto point : polygon )
		{
			output.m_Corners.push_back( index( point ) );
			if ( std::abs( plane.Distance( point ) ) < 0.01 && std::ranges::none_of( cap, [ & ]( Point_t other ) { return Length( Subtract( point, other ) ) < 1e-5; } ) )
				cap.push_back( point );
		}
		mesh.m_Faces.push_back( std::move( output ) );
	}
	if ( cap.size() < 3 )
		return std::nullopt;
	Point_t centre{};
	for ( auto point : cap )
		centre = Add( centre, point );
	centre = Scale( centre, 1.0 / cap.size() );
	const auto u = Normalized( Cross( plane.m_Normal, std::abs( plane.m_Normal[ 2 ] ) < 0.9 ? Point_t{ 0, 0, 1 } : Point_t{ 0, 1, 0 } ) );
	const auto v = Cross( plane.m_Normal, u );
	std::ranges::sort( cap, [ & ]( Point_t a, Point_t b ) {a=Subtract(a,centre);b=Subtract(b,centre);return std::atan2(Dot(a,v),Dot(a,u))>std::atan2(Dot(b,v),Dot(b,u)); } );
	Solid_t textured = source;
	textured.m_Sides.push_back( like );
	BrushFace_t cut;
	cut.m_nSide = source.m_Sides.size();
	cut.m_bNew = true;
	for ( auto point : cap )
		cut.m_Corners.push_back( index( point ) );
	mesh.m_Faces.push_back( std::move( cut ) );
	auto result = ToSolids( map, textured, mesh, BrushConcave_t::BRUSH_CONCAVE_SPLIT );
	if ( result.m_Solids.size() != 1 || !BuildSolid( result.m_Solids.front() ).IsValid() )
	{
		szProblem = result.m_szProblem.empty() ? "The cut would leave an invalid brush." : result.m_szProblem;
		return std::nullopt;
	}
	return std::move( result.m_Solids.front() );
}
//-----------------------------------------------------------------------------
// Purpose: Give only the shared portions of partition faces nodraw, leaving exposed portions visible.
//-----------------------------------------------------------------------------
void MarkInternalFaces( const CMap &map, std::vector<Solid_t> &pieces )
{
	std::vector<SolidGeometry_t> shapes;
	for ( const auto &piece : pieces )
		shapes.push_back( BuildSolid( piece ) );
	for ( std::size_t i = 0; i < pieces.size(); ++i )
	{
		std::vector<Side_t> sides;
		for ( const auto &face : shapes[ i ].m_Faces )
		{
			std::vector<const FaceGeometry_t *> neighbours;
			for ( std::size_t j = 0; j < shapes.size(); ++j )
				if ( i != j )
					for ( const auto &other : shapes[ j ].m_Faces )
						if ( Dot( face.m_Plane.m_Normal, other.m_Plane.m_Normal ) < -1 + 1e-8 && std::abs( face.m_Plane.m_flDistance + other.m_Plane.m_flDistance ) < 0.01 )
							neighbours.push_back( &other );
			std::vector<std::vector<Point_t>> fragments{ face.m_Points };
			for ( const auto *pOther : neighbours )
				for ( std::size_t k = 0; k < pOther->m_Points.size(); ++k )
				{
					const auto a = pOther->m_Points[ k ], b = pOther->m_Points[ ( k + 1 ) % pOther->m_Points.size() ];
					const auto normal = Normalized( Cross( pOther->m_Plane.m_Normal, Subtract( b, a ) ) );
					const Plane_t plane{ normal, Dot( normal, a ) };
					std::vector<std::vector<Point_t>> next;
					for ( const auto &polygon : fragments )
					{
						const bool bFront = std::ranges::any_of( polygon, [ & ]( Point_t p ) { return plane.Distance( p ) > EPSILON; } ), bBack = std::ranges::any_of( polygon, [ & ]( Point_t p ) { return plane.Distance( p ) < -EPSILON; } );
						if ( bFront && bBack )
						{
							next.push_back( ClipPolygon( polygon, plane, 1e-6 ) );
							next.push_back( ClipPolygon( polygon, { Scale( normal, -1 ), -plane.m_flDistance }, 1e-6 ) );
						}
						else
							next.push_back( polygon );
					}
					fragments = std::move( next );
				}
			bool bFirst = true;
			for ( const auto &polygon : fragments )
			{
				if ( polygon.size() < 3 )
					continue;
				Point_t centre{};
				for ( auto p : polygon )
					centre = Add( centre, p );
				centre = Scale( centre, 1.0 / polygon.size() );
				const bool bInternal = std::ranges::any_of( neighbours, [ & ]( const FaceGeometry_t *pOther ) {for(std::size_t k=0;k<pOther->m_Points.size();++k){const auto a=pOther->m_Points[k],b=pOther->m_Points[(k+1)%pOther->m_Points.size()];if(Dot(Cross(pOther->m_Plane.m_Normal,Subtract(b,a)),Subtract(centre,a))>0.01)return false;}return true; } );
				auto side = pieces[ i ].m_Sides[ face.m_nSide ];
				side.m_PlusVertices = polygon;
				side.m_Span = {};
				if ( !bFirst )
					side.m_nId = 0;
				bFirst = false;
				if ( bInternal )
				{
					side.m_szMaterial = map.Store( "TOOLS/TOOLSNODRAW" );
					side.m_nMaterial = NEW_MATERIAL;
					side.m_pDisp.reset();
				}
				sides.push_back( std::move( side ) );
			}
		}
		pieces[ i ].m_Sides = std::move( sides );
		pieces[ i ].m_bChanged = true;
	}
}
//-----------------------------------------------------------------------------
// Purpose: Fit the least-squares plane through a selection using a symmetric covariance matrix.
//-----------------------------------------------------------------------------
std::optional<Plane_t> BestPlane( const std::vector<Point_t> &points )
{
	if ( points.size() < 3 )
		return std::nullopt;
	Point_t centre{};
	for ( auto point : points )
		centre = Add( centre, point );
	centre = Scale( centre, 1.0 / points.size() );
	double a[ 3 ][ 3 ]{}, vectors[ 3 ][ 3 ]{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
	for ( auto point : points )
	{
		point = Subtract( point, centre );
		for ( int i = 0; i < 3; ++i )
			for ( int j = 0; j < 3; ++j )
				a[ i ][ j ] += point[ i ] * point[ j ];
	}
	for ( int step = 0; step < 32; ++step )
	{
		int p = 0, q = 1;
		for ( int i = 0; i < 3; ++i )
			for ( int j = i + 1; j < 3; ++j )
				if ( std::abs( a[ i ][ j ] ) > std::abs( a[ p ][ q ] ) )
				{
					p = i;
					q = j;
				}
		if ( std::abs( a[ p ][ q ] ) < 1e-10 )
			break;
		const double angle = 0.5 * std::atan2( 2 * a[ p ][ q ], a[ q ][ q ] - a[ p ][ p ] ), c = std::cos( angle ), s = std::sin( angle );
		for ( int k = 0; k < 3; ++k )
		{
			const double x = a[ k ][ p ], y = a[ k ][ q ];
			a[ k ][ p ] = c * x - s * y;
			a[ k ][ q ] = s * x + c * y;
			const double u = vectors[ k ][ p ], v = vectors[ k ][ q ];
			vectors[ k ][ p ] = c * u - s * v;
			vectors[ k ][ q ] = s * u + c * v;
		}
		for ( int k = 0; k < 3; ++k )
		{
			const double x = a[ p ][ k ], y = a[ q ][ k ];
			a[ p ][ k ] = c * x - s * y;
			a[ q ][ k ] = s * x + c * y;
		}
	}
	int n = 0;
	for ( int i = 1; i < 3; ++i )
		if ( a[ i ][ i ] < a[ n ][ n ] )
			n = i;
	int nRank = 0;
	for ( int i = 0; i < 3; ++i )
		nRank += a[ i ][ i ] > 1e-7;
	if ( nRank < 2 )
		return std::nullopt;
	Point_t normal{ vectors[ 0 ][ n ], vectors[ 1 ][ n ], vectors[ 2 ][ n ] };
	normal = Normalized( normal );
	for ( double value : normal )
		if ( std::abs( value ) > 1e-6 )
		{
			if ( value < 0 )
				normal = Scale( normal, -1 );
			break;
		}
	return Plane_t{ normal, Dot( normal, centre ) };
}
//-----------------------------------------------------------------------------
// Purpose: Round a vertex cap with chord planes over a sampled spherical patch.
//-----------------------------------------------------------------------------
std::vector<Plane_t> VertexBevelPlanes( const CBrushMesh &mesh, std::uint32_t nVertex, double flWidth, int nSegments )
{
	if ( nVertex >= mesh.m_Vertices.size() )
		return {};
	const auto at = mesh.m_Vertices[ nVertex ];
	std::vector<Point_t> cutPoints;
	Point_t inward{};
	for ( auto [ a, b ] : mesh.GetEdges() )
		if ( a == nVertex || b == nVertex )
		{
			const auto direction = Normalized( Subtract( mesh.m_Vertices[ a == nVertex ? b : a ], at ) );
			cutPoints.push_back( Add( at, Scale( direction, flWidth ) ) );
			inward = Add( inward, direction );
		}
	if ( cutPoints.size() < 3 )
		return {};
	const auto fit = BestPlane( cutPoints );
	if ( !fit )
		return {};
	auto normal = fit->m_Normal;
	if ( Dot( normal, Subtract( at, cutPoints.front() ) ) < 0 )
		normal = Scale( normal, -1 );
	if ( nSegments == 1 )
		return { { normal, Dot( normal, cutPoints.front() ) } };
	const auto centre = Add( at, Scale( inward, flWidth ) );
	Point_t average{};
	for ( auto p : cutPoints )
		average = Add( average, p );
	average = Scale( average, 1.0 / cutPoints.size() );
	const auto u = Normalized( Subtract( cutPoints.front(), average ) ), v = Cross( normal, u );
	std::ranges::sort( cutPoints, [ & ]( Point_t a, Point_t b ) { return std::atan2( Dot( Subtract( a, average ), v ), Dot( Subtract( a, average ), u ) ) < std::atan2( Dot( Subtract( b, average ), v ), Dot( Subtract( b, average ), u ) ); } );
	std::vector<std::array<Point_t, 3>> triangles;
	if ( cutPoints.size() == 3 )
		triangles.push_back( { cutPoints[ 0 ], cutPoints[ 1 ], cutPoints[ 2 ] } );
	else
	{
		double radius = 0;
		for ( auto p : cutPoints )
			radius += Length( Subtract( p, centre ) );
		radius /= cutPoints.size();
		const auto mid = Add( centre, Scale( normal, radius ) );
		for ( std::size_t i = 0; i < cutPoints.size(); ++i )
			triangles.push_back( { cutPoints[ i ], cutPoints[ ( i + 1 ) % cutPoints.size() ], mid } );
	}
	std::vector<Plane_t> planes;
	for ( const auto &triangle : triangles )
	{
		auto point = [ & ]( int i, int j ) {const double a=double(i)/nSegments,b=double(j)/nSegments,c=1-a-b;const auto direction=Add(Add(Scale(Subtract(triangle[0],centre),a),Scale(Subtract(triangle[1],centre),b)),Scale(Subtract(triangle[2],centre),c));const double radius=a*Length(Subtract(triangle[0],centre))+b*Length(Subtract(triangle[1],centre))+c*Length(Subtract(triangle[2],centre));return Add(centre,Scale(Normalized(direction),radius)); };
		auto add = [ & ]( Point_t a, Point_t b, Point_t c ) {auto n=Cross(Subtract(b,a),Subtract(c,a));if(Length(n)<1e-8)return;n=Normalized(n);if(Dot(n,normal)<0)n=Scale(n,-1);planes.push_back({n,Dot(n,a)}); };
		for ( int i = 0; i < nSegments; ++i )
			for ( int j = 0; j < nSegments - i; ++j )
			{
				add( point( i, j ), point( i + 1, j ), point( i, j + 1 ) );
				if ( i + j + 1 < nSegments )
					add( point( i + 1, j ), point( i + 1, j + 1 ), point( i, j + 1 ) );
			}
	}
	return planes;
}
//-----------------------------------------------------------------------------
// Purpose: Subtract a convex cutter, retaining disjoint convex pieces outside its planes.
//-----------------------------------------------------------------------------
std::vector<Solid_t> SubtractSolid( const CMap &map, const Solid_t &source, const Solid_t &cutter, const Side_t &like, std::string &szProblem )
{
	std::vector<Solid_t> result;
	std::optional<Solid_t> inside = source;
	for ( const auto &face : BuildSolid( cutter ).m_Faces )
	{
		if ( !inside )
			break;
		auto front = ClipBack( map, *inside, { Scale( face.m_Plane.m_Normal, -1 ), -face.m_Plane.m_flDistance }, like, szProblem );
		if ( !szProblem.empty() )
			return {};
		auto back = ClipBack( map, *inside, face.m_Plane, like, szProblem );
		if ( !szProblem.empty() )
			return {};
		if ( front )
			result.push_back( std::move( *front ) );
		inside = std::move( back );
	}
	return result;
}
//-----------------------------------------------------------------------------
// Purpose: Count overlap volume so bridge construction cannot penetrate its source brushes.
//-----------------------------------------------------------------------------
double Volume( const SolidGeometry_t &shape )
{
	double flVolume = 0;
	const auto centre = shape.m_Bounds.GetCenter();
	for ( const auto &face : shape.m_Faces )
		for ( std::size_t i = 1; i + 1 < face.m_Points.size(); ++i )
			flVolume += std::abs( Dot( Subtract( face.m_Points[ 0 ], centre ), Cross( Subtract( face.m_Points[ i ], centre ), Subtract( face.m_Points[ i + 1 ], centre ) ) ) ) / 6;
	return flVolume;
}
//-----------------------------------------------------------------------------
// Purpose: Package replacements and additions with distinct identities and stable source ownership.
//-----------------------------------------------------------------------------
class CResultBuilder
{
public:
	//-----------------------------------------------------------------------------
	// Purpose: Allocate identities consistently across all products of one operation.
	//-----------------------------------------------------------------------------
	CResultBuilder( const CScene &scene, BrushModelResult_t &result )
		: m_Scene( scene ),
		  m_Result( result ),
		  m_nId( scene.m_nNextObjectId ),
		  m_nSide( scene.m_nNextSideId ),
		  m_nAdded( scene.m_Map.m_Solids.size() )
	{}
	//-----------------------------------------------------------------------------
	// Purpose: Replace a source while retaining its owner, identity and grouping.
	//-----------------------------------------------------------------------------
	void Replace( std::uint32_t nSolid, std::vector<Solid_t> pieces, bool bSelectCaps = false, Point_t normal = {}, std::optional<double> flCapPlane = std::nullopt )
	{
		if ( pieces.empty() )
		{
			auto removed = m_Scene.m_Map.m_Solids[ nSolid ];
			removed.m_bDeleted = true;
			removed.m_bChanged = true;
			m_Result.m_Edit.m_Solids.emplace_back( nSolid, std::move( removed ) );
			return;
		}
		const auto &source = m_Scene.m_Map.m_Solids[ nSolid ];
		int nGroup = source.m_Editor.m_nGroup;
		if ( pieces.size() > 1 && !nGroup )
		{
			Group_t group;
			group.m_nId = m_nId++;
			group.m_Editor = source.m_Editor;
			m_Result.m_Edit.m_Groups.push_back( group );
			nGroup = group.m_nId;
		}
		std::set<int> used;
		for ( std::size_t i = 0; i < pieces.size(); ++i )
		{
			auto piece = std::move( pieces[ i ] );
			if ( i )
			{
				piece = AsNew( std::move( piece ), m_nId, m_nSide );
			}
			else
			{
				piece.m_nId = source.m_nId;
				piece.m_Span = source.m_Span;
				piece.m_nReadEntity = source.m_nReadEntity;
			}
			piece.m_nEntity = source.m_nEntity;
			piece.m_bChanged = true;
			piece.m_Editor = source.m_Editor;
			if ( nGroup )
			{
				piece.m_Editor.m_nGroup = nGroup;
				piece.m_Editor.m_bChanged = true;
			}
			for ( auto &side : piece.m_Sides )
			{
				if ( !side.m_nId || used.contains( side.m_nId ) )
					side.m_nId = m_nSide++;
				used.insert( side.m_nId );
			}
			if ( bSelectCaps )
				for ( const auto &face : BuildSolid( piece ).m_Faces )
					if ( Dot( face.m_Plane.m_Normal, normal ) > 0.999 && ( !flCapPlane || std::abs( face.m_Plane.m_flDistance - *flCapPlane ) < 0.01 ) )
						m_Result.m_SelectedSides.push_back( piece.m_Sides[ face.m_nSide ].m_nId );
			m_Result.m_Edit.m_Solids.emplace_back( i ? m_nAdded++ : nSolid, std::move( piece ) );
		}
	}
	//-----------------------------------------------------------------------------
	// Purpose: Append a brush with fresh identities and its source ownership.
	//-----------------------------------------------------------------------------
	void Add( Solid_t solid, const Solid_t &source, bool bSelectCaps = false, Point_t normal = {} )
	{
		solid = AsNew( std::move( solid ), m_nId, m_nSide );
		solid.m_nEntity = source.m_nEntity;
		solid.m_Editor = source.m_Editor;
		solid.m_bChanged = true;
		if ( bSelectCaps )
			for ( const auto &face : BuildSolid( solid ).m_Faces )
				if ( Dot( face.m_Plane.m_Normal, normal ) > 0.999 )
					m_Result.m_SelectedSides.push_back( solid.m_Sides[ face.m_nSide ].m_nId );
		m_Result.m_Edit.m_Solids.emplace_back( m_nAdded++, std::move( solid ) );
	}

private:
	const CScene &m_Scene;
	BrushModelResult_t &m_Result;
	int m_nId, m_nSide;
	std::uint32_t m_nAdded;
};
//-----------------------------------------------------------------------------
// Purpose: Recognize a point in a coplanar contour without assuming it is convex.
//-----------------------------------------------------------------------------
bool InsideContour( const std::vector<Point_t> &polygon, Point_t point, Point_t normal )
{
	int nAxis = 0;
	for ( int i = 1; i < 3; ++i )
		if ( std::abs( normal[ i ] ) > std::abs( normal[ nAxis ] ) )
			nAxis = i;
	const int x = ( nAxis + 1 ) % 3, y = ( nAxis + 2 ) % 3;
	bool bInside = false;
	for ( std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++ )
	{
		const auto a = polygon[ i ], b = polygon[ j ];
		const auto edge = Subtract( b, a ), delta = Subtract( point, a );
		if ( Length( Cross( edge, delta ) ) < 1e-5 * std::max( 1.0, Length( edge ) ) && Dot( delta, edge ) >= -EPSILON && Dot( Subtract( point, b ), edge ) <= EPSILON )
			return true;
		if ( ( a[ y ] > point[ y ] ) != ( b[ y ] > point[ y ] ) && point[ x ] < ( b[ x ] - a[ x ] ) * ( point[ y ] - a[ y ] ) / ( b[ y ] - a[ y ] ) + a[ x ] )
			bInside = !bInside;
	}
	return bInside;
}
//-----------------------------------------------------------------------------
// Purpose: Recover region contours, splitting T junctions before cancelling shared edges.
//-----------------------------------------------------------------------------
std::vector<std::vector<Point_t>> RegionContours( const std::vector<std::vector<Point_t>> &faces )
{
	std::vector<Point_t> vertices;
	auto index = [ & ]( Point_t p ) {for(std::uint32_t i=0;i<vertices.size();++i)if(Length(Subtract(vertices[i],p))<EPSILON)return i;vertices.push_back(p);return static_cast<std::uint32_t>(vertices.size()-1); };
	for ( const auto &face : faces )
		for ( auto p : face )
			index( p );
	std::vector<std::pair<std::uint32_t, std::uint32_t>> boundary;
	for ( const auto &face : faces )
		for ( std::size_t i = 0; i < face.size(); ++i )
		{
			const auto a = face[ i ], b = face[ ( i + 1 ) % face.size() ], direction = Subtract( b, a );
			const double flLength = Dot( direction, direction );
			if ( flLength < 1e-8 )
				return {};
			std::vector<std::pair<double, std::uint32_t>> along;
			for ( std::uint32_t n = 0; n < vertices.size(); ++n )
			{
				const double t = Dot( Subtract( vertices[ n ], a ), direction ) / flLength;
				if ( t >= -1e-6 && t <= 1 + 1e-6 && Length( Subtract( vertices[ n ], Add( a, Scale( direction, t ) ) ) ) < EPSILON )
					along.emplace_back( t, n );
			}
			std::ranges::sort( along );
			for ( std::size_t k = 1; k < along.size(); ++k )
			{
				const auto edge = std::pair{ along[ k - 1 ].second, along[ k ].second };
				auto opposite = std::ranges::find( boundary, std::pair{ edge.second, edge.first } );
				if ( opposite != boundary.end() )
					boundary.erase( opposite );
				else if ( std::ranges::find( boundary, edge ) != boundary.end() )
					return {};
				else
					boundary.push_back( edge );
			}
		}
	std::vector<std::vector<Point_t>> loops;
	while ( !boundary.empty() )
	{
		std::vector<Point_t> loop;
		const auto first = boundary.front().first;
		auto at = first;
		while ( true )
		{
			auto found = std::ranges::find_if( boundary, [ & ]( auto edge ) { return edge.first == at; } );
			if ( found == boundary.end() )
				return {};
			loop.push_back( vertices[ at ] );
			at = found->second;
			boundary.erase( found );
			if ( at == first )
				break;
			if ( loop.size() > vertices.size() )
				return {};
		}
		for ( bool bChanged = true; bChanged && loop.size() > 3; )
		{
			bChanged = false;
			for ( std::size_t i = 0; i < loop.size(); ++i )
			{
				const auto a = loop[ ( i + loop.size() - 1 ) % loop.size() ], b = loop[ i ], c = loop[ ( i + 1 ) % loop.size() ];
				if ( Length( Cross( Subtract( b, a ), Subtract( c, b ) ) ) < EPSILON * Length( Subtract( c, a ) ) )
				{
					loop.erase( loop.begin() + i );
					bChanged = true;
					break;
				}
			}
		}
		if ( loop.size() < 3 )
			return {};
		loops.push_back( std::move( loop ) );
	}
	return loops;
}
//-----------------------------------------------------------------------------
// Purpose: Offset a contour at mitred corners, refusing reversals and self-intersections.
//-----------------------------------------------------------------------------
std::optional<std::vector<Point_t>> OffsetContour( const std::vector<Point_t> &outline, const Plane_t &face, double flWidth )
{
	std::vector<Plane_t> edges;
	for ( std::size_t i = 0; i < outline.size(); ++i )
	{
		const auto a = outline[ i ], b = outline[ ( i + 1 ) % outline.size() ];
		const auto n = Normalized( Cross( face.m_Normal, Subtract( b, a ) ) );
		edges.push_back( { n, Dot( n, a ) - flWidth } );
	}
	std::vector<Point_t> inner;
	for ( std::size_t i = 0; i < edges.size(); ++i )
	{
		const auto &a = edges[ ( i + edges.size() - 1 ) % edges.size() ], &b = edges[ i ];
		const auto cross = Cross( a.m_Normal, b.m_Normal );
		const double d = Dot( cross, face.m_Normal );
		if ( std::abs( d ) < 1e-8 )
			return std::nullopt;
		inner.push_back( Scale( Add( Add( Scale( Cross( b.m_Normal, face.m_Normal ), a.m_flDistance ), Scale( Cross( face.m_Normal, a.m_Normal ), b.m_flDistance ) ), Scale( cross, face.m_flDistance ) ), 1 / d ) );
	}
	for ( std::size_t i = 0; i < inner.size(); ++i )
	{
		const auto a = inner[ i ], b = inner[ ( i + 1 ) % inner.size() ];
		if ( Dot( Subtract( b, a ), Subtract( outline[ ( i + 1 ) % outline.size() ], outline[ i ] ) ) <= EPSILON )
			return std::nullopt;
		for ( std::size_t j = i + 2; j < inner.size(); ++j )
		{
			if ( ( j + 1 ) % inner.size() == i )
				continue;
			const auto c = inner[ j ], d = inner[ ( j + 1 ) % inner.size() ];
			const double ac = Dot( Cross( Subtract( b, a ), Subtract( c, a ) ), face.m_Normal ), ad = Dot( Cross( Subtract( b, a ), Subtract( d, a ) ), face.m_Normal ), ca = Dot( Cross( Subtract( d, c ), Subtract( a, c ) ), face.m_Normal ), cb = Dot( Cross( Subtract( d, c ), Subtract( b, c ) ), face.m_Normal );
			if ( ac * ad < -1e-8 && ca * cb < -1e-8 )
				return std::nullopt;
		}
	}
	return inner;
}
//-----------------------------------------------------------------------------
// Purpose: Make a possibly concave planar mask into convex prisms using the shared partitioner.
//-----------------------------------------------------------------------------
std::vector<Solid_t> MaskPrisms( const CMap &map, const Solid_t &source, std::vector<Point_t> polygon, Point_t normal, double flDepth )
{
	Point_t winding{};
	for ( std::size_t i = 0; i < polygon.size(); ++i )
		winding = Add( winding, Cross( polygon[ i ], polygon[ ( i + 1 ) % polygon.size() ] ) );
	if ( Length( winding ) < 1e-6 )
		return {};
	if ( Dot( winding, normal ) > 0 )
		std::ranges::reverse( polygon );
	CBrushMesh mesh;
	mesh.m_Vertices = polygon;
	for ( auto p : polygon )
		mesh.m_Vertices.push_back( Subtract( p, Scale( normal, flDepth ) ) );
	const auto n = static_cast<std::uint32_t>( polygon.size() );
	BrushFace_t top, bottom;
	top.m_nSide = 0;
	bottom.m_nSide = 0;
	top.m_bNew = bottom.m_bNew = true;
	for ( std::uint32_t i = 0; i < n; ++i )
	{
		top.m_Corners.push_back( i );
		bottom.m_Corners.push_back( n + n - 1 - i );
	}
	mesh.m_Faces.push_back( top );
	mesh.m_Faces.push_back( bottom );
	for ( std::uint32_t i = 0; i < n; ++i )
	{
		const auto j = ( i + 1 ) % n;
		BrushFace_t side;
		side.m_nSide = 0;
		side.m_bNew = true;
		side.m_Corners = { i + n, j + n, j, i };
		mesh.m_Faces.push_back( side );
	}
	return ToSolids( map, source, mesh, BrushConcave_t::BRUSH_CONCAVE_SPLIT ).m_Solids;
}
//-----------------------------------------------------------------------------
// Purpose: Partition an individual face or a coplanar region into a centre and a legal frame.
//-----------------------------------------------------------------------------
BrushModelResult_t InsetSelection( const CScene &scene, std::span<const BrushPart_t> parts, const BrushModelParams_t &params )
{
	BrushModelResult_t result;
	auto refuse = [ & ]( const char *szReason ) {BrushModelResult_t failure;failure.m_szProblem=szReason;return failure; };
	if ( parts.empty() || params.m_flAmount <= 0 )
		return refuse( "Select faces and a positive inset thickness." );
	std::vector<std::vector<BrushPart_t>> regions;
	if ( params.m_bRegion )
		regions.emplace_back( parts.begin(), parts.end() );
	else
		for ( const auto &part : parts )
			regions.push_back( { part } );
	CResultBuilder builder( scene, result );
	std::map<std::uint32_t, std::vector<Solid_t>> outputs;
	for ( const auto &region : regions )
	{
		std::vector<std::vector<Point_t>> faces;
		std::set<std::uint32_t> solids;
		std::optional<Plane_t> plane;
		double flDepth = 0;
		int nOwner = -2;
		for ( const auto &part : region )
		{
			if ( part.m_nKind != BrushPartKind_t::BRUSH_PART_FACE )
				return refuse( "Inset works on faces only." );
			const auto nSolid = scene.GetIndex( part.m_nObject );
			const auto &source = scene.m_Map.m_Solids[ nSolid ];
			const auto &face = scene.m_Solids[ nSolid ].m_Faces[ part.m_nIndex ];
			if ( !plane )
			{
				plane = face.m_Plane;
				nOwner = source.m_nEntity;
			}
			else if ( Dot( plane->m_Normal, face.m_Plane.m_Normal ) < 1 - 1e-8 || std::abs( plane->m_flDistance - face.m_Plane.m_flDistance ) > 0.01 )
				return refuse( "A region inset needs coplanar faces pointing the same way." );
			if ( source.m_nEntity != nOwner )
				return refuse( "A region inset cannot join different entity owners." );
			faces.push_back( face.m_Points );
			solids.insert( nSolid );
			for ( const auto &surface : scene.m_Solids[ nSolid ].m_Faces )
				for ( auto point : surface.m_Points )
					flDepth = std::max( flDepth, -plane->Distance( point ) + 16 );
		}
		const auto contours = RegionContours( faces );
		if ( contours.empty() )
			return refuse( "The selected faces do not form usable region boundaries." );
		std::vector<std::vector<Point_t>> inner;
		std::vector<Solid_t> masks;
		const auto &like = scene.m_Map.m_Solids[ *solids.begin() ];
		for ( const auto &contour : contours )
		{
			const auto offset = OffsetContour( contour, *plane, params.m_flAmount );
			if ( !offset )
				return refuse( "That inset thickness collapses or crosses its boundary." );
			inner.push_back( *offset );
			for ( std::size_t i = 0; i < contour.size(); ++i )
			{
				const auto j = ( i + 1 ) % contour.size();
				std::vector<std::vector<Point_t>> strips;
				if ( !params.m_bSquareCorners )
					strips.push_back( { contour[ i ], contour[ j ], ( *offset )[ j ], ( *offset )[ i ] } );
				else
				{
					const auto direction = Normalized( Subtract( contour[ j ], contour[ i ] ) );
					if ( Length( Subtract( contour[ j ], contour[ i ] ) ) <= params.m_flAmount * 2 + EPSILON )
						return refuse( "Square corners need an edge longer than twice the inset width." );
					const auto outward = Normalized( Cross( plane->m_Normal, direction ) );
					const auto a = Add( contour[ i ], Scale( direction, params.m_flAmount ) ), b = Subtract( contour[ j ], Scale( direction, params.m_flAmount ) );
					strips.push_back( { a, b, Subtract( b, Scale( outward, params.m_flAmount ) ), Subtract( a, Scale( outward, params.m_flAmount ) ) } );
					const auto previous = Normalized( Subtract( contour[ i ], contour[ ( i + contour.size() - 1 ) % contour.size() ] ) );
					strips.push_back( { contour[ i ], a, ( *offset )[ i ], Subtract( contour[ i ], Scale( previous, params.m_flAmount ) ) } );
				}
				for ( auto strip : strips )
				{
					auto pieces = MaskPrisms( scene.m_Map, like, std::move( strip ), plane->m_Normal, flDepth );
					if ( pieces.empty() )
						return refuse( "The inset frame would be degenerate." );
					masks.insert( masks.end(), pieces.begin(), pieces.end() );
				}
			}
		}
		for ( auto nSolid : solids )
		{
			const auto &source = scene.m_Map.m_Solids[ nSolid ];
			auto remaining = outputs.contains( nSolid ) ? outputs[ nSolid ] : std::vector<Solid_t>{ source };
			std::vector<Solid_t> frame;
			for ( const auto &mask : masks )
			{
				std::vector<Solid_t> next;
				for ( const auto &piece : remaining )
				{
					std::optional<Solid_t> overlap = piece;
					for ( const auto &face : BuildSolid( mask ).m_Faces )
					{
						if ( !overlap )
							break;
						overlap = ClipBack( scene.m_Map, *overlap, face.m_Plane, source.m_Sides.front(), result.m_szProblem );
					}
					if ( !result.m_szProblem.empty() )
						return result;
					if ( !overlap || Volume( BuildSolid( *overlap ) ) < 1e-6 )
					{
						next.push_back( piece );
						continue;
					}
					frame.push_back( std::move( *overlap ) );
					auto outside = SubtractSolid( scene.m_Map, piece, mask, source.m_Sides.front(), result.m_szProblem );
					if ( !result.m_szProblem.empty() )
						return result;
					next.insert( next.end(), outside.begin(), outside.end() );
				}
				remaining = std::move( next );
			}
			if ( remaining.empty() )
				return refuse( "That inset thickness leaves no centre." );
			if ( std::abs( params.m_flDepth ) > 1e-6 )
			{
				std::vector<Solid_t> depthPieces;
				for ( auto &piece : remaining )
				{
					const auto shape = BuildSolid( piece );
					auto cap = std::ranges::find_if( shape.m_Faces, [ & ]( const FaceGeometry_t &face ) { return Dot( face.m_Plane.m_Normal, plane->m_Normal ) > 1 - 1e-8 && std::abs( face.m_Plane.m_flDistance - plane->m_flDistance ) < 0.01; } );
					if ( cap == shape.m_Faces.end() )
					{
						depthPieces.push_back( piece );
						continue;
					}
					auto prism = Prism( scene.m_Map, piece, *cap, params.m_flDepth );
					if ( !prism )
						return refuse( "The inset depth would leave invalid geometry." );
					if ( params.m_flDepth > 0 )
					{
						depthPieces.push_back( piece );
						depthPieces.push_back( *prism );
					}
					else
					{
						auto cut = SubtractSolid( scene.m_Map, piece, *prism, piece.m_Sides[ cap->m_nSide ], result.m_szProblem );
						if ( !result.m_szProblem.empty() || cut.empty() )
							return refuse( "That inset depth removes its entire centre." );
						for ( auto &output : cut )
							ContinueCap( output, piece.m_Sides[ cap->m_nSide ], cap->m_Plane, cap->m_Plane.m_flDistance + params.m_flDepth );
						depthPieces.insert( depthPieces.end(), cut.begin(), cut.end() );
					}
				}
				remaining = std::move( depthPieces );
			}
			remaining.insert( remaining.end(), frame.begin(), frame.end() );
			outputs[ nSolid ] = std::move( remaining );
		}
	}
	std::vector<Solid_t> all;
	std::vector<std::pair<std::uint32_t, std::size_t>> spans;
	for ( auto &[ n, pieces ] : outputs )
	{
		spans.emplace_back( n, pieces.size() );
		all.insert( all.end(), pieces.begin(), pieces.end() );
	}
	MarkInternalFaces( scene.m_Map, all );
	std::size_t at = 0;
	for ( auto [ n, count ] : spans )
	{
		std::vector<Solid_t> pieces( all.begin() + at, all.begin() + at + count );
		at += count;
		builder.Replace( n, std::move( pieces ) );
	}
	if ( params.m_bRegion && outputs.size() > 1 )
	{
		int nGroup = scene.m_nNextObjectId;
		for ( const auto &group : result.m_Edit.m_Groups )
			nGroup = std::max( nGroup, group.m_nId + 1 );
		for ( const auto &[ n, solid ] : result.m_Edit.m_Solids )
			nGroup = std::max( nGroup, solid.m_nId + 1 );
		Group_t group;
		group.m_nId = nGroup;
		result.m_Edit.m_Groups.push_back( group );
		for ( auto &[ n, solid ] : result.m_Edit.m_Solids )
		{
			solid.m_Editor.m_nGroup = nGroup;
			solid.m_Editor.m_bChanged = true;
		}
	}
	for ( const auto &[ nSolid, piece ] : result.m_Edit.m_Solids )
		if ( !piece.m_bDeleted )
			for ( const auto &face : BuildSolid( piece ).m_Faces )
				for ( const auto &region : regions )
				{
					const auto &original = scene.m_Solids[ scene.GetIndex( region.front().m_nObject ) ].m_Faces[ region.front().m_nIndex ];
					if ( Dot( face.m_Plane.m_Normal, original.m_Plane.m_Normal ) < 0.999 || std::abs( face.m_Plane.m_flDistance - original.m_Plane.m_flDistance - params.m_flDepth ) > 0.01 )
						continue;
					Point_t centre{};
					for ( auto p : face.m_Points )
						centre = Add( centre, p );
					centre = Scale( centre, 1.0 / face.m_Points.size() );
					bool bInner = false;
					std::vector<std::vector<Point_t>> outlines;
					for ( const auto &part : region )
						outlines.push_back( scene.m_Solids[ scene.GetIndex( part.m_nObject ) ].m_Faces[ part.m_nIndex ].m_Points );
					for ( const auto &contour : RegionContours( outlines ) )
					{
						const auto inner = OffsetContour( contour, original.m_Plane, params.m_flAmount );
						if ( inner && InsideContour( *inner, Subtract( centre, Scale( original.m_Plane.m_Normal, params.m_flDepth ) ), original.m_Plane.m_Normal ) )
							bInner = !bInner;
					}
					if ( bInner )
						result.m_SelectedSides.push_back( piece.m_Sides[ face.m_nSide ].m_nId );
				}
	return result;
}
//-----------------------------------------------------------------------------
// Purpose: Merge two coplanar face boundaries, refusing holes or disconnected outlines.
//-----------------------------------------------------------------------------
bool JoinFaces( CBrushMesh &mesh, std::size_t a, std::size_t b )
{
	const auto pa = mesh.FacePlane( a ), pb = mesh.FacePlane( b );
	if ( !pa || !pb || Dot( pa->m_Normal, pb->m_Normal ) < 1 - 1e-8 || std::abs( pa->m_flDistance - pb->m_flDistance ) > 0.01 )
		return false;
	std::vector<std::pair<std::uint32_t, std::uint32_t>> boundary;
	for ( auto n : { a, b } )
	{
		const auto &face = mesh.m_Faces[ n ];
		for ( std::size_t i = 0; i < face.m_Corners.size(); ++i )
		{
			const auto u = face.m_Corners[ i ], v = face.m_Corners[ ( i + 1 ) % face.m_Corners.size() ];
			auto found = std::ranges::find( boundary, std::pair{ v, u } );
			if ( found != boundary.end() )
				boundary.erase( found );
			else
				boundary.emplace_back( u, v );
		}
	}
	if ( boundary.size() >= mesh.m_Faces[ a ].m_Corners.size() + mesh.m_Faces[ b ].m_Corners.size() )
		return false;
	std::vector<std::uint32_t> corners;
	const auto first = boundary.front().first;
	auto at = first;
	while ( !boundary.empty() )
	{
		auto found = std::ranges::find_if( boundary, [ & ]( auto edge ) { return edge.first == at; } );
		if ( found == boundary.end() )
			return false;
		corners.push_back( at );
		at = found->second;
		boundary.erase( found );
		if ( at == first )
			break;
	}
	if ( !boundary.empty() || corners.size() < 3 )
		return false;
	mesh.m_Faces[ a ].m_Corners = std::move( corners );
	mesh.m_Faces.erase( mesh.m_Faces.begin() + b );
	return true;
}
//-----------------------------------------------------------------------------
// Purpose: Find an outward clipping plane for a rounded edge without changing the original faces.
//-----------------------------------------------------------------------------
std::vector<Plane_t> EdgeBevelPlanes( const CBrushMesh &mesh, std::pair<std::uint32_t, std::uint32_t> edge, double flWidth, int nSegments )
{
	std::vector<Point_t> normals;
	for ( std::size_t f = 0; f < mesh.m_Faces.size(); ++f )
	{
		const auto &corners = mesh.m_Faces[ f ].m_Corners;
		for ( std::size_t i = 0; i < corners.size(); ++i )
			if ( std::minmax( corners[ i ], corners[ ( i + 1 ) % corners.size() ] ) == std::minmax( edge.first, edge.second ) )
			{
				normals.push_back( mesh.FacePlane( f )->m_Normal );
				break;
			}
	}
	if ( normals.size() != 2 )
		return {};
	const double flAngle = std::acos( std::clamp( Dot( normals[ 0 ], normals[ 1 ] ), -1.0, 1.0 ) );
	if ( flAngle < 1e-5 || flAngle > std::numbers::pi - 1e-5 )
		return {};
	const double flRadius = flWidth / std::tan( flAngle / 2 );
	const auto centre = Subtract( mesh.m_Vertices[ edge.first ], Scale( Normalized( Add( normals[ 0 ], normals[ 1 ] ) ), flRadius / std::cos( flAngle / 2 ) ) );
	std::vector<Plane_t> planes;
	for ( int i = 0; i < nSegments; ++i )
	{
		const double flT = ( i + 0.5 ) / nSegments;
		const auto normal = Normalized( Add( Scale( normals[ 0 ], std::sin( ( 1 - flT ) * flAngle ) ), Scale( normals[ 1 ], std::sin( flT * flAngle ) ) ) );
		planes.push_back( { normal, Dot( normal, centre ) + flRadius * std::cos( flAngle / ( 2 * nSegments ) ) } );
	}
	return planes;
}
} // namespace

//-----------------------------------------------------------------------------
// Purpose: Walk opposite edges of quad faces with a consistent fractional direction.
//-----------------------------------------------------------------------------
std::vector<BrushRingEdge_t> FindBrushEdgeRing( const CBrushMesh &mesh, std::uint32_t nEdge )
{
	const auto edges = mesh.GetEdges();
	if ( nEdge >= edges.size() )
		return {};
	std::map<std::uint32_t, bool> found{ { nEdge, false } };
	std::vector<std::uint32_t> pending{ nEdge };
	for ( std::size_t i = 0; i < pending.size(); ++i )
	{
		const auto edge = edges[ pending[ i ] ];
		for ( const auto &face : mesh.m_Faces )
		{
			if ( face.m_Corners.size() != 4 )
				continue;
			for ( int j = 0; j < 4; ++j )
			{
				const auto u = face.m_Corners[ j ], v = face.m_Corners[ ( j + 1 ) % 4 ];
				if ( std::minmax( u, v ) != std::minmax( edge.first, edge.second ) )
					continue;
				const auto x = face.m_Corners[ ( j + 3 ) % 4 ], w = face.m_Corners[ ( j + 2 ) % 4 ];
				const auto target = std::pair{ std::min( x, w ), std::max( x, w ) };
				const auto at = std::ranges::find( edges, target );
				if ( at == edges.end() )
					return {};
				const auto n = static_cast<std::uint32_t>( at - edges.begin() );
				const bool bReverse = found[ pending[ i ] ] ^ ( u > v ) ^ ( x > w );
				if ( auto existing = found.find( n ); existing != found.end() )
				{
					if ( existing->second != bReverse )
						return {};
				}
				else
				{
					found[ n ] = bReverse;
					pending.push_back( n );
				}
			}
		}
	}
	std::vector<BrushRingEdge_t> result;
	for ( auto [ n, bReverse ] : found )
		result.push_back( { n, bReverse } );
	return result;
}
//-----------------------------------------------------------------------------
// Purpose: Insert shared cut vertices and split every quad along an actual topological edge ring.
//-----------------------------------------------------------------------------
bool LoopCutBrush( CBrushMesh &input, std::uint32_t nEdge, int nCount, double flSlide )
{
	CBrushMesh mesh = input;
	if ( nCount < 1 || nCount > 64 || !std::isfinite( flSlide ) || std::abs( flSlide ) >= 1 )
		return false;
	const auto ring = FindBrushEdgeRing( mesh, nEdge );
	if ( ring.size() < 2 )
		return false;
	const auto edges = mesh.GetEdges();
	std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<std::uint32_t>> cuts;
	for ( const auto &entry : ring )
	{
		const auto edge = edges[ entry.m_nEdge ];
		auto &indices = cuts[ edge ];
		for ( int k = 1; k <= nCount; ++k )
		{
			double t = ( k + flSlide ) / ( nCount + 1 );
			if ( entry.m_bReverse )
				t = 1 - t;
			indices.push_back( mesh.m_Vertices.size() );
			mesh.m_Vertices.push_back( Lerp( mesh.m_Vertices[ edge.first ], mesh.m_Vertices[ edge.second ], t ) );
		}
		if ( entry.m_bReverse )
			std::ranges::reverse( indices );
	}
	std::vector<BrushFace_t> result;
	bool bSplit = false;
	for ( const auto &face : mesh.m_Faces )
	{
		int nStart = -1;
		for ( std::size_t i = 0; i < face.m_Corners.size(); ++i )
		{
			auto u = face.m_Corners[ i ], v = face.m_Corners[ ( i + 1 ) % face.m_Corners.size() ];
			if ( cuts.contains( { std::min( u, v ), std::max( u, v ) } ) )
			{
				nStart = i;
				break;
			}
		}
		if ( nStart < 0 )
		{
			result.push_back( face );
			continue;
		}
		const auto u = face.m_Corners[ nStart ], v = face.m_Corners[ ( nStart + 1 ) % face.m_Corners.size() ];
		auto first = cuts[ { std::min( u, v ), std::max( u, v ) } ];
		if ( u > v )
			std::ranges::reverse( first );
		if ( face.m_Corners.size() == 4 )
		{
			const auto w = face.m_Corners[ ( nStart + 2 ) % 4 ], x = face.m_Corners[ ( nStart + 3 ) % 4 ];
			auto second = cuts.find( { std::min( w, x ), std::max( w, x ) } );
			if ( second != cuts.end() )
			{
				auto opposite = second->second;
				if ( x > w )
					std::ranges::reverse( opposite );
				first.insert( first.begin(), u );
				first.push_back( v );
				opposite.insert( opposite.begin(), x );
				opposite.push_back( w );
				for ( int k = 0; k <= nCount; ++k )
				{
					auto strip = face;
					strip.m_Corners = { first[ k ], first[ k + 1 ], opposite[ k + 1 ], opposite[ k ] };
					result.push_back( std::move( strip ) );
				}
				bSplit = true;
				continue;
			}
		}
		auto expanded = face;
		expanded.m_Corners.clear();
		for ( std::size_t i = 0; i < face.m_Corners.size(); ++i )
		{
			auto a = face.m_Corners[ i ], b = face.m_Corners[ ( i + 1 ) % face.m_Corners.size() ];
			expanded.m_Corners.push_back( a );
			if ( auto found = cuts.find( { std::min( a, b ), std::max( a, b ) } ); found != cuts.end() )
			{
				auto inserted = found->second;
				if ( a > b )
					std::ranges::reverse( inserted );
				expanded.m_Corners.insert( expanded.m_Corners.end(), inserted.begin(), inserted.end() );
			}
		}
		result.push_back( std::move( expanded ) );
	}
	mesh.m_Faces = std::move( result );
	if ( !bSplit || !mesh.Validate().empty() )
		return false;
	input = std::move( mesh );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Build a repeatable modeling edit from the original scene without changing it.
//-----------------------------------------------------------------------------
BrushModelResult_t ModelBrushes( const CScene &scene, const std::vector<std::uint32_t> &objects, std::span<const BrushPart_t> parts, BrushModelOperation_t nOperation, const BrushModelParams_t &params )
{
	BrushModelResult_t result;
	auto refuse = [ & ]( std::string szProblem ) {BrushModelResult_t failure;failure.m_szProblem=std::move(szProblem);return failure; };
	if ( !std::isfinite( params.m_flAmount ) || !std::isfinite( params.m_flDepth ) || !std::isfinite( params.m_flAngle ) || !std::isfinite( params.m_flSlide ) || params.m_nCount < 1 || params.m_nCount > 256 )
		return refuse( "Operation values must be finite, with a count between 1 and 256." );
	for ( auto point : { params.m_Offset, params.m_Pivot, params.m_Plane.m_Normal } )
		for ( double value : point )
			if ( !std::isfinite( value ) )
				return refuse( "Operation coordinates must be finite." );
	auto selected = ObjectsOf( scene, objects ).m_Solids;
	std::map<std::uint32_t, std::vector<BrushPart_t>> bySolid;
	CMeshCache meshes( scene );
	for ( const auto &part : parts )
	{
		if ( part.m_nObject >= scene.GetObjectCount() || !scene.IsSolidObject( part.m_nObject ) || scene.IsDeleted( part.m_nObject ) )
			return refuse( "A selected brush part no longer exists." );
		const auto nSolid = scene.GetIndex( part.m_nObject );
		if ( CornersOf( *meshes.Get( part.m_nObject ), part.m_nKind, part.m_nIndex ).empty() )
			return refuse( "A selected brush part no longer exists." );
		bySolid[ nSolid ].push_back( part );
	}
	if ( selected.empty() )
		for ( auto &[ nSolid, solidParts ] : bySolid )
			selected.push_back( nSolid );
	if ( selected.empty() )
		return refuse( "Select brushes or brush parts first." );
	Box_t bounds;
	for ( auto n : selected )
	{
		if ( !scene.m_Solids[ n ].IsValid() )
			return refuse( "Repair invalid brushes before using this operation." );
		bounds.Add( scene.m_Solids[ n ].m_Bounds );
	}
	const auto pivot = params.m_bUsePivot ? params.m_Pivot : bounds.GetCenter();
	Point_t axis{};
	if ( params.m_nAxis >= 0 && params.m_nAxis < 3 )
		axis[ params.m_nAxis ] = 1;
	else
		axis = Normalized( params.m_Plane.m_Normal );
	if ( Length( axis ) < 0.5 )
		return refuse( "Choose a usable axis or plane." );
	if ( nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_INSET )
		return InsetSelection( scene, parts, params );
	CResultBuilder builder( scene, result );
	const auto nConcave = params.m_nConcave;
	auto convert = [ & ]( std::uint32_t nSolid, const CBrushMesh &mesh, const Solid_t &textured ) -> bool {auto output=ToSolids(scene.m_Map,textured,mesh,nConcave);if(output.m_Solids.empty()){result.m_szProblem=output.m_szProblem;return false;}if(output.m_Solids.size()>1)MarkInternalFaces(scene.m_Map,output.m_Solids);builder.Replace(nSolid,std::move(output.m_Solids));return true; };
	if ( nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_MIRROR )
	{
		result.m_Edit = TransformObjects( scene, objects, Transform_t::ScalingAlong( pivot, axis, -1 ), true );
		return result;
	}
	if ( nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_ARRAY || nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_SPIN )
	{
		auto working = std::shared_ptr<const CScene>( &scene, []( const CScene * ) {} );
		for ( int k = 1; k <= params.m_nCount; ++k )
		{
			auto transform = Transform_t::Translation( Scale( params.m_Offset, k ) );
			if ( nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_SPIN )
				transform = transform * Transform_t::RotationAbout( pivot, axis, params.m_flAngle * k );
			std::vector<std::uint32_t> added;
			auto edit = CopyObjects( *working, objects, transform, true, &added );
			if ( edit.IsEmpty() )
				return refuse( "There is nothing to copy." );
			working = ApplyEdit( *working, edit );
			result.m_Edit.m_Solids.insert( result.m_Edit.m_Solids.end(), edit.m_Solids.begin(), edit.m_Solids.end() );
			result.m_Edit.m_Entities.insert( result.m_Edit.m_Entities.end(), edit.m_Entities.begin(), edit.m_Entities.end() );
			result.m_Edit.m_Groups.insert( result.m_Edit.m_Groups.end(), edit.m_Groups.begin(), edit.m_Groups.end() );
		}
		return result;
	}
	if ( nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_KNIFE )
	{
		auto plane = params.m_Plane;
		const double flLength = Length( plane.m_Normal );
		if ( flLength < 1e-6 || !std::isfinite( plane.m_flDistance ) )
			return refuse( "Draw a usable knife line first." );
		plane.m_Normal = Scale( plane.m_Normal, 1 / flLength );
		plane.m_flDistance /= flLength;
		for ( auto nSolid : selected )
		{
			const auto &source = scene.m_Map.m_Solids[ nSolid ];
			std::string szProblem;
			auto front = ClipBack( scene.m_Map, source, { Scale( plane.m_Normal, -1 ), -plane.m_flDistance }, source.m_Sides.front(), szProblem );
			auto back = ClipBack( scene.m_Map, source, plane, source.m_Sides.front(), szProblem );
			if ( !szProblem.empty() )
				return refuse( szProblem );
			if ( !front || !back )
				continue;
			std::vector<Solid_t> pieces{ *front, *back };
			MarkInternalFaces( scene.m_Map, pieces );
			builder.Replace( nSolid, std::move( pieces ) );
		}
		if ( result.m_Edit.IsEmpty() )
			return refuse( "The knife line misses the selected brushes." );
		return result;
	}
	if ( nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_HULL || nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_BRIDGE || nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_INTERSECT )
	{
		const auto &source = scene.m_Map.m_Solids[ selected.front() ];
		for ( auto n : selected )
		{
			if ( scene.m_Map.m_Solids[ n ].m_nEntity != source.m_nEntity )
				return refuse( "These brushes belong to different entities." );
			if ( std::ranges::any_of( scene.m_Map.m_Solids[ n ].m_Sides, []( const Side_t &side ) { return bool( side.m_pDisp ); } ) )
				return refuse( "This operation cannot reconstruct a displaced brush." );
		}
		if ( nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_INTERSECT )
		{
			if ( selected.size() < 2 )
				return refuse( "Select at least two brushes to intersect." );
			std::optional<Solid_t> output = source;
			std::string szProblem;
			for ( std::size_t i = 1; i < selected.size() && output; ++i )
			{
				const auto n = selected[ i ];
				for ( const auto &face : scene.m_Solids[ n ].m_Faces )
				{
					if ( !output )
						break;
					output = ClipBack( scene.m_Map, *output, face.m_Plane, scene.m_Map.m_Solids[ n ].m_Sides[ face.m_nSide ], szProblem );
				}
			}
			if ( !szProblem.empty() )
				return refuse( szProblem );
			if ( !output || Volume( BuildSolid( *output ) ) < 1e-6 )
				return refuse( "The selected brushes share no volume." );
			builder.Replace( selected.front(), { *output } );
			for ( std::size_t i = 1; i < selected.size(); ++i )
				builder.Replace( selected[ i ], {} );
			return result;
		}
		std::vector<Point_t> points;
		if ( nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_BRIDGE )
		{
			if ( parts.size() != 2 || parts[ 0 ].m_nKind != BrushPartKind_t::BRUSH_PART_FACE || parts[ 1 ].m_nKind != BrushPartKind_t::BRUSH_PART_FACE || parts[ 0 ].m_nObject == parts[ 1 ].m_nObject )
				return refuse( "Select one face on each of two different brushes to bridge." );
			const auto a = scene.GetIndex( parts[ 0 ].m_nObject ), b = scene.GetIndex( parts[ 1 ].m_nObject );
			const auto meshA = CBrushMesh::FromGeometry( scene.m_Solids[ a ] ), meshB = CBrushMesh::FromGeometry( scene.m_Solids[ b ] );
			const auto pa = *meshA.FacePlane( parts[ 0 ].m_nIndex ), pb = *meshB.FacePlane( parts[ 1 ].m_nIndex );
			for ( auto n : meshA.m_Faces[ parts[ 0 ].m_nIndex ].m_Corners )
			{
				const auto point = meshA.m_Vertices[ n ];
				if ( pb.Distance( point ) < -0.01 )
					return refuse( "The bridge would enter the second brush." );
				points.push_back( point );
			}
			for ( auto n : meshB.m_Faces[ parts[ 1 ].m_nIndex ].m_Corners )
			{
				const auto point = meshB.m_Vertices[ n ];
				if ( pa.Distance( point ) < -0.01 )
					return refuse( "The bridge would enter the first brush." );
				points.push_back( point );
			}
		}
		else if ( !parts.empty() )
			for ( const auto &part : parts )
			{
				const auto &mesh = *meshes.Get( part.m_nObject );
				for ( auto n : CornersOf( mesh, part.m_nKind, part.m_nIndex ) )
					points.push_back( mesh.m_Mesh.m_Vertices[ n ] );
			}
		else
			for ( auto n : selected )
			{
				const auto mesh = CBrushMesh::FromGeometry( scene.m_Solids[ n ] );
				points.insert( points.end(), mesh.m_Vertices.begin(), mesh.m_Vertices.end() );
			}
		const Side_t *pLike = nullptr;
		if ( nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_BRIDGE )
		{
			const auto &part = parts.front();
			const auto n = scene.GetIndex( part.m_nObject );
			const auto mesh = CBrushMesh::FromGeometry( scene.m_Solids[ n ] );
			pLike = &scene.m_Map.m_Solids[ n ].m_Sides[ mesh.m_Faces[ part.m_nIndex ].m_nSide ];
		}
		auto output = Hull( scene.m_Map, source, points, pLike );
		if ( !output )
			return refuse( "The selection does not enclose a usable hull." );
		if ( nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_HULL )
			for ( const auto &face : BuildSolid( *output ).m_Faces )
				for ( auto n : selected )
					for ( const auto &original : scene.m_Solids[ n ].m_Faces )
						if ( Dot( face.m_Plane.m_Normal, original.m_Plane.m_Normal ) > 0.999999 && std::abs( face.m_Plane.m_flDistance - original.m_Plane.m_flDistance ) < 0.01 )
						{
							auto side = scene.m_Map.m_Solids[ n ].m_Sides[ original.m_nSide ];
							side.m_PlusVertices.clear();
							side.m_Span = {};
							output->m_Sides[ face.m_nSide ] = std::move( side );
						}
		builder.Add( *output, source );
		return result;
	}
	if ( nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_ALIGN_FACE )
	{
		if ( parts.size() != 1 || parts.front().m_nKind != BrushPartKind_t::BRUSH_PART_FACE )
			return refuse( "Select one face to make axis-aligned." );
		const auto &part = parts.front();
		const auto mesh = CBrushMesh::FromGeometry( scene.m_Solids[ scene.GetIndex( part.m_nObject ) ] );
		const auto normal = mesh.FacePlane( part.m_nIndex )->m_Normal;
		int nAxis = params.m_nAxis;
		if ( params.m_bBestPlane )
		{
			nAxis = 0;
			for ( int i = 1; i < 3; ++i )
				if ( std::abs( normal[ i ] ) > std::abs( normal[ nAxis ] ) )
					nAxis = i;
		}
		if ( nAxis < 0 || nAxis > 2 )
			return refuse( "Choose X, Y or Z." );
		Point_t target{};
		target[ nAxis ] = normal[ nAxis ] < 0 ? -1 : 1;
		const auto cross = Cross( normal, target );
		if ( Length( cross ) < 1e-8 )
			return refuse( "The face is already axis-aligned." );
		const auto transform = Transform_t::RotationAbout( mesh.FaceCenter( part.m_nIndex ), Normalized( cross ), std::acos( std::clamp( Dot( normal, target ), -1.0, 1.0 ) ) * 180 / std::numbers::pi );
		result.m_Edit = MoveParts( scene, parts, transform, nConcave, nullptr, &result.m_szProblem );
		return result;
	}
	if ( bySolid.empty() && nOperation != BrushModelOperation_t::BRUSH_MODEL_OPERATION_BEVEL )
		return refuse( "Select the vertices, edges or faces for this operation." );
	if ( nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_BEVEL && params.m_bAllEdges )
	{
		bySolid.clear();
		for ( auto n : selected )
			bySolid[ n ] = {};
	}
	if ( nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_BEVEL && bySolid.empty() )
		for ( auto n : selected )
			bySolid[ n ] = {};
	std::optional<Plane_t> flatten;
	if ( nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_FLATTEN )
	{
		std::vector<Point_t> points;
		for ( const auto &[ n, solidParts ] : bySolid )
		{
			const auto mesh = CBrushMesh::FromGeometry( scene.m_Solids[ n ] );
			std::set<std::uint32_t> corners;
			for ( const auto &part : solidParts )
				for ( auto index : CornersOf( mesh, part.m_nKind, part.m_nIndex ) )
					corners.insert( index );
			for ( auto index : corners )
				points.push_back( mesh.m_Vertices[ index ] );
		}
		if ( params.m_bBestPlane )
			flatten = BestPlane( points );
		else
		{
			Point_t centre{};
			for ( auto point : points )
				centre = Add( centre, point );
			if ( !points.empty() )
				centre = Scale( centre, 1.0 / points.size() );
			flatten = Plane_t{ axis, Dot( axis, params.m_bUsePivot ? pivot : centre ) + params.m_flAmount };
		}
		if ( !flatten )
			return refuse( "Select at least three non-collinear vertices for a fitted plane." );
	}
	std::map<std::uint32_t, double> loopSlides;
	if ( nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_LOOP )
	{
		for ( const auto &[ n, solidParts ] : bySolid )
		{
			if ( solidParts.empty() || solidParts.front().m_nKind != BrushPartKind_t::BRUSH_PART_EDGE )
				return refuse( "Point at an edge to choose its ring." );
			loopSlides[ n ] = params.m_flSlide;
		}
		std::vector<std::uint32_t> pending;
		for ( const auto &[ n, slide ] : loopSlides )
			pending.push_back( n );
		for ( std::size_t i = 0; i < pending.size(); ++i )
		{
			const auto nFrom = pending[ i ];
			const auto mesh = CBrushMesh::FromGeometry( scene.m_Solids[ nFrom ] );
			const auto edges = mesh.GetEdges();
			const auto ring = FindBrushEdgeRing( mesh, bySolid[ nFrom ].front().m_nIndex );
			for ( auto nTo : selected )
			{
				if ( loopSlides.contains( nTo ) )
					continue;
				bool bFlush = false;
				for ( const auto &a : scene.m_Solids[ nFrom ].m_Faces )
					for ( const auto &b : scene.m_Solids[ nTo ].m_Faces )
					{
						if ( Dot( a.m_Plane.m_Normal, b.m_Plane.m_Normal ) > -0.999 || std::abs( a.m_Plane.m_flDistance + b.m_Plane.m_flDistance ) > 0.01 )
							continue;
						auto polygon = a.m_Points;
						for ( std::size_t k = 0; k < b.m_Points.size() && !polygon.empty(); ++k )
						{
							const auto p = b.m_Points[ k ], q = b.m_Points[ ( k + 1 ) % b.m_Points.size() ];
							const auto normal = Normalized( Cross( b.m_Plane.m_Normal, Subtract( q, p ) ) );
							polygon = ClipPolygon( polygon, { normal, Dot( normal, p ) }, 1e-6 );
						}
						Point_t area{};
						for ( std::size_t k = 0; k < polygon.size(); ++k )
							area = Add( area, Cross( polygon[ k ], polygon[ ( k + 1 ) % polygon.size() ] ) );
						if ( Length( area ) > 0.001 )
							bFlush = true;
					}
				if ( !bFlush )
					continue;
				const auto neighbour = CBrushMesh::FromGeometry( scene.m_Solids[ nTo ] );
				const auto otherEdges = neighbour.GetEdges();
				bool bFound = false;
				for ( const auto &entry : ring )
				{
					const auto edge = edges[ entry.m_nEdge ];
					const auto a = mesh.m_Vertices[ edge.first ], b = mesh.m_Vertices[ edge.second ], along = Subtract( b, a );
					for ( std::uint32_t e = 0; e < otherEdges.size() && !bFound; ++e )
					{
						const auto p = neighbour.m_Vertices[ otherEdges[ e ].first ], q = neighbour.m_Vertices[ otherEdges[ e ].second ], direction = Subtract( q, p );
						if ( Length( Cross( along, direction ) ) > 0.001 * Length( along ) * Length( direction ) || Length( Cross( Subtract( p, a ), along ) ) > 0.001 * Length( along ) )
							continue;
						std::vector<double> fractions;
						for ( int k = 1; k <= params.m_nCount; ++k )
						{
							double t = ( k + loopSlides[ nFrom ] ) / ( params.m_nCount + 1 );
							if ( entry.m_bReverse )
								t = 1 - t;
							fractions.push_back( Dot( Subtract( Lerp( a, b, t ), p ), direction ) / Dot( direction, direction ) );
						}
						std::ranges::sort( fractions );
						if ( fractions.front() <= 0.001 || fractions.back() >= 0.999 )
							continue;
						const double slide = fractions.front() * ( params.m_nCount + 1 ) - 1;
						bool bCompatible = std::abs( slide ) < 1;
						for ( int k = 0; k < params.m_nCount; ++k )
							bCompatible &= std::abs( fractions[ k ] - ( k + 1 + slide ) / ( params.m_nCount + 1 ) ) < 1e-5;
						if ( !bCompatible )
							return refuse( "Multiple cuts need matching edge spacing across the flush brushes." );
						bySolid[ nTo ] = { { scene.SolidObject( nTo ), BrushPartKind_t::BRUSH_PART_EDGE, e } };
						loopSlides[ nTo ] = slide;
						pending.push_back( nTo );
						bFound = true;
					}
					if ( bFound )
						break;
				}
			}
		}
	}
	// Faces pushed out where their brush grows: moved together after, per direction, so many
	// brushes are refitted at once (and new side ids don't repeat between them).
	std::vector<std::pair<BrushPart_t, Point_t>> grown;
	for ( const auto &[ nSolid, solidParts ] : bySolid )
	{
		const auto &source = scene.m_Map.m_Solids[ nSolid ];
		auto mesh = CBrushMesh::FromGeometry( scene.m_Solids[ nSolid ] );
		std::set<std::uint32_t> corners;
		for ( const auto &part : solidParts )
			for ( auto n : CornersOf( mesh, part.m_nKind, part.m_nIndex ) )
				corners.insert( n );
		for ( const auto &face : mesh.m_Faces )
			if ( source.m_Sides[ face.m_nSide ].m_pDisp && std::ranges::any_of( face.m_Corners, [ & ]( auto n ) { return corners.contains( n ); } ) )
				return refuse( "The selected parts touch a displacement's corners." );
		if ( nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_EXTRUDE )
		{
			if ( std::abs( params.m_flAmount ) < 1e-6 )
				return refuse( "Extrude needs a non-zero distance." );
			std::vector<Solid_t> remaining{ source };
			std::vector<std::pair<Solid_t, Point_t>> added;
			for ( const auto &part : solidParts )
			{
				if ( part.m_nKind != BrushPartKind_t::BRUSH_PART_FACE )
					return refuse( "Extrude works on faces." );
				const auto &face = scene.m_Solids[ nSolid ].m_Faces[ part.m_nIndex ];
				auto prism = Prism( scene.m_Map, source, face, params.m_flAmount );
				if ( !prism )
					return refuse( "This face cannot form a prism at that distance." );
				if ( params.m_flAmount < 0 )
				{
					std::vector<Solid_t> next;
					for ( const auto &piece : remaining )
					{
						auto pieces = SubtractSolid( scene.m_Map, piece, *prism, source.m_Sides[ face.m_nSide ], result.m_szProblem );
						if ( !result.m_szProblem.empty() )
							return refuse( result.m_szProblem );
						next.insert( next.end(), pieces.begin(), pieces.end() );
					}
					remaining = std::move( next );
				}
				else if ( !params.m_bNewBrush && solidParts.size() == 1 )
				{
					auto moved = mesh;
					for ( auto n : mesh.m_Faces[ part.m_nIndex ].m_Corners )
						moved.m_Vertices[ n ] = Add( moved.m_Vertices[ n ], Scale( face.m_Plane.m_Normal, params.m_flAmount ) );
					if ( moved.IsConvex( 0.01 ) && std::ranges::all_of( std::views::iota( std::size_t{ 0 }, moved.m_Faces.size() ), [ & ]( auto f ) { return moved.IsFlat( f, 0.01 ); } ) )
					{
						grown.emplace_back( part, Scale( face.m_Plane.m_Normal, params.m_flAmount ) );
						result.m_SelectedSides.push_back( source.m_Sides[ face.m_nSide ].m_nId );
						continue;
					}
					else
						added.emplace_back( *prism, face.m_Plane.m_Normal );
				}
				else
					added.emplace_back( *prism, face.m_Plane.m_Normal );
			}
			if ( params.m_flAmount < 0 )
			{
				if ( remaining.empty() )
					return refuse( "The extrusion would remove the whole brush." );
				for ( auto &piece : remaining )
					for ( const auto &part : solidParts )
					{
						const auto &face = scene.m_Solids[ nSolid ].m_Faces[ part.m_nIndex ];
						ContinueCap( piece, source.m_Sides[ face.m_nSide ], face.m_Plane, face.m_Plane.m_flDistance + params.m_flAmount );
					}
				MarkInternalFaces( scene.m_Map, remaining );
				builder.Replace( nSolid, std::move( remaining ), true, scene.m_Solids[ nSolid ].m_Faces[ solidParts.front().m_nIndex ].m_Plane.m_Normal, scene.m_Solids[ nSolid ].m_Faces[ solidParts.front().m_nIndex ].m_Plane.m_flDistance + params.m_flAmount );
			}
			if ( !added.empty() )
			{
				std::vector<Solid_t> touching{ source };
				for ( auto &[ piece, normal ] : added )
					touching.push_back( piece );
				MarkInternalFaces( scene.m_Map, touching );
				builder.Replace( nSolid, { touching.front() } );
				for ( std::size_t i = 0; i < added.size(); ++i )
					builder.Add( touching[ i + 1 ], source, true, added[ i ].second );
			}
			continue;
		}
		if ( nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_BEVEL )
		{
			if ( params.m_flAmount <= 0 || params.m_nCount > 32 )
				return refuse( "Bevel needs a positive width and at most 32 segments." );
			std::vector<Plane_t> planes;
			const auto edges = mesh.GetEdges();
			if ( solidParts.empty() )
			{
				for ( auto edge : edges )
				{
					auto cuts = EdgeBevelPlanes( mesh, edge, params.m_flAmount, params.m_nCount );
					planes.insert( planes.end(), cuts.begin(), cuts.end() );
				}
			}
			for ( const auto &part : solidParts )
			{
				std::vector<Plane_t> cuts;
				if ( part.m_nKind == BrushPartKind_t::BRUSH_PART_EDGE )
					cuts = EdgeBevelPlanes( mesh, edges[ part.m_nIndex ], params.m_flAmount, params.m_nCount );
				else if ( part.m_nKind == BrushPartKind_t::BRUSH_PART_VERTEX )
					cuts = VertexBevelPlanes( mesh, part.m_nIndex, params.m_flAmount, params.m_nCount );
				else
					return refuse( "Select edges, vertices, or the brush alone to bevel." );
				planes.insert( planes.end(), cuts.begin(), cuts.end() );
			}
			std::optional<Solid_t> output = source;
			for ( const auto &plane : planes )
			{
				if ( !output )
					break;
				output = ClipBack( scene.m_Map, *output, plane, source.m_Sides.front(), result.m_szProblem );
				if ( !result.m_szProblem.empty() )
					return refuse( result.m_szProblem );
			}
			if ( !output || planes.empty() )
				return refuse( "That bevel would leave no usable brush." );
			builder.Replace( nSolid, { *output } );
			continue;
		}
		if ( nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_LOOP )
		{
			if ( solidParts.empty() || solidParts.front().m_nKind != BrushPartKind_t::BRUSH_PART_EDGE )
				return refuse( "Point at an edge to choose its ring." );
			if ( !LoopCutBrush( mesh, solidParts.front().m_nIndex, params.m_nCount, loopSlides[ nSolid ] ) )
				return refuse( "The edge does not lead through a usable quad ring." );
			if ( !convert( nSolid, mesh, source ) )
				return refuse( result.m_szProblem );
			continue;
		}
		if ( nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_DISSOLVE )
		{
			std::vector<std::pair<std::uint32_t, std::uint32_t>> edges;
			const auto originalEdges = mesh.GetEdges();
			for ( const auto &part : solidParts )
			{
				if ( part.m_nKind == BrushPartKind_t::BRUSH_PART_EDGE )
					edges.push_back( originalEdges[ part.m_nIndex ] );
				else if ( part.m_nKind == BrushPartKind_t::BRUSH_PART_VERTEX )
				{
					for ( auto edge : originalEdges )
						if ( edge.first == part.m_nIndex || edge.second == part.m_nIndex )
							edges.push_back( edge );
				}
				else
					return refuse( "Dissolve works on edges or vertices." );
			}
			bool bChanged = false;
			for ( auto edge : edges )
			{
				std::vector<std::size_t> adjacent;
				for ( std::size_t f = 0; f < mesh.m_Faces.size(); ++f )
				{
					const auto &c = mesh.m_Faces[ f ].m_Corners;
					for ( std::size_t k = 0; k < c.size(); ++k )
						if ( std::minmax( c[ k ], c[ ( k + 1 ) % c.size() ] ) == std::minmax( edge.first, edge.second ) )
						{
							adjacent.push_back( f );
							break;
						}
				}
				if ( adjacent.size() == 2 )
					bChanged |= JoinFaces( mesh, adjacent[ 0 ], adjacent[ 1 ] );
			}
			for ( auto vertex : corners )
				for ( auto &face : mesh.m_Faces )
					if ( face.m_Corners.size() > 3 )
					{
						auto found = std::ranges::find( face.m_Corners, vertex );
						if ( found == face.m_Corners.end() )
							continue;
						const auto i = static_cast<std::size_t>( found - face.m_Corners.begin() );
						const auto n = face.m_Corners.size();
						const auto a = mesh.m_Vertices[ face.m_Corners[ ( i + n - 1 ) % n ] ], b = mesh.m_Vertices[ face.m_Corners[ ( i + 1 ) % n ] ], p = mesh.m_Vertices[ vertex ];
						if ( Length( Cross( Subtract( p, a ), Subtract( b, a ) ) ) <= 0.01 * Length( Subtract( b, a ) ) )
						{
							face.m_Corners.erase( found );
							bChanged = true;
						}
					}
			if ( !bChanged )
				return refuse( "Only coplanar edges and redundant collinear vertices can dissolve." );
			mesh.Tidy();
			if ( !convert( nSolid, mesh, source ) )
				return refuse( result.m_szProblem );
			continue;
		}
		if ( nOperation == BrushModelOperation_t::BRUSH_MODEL_OPERATION_FLATTEN )
		{
			const auto before = mesh;
			bool bChanged = false;
			for ( auto n : corners )
			{
				const auto point = Subtract( mesh.m_Vertices[ n ], Scale( flatten->m_Normal, flatten->Distance( mesh.m_Vertices[ n ] ) ) );
				bChanged |= Length( Subtract( point, mesh.m_Vertices[ n ] ) ) > 1e-6;
				mesh.m_Vertices[ n ] = point;
			}
			if ( !bChanged )
				continue;
			auto textured = source;
			for ( const auto &face : mesh.m_Faces )
				if ( std::ranges::all_of( face.m_Corners, [ & ]( auto n ) { return corners.contains( n ); } ) )
					ProjectTexture( textured.m_Sides[ face.m_nSide ], before, mesh, face );
			if ( !convert( nSolid, mesh, textured ) )
				return refuse( result.m_szProblem );
			continue;
		}

		return refuse( "This operation cannot use the selected parts." );
	}
	while ( !grown.empty() )
	{
		const Point_t offset = grown.front().second;
		std::vector<BrushPart_t> together;
		std::erase_if( grown, [ & ]( const auto &entry ) {
			if ( Length( Subtract( entry.second, offset ) ) > 1e-9 )
				return false;
			together.push_back( entry.first );
			return true;
		} );
		auto edit = MoveParts( scene, together, Transform_t::Translation( offset ), nConcave, nullptr, &result.m_szProblem );
		if ( !result.m_szProblem.empty() )
			return refuse( result.m_szProblem );
		result.m_Edit.m_Solids.insert( result.m_Edit.m_Solids.end(), edit.m_Solids.begin(), edit.m_Solids.end() );
	}
	if ( result.m_Edit.IsEmpty() )
		return refuse( "The operation changes no geometry." );
	return result;
}
} // namespace xv::map
