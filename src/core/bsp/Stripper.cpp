//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Stripper.h.
//
//===========================================================================//
#include "Stripper.h"

#include <algorithm>
#include <map>
#include <regex>

#include "vfs/Vfs.h"

namespace fs = std::filesystem;

namespace xv::bsp
{

namespace
{

struct Token_t
{
	enum class Kind_t
	{
		KIND_STRING,
		KIND_WORD, // add:, match: and so on
		KIND_OPEN,
		KIND_CLOSE
	};
	Kind_t m_nKind = Kind_t::KIND_STRING;
	std::string m_szText;
	int m_nLine = 1;
};

//-----------------------------------------------------------------------------
// Purpose: Quoted strings, braces and bare words, with comment lines left out.
//-----------------------------------------------------------------------------
std::vector<Token_t> Tokenize( std::string_view szText )
{
	std::vector<Token_t> tokens;
	int nLine = 1;
	for ( std::size_t i = 0; i < szText.size(); )
	{
		const char ch = szText[ i ];
		if ( ch == '\n' )
		{
			nLine++;
			i++;
		}
		else if ( ch == ' ' || ch == '\t' || ch == '\r' )
		{
			i++;
		}
		else if ( ch == ';' || ch == '#' || szText.substr( i, 2 ) == "//" )
		{
			i = std::min( szText.find( '\n', i ), szText.size() );
		}
		else if ( ch == '{' || ch == '}' )
		{
			tokens.push_back( { ch == '{' ? Token_t::Kind_t::KIND_OPEN : Token_t::Kind_t::KIND_CLOSE, {}, nLine } );
			i++;
		}
		else if ( ch == '"' )
		{
			const std::size_t nEnd = szText.find( '"', i + 1 );
			const std::size_t nStop = nEnd == std::string_view::npos ? szText.size() : nEnd;
			tokens.push_back( { Token_t::Kind_t::KIND_STRING, std::string( szText.substr( i + 1, nStop - i - 1 ) ), nLine } );
			nLine += static_cast<int>( std::count( szText.begin() + static_cast<std::ptrdiff_t>( i ), szText.begin() + static_cast<std::ptrdiff_t>( nStop ), '\n' ) );
			i = nStop + 1;
		}
		else
		{
			std::size_t nEnd = i;
			while ( nEnd < szText.size() && std::string_view( " \t\r\n{}\"" ).find( szText[ nEnd ] ) == std::string_view::npos )
			{
				nEnd++;
			}
			std::string szWord( szText.substr( i, nEnd - i ) );
			std::ranges::transform( szWord, szWord.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
			tokens.push_back( { Token_t::Kind_t::KIND_WORD, std::move( szWord ), nLine } );
			i = nEnd;
		}
	}
	return tokens;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool KeysEqual( std::string_view a, std::string_view b )
{
	return std::ranges::equal( a, b, []( unsigned char x, unsigned char y ) { return std::tolower( x ) == std::tolower( y ); } );
}

//-----------------------------------------------------------------------------
// Purpose: A value from a config against an entity's: "/regex/" searches it (Stripper uses PCRE;
//          std::regex's ECMAScript grammar covers what configs use), anything else must be equal.
//-----------------------------------------------------------------------------
bool ValueMatches( const std::string &szPattern, std::string_view szValue )
{
	if ( szPattern.size() < 2 || szPattern.front() != '/' || szPattern.back() != '/' )
	{
		return szPattern == szValue;
	}
	thread_local std::map<std::string, std::optional<std::regex>> cache;
	auto [ it, bNew ] = cache.try_emplace( szPattern );
	if ( bNew )
	{
		try
		{
			it->second.emplace( szPattern.substr( 1, szPattern.size() - 2 ) );
		}
		catch ( const std::regex_error & )
		{
		}
	}
	return it->second && std::regex_search( szValue.begin(), szValue.end(), *it->second );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Section words hold until the next one; each block after them is one rule.
//-----------------------------------------------------------------------------
StripperConfig_t ParseStripper( std::string_view szText, std::string_view szName )
{
	StripperConfig_t config;
	const std::vector<Token_t> tokens = Tokenize( szText );
	const auto where = [ &szName ]( int nLine ) { return std::string( szName ) + ":" + std::to_string( nLine ); };
	std::optional<StripperRule_t::Kind_t> section;
	bool bUnknownSection = false; // its blocks are skipped without another word

	// Reads key/value pairs up to the closing brace; i is just past the opening one.
	const auto readPairs = [ & ]( std::size_t &i, Entity_t &into, bool bQuiet = false ) {
		while ( i < tokens.size() && tokens[ i ].m_nKind != Token_t::Kind_t::KIND_CLOSE )
		{
			if ( tokens[ i ].m_nKind == Token_t::Kind_t::KIND_STRING && i + 1 < tokens.size() && tokens[ i + 1 ].m_nKind == Token_t::Kind_t::KIND_STRING )
			{
				into.m_KeyValues.emplace_back( tokens[ i ].m_szText, tokens[ i + 1 ].m_szText );
				i += 2;
				continue;
			}
			if ( !bQuiet )
			{
				config.m_Problems.push_back( where( tokens[ i ].m_nLine ) + ": expected a \"key\" \"value\" pair" );
			}
			i++;
		}
		if ( i >= tokens.size() )
		{
			config.m_Problems.push_back( std::string( szName ) + ": a block isn't closed" );
		}
		i++;
	};

	for ( std::size_t i = 0; i < tokens.size(); )
	{
		const Token_t &token = tokens[ i ];
		if ( token.m_nKind == Token_t::Kind_t::KIND_WORD )
		{
			if ( token.m_szText == "add:" )
			{
				section = StripperRule_t::Kind_t::KIND_ADD;
			}
			else if ( token.m_szText == "filter:" || token.m_szText == "remove:" )
			{
				section = StripperRule_t::Kind_t::KIND_FILTER;
			}
			else if ( token.m_szText == "modify:" )
			{
				section = StripperRule_t::Kind_t::KIND_MODIFY;
			}
			else
			{
				config.m_Problems.push_back( where( token.m_nLine ) + ": unknown section \"" + token.m_szText + "\"; its blocks are skipped" );
				section.reset();
				bUnknownSection = true;
				i++;
				continue;
			}
			bUnknownSection = false;
			i++;
			continue;
		}
		if ( token.m_nKind != Token_t::Kind_t::KIND_OPEN )
		{
			config.m_Problems.push_back( where( token.m_nLine ) + ": expected a section (add:, filter:, modify:) or a block" );
			i++;
			continue;
		}
		if ( !section && !bUnknownSection )
		{
			config.m_Problems.push_back( where( token.m_nLine ) + ": a block before any section; it's skipped" );
		}
		StripperRule_t rule;
		rule.m_nKind = section.value_or( StripperRule_t::Kind_t::KIND_ADD );
		rule.m_szWhere = where( token.m_nLine );
		i++;
		if ( rule.m_nKind != StripperRule_t::Kind_t::KIND_MODIFY || !section )
		{
			readPairs( i, rule.m_Entity, !section );
		}
		else
		{
			// match:, replace:, delete: and insert:, each with its block, then the closing brace.
			while ( i < tokens.size() && tokens[ i ].m_nKind != Token_t::Kind_t::KIND_CLOSE )
			{
				Entity_t *pInto = nullptr;
				const std::string &szWord = tokens[ i ].m_szText;
				if ( tokens[ i ].m_nKind == Token_t::Kind_t::KIND_WORD )
				{
					pInto = szWord == "match:" ? &rule.m_Entity : szWord == "replace:" ? &rule.m_Replace
						: szWord == "delete:"                                          ? &rule.m_Delete
						: szWord == "insert:"                                          ? &rule.m_Insert
																					   : nullptr;
				}
				if ( !pInto || i + 1 >= tokens.size() || tokens[ i + 1 ].m_nKind != Token_t::Kind_t::KIND_OPEN )
				{
					config.m_Problems.push_back( where( tokens[ i ].m_nLine ) + ": expected match:, replace:, delete: or insert: and a block" );
					i++;
					continue;
				}
				i += 2;
				readPairs( i, *pInto );
			}
			i++;
			if ( rule.m_Entity.m_KeyValues.empty() )
			{
				config.m_Problems.push_back( rule.m_szWhere + ": modify without a match: block changes nothing" );
				continue;
			}
		}
		if ( section )
		{
			config.m_Rules.push_back( std::move( rule ) );
		}
	}
	return config;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool StripperMatches( const Entity_t &match, const Entity_t &entity )
{
	return std::ranges::all_of( match.m_KeyValues, [ &entity ]( const auto &wanted ) {
		return std::ranges::any_of( entity.m_KeyValues, [ &wanted ]( const auto &pair ) { return KeysEqual( pair.first, wanted.first ) && ValueMatches( wanted.second, pair.second ); } );
	} );
}

//-----------------------------------------------------------------------------
// Purpose: A modify runs its parts in a fixed order: replace (every pair with the key), delete
//          (pairs whose key and value match), insert (added at the end).
//-----------------------------------------------------------------------------
std::vector<StrippedEntity_t> ApplyStripper( const std::vector<Entity_t> &entities, const std::vector<StripperConfig_t> &configs )
{
	using Status_t = StrippedEntity_t::Status_t;
	std::vector<StrippedEntity_t> result;
	result.reserve( entities.size() );
	for ( std::size_t i = 0; i < entities.size(); i++ )
	{
		result.push_back( { entities[ i ], Status_t::STATUS_KEPT, i, {}, {}, {} } );
	}
	const auto noteChanged = []( StrippedEntity_t &item, const std::string &szKey ) {
		if ( std::ranges::none_of( item.m_ChangedKeys, [ &szKey ]( const std::string &sz ) { return KeysEqual( sz, szKey ); } ) )
		{
			item.m_ChangedKeys.push_back( szKey );
		}
	};
	for ( const StripperConfig_t &config : configs )
	{
		for ( const StripperRule_t &rule : config.m_Rules )
		{
			if ( rule.m_nKind == StripperRule_t::Kind_t::KIND_ADD )
			{
				result.push_back( { rule.m_Entity, Status_t::STATUS_ADDED, std::nullopt, {}, { rule.m_szWhere }, {} } );
				continue;
			}
			for ( StrippedEntity_t &item : result )
			{
				if ( item.m_nStatus == Status_t::STATUS_REMOVED || !StripperMatches( rule.m_Entity, item.m_Entity ) )
				{
					continue;
				}
				item.m_Rules.push_back( rule.m_szWhere );
				if ( rule.m_nKind == StripperRule_t::Kind_t::KIND_FILTER )
				{
					item.m_nStatus = Status_t::STATUS_REMOVED;
					continue;
				}
				auto &pairs = item.m_Entity.m_KeyValues;
				for ( const auto &[ key, value ] : rule.m_Replace.m_KeyValues )
				{
					for ( auto &pair : pairs )
					{
						if ( KeysEqual( pair.first, key ) && pair.second != value )
						{
							pair.second = value;
							noteChanged( item, pair.first );
						}
					}
				}
				std::erase_if( pairs, [ & ]( const auto &pair ) {
					const bool bDelete = std::ranges::any_of( rule.m_Delete.m_KeyValues, [ &pair ]( const auto &gone ) { return KeysEqual( pair.first, gone.first ) && ValueMatches( gone.second, pair.second ); } );
					if ( bDelete )
					{
						item.m_Deleted.push_back( pair );
					}
					return bDelete;
				} );
				for ( const auto &pair : rule.m_Insert.m_KeyValues )
				{
					pairs.push_back( pair );
					noteChanged( item, pair.first );
				}
				if ( item.m_nStatus == Status_t::STATUS_KEPT )
				{
					item.m_nStatus = Status_t::STATUS_MODIFIED;
				}
			}
		}
	}
	return result;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<fs::path> FindStripperConfigs( const fs::path &map )
{
	// The nearest folder up from the map with addons/stripper: maps sit in the game folder's maps/,
	// but also in download/maps/ (from servers) and custom/<pack>/maps/.
	std::vector<fs::path> found;
	std::error_code ec;
	fs::path stripper;
	fs::path folder = map.parent_path();
	for ( int nUp = 0; nUp < 4 && !folder.empty() && stripper.empty(); nUp++, folder = folder.parent_path() )
	{
		const fs::path candidate = vfs::FindCaseInsensitive( folder / "addons" / "stripper" );
		if ( fs::is_directory( candidate, ec ) )
		{
			stripper = candidate;
		}
		if ( folder == folder.parent_path() )
		{
			break;
		}
	}
	if ( stripper.empty() )
	{
		return found;
	}
	for ( const fs::path &candidate : { stripper / "global_filters.cfg", stripper / "maps" / ( map.stem().native() + fs::path( ".cfg" ).native() ) } )
	{
		const fs::path real = vfs::FindCaseInsensitive( candidate );
		if ( fs::is_regular_file( real, ec ) )
		{
			found.push_back( real );
		}
	}
	return found;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
fs::path StripperMapConfigPath( const fs::path &map )
{
	std::error_code ec;
	const fs::path name = fs::path( map.stem().native() + fs::path( ".cfg" ).native() );
	fs::path folder = map.parent_path();
	for ( int nUp = 0; nUp < 4 && !folder.empty(); nUp++, folder = folder.parent_path() )
	{
		const fs::path stripper = vfs::FindCaseInsensitive( folder / "addons" / "stripper" );
		if ( fs::is_directory( stripper, ec ) )
		{
			return vfs::FindCaseInsensitive( stripper / "maps" ) / name;
		}
		if ( fs::is_regular_file( vfs::FindCaseInsensitive( folder / "gameinfo.txt" ), ec ) )
		{
			return folder / "addons" / "stripper" / "maps" / name;
		}
		if ( folder == folder.parent_path() )
		{
			break;
		}
	}
	return {};
}

namespace
{

//-----------------------------------------------------------------------------
// Purpose: A block of pairs, indented to depth (in tabs).
//-----------------------------------------------------------------------------
std::string Block( const std::vector<std::pair<std::string, std::string>> &pairs, int nDepth )
{
	const std::string szIndent( static_cast<std::size_t>( nDepth ), '\t' );
	std::string szText = szIndent + "{\n";
	for ( const auto &[ key, value ] : pairs )
	{
		szText += szIndent + "\t\"" + key + "\" \"" + value + "\"\n";
	}
	return szText + szIndent + "}\n";
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::pair<std::string, std::string>> MatchFor( const Entity_t &entity )
{
	const std::string_view szHammerId = entity.Get( "hammerid" );
	if ( !szHammerId.empty() && szHammerId != "0" )
	{
		return { { "hammerid", std::string( szHammerId ) } };
	}
	std::vector<std::pair<std::string, std::string>> match;
	for ( const char *pszKey : { "classname", "targetname", "origin" } )
	{
		if ( const std::string_view szValue = entity.Get( pszKey ); !szValue.empty() )
		{
			match.emplace_back( pszKey, std::string( szValue ) );
		}
	}
	return match;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string StripperFilterFor( const Entity_t &entity )
{
	return "filter:\n" + Block( MatchFor( entity ), 0 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string StripperModifyFor( const Entity_t &entity )
{
	std::vector<std::pair<std::string, std::string>> replace;
	for ( const auto &pair : entity.m_KeyValues )
	{
		const auto nCount = std::ranges::count_if( entity.m_KeyValues, [ &pair ]( const auto &other ) { return KeysEqual( other.first, pair.first ); } );
		const bool bOutput = pair.first.size() > 2 && ( pair.first[ 0 ] == 'O' || pair.first[ 0 ] == 'o' ) && ( pair.first[ 1 ] == 'n' || pair.first[ 1 ] == 'N' ) && std::isupper( static_cast<unsigned char>( pair.first[ 2 ] ) );
		if ( nCount == 1 && !bOutput && !KeysEqual( pair.first, "hammerid" ) && !KeysEqual( pair.first, "classname" ) )
		{
			replace.push_back( pair );
		}
	}
	return "modify:\n{\n\tmatch:\n" + Block( MatchFor( entity ), 1 ) + "\treplace:\n" + Block( replace, 1 ) + "}\n";
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string StripperAddFor( const Entity_t &entity )
{
	std::vector<std::pair<std::string, std::string>> pairs;
	std::ranges::copy_if( entity.m_KeyValues, std::back_inserter( pairs ), []( const auto &pair ) { return !KeysEqual( pair.first, "hammerid" ); } );
	return "add:\n" + Block( pairs, 0 );
}

} // namespace xv::bsp
