//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Detail.h.
//===========================================================================//
#include "Detail.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <locale>
#include <utility>
#include "util/Numbers.h"
#include "vfs/Vfs.h"

namespace xv::detail
{
namespace
{
//-----------------------------------------------------------------------------
// Purpose: Read only pairs, leaving unexpected blocks intact in the document.
//-----------------------------------------------------------------------------
std::string Value( const kv1::CNode &node, std::string_view szKey )
{
	const auto *pValue = node.Find( szKey );
	return pValue && pValue->IsPair() ? pValue->GetValue() : std::string{};
}
//-----------------------------------------------------------------------------
// Purpose: Preserve invalid numbers as NaN for validation instead of silently replacing them.
//-----------------------------------------------------------------------------
double Number( const kv1::CNode &node, std::string_view szKey )
{
	const auto values = Numbers( Value( node, szKey ) );
	return values.size() == 1 ? values.front() : std::numeric_limits<double>::quiet_NaN();
}
//-----------------------------------------------------------------------------
// Purpose: Read a fixed-length sprite field without inventing missing coordinates.
//-----------------------------------------------------------------------------
template <std::size_t N>
std::optional<std::array<double, N>> Tuple( const kv1::CNode &node, std::string_view szKey )
{
	const auto values = Numbers( Value( node, szKey ) );
	if ( values.size() != N )
	{
		return std::nullopt;
	}
	std::array<double, N> result;
	std::copy( values.begin(), values.end(), result.begin() );
	return result;
}
} // namespace
//-----------------------------------------------------------------------------
// Purpose: Parse whitespace-separated, finite numbers independently of the user's locale.
//-----------------------------------------------------------------------------
std::vector<double> Numbers( std::string_view szValue )
{
	std::istringstream stream{ std::string( szValue ) };
	stream.imbue( std::locale::classic() );
	std::vector<double> result;
	std::string szPart;
	while ( stream >> szPart )
	{
		const auto flValue = ParseNumber<double>( szPart );
		if ( !flValue || !std::isfinite( *flValue ) )
		{
			return {};
		}
		result.push_back( *flValue );
	}
	return result;
}
//-----------------------------------------------------------------------------
// Purpose: Resolve stable child indices without assuming group or entry names.
//-----------------------------------------------------------------------------
const kv1::CNode *Node( const kv1::CDocument &document, const NodePath_t &path )
{
	if ( path.empty() || path.front() >= document.m_Roots.size() )
	{
		return nullptr;
	}
	const auto *pNode = &document.m_Roots[ path.front() ];
	for ( std::size_t i = 1; i < path.size(); ++i )
	{
		if ( !pNode->IsBlock() || path[ i ] >= pNode->GetChildren().size() )
		{
			return nullptr;
		}
		pNode = &pNode->Child( path[ i ] );
	}
	return pNode;
}
//-----------------------------------------------------------------------------
// Purpose: Resolve a mutable node through the same checked path.
//-----------------------------------------------------------------------------
kv1::CNode *Node( kv1::CDocument &document, const NodePath_t &path )
{
	return const_cast<kv1::CNode *>( Node( std::as_const( document ), path ) );
}
//-----------------------------------------------------------------------------
// Purpose: Read types from the stock file's detail wrapper, retaining paths to original tokens.
//-----------------------------------------------------------------------------
std::vector<Type_t> Types( const kv1::CDocument &document )
{
	std::vector<Type_t> result;
	for ( std::size_t r = 0; r < document.m_Roots.size(); ++r )
	{
		const auto &root = document.m_Roots[ r ];
		if ( !root.IsBlock() || !kv1::EqualsIgnoreCase( root.GetKey(), "detail" ) )
		{
			continue;
		}
		for ( std::size_t t = 0; t < root.GetChildren().size(); ++t )
		{
			const auto &type = root.Child( t );
			if ( !type.IsBlock() )
			{
				continue;
			}
			Type_t typed{ { r, t }, type.GetKey(), Number( type, "density" ), {} };
			for ( std::size_t g = 0; g < type.GetChildren().size(); ++g )
			{
				const auto &group = type.Child( g );
				if ( !group.IsBlock() )
				{
					continue;
				}
				Group_t grouped{ { r, t, g }, group.GetKey(), Number( group, "alpha" ), {} };
				for ( std::size_t e = 0; e < group.GetChildren().size(); ++e )
				{
					const auto &entry = group.Child( e );
					if ( !entry.IsBlock() )
					{
						continue;
					}
					grouped.m_Entries.push_back( { { r, t, g, e }, entry.GetKey(), Value( entry, "model" ), Tuple<5>( entry, "sprite" ), Tuple<4>( entry, "spritesize" ), Number( entry, "amount" ) } );
				}
				typed.m_Groups.push_back( std::move( grouped ) );
			}
			result.push_back( std::move( typed ) );
		}
	}
	return result;
}
//-----------------------------------------------------------------------------
// Purpose: Change a pair in place or add it with the block's existing indentation.
//-----------------------------------------------------------------------------
bool Set( kv1::CDocument &document, const NodePath_t &path, std::string_view szKey, std::string_view szValue )
{
	auto *pNode = Node( document, path );
	if ( !pNode || !pNode->IsBlock() )
	{
		return false;
	}
	if ( auto *pValue = pNode->Find( szKey ) )
	{
		return pValue->IsPair() && pValue->SetValue( szValue );
	}
	pNode->AppendChild( kv1::CNode::MakePair( szKey, szValue ) );
	return true;
}
//-----------------------------------------------------------------------------
// Purpose: Load editor field definitions from the bundled KeyValues data table.
//-----------------------------------------------------------------------------
CSchema CSchema::Parse( std::string_view szText )
{
	CSchema schema;
	const auto parsed = kv1::Parse( szText );
	const auto *pRoot = parsed.m_Document.Find( "detail_parameters" );
	if ( !pRoot || !pRoot->IsBlock() )
	{
		return schema;
	}
	for ( const auto &scope : pRoot->GetChildren() )
	{
		const int nDepth = scope.GetKey() == "type" ? 2 : scope.GetKey() == "group" ? 3
																					: 4;
		for ( const auto &key : scope.GetChildren() )
		{
			if ( key.IsBlock() )
			{
				schema.m_Keys.push_back( { key.GetKey(), Value( key, "kind" ), Value( key, "label" ), Value( key, "default" ), Value( key, "units" ), nDepth } );
			}
		}
	}
	return schema;
}
//-----------------------------------------------------------------------------
// Purpose: Match keys case-insensitively in their containing scope.
//-----------------------------------------------------------------------------
const KeyDef_t *CSchema::Find( std::string_view szKey, int nDepth ) const
{
	for ( const auto &key : m_Keys )
	{
		if ( key.m_nDepth == nDepth && kv1::EqualsIgnoreCase( key.m_szKey, szKey ) )
		{
			return &key;
		}
	}
	return nullptr;
}
//-----------------------------------------------------------------------------
// Purpose: Resolve each visible VMT, including patch inheritance, for detail type references.
//-----------------------------------------------------------------------------
std::map<std::string, std::vector<std::string>> ScanMaterials( const std::vector<std::string> &files, const material::Reader_t &pfnRead )
{
	std::map<std::string, std::vector<std::string>> uses;
	if ( !pfnRead )
	{
		return uses;
	}
	for ( const auto &szFile : files )
	{
		const auto bytes = pfnRead( szFile );
		if ( !bytes )
		{
			continue;
		}
		const auto material = material::ParseMaterial( std::string_view( reinterpret_cast<const char *>( bytes->data() ), bytes->size() ), pfnRead );
		if ( material )
		{
			const auto szType = material->Value( "%detailtype" );
			if ( !szType.empty() )
			{
				uses[ vfs::NormalizePath( szType ) ].push_back( szFile );
			}
		}
	}
	return uses;
}
//-----------------------------------------------------------------------------
// Purpose: Report malformed fields and cross-references without changing the document.
//-----------------------------------------------------------------------------
std::vector<Issue_t> Lint( const kv1::CDocument &document, const CSchema &schema, const LintContext_t &context )
{
	std::vector<Issue_t> issues;
	const auto add = [ & ]( const NodePath_t &path, std::string szMessage ) { issues.push_back( { path, std::move( szMessage ) } ); };
	const auto check = [ & ]( const NodePath_t &path ) {
		const auto *pNode = Node( document, path );
		for ( const auto &pair : pNode->GetChildren() )
		{
			if ( !pair.IsPair() )
			{
				continue;
			}
			const auto *pDef = schema.Find( pair.GetKey(), static_cast<int>( path.size() ) );
			if ( !pDef )
			{
				continue;
			}
			const auto &szKind = pDef->m_szKind;
			const auto szValue = pair.GetValue();
			const auto values = Numbers( szValue );
			bool bValid = true;
			if ( szKind == "rect" )
			{
				bValid = values.size() == 5;
			}
			else if ( szKind == "size" )
			{
				bValid = values.size() == 4 && values[ 0 ] >= 0 && values[ 0 ] <= 1 && values[ 1 ] >= 0 && values[ 1 ] <= 1 && values[ 2 ] > 0 && values[ 3 ] > 0;
			}
			else if ( szKind == "shape" )
			{
				bValid = szValue == "tri" || szValue == "cross";
			}
			else if ( szKind != "model" && szKind != "presence" )
			{
				bValid = values.size() == 1;
				if ( bValid && szKind == "normal" )
				{
					bValid = values[ 0 ] >= 0 && values[ 0 ] <= 1;
				}
				if ( bValid && szKind == "positive" )
				{
					bValid = values[ 0 ] >= 0;
				}
				if ( bValid && szKind == "angle" )
				{
					bValid = values[ 0 ] >= 0 && values[ 0 ] <= 180;
				}
				if ( bValid && szKind == "orientation" )
				{
					bValid = values[ 0 ] == 0 || values[ 0 ] == 1 || values[ 0 ] == 2;
				}
			}
			if ( !bValid )
			{
				add( path, "Invalid " + pair.GetKey() + ": " + szValue );
			}
			if ( pNode->FindAll( pair.GetKey() ).size() > 1 && pNode->Find( pair.GetKey() ) == &pair )
			{
				add( path, "Repeated key: " + pair.GetKey() );
			}
		}
	};
	const auto types = Types( document );
	if ( !document.Find( "detail" ) )
	{
		add( {}, "Missing detail wrapper." );
	}
	bool bSprites = false;
	std::vector<std::string> names;
	for ( const auto &type : types )
	{
		check( type.m_Path );
		const auto szName = vfs::NormalizePath( type.m_szName );
		if ( std::ranges::find( names, szName ) != names.end() )
		{
			add( type.m_Path, "Duplicate detail type: " + type.m_szName );
		}
		names.push_back( szName );
		if ( context.m_Uses && !context.m_Uses->contains( szName ) )
		{
			add( type.m_Path, "No scanned material uses this detail type." );
		}
		if ( !std::isfinite( type.m_flDensity ) || type.m_flDensity < 0 )
		{
			add( type.m_Path, "Density must be a finite, nonnegative number." );
		}
		int nDirection = 0;
		double flPrevious = std::numeric_limits<double>::quiet_NaN();
		for ( const auto &group : type.m_Groups )
		{
			check( group.m_Path );
			if ( std::isfinite( group.m_flAlpha ) && std::isfinite( flPrevious ) )
			{
				const int nNext = group.m_flAlpha > flPrevious ? 1 : group.m_flAlpha < flPrevious ? -1
																								  : 0;
				if ( nDirection && nNext && nDirection != nNext )
				{
					add( group.m_Path, "Group alphas change order; VBSP sorts them before use." );
				}
				if ( nNext )
				{
					nDirection = nNext;
				}
			}
			flPrevious = group.m_flAlpha;
			double flTotal = 0;
			for ( const auto &entry : group.m_Entries )
			{
				check( entry.m_Path );
				if ( !std::isfinite( entry.m_flAmount ) )
				{
					add( entry.m_Path, "Entry amount must be a finite number between 0 and 1." );
				}
				if ( std::isfinite( entry.m_flAmount ) )
				{
					flTotal += entry.m_flAmount;
				}
				const auto *pNode = Node( document, entry.m_Path );
				const bool bSprite = pNode->Find( "sprite" ) != nullptr;
				bSprites |= bSprite;
				if ( bSprite == !entry.m_szModel.empty() )
				{
					add( entry.m_Path, "Choose either a sprite or a model." );
				}
				if ( !entry.m_szModel.empty() && context.m_pfnExists && !context.m_pfnExists( vfs::NormalizePath( entry.m_szModel ) ) )
				{
					add( entry.m_Path, "Missing model: " + entry.m_szModel );
				}
				if ( bSprite && !entry.m_Size )
				{
					add( entry.m_Path, "Sprite needs an origin and world size (spritesize)." );
				}
				if ( entry.m_Size && ( ( *entry.m_Size )[ 2 ] <= 0 || ( *entry.m_Size )[ 3 ] <= 0 ) )
				{
					add( entry.m_Path, "Sprite world dimensions must be positive." );
				}
				if ( entry.m_Sprite )
				{
					const auto &r = *entry.m_Sprite;
					if ( r[ 0 ] < 0 || r[ 1 ] < 0 || r[ 2 ] <= 0 || r[ 3 ] <= 0 || r[ 4 ] <= 0 || r[ 0 ] + r[ 2 ] > r[ 4 ] || r[ 1 ] + r[ 3 ] > r[ 4 ] )
					{
						add( entry.m_Path, "Sprite rectangle is outside the sheet's square coordinate space." );
					}
				}
			}
			if ( flTotal > 1.000001 )
			{
				add( group.m_Path, "Entry amounts add up to more than 1." );
			}
		}
	}
	if ( bSprites && context.m_pfnExists && !context.m_pfnExists( "materials/" + context.m_szMaterial + ".vmt" ) )
	{
		add( {}, "Missing sprite material: " + context.m_szMaterial );
	}
	if ( context.m_Uses )
	{
		for ( const auto &[ szType, files ] : *context.m_Uses )
		{
			if ( std::ranges::find( names, szType ) == names.end() )
			{
				for ( const auto &szFile : files )
				{
					add( {}, szFile + " names undefined detail type: " + szType );
				}
			}
		}
	}
	return issues;
}

} // namespace xv::detail
