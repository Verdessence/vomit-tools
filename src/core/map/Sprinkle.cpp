//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Sprinkle.h. The definition format and the modes are the local VDC Sprinkle
//          article's and Hammer++'s example.txt; where the article leaves a choice (the grid's
//          plane, the cells' chance), the choice is noted at the code that makes it.
//
//===========================================================================//
#include "map/Sprinkle.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

#include "kv1/Kv1.h"
#include "map/GameConfig.h"
#include "map/Primitives.h"
#include "map/Scene.h"
#include "map/VmfValues.h"
#include "util/Numbers.h"

namespace xv::map
{
namespace
{
//-----------------------------------------------------------------------------
// Purpose: Definition names and file names are matched without case, as Hammer does.
//-----------------------------------------------------------------------------
std::string Lower( std::string_view sz )
{
	std::string szOut{ sz };
	std::ranges::transform( szOut, szOut.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose: Model paths match whichever way their slashes lean.
//-----------------------------------------------------------------------------
bool SameValue( std::string_view a, std::string_view b )
{
	if ( a.size() != b.size() )
		return false;
	for ( std::size_t i = 0; i < a.size(); i++ )
	{
		const char chA = a[ i ] == '\\' ? '/' : static_cast<char>( std::tolower( static_cast<unsigned char>( a[ i ] ) ) );
		const char chB = b[ i ] == '\\' ? '/' : static_cast<char>( std::tolower( static_cast<unsigned char>( b[ i ] ) ) );
		if ( chA != chB )
			return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: A block's pairs, in order; nested blocks mean nothing to the tool.
//-----------------------------------------------------------------------------
SprinkleKeys_t PairsOf( const kv1::CNode &block )
{
	SprinkleKeys_t keys;
	for ( const kv1::CNode &child : block.GetChildren() )
		if ( child.IsPair() )
			keys.emplace_back( child.GetKey(), child.GetValue() );
	return keys;
}

//-----------------------------------------------------------------------------
// Purpose: A cell's own random numbers: the same cell in the same stroke always rolls the same,
//          so dabbing over a place twice doesn't change what the first dab put there.
//-----------------------------------------------------------------------------
std::uint64_t Mix( std::uint64_t n )
{
	n += 0x9e3779b97f4a7c15ull;
	n = ( n ^ ( n >> 30 ) ) * 0xbf58476d1ce4e5b9ull;
	n = ( n ^ ( n >> 27 ) ) * 0x94d049bb133111ebull;
	return n ^ ( n >> 31 );
}

//-----------------------------------------------------------------------------
// Purpose: The nth number in [0, 1) of a cell.
//-----------------------------------------------------------------------------
double Roll( std::uint32_t nSeed, int nAxis, long long nU, long long nV, int n )
{
	std::uint64_t h = Mix( nSeed );
	h = Mix( h ^ static_cast<std::uint64_t>( nAxis ) );
	h = Mix( h ^ static_cast<std::uint64_t>( nU ) );
	h = Mix( h ^ static_cast<std::uint64_t>( nV ) );
	h = Mix( h ^ static_cast<std::uint64_t>( n ) );
	return static_cast<double>( h >> 11 ) / static_cast<double>( 1ull << 53 );
}

//-----------------------------------------------------------------------------
// Purpose: The axis a surface faces most, and the two that make its grid's plane.
//-----------------------------------------------------------------------------
std::array<int, 3> AxesFacing( Point_t normal )
{
	int nAxis = 2;
	for ( int a = 0; a < 3; a++ )
		if ( std::abs( normal[ a ] ) > std::abs( normal[ nAxis ] ) + 1e-9 )
			nAxis = a;
	// Top-down views grid x then y; walls their horizontal axis then z.
	if ( nAxis == 2 )
		return { 2, 0, 1 };
	return nAxis == 0 ? std::array{ 0, 1, 2 } : std::array{ 1, 0, 2 };
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view SprinkleType_t::GetClassname() const
{
	for ( const auto &[ szKey, szValue ] : m_Base )
		if ( kv1::EqualsIgnoreCase( szKey, "classname" ) )
			return szValue;
	return {};
}

//-----------------------------------------------------------------------------
// Purpose: Each root block is a type; in it, "base" holds the shared keys and the grid, and
//          each block named by a number is a case with that weight.
//-----------------------------------------------------------------------------
std::vector<SprinkleType_t> ParseSprinkleTypes( std::string_view szText, const std::filesystem::path &file, std::vector<std::string> *pErrors )
{
	std::vector<SprinkleType_t> types;
	const kv1::CParseResult parsed = kv1::Parse( szText );
	if ( pErrors )
		for ( const kv1::Diagnostic_t &diagnostic : parsed.m_Diagnostics )
			if ( diagnostic.m_nSeverity == kv1::Diagnostic_t::Severity_t::SEVERITY_ERROR )
				pErrors->push_back( file.filename().string() + ":" + std::to_string( diagnostic.m_nLine ) + ": " + diagnostic.m_szMessage );
	for ( const kv1::CNode &root : parsed.m_Document.m_Roots )
	{
		if ( !root.IsBlock() )
			continue;
		SprinkleType_t type;
		type.m_szName = root.GetKey();
		type.m_File = file;
		for ( const kv1::CNode &child : root.GetChildren() )
		{
			if ( !child.IsBlock() )
				continue;
			if ( kv1::EqualsIgnoreCase( child.GetKey(), "base" ) )
			{
				for ( auto &[ szKey, szValue ] : PairsOf( child ) )
				{
					if ( kv1::EqualsIgnoreCase( szKey, "grid" ) )
					{
						std::vector<double> grid;
						if ( ParseNumbers( szValue, 2, grid ) && grid[ 0 ] > 0.0 && grid[ 1 ] > 0.0 )
							type.m_Grid = { grid[ 0 ], grid[ 1 ] };
						else if ( const auto flOne = ParseNumber<double>( szValue ); flOne && *flOne > 0.0 )
							type.m_Grid = { *flOne, *flOne };
						else if ( pErrors )
							pErrors->push_back( file.filename().string() + ": " + type.m_szName + ": grid \"" + szValue + "\" is not two sizes" );
					}
					else
						type.m_Base.emplace_back( std::move( szKey ), std::move( szValue ) );
				}
			}
			else if ( const auto flWeight = ParseNumber<double>( child.GetKey() ) )
			{
				if ( *flWeight > 0.0 )
					type.m_Cases.push_back( { *flWeight, PairsOf( child ) } );
			}
			else if ( pErrors )
				pErrors->push_back( file.filename().string() + ": " + type.m_szName + ": \"" + child.GetKey() + "\" is neither base nor a chance" );
		}
		if ( type.GetClassname().empty() && std::ranges::none_of( type.m_Cases, []( const SprinkleCase_t &c ) {
				 return std::ranges::any_of( c.m_Keys, []( const auto &key ) { return kv1::EqualsIgnoreCase( key.first, "classname" ); } );
			 } ) )
		{
			if ( pErrors )
				pErrors->push_back( file.filename().string() + ": " + type.m_szName + ": no classname" );
			continue;
		}
		types.push_back( std::move( type ) );
	}
	return types;
}

//-----------------------------------------------------------------------------
// Purpose: The article puts definitions in the game's scripts/hammer/sprinkle; Hammer++ ships
//          its example in scripts/sprinkle beside its own executable.
//-----------------------------------------------------------------------------
std::vector<std::filesystem::path> SprinkleFolders( const std::filesystem::path &gameDir )
{
	const std::filesystem::path root = gameDir.parent_path();
	return { FindFileIgnoringCase( gameDir / "scripts/hammer/sprinkle" ), FindFileIgnoringCase( root / "bin/x64/hammerplusplus/scripts/sprinkle" ),
		FindFileIgnoringCase( root / "bin/hammerplusplus/scripts/sprinkle" ) };
}

//-----------------------------------------------------------------------------
// Purpose: A folder reached twice (bin and BIN can be one folder) is read once.
//-----------------------------------------------------------------------------
std::vector<SprinkleType_t> FindSprinkleTypes( std::span<const std::filesystem::path> folders, std::vector<std::string> *pErrors )
{
	std::vector<SprinkleType_t> types;
	std::set<std::string> names;
	std::set<std::filesystem::path> seen;
	for ( const std::filesystem::path &folder : folders )
	{
		std::error_code error;
		const std::filesystem::path canonical = std::filesystem::canonical( folder, error );
		if ( error || !seen.insert( canonical ).second )
			continue;
		std::vector<std::filesystem::path> files;
		for ( std::filesystem::directory_iterator it( canonical, error ), end; !error && it != end; it.increment( error ) )
			if ( it->is_regular_file( error ) && Lower( it->path().extension().string() ) == ".txt" )
				files.push_back( it->path() );
		std::ranges::sort( files, []( const auto &a, const auto &b ) { return Lower( a.filename().string() ) < Lower( b.filename().string() ); } );
		for ( const std::filesystem::path &file : files )
		{
			std::ifstream stream( file, std::ios::binary );
			std::stringstream text;
			text << stream.rdbuf();
			for ( SprinkleType_t &type : ParseSprinkleTypes( text.str(), file, pErrors ) )
				if ( names.insert( Lower( type.m_szName ) ).second )
					types.push_back( std::move( type ) );
		}
	}
	return types;
}

//-----------------------------------------------------------------------------
// Purpose: The classname a case gives wins over the base's.
//-----------------------------------------------------------------------------
bool MatchesSprinkleType( const Entity_t &entity, const SprinkleType_t &type )
{
	auto matches = [ & ]( const SprinkleKeys_t &keys ) {
		return std::ranges::all_of( keys, [ & ]( const auto &key ) { return SameValue( entity.GetValue( key.first ), key.second ); } );
	};
	const std::string_view szBaseClass = type.GetClassname();
	if ( type.m_Cases.empty() )
		return !szBaseClass.empty() && SameValue( entity.GetClassname(), szBaseClass );
	return std::ranges::any_of( type.m_Cases, [ & ]( const SprinkleCase_t &c ) {
		const bool bOwnClass = std::ranges::any_of( c.m_Keys, []( const auto &key ) { return kv1::EqualsIgnoreCase( key.first, "classname" ); } );
		return ( bOwnClass || SameValue( entity.GetClassname(), szBaseClass ) ) && matches( c.m_Keys );
	} );
}

//-----------------------------------------------------------------------------
// Purpose: What the tool made before, by the cell its origin is in on each plane.
//-----------------------------------------------------------------------------
CSprinkleStroke::CSprinkleStroke( std::shared_ptr<const CScene> pScene, SprinkleType_t type, SprinkleOptions_t options, SprinkleTrace_t pfnTrace )
	: m_pScene( std::move( pScene ) ),
	  m_Type( std::move( type ) ),
	  m_Options( options ),
	  m_pfnTrace( std::move( pfnTrace ) )
{
	const auto grid = GetGrid();
	const auto &entities = m_pScene->m_Map.m_Entities;
	for ( std::uint32_t e = 0; e < entities.size(); e++ )
	{
		const Entity_t &entity = entities[ e ];
		if ( entity.m_bDeleted || !MatchesSprinkleType( entity, m_Type ) )
			continue;
		const auto origin = ParsePoint( entity.GetValue( "origin" ) );
		if ( !origin )
			continue;
		for ( int nAxis = 0; nAxis < 3; nAxis++ )
		{
			const auto axes = AxesFacing( { nAxis == 0 ? 1.0 : 0.0, nAxis == 1 ? 1.0 : 0.0, nAxis == 2 ? 1.0 : 0.0 } );
			const auto nU = static_cast<long long>( std::floor( ( *origin )[ axes[ 1 ] ] / grid[ 0 ] ) );
			const auto nV = static_cast<long long>( std::floor( ( *origin )[ axes[ 2 ] ] / grid[ 1 ] ) );
			m_Existing[ nAxis ][ { nU, nV } ].push_back( e );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::array<double, 2> CSprinkleStroke::GetGrid() const
{
	auto grid = m_Options.m_Grid.value_or( m_Type.m_Grid );
	for ( double &fl : grid )
		fl = std::max( fl, 1.0 );
	return grid;
}

//-----------------------------------------------------------------------------
// Purpose: The cells whose middles are in the brush's circle on the plane the surface faces.
//          Each is traced from in front of the surface (by the radius) to behind it, so the
//          entity lands on what is there, not at the brush's height.
//-----------------------------------------------------------------------------
bool CSprinkleStroke::Dab( Point_t at, Point_t normal, double flRadius, std::optional<SprinkleMode_t> nMode )
{
	const SprinkleMode_t nUse = nMode.value_or( m_Options.m_nMode );
	const auto axes = AxesFacing( normal );
	const int nAxis = axes[ 0 ];
	const double flSign = normal[ nAxis ] < 0.0 ? -1.0 : 1.0;
	const auto grid = GetGrid();
	flRadius = std::max( flRadius, 1.0 );
	const auto nU0 = static_cast<long long>( std::floor( ( at[ axes[ 1 ] ] - flRadius ) / grid[ 0 ] ) );
	const auto nU1 = static_cast<long long>( std::floor( ( at[ axes[ 1 ] ] + flRadius ) / grid[ 0 ] ) );
	const auto nV0 = static_cast<long long>( std::floor( ( at[ axes[ 2 ] ] - flRadius ) / grid[ 1 ] ) );
	const auto nV1 = static_cast<long long>( std::floor( ( at[ axes[ 2 ] ] + flRadius ) / grid[ 1 ] ) );
	bool bChanged = false;
	for ( long long nU = nU0; nU <= nU1; nU++ )
	{
		for ( long long nV = nV0; nV <= nV1; nV++ )
		{
			const double flU = ( static_cast<double>( nU ) + 0.5 ) * grid[ 0 ];
			const double flV = ( static_cast<double>( nV ) + 0.5 ) * grid[ 1 ];
			const double flDU = flU - at[ axes[ 1 ] ];
			const double flDV = flV - at[ axes[ 2 ] ];
			if ( flDU * flDU + flDV * flDV > flRadius * flRadius )
				continue;
			const Cell_t cell{ nAxis, nU, nV };
			const bool bClearing = nUse == SprinkleMode_t::SPRINKLE_MODE_SUBTRACTIVE;
			if ( const auto found = m_Cells.find( cell ); found != m_Cells.end() )
			{
				// A cell is decided once a stroke; only Ctrl's eraser goes back over it.
				if ( !bClearing || !found->second.m_Added )
					continue;
				found->second.m_Added.reset();
				bChanged = true;
				continue;
			}
			// What the tool made in this cell, near enough the brush along the facing axis.
			Planned_t planned;
			if ( const auto existing = m_Existing[ nAxis ].find( { nU, nV } ); existing != m_Existing[ nAxis ].end() )
			{
				for ( const std::uint32_t e : existing->second )
				{
					const auto origin = ParsePoint( m_pScene->m_Map.m_Entities[ e ].GetValue( "origin" ) );
					if ( origin && std::abs( ( *origin )[ nAxis ] - at[ nAxis ] ) <= flRadius )
						planned.m_Removed.push_back( e );
				}
			}
			const bool bOccupied = !planned.m_Removed.empty();
			bool bPlace = false;
			switch ( nUse )
			{
				case SprinkleMode_t::SPRINKLE_MODE_SUBTRACTIVE:
					break;
				case SprinkleMode_t::SPRINKLE_MODE_ADDITIVE:
				case SprinkleMode_t::SPRINKLE_MODE_REPLACE:
					bPlace = Roll( m_Options.m_nSeed, nAxis, nU, nV, 0 ) < m_Options.m_flDensity;
					break;
				case SprinkleMode_t::SPRINKLE_MODE_OVERWRITE:
					bPlace = bOccupied;
					break;
			}
			// Additive leaves a cell that rolled empty as it was; Replace clears it.
			if ( !bPlace && nUse == SprinkleMode_t::SPRINKLE_MODE_ADDITIVE )
				planned.m_Removed.clear();
			if ( bPlace && !m_Type.m_Cases.empty() )
			{
				Point_t from = at;
				from[ axes[ 1 ] ] = flU;
				from[ axes[ 2 ] ] = flV;
				from[ nAxis ] += flSign * flRadius;
				Point_t dir{};
				dir[ nAxis ] = -flSign;
				const auto hit = m_pfnTrace ? m_pfnTrace( from, dir, 2.0 * flRadius ) : std::nullopt;
				if ( hit && Dot( hit->second, Scale( dir, -1.0 ) ) > 0.0 )
					planned.m_Added = MakeEntity( cell, hit->first, hit->second );
				else if ( nUse == SprinkleMode_t::SPRINKLE_MODE_ADDITIVE )
					planned.m_Removed.clear();
			}
			if ( !planned.m_Added && planned.m_Removed.empty() && nUse != SprinkleMode_t::SPRINKLE_MODE_ADDITIVE && nUse != SprinkleMode_t::SPRINKLE_MODE_REPLACE )
				continue;
			bChanged = bChanged || planned.m_Added || !planned.m_Removed.empty();
			m_Cells.emplace( cell, std::move( planned ) );
		}
	}
	return bChanged;
}

//-----------------------------------------------------------------------------
// Purpose: The case by its weight, then the base's keys with the case's over them; the entity
//          tool's defaults from the game data under both.
//-----------------------------------------------------------------------------
Entity_t CSprinkleStroke::MakeEntity( const Cell_t &cell, Point_t origin, Point_t normal ) const
{
	const auto &[ nAxis, nU, nV ] = cell;
	double flTotal = 0.0;
	for ( const SprinkleCase_t &c : m_Type.m_Cases )
		flTotal += c.m_flWeight;
	double flPick = Roll( m_Options.m_nSeed, nAxis, nU, nV, 1 ) * flTotal;
	const SprinkleCase_t *pCase = &m_Type.m_Cases.back();
	for ( const SprinkleCase_t &c : m_Type.m_Cases )
	{
		if ( flPick < c.m_flWeight )
		{
			pCase = &c;
			break;
		}
		flPick -= c.m_flWeight;
	}
	std::string_view szClass = m_Type.GetClassname();
	for ( const auto &[ szKey, szValue ] : pCase->m_Keys )
		if ( kv1::EqualsIgnoreCase( szKey, "classname" ) )
			szClass = szValue;
	const CMap &map = m_pScene->m_Map;
	origin = Add( origin, Scale( normal, m_Options.m_flOffset ) );
	for ( double &fl : origin )
		fl = std::round( fl * 100.0 ) / 100.0;
	Entity_t entity = MakePointEntity( map, m_pScene->m_pFgd.get(), szClass, origin );
	for ( const SprinkleKeys_t *pKeys : { &m_Type.m_Base, &pCase->m_Keys } )
		for ( const auto &[ szKey, szValue ] : *pKeys )
			if ( !kv1::EqualsIgnoreCase( szKey, "classname" ) && !kv1::EqualsIgnoreCase( szKey, "origin" ) )
				SetKeyValue( map, entity, szKey, szValue );
	if ( m_Options.m_bRandomYaw )
	{
		std::array<double, 3> angles = ParsePoint( entity.GetValue( "angles" ) ).value_or( Point_t{} );
		angles[ 1 ] = std::floor( Roll( m_Options.m_nSeed, nAxis, nU, nV, 2 ) * 360.0 );
		SetKeyValue( map, entity, "angles", FormatPoint( angles ) );
	}
	return entity;
}

//-----------------------------------------------------------------------------
// Purpose: Each entity the stroke takes away, once, then the new ones (their indices follow
//          the map's last, as AddObjects gives them).
//-----------------------------------------------------------------------------
MapEdit_t CSprinkleStroke::GetEdit( std::vector<std::uint32_t> *pAdded ) const
{
	std::vector<Entity_t> added;
	std::set<std::uint32_t> removed;
	for ( const auto &[ cell, planned ] : m_Cells )
	{
		removed.insert( planned.m_Removed.begin(), planned.m_Removed.end() );
		if ( planned.m_Added )
			added.push_back( *planned.m_Added );
	}
	MapEdit_t edit = added.empty() ? MapEdit_t{} : AddObjects( *m_pScene, {}, std::move( added ), pAdded );
	std::vector<std::pair<std::uint32_t, Entity_t>> gone;
	for ( const std::uint32_t e : removed )
	{
		Entity_t entity = m_pScene->m_Map.m_Entities[ e ];
		entity.m_bDeleted = true;
		gone.emplace_back( e, std::move( entity ) );
	}
	edit.m_Entities.insert( edit.m_Entities.begin(), std::make_move_iterator( gone.begin() ), std::make_move_iterator( gone.end() ) );
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::size_t CSprinkleStroke::GetAddedCount() const
{
	return static_cast<std::size_t>( std::ranges::count_if( m_Cells, []( const auto &entry ) { return entry.second.m_Added.has_value(); } ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::size_t CSprinkleStroke::GetRemovedCount() const
{
	std::set<std::uint32_t> removed;
	for ( const auto &[ cell, planned ] : m_Cells )
		removed.insert( planned.m_Removed.begin(), planned.m_Removed.end() );
	return removed.size();
}

} // namespace xv::map
