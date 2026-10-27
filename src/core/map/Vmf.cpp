//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Vmf.h. Block and key names follow the VDC's VMF article; Hammer++'s additions
//          (vertices_plus, the *_plus blocks) are read where they matter and kept otherwise.
//
//===========================================================================//
#include "Vmf.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <tuple>
#include <unordered_map>

#include "kv1/Kv1.h"
#include "kv1/Lexer.h"
#include "map/Io.h"
#include "map/VmfValues.h"
#include "util/Numbers.h"
#include "util/Scan.h"

namespace xv::map
{

namespace
{

using kv1::Token_t;
using kv1::TokenKind_t;
using Severity_t = Diagnostic_t::Severity_t;

constexpr int MAX_DEPTH = 64;
constexpr std::size_t MAX_SIDES = 1024;    // beyond this a solid is junk, not a brush
constexpr std::size_t MANY_SIDES = 128;    // vbsp's own limit
constexpr std::size_t MAX_PLUS_VERTICES = 64;
constexpr double COORDINATE_LIMIT = 16384.0;

//-----------------------------------------------------------------------------
// Purpose: Case-insensitive comparison with a lower-case name, quick for the many keys that
//          differ in length.
//-----------------------------------------------------------------------------
bool Is( std::string_view szText, std::string_view szLowerName )
{
	if ( szText.size() != szLowerName.size() )
	{
		return false;
	}
	for ( std::size_t i = 0; i < szText.size(); i++ )
	{
		char ch = szText[ i ];
		if ( ch >= 'A' && ch <= 'Z' )
		{
			ch = static_cast<char>( ch - 'A' + 'a' );
		}
		if ( ch != szLowerName[ i ] )
		{
			return false;
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Whether every value is a number a map can hold.
//-----------------------------------------------------------------------------
bool IsFinite( const Point_t &p )
{
	return std::isfinite( p[ 0 ] ) && std::isfinite( p[ 1 ] ) && std::isfinite( p[ 2 ] );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsInBounds( const Point_t &p )
{
	return std::abs( p[ 0 ] ) <= COORDINATE_LIMIT && std::abs( p[ 1 ] ) <= COORDINATE_LIMIT && std::abs( p[ 2 ] ) <= COORDINATE_LIMIT;
}

class CReader
{
public:
	CReader( CMap &map, const ReadOptions_t &options )
		: m_Map( map ),
		  m_Options( options ),
		  m_Lexer( map.GetText() )
	{
	}

	void Read();

private:
	// Spans are 32-bit; ReadVmf refuses texts that don't fit.
	static Span_t SpanOf( std::size_t nBegin, std::size_t nEnd )
	{
		return { static_cast<std::uint32_t>( nBegin ), static_cast<std::uint32_t>( nEnd - nBegin ) };
	}
	static Span_t PairSpan( const Token_t &key, const Token_t &value )
	{
		return SpanOf( key.m_nOffset, value.m_nOffset + value.m_szRaw.size() );
	}

	void Report( std::size_t nOffset, Severity_t nSeverity, std::string szMessage, int nObjectId = 0 );
	bool Next( Token_t &token );

	template <typename OnPair, typename OnBlock>
	std::size_t ReadBody( std::size_t nOpen, int nDepth, OnPair &&onPair, OnBlock &&onBlock );
	std::size_t SkipBody( std::size_t nOpen );

	std::size_t ReadVersionInfo( std::size_t nOpen, int nDepth );
	std::size_t ReadVisgroups( std::size_t nOpen, int nDepth, int nParent );
	std::size_t ReadViewSettings( std::size_t nOpen, int nDepth );
	std::size_t ReadEntity( const Token_t &key, int nDepth, bool bWorld, bool bInHidden );
	std::size_t ReadSolid( const Token_t &key, int nDepth, int nEntity, bool bInHidden );
	std::size_t ReadSide( const Token_t &key, int nDepth, Side_t &side );
	std::size_t ReadDisp( const Token_t &key, int nDepth, Side_t &side );
	std::size_t ReadEditor( const Token_t &key, int nDepth, EditorInfo_t &editor );
	std::size_t ReadGroup( const Token_t &key, int nDepth );
	std::size_t ReadCameras( std::size_t nOpen, int nDepth );
	std::size_t ReadCordon( std::size_t nOpen, int nDepth, Cordon_t &cordon );
	std::size_t ReadCordons( std::size_t nOpen, int nDepth );
	std::uint32_t Intern( std::string_view szSpelling );
	void ReadRows( std::string_view szBlock, const std::vector<std::pair<std::size_t, std::string_view>> &rows, std::size_t nRows, std::size_t nPerRow, std::vector<double> &out, std::size_t nOffset, int nSideId );

	CMap &m_Map;
	const ReadOptions_t &m_Options;
	kv1::CLexer m_Lexer;
	bool m_bHaveWorld = false;
	std::unordered_map<std::string_view, std::uint32_t> m_Spellings;
	std::unordered_map<std::string, std::uint32_t> m_MaterialIndex;
	std::vector<double> m_Numbers; // reused for rows
	std::vector<Side_t> m_Sides;           // the solid being read's, reused
	std::vector<Point_t> m_PlusVertices;   // the side being read's Hammer++ vertices, reused
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CReader::Report( std::size_t nOffset, Severity_t nSeverity, std::string szMessage, int nObjectId )
{
	if ( m_Map.m_Diagnostics.size() >= m_Options.m_nMaxDiagnostics )
	{
		m_Map.m_nMoreDiagnostics++;
		return;
	}
	Diagnostic_t diagnostic;
	diagnostic.m_nSeverity = nSeverity;
	diagnostic.m_nOffset = nOffset;
	diagnostic.m_nObjectId = nObjectId;
	diagnostic.m_szMessage = std::move( szMessage );
	m_Map.m_Diagnostics.push_back( std::move( diagnostic ) );
}

//-----------------------------------------------------------------------------
// Purpose: The next token, skipping the conditions KeyValues allows and maps never use.
//-----------------------------------------------------------------------------
bool CReader::Next( Token_t &token )
{
	while ( m_Lexer.Next( token ) )
	{
		if ( token.m_bUnterminated )
		{
			Report( token.m_nOffset, Severity_t::SEVERITY_ERROR, "string is never closed" );
		}
		if ( token.m_nKind != TokenKind_t::TOKEN_KIND_CONDITIONAL )
		{
			return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Reads a block's contents up to its closing brace (its '{' at nOpen was just read).
//          onPair(key, value) sees each pair; onBlock(key, depth) each block inside, and must
//          read it to its end, returning where that is. Returns the offset past the '}'.
//-----------------------------------------------------------------------------
template <typename OnPair, typename OnBlock>
std::size_t CReader::ReadBody( std::size_t nOpen, int nDepth, OnPair &&onPair, OnBlock &&onBlock )
{
	Token_t token;
	while ( Next( token ) )
	{
		if ( token.m_nKind == TokenKind_t::TOKEN_KIND_CLOSE_BRACE )
		{
			return token.m_nOffset + 1;
		}
		if ( token.m_nKind == TokenKind_t::TOKEN_KIND_OPEN_BRACE )
		{
			Report( token.m_nOffset, Severity_t::SEVERITY_ERROR, "block without a name" );
			SkipBody( token.m_nOffset );
			continue;
		}
		const Token_t key = token;
		if ( !Next( token ) )
		{
			Report( key.m_nOffset, Severity_t::SEVERITY_ERROR, "key has no value" );
			break;
		}
		if ( token.m_nKind == TokenKind_t::TOKEN_KIND_OPEN_BRACE )
		{
			if ( nDepth + 1 > MAX_DEPTH )
			{
				Report( key.m_nOffset, Severity_t::SEVERITY_ERROR, "blocks nested too deeply" );
				SkipBody( token.m_nOffset );
				continue;
			}
			onBlock( key, nDepth + 1 );
			continue;
		}
		if ( token.m_nKind == TokenKind_t::TOKEN_KIND_CLOSE_BRACE )
		{
			Report( key.m_nOffset, Severity_t::SEVERITY_ERROR, "key has no value" );
			return token.m_nOffset + 1;
		}
		onPair( key, token );
	}
	Report( nOpen, Severity_t::SEVERITY_ERROR, "block is never closed" );
	return m_Map.GetText().size();
}

//-----------------------------------------------------------------------------
// Purpose: Passes over a block's contents. Returns the offset past its '}'.
//-----------------------------------------------------------------------------
std::size_t CReader::SkipBody( std::size_t nOpen )
{
	int nLevel = 1;
	Token_t token;
	while ( Next( token ) )
	{
		if ( token.m_nKind == TokenKind_t::TOKEN_KIND_OPEN_BRACE )
		{
			nLevel++;
		}
		else if ( token.m_nKind == TokenKind_t::TOKEN_KIND_CLOSE_BRACE && --nLevel == 0 )
		{
			return token.m_nOffset + 1;
		}
	}
	Report( nOpen, Severity_t::SEVERITY_ERROR, "block is never closed" );
	return m_Map.GetText().size();
}

//-----------------------------------------------------------------------------
// Purpose: The material's index, adding it the first time a spelling of it is met.
//-----------------------------------------------------------------------------
std::uint32_t CReader::Intern( std::string_view szSpelling )
{
	if ( const auto it = m_Spellings.find( szSpelling ); it != m_Spellings.end() )
	{
		return it->second;
	}
	std::string szName = NormalizeMaterial( szSpelling );
	auto [ it, bAdded ] = m_MaterialIndex.try_emplace( szName, static_cast<std::uint32_t>( m_Map.m_Materials.size() ) );
	if ( bAdded )
	{
		m_Map.m_Materials.push_back( std::move( szName ) );
	}
	m_Spellings.emplace( szSpelling, it->second );
	return it->second;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::size_t CReader::ReadVersionInfo( std::size_t nOpen, int nDepth )
{
	VersionInfo_t &info = m_Map.m_VersionInfo;
	return ReadBody(
		nOpen, nDepth,
		[ & ]( const Token_t &key, const Token_t &value ) {
			const std::string_view szKey = key.GetText();
			const int nValue = ParseNumber<int>( value.GetText() ).value_or( 0 );
			if ( Is( szKey, "editorversion" ) )
			{
				info.m_nEditorVersion = nValue;
			}
			else if ( Is( szKey, "editorbuild" ) )
			{
				info.m_nEditorBuild = nValue;
			}
			else if ( Is( szKey, "mapversion" ) )
			{
				info.m_nMapVersion = nValue;
			}
			else if ( Is( szKey, "formatversion" ) )
			{
				info.m_nFormatVersion = nValue;
			}
			else if ( Is( szKey, "prefab" ) )
			{
				info.m_bPrefab = nValue != 0;
			}
		},
		[ & ]( const Token_t &, int ) { return SkipBody( m_Lexer.GetOffset() - 1 ); } );
}

//-----------------------------------------------------------------------------
// Purpose: A "visgroups" block or a "visgroup" inside one, with its nested visgroups.
//-----------------------------------------------------------------------------
std::size_t CReader::ReadVisgroups( std::size_t nOpen, int nDepth, int nParent )
{
	return ReadBody(
		nOpen, nDepth,
		[ & ]( const Token_t &key, const Token_t &value ) {
			if ( nParent < 0 )
			{
				return; // pairs directly in "visgroups" mean nothing
			}
			Visgroup_t &group = m_Map.m_Visgroups[ static_cast<std::size_t>( nParent ) ];
			const std::string_view szKey = key.GetText();
			if ( Is( szKey, "name" ) )
			{
				group.m_szName = value.GetText();
			}
			else if ( Is( szKey, "visgroupid" ) )
			{
				group.m_nId = ParseNumber<int>( value.GetText() ).value_or( 0 );
			}
			else if ( Is( szKey, "color" ) )
			{
				group.m_Color = ParseColor( value.GetText() ).value_or( Color_t{} );
			}
		},
		[ & ]( const Token_t &key, int nInner ) {
			if ( !Is( key.GetText(), "visgroup" ) )
			{
				return SkipBody( m_Lexer.GetOffset() - 1 );
			}
			const auto nIndex = static_cast<int>( m_Map.m_Visgroups.size() );
			Visgroup_t group;
			group.m_nParent = nParent;
			m_Map.m_Visgroups.push_back( group );
			const std::size_t nEnd = ReadVisgroups( m_Lexer.GetOffset() - 1, nInner, nIndex );
			m_Map.m_Visgroups[ static_cast<std::size_t>( nIndex ) ].m_Span = SpanOf( key.m_nOffset, nEnd );
			return nEnd;
		} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::size_t CReader::ReadViewSettings( std::size_t nOpen, int nDepth )
{
	ViewSettings_t &settings = m_Map.m_ViewSettings;
	return ReadBody(
		nOpen, nDepth,
		[ & ]( const Token_t &key, const Token_t &value ) {
			const std::string_view szKey = key.GetText();
			const auto nValue = ParseNumber<int>( value.GetText() );
			if ( !nValue )
			{
				return;
			}
			if ( Is( szKey, "bsnaptogrid" ) )
			{
				settings.m_bSnapToGrid = *nValue != 0;
			}
			else if ( Is( szKey, "bshowgrid" ) )
			{
				settings.m_bShowGrid = *nValue != 0;
			}
			else if ( Is( szKey, "bshowlogicalgrid" ) )
			{
				settings.m_bShowLogicalGrid = *nValue != 0;
			}
			else if ( Is( szKey, "ngridspacing" ) )
			{
				settings.m_nGridSpacing = std::clamp( *nValue, 1, 16384 );
			}
			else if ( Is( szKey, "bshow3dgrid" ) )
			{
				settings.m_bShow3DGrid = *nValue != 0;
			}
		},
		[ & ]( const Token_t &, int ) { return SkipBody( m_Lexer.GetOffset() - 1 ); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::size_t CReader::ReadEditor( const Token_t &key, int nDepth, EditorInfo_t &editor )
{
	const std::size_t nEnd = ReadBody(
		m_Lexer.GetOffset() - 1, nDepth,
		[ & ]( const Token_t &name, const Token_t &value ) {
			const std::string_view szKey = name.GetText();
			const std::string_view szValue = value.GetText();
			if ( Is( szKey, "color" ) )
			{
				if ( const auto color = ParseColor( szValue ) )
				{
					editor.m_Color = *color;
					editor.m_bHasColor = true;
				}
			}
			else if ( Is( szKey, "visgroupid" ) )
			{
				if ( const auto nId = ParseNumber<int>( szValue ) )
				{
					editor.m_Visgroups.push_back( *nId );
				}
			}
			else if ( Is( szKey, "groupid" ) )
			{
				editor.m_nGroup = ParseNumber<int>( szValue ).value_or( 0 );
			}
			else if ( Is( szKey, "visgroupshown" ) )
			{
				editor.m_bVisgroupShown = ParseNumber<int>( szValue ).value_or( 1 ) != 0;
			}
			else if ( Is( szKey, "visgroupautoshown" ) )
			{
				editor.m_bAutoShown = ParseNumber<int>( szValue ).value_or( 1 ) != 0;
			}
			else if ( Is( szKey, "comments" ) )
			{
				editor.m_szComments = szValue;
			}
		},
		[ & ]( const Token_t &, int ) { return SkipBody( m_Lexer.GetOffset() - 1 ); } );
	editor.m_Span = SpanOf( key.m_nOffset, nEnd );
	return nEnd;
}

//-----------------------------------------------------------------------------
// Purpose: The world (bWorld) or an entity, with its solids, outputs and editor notes.
//-----------------------------------------------------------------------------
std::size_t CReader::ReadEntity( const Token_t &key, int nDepth, bool bWorld, bool bInHidden )
{
	int nEntity = -1;
	if ( bWorld )
	{
		if ( m_bHaveWorld )
		{
			// Hammer merges extra worlds into the first; the later keys win.
			Report( key.m_nOffset, Severity_t::SEVERITY_WARNING, "a second world block, merged into the first" );
		}
	}
	else
	{
		nEntity = static_cast<int>( m_Map.m_Entities.size() );
		m_Map.m_Entities.emplace_back();
		m_Map.m_Entities.back().m_bInHidden = bInHidden;
	}
	auto entity = [ & ]() -> Entity_t & { return nEntity < 0 ? m_Map.m_World : m_Map.m_Entities[ static_cast<std::size_t>( nEntity ) ]; };

	const std::size_t nEnd = ReadBody(
		m_Lexer.GetOffset() - 1, nDepth,
		[ & ]( const Token_t &name, const Token_t &value ) {
			if ( Is( name.GetText(), "id" ) )
			{
				if ( !bWorld || !m_bHaveWorld )
				{
					entity().m_nId = ParseNumber<int>( value.GetText() ).value_or( 0 );
				}
				return;
			}
			entity().m_Keys.push_back( { name.GetText(), value.GetText(), PairSpan( name, value ) } );
		},
		[ & ]( const Token_t &name, int nInner ) -> std::size_t {
			const std::string_view szName = name.GetText();
			if ( Is( szName, "solid" ) )
			{
				return ReadSolid( name, nInner, nEntity, bInHidden );
			}
			if ( Is( szName, "hidden" ) )
			{
				return ReadBody(
					m_Lexer.GetOffset() - 1, nInner,
					[]( const Token_t &, const Token_t & ) {},
					[ & ]( const Token_t &inner, int nHidden ) -> std::size_t {
						if ( Is( inner.GetText(), "solid" ) )
						{
							return ReadSolid( inner, nHidden, nEntity, true );
						}
						return SkipBody( m_Lexer.GetOffset() - 1 );
					} );
			}
			if ( Is( szName, "connections" ) )
			{
				const std::size_t nConnectionsAt = name.m_nOffset;
				const std::size_t nConnectionsEnd = ReadBody(
					m_Lexer.GetOffset() - 1, nInner,
					[ & ]( const Token_t &output, const Token_t &value ) {
						Connection_t connection = ParseConnection( output.GetText(), value.GetText() );
						connection.m_Span = PairSpan( output, value );
						if ( connection.m_bMalformed )
						{
							Report( output.m_nOffset, Severity_t::SEVERITY_WARNING, "an output that doesn't split into target, input, parameter, delay and times", entity().m_nId );
						}
						entity().m_Connections.push_back( connection );
					},
					[ & ]( const Token_t &, int ) { return SkipBody( m_Lexer.GetOffset() - 1 ); } );
				entity().m_ConnectionsSpan = SpanOf( nConnectionsAt, nConnectionsEnd );
				return nConnectionsEnd;
			}
			if ( Is( szName, "editor" ) )
			{
				return ReadEditor( name, nInner, entity().m_Editor );
			}
			if ( bWorld && Is( szName, "group" ) )
			{
				return ReadGroup( name, nInner );
			}
			return SkipBody( m_Lexer.GetOffset() - 1 );
		} );
	if ( !bWorld || !m_bHaveWorld )
	{
		entity().m_Span = SpanOf( key.m_nOffset, nEnd );
	}
	if ( bWorld )
	{
		m_bHaveWorld = true;
	}
	return nEnd;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::size_t CReader::ReadGroup( const Token_t &key, int nDepth )
{
	const std::size_t nIndex = m_Map.m_Groups.size();
	m_Map.m_Groups.emplace_back();
	const std::size_t nEnd = ReadBody(
		m_Lexer.GetOffset() - 1, nDepth,
		[ & ]( const Token_t &name, const Token_t &value ) {
			if ( Is( name.GetText(), "id" ) )
			{
				m_Map.m_Groups[ nIndex ].m_nId = ParseNumber<int>( value.GetText() ).value_or( 0 );
			}
		},
		[ & ]( const Token_t &name, int nInner ) {
			if ( Is( name.GetText(), "editor" ) )
			{
				return ReadEditor( name, nInner, m_Map.m_Groups[ nIndex ].m_Editor );
			}
			return SkipBody( m_Lexer.GetOffset() - 1 );
		} );
	m_Map.m_Groups[ nIndex ].m_Span = SpanOf( key.m_nOffset, nEnd );
	return nEnd;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::size_t CReader::ReadSolid( const Token_t &key, int nDepth, int nEntity, bool bInHidden )
{
	Solid_t solid;
	solid.m_nEntity = nEntity;
	solid.m_bInHidden = bInHidden;
	bool bTooMany = false;
	const std::size_t nEnd = ReadBody(
		m_Lexer.GetOffset() - 1, nDepth,
		[ & ]( const Token_t &name, const Token_t &value ) {
			if ( Is( name.GetText(), "id" ) )
			{
				solid.m_nId = ParseNumber<int>( value.GetText() ).value_or( 0 );
			}
		},
		[ & ]( const Token_t &name, int nInner ) -> std::size_t {
			const std::string_view szName = name.GetText();
			if ( Is( szName, "side" ) )
			{
				if ( m_Sides.size() >= MAX_SIDES )
				{
					bTooMany = true;
					return SkipBody( m_Lexer.GetOffset() - 1 );
				}
				m_Sides.emplace_back();
				return ReadSide( name, nInner, m_Sides.back() );
			}
			if ( Is( szName, "editor" ) )
			{
				return ReadEditor( name, nInner, solid.m_Editor );
			}
			return SkipBody( m_Lexer.GetOffset() - 1 );
		} );
	// Read into the reader's own list, then moved over at their number: each solid's sides are
	// one allocation of the size they need.
	solid.m_Sides.reserve( m_Sides.size() );
	std::ranges::move( m_Sides, std::back_inserter( solid.m_Sides ) );
	m_Sides.clear();
	solid.m_Span = SpanOf( key.m_nOffset, nEnd );
	if ( bTooMany )
	{
		Report( key.m_nOffset, Severity_t::SEVERITY_ERROR, "more than " + std::to_string( MAX_SIDES ) + " sides; the rest are left out", solid.m_nId );
	}
	else if ( solid.m_Sides.size() > MANY_SIDES )
	{
		Report( key.m_nOffset, Severity_t::SEVERITY_WARNING, "more than " + std::to_string( MANY_SIDES ) + " sides, which vbsp refuses", solid.m_nId );
	}
	const auto nIndex = static_cast<std::uint32_t>( m_Map.m_Solids.size() );
	m_Map.m_Solids.push_back( std::move( solid ) );
	( nEntity < 0 ? m_Map.m_World : m_Map.m_Entities[ static_cast<std::size_t>( nEntity ) ] ).m_Solids.push_back( nIndex );
	return nEnd;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::size_t CReader::ReadSide( const Token_t &key, int nDepth, Side_t &side )
{
	std::size_t nPlaneOffset = key.m_nOffset;
	bool bHasMaterial = false;
	const std::size_t nEnd = ReadBody(
		m_Lexer.GetOffset() - 1, nDepth,
		[ & ]( const Token_t &name, const Token_t &value ) {
			const std::string_view szKey = name.GetText();
			const std::string_view szValue = value.GetText();
			if ( Is( szKey, "id" ) )
			{
				side.m_nId = ParseNumber<int>( szValue ).value_or( 0 );
			}
			else if ( Is( szKey, "plane" ) )
			{
				nPlaneOffset = value.m_nOffset;
				if ( const auto plane = ParsePlane( szValue ) )
				{
					side.m_Plane = *plane;
					side.m_bPlaneRead = true;
				}
			}
			else if ( Is( szKey, "material" ) )
			{
				side.m_nMaterial = Intern( szValue );
				side.m_szMaterial = szValue;
				bHasMaterial = true;
			}
			else if ( Is( szKey, "uaxis" ) || Is( szKey, "vaxis" ) )
			{
				const auto axis = ParseAxis( szValue );
				if ( !axis )
				{
					Report( value.m_nOffset, Severity_t::SEVERITY_WARNING, "a texture axis that isn't \"[x y z shift] scale\"", side.m_nId );
					return;
				}
				( szKey[ 0 ] == 'u' || szKey[ 0 ] == 'U' ? side.m_UAxis : side.m_VAxis ) = *axis;
			}
			else if ( Is( szKey, "rotation" ) )
			{
				side.m_flRotation = ParseNumber<double>( szValue ).value_or( 0.0 );
			}
			else if ( Is( szKey, "lightmapscale" ) )
			{
				side.m_nLightmapScale = ParseNumber<int>( szValue ).value_or( 16 );
			}
			else if ( Is( szKey, "smoothing_groups" ) )
			{
				side.m_nSmoothingGroups = static_cast<std::uint32_t>( ParseNumber<std::int64_t>( szValue ).value_or( 0 ) );
			}
		},
		[ & ]( const Token_t &name, int nInner ) -> std::size_t {
			const std::string_view szName = name.GetText();
			if ( Is( szName, "dispinfo" ) )
			{
				return ReadDisp( name, nInner, side );
			}
			if ( Is( szName, "vertices_plus" ) )
			{
				bool bBad = false;
				std::vector<Point_t> &points = m_PlusVertices;
				points.clear();
				const std::size_t nPlusEnd = ReadBody(
					m_Lexer.GetOffset() - 1, nInner,
					[ & ]( const Token_t &vertex, const Token_t &value ) {
						if ( !Is( vertex.GetText(), "v" ) )
						{
							return;
						}
						const auto p = ParsePoint( value.GetText() );
						if ( !p || !IsFinite( *p ) || points.size() >= MAX_PLUS_VERTICES )
						{
							bBad = true;
							return;
						}
						points.push_back( *p );
					},
					[ & ]( const Token_t &, int ) { return SkipBody( m_Lexer.GetOffset() - 1 ); } );
				if ( bBad )
				{
					Report( name.m_nOffset, Severity_t::SEVERITY_WARNING, "Hammer++ vertices that can't be read; the plane is used instead", side.m_nId );
				}
				else
				{
					side.m_PlusVertices.assign( points.begin(), points.end() );
				}
				return nPlusEnd;
			}
			return SkipBody( m_Lexer.GetOffset() - 1 );
		} );
	side.m_Span = SpanOf( key.m_nOffset, nEnd );
	if ( !side.m_bPlaneRead )
	{
		Report( nPlaneOffset, Severity_t::SEVERITY_ERROR, "a side without a readable plane", side.m_nId );
	}
	else if ( !std::ranges::all_of( side.m_Plane, IsFinite ) )
	{
		side.m_bPlaneRead = false;
		Report( nPlaneOffset, Severity_t::SEVERITY_ERROR, "a plane with a point that isn't a number", side.m_nId );
	}
	else if ( !std::ranges::all_of( side.m_Plane, IsInBounds ) )
	{
		Report( nPlaneOffset, Severity_t::SEVERITY_WARNING, "a plane point outside the map's +/-16384 units", side.m_nId );
	}
	if ( !bHasMaterial )
	{
		side.m_nMaterial = Intern( "" );
	}
	return nEnd;
}

//-----------------------------------------------------------------------------
// Purpose: Fills out with a grid's rows (row0, row1...), each nPerRow numbers. Missing rows
//          stay zero; rows that don't read are reported.
//-----------------------------------------------------------------------------
void CReader::ReadRows( std::string_view szBlock, const std::vector<std::pair<std::size_t, std::string_view>> &rows, std::size_t nRows, std::size_t nPerRow, std::vector<double> &out, std::size_t nOffset, int nSideId )
{
	out.assign( nRows * nPerRow, 0.0 );
	bool bBad = false;
	for ( const auto &[ nRow, szText ] : rows )
	{
		if ( nRow >= nRows || !ParseNumbers( szText, nPerRow, m_Numbers ) )
		{
			bBad = true;
			continue;
		}
		std::copy( m_Numbers.begin(), m_Numbers.end(), out.begin() + static_cast<std::ptrdiff_t>( nRow * nPerRow ) );
	}
	if ( bBad )
	{
		Report( nOffset, Severity_t::SEVERITY_WARNING, "displacement " + std::string{ szBlock } + " rows that don't fit its power", nSideId );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::size_t CReader::ReadDisp( const Token_t &key, int nDepth, Side_t &side )
{
	auto pDisp = std::make_unique<DispInfo_t>();
	DispInfo_t &disp = *pDisp;
	// Rows are kept as text until the block ends, as nothing says the power comes first.
	std::array<std::vector<std::pair<std::size_t, std::string_view>>, 7> rows;
	constexpr std::array<std::string_view, 7> BLOCKS = { "normals", "distances", "offsets", "offset_normals", "alphas", "triangle_tags", "allowed_verts" };
	bool bPowerRead = false;
	const std::size_t nEnd = ReadBody(
		m_Lexer.GetOffset() - 1, nDepth,
		[ & ]( const Token_t &name, const Token_t &value ) {
			const std::string_view szKey = name.GetText();
			const std::string_view szValue = value.GetText();
			if ( Is( szKey, "power" ) )
			{
				const auto nPower = ParseNumber<int>( szValue );
				bPowerRead = nPower.has_value();
				disp.m_nPower = nPower.value_or( 0 );
			}
			else if ( Is( szKey, "startposition" ) )
			{
				disp.m_Start = ParsePoint( szValue ).value_or( Point_t{} );
			}
			else if ( Is( szKey, "elevation" ) )
			{
				disp.m_flElevation = ParseNumber<double>( szValue ).value_or( 0.0 );
			}
			else if ( Is( szKey, "subdiv" ) )
			{
				disp.m_bSubdivided = ParseNumber<int>( szValue ).value_or( 0 ) != 0;
			}
			else if ( Is( szKey, "flags" ) )
			{
				disp.m_nFlags = ParseNumber<int>( szValue ).value_or( 0 );
			}
		},
		[ & ]( const Token_t &name, int nInner ) -> std::size_t {
			const std::string_view szName = name.GetText();
			for ( std::size_t i = 0; i < BLOCKS.size(); i++ )
			{
				if ( !Is( szName, BLOCKS[ i ] ) )
				{
					continue;
				}
				return ReadBody(
					m_Lexer.GetOffset() - 1, nInner,
					[ &, i ]( const Token_t &row, const Token_t &value ) {
						std::string_view szRow = row.GetText();
						if ( szRow.size() > 3 && Is( szRow.substr( 0, 3 ), "row" ) )
						{
							szRow.remove_prefix( 3 );
						}
						if ( const auto nRow = ParseNumber<std::size_t>( szRow ) )
						{
							rows[ i ].emplace_back( *nRow, value.GetText() );
						}
					},
					[ & ]( const Token_t &, int ) { return SkipBody( m_Lexer.GetOffset() - 1 ); } );
			}
			return SkipBody( m_Lexer.GetOffset() - 1 );
		} );
	disp.m_Span = SpanOf( key.m_nOffset, nEnd );

	if ( !bPowerRead )
	{
		// Hammer reads a displacement without a power as power 4.
		disp.m_nPower = 4;
	}
	if ( disp.m_nPower < 2 || disp.m_nPower > 4 )
	{
		Report( key.m_nOffset, Severity_t::SEVERITY_ERROR, "displacement power " + std::to_string( disp.m_nPower ) + " (only 2, 3 and 4 work); drawn flat", side.m_nId );
		side.m_pDisp = std::move( pDisp );
		return nEnd;
	}
	const auto nSize = static_cast<std::size_t>( disp.GetSize() );
	const std::size_t nSquares = nSize - 1;
	std::vector<double> values;
	auto vectors = [ & ]( std::size_t nBlock, std::vector<CVec3> &out ) {
		ReadRows( BLOCKS[ nBlock ], rows[ nBlock ], nSize, nSize * 3, values, key.m_nOffset, side.m_nId );
		out.resize( nSize * nSize );
		for ( std::size_t i = 0; i < out.size(); i++ )
		{
			out[ i ] = { static_cast<float>( values[ i * 3 ] ), static_cast<float>( values[ i * 3 + 1 ] ), static_cast<float>( values[ i * 3 + 2 ] ) };
		}
	};
	auto scalars = [ & ]( std::size_t nBlock, std::vector<float> &out ) {
		ReadRows( BLOCKS[ nBlock ], rows[ nBlock ], nSize, nSize, values, key.m_nOffset, side.m_nId );
		out.assign( values.begin(), values.end() );
	};
	vectors( 0, disp.m_Normals );
	scalars( 1, disp.m_Distances );
	vectors( 2, disp.m_Offsets );
	vectors( 3, disp.m_OffsetNormals );
	scalars( 4, disp.m_Alphas );
	ReadRows( BLOCKS[ 5 ], rows[ 5 ], nSquares, nSquares * 2, values, key.m_nOffset, side.m_nId );
	disp.m_TriangleTags.assign( values.begin(), values.end() );
	for ( const auto &[ nRow, szText ] : rows[ 6 ] )
	{
		if ( nRow == 10 && ParseNumbers( szText, 10, m_Numbers ) )
		{
			disp.m_AllowedVerts.assign( m_Numbers.begin(), m_Numbers.end() );
		}
	}
	disp.m_bValid = true;
	side.m_pDisp = std::move( pDisp );
	return nEnd;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::size_t CReader::ReadCameras( std::size_t nOpen, int nDepth )
{
	return ReadBody(
		nOpen, nDepth,
		[ & ]( const Token_t &key, const Token_t &value ) {
			if ( Is( key.GetText(), "activecamera" ) )
			{
				m_Map.m_nActiveCamera = ParseNumber<int>( value.GetText() ).value_or( -1 );
			}
		},
		[ & ]( const Token_t &key, int nInner ) {
			if ( !Is( key.GetText(), "camera" ) )
			{
				return SkipBody( m_Lexer.GetOffset() - 1 );
			}
			Camera_t camera;
			const std::size_t nEnd = ReadBody(
				m_Lexer.GetOffset() - 1, nInner,
				[ & ]( const Token_t &name, const Token_t &value ) {
					if ( Is( name.GetText(), "position" ) )
					{
						camera.m_Position = ParsePoint( value.GetText() ).value_or( Point_t{} );
					}
					else if ( Is( name.GetText(), "look" ) )
					{
						camera.m_Look = ParsePoint( value.GetText() ).value_or( Point_t{} );
					}
				},
				[ & ]( const Token_t &, int ) { return SkipBody( m_Lexer.GetOffset() - 1 ); } );
			m_Map.m_Cameras.push_back( camera );
			return nEnd;
		} );
}

//-----------------------------------------------------------------------------
// Purpose: One cordon: the old single "cordon" block, or one inside "cordons" with its boxes.
//-----------------------------------------------------------------------------
std::size_t CReader::ReadCordon( std::size_t nOpen, int nDepth, Cordon_t &cordon )
{
	Box_t single;
	bool bSingle = false;
	const std::size_t nEnd = ReadBody(
		nOpen, nDepth,
		[ & ]( const Token_t &key, const Token_t &value ) {
			const std::string_view szKey = key.GetText();
			if ( Is( szKey, "name" ) )
			{
				cordon.m_szName = value.GetText();
			}
			else if ( Is( szKey, "active" ) )
			{
				cordon.m_bActive = ParseNumber<int>( value.GetText() ).value_or( 0 ) != 0;
			}
			else if ( Is( szKey, "mins" ) || Is( szKey, "maxs" ) )
			{
				if ( const auto p = ParsePoint( value.GetText() ) )
				{
					( szKey[ 1 ] == 'i' || szKey[ 1 ] == 'I' ? single.m_Mins : single.m_Maxs ) = *p;
					bSingle = true;
				}
			}
		},
		[ & ]( const Token_t &key, int nInner ) {
			if ( !Is( key.GetText(), "box" ) )
			{
				return SkipBody( m_Lexer.GetOffset() - 1 );
			}
			Box_t box;
			const std::size_t nBoxEnd = ReadBody(
				m_Lexer.GetOffset() - 1, nInner,
				[ & ]( const Token_t &name, const Token_t &value ) {
					const auto p = ParsePoint( value.GetText() );
					if ( p && Is( name.GetText(), "mins" ) )
					{
						box.m_Mins = *p;
					}
					else if ( p && Is( name.GetText(), "maxs" ) )
					{
						box.m_Maxs = *p;
					}
				},
				[ & ]( const Token_t &, int ) { return SkipBody( m_Lexer.GetOffset() - 1 ); } );
			cordon.m_Boxes.push_back( box );
			return nBoxEnd;
		} );
	if ( bSingle )
	{
		cordon.m_Boxes.push_back( single );
	}
	return nEnd;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::size_t CReader::ReadCordons( std::size_t nOpen, int nDepth )
{
	return ReadBody(
		nOpen, nDepth,
		[ & ]( const Token_t &key, const Token_t &value ) {
			if ( Is( key.GetText(), "active" ) )
			{
				m_Map.m_bCordonsActive = ParseNumber<int>( value.GetText() ).value_or( 0 ) != 0;
			}
		},
		[ & ]( const Token_t &key, int nInner ) {
			if ( !Is( key.GetText(), "cordon" ) )
			{
				return SkipBody( m_Lexer.GetOffset() - 1 );
			}
			Cordon_t cordon;
			const std::size_t nEnd = ReadCordon( m_Lexer.GetOffset() - 1, nInner, cordon );
			m_Map.m_Cordons.push_back( std::move( cordon ) );
			return nEnd;
		} );
}

//-----------------------------------------------------------------------------
// Purpose: The top level: a list of named blocks.
//-----------------------------------------------------------------------------
void CReader::Read()
{
	Token_t token;
	while ( Next( token ) )
	{
		if ( token.m_nKind == TokenKind_t::TOKEN_KIND_CLOSE_BRACE )
		{
			Report( token.m_nOffset, Severity_t::SEVERITY_ERROR, "a '}' that closes nothing" );
			continue;
		}
		if ( token.m_nKind == TokenKind_t::TOKEN_KIND_OPEN_BRACE )
		{
			Report( token.m_nOffset, Severity_t::SEVERITY_ERROR, "block without a name" );
			const std::size_t nEnd = SkipBody( token.m_nOffset );
			m_Map.m_TopBlocks.push_back( SpanOf( token.m_nOffset, nEnd ) );
			continue;
		}
		const Token_t key = token;
		if ( !Next( token ) )
		{
			Report( key.m_nOffset, Severity_t::SEVERITY_ERROR, "key has no value" );
			break;
		}
		if ( token.m_nKind != TokenKind_t::TOKEN_KIND_OPEN_BRACE )
		{
			Report( key.m_nOffset, Severity_t::SEVERITY_WARNING, "a key outside every block" );
			if ( token.m_nKind == TokenKind_t::TOKEN_KIND_CLOSE_BRACE )
			{
				Report( token.m_nOffset, Severity_t::SEVERITY_ERROR, "a '}' that closes nothing" );
			}
			continue;
		}
		const std::size_t nOpen = token.m_nOffset;
		const std::string_view szName = key.GetText();
		std::size_t nEnd = 0;
		if ( Is( szName, "versioninfo" ) )
		{
			nEnd = ReadVersionInfo( nOpen, 1 );
		}
		else if ( Is( szName, "visgroups" ) )
		{
			nEnd = ReadVisgroups( nOpen, 1, -1 );
		}
		else if ( Is( szName, "viewsettings" ) )
		{
			nEnd = ReadViewSettings( nOpen, 1 );
		}
		else if ( Is( szName, "world" ) )
		{
			nEnd = ReadEntity( key, 1, true, false );
		}
		else if ( Is( szName, "entity" ) )
		{
			nEnd = ReadEntity( key, 1, false, false );
		}
		else if ( Is( szName, "hidden" ) )
		{
			nEnd = ReadBody(
				nOpen, 1,
				[]( const Token_t &, const Token_t & ) {},
				[ & ]( const Token_t &inner, int nInner ) -> std::size_t {
					if ( Is( inner.GetText(), "entity" ) )
					{
						return ReadEntity( inner, nInner, false, true );
					}
					if ( Is( inner.GetText(), "solid" ) )
					{
						return ReadSolid( inner, nInner, -1, true );
					}
					return SkipBody( m_Lexer.GetOffset() - 1 );
				} );
		}
		else if ( Is( szName, "cameras" ) )
		{
			nEnd = ReadCameras( nOpen, 1 );
		}
		else if ( Is( szName, "cordon" ) )
		{
			Cordon_t cordon;
			cordon.m_szName = "cordon";
			nEnd = ReadCordon( nOpen, 1, cordon );
			m_Map.m_bCordonsActive = m_Map.m_bCordonsActive || cordon.m_bActive;
			m_Map.m_Cordons.push_back( std::move( cordon ) );
		}
		else if ( Is( szName, "cordons" ) )
		{
			nEnd = ReadCordons( nOpen, 1 );
		}
		else
		{
			nEnd = SkipBody( nOpen );
			m_Map.m_RawBlocks.push_back( { szName, SpanOf( key.m_nOffset, nEnd ) } );
		}
		m_Map.m_TopBlocks.push_back( SpanOf( key.m_nOffset, nEnd ) );
	}

	if ( !m_Map.m_Diagnostics.empty() )
	{
		const kv1::CLineTable lines{ m_Map.GetText() };
		for ( Diagnostic_t &diagnostic : m_Map.m_Diagnostics )
		{
			std::tie( diagnostic.m_nLine, diagnostic.m_nColumn ) = lines.LineColumn( diagnostic.m_nOffset );
		}
	}
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view Entity_t::GetValue( std::string_view szKey ) const
{
	for ( auto it = m_Keys.rbegin(); it != m_Keys.rend(); ++it )
	{
		if ( !it->m_bDeleted && kv1::EqualsIgnoreCase( it->m_szKey, szKey ) )
		{
			return it->m_szValue;
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool Entity_t::HasKey( std::string_view szKey ) const
{
	return std::ranges::any_of( m_Keys, [ szKey ]( const EntityKey_t &key ) { return !key.m_bDeleted && kv1::EqualsIgnoreCase( key.m_szKey, szKey ); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMap::HasErrors() const
{
	return std::ranges::any_of( m_Diagnostics, []( const Diagnostic_t &diagnostic ) { return diagnostic.m_nSeverity == Diagnostic_t::Severity_t::SEVERITY_ERROR; } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view CMap::Store( std::string szText )
{
	return m_pStrings->emplace_back( std::move( szText ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::uint32_t CMap::FindMaterial( std::string_view szName )
{
	std::string szNormal = NormalizeMaterial( szName );
	const auto it = std::ranges::find( m_Materials, szNormal );
	if ( it != m_Materials.end() )
	{
		return static_cast<std::uint32_t>( it - m_Materials.begin() );
	}
	m_Materials.push_back( std::move( szNormal ) );
	return static_cast<std::uint32_t>( m_Materials.size() - 1 );
}

//-----------------------------------------------------------------------------
// Purpose: The first line's ending; Hammer's own when there's no text yet.
//-----------------------------------------------------------------------------
std::string_view CMap::GetNewline() const
{
	const std::string_view szText = GetText();
	const std::size_t nAt = szText.find( '\n' );
	return nAt == std::string_view::npos || ( nAt > 0 && szText[ nAt - 1 ] == '\r' ) ? std::string_view{ "\r\n" } : std::string_view{ "\n" };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string NormalizeMaterial( std::string_view szName )
{
	std::string szOut{ szName };
	for ( char &ch : szOut )
	{
		if ( ch >= 'A' && ch <= 'Z' )
		{
			ch = static_cast<char>( ch - 'A' + 'a' );
		}
		else if ( ch == '\\' )
		{
			ch = '/';
		}
	}
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<CMap> ReadVmf( std::vector<char> text, const ReadOptions_t &options, std::string *pError )
{
	if ( text.size() >= std::numeric_limits<std::uint32_t>::max() )
	{
		if ( pError )
		{
			*pError = "the file is over 4 GB";
		}
		return std::nullopt;
	}
	CMap map;
	map.m_pText = std::make_shared<const std::vector<char>>( std::move( text ) );
	CReader{ map, options }.Read();
	if ( map.m_TopBlocks.empty() )
	{
		if ( pError )
		{
			*pError = "not a map: there are no blocks in it";
		}
		return std::nullopt;
	}
	return map;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<CMap> ReadVmfFile( const std::filesystem::path &path, const ReadOptions_t &options, std::string *pError )
{
	std::ifstream stream{ path, std::ios::binary };
	if ( !stream )
	{
		if ( pError )
		{
			*pError = "can't open the file";
		}
		return std::nullopt;
	}
	stream.seekg( 0, std::ios::end );
	const std::streamoff nSize = stream.tellg();
	stream.seekg( 0, std::ios::beg );
	if ( nSize < 0 || static_cast<std::uint64_t>( nSize ) >= std::numeric_limits<std::uint32_t>::max() )
	{
		if ( pError )
		{
			*pError = "the file is over 4 GB";
		}
		return std::nullopt;
	}
	std::vector<char> text( static_cast<std::size_t>( nSize ) );
	if ( !stream.read( text.data(), nSize ) )
	{
		if ( pError )
		{
			*pError = "can't read the file";
		}
		return std::nullopt;
	}
	return ReadVmf( std::move( text ), options, pError );
}

} // namespace xv::map
