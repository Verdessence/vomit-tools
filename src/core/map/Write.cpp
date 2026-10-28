//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Write.h.
//
//===========================================================================//
#include "Write.h"

#include <algorithm>
#include <charconv>
#include <optional>
#include <system_error>
#include <vector>

#include "kv1/Lexer.h"
#include "map/VmfValues.h"

namespace xv::map
{

namespace
{

using kv1::TokenKind_t;

//-----------------------------------------------------------------------------
// Purpose: Case-insensitive comparison with a lower-case name.
//-----------------------------------------------------------------------------
bool Is( std::string_view szText, std::string_view szLowerName )
{
	return szText.size() == szLowerName.size() && std::ranges::equal( szText, szLowerName, []( char a, char b ) { return ( a >= 'A' && a <= 'Z' ? static_cast<char>( a - 'A' + 'a' ) : a ) == b; } );
}

//-----------------------------------------------------------------------------
// Purpose: A value as it goes between quotes. A quote inside one would end it early and break
//          the file (Hammer has the same trouble), so it becomes an apostrophe.
//-----------------------------------------------------------------------------
std::string Quoted( std::string_view szValue )
{
	std::string szOut = "\"";
	for ( const char ch : szValue )
	{
		szOut += ch == '"' ? '\'' : ch;
	}
	return szOut + '"';
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string Pair( std::string_view szKey, std::string_view szValue )
{
	return Quoted( szKey ) + ' ' + Quoted( szValue );
}

// A block of a VMF's text as a tree: each key with its value's place, each block with its own.
struct Node_t
{
	std::string_view m_szName;
	std::size_t m_nAt = 0;       // where the name starts in the map's text
	std::size_t m_nEnd = 0;      // past the value, or past the closing brace
	std::size_t m_nValueAt = 0;  // a key's value, quotes included
	std::size_t m_nValueLength = 0;
	std::string_view m_szValue;  // without quotes
	bool m_bBlock = false;
	std::vector<Node_t> m_Children;

	[[nodiscard]] Span_t GetSpan() const { return { static_cast<std::uint32_t>( m_nAt ), static_cast<std::uint32_t>( m_nEnd - m_nAt ) }; }
};

//-----------------------------------------------------------------------------
// Purpose: The block at span (as the reader found it, so it is well formed) as a tree.
//-----------------------------------------------------------------------------
std::optional<Node_t> ParseTree( std::string_view szText, Span_t span )
{
	kv1::CLexer lexer{ szText.substr( span.m_nOffset, span.m_nLength ) };
	const std::size_t nBase = span.m_nOffset;
	kv1::Token_t token;
	// A block's contents, up to its closing brace.
	auto body = [ & ]( auto &self, Node_t &block, int nDepth ) -> bool {
		while ( lexer.Next( token ) )
		{
			if ( token.m_nKind == TokenKind_t::TOKEN_KIND_CLOSE_BRACE )
			{
				block.m_nEnd = nBase + token.m_nOffset + 1;
				return true;
			}
			Node_t child;
			child.m_szName = token.GetText();
			child.m_nAt = nBase + token.m_nOffset;
			if ( !lexer.Next( token ) )
			{
				return false;
			}
			if ( token.m_nKind == TokenKind_t::TOKEN_KIND_OPEN_BRACE )
			{
				child.m_bBlock = true;
				if ( nDepth > 64 || !self( self, child, nDepth + 1 ) )
				{
					return false;
				}
			}
			else
			{
				child.m_nValueAt = nBase + token.m_nOffset;
				child.m_nValueLength = token.m_szRaw.size();
				child.m_szValue = token.GetText();
				child.m_nEnd = child.m_nValueAt + child.m_nValueLength;
			}
			block.m_Children.push_back( std::move( child ) );
		}
		return false;
	};
	Node_t root;
	if ( !lexer.Next( token ) )
	{
		return std::nullopt;
	}
	root.m_szName = token.GetText();
	root.m_nAt = nBase + token.m_nOffset;
	root.m_bBlock = true;
	if ( !lexer.Next( token ) || token.m_nKind != TokenKind_t::TOKEN_KIND_OPEN_BRACE || !body( body, root, 1 ) )
	{
		return std::nullopt;
	}
	return root;
}

class CWriter
{
public:
	explicit CWriter( const CMap &map )
		: m_Map( map ),
		  m_szText( map.GetText() ),
		  m_szNewline( map.GetNewline() )
	{
	}

	[[nodiscard]] std::string Run();

	// Whole objects, as Hammer writes them (see Write.h).
	[[nodiscard]] std::string Solid( const Solid_t &solid, std::string_view szIndent ) const;
	[[nodiscard]] std::string Entity( const Entity_t &entity, std::string_view szIndent ) const;

private:
	struct Patch_t
	{
		std::size_t m_nAt = 0;
		std::size_t m_nLength = 0;
		std::string m_szText;
	};

	[[nodiscard]] std::size_t LineStart( std::size_t nAt ) const;
	[[nodiscard]] std::size_t LineEnd( std::size_t nAt ) const;
	[[nodiscard]] std::string LineIndent( std::size_t nAt ) const;
	void Replace( std::size_t nAt, std::size_t nLength, std::string szText ) { m_Patches.push_back( { nAt, nLength, std::move( szText ) } ); }
	void ReplaceValue( const Node_t &key, std::string_view szValue ) { Replace( key.m_nValueAt, key.m_nValueLength, Quoted( szValue ) ); }
	// Cuts out what span covers, with its lines when nothing else is on them.
	void Remove( Span_t span );
	// Puts whole lines in at the start of the line nAt is on, indented as szIndent.
	void InsertLines( std::size_t nAt, std::string_view szIndent, std::string_view szBlock );

	[[nodiscard]] std::string Side( const Side_t &side, std::string_view szIndent ) const;
	[[nodiscard]] std::string Disp( const DispInfo_t &disp, std::string_view szIndent ) const;
	[[nodiscard]] std::string PlusVertices( const std::vector<Point_t> &points, std::string_view szIndent ) const;
	// With the keys this doesn't know copied from the block as read (Hammer's logicalpos).
	[[nodiscard]] std::string Editor( const EditorInfo_t &editor, std::string_view szIndent, const Node_t *pOriginal ) const;
	[[nodiscard]] std::string Connections( const Entity_t &entity, std::string_view szIndent ) const;

	void PatchSolid( const Solid_t &solid );
	void PatchSide( const Side_t &side, const Node_t &node );
	void PatchDisp( const DispInfo_t &disp, const Node_t &node );
	void PatchEntity( const Entity_t &entity );
	void PatchEditor( const EditorInfo_t &editor, Span_t owner );

	const CMap &m_Map;
	std::string_view m_szText;
	std::string_view m_szNewline;
	std::vector<Patch_t> m_Patches;
};

//-----------------------------------------------------------------------------
// Purpose: Back over indentation to the line's start; nAt itself when something else is before
//          it on the line.
//-----------------------------------------------------------------------------
std::size_t CWriter::LineStart( std::size_t nAt ) const
{
	std::size_t n = nAt;
	while ( n > 0 && ( m_szText[ n - 1 ] == ' ' || m_szText[ n - 1 ] == '\t' ) )
	{
		n--;
	}
	return n == 0 || m_szText[ n - 1 ] == '\n' ? n : nAt;
}

//-----------------------------------------------------------------------------
// Purpose: On past trailing blanks and the line's ending; nAt itself when something else
//          follows on the line.
//-----------------------------------------------------------------------------
std::size_t CWriter::LineEnd( std::size_t nAt ) const
{
	std::size_t n = nAt;
	while ( n < m_szText.size() && ( m_szText[ n ] == ' ' || m_szText[ n ] == '\t' ) )
	{
		n++;
	}
	if ( n < m_szText.size() && m_szText[ n ] == '\r' )
	{
		n++;
	}
	if ( n < m_szText.size() && m_szText[ n ] == '\n' )
	{
		return n + 1;
	}
	return n == m_szText.size() ? n : nAt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string CWriter::LineIndent( std::size_t nAt ) const
{
	std::size_t nStart = nAt;
	while ( nStart > 0 && m_szText[ nStart - 1 ] != '\n' )
	{
		nStart--;
	}
	std::size_t nEnd = nStart;
	while ( nEnd < m_szText.size() && ( m_szText[ nEnd ] == ' ' || m_szText[ nEnd ] == '\t' ) )
	{
		nEnd++;
	}
	return std::string{ m_szText.substr( nStart, nEnd - nStart ) };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWriter::Remove( Span_t span )
{
	const std::size_t nStart = LineStart( span.m_nOffset );
	const std::size_t nEnd = LineEnd( span.m_nOffset + span.m_nLength );
	Replace( nStart, nEnd - nStart, {} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWriter::InsertLines( std::size_t nAt, std::string_view szIndent, std::string_view szBlock )
{
	std::size_t nLine = nAt;
	while ( nLine > 0 && m_szText[ nLine - 1 ] != '\n' )
	{
		nLine--;
	}
	Replace( nLine, 0, std::string{ szIndent } + std::string{ szBlock } + std::string{ m_szNewline } );
}

//-----------------------------------------------------------------------------
// Purpose: Hammer's order: the plane, Hammer++'s vertices, the material and its alignment, then
//          the displacement.
//-----------------------------------------------------------------------------
std::string CWriter::Side( const Side_t &side, std::string_view szIndent ) const
{
	const std::string szIn = std::string{ szIndent } + '\t';
	const std::string_view nl = m_szNewline;
	std::string sz = "side" + std::string{ nl } + std::string{ szIndent } + '{' + std::string{ nl };
	auto line = [ & ]( std::string_view szKey, std::string_view szValue ) { sz += szIn + Pair( szKey, szValue ) + std::string{ nl }; };
	line( "id", std::to_string( side.m_nId ) );
	line( "plane", FormatPlane( side.m_Plane ) );
	if ( !side.m_PlusVertices.empty() )
	{
		sz += szIn + PlusVertices( side.m_PlusVertices, szIn ) + std::string{ nl };
	}
	const std::string_view szMaterial = !side.m_szMaterial.empty() || side.m_nMaterial >= m_Map.m_Materials.size() ? side.m_szMaterial : std::string_view{ m_Map.m_Materials[ side.m_nMaterial ] };
	line( "material", szMaterial );
	line( "uaxis", FormatAxis( side.m_UAxis ) );
	line( "vaxis", FormatAxis( side.m_VAxis ) );
	line( "rotation", FormatNumber( side.m_flRotation ) );
	line( "lightmapscale", std::to_string( side.m_nLightmapScale ) );
	line( "smoothing_groups", std::to_string( side.m_nSmoothingGroups ) );
	if ( side.m_pDisp )
	{
		sz += szIn + Disp( *side.m_pDisp, szIn ) + std::string{ nl };
	}
	return sz + std::string{ szIndent } + '}';
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string CWriter::PlusVertices( const std::vector<Point_t> &points, std::string_view szIndent ) const
{
	const std::string_view nl = m_szNewline;
	std::string sz = "vertices_plus" + std::string{ nl } + std::string{ szIndent } + '{' + std::string{ nl };
	for ( const Point_t &p : points )
	{
		sz += std::string{ szIndent } + '\t' + Pair( "v", FormatPoint( p ) ) + std::string{ nl };
	}
	return sz + std::string{ szIndent } + '}';
}

//-----------------------------------------------------------------------------
// Purpose: Its keys, then its grids a row each, as Hammer writes them.
//-----------------------------------------------------------------------------
std::string CWriter::Disp( const DispInfo_t &disp, std::string_view szIndent ) const
{
	const std::string szIn = std::string{ szIndent } + '\t';
	const std::string_view nl = m_szNewline;
	std::string sz = "dispinfo" + std::string{ nl } + std::string{ szIndent } + '{' + std::string{ nl };
	auto line = [ & ]( std::string_view szKey, std::string_view szValue ) { sz += szIn + Pair( szKey, szValue ) + std::string{ nl }; };
	line( "power", std::to_string( disp.m_nPower ) );
	line( "startposition", '[' + FormatPoint( disp.m_Start ) + ']' );
	line( "flags", std::to_string( disp.m_nFlags ) );
	line( "elevation", FormatNumber( disp.m_flElevation ) );
	line( "subdiv", disp.m_bSubdivided ? "1" : "0" );
	if ( !disp.m_bValid )
	{
		return sz + std::string{ szIndent } + '}';
	}
	const auto nSize = static_cast<std::size_t>( disp.GetSize() );
	auto rows = [ & ]( std::string_view szName, std::size_t nRows, std::size_t nPerRow, auto pfnValue ) {
		sz += szIn + std::string{ szName } + std::string{ nl } + szIn + '{' + std::string{ nl };
		for ( std::size_t r = 0; r < nRows; r++ )
		{
			std::string szRow;
			for ( std::size_t k = 0; k < nPerRow; k++ )
			{
				szRow += ( k ? " " : "" ) + pfnValue( r * nPerRow + k );
			}
			sz += szIn + '\t' + Pair( "row" + std::to_string( r ), szRow ) + std::string{ nl };
		}
		sz += szIn + '}' + std::string{ nl };
	};
	auto vector = [ & ]( const std::vector<CVec3> &values ) {
		return [ &values ]( std::size_t i ) {
			const CVec3 &v = values[ i / 3 ];
			return FormatNumber( i % 3 == 0 ? v.m_flX : i % 3 == 1 ? v.m_flY
																   : v.m_flZ );
		};
	};
	auto scalar = []( const auto &values ) { return [ &values ]( std::size_t i ) { return FormatNumber( values[ i ] ); }; };
	rows( "normals", nSize, nSize * 3, vector( disp.m_Normals ) );
	rows( "distances", nSize, nSize, scalar( disp.m_Distances ) );
	rows( "offsets", nSize, nSize * 3, vector( disp.m_Offsets ) );
	rows( "offset_normals", nSize, nSize * 3, vector( disp.m_OffsetNormals ) );
	rows( "alphas", nSize, nSize, scalar( disp.m_Alphas ) );
	if ( disp.m_TriangleTags.size() == ( nSize - 1 ) * ( nSize - 1 ) * 2 )
	{
		rows( "triangle_tags", nSize - 1, ( nSize - 1 ) * 2, scalar( disp.m_TriangleTags ) );
	}
	if ( disp.m_AllowedVerts.size() == 10 )
	{
		std::string szRow;
		for ( std::size_t k = 0; k < 10; k++ )
		{
			szRow += ( k ? " " : "" ) + std::to_string( disp.m_AllowedVerts[ k ] );
		}
		sz += szIn + "allowed_verts" + std::string{ nl } + szIn + '{' + std::string{ nl } + szIn + '\t' + Pair( "10", szRow ) + std::string{ nl } + szIn + '}' + std::string{ nl };
	}
	return sz + std::string{ szIndent } + '}';
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string CWriter::Editor( const EditorInfo_t &editor, std::string_view szIndent, const Node_t *pOriginal ) const
{
	const std::string szIn = std::string{ szIndent } + '\t';
	const std::string_view nl = m_szNewline;
	std::string sz = "editor" + std::string{ nl } + std::string{ szIndent } + '{' + std::string{ nl };
	auto line = [ & ]( std::string_view szKey, std::string_view szValue ) { sz += szIn + Pair( szKey, szValue ) + std::string{ nl }; };
	if ( editor.m_bHasColor )
	{
		line( "color", FormatColor( editor.m_Color ) );
	}
	for ( const int nVisgroup : editor.m_Visgroups )
	{
		line( "visgroupid", std::to_string( nVisgroup ) );
	}
	if ( editor.m_nGroup != 0 )
	{
		line( "groupid", std::to_string( editor.m_nGroup ) );
	}
	line( "visgroupshown", editor.m_bVisgroupShown ? "1" : "0" );
	line( "visgroupautoshown", editor.m_bAutoShown ? "1" : "0" );
	if ( !editor.m_szComments.empty() )
	{
		line( "comments", editor.m_szComments );
	}
	static constexpr std::string_view KNOWN[] = { "color", "visgroupid", "groupid", "visgroupshown", "visgroupautoshown", "comments" };
	for ( std::size_t i = 0; pOriginal && i < pOriginal->m_Children.size(); i++ )
	{
		const Node_t &child = pOriginal->m_Children[ i ];
		if ( !std::ranges::any_of( KNOWN, [ & ]( std::string_view szKnown ) { return Is( child.m_szName, szKnown ); } ) )
		{
			sz += szIn + std::string{ m_szText.substr( child.m_nAt, child.m_nEnd - child.m_nAt ) } + std::string{ nl };
		}
	}
	return sz + std::string{ szIndent } + '}';
}

//-----------------------------------------------------------------------------
// Purpose: Written with the separator each one was read with (commas before Left 4 Dead 2's
//          tools, ESC after).
//-----------------------------------------------------------------------------
std::string CWriter::Connections( const Entity_t &entity, std::string_view szIndent ) const
{
	const std::string_view nl = m_szNewline;
	std::string sz = "connections" + std::string{ nl } + std::string{ szIndent } + '{' + std::string{ nl };
	for ( const Connection_t &connection : entity.m_Connections )
	{
		const std::string szSep( 1, connection.m_chSeparator );
		const std::string szValue = std::string{ connection.m_szTarget } + szSep + std::string{ connection.m_szInput } + szSep + std::string{ connection.m_szParameter } + szSep + FormatNumber( connection.m_flDelay ) + szSep + std::to_string( connection.m_nTimes );
		sz += std::string{ szIndent } + '\t' + Pair( connection.m_szOutput, szValue ) + std::string{ nl };
	}
	return sz + std::string{ szIndent } + '}';
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string CWriter::Solid( const Solid_t &solid, std::string_view szIndent ) const
{
	const std::string szIn = std::string{ szIndent } + '\t';
	const std::string_view nl = m_szNewline;
	std::string sz = "solid" + std::string{ nl } + std::string{ szIndent } + '{' + std::string{ nl };
	sz += szIn + Pair( "id", std::to_string( solid.m_nId ) ) + std::string{ nl };
	for ( const Side_t &side : solid.m_Sides )
	{
		sz += szIn + Side( side, szIn ) + std::string{ nl };
	}
	sz += szIn + Editor( solid.m_Editor, szIn, nullptr ) + std::string{ nl };
	return sz + std::string{ szIndent } + '}';
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string CWriter::Entity( const Entity_t &entity, std::string_view szIndent ) const
{
	const std::string szIn = std::string{ szIndent } + '\t';
	const std::string_view nl = m_szNewline;
	std::string sz = "entity" + std::string{ nl } + std::string{ szIndent } + '{' + std::string{ nl };
	sz += szIn + Pair( "id", std::to_string( entity.m_nId ) ) + std::string{ nl };
	for ( const EntityKey_t &key : entity.m_Keys )
	{
		if ( !key.m_bDeleted )
		{
			sz += szIn + Pair( key.m_szKey, key.m_szValue ) + std::string{ nl };
		}
	}
	if ( !entity.m_Connections.empty() )
	{
		sz += szIn + Connections( entity, szIn ) + std::string{ nl };
	}
	for ( const std::uint32_t nSolid : entity.m_Solids )
	{
		if ( nSolid < m_Map.m_Solids.size() && !m_Map.m_Solids[ nSolid ].m_bDeleted )
		{
			sz += szIn + Solid( m_Map.m_Solids[ nSolid ], szIn ) + std::string{ nl };
		}
	}
	sz += szIn + Editor( entity.m_Editor, szIn, nullptr ) + std::string{ nl };
	return sz + std::string{ szIndent } + '}';
}

//-----------------------------------------------------------------------------
// Purpose: The values that differ from the text are written again; anything else in the solid
//          stays as it was. A solid whose sides were added or taken away is written out whole.
//-----------------------------------------------------------------------------
void CWriter::PatchSolid( const Solid_t &solid )
{
	const std::optional<Node_t> tree = ParseTree( m_szText, solid.m_Span );
	if ( !tree )
	{
		Replace( solid.m_Span.m_nOffset, solid.m_Span.m_nLength, Solid( solid, LineIndent( solid.m_Span.m_nOffset ) ) );
		return;
	}
	std::vector<const Node_t *> sides;
	const Node_t *pEditor = nullptr;
	for ( const Node_t &child : tree->m_Children )
	{
		if ( child.m_bBlock && Is( child.m_szName, "side" ) )
		{
			sides.push_back( &child );
		}
		else if ( child.m_bBlock && Is( child.m_szName, "editor" ) )
		{
			pEditor = &child;
		}
	}
	if ( sides.size() != solid.m_Sides.size() )
	{
		Replace( solid.m_Span.m_nOffset, solid.m_Span.m_nLength, Solid( solid, LineIndent( solid.m_Span.m_nOffset ) ) );
		return;
	}
	for ( std::size_t i = 0; i < sides.size(); i++ )
	{
		PatchSide( solid.m_Sides[ i ], *sides[ i ] );
	}
	if ( solid.m_Editor.m_bChanged )
	{
		if ( pEditor )
		{
			Replace( pEditor->m_nAt, pEditor->m_nEnd - pEditor->m_nAt, Editor( solid.m_Editor, LineIndent( pEditor->m_nAt ), pEditor ) );
		}
		else
		{
			const std::string szIndent = LineIndent( solid.m_Span.m_nOffset ) + '\t';
			InsertLines( tree->m_nEnd - 1, szIndent, Editor( solid.m_Editor, szIndent, nullptr ) );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWriter::PatchSide( const Side_t &side, const Node_t &node )
{
	const Node_t *pPlane = nullptr;
	const Node_t *pPlus = nullptr;
	for ( const Node_t &child : node.m_Children )
	{
		if ( child.m_bBlock )
		{
			if ( Is( child.m_szName, "vertices_plus" ) )
			{
				pPlus = &child;
			}
			else if ( Is( child.m_szName, "dispinfo" ) && side.m_pDisp )
			{
				PatchDisp( *side.m_pDisp, child );
			}
			continue;
		}
		if ( Is( child.m_szName, "plane" ) )
		{
			pPlane = &child;
			if ( side.m_bPlaneRead && ParsePlane( child.m_szValue ) != std::optional{ side.m_Plane } )
			{
				ReplaceValue( child, FormatPlane( side.m_Plane ) );
			}
		}
		else if ( Is( child.m_szName, "material" ) )
		{
			if ( child.m_szValue != side.m_szMaterial )
			{
				ReplaceValue( child, side.m_szMaterial );
			}
		}
		else if ( Is( child.m_szName, "uaxis" ) || Is( child.m_szName, "vaxis" ) )
		{
			const TextureAxis_t &axis = child.m_szName[ 0 ] == 'u' || child.m_szName[ 0 ] == 'U' ? side.m_UAxis : side.m_VAxis;
			const std::optional<TextureAxis_t> read = ParseAxis( child.m_szValue );
			if ( !read || read->m_Axis != axis.m_Axis || read->m_flShift != axis.m_flShift || read->m_flScale != axis.m_flScale )
			{
				ReplaceValue( child, FormatAxis( axis ) );
			}
		}
		else if ( Is( child.m_szName, "rotation" ) )
		{
			std::vector<double> values;
			if ( !ParseNumbers( child.m_szValue, 1, values ) || values[ 0 ] != side.m_flRotation )
			{
				ReplaceValue( child, FormatNumber( side.m_flRotation ) );
			}
		}
		else if ( Is( child.m_szName, "lightmapscale" ) )
		{
			std::vector<double> values;
			if ( !ParseNumbers( child.m_szValue, 1, values ) || values[ 0 ] != side.m_nLightmapScale )
			{
				ReplaceValue( child, std::to_string( side.m_nLightmapScale ) );
			}
		}
		else if ( Is( child.m_szName, "smoothing_groups" ) )
		{
			std::vector<double> values;
			if ( !ParseNumbers( child.m_szValue, 1, values ) || values[ 0 ] != side.m_nSmoothingGroups )
			{
				ReplaceValue( child, std::to_string( side.m_nSmoothingGroups ) );
			}
		}
	}
	// Hammer++'s exact corners follow the plane: written again when they moved, taken out when
	// they are gone, put in after the plane when they are new.
	if ( pPlus )
	{
		std::vector<Point_t> read;
		for ( const Node_t &child : pPlus->m_Children )
		{
			if ( !child.m_bBlock && Is( child.m_szName, "v" ) )
			{
				read.push_back( ParsePoint( child.m_szValue ).value_or( Point_t{} ) );
			}
		}
		if ( side.m_PlusVertices.empty() )
		{
			Remove( pPlus->GetSpan() );
		}
		else if ( read != side.m_PlusVertices )
		{
			Replace( pPlus->m_nAt, pPlus->m_nEnd - pPlus->m_nAt, PlusVertices( side.m_PlusVertices, LineIndent( pPlus->m_nAt ) ) );
		}
	}
	else if ( !side.m_PlusVertices.empty() && pPlane )
	{
		const std::string szIndent = LineIndent( pPlane->m_nAt );
		InsertLines( LineEnd( pPlane->m_nEnd ), szIndent, PlusVertices( side.m_PlusVertices, szIndent ) );
	}
	// Keys Hammer always writes but a file made by hand may leave out: put in after the last
	// key when they no longer mean their defaults.
	const Node_t *pLast = nullptr;
	auto has = [ & ]( std::string_view szKey ) {
		return std::ranges::any_of( node.m_Children, [ & ]( const Node_t &child ) { return !child.m_bBlock && Is( child.m_szName, szKey ); } );
	};
	for ( const Node_t &child : node.m_Children )
	{
		pLast = child.m_bBlock ? pLast : &child;
	}
	std::vector<std::string> missing;
	if ( !has( "rotation" ) && side.m_flRotation != 0.0 )
	{
		missing.push_back( Pair( "rotation", FormatNumber( side.m_flRotation ) ) );
	}
	if ( !has( "lightmapscale" ) && side.m_nLightmapScale != 16 )
	{
		missing.push_back( Pair( "lightmapscale", std::to_string( side.m_nLightmapScale ) ) );
	}
	if ( !has( "smoothing_groups" ) && side.m_nSmoothingGroups != 0 )
	{
		missing.push_back( Pair( "smoothing_groups", std::to_string( side.m_nSmoothingGroups ) ) );
	}
	if ( pLast && !missing.empty() )
	{
		const std::string szIndent = LineIndent( pLast->m_nAt );
		std::string szBlock;
		for ( std::size_t i = 0; i < missing.size(); i++ )
		{
			szBlock += ( i > 0 ? std::string{ m_szNewline } + szIndent : std::string{} ) + missing[ i ];
		}
		InsertLines( LineEnd( pLast->m_nEnd ), szIndent, szBlock );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The keys that changed, and any grid whose numbers did.
//-----------------------------------------------------------------------------
void CWriter::PatchDisp( const DispInfo_t &disp, const Node_t &node )
{
	for ( const Node_t &child : node.m_Children )
	{
		if ( child.m_bBlock )
		{
			continue;
		}
		std::vector<double> values;
		if ( Is( child.m_szName, "startposition" ) )
		{
			if ( ParsePoint( child.m_szValue ) != std::optional{ disp.m_Start } )
			{
				ReplaceValue( child, '[' + FormatPoint( disp.m_Start ) + ']' );
			}
		}
		else if ( Is( child.m_szName, "elevation" ) )
		{
			if ( !ParseNumbers( child.m_szValue, 1, values ) || values[ 0 ] != disp.m_flElevation )
			{
				ReplaceValue( child, FormatNumber( disp.m_flElevation ) );
			}
		}
		else if ( Is( child.m_szName, "power" ) )
		{
			if ( !ParseNumbers( child.m_szValue, 1, values ) || values[ 0 ] != disp.m_nPower )
			{
				ReplaceValue( child, std::to_string( disp.m_nPower ) );
			}
		}
	}
	// The grids are only written again when their numbers changed (a displacement edit).
	if ( !disp.m_bValid )
	{
		return;
	}
	const auto nSize = static_cast<std::size_t>( disp.GetSize() );
	auto rowsDiffer = [ & ]( const Node_t &block, std::size_t nPerRow, auto pfnValue ) {
		std::vector<double> values;
		for ( const Node_t &row : block.m_Children )
		{
			std::string_view szRow = row.m_szName;
			szRow.remove_prefix( std::min<std::size_t>( 3, szRow.size() ) );
			std::size_t nRow = 0;
			if ( row.m_bBlock || std::from_chars( szRow.data(), szRow.data() + szRow.size(), nRow ).ec != std::errc{} || !ParseNumbers( row.m_szValue, nPerRow, values ) )
			{
				continue;
			}
			for ( std::size_t k = 0; k < nPerRow; k++ )
			{
				if ( static_cast<float>( values[ k ] ) != pfnValue( nRow * nPerRow + k ) )
				{
					return true;
				}
			}
		}
		return false;
	};
	auto component = []( const std::vector<CVec3> &values ) {
		return [ &values ]( std::size_t i ) {
			const std::size_t n = i / 3;
			if ( n >= values.size() )
			{
				return 0.0f;
			}
			return i % 3 == 0 ? values[ n ].m_flX : i % 3 == 1 ? values[ n ].m_flY
															   : values[ n ].m_flZ;
		};
	};
	auto scalar = []( const std::vector<float> &values ) { return [ &values ]( std::size_t i ) { return i < values.size() ? values[ i ] : 0.0f; }; };
	bool bRows = false;
	for ( const Node_t &child : node.m_Children )
	{
		if ( !child.m_bBlock )
		{
			continue;
		}
		if ( Is( child.m_szName, "normals" ) )
		{
			bRows = bRows || rowsDiffer( child, nSize * 3, component( disp.m_Normals ) );
		}
		else if ( Is( child.m_szName, "distances" ) )
		{
			bRows = bRows || rowsDiffer( child, nSize, scalar( disp.m_Distances ) );
		}
		else if ( Is( child.m_szName, "offsets" ) )
		{
			bRows = bRows || rowsDiffer( child, nSize * 3, component( disp.m_Offsets ) );
		}
		else if ( Is( child.m_szName, "offset_normals" ) )
		{
			bRows = bRows || rowsDiffer( child, nSize * 3, component( disp.m_OffsetNormals ) );
		}
		else if ( Is( child.m_szName, "alphas" ) )
		{
			bRows = bRows || rowsDiffer( child, nSize, scalar( disp.m_Alphas ) );
		}
	}
	if ( bRows )
	{
		Replace( node.m_nAt, node.m_nEnd - node.m_nAt, Disp( disp, LineIndent( node.m_nAt ) ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWriter::PatchEditor( const EditorInfo_t &editor, Span_t owner )
{
	if ( !editor.m_bChanged )
	{
		return;
	}
	if ( editor.m_Span.m_nLength > 0 )
	{
		const std::optional<Node_t> original = ParseTree( m_szText, editor.m_Span );
		Replace( editor.m_Span.m_nOffset, editor.m_Span.m_nLength, Editor( editor, LineIndent( editor.m_Span.m_nOffset ), original ? &*original : nullptr ) );
		return;
	}
	const std::string szIndent = LineIndent( owner.m_nOffset ) + '\t';
	InsertLines( owner.m_nOffset + owner.m_nLength - 1, szIndent, Editor( editor, szIndent, nullptr ) );
}

//-----------------------------------------------------------------------------
// Purpose: An entity's (or the world's) keys a key at a time, then its outputs and editor notes.
//-----------------------------------------------------------------------------
void CWriter::PatchEntity( const Entity_t &entity )
{
	const std::size_t nClose = entity.m_Span.m_nOffset + entity.m_Span.m_nLength - 1;
	std::size_t nAfterKeys = 0;
	std::string szKeyIndent = LineIndent( entity.m_Span.m_nOffset ) + '\t';
	std::string szNewKeys;
	for ( const EntityKey_t &key : entity.m_Keys )
	{
		if ( !key.IsNew() )
		{
			nAfterKeys = key.m_Span.m_nOffset + key.m_Span.m_nLength;
			szKeyIndent = LineIndent( key.m_Span.m_nOffset );
			if ( key.m_bDeleted )
			{
				Remove( key.m_Span );
			}
			else if ( key.m_bChanged )
			{
				Replace( key.m_Span.m_nOffset, key.m_Span.m_nLength, Pair( key.m_szKey, key.m_szValue ) );
			}
		}
		else if ( !key.m_bDeleted )
		{
			szNewKeys += szKeyIndent + Pair( key.m_szKey, key.m_szValue ) + std::string{ m_szNewline };
		}
	}
	if ( !szNewKeys.empty() )
	{
		if ( nAfterKeys == 0 )
		{
			// After the id, which every entity starts with: the line after the opening brace's.
			const std::optional<Node_t> tree = ParseTree( m_szText, entity.m_Span );
			const auto id = tree ? std::ranges::find_if( tree->m_Children, []( const Node_t &child ) { return !child.m_bBlock && Is( child.m_szName, "id" ); } ) : std::vector<Node_t>::const_iterator{};
			nAfterKeys = tree && id != tree->m_Children.end() ? id->m_nEnd : static_cast<std::size_t>( m_szText.find( '{', entity.m_Span.m_nOffset ) + 1 );
		}
		Replace( LineEnd( nAfterKeys ), 0, szNewKeys );
	}
	if ( entity.m_bConnectionsChanged )
	{
		const Span_t span = entity.m_ConnectionsSpan;
		if ( entity.m_Connections.empty() )
		{
			if ( span.m_nLength > 0 )
			{
				Remove( span );
			}
		}
		else if ( span.m_nLength > 0 )
		{
			Replace( span.m_nOffset, span.m_nLength, Connections( entity, LineIndent( span.m_nOffset ) ) );
		}
		else
		{
			// Before the solids and the editor notes, as Hammer has them.
			std::size_t nAt = entity.m_Editor.m_Span.m_nLength > 0 ? entity.m_Editor.m_Span.m_nOffset : nClose;
			for ( const std::uint32_t nSolid : entity.m_Solids )
			{
				if ( nSolid < m_Map.m_Solids.size() && !m_Map.m_Solids[ nSolid ].IsNew() )
				{
					nAt = std::min<std::size_t>( nAt, m_Map.m_Solids[ nSolid ].m_Span.m_nOffset );
				}
			}
			InsertLines( nAt, szKeyIndent, Connections( entity, szKeyIndent ) );
		}
	}
	PatchEditor( entity.m_Editor, entity.m_Span );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string CWriter::Run()
{
	auto deadEntity = [ & ]( int nEntity ) { return nEntity >= 0 && m_Map.m_Entities[ static_cast<std::size_t>( nEntity ) ].m_bDeleted; };
	auto newEntity = [ & ]( int nEntity ) { return nEntity >= 0 && m_Map.m_Entities[ static_cast<std::size_t>( nEntity ) ].IsNew(); };
	PatchEntity( m_Map.m_World );
	for ( const Entity_t &entity : m_Map.m_Entities )
	{
		if ( entity.IsNew() )
		{
			continue;
		}
		if ( entity.m_bDeleted )
		{
			Remove( entity.m_Span );
			continue;
		}
		PatchEntity( entity );
	}
	// New solids go before their owner's closing brace (the world's, or an entity's).
	for ( const Solid_t &solid : m_Map.m_Solids )
	{
		if ( deadEntity( solid.m_nEntity ) || newEntity( solid.m_nEntity ) )
		{
			continue;
		}
		if ( solid.IsNew() )
		{
			if ( !solid.m_bDeleted )
			{
				const Entity_t &owner = m_Map.GetOwner( solid );
				const std::string szIndent = LineIndent( owner.m_Span.m_nOffset ) + '\t';
				InsertLines( owner.m_Span.m_nOffset + owner.m_Span.m_nLength - 1, szIndent, Solid( solid, szIndent ) );
			}
			continue;
		}
		if ( solid.m_bDeleted )
		{
			Remove( solid.m_Span );
		}
		else if ( solid.m_bChanged )
		{
			PatchSolid( solid );
		}
	}
	// New entities after the last one there was (or the world).
	std::size_t nLastEntity = m_Map.m_World.m_Span.m_nOffset + m_Map.m_World.m_Span.m_nLength;
	for ( const Entity_t &entity : m_Map.m_Entities )
	{
		if ( !entity.IsNew() )
		{
			nLastEntity = std::max<std::size_t>( nLastEntity, entity.m_Span.m_nOffset + entity.m_Span.m_nLength );
		}
	}
	std::string szNewEntities;
	for ( const Entity_t &entity : m_Map.m_Entities )
	{
		if ( entity.IsNew() && !entity.m_bDeleted )
		{
			szNewEntities += Entity( entity, {} ) + std::string{ m_szNewline };
		}
	}
	if ( !szNewEntities.empty() )
	{
		const std::size_t nAt = LineEnd( nLastEntity );
		Replace( nAt, 0, nAt == nLastEntity ? std::string{ m_szNewline } + szNewEntities : szNewEntities );
	}

	// Applied in order; where two touch the same place, the one made first goes first.
	std::ranges::stable_sort( m_Patches, {}, &Patch_t::m_nAt );
	std::string szOut;
	szOut.reserve( m_szText.size() + 1024 );
	std::size_t nAt = 0;
	for ( const Patch_t &patch : m_Patches )
	{
		if ( patch.m_nAt < nAt )
		{
			continue; // inside something already cut or written again
		}
		szOut.append( m_szText.substr( nAt, patch.m_nAt - nAt ) );
		szOut += patch.m_szText;
		nAt = patch.m_nAt + patch.m_nLength;
	}
	szOut.append( m_szText.substr( nAt ) );
	return szOut;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string WriteVmf( const CMap &map )
{
	return CWriter{ map }.Run();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string WriteSolid( const CMap &map, const Solid_t &solid, std::string_view szIndent )
{
	return CWriter{ map }.Solid( solid, szIndent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string WriteEntity( const CMap &map, const Entity_t &entity, std::string_view szIndent )
{
	return CWriter{ map }.Entity( entity, szIndent );
}

//-----------------------------------------------------------------------------
// Purpose: What Hammer++ writes for a map it has just made and saved, except the editor's build
//          (which isn't Hammer's here).
//-----------------------------------------------------------------------------
std::string NewVmf()
{
	std::string szText = "versioninfo\n{\n\t\"editorversion\" \"400\"\n\t\"editorbuild\" \"0\"\n\t\"mapversion\" \"0\"\n\t\"formatversion\" \"100\"\n\t\"prefab\" \"0\"\n}\n"
						 "visgroups\n{\n}\n"
						 "viewsettings\n{\n\t\"bSnapToGrid\" \"1\"\n\t\"bShowGrid\" \"1\"\n\t\"bShowLogicalGrid\" \"0\"\n\t\"nGridSpacing\" \"64\"\n}\n"
						 "world\n{\n\t\"id\" \"1\"\n\t\"mapversion\" \"0\"\n\t\"classname\" \"worldspawn\"\n\t\"detailmaterial\" \"detail/detailsprites\"\n\t\"detailvbsp\" \"detail.vbsp\"\n\t\"maxpropscreenwidth\" \"-1\"\n\t\"skyname\" \"sky_day01_01\"\n}\n"
						 "cameras\n{\n\t\"activecamera\" \"-1\"\n}\n"
						 "cordons\n{\n\t\"active\" \"0\"\n}\n";
	std::string szOut;
	szOut.reserve( szText.size() * 2 );
	for ( const char ch : szText )
	{
		if ( ch == '\n' )
		{
			szOut += '\r';
		}
		szOut += ch;
	}
	return szOut;
}

} // namespace xv::map
