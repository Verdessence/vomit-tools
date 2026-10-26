//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Independent brush construction and VMF writing from local VDC facts and VMF samples.
//===========================================================================//
#include "Vmf.h"
#include "Mdl.h"
#include "kv1/Kv1.h"
#include "map/Geometry.h"
#include "material/Material.h"
#include "vfs/Vfs.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <locale>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <tuple>
namespace xv::model
{
namespace
{
using map::Add;
using map::Point_t;
using map::Scale;
using map::Subtract;
using Face_t = map::Polygon_t;
struct Plane_t
{
	Point_t m_Normal;
	double m_flDistance;
	std::array<Point_t, 3> m_Points;
};
constexpr double COORDINATE_LIMIT = 16384, EPSILON = 1e-7, PLANE_TOLERANCE = 0.01;
//-----------------------------------------------------------------------------
// Purpose: Keeps brush calculations in double precision after decoding the model's floats.
//-----------------------------------------------------------------------------
Point_t Point( CVec3 vec )
{
	return { vec.m_flX, vec.m_flY, vec.m_flZ };
}
//-----------------------------------------------------------------------------
// Purpose: Rejects unrepresentable positions before geometric operations.
//-----------------------------------------------------------------------------
void CheckPoint( Point_t p )
{
	for ( double flValue : p )
		if ( !std::isfinite( flValue ) || std::abs( flValue ) >= COORDINATE_LIMIT )
			throw std::runtime_error( "coordinate outside VMF export bounds" );
}
//-----------------------------------------------------------------------------
// Purpose: Enforces finite options and explicit work limits for untrusted inputs.
//-----------------------------------------------------------------------------
void CheckOptions( const VmfOptions_t &options )
{
	if ( !std::isfinite( options.m_flGrid ) || options.m_flGrid < 0 || options.m_flGrid > 1024 ||
		!std::isfinite( options.m_flConcavity ) || options.m_flConcavity < 0.01f || options.m_flConcavity > 1 ||
		options.m_nMaxSides < 4 || options.m_nMaxSides > 128 || options.m_nMaxHulls < 1 || options.m_nMaxHulls > 1024 ||
		options.m_nMaxVerticesPerHull < 4 || options.m_nMaxVerticesPerHull > 64 ||
		options.m_nVoxelResolution < 100000 || options.m_nVoxelResolution > 8000000 ||
		options.m_Selection.m_nSkin < 0 || options.m_Selection.m_nLod < 0 || options.m_Selection.m_Bodygroups.size() > 1024 ||
		std::ranges::any_of( options.m_Selection.m_Bodygroups, []( int nModel ) { return nModel < 0; } ) ||
		static_cast<unsigned>( options.m_nHullSource ) > 3 || static_cast<unsigned>( options.m_nTexturing ) > 2 )
		throw std::runtime_error( "Invalid VMF export options" );
	CheckPoint( Point( options.m_vecOffset ) );
}
//-----------------------------------------------------------------------------
// Purpose: Serializes enough digits to recover double precision independently of user locale.
//-----------------------------------------------------------------------------
std::string Text( Point_t p )
{
	std::ostringstream out;
	out.imbue( std::locale::classic() );
	out << std::setprecision( 17 ) << p[ 0 ] << ' ' << p[ 1 ] << ' ' << p[ 2 ];
	return out.str();
}
//-----------------------------------------------------------------------------
// Purpose: Constructs an outward plane, rejecting collinear triangle points.
//-----------------------------------------------------------------------------
Plane_t Plane( std::array<Point_t, 3> points, Point_t vecInterior )
{
	auto vecNormal = map::Cross( Subtract( points[ 1 ], points[ 0 ] ), Subtract( points[ 2 ], points[ 0 ] ) );
	const double flLength = std::sqrt( map::Dot( vecNormal, vecNormal ) );
	if ( flLength < EPSILON )
		throw std::runtime_error( "degenerate plane" );
	vecNormal = Scale( vecNormal, 1 / flLength );
	double flDistance = map::Dot( vecNormal, points[ 0 ] );
	if ( map::Dot( vecNormal, vecInterior ) > flDistance )
	{
		vecNormal = Scale( vecNormal, -1 );
		flDistance = -flDistance;
		std::swap( points[ 1 ], points[ 2 ] );
	}
	return { vecNormal, flDistance, points };
}
//-----------------------------------------------------------------------------
// Purpose: Intersects supporting half-spaces by polygon clipping, detecting unbounded results.
//-----------------------------------------------------------------------------
std::vector<Face_t> Intersect( const std::vector<Plane_t> &planes )
{
	std::vector<map::Plane_t> halfSpaces;
	for ( const Plane_t &plane : planes )
		halfSpaces.push_back( { plane.m_Normal, plane.m_flDistance } );
	std::string szError;
	auto faces = map::IntersectHalfSpaces( halfSpaces, COORDINATE_LIMIT * 2, EPSILON, &szError );
	if ( !faces )
		throw std::runtime_error( szError );
	return std::move( *faces );
}
//-----------------------------------------------------------------------------
// Purpose: Converts one hull into bounded supporting planes and validated face polygons.
//-----------------------------------------------------------------------------
std::pair<std::vector<Plane_t>, std::vector<Face_t>> Brush( const CModel &model, const Convex_t &convex, const VmfOptions_t &options )
{
	if ( convex.m_Indices.size() < 12 || convex.m_Indices.size() % 3 || convex.m_Indices.size() > 12288 )
		throw std::runtime_error( "invalid or oversized hull triangles" );
	if ( convex.m_nBone < -1 || convex.m_nBone >= static_cast<int>( model.m_Bones.size() ) )
		throw std::runtime_error( "invalid hull bone" );
	std::map<std::uint32_t, Point_t> vertices;
	Point_t vecCenter{};
	for ( auto nIndex : convex.m_Indices )
	{
		if ( nIndex >= convex.m_Vertices.size() )
			throw std::runtime_error( "invalid hull index" );
		if ( vertices.contains( nIndex ) )
			continue;
		auto p = convex.m_Vertices[ nIndex ];
		if ( convex.m_nBone >= 0 )
			p = model.m_Bones[ convex.m_nBone ].m_ToModel.Apply( p );
		const auto at = Add( Point( p ), Point( options.m_vecOffset ) );
		CheckPoint( at );
		vertices.emplace( nIndex, at );
		vecCenter = Add( vecCenter, at );
	}
	if ( vertices.size() < 4 || vertices.size() > 2048 )
		throw std::runtime_error( "invalid or oversized hull vertices" );
	vecCenter = Scale( vecCenter, 1.0 / vertices.size() );
	std::vector<Plane_t> planes;
	for ( std::size_t i = 0; i < convex.m_Indices.size(); i += 3 )
	{
		std::array<Point_t, 3> points{ vertices.at( convex.m_Indices[ i ] ), vertices.at( convex.m_Indices[ i + 1 ] ), vertices.at( convex.m_Indices[ i + 2 ] ) };
		const auto vecArea = map::Cross( Subtract( points[ 1 ], points[ 0 ] ), Subtract( points[ 2 ], points[ 0 ] ) );
		if ( map::Dot( vecArea, vecArea ) < EPSILON * EPSILON )
			continue;
		auto plane = Plane( points, vecCenter );
		for ( const auto &[ nIndex, p ] : vertices )
			if ( map::Dot( plane.m_Normal, p ) - plane.m_flDistance > PLANE_TOLERANCE )
				throw std::runtime_error( "non-convex hull" );
		if ( std::ranges::any_of( planes, [ & ]( const auto &other ) { return map::Dot( plane.m_Normal, other.m_Normal ) > 1 - 1e-8 && std::abs( plane.m_flDistance - other.m_flDistance ) < PLANE_TOLERANCE; } ) )
			continue;
		planes.push_back( plane );
		if ( planes.size() > static_cast<std::size_t>( options.m_nMaxSides ) )
			throw std::runtime_error( "side limit" );
	}
	if ( planes.size() < 4 )
		throw std::runtime_error( "too few planes" );
	const auto original = planes;
	if ( options.m_flGrid > 0 )
		for ( auto &plane : planes )
		{
			auto points = plane.m_Points;
			for ( auto &p : points )
				for ( double &flValue : p )
					flValue = std::round( flValue / options.m_flGrid ) * options.m_flGrid;
			plane = Plane( points, vecCenter );
		}
	std::vector<Plane_t> distinct;
	for ( const auto &plane : planes )
		if ( std::ranges::none_of( distinct, [ & ]( const auto &other ) { return map::Dot( plane.m_Normal, other.m_Normal ) > 1 - 1e-12 && std::abs( plane.m_flDistance - other.m_flDistance ) < EPSILON; } ) )
			distinct.push_back( plane );
	planes = std::move( distinct );
	auto faces = Intersect( planes );
	const double flError = std::max( PLANE_TOLERANCE, options.m_flGrid * 1.75 );
	double flVolume = 0;
	for ( const auto &face : faces )
	{
		const auto &plane = planes[ face.m_nPlane ];
		Point_t vecArea{};
		for ( std::size_t i = 1; i + 1 < face.m_Points.size(); i++ )
		{
			const auto a = Subtract( face.m_Points[ i ], face.m_Points[ 0 ] ), b = Subtract( face.m_Points[ i + 1 ], face.m_Points[ 0 ] );
			vecArea = Add( vecArea, map::Cross( a, b ) );
			flVolume += std::abs( map::Dot( Subtract( face.m_Points[ 0 ], vecCenter ), map::Cross( a, b ) ) ) / 6;
		}
		if ( map::Dot( vecArea, vecArea ) < 1e-10 )
			throw std::runtime_error( "zero-area face" );
		double flWidth = 0;
		for ( const auto &other : faces )
			for ( auto p : other.m_Points )
			{
				CheckPoint( p );
				flWidth = std::max( flWidth, plane.m_flDistance - map::Dot( plane.m_Normal, p ) );
				if ( map::Dot( plane.m_Normal, p ) - plane.m_flDistance > PLANE_TOLERANCE )
					throw std::runtime_error( "inconsistent half-spaces" );
			}
		const bool bFine = options.m_nHullSource == VmfOptions_t::HullSource_t::HULL_SOURCE_CUTOUT || options.m_nHullSource == VmfOptions_t::HullSource_t::HULL_SOURCE_DECOMPOSE;
		if ( flWidth < ( bFine ? 0.001 : 0.125 ) )
			throw std::runtime_error( bFine ? "too thin (below 0.001 unit)" : "too thin (below 1/8 unit)" );
		for ( auto p : face.m_Points )
			for ( const auto &other : original )
				if ( map::Dot( other.m_Normal, p ) - other.m_flDistance > flError )
					throw std::runtime_error( "excess reconstruction error" );
	}
	for ( const auto &[ nIndex, p ] : vertices )
		for ( const auto &plane : planes )
			if ( map::Dot( plane.m_Normal, p ) - plane.m_flDistance > flError )
				throw std::runtime_error( "lost hull coverage" );
	if ( flVolume < 1e-5 )
		throw std::runtime_error( "zero-volume hull" );
	return { std::move( planes ), std::move( faces ) };
}
struct MaterialTriangle_t
{
	Point_t m_Normal, m_Center;
	int m_nMaterial;
};
//-----------------------------------------------------------------------------
// Purpose: Prepares material matching once, keeping mesh work bounded across many brushes.
//-----------------------------------------------------------------------------
std::vector<MaterialTriangle_t> MaterialTriangles( const CModel &model )
{
	std::vector<MaterialTriangle_t> triangles;
	for ( const auto &mesh : model.m_Meshes )
	{
		if ( mesh.m_Indices.size() % 3 )
			throw std::runtime_error( "Invalid render triangle list" );
		for ( std::size_t i = 0; i < mesh.m_Indices.size(); i += 3 )
		{
			Point_t p[ 3 ];
			for ( int j = 0; j < 3; j++ )
			{
				if ( mesh.m_Indices[ i + j ] >= model.m_Vertices.size() )
					throw std::runtime_error( "Invalid render triangle index" );
				p[ j ] = Point( model.m_Vertices[ mesh.m_Indices[ i + j ] ].m_vecPosition );
				CheckPoint( p[ j ] );
			}
			// Source render indices are clockwise, as in the existing OBJ/glTF exporters.
			const auto vecNormal = map::Cross( Subtract( p[ 2 ], p[ 0 ] ), Subtract( p[ 1 ], p[ 0 ] ) );
			const double flLength = std::sqrt( map::Dot( vecNormal, vecNormal ) );
			if ( flLength > EPSILON )
				triangles.push_back( { Scale( vecNormal, 1 / flLength ), Scale( Add( Add( p[ 0 ], p[ 1 ] ), p[ 2 ] ), 1.0 / 3 ), mesh.m_nMaterial } );
			if ( triangles.size() > 200000 )
				throw std::runtime_error( "Render mesh exceeds material matching limit" );
		}
	}
	return triangles;
}
//-----------------------------------------------------------------------------
// Purpose: Makes a KV pair without allowing untrusted names to inject VMF syntax.
//-----------------------------------------------------------------------------
void Pair( kv1::CNode &node, std::string_view szKey, std::string_view szValue )
{
	if ( szValue.find_first_of( "\"\r\n" ) != std::string_view::npos )
		throw std::runtime_error( "Unsafe VMF property" );
	node.AppendChild( kv1::CNode::MakePair( szKey, szValue ) );
}
} // namespace
//-----------------------------------------------------------------------------
// Purpose: Writes only validated brushes, retaining explicit Hammer++ polygons when requested.
//-----------------------------------------------------------------------------
Export_t ExportVmf( const CModel &model, std::span<const Convex_t> convexes, const Reader_t &pfnRead, const VmfOptions_t &options, std::string_view szReferenceModel )
{
	CheckOptions( options );
	if ( convexes.size() > 8192 )
		throw std::runtime_error( "Too many input hulls" );
	Export_t result;
	kv1::CDocument document;
	auto version = kv1::CNode::MakeBlock( "versioninfo" );
	Pair( version, "editorversion", "400" );
	Pair( version, "editorbuild", "0" );
	Pair( version, "mapversion", "1" );
	Pair( version, "formatversion", "100" );
	Pair( version, "prefab", "0" );
	document.AppendRoot( std::move( version ) );
	document.AppendRoot( kv1::CNode::MakeBlock( "visgroups" ) );
	document.AppendRoot( kv1::CNode::MakeBlock( "viewsettings" ) );
	auto world = kv1::CNode::MakeBlock( "world" ), detail = kv1::CNode::MakeBlock( "entity" );
	Pair( world, "id", "1" );
	Pair( world, "classname", "worldspawn" );
	Pair( world, "mapversion", "1" );
	Pair( detail, "id", "2" );
	Pair( detail, "classname", "func_detail" );
	int nId = 3, nBrushes = 0, nSides = 0, nReplaced = 0;
	std::map<std::string, int> drops;
	std::vector<std::string> materials;
	std::vector<MaterialTriangle_t> triangles;
	if ( options.m_nTexturing == VmfOptions_t::Texturing_t::TEXTURING_MODEL )
	{
		triangles = MaterialTriangles( model );
		if ( static_cast<std::uint64_t>( triangles.size() ) * convexes.size() * options.m_nMaxSides > 100000000 )
			throw std::runtime_error( "Material matching exceeds export work limit; use nodraw or developer materials" );
		for ( auto szName : model.m_Materials )
		{
			szName = vfs::NormalizePath( szName );
			if ( szName.ends_with( ".vmt" ) )
				szName.resize( szName.size() - 4 );
			if ( szName.starts_with( "materials/" ) )
				szName.erase( 0, 10 );
			bool bCompatible = false;
			if ( IsExportPath( szName ) && pfnRead )
				if ( const auto bytes = pfnRead( "materials/" + szName + ".vmt" ) )
					if ( const auto material = material::ParseMaterial( std::string_view( reinterpret_cast<const char *>( bytes->data() ), bytes->size() ), pfnRead ) )
						bCompatible = material->GetShaderKind() == "lightmappedgeneric";
			if ( !IsExportPath( szName ) || ( !bCompatible && options.m_bNodrawIncompatibleMaterials ) )
				szName = "tools/toolsnodraw";
			materials.push_back( std::move( szName ) );
		}
		result.m_Notes.push_back( "Model materials use approximate planar projection; model UVs and lighting are not preserved." );
	}
	for ( const auto &convex : convexes )
	{
		std::vector<Plane_t> planes;
		std::vector<Face_t> faces;
		try
		{
			std::tie( planes, faces ) = Brush( model, convex, options );
		}
		catch ( const std::runtime_error &e )
		{
			drops[ e.what() ]++;
			continue;
		}
		if ( nSides + faces.size() > 30000 )
		{
			drops[ "total side budget" ]++;
			continue;
		}
		auto solid = kv1::CNode::MakeBlock( "solid" );
		Pair( solid, "id", std::to_string( nId++ ) );
		for ( const auto &face : faces )
		{
			const auto &plane = planes[ face.m_nPlane ];
			auto side = kv1::CNode::MakeBlock( "side" );
			Pair( side, "id", std::to_string( nId++ ) );
			// VMF's box sample faces outward with the reverse of the usual triangle cross product.
			Pair( side, "plane", "(" + Text( plane.m_Points[ 0 ] ) + ") (" + Text( plane.m_Points[ 2 ] ) + ") (" + Text( plane.m_Points[ 1 ] ) + ")" );
			if ( options.m_bHammerPlusPlusVertices )
			{
				auto polygon = kv1::CNode::MakeBlock( "vertices_plus" );
				for ( auto it = face.m_Points.rbegin(); it != face.m_Points.rend(); ++it )
					Pair( polygon, "v", Text( *it ) );
				side.AppendChild( std::move( polygon ) );
			}
			std::string szMaterial = options.m_nTexturing == VmfOptions_t::Texturing_t::TEXTURING_DEV ? "dev/dev_measuregeneric01" : "tools/toolsnodraw";
			if ( options.m_nTexturing == VmfOptions_t::Texturing_t::TEXTURING_MODEL )
			{
				double flBest = std::numeric_limits<double>::infinity();
				for ( const auto &triangle : triangles )
				{
					const double flAlignment = map::Dot( triangle.m_Normal, plane.m_Normal );
					if ( flAlignment < 0.5 )
						continue;
					const double flScore = std::abs( map::Dot( plane.m_Normal, Add( triangle.m_Center, Point( options.m_vecOffset ) ) ) - plane.m_flDistance ) + ( 1 - flAlignment ) * 16;
					if ( flScore < flBest && triangle.m_nMaterial >= 0 && triangle.m_nMaterial < static_cast<int>( materials.size() ) )
					{
						flBest = flScore;
						szMaterial = materials[ triangle.m_nMaterial ];
					}
				}
				if ( szMaterial == "tools/toolsnodraw" )
					nReplaced++;
			}
			Pair( side, "material", szMaterial );
			const auto &n = plane.m_Normal;
			const bool bZ = std::abs( n[ 2 ] ) >= std::max( std::abs( n[ 0 ] ), std::abs( n[ 1 ] ) );
			const bool bX = std::abs( n[ 0 ] ) >= std::abs( n[ 1 ] );
			Pair( side, "uaxis", bZ || !bX ? "[1 0 0 0] 0.25" : "[0 1 0 0] 0.25" );
			Pair( side, "vaxis", bZ ? "[0 -1 0 0] 0.25" : "[0 0 -1 0] 0.25" );
			Pair( side, "rotation", "0" );
			Pair( side, "lightmapscale", "16" );
			Pair( side, "smoothing_groups", "0" );
			solid.AppendChild( std::move( side ) );
		}
		( options.m_bDetail ? detail : world ).AppendChild( std::move( solid ) );
		nSides += static_cast<int>( faces.size() );
		nBrushes++;
	}
	const auto nDropped = convexes.size() - nBrushes;
	std::string szSummary = std::to_string( nBrushes ) + ( nBrushes == 1 ? " brush, " : " brushes, " ) + std::to_string( nDropped ) + ( nDropped == 1 ? " hull dropped" : " hulls dropped" );
	for ( const auto &[ szReason, nCount ] : drops )
		szSummary += "; " + std::to_string( nCount ) + " " + szReason;
	if ( !nBrushes )
		throw std::runtime_error( szSummary );
	result.m_Notes.push_back( szSummary );
	if ( nReplaced )
		result.m_Notes.push_back( std::to_string( nReplaced ) + " sides use nodraw in place of missing or incompatible model materials." );
	document.AppendRoot( std::move( world ) );
	if ( options.m_bDetail )
		document.AppendRoot( std::move( detail ) );
	if ( options.m_bReferenceProp && !szReferenceModel.empty() )
	{
		if ( !IsExportPath( szReferenceModel ) || !szReferenceModel.starts_with( "models/" ) || !szReferenceModel.ends_with( ".mdl" ) )
			result.m_Notes.push_back( "Reference prop omitted: model has no portable models/ path." );
		else if ( std::ranges::any_of( options.m_Selection.m_Bodygroups, []( int nModel ) { return nModel != 0; } ) )
			result.m_Notes.push_back( "Reference prop omitted: non-default bodygroups cannot be represented by this static reference." );
		else
		{
			auto prop = kv1::CNode::MakeBlock( "entity" );
			Pair( prop, "id", std::to_string( nId++ ) );
			Pair( prop, "classname", "prop_static" );
			Pair( prop, "model", szReferenceModel );
			Pair( prop, "origin", Text( Point( options.m_vecOffset ) ) );
			Pair( prop, "angles", "0 0 0" );
			Pair( prop, "solid", "0" );
			Pair( prop, "skin", std::to_string( options.m_Selection.m_nSkin ) );
			document.AppendRoot( std::move( prop ) );
		}
	}
	const auto szText = kv1::Write( document );
	result.m_Files[ "model.vmf" ] = { reinterpret_cast<const std::byte *>( szText.data() ), reinterpret_cast<const std::byte *>( szText.data() + szText.size() ) };
	return result;
}
//-----------------------------------------------------------------------------
// Purpose: Selects checked physics hulls or render decomposition without masking corrupt PHY data.
//-----------------------------------------------------------------------------
Export_t ExportVmf( std::string_view szPath, const Reader_t &pfnRead, const VmfOptions_t &options )
{
	CheckOptions( options );
	if ( !pfnRead || !IsExportPath( szPath ) || !szPath.ends_with( ".mdl" ) )
		throw std::runtime_error( "Choose a relative MDL path" );
	std::string szError;
	const auto studio = ReadStudio( szPath, pfnRead, &szError );
	if ( !studio )
		throw std::runtime_error( szError );
	std::vector<Convex_t> convexes;
	std::vector<std::string> notes;
	const bool bAuto = options.m_nHullSource == VmfOptions_t::HullSource_t::HULL_SOURCE_AUTO;
	const bool bPhysics = options.m_nHullSource == VmfOptions_t::HullSource_t::HULL_SOURCE_PHYSICS;
	const bool bExact = options.m_nHullSource == VmfOptions_t::HullSource_t::HULL_SOURCE_CUTOUT;
	std::optional<CModel> model;
	if ( !bPhysics || options.m_nTexturing == VmfOptions_t::Texturing_t::TEXTURING_MODEL )
		model = LoadModel( szPath, pfnRead, &szError, options.m_Selection );
	bool bCutout = false, bRender = false;
	if ( model && !bPhysics )
	{
		if ( bAuto || bExact )
			if ( auto cutout = ExtractCutoutHulls( *model, options, &szError ) )
			{
				convexes = std::move( *cutout );
				bCutout = true;
			}
		if ( bExact && !bCutout )
			throw std::runtime_error( szError );
		if ( !bCutout )
			convexes = DecomposeModel( *model, options, &notes );
		bRender = true;
	}
	else
	{
		if ( !bAuto && !bPhysics )
			throw std::runtime_error( szError );
		const auto bytes = pfnRead( std::string( szPath.substr( 0, szPath.size() - 4 ) ) + ".phy" );
		if ( !bytes )
			throw std::runtime_error( bPhysics ? "Model has no PHY collision hulls" : szError );
		const auto physics = ReadPhysics( *bytes, &szError );
		if ( !physics )
			throw std::runtime_error( szError );
		if ( physics->m_nChecksum != studio->m_nChecksum )
			throw std::runtime_error( "PHY checksum does not match MDL" );
		convexes = physics->m_Convexes;
		if ( !model )
		{
			if ( options.m_nTexturing == VmfOptions_t::Texturing_t::TEXTURING_MODEL )
				throw std::runtime_error( szError );
			model.emplace();
			for ( const auto &bone : studio->m_Bones )
			{
				const auto transform = bone.m_PoseToBone.Inverse();
				if ( !transform )
					throw std::runtime_error( "Invalid reference bone transform" );
				model->m_Bones.push_back( { bone.m_szName, bone.m_nParent, *transform } );
			}
		}
		notes.push_back( bAuto ? "Render companions unavailable: exported physics collision hulls. These may differ substantially from the visible model." : "Physics collision hulls exported; these may differ substantially from the visible model." );
	}
	const bool bStatic = CanUseStaticReference( *studio );
	auto brushOptions = options;
	brushOptions.m_nHullSource = bCutout ? VmfOptions_t::HullSource_t::HULL_SOURCE_CUTOUT : bRender ? VmfOptions_t::HullSource_t::HULL_SOURCE_DECOMPOSE
																									: VmfOptions_t::HullSource_t::HULL_SOURCE_PHYSICS;
	auto result = ExportVmf( *model, convexes, pfnRead, brushOptions, bStatic ? szPath : std::string_view{} );
	result.m_Notes.insert( result.m_Notes.end(), notes.begin(), notes.end() );
	if ( bCutout )
		result.m_Notes.push_back( "Cutout brushes follow the render caps directly, retaining their outline and holes; any omitted pieces are reported above." );
	if ( options.m_bReferenceProp && !bStatic )
		result.m_Notes.push_back( "Reference prop omitted: model is not eligible for prop_static." );
	return result;
}
//-----------------------------------------------------------------------------
// Purpose: Avoids static references that compilers remove because of bones or prop_data rules.
//-----------------------------------------------------------------------------
bool CanUseStaticReference( const CStudioFile &studio )
{
	if ( !( studio.m_nFlags & 16 ) || studio.m_Bones.size() != 1 )
		return false;
	const auto parsed = kv1::Parse( studio.m_szKeyValues );
	if ( parsed.HasErrors() )
		return false;
	const auto *pRoot = parsed.m_Document.Find( "mdlkeyvalue" );
	const auto *pData = pRoot ? pRoot->Find( "prop_data" ) : parsed.m_Document.Find( "prop_data" );
	if ( !pData )
		return true;
	const auto *pAllow = pData->Find( "allowstatic" );
	return pAllow && pAllow->GetValue() == "1";
}
} // namespace xv::model
