//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Particles.h.
//
//===========================================================================//
#include "Particles.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <random>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "vfs/Vfs.h"

namespace xv::particles
{

using dmx::Attribute_t;
using dmx::AttrType_t;
using dmx::CElement;
using dmx::CElementRef;
using dmx::Document_t;

namespace
{

constexpr std::array<std::string_view, 6> CATEGORY_ATTRIBUTES{
	"emitters",
	"initializers",
	"operators",
	"renderers",
	"forces",
	"constraints",
};

constexpr std::array<std::string_view, 5> COMMON_OPERATOR_ATTRIBUTES{
	"operator start fadein",
	"operator end fadein",
	"operator start fadeout",
	"operator end fadeout",
	"operator fade oscillate",
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string Lower( std::string_view szText )
{
	std::string szOut{ szText };
	std::ranges::transform( szOut, szOut.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CElement &At( Document_t &document, std::int32_t nIndex )
{
	return document.m_Elements[ static_cast<std::size_t>( nIndex ) ];
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const CElement &At( const Document_t &document, std::int32_t nIndex )
{
	return document.m_Elements[ static_cast<std::size_t>( nIndex ) ];
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool Valid( const Document_t &document, std::int32_t nIndex )
{
	return nIndex >= 0 && static_cast<std::size_t>( nIndex ) < document.m_Elements.size();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<CElementRef> *RefList( CElement &element, std::string_view szAttribute )
{
	Attribute_t *pFound = element.Find( szAttribute );
	return pFound ? std::get_if<std::vector<CElementRef>>( &pFound->m_Value ) : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: The list, created empty when the element doesn't have it yet.
//-----------------------------------------------------------------------------
std::vector<CElementRef> &EnsureRefList( CElement &element, std::string_view szAttribute )
{
	if ( auto *pList = RefList( element, szAttribute ) )
	{
		return *pList;
	}
	SetAttribute( element, szAttribute, AttrType_t::ATTR_TYPE_ELEMENT_ARRAY, std::vector<CElementRef>{} );
	return std::get<std::vector<CElementRef>>( element.Find( szAttribute )->m_Value );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::int32_t Append( Document_t &document, CElement element )
{
	document.m_Elements.push_back( std::move( element ) );
	return static_cast<std::int32_t>( document.m_Elements.size() - 1 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void InsertRef( std::vector<CElementRef> &list, std::int32_t nIndex, std::optional<std::size_t> position )
{
	const std::size_t nAt = std::min( position.value_or( list.size() ), list.size() );
	list.insert( list.begin() + static_cast<std::ptrdiff_t>( nAt ), CElementRef{ nIndex, {} } );
}

//-----------------------------------------------------------------------------
// Purpose: Calls visit for every element reference an element holds.
//-----------------------------------------------------------------------------
template <typename Visit>
void ForEachRef( CElement &element, Visit &&visit )
{
	for ( Attribute_t &attribute : element.m_Attributes )
	{
		if ( auto *pRef = std::get_if<CElementRef>( &attribute.m_Value ) )
		{
			visit( *pRef );
		}
		else if ( auto *pRefs = std::get_if<std::vector<CElementRef>>( &attribute.m_Value ) )
		{
			for ( CElementRef &r : *pRefs )
			{
				visit( r );
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
template <typename Visit>
void ForEachRef( const CElement &element, Visit &&visit )
{
	ForEachRef( const_cast<CElement &>( element ), [ &visit ]( const CElementRef &ref ) { visit( ref ); } );
}

//-----------------------------------------------------------------------------
// Purpose: Elements reachable from the given starting points.
//-----------------------------------------------------------------------------
std::vector<bool> Reachable( const Document_t &document, std::vector<std::int32_t> start )
{
	std::vector<bool> seen( document.m_Elements.size() );
	for ( std::int32_t nIndex : start )
	{
		if ( Valid( document, nIndex ) )
		{
			seen[ static_cast<std::size_t>( nIndex ) ] = true;
		}
	}
	while ( !start.empty() )
	{
		const std::int32_t nIndex = start.back();
		start.pop_back();
		if ( !Valid( document, nIndex ) )
		{
			continue;
		}
		ForEachRef( At( document, nIndex ), [ & ]( const CElementRef &ref ) {
			if ( Valid( document, ref.m_nIndex ) && !seen[ static_cast<std::size_t>( ref.m_nIndex ) ] )
			{
				seen[ static_cast<std::size_t>( ref.m_nIndex ) ] = true;
				start.push_back( ref.m_nIndex );
			}
		} );
	}
	return seen;
}

//-----------------------------------------------------------------------------
// Purpose: Copies the marked elements into a new list, renumbering references; references to
//          elements left
//          out become null.
//-----------------------------------------------------------------------------
std::vector<CElement> CopyMarked( const Document_t &document, const std::vector<bool> &keep, std::vector<std::int32_t> &mapping )
{
	mapping.assign( document.m_Elements.size(), CElementRef::NONE );
	std::vector<CElement> out;
	for ( std::size_t i = 0; i < document.m_Elements.size(); i++ )
	{
		if ( keep[ i ] )
		{
			mapping[ i ] = static_cast<std::int32_t>( out.size() );
			out.push_back( document.m_Elements[ i ] );
		}
	}
	for ( CElement &element : out )
	{
		ForEachRef( element, [ &mapping ]( CElementRef &ref ) {
			if ( ref.m_nIndex >= 0 )
			{
				ref.m_nIndex = static_cast<std::size_t>( ref.m_nIndex ) < mapping.size() ? mapping[ static_cast<std::size_t>( ref.m_nIndex ) ] : CElementRef::NONE;
			}
		} );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float FloatAttribute( const CElement &element, std::string_view szName, float flFallback )
{
	const Attribute_t *pAttribute = element.Find( szName );
	const float *pValue = pAttribute ? std::get_if<float>( &pAttribute->m_Value ) : nullptr;
	return pValue ? *pValue : flFallback;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::int32_t IntAttribute( const CElement &element, std::string_view szName, std::int32_t nFallback )
{
	const Attribute_t *pAttribute = element.Find( szName );
	const std::int32_t *pValue = pAttribute ? std::get_if<std::int32_t>( &pAttribute->m_Value ) : nullptr;
	return pValue ? *pValue : nFallback;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string StringAttribute( const CElement &element, std::string_view szName )
{
	const Attribute_t *pAttribute = element.Find( szName );
	const std::string *pValue = pAttribute ? std::get_if<std::string>( &pAttribute->m_Value ) : nullptr;
	return pValue ? *pValue : std::string{};
}

//-----------------------------------------------------------------------------
// Purpose: The value an attribute of this type starts at when nothing better is known.
//-----------------------------------------------------------------------------
dmx::Value_t ZeroValue( AttrType_t nType )
{
	switch ( nType )
	{
		case AttrType_t::ATTR_TYPE_ELEMENT: return CElementRef{};
		case AttrType_t::ATTR_TYPE_INT: return std::int32_t{ 0 };
		case AttrType_t::ATTR_TYPE_FLOAT: return 0.0f;
		case AttrType_t::ATTR_TYPE_BOOL: return false;
		case AttrType_t::ATTR_TYPE_STRING: return std::string{};
		case AttrType_t::ATTR_TYPE_COLOR: return dmx::CColor{ 255, 255, 255, 255 };
		case AttrType_t::ATTR_TYPE_VECTOR2: return dmx::Vec2_t{};
		case AttrType_t::ATTR_TYPE_VECTOR3:
		case AttrType_t::ATTR_TYPE_QANGLE: return dmx::Vec3_t{};
		case AttrType_t::ATTR_TYPE_VECTOR4:
		case AttrType_t::ATTR_TYPE_QUATERNION: return dmx::Vec4_t{};
		case AttrType_t::ATTR_TYPE_ELEMENT_ARRAY: return std::vector<CElementRef>{};
		default: return std::monostate{};
	}
}

// The material a new system starts with; every Source game has it.
constexpr std::string_view NEW_SYSTEM_MATERIAL = "vgui/white.vmt";

//-----------------------------------------------------------------------------
// Purpose: System properties for a new system when no game files are at hand: the values CS:S's
//          and HL2's
//          files use most.
//-----------------------------------------------------------------------------
std::vector<AttributeInfo_t> BasicSystemAttributes()
{
	return {
		{ "max_particles", AttrType_t::ATTR_TYPE_INT, std::int32_t{ 1000 } },
		{ "initial_particles", AttrType_t::ATTR_TYPE_INT, std::int32_t{ 0 } },
		{ "material", AttrType_t::ATTR_TYPE_STRING, std::string{ NEW_SYSTEM_MATERIAL } },
		{ "bounding_box_min", AttrType_t::ATTR_TYPE_VECTOR3, dmx::Vec3_t{ -10, -10, -10 } },
		{ "bounding_box_max", AttrType_t::ATTR_TYPE_VECTOR3, dmx::Vec3_t{ 10, 10, 10 } },
		{ "cull_radius", AttrType_t::ATTR_TYPE_FLOAT, 0.0f },
		{ "cull_cost", AttrType_t::ATTR_TYPE_FLOAT, 1.0f },
		{ "cull_control_point", AttrType_t::ATTR_TYPE_INT, std::int32_t{ 0 } },
		{ "cull_replacement_definition", AttrType_t::ATTR_TYPE_STRING, std::string{} },
		{ "radius", AttrType_t::ATTR_TYPE_FLOAT, 5.0f },
		{ "color", AttrType_t::ATTR_TYPE_COLOR, dmx::CColor{ 255, 255, 255, 255 } },
		{ "rotation", AttrType_t::ATTR_TYPE_FLOAT, 0.0f },
		{ "rotation_speed", AttrType_t::ATTR_TYPE_FLOAT, 0.0f },
		{ "sequence_number", AttrType_t::ATTR_TYPE_INT, std::int32_t{ 0 } },
		{ "sequence_number 1", AttrType_t::ATTR_TYPE_INT, std::int32_t{ 0 } },
		{ "group id", AttrType_t::ATTR_TYPE_INT, std::int32_t{ 0 } },
		{ "maximum time step", AttrType_t::ATTR_TYPE_FLOAT, 0.1f },
		{ "maximum sim tick rate", AttrType_t::ATTR_TYPE_FLOAT, 0.0f },
		{ "minimum sim tick rate", AttrType_t::ATTR_TYPE_FLOAT, 0.0f },
		{ "minimum rendered frames", AttrType_t::ATTR_TYPE_INT, std::int32_t{ 0 } },
		{ "control point to disable rendering if it is the camera", AttrType_t::ATTR_TYPE_INT, std::int32_t{ -1 } },
		{ "maximum draw distance", AttrType_t::ATTR_TYPE_FLOAT, 100000.0f },
		{ "time to sleep when not drawn", AttrType_t::ATTR_TYPE_FLOAT, 8.0f },
		{ "Sort particles", AttrType_t::ATTR_TYPE_BOOL, true },
		{ "batch particle systems", AttrType_t::ATTR_TYPE_BOOL, false },
		{ "view model effect", AttrType_t::ATTR_TYPE_BOOL, false },
		{ "bounding_box_control_point", AttrType_t::ATTR_TYPE_INT, std::int32_t{ 0 } },
		{ "preventNameBasedLookup", AttrType_t::ATTR_TYPE_BOOL, false },
		{ "children", AttrType_t::ATTR_TYPE_ELEMENT_ARRAY, std::vector<CElementRef>{} },
		{ "emitters", AttrType_t::ATTR_TYPE_ELEMENT_ARRAY, std::vector<CElementRef>{} },
		{ "initializers", AttrType_t::ATTR_TYPE_ELEMENT_ARRAY, std::vector<CElementRef>{} },
		{ "operators", AttrType_t::ATTR_TYPE_ELEMENT_ARRAY, std::vector<CElementRef>{} },
		{ "renderers", AttrType_t::ATTR_TYPE_ELEMENT_ARRAY, std::vector<CElementRef>{} },
		{ "forces", AttrType_t::ATTR_TYPE_ELEMENT_ARRAY, std::vector<CElementRef>{} },
		{ "constraints", AttrType_t::ATTR_TYPE_ELEMENT_ARRAY, std::vector<CElementRef>{} },
	};
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The system attribute holding the category's list: "emitters", "initializers", ...
//-----------------------------------------------------------------------------
std::string_view AttributeName( Category_t nCategory )
{
	return CATEGORY_ATTRIBUTES[ static_cast<std::size_t>( nCategory ) ];
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<Category_t> CategoryOf( std::string_view szAttributeName )
{
	for ( Category_t nCategory : CATEGORIES )
	{
		if ( particles::AttributeName( nCategory ) == szAttributeName )
		{
			return nCategory;
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: The fade window every operator has ("operator start fadein" and so on), plus
//          functionName.
//-----------------------------------------------------------------------------
bool IsCommonOperatorAttribute( std::string_view szName )
{
	return szName == FUNCTION_NAME || std::ranges::find( COMMON_OPERATOR_ATTRIBUTES, szName ) != COMMON_OPERATOR_ATTRIBUTES.end();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsParticleFile( const Document_t &document )
{
	if ( document.m_Elements.empty() )
	{
		return false;
	}
	const Attribute_t *pList = document.m_Elements.front().Find( DEFINITIONS );
	return pList && pList->m_nType == AttrType_t::ATTR_TYPE_ELEMENT_ARRAY;
}

//-----------------------------------------------------------------------------
// Purpose: The elements an element-array attribute points at (null and external references left
//          out).
//-----------------------------------------------------------------------------
std::vector<std::int32_t> References( const CElement &element, std::string_view szAttribute )
{
	std::vector<std::int32_t> out;
	const Attribute_t *pFound = element.Find( szAttribute );
	if ( const auto *pRefs = pFound ? std::get_if<std::vector<CElementRef>>( &pFound->m_Value ) : nullptr )
	{
		for ( const CElementRef &ref : *pRefs )
		{
			if ( ref.m_nIndex >= 0 )
			{
				out.push_back( ref.m_nIndex );
			}
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view FunctionName( const CElement &op )
{
	const Attribute_t *pAttribute = op.Find( FUNCTION_NAME );
	const std::string *pName = pAttribute ? std::get_if<std::string>( &pAttribute->m_Value ) : nullptr;
	return pName ? std::string_view{ *pName } : std::string_view{};
}

//-----------------------------------------------------------------------------
// Purpose: Reading
//          The particle systems of a document, as element indices: the root's list in order, then
//          systems
//          only reachable as children.
//-----------------------------------------------------------------------------
std::vector<std::int32_t> Systems( const Document_t &document )
{
	std::vector<std::int32_t> out;
	if ( !IsParticleFile( document ) )
	{
		return out;
	}
	std::vector<bool> seen( document.m_Elements.size() );
	const auto take = [ & ]( std::int32_t nIndex ) {
		if ( Valid( document, nIndex ) && !seen[ static_cast<std::size_t>( nIndex ) ] && At( document, nIndex ).m_szType == SYSTEM_TYPE )
		{
			seen[ static_cast<std::size_t>( nIndex ) ] = true;
			out.push_back( nIndex );
		}
	};
	for ( std::int32_t nIndex : References( document.m_Elements.front(), DEFINITIONS ) )
	{
		take( nIndex );
	}
	// Systems only reachable as children, breadth first.
	for ( std::size_t i = 0; i < out.size(); i++ )
	{
		for ( const ChildLink_t &link : Children( document, out[ i ] ) )
		{
			take( link.m_nSystem );
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::int32_t> FindSystem( const Document_t &document, std::string_view szName )
{
	for ( std::int32_t nIndex : Systems( document ) )
	{
		if ( At( document, nIndex ).m_szName == szName )
		{
			return nIndex;
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<ChildLink_t> Children( const Document_t &document, std::int32_t nSystem )
{
	std::vector<ChildLink_t> out;
	if ( !Valid( document, nSystem ) )
	{
		return out;
	}
	for ( std::int32_t nLink : References( At( document, nSystem ), CHILDREN ) )
	{
		if ( !Valid( document, nLink ) )
		{
			continue;
		}
		const CElement &element = At( document, nLink );
		const Attribute_t *pChild = element.Find( "child" );
		const CElementRef *pRef = pChild ? std::get_if<CElementRef>( &pChild->m_Value ) : nullptr;
		const std::int32_t nTarget = pRef && Valid( document, pRef->m_nIndex ) && At( document, pRef->m_nIndex ).m_szType == SYSTEM_TYPE ? pRef->m_nIndex : -1;
		out.push_back( { nLink, nTarget, FloatAttribute( element, "delay", 0.0f ) } );
	}
	return out;
}

// ---------------------------------------------------------------------------------------------
// Schema

//-----------------------------------------------------------------------------
// Purpose: ---------------------------------------------------------------------------------------
//          ------
//          Schema
//-----------------------------------------------------------------------------
const AttributeInfo_t *CFunctionInfo::Attribute( std::string_view szAttributeName ) const
{
	const auto it = std::ranges::find( m_Attributes, szAttributeName, &AttributeInfo_t::m_szName );
	return it == m_Attributes.end() ? nullptr : &*it;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSchema::Count( std::map<std::string, Tally_t> &tallies, const CElement &element )
{
	for ( const Attribute_t &attribute : element.m_Attributes )
	{
		auto [ it, added ] = tallies.try_emplace( attribute.m_szName );
		Tally_t &tally = it->second;
		if ( added )
		{
			tally.m_nType = attribute.m_nType;
			tally.m_nOrder = tallies.size();
		}
		if ( tally.m_nType != attribute.m_nType )
		{
			continue;
		}
		// References are file-specific; a new element starts with none.
		const bool bReference = attribute.m_nType == AttrType_t::ATTR_TYPE_ELEMENT || attribute.m_nType == AttrType_t::ATTR_TYPE_ELEMENT_ARRAY;
		const std::string szKey = bReference ? std::string{} : dmx::ValueText( attribute.m_Value );
		auto &[ uses, value ] = tally.m_Values[ szKey ];
		if ( uses++ == 0 )
		{
			value = bReference ? ZeroValue( attribute.m_nType ) : attribute.m_Value;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<AttributeInfo_t> CSchema::MostCommon( const std::map<std::string, Tally_t> &tallies )
{
	std::vector<std::pair<std::size_t, AttributeInfo_t>> ordered;
	for ( const auto &[ name, tally ] : tallies )
	{
		const auto best = std::ranges::max_element( tally.m_Values, {}, []( const auto &entry ) { return entry.second.first; } );
		ordered.emplace_back( tally.m_nOrder, AttributeInfo_t{ name, tally.m_nType, best == tally.m_Values.end() ? ZeroValue( tally.m_nType ) : best->second.second } );
	}
	std::ranges::sort( ordered, {}, &std::pair<std::size_t, AttributeInfo_t>::first );
	std::vector<AttributeInfo_t> out;
	for ( auto &[ order, info ] : ordered )
	{
		out.push_back( std::move( info ) );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Collects one file. Call finish() once every file is in.
//-----------------------------------------------------------------------------
void CSchema::Add( const Document_t &document )
{
	if ( !IsParticleFile( document ) )
	{
		return;
	}
	m_nFiles++;
	for ( std::int32_t nIndex : Systems( document ) )
	{
		const CElement &system = At( document, nIndex );
		Count( m_SystemTallies, system );
		for ( Category_t nCategory : CATEGORIES )
		{
			for ( std::int32_t nOp : References( system, AttributeName( nCategory ) ) )
			{
				if ( !Valid( document, nOp ) || At( document, nOp ).m_szType != OPERATOR_TYPE )
				{
					continue;
				}
				const CElement &element = At( document, nOp );
				const std::string szName{ FunctionName( element ) };
				Collected_t &collected = m_Collected[ { nCategory, szName } ];
				collected.m_nCategory = nCategory;
				collected.m_szName = szName;
				collected.m_nUses++;
				Count( collected.m_Attributes, element );
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSchema::Finish()
{
	m_Functions.clear();
	for ( const auto &[ key, collected ] : m_Collected )
	{
		m_Functions.push_back( { collected.m_nCategory, collected.m_szName, MostCommon( collected.m_Attributes ), collected.m_nUses } );
	}
	m_System = MostCommon( m_SystemTallies );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSchema::AddSystemFile( std::string_view szSystemName, std::string_view szFile )
{
	m_SystemFiles.try_emplace( Lower( szSystemName ), std::string{ szFile } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const CFunctionInfo *CSchema::Function( Category_t nCategory, std::string_view szName ) const
{
	const auto it = std::ranges::find_if( m_Functions, [ & ]( const CFunctionInfo &f ) { return f.m_nCategory == nCategory && f.m_szName == szName; } );
	return it == m_Functions.end() ? nullptr : &*it;
}

//-----------------------------------------------------------------------------
// Purpose: A category's functions, most used first.
//-----------------------------------------------------------------------------
std::vector<const CFunctionInfo *> CSchema::Functions( Category_t nCategory ) const
{
	std::vector<const CFunctionInfo *> out;
	for ( const CFunctionInfo &f : m_Functions )
	{
		if ( f.m_nCategory == nCategory )
		{
			out.push_back( &f );
		}
	}
	std::ranges::stable_sort( out, std::greater{}, &CFunctionInfo::m_nUses );
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: The categories a function is used in.
//-----------------------------------------------------------------------------
std::vector<Category_t> CSchema::CategoriesOf( std::string_view szName ) const
{
	std::vector<Category_t> out;
	for ( const CFunctionInfo &f : m_Functions )
	{
		if ( f.m_szName == szName )
		{
			out.push_back( f.m_nCategory );
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const AttributeInfo_t *CSchema::SystemAttribute( std::string_view szName ) const
{
	const auto it = std::ranges::find( m_System, szName, &AttributeInfo_t::m_szName );
	return it == m_System.end() ? nullptr : &*it;
}

// ---------------------------------------------------------------------------------------------
// Editing

//-----------------------------------------------------------------------------
// Purpose: Editing. Elements are only ever appended, never removed: something taken out of a list
//          is left
//          unreferenced, and Compact() drops it when the file is written.
//-----------------------------------------------------------------------------
dmx::Guid_t NewGuid()
{
	thread_local std::mt19937_64 random{ std::random_device{}() };
	dmx::Guid_t guid{};
	for ( std::size_t i = 0; i < guid.size(); i += 8 )
	{
		const std::uint64_t nBits = random();
		for ( std::size_t j = 0; j < 8; j++ )
		{
			guid[ i + j ] = static_cast<std::uint8_t>( nBits >> ( j * 8 ) );
		}
	}
	// A version 4 (random) UUID, as dmx::formatGuid lays the bytes out.
	guid[ 7 ] = static_cast<std::uint8_t>( ( guid[ 7 ] & 0x0f ) | 0x40 );
	guid[ 8 ] = static_cast<std::uint8_t>( ( guid[ 8 ] & 0x3f ) | 0x80 );
	return guid;
}

//-----------------------------------------------------------------------------
// Purpose: A name not used by any system yet: base, base_2, base_3...
//-----------------------------------------------------------------------------
std::string UniqueSystemName( const Document_t &document, std::string_view szBase )
{
	std::unordered_set<std::string> used;
	for ( const CElement &element : document.m_Elements )
	{
		if ( element.m_szType == SYSTEM_TYPE )
		{
			used.insert( Lower( element.m_szName ) );
		}
	}
	std::string szName{ szBase.empty() ? std::string_view{ "new_system" } : szBase };
	for ( int n = 2; used.contains( Lower( szName ) ); n++ )
	{
		szName = std::format( "{}_{}", szBase.empty() ? std::string_view{ "new_system" } : szBase, n );
	}
	return szName;
}

//-----------------------------------------------------------------------------
// Purpose: An empty particle file: a root with an empty system list, binary encoding 2, format pcf
//          1.
//-----------------------------------------------------------------------------
Document_t EmptyFile()
{
	Document_t document;
	document.m_szEncoding = "binary";
	document.m_nEncodingVersion = 2;
	document.m_szFormat = "pcf";
	document.m_nFormatVersion = 1;
	document.m_Elements.push_back( CElement{ "DmElement", "untitled", NewGuid(), { { std::string{ DEFINITIONS }, AttrType_t::ATTR_TYPE_ELEMENT_ARRAY, std::vector<CElementRef>{} } } } );
	return document;
}

//-----------------------------------------------------------------------------
// Purpose: Sets an attribute, adding it when missing.
//-----------------------------------------------------------------------------
void SetAttribute( CElement &element, std::string_view szName, AttrType_t nType, dmx::Value_t value )
{
	if ( Attribute_t *pAttribute = element.Find( szName ) )
	{
		pAttribute->m_nType = nType;
		pAttribute->m_Value = std::move( value );
		return;
	}
	element.m_Attributes.push_back( { std::string{ szName }, nType, std::move( value ) } );
}

//-----------------------------------------------------------------------------
// Purpose: A new system at the end of the root's list, with the schema's properties (or a basic
//          set when
//          the schema is empty).
//-----------------------------------------------------------------------------
std::int32_t AddSystem( Document_t &document, std::string_view szName, const CSchema &schema )
{
	CElement system{ std::string{ SYSTEM_TYPE }, std::string{ szName }, NewGuid(), {} };
	const std::vector<AttributeInfo_t> attributes = schema.GetSystemAttributes().empty() ? BasicSystemAttributes() : schema.GetSystemAttributes();
	for ( const AttributeInfo_t &attribute : attributes )
	{
		system.m_Attributes.push_back( { attribute.m_szName, attribute.m_nType, attribute.m_Value } );
	}
	// The most used material is some effect's own texture.
	SetAttribute( system, "material", AttrType_t::ATTR_TYPE_STRING, std::string{ NEW_SYSTEM_MATERIAL } );
	// Every system has its lists, even when the schema's files had none of one.
	for ( std::string_view szList : CATEGORY_ATTRIBUTES )
	{
		EnsureRefList( system, szList );
	}
	EnsureRefList( system, CHILDREN );
	const std::int32_t nIndex = Append( document, std::move( system ) );
	EnsureRefList( document.m_Elements.front(), DEFINITIONS ).push_back( { nIndex, {} } );
	return nIndex;
}

//-----------------------------------------------------------------------------
// Purpose: A copy of a system with its own copies of operators and child links (child systems are
//          shared).
//-----------------------------------------------------------------------------
std::int32_t DuplicateSystem( Document_t &document, std::int32_t nSystem, std::string_view szName )
{
	if ( !Valid( document, nSystem ) )
	{
		return -1;
	}
	CElement copy = At( document, nSystem );
	copy.m_szName = std::string{ szName };
	copy.m_Id = NewGuid();
	// Its own operators and child links, so editing the copy leaves the original alone.
	for ( Attribute_t &attribute : copy.m_Attributes )
	{
		if ( !CategoryOf( attribute.m_szName ) && attribute.m_szName != CHILDREN )
		{
			continue;
		}
		if ( auto *pRefs = std::get_if<std::vector<CElementRef>>( &attribute.m_Value ) )
		{
			for ( CElementRef &ref : *pRefs )
			{
				if ( Valid( document, ref.m_nIndex ) )
				{
					CElement element = At( document, ref.m_nIndex );
					element.m_Id = NewGuid();
					ref.m_nIndex = Append( document, std::move( element ) );
				}
			}
		}
	}
	const std::int32_t nIndex = Append( document, std::move( copy ) );
	auto &list = EnsureRefList( document.m_Elements.front(), DEFINITIONS );
	const auto original = std::ranges::find( list, nSystem, &CElementRef::m_nIndex );
	list.insert( original == list.end() ? list.end() : original + 1, CElementRef{ nIndex, {} } );
	return nIndex;
}

//-----------------------------------------------------------------------------
// Purpose: Takes a system out of the root's list and out of every child link pointing at it.
//-----------------------------------------------------------------------------
void RemoveSystem( Document_t &document, std::int32_t nSystem )
{
	if ( document.m_Elements.empty() )
	{
		return;
	}
	auto &list = EnsureRefList( document.m_Elements.front(), DEFINITIONS );
	std::erase_if( list, [ nSystem ]( const CElementRef &ref ) { return ref.m_nIndex == nSystem; } );
	for ( CElement &element : document.m_Elements )
	{
		if ( element.m_szType != SYSTEM_TYPE )
		{
			continue;
		}
		if ( auto *pLinks = RefList( element, CHILDREN ) )
		{
			std::erase_if( *pLinks, [ & ]( const CElementRef &ref ) {
				if ( !Valid( document, ref.m_nIndex ) )
				{
					return false;
				}
				const Attribute_t *pChild = At( document, ref.m_nIndex ).Find( "child" );
				const CElementRef *pTarget = pChild ? std::get_if<CElementRef>( &pChild->m_Value ) : nullptr;
				return pTarget && pTarget->m_nIndex == nSystem;
			} );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Renames a system and everything naming it in this file: child links and
//          cull_replacement_definition.
//-----------------------------------------------------------------------------
void RenameSystem( Document_t &document, std::int32_t nSystem, std::string_view szName )
{
	if ( !Valid( document, nSystem ) )
	{
		return;
	}
	const std::string szOld = At( document, nSystem ).m_szName;
	At( document, nSystem ).m_szName = std::string{ szName };
	for ( CElement &element : document.m_Elements )
	{
		if ( element.m_szType == CHILD_TYPE )
		{
			const Attribute_t *pChild = element.Find( "child" );
			const CElementRef *pTarget = pChild ? std::get_if<CElementRef>( &pChild->m_Value ) : nullptr;
			if ( pTarget && pTarget->m_nIndex == nSystem )
			{
				element.m_szName = std::string{ szName };
			}
		}
		else if ( element.m_szType == SYSTEM_TYPE && !szOld.empty() && StringAttribute( element, "cull_replacement_definition" ) == szOld )
		{
			SetAttribute( element, "cull_replacement_definition", AttrType_t::ATTR_TYPE_STRING, std::string{ szName } );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Adds an operator with the function's attributes at position (the end when nullopt).
//-----------------------------------------------------------------------------
std::int32_t AddOperator( Document_t &document, std::int32_t nSystem, Category_t nCategory, const CFunctionInfo &function, std::optional<std::size_t> position )
{
	if ( !Valid( document, nSystem ) )
	{
		return -1;
	}
	CElement op{ std::string{ OPERATOR_TYPE }, function.m_szName, NewGuid(), {} };
	for ( const AttributeInfo_t &attribute : function.m_Attributes )
	{
		op.m_Attributes.push_back( { attribute.m_szName, attribute.m_nType, attribute.m_Value } );
	}
	SetAttribute( op, FUNCTION_NAME, AttrType_t::ATTR_TYPE_STRING, function.m_szName );
	const std::int32_t nIndex = Append( document, std::move( op ) );
	InsertRef( EnsureRefList( At( document, nSystem ), AttributeName( nCategory ) ), nIndex, position );
	return nIndex;
}

//-----------------------------------------------------------------------------
// Purpose: Puts a control point somewhere with a "Set Control Point Positions" operator: moves
//          the slots of an existing one, or adds one whose four slots all place the point.
//-----------------------------------------------------------------------------
std::int32_t PlaceControlPoint( Document_t &document, std::int32_t nSystem, int nIndex, dmx::Vec3_t vecLocation, std::int32_t nOperator, int nSlot,
	const CSchema &schema )
{
	const auto slotName = []( std::size_t nWhich, std::string_view szWhat ) { return std::string{ CONTROL_POINT_SLOTS[ nWhich ] } + " Control Point " + std::string{ szWhat }; };
	if ( nOperator < 0 )
	{
		if ( !Valid( document, nSystem ) )
		{
			return -1;
		}
		CFunctionInfo function;
		if ( const CFunctionInfo *pKnown = schema.Function( Category_t::CATEGORY_OPERATORS, SET_CONTROL_POINTS ) )
		{
			function = *pKnown;
		}
		else
		{
			// Without the game's files: the settings Valve's editor writes for it.
			function.m_szName = SET_CONTROL_POINTS;
			for ( std::string_view szCommon : COMMON_OPERATOR_ATTRIBUTES )
			{
				function.m_Attributes.push_back( { std::string{ szCommon }, AttrType_t::ATTR_TYPE_FLOAT, 0.0f } );
			}
			for ( std::size_t i = 0; i < CONTROL_POINT_SLOTS.size(); i++ )
			{
				function.m_Attributes.push_back( { slotName( i, "Number" ), AttrType_t::ATTR_TYPE_INT, std::int32_t{ 0 } } );
				function.m_Attributes.push_back( { slotName( i, "Parent" ), AttrType_t::ATTR_TYPE_INT, std::int32_t{ 0 } } );
				function.m_Attributes.push_back( { slotName( i, "Location" ), AttrType_t::ATTR_TYPE_VECTOR3, dmx::Vec3_t{} } );
			}
			function.m_Attributes.push_back( { "Set positions in world space", AttrType_t::ATTR_TYPE_BOOL, false } );
			function.m_Attributes.push_back( { "Control Point to offset positions from", AttrType_t::ATTR_TYPE_INT, std::int32_t{ 0 } } );
		}
		nOperator = AddOperator( document, nSystem, Category_t::CATEGORY_OPERATORS, function, 0 );
		CElement &op = At( document, nOperator );
		for ( std::size_t i = 0; i < CONTROL_POINT_SLOTS.size(); i++ )
		{
			SetAttribute( op, slotName( i, "Number" ), AttrType_t::ATTR_TYPE_INT, std::int32_t{ nIndex } );
			SetAttribute( op, slotName( i, "Parent" ), AttrType_t::ATTR_TYPE_INT, std::int32_t{ 0 } );
			SetAttribute( op, slotName( i, "Location" ), AttrType_t::ATTR_TYPE_VECTOR3, vecLocation );
		}
		SetAttribute( op, "Set positions in world space", AttrType_t::ATTR_TYPE_BOOL, false );
		SetAttribute( op, "Control Point to offset positions from", AttrType_t::ATTR_TYPE_INT, std::int32_t{ 0 } );
		return nOperator;
	}
	if ( !Valid( document, nOperator ) )
	{
		return -1;
	}
	CElement &op = At( document, nOperator );
	if ( nSlot >= 0 && nSlot < static_cast<int>( CONTROL_POINT_SLOTS.size() ) )
	{
		SetAttribute( op, slotName( static_cast<std::size_t>( nSlot ), "Number" ), AttrType_t::ATTR_TYPE_INT, std::int32_t{ nIndex } );
	}
	for ( std::size_t i = 0; i < CONTROL_POINT_SLOTS.size(); i++ )
	{
		const Attribute_t *pNumber = op.Find( slotName( i, "Number" ) );
		const auto *pValue = pNumber ? std::get_if<std::int32_t>( &pNumber->m_Value ) : nullptr;
		if ( pValue && *pValue == nIndex )
		{
			SetAttribute( op, slotName( i, "Location" ), AttrType_t::ATTR_TYPE_VECTOR3, vecLocation );
		}
	}
	return nOperator;
}

//-----------------------------------------------------------------------------
// Purpose: Copies an element with no references (an operator) into a system's list at position.
//-----------------------------------------------------------------------------
std::int32_t InsertCopy( Document_t &document, std::int32_t nSystem, std::string_view szAttribute, const CElement &element, std::optional<std::size_t> position )
{
	if ( !Valid( document, nSystem ) )
	{
		return -1;
	}
	CElement copy = element;
	copy.m_Id = NewGuid();
	const std::int32_t nIndex = Append( document, std::move( copy ) );
	InsertRef( EnsureRefList( At( document, nSystem ), szAttribute ), nIndex, position );
	return nIndex;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::int32_t AddChild( Document_t &document, std::int32_t nParent, std::int32_t nChild, float flDelay )
{
	if ( !Valid( document, nParent ) || !Valid( document, nChild ) )
	{
		return -1;
	}
	CElement link{ std::string{ CHILD_TYPE }, At( document, nChild ).m_szName, NewGuid(), {} };
	link.m_Attributes.push_back( { "child", AttrType_t::ATTR_TYPE_ELEMENT, CElementRef{ nChild, {} } } );
	link.m_Attributes.push_back( { "delay", AttrType_t::ATTR_TYPE_FLOAT, flDelay } );
	const std::int32_t nIndex = Append( document, std::move( link ) );
	EnsureRefList( At( document, nParent ), CHILDREN ).push_back( { nIndex, {} } );
	return nIndex;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool RemoveReference( Document_t &document, std::int32_t nElement, std::string_view szAttribute, std::size_t nPosition )
{
	if ( !Valid( document, nElement ) )
	{
		return false;
	}
	auto *pList = RefList( At( document, nElement ), szAttribute );
	if ( !pList || nPosition >= pList->size() )
	{
		return false;
	}
	pList->erase( pList->begin() + static_cast<std::ptrdiff_t>( nPosition ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool MoveReference( Document_t &document, std::int32_t nElement, std::string_view szAttribute, std::size_t nFrom, std::size_t nTo )
{
	if ( !Valid( document, nElement ) )
	{
		return false;
	}
	auto *pList = RefList( At( document, nElement ), szAttribute );
	if ( !pList || nFrom >= pList->size() || nTo >= pList->size() || nFrom == nTo )
	{
		return false;
	}
	const CElementRef moved = ( *pList )[ nFrom ];
	pList->erase( pList->begin() + static_cast<std::ptrdiff_t>( nFrom ) );
	pList->insert( pList->begin() + static_cast<std::ptrdiff_t>( nTo ), moved );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: The document without unreferenced elements, indices renumbered. Unchanged when every
//          element is
//          reachable from the root.
//-----------------------------------------------------------------------------
Document_t Compact( const Document_t &document )
{
	if ( document.m_Elements.empty() )
	{
		return document;
	}
	const std::vector<bool> keep = Reachable( document, { 0 } );
	if ( std::ranges::all_of( keep, std::identity{} ) )
	{
		return document;
	}
	Document_t out = document;
	std::vector<std::int32_t> mapping;
	out.m_Elements = CopyMarked( document, keep, mapping );
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Copy and paste between files. Extract() makes a small particle file holding the
//          systems, their
//          operators and every system they spawn; paste() adds such a file's systems. A pasted
//          child system
//          whose name already exists links to the existing one; a pasted top-level system whose
//          name exists
//          is renamed. Returns the pasted top-level systems.
//-----------------------------------------------------------------------------
Document_t Extract( const Document_t &document, std::span<const std::int32_t> picked )
{
	Document_t out = EmptyFile();
	out.m_nEncodingVersion = document.m_nEncodingVersion;
	out.m_szFormat = document.m_szFormat;
	out.m_nFormatVersion = document.m_nFormatVersion;
	std::vector<std::int32_t> start;
	for ( std::int32_t nSystem : picked )
	{
		if ( Valid( document, nSystem ) && At( document, nSystem ).m_szType == SYSTEM_TYPE )
		{
			start.push_back( nSystem );
		}
	}
	std::vector<bool> keep = Reachable( document, start );
	if ( !document.m_Elements.empty() )
	{
		keep[ 0 ] = false; // the source's root and its whole list stay behind
	}
	std::vector<std::int32_t> mapping;
	std::vector<CElement> copied = CopyMarked( document, keep, mapping );
	auto &list = std::get<std::vector<CElementRef>>( out.m_Elements.front().m_Attributes.front().m_Value );
	for ( std::int32_t nSystem : start )
	{
		list.push_back( { mapping[ static_cast<std::size_t>( nSystem ) ] + 1, {} } );
	}
	for ( CElement &element : copied )
	{
		ForEachRef( element, []( CElementRef &ref ) {
			if ( ref.m_nIndex >= 0 )
			{
				ref.m_nIndex++; // after the new root
			}
		} );
		out.m_Elements.push_back( std::move( element ) );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::int32_t> Paste( Document_t &document, const Document_t &clip )
{
	std::vector<std::int32_t> pasted;
	if ( !IsParticleFile( clip ) || !IsParticleFile( document ) )
	{
		return pasted;
	}
	const std::vector<std::int32_t> top = References( clip.m_Elements.front(), DEFINITIONS );
	// A child system the file already has (by name) is linked to rather than copied.
	std::vector<std::int32_t> mapping( clip.m_Elements.size(), CElementRef::NONE );
	for ( std::int32_t nSystem : Systems( clip ) )
	{
		if ( std::ranges::find( top, nSystem ) == top.end() )
		{
			if ( const auto existing = FindSystem( document, At( clip, nSystem ).m_szName ) )
			{
				mapping[ static_cast<std::size_t>( nSystem ) ] = *existing;
			}
		}
	}
	std::vector<std::int32_t> added;
	for ( std::size_t i = 1; i < clip.m_Elements.size(); i++ )
	{
		if ( mapping[ i ] == CElementRef::NONE )
		{
			CElement element = clip.m_Elements[ i ];
			element.m_Id = NewGuid();
			mapping[ i ] = Append( document, std::move( element ) );
			added.push_back( mapping[ i ] );
		}
	}
	for ( std::int32_t nIndex : added )
	{
		ForEachRef( At( document, nIndex ), [ &mapping ]( CElementRef &ref ) {
			if ( ref.m_nIndex >= 0 )
			{
				ref.m_nIndex = static_cast<std::size_t>( ref.m_nIndex ) < mapping.size() ? mapping[ static_cast<std::size_t>( ref.m_nIndex ) ] : CElementRef::NONE;
			}
		} );
	}
	auto &list = EnsureRefList( document.m_Elements.front(), DEFINITIONS );
	for ( std::int32_t nSystem : top )
	{
		const std::int32_t nIndex = mapping[ static_cast<std::size_t>( nSystem ) ];
		const std::string szName = At( document, nIndex ).m_szName;
		At( document, nIndex ).m_szName.clear(); // so it doesn't count as taken
		RenameSystem( document, nIndex, UniqueSystemName( document, szName ) );
		list.push_back( { nIndex, {} } );
		pasted.push_back( nIndex );
	}
	// Copied child systems are listed too, as in Valve's files, so the game can find them.
	for ( std::int32_t nIndex : added )
	{
		if ( At( document, nIndex ).m_szType == SYSTEM_TYPE && std::ranges::find( list, nIndex, &CElementRef::m_nIndex ) == list.end() )
		{
			list.push_back( { nIndex, {} } );
		}
	}
	return pasted;
}

// ---------------------------------------------------------------------------------------------
// Searching

//-----------------------------------------------------------------------------
// Purpose: ---------------------------------------------------------------------------------------
//          ------
//          Searching
//-----------------------------------------------------------------------------
std::vector<SystemSummary_t> Summarize( const Document_t &document, std::string_view szFile )
{
	std::vector<SystemSummary_t> out;
	for ( std::int32_t nIndex : Systems( document ) )
	{
		const CElement &system = At( document, nIndex );
		SystemSummary_t summary;
		summary.m_szName = system.m_szName;
		summary.m_szFile = std::string{ szFile };
		summary.m_szMaterial = StringAttribute( system, "material" );
		for ( Category_t nCategory : CATEGORIES )
		{
			for ( std::int32_t nOp : References( system, AttributeName( nCategory ) ) )
			{
				if ( Valid( document, nOp ) )
				{
					summary.m_Functions.emplace_back( FunctionName( At( document, nOp ) ) );
				}
			}
		}
		for ( const ChildLink_t &link : Children( document, nIndex ) )
		{
			if ( link.m_nSystem >= 0 )
			{
				summary.m_Children.push_back( At( document, link.m_nSystem ).m_szName );
			}
		}
		out.push_back( std::move( summary ) );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Systems whose name, material, operators or children contain every word of query
//          (ignoring case),
//          those matching by name first.
//-----------------------------------------------------------------------------
std::vector<SearchHit_t> Search( std::span<const SystemSummary_t> systems, std::string_view szQuery )
{
	std::vector<std::string> words;
	for ( std::size_t nAt = 0; nAt < szQuery.size(); )
	{
		const auto nStart = szQuery.find_first_not_of( " \t", nAt );
		if ( nStart == std::string_view::npos )
		{
			break;
		}
		const auto nEnd = std::min( szQuery.find_first_of( " \t", nStart ), szQuery.size() );
		words.push_back( Lower( szQuery.substr( nStart, nEnd - nStart ) ) );
		nAt = nEnd;
	}
	std::vector<SearchHit_t> byName;
	std::vector<SearchHit_t> other;
	if ( words.empty() )
	{
		return byName;
	}
	for ( const SystemSummary_t &system : systems )
	{
		const std::string szName = Lower( system.m_szName );
		// Each word must be somewhere; the hit is described by the first place that isn't the name.
		std::string szMatched;
		bool bAll = true;
		bool bOnlyName = true;
		for ( const std::string &szWord : words )
		{
			if ( szName.find( szWord ) != std::string::npos )
			{
				continue;
			}
			bOnlyName = false;
			std::string szWhere;
			if ( Lower( system.m_szMaterial ).find( szWord ) != std::string::npos )
			{
				szWhere = "material " + system.m_szMaterial;
			}
			for ( const std::string &f : system.m_Functions )
			{
				if ( szWhere.empty() && Lower( f ).find( szWord ) != std::string::npos )
				{
					szWhere = f;
				}
			}
			for ( const std::string &szChild : system.m_Children )
			{
				if ( szWhere.empty() && Lower( szChild ).find( szWord ) != std::string::npos )
				{
					szWhere = "spawns " + szChild;
				}
			}
			if ( Lower( system.m_szFile ).find( szWord ) != std::string::npos && szWhere.empty() )
			{
				szWhere = "in " + system.m_szFile;
			}
			if ( szWhere.empty() )
			{
				bAll = false;
				break;
			}
			szMatched = szMatched.empty() ? szWhere : szMatched;
		}
		if ( bAll )
		{
			( bOnlyName ? byName : other ).push_back( { &system, bOnlyName ? "name" : szMatched } );
		}
	}
	byName.insert( byName.end(), other.begin(), other.end() );
	return byName;
}

// ---------------------------------------------------------------------------------------------
// Checks

//-----------------------------------------------------------------------------
// Purpose: "effects/fire_cloud1.vmt" or "effects/fire_cloud1" ->
//          "materials/effects/fire_cloud1.vmt"
//-----------------------------------------------------------------------------
std::string MaterialPath( std::string_view szMaterial )
{
	std::string szPath = vfs::NormalizePath( szMaterial );
	if ( !szPath.ends_with( ".vmt" ) )
	{
		szPath += ".vmt";
	}
	return szPath.starts_with( "materials/" ) ? szPath : "materials/" + szPath;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<Issue_t> Lint( const Document_t &document, const LintContext_t &context )
{
	std::vector<Issue_t> issues;
	const auto report = [ &issues ]( Issue_t::Severity_t nSeverity, std::string szMessage, std::int32_t nSystem = -1, std::int32_t nElement = -1 ) {
		issues.push_back( { nSeverity, std::move( szMessage ), nSystem, nElement } );
	};
	if ( !IsParticleFile( document ) )
	{
		report( Issue_t::Severity_t::SEVERITY_ERROR, "This DMX file holds no particle systems (its root has no particleSystemDefinitions list)." );
		return issues;
	}
	if ( context.m_ListedInManifest == false )
	{
		report( Issue_t::Severity_t::SEVERITY_WARNING, std::format( "{} doesn't list this file, so the game only loads it for maps that ask for it.", MANIFEST_PATH ) );
	}

	const std::vector<std::int32_t> all = Systems( document );
	const std::vector<std::int32_t> listed = References( document.m_Elements.front(), DEFINITIONS );
	const bool bHaveGame = context.m_pGame && !context.m_pGame->Empty();
	std::unordered_map<std::string, int> names;
	for ( std::int32_t nIndex : all )
	{
		names[ Lower( At( document, nIndex ).m_szName ) ]++;
	}

	for ( std::int32_t nIndex : all )
	{
		const CElement &system = At( document, nIndex );
		const std::string &szName = system.m_szName;
		if ( szName.empty() )
		{
			report( Issue_t::Severity_t::SEVERITY_ERROR, "A system has no name; the game finds systems by name.", nIndex );
		}
		else if ( names[ Lower( szName ) ] > 1 )
		{
			report( Issue_t::Severity_t::SEVERITY_WARNING, std::format( "{} is defined more than once in this file; only one of them is used.", szName ), nIndex );
		}
		if ( std::ranges::find( listed, nIndex ) == listed.end() )
		{
			report( Issue_t::Severity_t::SEVERITY_WARNING, std::format( "{} is only a child here, not in the file's list of systems, so the game may not load it.", szName ), nIndex );
		}

		// Children
		for ( const ChildLink_t &link : Children( document, nIndex ) )
		{
			if ( link.m_nSystem < 0 )
			{
				report( Issue_t::Severity_t::SEVERITY_ERROR, std::format( "{} has a child link that points at no system.", szName ), nIndex, link.m_nLink );
			}
		}
		// A system that spawns itself, directly or further down, would never stop.
		std::vector<std::int32_t> stack;
		std::set<std::int32_t> seen;
		for ( const ChildLink_t &link : Children( document, nIndex ) )
		{
			stack.push_back( link.m_nSystem );
		}
		while ( !stack.empty() )
		{
			const std::int32_t nNext = stack.back();
			stack.pop_back();
			if ( nNext < 0 || !seen.insert( nNext ).second )
			{
				continue;
			}
			if ( nNext == nIndex )
			{
				report( Issue_t::Severity_t::SEVERITY_ERROR, std::format( "{} spawns itself through its child systems.", szName ), nIndex );
				break;
			}
			for ( const ChildLink_t &link : Children( document, nNext ) )
			{
				stack.push_back( link.m_nSystem );
			}
		}

		// What it draws
		const bool bRenders = !References( system, AttributeName( Category_t::CATEGORY_RENDERERS ) ).empty();
		const std::string szMaterial = StringAttribute( system, "material" );
		if ( bRenders && szMaterial.empty() )
		{
			report( Issue_t::Severity_t::SEVERITY_WARNING, std::format( "{} has renderers but no material.", szName ), nIndex );
		}
		else if ( bRenders && context.m_pfnFileExists && !context.m_pfnFileExists( MaterialPath( szMaterial ) ) )
		{
			report( Issue_t::Severity_t::SEVERITY_WARNING, std::format( "{}'s material {} isn't in the game.", szName, szMaterial ), nIndex );
		}
		if ( bRenders && References( system, AttributeName( Category_t::CATEGORY_EMITTERS ) ).empty() && IntAttribute( system, "initial_particles", 0 ) <= 0 )
		{
			report( Issue_t::Severity_t::SEVERITY_INFO, std::format( "{} has no emitter and no initial particles, so it shows nothing by itself.", szName ), nIndex );
		}

		const std::string szReplacement = StringAttribute( system, "cull_replacement_definition" );
		if ( !szReplacement.empty() && !names.contains( Lower( szReplacement ) ) && !( bHaveGame && context.m_pGame->GetSystemFiles().contains( Lower( szReplacement ) ) ) )
		{
			report( Issue_t::Severity_t::SEVERITY_WARNING, std::format( "{}'s cull replacement {} doesn't exist.", szName, szReplacement ), nIndex );
		}
		if ( bHaveGame )
		{
			const auto &files = context.m_pGame->GetSystemFiles();
			if ( const auto it = files.find( Lower( szName ) ); it != files.end() && vfs::NormalizePath( it->second ) != vfs::NormalizePath( context.m_szPath ) )
			{
				report( Issue_t::Severity_t::SEVERITY_INFO, std::format( "{} is also defined in {}; which one the game uses depends on load order.", szName, it->second ), nIndex );
			}
		}

		// Operators
		for ( Category_t nCategory : CATEGORIES )
		{
			for ( std::int32_t nOp : References( system, AttributeName( nCategory ) ) )
			{
				if ( !Valid( document, nOp ) )
				{
					continue;
				}
				const CElement &element = At( document, nOp );
				const std::string_view szFunction = FunctionName( element );
				if ( szFunction.empty() )
				{
					report( Issue_t::Severity_t::SEVERITY_ERROR, std::format( "{} has an operator without a functionName.", szName ), nIndex, nOp );
					continue;
				}
				if ( !bHaveGame )
				{
					continue;
				}
				const CFunctionInfo *pKnown = context.m_pGame->Function( nCategory, szFunction );
				if ( !pKnown )
				{
					const auto elsewhere = context.m_pGame->CategoriesOf( szFunction );
					if ( elsewhere.empty() )
					{
						report( Issue_t::Severity_t::SEVERITY_WARNING, std::format( "{}: none of the game's particle files use \"{}\"; it may not exist in this game.", szName, szFunction ), nIndex, nOp );
					}
					else
					{
						report( Issue_t::Severity_t::SEVERITY_WARNING, std::format( "{}: \"{}\" is one of the {}, not the {}.", szName, szFunction, AttributeName( elsewhere.front() ), AttributeName( nCategory ) ), nIndex, nOp );
					}
					continue;
				}
				for ( const Attribute_t &attribute : element.m_Attributes )
				{
					const AttributeInfo_t *pExpected = pKnown->Attribute( attribute.m_szName );
					if ( pExpected && pExpected->m_nType != attribute.m_nType )
					{
						report( Issue_t::Severity_t::SEVERITY_WARNING, std::format( "{}: \"{}\" {} is a {} here but a {} in the game's files.", szName, szFunction, attribute.m_szName, dmx::TypeName( attribute.m_nType ), dmx::TypeName( pExpected->m_nType ) ), nIndex, nOp );
					}
				}
			}
		}
	}
	return issues;
}

//-----------------------------------------------------------------------------
// Purpose: The files a manifest lists, normalized and without the '!'.
//-----------------------------------------------------------------------------
std::vector<std::string> ManifestFiles( const kv1::CDocument &manifest )
{
	std::vector<std::string> files;
	for ( const kv1::CNode &root : manifest.m_Roots )
	{
		if ( !root.IsBlock() )
		{
			continue;
		}
		for ( const kv1::CNode &child : root.GetChildren() )
		{
			if ( child.IsPair() && kv1::EqualsIgnoreCase( child.GetKey(), "file" ) )
			{
				const std::string szValue = child.GetValue();
				files.push_back( vfs::NormalizePath( std::string_view{ szValue }.substr( szValue.starts_with( '!' ) ? 1 : 0 ) ) );
			}
		}
		break;
	}
	return files;
}

//-----------------------------------------------------------------------------
// Purpose: Adds path to the manifest's block, creating the block in an empty manifest. Returns
//          false if it
//          was already listed.
//-----------------------------------------------------------------------------
std::string MapManifestPath( std::string_view szMap )
{
	return "maps/" + std::string{ szMap } + "_particles.txt";
}

//-----------------------------------------------------------------------------
// Purpose: Adds a file to a manifest's block.
//-----------------------------------------------------------------------------
bool AddToManifest( kv1::CDocument &manifest, std::string_view szPath, bool bPrecache )
{
	const auto listed = ManifestFiles( manifest );
	if ( std::ranges::find( listed, vfs::NormalizePath( szPath ) ) != listed.end() )
	{
		return false;
	}
	auto block = std::ranges::find_if( manifest.m_Roots, &kv1::CNode::IsBlock );
	if ( block == manifest.m_Roots.end() )
	{
		manifest.AppendRoot( kv1::CNode::MakeBlock( "particles_manifest" ) );
		block = std::ranges::find_if( manifest.m_Roots, &kv1::CNode::IsBlock );
	}
	block->AppendChild( kv1::CNode::MakePair( "file", bPrecache ? "!" + std::string{ szPath } : std::string{ szPath } ) );
	return true;
}

} // namespace xv::particles
