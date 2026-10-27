//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Reading func_instances' files and placing what they hold.
//
//===========================================================================//
#include "map/Instance.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>

#include "kv1/Kv1.h"
#include "map/GameConfig.h"
#include "map/Scene.h"
#include "map/Vmf.h"

namespace xv::map
{

// What the instances of one map share: each file's scene, once per set of replacements, the
// files being read (to stop one that comes back into itself), and how much has been read.
class CInstanceCache
{
public:
	std::map<std::pair<std::string, std::string>, std::shared_ptr<const CScene>> m_Scenes;
	std::vector<std::filesystem::path> m_Reading;
	std::size_t m_nInstances = 0;
	std::uintmax_t m_nBytes = 0;
};

namespace
{

constexpr double DEGREES = 3.14159265358979323846 / 180.0;

//-----------------------------------------------------------------------------
// Purpose: A func_instance's replacements ("replace01" to "replace10", each "$name value"),
//          longest name first so "$colour" doesn't eat "$colours".
//-----------------------------------------------------------------------------
std::vector<std::pair<std::string, std::string>> Replacements( const Entity_t &entity )
{
	std::vector<std::pair<std::string, std::string>> out;
	for ( int i = 1; i <= 10; i++ )
	{
		const std::string szKey = "replace" + std::string( i < 10 ? "0" : "" ) + std::to_string( i );
		const std::string_view szValue = entity.GetValue( szKey );
		const std::size_t nSpace = szValue.find( ' ' );
		if ( !szValue.starts_with( '$' ) || nSpace == std::string_view::npos )
		{
			continue;
		}
		out.emplace_back( std::string{ szValue.substr( 0, nSpace ) }, std::string{ szValue.substr( nSpace + 1 ) } );
	}
	std::ranges::sort( out, []( const auto &a, const auto &b ) { return a.first.size() != b.first.size() ? a.first.size() > b.first.size() : a.first < b.first; } );
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: The text with each $name swapped for its value. VBSP swaps them in entities' keys,
//          where they are used; nothing else in a map spells one.
//-----------------------------------------------------------------------------
void Replace( std::vector<char> &text, const std::vector<std::pair<std::string, std::string>> &replacements )
{
	if ( replacements.empty() )
	{
		return;
	}
	std::string sz( text.begin(), text.end() );
	for ( const auto &[ szName, szValue ] : replacements )
	{
		for ( std::size_t nAt = sz.find( szName ); nAt != std::string::npos; nAt = sz.find( szName, nAt + szValue.size() ) )
		{
			sz.replace( nAt, szName.size(), szValue );
		}
	}
	text.assign( sz.begin(), sz.end() );
}

//-----------------------------------------------------------------------------
// Purpose: The box around box once placed.
//-----------------------------------------------------------------------------
Box_t PlaceBox( const Box_t &box, const Transform_t &transform )
{
	Box_t out;
	if ( box.IsEmpty() )
	{
		return out;
	}
	for ( int i = 0; i < 8; i++ )
	{
		out.Add( transform.Apply( { ( i & 1 ) ? box.m_Maxs[ 0 ] : box.m_Mins[ 0 ], ( i & 2 ) ? box.m_Maxs[ 1 ] : box.m_Mins[ 1 ], ( i & 4 ) ? box.m_Maxs[ 2 ] : box.m_Mins[ 2 ] } ) );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Every instance's solids (and its instances'), placed, as the func_instance's object,
//          with materials as the scene's table names them (adding those only instances use).
//-----------------------------------------------------------------------------
void BuildInstanceMesh( CScene &scene )
{
	std::unordered_map<std::string, std::uint32_t> indexOf;
	for ( std::uint32_t i = 0; i < scene.m_Materials.size(); i++ )
	{
		indexOf.try_emplace( scene.m_Materials[ i ], i );
	}
	struct Triangle_t
	{
		std::uint64_t m_nKey = 0;
		std::array<std::uint32_t, 3> m_Indices{};
	};
	std::vector<Triangle_t> triangles;
	CMapMesh out;
	for ( const PlacedInstance_t &placed : scene.m_Instances )
	{
		const CScene &inner = *placed.m_pScene;
		std::vector<std::uint32_t> remap( inner.m_Materials.size() );
		for ( std::size_t i = 0; i < inner.m_Materials.size(); i++ )
		{
			const auto [ it, bAdded ] = indexOf.try_emplace( inner.m_Materials[ i ], static_cast<std::uint32_t>( scene.m_Materials.size() ) );
			if ( bAdded )
			{
				scene.m_Materials.push_back( inner.m_Materials[ i ] );
			}
			remap[ i ] = it->second;
		}
		for ( const CMapMesh *pMesh : { &inner.GetMesh(), &inner.GetInstanceMesh() } )
		{
			const auto nBase = static_cast<std::uint32_t>( out.m_Vertices.size() );
			const auto nChunkBase = static_cast<std::uint32_t>( out.m_Chunks.size() );
			for ( const MeshVertex_t &source : pMesh->m_Vertices )
			{
				MeshVertex_t vertex = source;
				vertex.m_vecPosition = ToVec3( placed.m_ToParent.Apply( ToPoint( source.m_vecPosition ) ) );
				vertex.m_vecNormal = ToVec3( Normalized( placed.m_ToParent.Rotate( ToPoint( source.m_vecNormal ) ) ) );
				vertex.m_nObject = placed.m_nObject;
				out.m_Vertices.push_back( vertex );
			}
			for ( const MeshChunk_t &chunk : pMesh->m_Chunks )
			{
				out.m_Chunks.push_back( { PlaceBox( chunk.m_Bounds, placed.m_ToParent ) } );
			}
			for ( const MeshBatch_t &batch : pMesh->m_Batches )
			{
				const std::uint32_t nMaterial = batch.m_nMaterial < remap.size() ? remap[ batch.m_nMaterial ] : batch.m_nMaterial;
				const std::uint32_t nChunk = batch.m_nChunk == ~0u ? ~0u : nChunkBase + batch.m_nChunk;
				for ( std::uint32_t i = 0; i + 2 < batch.m_nIndexCount; i += 3 )
				{
					const std::uint32_t *pIndices = &pMesh->m_Indices[ batch.m_nFirstIndex + i ];
					triangles.push_back( { ( static_cast<std::uint64_t>( nMaterial ) << 32 ) | nChunk, { nBase + pIndices[ 0 ], nBase + pIndices[ 1 ], nBase + pIndices[ 2 ] } } );
				}
			}
			for ( const std::uint32_t n : pMesh->m_Lines )
			{
				out.m_Lines.push_back( nBase + n );
			}
		}
	}
	std::ranges::stable_sort( triangles, {}, &Triangle_t::m_nKey );
	for ( const Triangle_t &triangle : triangles )
	{
		const auto nMaterial = static_cast<std::uint32_t>( triangle.m_nKey >> 32 );
		const auto nChunk = static_cast<std::uint32_t>( triangle.m_nKey & 0xFFFFFFFFu );
		if ( out.m_Batches.empty() || out.m_Batches.back().m_nMaterial != nMaterial || out.m_Batches.back().m_nChunk != nChunk )
		{
			out.m_Batches.push_back( { nMaterial, nChunk, static_cast<std::uint32_t>( out.m_Indices.size() ), 0 } );
		}
		out.m_Indices.insert( out.m_Indices.end(), triangle.m_Indices.begin(), triangle.m_Indices.end() );
		out.m_Batches.back().m_nIndexCount += 3;
	}
	scene.m_pInstanceMesh = std::make_shared<const CMapMesh>( std::move( out ) );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Transform_t Transform_t::FromAngles( Point_t origin, const std::array<double, 3> &angles, double flScale )
{
	const double cp = std::cos( angles[ 0 ] * DEGREES ), sp = std::sin( angles[ 0 ] * DEGREES );
	const double cy = std::cos( angles[ 1 ] * DEGREES ), sy = std::sin( angles[ 1 ] * DEGREES );
	const double cr = std::cos( angles[ 2 ] * DEGREES ), sr = std::sin( angles[ 2 ] * DEGREES );
	// Yaw, then pitch, then roll, as model::CTransform::Rotation turns them.
	Transform_t yaw;
	yaw.m_Rotation = { cy, -sy, 0, sy, cy, 0, 0, 0, 1 };
	Transform_t pitch;
	pitch.m_Rotation = { cp, 0, sp, 0, 1, 0, -sp, 0, cp };
	Transform_t roll;
	roll.m_Rotation = { 1, 0, 0, 0, cr, -sr, 0, sr, cr };
	Transform_t out = yaw * pitch * roll;
	for ( double &fl : out.m_Rotation )
	{
		fl *= flScale;
	}
	out.m_Offset = origin;
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Point_t Transform_t::Rotate( Point_t v ) const
{
	const auto &m = m_Rotation;
	return { m[ 0 ] * v[ 0 ] + m[ 1 ] * v[ 1 ] + m[ 2 ] * v[ 2 ], m[ 3 ] * v[ 0 ] + m[ 4 ] * v[ 1 ] + m[ 5 ] * v[ 2 ], m[ 6 ] * v[ 0 ] + m[ 7 ] * v[ 1 ] + m[ 8 ] * v[ 2 ] };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Point_t Transform_t::Apply( Point_t p ) const
{
	return Add( Rotate( p ), m_Offset );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Transform_t Transform_t::operator*( const Transform_t &other ) const
{
	Transform_t out;
	for ( std::size_t r = 0; r < 3; r++ )
	{
		for ( std::size_t c = 0; c < 3; c++ )
		{
			out.m_Rotation[ r * 3 + c ] = m_Rotation[ r * 3 ] * other.m_Rotation[ c ] + m_Rotation[ r * 3 + 1 ] * other.m_Rotation[ 3 + c ] + m_Rotation[ r * 3 + 2 ] * other.m_Rotation[ 6 + c ];
		}
	}
	out.m_Offset = Apply( other.m_Offset );
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void ForEachInstance( const CScene &scene, const InstanceVisitor_t &pfnVisit )
{
	const auto visit = [ & ]( auto &self, const CScene &inner, const Transform_t &toScene, std::optional<std::uint32_t> nObject ) -> void {
		pfnVisit( inner, toScene, nObject );
		for ( const PlacedInstance_t &placed : inner.m_Instances )
		{
			self( self, *placed.m_pScene, toScene * placed.m_ToParent, nObject ? nObject : std::optional<std::uint32_t>{ placed.m_nObject } );
		}
	};
	visit( visit, scene, Transform_t{}, std::nullopt );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void AppendPlaced( CMapMesh &out, const CMapMesh &in, const Transform_t &transform, std::optional<std::uint32_t> nObject )
{
	const auto nBase = static_cast<std::uint32_t>( out.m_Vertices.size() );
	const auto nFirstIndex = static_cast<std::uint32_t>( out.m_Indices.size() );
	const auto nChunkBase = static_cast<std::uint32_t>( out.m_Chunks.size() );
	for ( const MeshVertex_t &source : in.m_Vertices )
	{
		MeshVertex_t vertex = source;
		vertex.m_vecPosition = ToVec3( transform.Apply( ToPoint( source.m_vecPosition ) ) );
		vertex.m_vecNormal = ToVec3( Normalized( transform.Rotate( ToPoint( source.m_vecNormal ) ) ) );
		vertex.m_nObject = nObject.value_or( source.m_nObject );
		out.m_Vertices.push_back( vertex );
	}
	for ( const std::uint32_t n : in.m_Indices )
	{
		out.m_Indices.push_back( nBase + n );
	}
	for ( const std::uint32_t n : in.m_Lines )
	{
		out.m_Lines.push_back( nBase + n );
	}
	for ( const MeshChunk_t &chunk : in.m_Chunks )
	{
		out.m_Chunks.push_back( { PlaceBox( chunk.m_Bounds, transform ) } );
	}
	for ( MeshBatch_t batch : in.m_Batches )
	{
		batch.m_nFirstIndex += nFirstIndex;
		batch.m_nChunk = batch.m_nChunk == ~0u ? ~0u : nChunkBase + batch.m_nChunk;
		out.m_Batches.push_back( batch );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::filesystem::path> FindInstanceFile( std::string_view szFile, const InstanceOptions_t &options )
{
	std::string szName{ szFile };
	std::ranges::replace( szName, '\\', '/' );
	while ( !szName.empty() && ( szName.back() == ' ' || szName.back() == '\t' ) )
	{
		szName.pop_back();
	}
	if ( szName.empty() )
	{
		return std::nullopt;
	}
	std::vector<std::string> names;
	std::string szLower = szName;
	std::ranges::transform( szLower, szLower.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	if ( !szLower.ends_with( ".vmf" ) )
	{
		names.push_back( szName + ".vmf" );
	}
	names.push_back( szName );

	std::vector<std::filesystem::path> bases;
	const std::filesystem::path folder = options.m_MapFile.parent_path();
	bases.push_back( folder );
	for ( std::filesystem::path up = folder; up.has_relative_path() && up != up.parent_path(); up = up.parent_path() )
	{
		std::string szPart = up.filename().string();
		std::ranges::transform( szPart, szPart.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
		if ( szPart == "maps" )
		{
			bases.push_back( up );
			break;
		}
	}
	bases.insert( bases.end(), options.m_SearchPaths.begin(), options.m_SearchPaths.end() );
	for ( const std::filesystem::path &base : bases )
	{
		for ( const std::string &szTry : names )
		{
			const std::filesystem::path found = FindFileIgnoringCase( ( base / std::filesystem::path{ szTry } ).lexically_normal() );
			std::error_code error;
			if ( std::filesystem::is_regular_file( found, error ) )
			{
				const std::filesystem::path canonical = std::filesystem::weakly_canonical( found, error );
				return error ? found : canonical;
			}
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void LoadInstances( CScene &scene, const SceneOptions_t &options )
{
	const InstanceOptions_t &instances = options.m_Instances;
	if ( instances.m_MapFile.empty() )
	{
		return;
	}
	const std::shared_ptr<CInstanceCache> pCache = instances.m_pCache ? instances.m_pCache : std::make_shared<CInstanceCache>();
	std::error_code error;
	const std::filesystem::path self = std::filesystem::weakly_canonical( instances.m_MapFile, error );
	pCache->m_Reading.push_back( error ? instances.m_MapFile : self );
	auto problem = [ & ]( const Entity_t &entity, const std::string &szWhat ) {
		scene.m_Problems.push_back( "func_instance " + std::to_string( entity.m_nId ) + ": " + szWhat );
	};
	for ( std::size_t e = 0; e < scene.m_Map.m_Entities.size(); e++ )
	{
		const Entity_t &entity = scene.m_Map.m_Entities[ e ];
		const std::string_view szFile = entity.GetValue( "file" );
		if ( !kv1::EqualsIgnoreCase( entity.GetClassname(), "func_instance" ) || szFile.empty() )
		{
			continue;
		}
		if ( options.m_pCancel && options.m_pCancel->load() )
		{
			break;
		}
		if ( instances.m_nDepth >= instances.m_nMaxDepth )
		{
			problem( entity, "instances nest more than " + std::to_string( instances.m_nMaxDepth ) + " deep" );
			continue;
		}
		if ( pCache->m_nInstances >= instances.m_nMaxInstances )
		{
			problem( entity, "more than " + std::to_string( instances.m_nMaxInstances ) + " instances" );
			break;
		}
		const std::optional<std::filesystem::path> path = FindInstanceFile( szFile, instances );
		if ( !path )
		{
			problem( entity, std::string{ szFile } + " wasn't found" );
			continue;
		}
		if ( std::ranges::find( pCache->m_Reading, *path ) != pCache->m_Reading.end() )
		{
			problem( entity, std::string{ szFile } + " is inside itself" );
			continue;
		}
		const std::vector<std::pair<std::string, std::string>> replacements = Replacements( entity );
		std::string szReplacements;
		for ( const auto &[ szName, szValue ] : replacements )
		{
			szReplacements += szName + '\n' + szValue + '\n';
		}
		const std::pair<std::string, std::string> key{ path->string(), szReplacements };
		auto found = pCache->m_Scenes.find( key );
		if ( found == pCache->m_Scenes.end() )
		{
			const std::uintmax_t nSize = std::filesystem::file_size( *path, error );
			if ( error || pCache->m_nBytes + nSize > instances.m_nMaxBytes )
			{
				problem( entity, error ? std::string{ szFile } + " can't be read" : "instance files over " + std::to_string( instances.m_nMaxBytes >> 20 ) + " MB" );
				continue;
			}
			pCache->m_nBytes += nSize;
			std::ifstream file( *path, std::ios::binary );
			std::vector<char> text( static_cast<std::size_t>( nSize ) );
			if ( !file.read( text.data(), static_cast<std::streamsize>( text.size() ) ) )
			{
				problem( entity, std::string{ szFile } + " can't be read" );
				continue;
			}
			Replace( text, replacements );
			std::string szError;
			std::optional<CMap> map = ReadVmf( std::move( text ), {}, &szError );
			if ( !map )
			{
				problem( entity, std::string{ szFile } + ": " + szError );
				continue;
			}
			SceneOptions_t inner;
			inner.m_pCancel = options.m_pCancel;
			inner.m_Instances = instances;
			inner.m_Instances.m_MapFile = *path;
			inner.m_Instances.m_nDepth = instances.m_nDepth + 1;
			inner.m_Instances.m_pCache = pCache;
			std::shared_ptr<const CScene> pInner = BuildScene( std::move( *map ), scene.m_pFgd, inner );
			if ( !pInner )
			{
				break; // cancelled
			}
			// What went wrong inside the file shows with the map's own problems, once.
			for ( const std::vector<std::string> *pProblems : { &pInner->GetMesh().m_Problems, &pInner->m_Problems } )
			{
				for ( const std::string &szProblem : *pProblems )
				{
					scene.m_Problems.push_back( std::string{ szFile } + ": " + szProblem );
				}
			}
			found = pCache->m_Scenes.emplace( key, std::move( pInner ) ).first;
		}
		// A func_instance_origin in the file marks the point that goes on the func_instance.
		const CScene &inner = *found->second;
		Point_t centre{};
		for ( std::size_t i = 0; i < inner.m_Map.m_Entities.size(); i++ )
		{
			if ( kv1::EqualsIgnoreCase( inner.m_Map.m_Entities[ i ].GetClassname(), "func_instance_origin" ) )
			{
				centre = inner.m_Entities[ i ].m_Origin;
			}
		}
		const EntityVisual_t &visual = scene.m_Entities[ e ];
		Transform_t toParent = Transform_t::FromAngles( visual.m_Origin, visual.m_Angles );
		Transform_t recentre;
		recentre.m_Offset = Scale( centre, -1.0 );
		scene.m_Instances.push_back( { scene.EntityObject( e ), found->second, toParent * recentre } );
		pCache->m_nInstances++;
	}
	pCache->m_Reading.pop_back();
	BuildInstanceMesh( scene );
}

} // namespace xv::map
