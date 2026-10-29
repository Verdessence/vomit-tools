//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Overlays and decals cut to faces. An overlay's corners lie in its plane (BasisOrigin,
//          BasisU, BasisV) at uv0-uv3, and take (StartU, StartV), (StartU, EndV), (EndU, EndV) and
//          (EndU, StartV) of its texture, as maps Hammer saves show; each face's triangles are
//          laid flat on that plane, the overlay cut to them there, and the pieces put back on the
//          triangles along BasisNormal.
//
//===========================================================================//
#include "map/Overlay.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <limits>
#include "map/Displacement.h"
#include "map/Edit.h"
#include <unordered_map>

#include "map/Scene.h"
#include "map/Vmf.h"
#include "map/VmfValues.h"
#include "util/Numbers.h"
#include "util/Scan.h"

namespace xv::map
{

namespace
{

using Point2_t = std::array<double, 2>;

// An overlay or a decal in its own plane: where its corners are and what of the texture each
// shows.
struct Frame_t
{
	Point_t m_Origin{};
	Point_t m_U{};
	Point_t m_V{};
	Point_t m_Normal{};
	std::array<Point2_t, 4> m_Corners{};   // in the plane, along U and V
	std::array<Point2_t, 4> m_Texcoords{}; // 0-1 of the texture
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double Cross2( Point2_t a, Point2_t b )
{
	return a[ 0 ] * b[ 1 ] - a[ 1 ] * b[ 0 ];
}

//-----------------------------------------------------------------------------
// Purpose: Three numbers from a key ("1 0 0"), or nullopt.
//-----------------------------------------------------------------------------
std::optional<Point_t> VectorOf( std::string_view szValue )
{
	CScanner scanner( szValue );
	Point_t p{};
	for ( double &fl : p )
	{
		const std::optional<double> value = scanner.Double();
		if ( !value )
		{
			return std::nullopt;
		}
		fl = *value;
	}
	return p;
}

//-----------------------------------------------------------------------------
// Purpose: A material named in a key the way the map's own are named: lower case, '/', and
//          neither "materials/" before nor ".vmt" after.
//-----------------------------------------------------------------------------
std::string MaterialKey( std::string_view szValue )
{
	std::string sz = NormalizeMaterial( szValue );
	if ( sz.starts_with( "materials/" ) )
	{
		sz.erase( 0, 10 );
	}
	if ( sz.ends_with( ".vmt" ) )
	{
		sz.resize( sz.size() - 4 );
	}
	return sz;
}

//-----------------------------------------------------------------------------
// Purpose: The part of polygon inside the convex polygon clip (both in the plane, clip wound
//          either way round).
//-----------------------------------------------------------------------------
std::vector<Point2_t> Clip2( std::vector<Point2_t> polygon, std::span<const Point2_t> clip )
{
	double flArea = 0.0;
	for ( std::size_t i = 0; i < clip.size(); i++ )
	{
		flArea += Cross2( clip[ i ], clip[ ( i + 1 ) % clip.size() ] );
	}
	const double flSide = flArea >= 0.0 ? 1.0 : -1.0;
	for ( std::size_t i = 0; i < clip.size() && !polygon.empty(); i++ )
	{
		const Point2_t a = clip[ i ];
		const Point2_t b = clip[ ( i + 1 ) % clip.size() ];
		const Point2_t edge{ b[ 0 ] - a[ 0 ], b[ 1 ] - a[ 1 ] };
		auto inside = [ & ]( Point2_t p ) { return flSide * Cross2( edge, { p[ 0 ] - a[ 0 ], p[ 1 ] - a[ 1 ] } ); };
		std::vector<Point2_t> kept;
		for ( std::size_t k = 0; k < polygon.size(); k++ )
		{
			const Point2_t p = polygon[ k ];
			const Point2_t q = polygon[ ( k + 1 ) % polygon.size() ];
			const double flP = inside( p );
			const double flQ = inside( q );
			if ( flP >= 0.0 )
			{
				kept.push_back( p );
			}
			if ( ( flP >= 0.0 ) != ( flQ >= 0.0 ) )
			{
				const double t = flP / ( flP - flQ );
				kept.push_back( { p[ 0 ] + ( q[ 0 ] - p[ 0 ] ) * t, p[ 1 ] + ( q[ 1 ] - p[ 1 ] ) * t } );
			}
		}
		polygon = std::move( kept );
	}
	return polygon.size() >= 3 ? polygon : std::vector<Point2_t>{};
}

// The mesh's triangles by the side they were made from.
using SideTriangles_t = std::unordered_map<int, std::vector<std::uint32_t>>;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
SideTriangles_t TrianglesBySide( const CScene &scene )
{
	SideTriangles_t triangles;
	const CMapMesh &mesh = scene.GetMesh();
	for ( std::uint32_t t = 0; t < mesh.m_TriangleFaces.size(); t++ )
	{
		const MeshFace_t &face = mesh.m_Faces[ mesh.m_TriangleFaces[ t ] ];
		triangles[ scene.m_Map.m_Solids[ face.m_nSolid ].m_Sides[ face.m_nSide ].m_nId ].push_back( t );
	}
	return triangles;
}

//-----------------------------------------------------------------------------
// Purpose: Lays the frame onto the triangles given (from the scene's mesh): each is flattened
//          onto the frame's plane, the frame's quad cut to it, and the piece put back on the
//          triangle along the frame's normal, a little off it. Triangles facing away from the
//          frame, or edge on to it, are skipped.
//-----------------------------------------------------------------------------
void LayOnto( CMapMesh &out, const CScene &scene, std::span<const std::uint32_t> triangles, const Frame_t &frame, const OverlayMaterial_t &material, std::uint32_t nObject )
{
	const CMapMesh &mesh = scene.GetMesh();
	const std::vector<Point2_t> quad( frame.m_Corners.begin(), frame.m_Corners.end() );
	for ( const std::uint32_t t : triangles )
	{
		std::array<Point_t, 3> corners{};
		for ( std::size_t k = 0; k < 3; k++ )
		{
			corners[ k ] = ToPoint( mesh.m_Vertices[ mesh.m_Indices[ t * 3 + k ] ].m_vecPosition );
		}
		// Faces wind clockwise seen from outside, so (b - a) x (c - a) points in.
		const Point_t outward = Normalized( Cross( Subtract( corners[ 2 ], corners[ 0 ] ), Subtract( corners[ 1 ], corners[ 0 ] ) ) );
		const double flFacing = Dot( outward, frame.m_Normal );
		if ( flFacing < 0.1 )
		{
			continue;
		}
		std::array<Point2_t, 3> flat{};
		for ( std::size_t k = 0; k < 3; k++ )
		{
			const Point_t d = Subtract( corners[ k ], frame.m_Origin );
			flat[ k ] = { Dot( d, frame.m_U ), Dot( d, frame.m_V ) };
		}
		std::vector<Point2_t> piece = Clip2( quad, flat );
		if ( piece.empty() )
		{
			continue;
		}
		// Back onto the triangle's plane along the frame's normal, then off it along the same
		// normal, so pieces on neighbouring triangles still meet at their shared edges.
		const double flPlane = Dot( outward, corners[ 0 ] );
		std::vector<Point_t> points;
		std::vector<Point2_t> texcoords;
		for ( const Point2_t &p : piece )
		{
			const Point_t inPlane = Add( frame.m_Origin, Add( Scale( frame.m_U, p[ 0 ] ), Scale( frame.m_V, p[ 1 ] ) ) );
			const double flAlong = ( flPlane - Dot( outward, inPlane ) ) / flFacing;
			points.push_back( Add( inPlane, Scale( frame.m_Normal, flAlong + OVERLAY_LIFT ) ) );
			const Point2_t st = InverseBilinear( p, frame.m_Corners );
			const auto lerp = [ & ]( std::size_t a ) {
				const double flLow = frame.m_Texcoords[ 0 ][ a ] + ( frame.m_Texcoords[ 3 ][ a ] - frame.m_Texcoords[ 0 ][ a ] ) * st[ 0 ];
				const double flHigh = frame.m_Texcoords[ 1 ][ a ] + ( frame.m_Texcoords[ 2 ][ a ] - frame.m_Texcoords[ 1 ][ a ] ) * st[ 0 ];
				return flLow + ( flHigh - flLow ) * st[ 1 ];
			};
			texcoords.push_back( { lerp( 0 ), lerp( 1 ) } );
		}
		// Wound as faces are: clockwise seen from outside.
		Point_t normal{};
		for ( std::size_t k = 0; k < points.size(); k++ )
		{
			normal = Add( normal, Cross( points[ k ], points[ ( k + 1 ) % points.size() ] ) );
		}
		if ( Dot( normal, outward ) > 0.0 )
		{
			std::ranges::reverse( points );
			std::ranges::reverse( texcoords );
		}
		const auto nBase = static_cast<std::uint32_t>( out.m_Vertices.size() );
		for ( std::size_t k = 0; k < points.size(); k++ )
		{
			MeshVertex_t vertex;
			vertex.m_vecPosition = ToVec3( points[ k ] );
			vertex.m_vecNormal = ToVec3( outward );
			vertex.m_Texel = { static_cast<float>( texcoords[ k ][ 0 ] * material.m_TextureSize[ 0 ] ), static_cast<float>( texcoords[ k ][ 1 ] * material.m_TextureSize[ 1 ] ) };
			vertex.m_nObject = nObject;
			out.m_Vertices.push_back( vertex );
		}
		for ( std::uint32_t k = 1; k + 1 < points.size(); k++ )
		{
			out.m_Indices.insert( out.m_Indices.end(), { nBase, nBase + k, nBase + k + 1 } );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: The frame's corners as lines, a little in front of its plane.
//-----------------------------------------------------------------------------
void Outline( CMapMesh &out, const Frame_t &frame, std::uint32_t nObject )
{
	const auto nBase = static_cast<std::uint32_t>( out.m_Vertices.size() );
	for ( const Point2_t &corner : frame.m_Corners )
	{
		MeshVertex_t vertex;
		vertex.m_vecPosition = ToVec3( Add( Add( frame.m_Origin, Scale( frame.m_Normal, OVERLAY_LIFT ) ), Add( Scale( frame.m_U, corner[ 0 ] ), Scale( frame.m_V, corner[ 1 ] ) ) ) );
		vertex.m_vecNormal = ToVec3( frame.m_Normal );
		vertex.m_nObject = nObject;
		out.m_Vertices.push_back( vertex );
	}
	out.m_Lines.insert( out.m_Lines.end(), { nBase, nBase + 1, nBase + 1, nBase + 2, nBase + 2, nBase + 3, nBase + 3, nBase } );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::array<double, 2> InverseBilinear( std::array<double, 2> p, const std::array<std::array<double, 2>, 4> &corners )
{
	// p = c0 + s e + t f + s t g, with e along s (c0 to c3) and f along t (c0 to c1).
	const Point2_t &c0 = corners[ 0 ];
	const Point2_t e{ corners[ 3 ][ 0 ] - c0[ 0 ], corners[ 3 ][ 1 ] - c0[ 1 ] };
	const Point2_t f{ corners[ 1 ][ 0 ] - c0[ 0 ], corners[ 1 ][ 1 ] - c0[ 1 ] };
	const Point2_t g{ c0[ 0 ] - corners[ 3 ][ 0 ] + corners[ 2 ][ 0 ] - corners[ 1 ][ 0 ], c0[ 1 ] - corners[ 3 ][ 1 ] + corners[ 2 ][ 1 ] - corners[ 1 ][ 1 ] };
	const Point2_t h{ p[ 0 ] - c0[ 0 ], p[ 1 ] - c0[ 1 ] };
	// Crossing h - t f = s (e + t g) with e + t g leaves k2 t^2 + k1 t + k0 = 0.
	const double k2 = Cross2( g, f );
	const double k1 = Cross2( e, f ) + Cross2( h, g );
	const double k0 = Cross2( h, e );
	auto sFor = [ & ]( double t ) {
		const Point2_t d{ e[ 0 ] + g[ 0 ] * t, e[ 1 ] + g[ 1 ] * t };
		const std::size_t a = std::abs( d[ 0 ] ) >= std::abs( d[ 1 ] ) ? 0 : 1;
		return d[ a ] != 0.0 ? ( h[ a ] - f[ a ] * t ) / d[ a ] : 0.0;
	};
	const double flScale = e[ 0 ] * e[ 0 ] + e[ 1 ] * e[ 1 ] + f[ 0 ] * f[ 0 ] + f[ 1 ] * f[ 1 ];
	if ( std::abs( k2 ) <= 1e-12 * std::max( flScale, 1e-30 ) )
	{
		// A parallelogram: the equation is linear.
		const double t = k1 != 0.0 ? -k0 / k1 : 0.0;
		return { sFor( t ), t };
	}
	const double flRoot = std::sqrt( std::max( k1 * k1 - 4.0 * k0 * k2, 0.0 ) );
	const double t0 = ( -k1 - flRoot ) / ( 2.0 * k2 );
	const double t1 = ( -k1 + flRoot ) / ( 2.0 * k2 );
	const Point2_t a{ sFor( t0 ), t0 };
	const Point2_t b{ sFor( t1 ), t1 };
	// The root inside the quad, or the nearer to it.
	auto outside = []( Point2_t st ) { return std::max( { -st[ 0 ], st[ 0 ] - 1.0, -st[ 1 ], st[ 1 ] - 1.0, 0.0 } ); };
	return outside( a ) <= outside( b ) ? a : b;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::string> OverlayMaterialNames( const CScene &scene )
{
	std::vector<std::string> names;
	for ( std::size_t e = 0; e < scene.m_Entities.size(); e++ )
	{
		const EntityVisual_t &visual = scene.m_Entities[ e ];
		const std::string_view szKey = visual.m_bOverlay ? "material" : visual.m_bDecal ? "texture"
																						: "";
		if ( szKey.empty() )
		{
			continue;
		}
		std::string szName = MaterialKey( scene.m_Map.m_Entities[ e ].GetValue( szKey ) );
		if ( !szName.empty() && std::ranges::find( names, szName ) == names.end() )
		{
			names.push_back( std::move( szName ) );
		}
	}
	return names;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapMesh BuildOverlayMesh( const CScene &scene, std::span<const OverlayMaterial_t> materials )
{
	CMapMesh mesh;
	const std::vector<std::string> names = OverlayMaterialNames( scene );
	if ( names.empty() || materials.size() < names.size() )
	{
		return mesh;
	}
	const SideTriangles_t bySide = TrianglesBySide( scene );
	// Pieces are gathered by material, so each material is one batch.
	std::map<std::uint32_t, CMapMesh> byMaterial;
	CMapMesh outlines;
	for ( std::size_t e = 0; e < scene.m_Entities.size(); e++ )
	{
		const EntityVisual_t &visual = scene.m_Entities[ e ];
		const Entity_t &entity = scene.m_Map.m_Entities[ e ];
		if ( !visual.m_bOverlay && !visual.m_bDecal )
		{
			continue;
		}
		const std::string szName = MaterialKey( entity.GetValue( visual.m_bOverlay ? "material" : "texture" ) );
		const auto found = std::ranges::find( names, szName );
		if ( found == names.end() )
		{
			continue;
		}
		const OverlayMaterial_t &material = materials[ static_cast<std::size_t>( found - names.begin() ) ];
		const std::uint32_t nObject = scene.EntityObject( e );
		CMapMesh &out = byMaterial[ material.m_nMaterial ];
		if ( visual.m_bOverlay )
		{
			Frame_t frame;
			const std::optional<Point_t> origin = VectorOf( entity.GetValue( "BasisOrigin" ) );
			const std::optional<Point_t> u = VectorOf( entity.GetValue( "BasisU" ) );
			const std::optional<Point_t> v = VectorOf( entity.GetValue( "BasisV" ) );
			const std::optional<Point_t> normal = VectorOf( entity.GetValue( "BasisNormal" ) );
			if ( !origin || !u || !v || !normal || Length( *normal ) < 1e-6 )
			{
				continue;
			}
			frame.m_Origin = *origin;
			frame.m_U = Normalized( *u );
			frame.m_V = Normalized( *v );
			frame.m_Normal = Normalized( *normal );
			bool bCorners = true;
			for ( std::size_t k = 0; k < 4; k++ )
			{
				const std::optional<Point_t> corner = VectorOf( entity.GetValue( "uv" + std::to_string( k ) ) );
				bCorners = bCorners && corner;
				frame.m_Corners[ k ] = corner ? Point2_t{ ( *corner )[ 0 ], ( *corner )[ 1 ] } : Point2_t{};
			}
			if ( !bCorners )
			{
				continue;
			}
			auto number = [ & ]( std::string_view szKey, double flDefault ) { return ParseNumber<double>( entity.GetValue( szKey ) ).value_or( flDefault ); };
			const double flStartU = number( "StartU", 0.0 );
			const double flEndU = number( "EndU", 1.0 );
			const double flStartV = number( "StartV", 0.0 );
			const double flEndV = number( "EndV", 1.0 );
			frame.m_Texcoords = { Point2_t{ flStartU, flStartV }, Point2_t{ flStartU, flEndV }, Point2_t{ flEndU, flEndV }, Point2_t{ flEndU, flStartV } };
			CScanner sides( entity.GetValue( "sides" ) );
			while ( const std::optional<int> nSide = sides.Int() )
			{
				if ( const auto it = bySide.find( *nSide ); it != bySide.end() )
				{
					LayOnto( out, scene, it->second, frame, material, nObject );
				}
			}
			Outline( outlines, frame, nObject );
			continue;
		}
		// A decal: its texture's size (times $decalscale) on every face within a unit of it.
		const double flHalfWidth = material.m_TextureSize[ 0 ] * material.m_flDecalScale * 0.5;
		const double flHalfHeight = material.m_TextureSize[ 1 ] * material.m_flDecalScale * 0.5;
		Box_t reach;
		reach.Add( Subtract( visual.m_Origin, { 1, 1, 1 } ) );
		reach.Add( Add( visual.m_Origin, { 1, 1, 1 } ) );
		for ( std::size_t s = 0; s < scene.m_Solids.size(); s++ )
		{
			const SolidGeometry_t &geometry = scene.m_Solids[ s ];
			if ( scene.m_Map.m_Solids[ s ].m_bDeleted || !geometry.m_Bounds.Intersects( reach ) )
			{
				continue;
			}
			for ( const FaceGeometry_t &face : geometry.m_Faces )
			{
				const Side_t &side = scene.m_Map.m_Solids[ s ].m_Sides[ face.m_nSide ];
				if ( side.m_pDisp || std::abs( face.m_Plane.Distance( visual.m_Origin ) ) > 1.0 )
				{
					continue;
				}
				const auto it = bySide.find( side.m_nId );
				if ( it == bySide.end() )
				{
					continue;
				}
				Frame_t frame;
				frame.m_Normal = face.m_Plane.m_Normal;
				frame.m_Origin = Subtract( visual.m_Origin, Scale( frame.m_Normal, face.m_Plane.Distance( visual.m_Origin ) ) );
				// Upright and never mirrored, whatever the face's texture does: the decal's top
				// is up on a wall, +Y on a floor or a ceiling, and U runs to the right seen from in
				// front.
				const Point_t worldUp = std::abs( frame.m_Normal[ 2 ] ) > 0.7 ? Point_t{ 0, 1, 0 } : Point_t{ 0, 0, 1 };
				const Point_t up = Subtract( worldUp, Scale( frame.m_Normal, Dot( worldUp, frame.m_Normal ) ) );
				if ( Length( up ) < 1e-6 )
				{
					continue;
				}
				frame.m_V = Scale( Normalized( up ), -1.0 );
				frame.m_U = Normalized( Cross( Scale( frame.m_Normal, -1.0 ), Normalized( up ) ) );
				frame.m_Corners = { Point2_t{ -flHalfWidth, -flHalfHeight }, Point2_t{ -flHalfWidth, flHalfHeight }, Point2_t{ flHalfWidth, flHalfHeight }, Point2_t{ flHalfWidth, -flHalfHeight } };
				frame.m_Texcoords = { Point2_t{ 0, 0 }, Point2_t{ 0, 1 }, Point2_t{ 1, 1 }, Point2_t{ 1, 0 } };
				LayOnto( out, scene, it->second, frame, material, nObject );
				Outline( outlines, frame, nObject );
			}
		}
	}
	// One batch per material, with no area: there are few enough to draw whenever their
	// material is. The outlines' vertices go last.
	for ( const auto &[ nMaterial, part ] : byMaterial )
	{
		const auto nBase = static_cast<std::uint32_t>( mesh.m_Vertices.size() );
		if ( part.m_Indices.empty() )
		{
			continue;
		}
		mesh.m_Batches.push_back( { nMaterial, ~0u, static_cast<std::uint32_t>( mesh.m_Indices.size() ), static_cast<std::uint32_t>( part.m_Indices.size() ) } );
		mesh.m_Vertices.insert( mesh.m_Vertices.end(), part.m_Vertices.begin(), part.m_Vertices.end() );
		for ( const std::uint32_t n : part.m_Indices )
		{
			mesh.m_Indices.push_back( nBase + n );
		}
	}
	const auto nBase = static_cast<std::uint32_t>( mesh.m_Vertices.size() );
	mesh.m_Vertices.insert( mesh.m_Vertices.end(), outlines.m_Vertices.begin(), outlines.m_Vertices.end() );
	for ( const std::uint32_t n : outlines.m_Lines )
	{
		mesh.m_Lines.push_back( nBase + n );
	}
	return mesh;
}

//-----------------------------------------------------------------------------
// Purpose: Re-express transformed corners in a stable orthonormal projection frame. This also
//          handles scale, mirroring and shear without leaving the corners at their old size.
//-----------------------------------------------------------------------------
void TransformOverlay( const CMap &map, Entity_t &entity, const Transform_t &transform )
{
	const auto origin = VectorOf( entity.GetValue( "BasisOrigin" ) );
	if ( !origin )
		return;
	SetKeyValue( map, entity, "BasisOrigin", FormatPoint( transform.Apply( *origin ) ) );
	const auto u = VectorOf( entity.GetValue( "BasisU" ) );
	const auto v = VectorOf( entity.GetValue( "BasisV" ) );
	const auto normal = VectorOf( entity.GetValue( "BasisNormal" ) );
	if ( !u || !v || !normal )
		return;
	const Point_t oldU = Normalized( *u ), oldV = Normalized( *v );
	const Point_t turnedU = transform.Rotate( oldU ), turnedV = transform.Rotate( oldV );
	const Point_t newU = Normalized( turnedU );
	const Point_t newV = Normalized( Subtract( turnedV, Scale( newU, Dot( turnedV, newU ) ) ) );
	if ( Length( newU ) < 0.5 || Length( newV ) < 0.5 )
		return;
	Point_t newNormal = Normalized( Cross( newU, newV ) );
	if ( Dot( Cross( oldU, oldV ), *normal ) < 0 )
		newNormal = Scale( newNormal, -1 );
	SetKeyValue( map, entity, "BasisU", FormatPoint( newU ) );
	SetKeyValue( map, entity, "BasisV", FormatPoint( newV ) );
	SetKeyValue( map, entity, "BasisNormal", FormatPoint( newNormal ) );
	for ( int n = 0; n < 4; ++n )
	{
		const std::string szKey = "uv" + std::to_string( n );
		if ( const auto corner = VectorOf( entity.GetValue( szKey ) ) )
		{
			const Point_t offset = transform.Rotate( Add( Scale( oldU, ( *corner )[ 0 ] ), Scale( oldV, ( *corner )[ 1 ] ) ) );
			SetKeyValue( map, entity, szKey, FormatPoint( { Dot( offset, newU ), Dot( offset, newV ), ( *corner )[ 2 ] } ) );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Preserve attachments by face ID across ordinary edits. A projection spanning faces
//          with different transforms follows the first transformed face in its sides list.
//-----------------------------------------------------------------------------
std::vector<std::pair<std::uint32_t, Entity_t>> FollowOverlayFaces( const CScene &scene, const MapEdit_t &edit )
{
	std::set<int> removed, surviving;
	std::map<int, Transform_t> transforms;
	for ( const auto &[ nSolid, solid ] : edit.m_Solids )
	{
		if ( nSolid < scene.m_Map.m_Solids.size() )
			for ( const Side_t &side : scene.m_Map.m_Solids[ nSolid ].m_Sides )
				removed.insert( side.m_nId );
		if ( !solid.m_bDeleted )
			for ( const Side_t &side : solid.m_Sides )
				surviving.insert( side.m_nId );
	}
	for ( const int nId : surviving )
		removed.erase( nId );
	for ( const auto &[ nSolid, transform ] : edit.m_SolidTransforms )
	{
		if ( nSolid < scene.m_Map.m_Solids.size() )
			for ( const Side_t &side : scene.m_Map.m_Solids[ nSolid ].m_Sides )
				transforms.emplace( side.m_nId, transform );
	}
	std::vector<std::pair<std::uint32_t, Entity_t>> entities;
	if ( removed.empty() && transforms.empty() )
		return entities;
	for ( std::uint32_t e = 0; e < scene.m_Map.m_Entities.size(); ++e )
	{
		const auto explicitEdit = std::ranges::find_if( edit.m_Entities, [ & ]( const auto &entry ) { return entry.first == e; } );
		const Entity_t &entity = explicitEdit != edit.m_Entities.end() ? explicitEdit->second : scene.m_Map.m_Entities[ e ];
		if ( entity.m_bDeleted || ( entity.GetClassname() != "info_overlay" && entity.GetClassname() != "info_overlay_transition" ) )
			continue;
		Entity_t out = entity;
		bool bChanged = false, bTransformed = explicitEdit != edit.m_Entities.end();
		for ( const std::string_view szKey : { "sides", "sides2" } )
		{
			if ( !entity.HasKey( szKey ) )
				continue;
			CScanner scanner( entity.GetValue( szKey ) );
			std::string szSides;
			bool bRemoved = false;
			while ( const auto nId = scanner.Int() )
			{
				if ( removed.contains( *nId ) )
				{
					bRemoved = true;
					continue;
				}
				if ( !szSides.empty() )
					szSides += ' ';
				szSides += std::to_string( *nId );
				if ( !bTransformed )
				{
					if ( const auto found = transforms.find( *nId ); found != transforms.end() )
					{
						out = TransformEntity( scene.m_Map, out, found->second );
						bTransformed = bChanged = true;
					}
				}
			}
			if ( bRemoved )
			{
				SetKeyValue( scene.m_Map, out, szKey, szSides );
				bChanged = true;
			}
		}
		if ( bChanged )
			entities.emplace_back( e, std::move( out ) );
	}
	return entities;
}

//-----------------------------------------------------------------------------
// Purpose: Projection corners also serve as editing handles, without the drawing lift.
//-----------------------------------------------------------------------------
std::optional<std::array<Point_t, 4>> OverlayCorners( const Entity_t &entity )
{
	const auto origin = VectorOf( entity.GetValue( "BasisOrigin" ) );
	const auto u = VectorOf( entity.GetValue( "BasisU" ) );
	const auto v = VectorOf( entity.GetValue( "BasisV" ) );
	if ( !origin || !u || !v )
		return std::nullopt;
	std::array<Point_t, 4> corners;
	for ( int n = 0; n < 4; ++n )
	{
		const auto local = VectorOf( entity.GetValue( "uv" + std::to_string( n ) ) );
		if ( !local )
			return std::nullopt;
		corners[ n ] = Add( *origin, Add( Scale( Normalized( *u ), ( *local )[ 0 ] ), Scale( Normalized( *v ), ( *local )[ 1 ] ) ) );
	}
	return corners;
}

//-----------------------------------------------------------------------------
// Purpose: Closest point on a triangle: the perpendicular projection when inside, else one
//          of its three finite edges. Degenerate triangles still contribute their edges.
//-----------------------------------------------------------------------------
static Point_t ClosestTrianglePoint( Point_t p, Point_t a, Point_t b, Point_t c )
{
	const Point_t normal = Cross( Subtract( b, a ), Subtract( c, a ) );
	const double flArea = Dot( normal, normal );
	if ( flArea > 1e-16 )
	{
		const Point_t projected = Subtract( p, Scale( normal, Dot( Subtract( p, a ), normal ) / flArea ) );
		if ( Dot( Cross( Subtract( b, a ), Subtract( projected, a ) ), normal ) >= -1e-8 &&
			Dot( Cross( Subtract( c, b ), Subtract( projected, b ) ), normal ) >= -1e-8 &&
			Dot( Cross( Subtract( a, c ), Subtract( projected, c ) ), normal ) >= -1e-8 )
			return projected;
	}
	Point_t best = a;
	double flBest = Dot( Subtract( best, p ), Subtract( best, p ) );
	for ( const auto &edge : { std::pair{ a, b }, std::pair{ b, c }, std::pair{ c, a } } )
	{
		const Point_t dir = Subtract( edge.second, edge.first );
		const double flLength = Dot( dir, dir );
		const Point_t at = Add( edge.first, Scale( dir, flLength > 1e-16 ? std::clamp( Dot( Subtract( p, edge.first ), dir ) / flLength, 0.0, 1.0 ) : 0.0 ) );
		const double flDistance = Dot( Subtract( at, p ), Subtract( at, p ) );
		if ( flDistance < flBest )
		{
			best = at;
			flBest = flDistance;
		}
	}
	return best;
}

//-----------------------------------------------------------------------------
// Purpose: Handles may snap to face edges, but cannot escape the assigned surface.
//-----------------------------------------------------------------------------
std::optional<Point_t> OverlaySurfacePoint( const CScene &scene, const Entity_t &entity, Point_t p )
{
	std::set<int> sides;
	for ( const auto szKey : { "sides", "sides2" } )
	{
		CScanner scanner( entity.GetValue( szKey ) );
		while ( const auto nSide = scanner.Int() )
			sides.insert( *nSide );
	}
	std::optional<Point_t> best;
	double flBest = std::numeric_limits<double>::max();
	auto consider = [ & ]( Point_t a, Point_t b, Point_t c ) {
		const Point_t at = ClosestTrianglePoint( p, a, b, c );
		const double flDistance = Dot( Subtract( at, p ), Subtract( at, p ) );
		if ( flDistance < flBest )
		{
			best = at;
			flBest = flDistance;
		}
	};
	for ( std::size_t s = 0; s < scene.m_Map.m_Solids.size(); ++s )
	{
		if ( scene.m_Map.m_Solids[ s ].m_bDeleted )
			continue;
		for ( const FaceGeometry_t &face : scene.m_Solids[ s ].m_Faces )
		{
			const Side_t &side = scene.m_Map.m_Solids[ s ].m_Sides[ face.m_nSide ];
			if ( !sides.contains( side.m_nId ) )
				continue;
			if ( const auto mesh = side.m_pDisp ? BuildDisplacement( *side.m_pDisp, face ) : std::nullopt )
			{
				for ( std::size_t n = 0; n + 2 < mesh->m_Indices.size(); n += 3 )
					consider( mesh->m_Points[ mesh->m_Indices[ n ] ], mesh->m_Points[ mesh->m_Indices[ n + 1 ] ], mesh->m_Points[ mesh->m_Indices[ n + 2 ] ] );
			}
			else
				for ( std::size_t n = 1; n + 1 < face.m_Points.size(); ++n )
					consider( face.m_Points[ 0 ], face.m_Points[ n ], face.m_Points[ n + 1 ] );
		}
	}
	return best;
}

//-----------------------------------------------------------------------------
// Purpose: Change only the dragged corner, retaining tiling, other corners and assignments.
//-----------------------------------------------------------------------------
Entity_t MoveOverlayCorner( const CScene &scene, const Entity_t &entity, int nCorner, Point_t p )
{
	Entity_t out = entity;
	const auto at = OverlaySurfacePoint( scene, entity, p );
	const auto origin = VectorOf( entity.GetValue( "BasisOrigin" ) );
	const auto u = VectorOf( entity.GetValue( "BasisU" ) );
	const auto v = VectorOf( entity.GetValue( "BasisV" ) );
	if ( at && origin && u && v && nCorner >= 0 && nCorner < 4 )
	{
		const Point_t delta = Subtract( *at, *origin );
		SetKeyValue( scene.m_Map, out, "uv" + std::to_string( nCorner ), FormatPoint( { Dot( delta, Normalized( *u ) ), Dot( delta, Normalized( *v ) ), 0 } ) );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Properties' Pick replaces a list, Ctrl toggles a face, Shift-placement adds one.
//-----------------------------------------------------------------------------
void AssignOverlayFace( const CMap &map, Entity_t &entity, std::string_view szKey, int nSide, bool bToggle, bool bReplace )
{
	std::vector<int> sides;
	CScanner scanner( entity.GetValue( szKey ) );
	if ( !bReplace )
		while ( const auto n = scanner.Int() )
			if ( std::ranges::find( sides, *n ) == sides.end() )
				sides.push_back( *n );
	const auto found = std::ranges::find( sides, nSide );
	if ( found != sides.end() && bToggle )
		sides.erase( found );
	else if ( found == sides.end() )
		sides.push_back( nSide );
	std::string szSides;
	for ( const int n : sides )
	{
		if ( !szSides.empty() )
			szSides += ' ';
		szSides += std::to_string( n );
	}
	SetKeyValue( map, entity, szKey, szSides );
}

} // namespace xv::map
