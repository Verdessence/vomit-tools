//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See SoundLint.h.
//
//===========================================================================//
#include "SoundLint.h"

#include <algorithm>
#include <format>
#include <unordered_set>

#include "../util/Numbers.h"
#include "../vfs/Vfs.h"
#include "SoundConstants.h"

namespace xv::sound
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view Trim( std::string_view szText )
{
	const auto nFirst = szText.find_first_not_of( " \t\r\n" );
	if ( nFirst == std::string_view::npos )
	{
		return {};
	}
	const auto nLast = szText.find_last_not_of( " \t\r\n" );
	return szText.substr( nFirst, nLast - nFirst + 1 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
template <typename T>
std::optional<T> Parse( std::string_view szText )
{
	return ParseNumber<T>( Trim( szText ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::string_view> Split( std::string_view szText, char chSeparator )
{
	std::vector<std::string_view> parts;
	std::size_t nStart = 0;
	while ( true )
	{
		const auto nEnd = szText.find( chSeparator, nStart );
		parts.push_back( Trim( szText.substr( nStart, nEnd == std::string_view::npos ? std::string_view::npos : nEnd - nStart ) ) );
		if ( nEnd == std::string_view::npos )
		{
			return parts;
		}
		nStart = nEnd + 1;
	}
}

//-----------------------------------------------------------------------------
// Purpose: A value that may be a "min,max" range, each part checked by checkPart.
//-----------------------------------------------------------------------------
template <typename F>
std::optional<std::string> CheckRange( std::string_view szValue, F &&checkPart )
{
	const auto parts = Split( szValue, ',' );
	if ( parts.size() > 2 )
	{
		return "expected one value or a min,max range";
	}
	for ( const std::string_view szPart : parts )
	{
		if ( szPart.empty() )
		{
			return "empty value";
		}
		if ( auto problem = checkPart( szPart ) )
		{
			return problem;
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
auto NumberOr( std::span<const Constant_t> table, std::string_view szPrefix )
{
	return [ table, szPrefix ]( std::string_view szPart ) -> std::optional<std::string> {
		if ( FindConstant( table, szPart ) || Parse<double>( szPart ) )
		{
			return std::nullopt;
		}
		return std::format( "\"{}\" is not a number or a {} constant", szPart, szPrefix );
	};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::string> NotNumber( std::string_view szPart )
{
	return Parse<double>( szPart ) ? std::nullopt : std::optional<std::string>{ std::format( "\"{}\" is not a number", szPart ) };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool EndsWithIgnoreCase( std::string_view szText, std::string_view szSuffix )
{
	return szText.size() >= szSuffix.size() && kv1::EqualsIgnoreCase( szText.substr( szText.size() - szSuffix.size() ), szSuffix );
}

class CLinter
{
public:
	CLinter( const kv1::CDocument &document, FileKind_t nKind, const CSchema &schema, const LintContext_t &context )
		: m_Doc( document ),
		  m_nKind( nKind ),
		  m_Schema( schema ),
		  m_Context( context )
	{}

	std::vector<Issue_t> Run()
	{
		if ( m_Context.m_ListedInManifest == false )
		{
			Add( Issue_t::Severity_t::SEVERITY_WARNING, {}, std::format( "Not listed in {}, so the game won't load this file", ManifestPathFor( m_nKind ) ) );
		}

		std::unordered_set<std::string> seen;
		for ( std::size_t i = 0; i < m_Doc.m_Roots.size(); i++ )
		{
			const kv1::CNode &root = m_Doc.m_Roots[ i ];
			if ( root.IsPair() )
			{
				Add( Issue_t::Severity_t::SEVERITY_WARNING, { i }, std::format( "\"{}\" is a key/value pair at the top level; the engine only reads named blocks", root.GetKey() ) );
				continue;
			}
			if ( !root.IsBlock() )
			{
				continue;
			}
			const std::string szName = ToLower( root.GetKey() );
			if ( szName.empty() )
			{
				Add( Issue_t::Severity_t::SEVERITY_ERROR, { i }, std::format( "This {} has no name", m_Schema.m_szEntryNoun ) );
			}
			else if ( !seen.insert( szName ).second )
			{
				Add( Issue_t::Severity_t::SEVERITY_WARNING, { i }, std::format( "\"{}\" is defined more than once in this file", root.GetKey() ) );
			}
			else
			{
				const auto &others = m_nKind == FileKind_t::FILE_KIND_SOUNDSCAPE ? m_Context.m_OtherSoundscapes : m_Context.m_OtherSounds;
				if ( const auto it = others.find( szName ); it != others.end() )
				{
					Add( Issue_t::Severity_t::SEVERITY_INFO, { i }, std::format( "\"{}\" is also defined in {}; only one definition is used", root.GetKey(), it->second ) );
				}
			}
			if ( m_nKind == FileKind_t::FILE_KIND_SOUNDSCAPE )
			{
				m_Nesting[ szName ] = NestedSoundscapes( root );
			}
		}

		for ( std::size_t i = 0; i < m_Doc.m_Roots.size(); i++ )
		{
			if ( m_Doc.m_Roots[ i ].IsBlock() )
			{
				CheckBlock( *m_Schema.m_pEntry, m_Doc.m_Roots[ i ], { i } );
			}
		}
		if ( m_nKind == FileKind_t::FILE_KIND_SOUNDSCAPE )
		{
			CheckLoops();
		}
		return std::move( m_Issues );
	}

private:
	void Add( Issue_t::Severity_t nSeverity, NodePath_t path, std::string szMessage )
	{
		m_Issues.push_back( { nSeverity, std::move( path ), std::move( szMessage ) } );
	}

	void CheckBlock( const BlockDef_t &def, const kv1::CNode &node, const NodePath_t &path )
	{
		bool bHasWave = false;
		bool bHasRndwave = false;
		bool bHasName = false;
		for ( std::size_t j = 0; j < node.GetChildren().size(); j++ )
		{
			const kv1::CNode &child = node.Child( j );
			NodePath_t childPath = path;
			childPath.push_back( j );
			const std::string szKey = child.GetKey();

			if ( child.IsPair() )
			{
				const KeyDef_t *pKeyDef = FindKey( def, szKey );
				if ( !pKeyDef )
				{
					Add( Issue_t::Severity_t::SEVERITY_WARNING, std::move( childPath ), AllowsBlock( def, szKey ) ? std::format( "\"{}\" should be a block {{ ... }}, not a value", szKey ) : std::format( "Unknown key \"{}\"; the engine ignores it", szKey ) );
					continue;
				}
				bHasWave |= pKeyDef->m_nKind == ValueKind_t::VALUE_KIND_WAVE;
				bHasName |= pKeyDef->m_nKind == ValueKind_t::VALUE_KIND_SOUNDSCAPE_NAME;
				const std::string szValue = child.GetValue();
				if ( auto problem = CheckValue( pKeyDef->m_nKind, szValue ) )
				{
					Add( Issue_t::Severity_t::SEVERITY_WARNING, std::move( childPath ), std::move( *problem ) );
					continue;
				}
				CheckMeaning( def, *pKeyDef, szValue, childPath );
			}
			else if ( child.IsBlock() )
			{
				if ( !AllowsBlock( def, szKey ) )
				{
					Add( Issue_t::Severity_t::SEVERITY_WARNING, std::move( childPath ), std::format( "Unknown block \"{}\"; the engine ignores it", szKey ) );
					continue;
				}
				if ( kv1::EqualsIgnoreCase( szKey, "rndwave" ) )
				{
					bHasRndwave = true;
					if ( child.FindAll( "wave" ).empty() )
					{
						Add( Issue_t::Severity_t::SEVERITY_WARNING, childPath, "rndwave has no wave entries" );
					}
				}
				if ( const BlockDef_t *pSub = m_Schema.Block( szKey ) )
				{
					CheckBlock( *pSub, child, childPath );
				}
			}
		}

		if ( kv1::EqualsIgnoreCase( def.m_szKey, "playlooping" ) && !bHasWave )
		{
			Add( Issue_t::Severity_t::SEVERITY_WARNING, path, "playlooping has no wave, so it plays nothing" );
		}
		else if ( kv1::EqualsIgnoreCase( def.m_szKey, "playrandom" ) && !bHasRndwave )
		{
			Add( Issue_t::Severity_t::SEVERITY_WARNING, path, "playrandom needs an rndwave block with the sounds to pick from" );
		}
		else if ( kv1::EqualsIgnoreCase( def.m_szKey, "playsoundscape" ) && !bHasName )
		{
			Add( Issue_t::Severity_t::SEVERITY_WARNING, path, "playsoundscape has no name" );
		}
		else if ( &def == m_Schema.m_pEntry && m_nKind == FileKind_t::FILE_KIND_SOUNDSCRIPT && !bHasWave && !bHasRndwave )
		{
			Add( Issue_t::Severity_t::SEVERITY_ERROR, path, "This sound has no wave or rndwave" );
		}
	}

	void CheckMeaning( const BlockDef_t &def, const KeyDef_t &keyDef, const std::string &szValue, const NodePath_t &path )
	{
		switch ( keyDef.m_nKind )
		{
			case ValueKind_t::VALUE_KIND_WAVE:
				CheckWave( szValue, path, kv1::EqualsIgnoreCase( def.m_szKey, "playlooping" ) );
				break;
			case ValueKind_t::VALUE_KIND_SOUNDSCAPE_NAME:
			{
				const std::string szName = ToLower( szValue );
				if ( !m_Nesting.contains( szName ) && !m_Context.m_OtherSoundscapes.contains( szName ) )
				{
					Add( Issue_t::Severity_t::SEVERITY_ERROR, path, std::format( "No soundscape named \"{}\"", szValue ) );
				}
				break;
			}
			case ValueKind_t::VALUE_KIND_DSP_PRESET:
				if ( const auto preset = Parse<int>( szValue ); preset && *preset >= static_cast<int>( DspPresets().size() ) )
				{
					Add( Issue_t::Severity_t::SEVERITY_INFO, path, std::format( "dsp {} is not one of the stock presets", *preset ) );
				}
				break;
			default:
				break;
		}
	}

	void CheckWave( std::string_view szValue, const NodePath_t &path, bool bLooping )
	{
		for ( std::size_t i = 0; i < 2 && i < szValue.size(); i++ )
		{
			if ( szValue[ i ] == '!' || szValue[ i ] == '?' )
			{
				return; // a sentence or voice stream, not a file
			}
		}
		const std::string_view szFile = StripPathFlags( szValue );
		if ( !EndsWithIgnoreCase( szFile, ".wav" ) && !EndsWithIgnoreCase( szFile, ".mp3" ) )
		{
			Add( Issue_t::Severity_t::SEVERITY_WARNING, path, std::format( "\"{}\" is not a .wav or .mp3 file", szFile ) );
			return;
		}
		const std::string szGamePath = "sound/" + vfs::NormalizePath( szFile );
		if ( m_Context.m_pfnFileExists && !m_Context.m_pfnFileExists( szGamePath ) )
		{
			Add( Issue_t::Severity_t::SEVERITY_WARNING, path, std::format( "{} is not in the mounted game", szGamePath ) );
			return;
		}
		if ( bLooping && EndsWithIgnoreCase( szFile, ".wav" ) && m_Context.m_pfnHasLoopPoints )
		{
			if ( const auto loops = m_Context.m_pfnHasLoopPoints( szGamePath ); loops && !*loops )
			{
				Add( Issue_t::Severity_t::SEVERITY_WARNING, path, std::format( "{} has no loop points, so playlooping plays it only once", szGamePath ) );
			}
		}
	}

	const std::vector<std::string> *NestedIn( const std::string &szName ) const
	{
		if ( const auto it = m_Nesting.find( szName ); it != m_Nesting.end() )
		{
			return &it->second;
		}
		if ( const auto it = m_Context.m_OtherNesting.find( szName ); it != m_Context.m_OtherNesting.end() )
		{
			return &it->second;
		}
		return nullptr;
	}

	// Depth-first search for a chain of playsoundscapes from `from` back to `target`.
	bool Reaches( const std::string &szFrom, const std::string &szTarget, std::unordered_set<std::string> &visited, std::vector<std::string> &chain ) const
	{
		if ( szFrom == szTarget )
		{
			return true;
		}
		if ( !visited.insert( szFrom ).second )
		{
			return false;
		}
		if ( const auto *pNested = NestedIn( szFrom ) )
		{
			for ( const std::string &szNext : *pNested )
			{
				chain.push_back( szNext );
				if ( Reaches( szNext, szTarget, visited, chain ) )
				{
					return true;
				}
				chain.pop_back();
			}
		}
		return false;
	}

	void CheckLoops()
	{
		for ( std::size_t i = 0; i < m_Doc.m_Roots.size(); i++ )
		{
			const kv1::CNode &root = m_Doc.m_Roots[ i ];
			if ( !root.IsBlock() )
			{
				continue;
			}
			const std::string szName = ToLower( root.GetKey() );
			for ( std::size_t j = 0; j < root.GetChildren().size(); j++ )
			{
				const kv1::CNode &child = root.Child( j );
				const kv1::CNode *pTarget = child.IsBlock() && kv1::EqualsIgnoreCase( child.GetKey(), "playsoundscape" ) ? child.Find( "name" ) : nullptr;
				if ( !pTarget )
				{
					continue;
				}
				std::unordered_set<std::string> visited;
				std::vector<std::string> chain{ szName, ToLower( pTarget->GetValue() ) };
				if ( Reaches( chain.back(), szName, visited, chain ) )
				{
					std::string szText;
					for ( const std::string &szStep : chain )
					{
						szText += ( szText.empty() ? "" : " → " ) + szStep;
					}
					Add( Issue_t::Severity_t::SEVERITY_ERROR, { i, j }, std::format( "playsoundscape loop: {}", szText ) );
				}
			}
		}
	}

	const kv1::CDocument &m_Doc;
	FileKind_t m_nKind;
	const CSchema &m_Schema;
	const LintContext_t &m_Context;
	std::unordered_map<std::string, std::vector<std::string>> m_Nesting;
	std::vector<Issue_t> m_Issues;
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string ToLower( std::string_view szText )
{
	std::string szOut{ szText };
	std::ranges::transform( szOut, szOut.begin(), []( char c ) {
		return ( c >= 'A' && c <= 'Z' ) ? static_cast<char>( c - 'A' + 'a' ) : c;
	} );
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose: Checks one value's syntax against its kind; returns what is wrong, or nullopt if it is
//          fine.
//-----------------------------------------------------------------------------
std::optional<std::string> CheckValue( ValueKind_t nKind, std::string_view szValue )
{
	const std::string_view szText = Trim( szValue );
	switch ( nKind )
	{
		case ValueKind_t::VALUE_KIND_TEXT:
			return std::nullopt;
		case ValueKind_t::VALUE_KIND_NUMBER:
			return NotNumber( szText );
		case ValueKind_t::VALUE_KIND_NUMBER_RANGE:
			return CheckRange( szText, NotNumber );
		case ValueKind_t::VALUE_KIND_VOLUME:
			return CheckRange( szText, NumberOr( Volumes(), "VOL_" ) );
		case ValueKind_t::VALUE_KIND_PITCH:
			return CheckRange( szText, NumberOr( Pitches(), "PITCH_" ) );
		case ValueKind_t::VALUE_KIND_SOUND_LEVEL:
			return CheckRange( szText, []( std::string_view szPart ) -> std::optional<std::string> {
				if ( ParseSoundLevel( szPart ) )
				{
					return std::nullopt;
				}
				return std::format( "\"{}\" is not a sound level (SNDLVL_* or a number of decibels)", szPart );
			} );
		case ValueKind_t::VALUE_KIND_ATTENUATION:
			return NumberOr( Attenuations(), "ATTN_" )( szText );
		case ValueKind_t::VALUE_KIND_CHANNEL:
			if ( FindConstant( Channels(), szText ) || Parse<int>( szText ) )
			{
				return std::nullopt;
			}
			return std::format( "\"{}\" is not a CHAN_ constant", szText );
		case ValueKind_t::VALUE_KIND_DSP_PRESET:
			if ( const auto preset = Parse<int>( szText ); preset && *preset >= 0 )
			{
				return std::nullopt;
			}
			return std::format( "\"{}\" is not a preset number", szText );
		case ValueKind_t::VALUE_KIND_POSITION:
		case ValueKind_t::VALUE_KIND_RANDOM_POSITION:
			if ( const auto position = Parse<int>( szText ); position && *position >= 0 && *position <= 7 )
			{
				return std::nullopt;
			}
			if ( nKind == ValueKind_t::VALUE_KIND_RANDOM_POSITION && kv1::EqualsIgnoreCase( szText, "random" ) )
			{
				return std::nullopt;
			}
			if ( nKind == ValueKind_t::VALUE_KIND_RANDOM_POSITION )
			{
				return std::format( "\"{}\" is not a position from 0 to 7 or \"random\"", szText );
			}
			return std::format( "\"{}\" is not a position from 0 to 7", szText );
		case ValueKind_t::VALUE_KIND_ORIGIN:
		{
			std::vector<std::string_view> parts;
			for ( const std::string_view szPart : Split( szText, ' ' ) )
			{
				if ( !szPart.empty() )
				{
					parts.push_back( szPart );
				}
			}
			if ( parts.size() == 3 && std::ranges::all_of( parts, []( std::string_view p ) { return Parse<double>( p ).has_value(); } ) )
			{
				return std::nullopt;
			}
			return std::format( "\"{}\" is not an \"x y z\" position", szText );
		}
		case ValueKind_t::VALUE_KIND_WAVE:
		case ValueKind_t::VALUE_KIND_SOUNDSCAPE_NAME:
			return szText.empty() ? std::optional<std::string>{ "empty" } : std::nullopt;
		case ValueKind_t::VALUE_KIND_BOOL:
			return szText == "0" || szText == "1" ? std::nullopt : std::optional<std::string>{ std::format( "\"{}\" is not 0 or 1", szText ) };
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<Issue_t> Lint( const kv1::CDocument &document, FileKind_t nKind, const LintContext_t &context )
{
	const CSchema *pSchema = SchemaFor( nKind );
	if ( !pSchema )
	{
		return {};
	}
	return CLinter{ document, nKind, *pSchema, context }.Run();
}

//-----------------------------------------------------------------------------
// Purpose: Lowercase names of the soundscapes a soundscape plays through playsoundscape.
//-----------------------------------------------------------------------------
std::vector<std::string> NestedSoundscapes( const kv1::CNode &soundscape )
{
	std::vector<std::string> names;
	for ( const kv1::CNode &child : soundscape.GetChildren() )
	{
		if ( child.IsBlock() && kv1::EqualsIgnoreCase( child.GetKey(), "playsoundscape" ) )
		{
			if ( const kv1::CNode *pName = child.Find( "name" ) )
			{
				names.push_back( ToLower( pName->GetValue() ) );
			}
		}
	}
	return names;
}

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
kv1::CNode *ManifestBlock( kv1::CDocument &manifest )
{
	const auto it = std::ranges::find_if( manifest.m_Roots, &kv1::CNode::IsBlock );
	return it == manifest.m_Roots.end() ? nullptr : &*it;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsManifestKey( std::string_view szKey )
{
	return kv1::EqualsIgnoreCase( szKey, "file" ) || kv1::EqualsIgnoreCase( szKey, "precache_file" ) || kv1::EqualsIgnoreCase( szKey, "preload_file" );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Files a manifest lists (values of "file", "precache_file" and "preload_file"),
//          normalized.
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
			if ( child.IsPair() && IsManifestKey( child.GetKey() ) )
			{
				files.push_back( vfs::NormalizePath( child.GetValue() ) );
			}
		}
		break;
	}
	return files;
}

//-----------------------------------------------------------------------------
// Purpose: Adds path to the manifest's block. Returns false if it was already listed or there is
//          no block.
//-----------------------------------------------------------------------------
bool AddToManifest( kv1::CDocument &manifest, FileKind_t nKind, std::string_view szPath )
{
	kv1::CNode *pBlock = ManifestBlock( manifest );
	const auto listed = ManifestFiles( manifest );
	if ( !pBlock || std::ranges::find( listed, vfs::NormalizePath( szPath ) ) != listed.end() )
	{
		return false;
	}
	pBlock->AppendChild( kv1::CNode::MakePair( nKind == FileKind_t::FILE_KIND_SOUNDSCAPE ? "file" : "precache_file", szPath ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool RemoveFromManifest( kv1::CDocument &manifest, std::string_view szPath )
{
	kv1::CNode *pBlock = ManifestBlock( manifest );
	if ( !pBlock )
	{
		return false;
	}
	const std::string szWanted = vfs::NormalizePath( szPath );
	bool bRemoved = false;
	for ( std::size_t i = pBlock->GetChildren().size(); i-- > 0; )
	{
		const kv1::CNode &child = pBlock->GetChildren()[ i ];
		if ( child.IsPair() && IsManifestKey( child.GetKey() ) && vfs::NormalizePath( child.GetValue() ) == szWanted )
		{
			pBlock->RemoveChild( i );
			bRemoved = true;
		}
	}
	return bRemoved;
}

} // namespace xv::sound
