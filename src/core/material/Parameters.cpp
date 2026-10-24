//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Parameters.h.
//
//===========================================================================//
#include "Parameters.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <map>

#include "kv1/Kv1.h"
#include "texture/Vtf.h"
#include "util/Numbers.h"
#include "vfs/Vfs.h"

namespace xv::material
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string Lower( std::string_view szText )
{
	std::string sz{ szText };
	std::ranges::transform( sz, sz.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
	return sz;
}

//-----------------------------------------------------------------------------
// Purpose: The words of a line, split on spaces and tabs.
//-----------------------------------------------------------------------------
std::vector<std::string> Words( std::string_view szText )
{
	std::vector<std::string> words;
	std::size_t i = 0;
	while ( i < szText.size() )
	{
		while ( i < szText.size() && ( szText[ i ] == ' ' || szText[ i ] == '\t' ) )
		{
			i++;
		}
		const std::size_t nStart = i;
		while ( i < szText.size() && szText[ i ] != ' ' && szText[ i ] != '\t' )
		{
			i++;
		}
		if ( i > nStart )
		{
			words.emplace_back( szText.substr( nStart, i - nStart ) );
		}
	}
	return words;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<ParameterKind_t> KindByName( std::string_view szName )
{
	static const std::map<std::string_view, ParameterKind_t> KINDS{
		{ "texture", ParameterKind_t::PARAMETER_KIND_TEXTURE },
		{ "material", ParameterKind_t::PARAMETER_KIND_MATERIAL },
		{ "bool", ParameterKind_t::PARAMETER_KIND_BOOL },
		{ "number", ParameterKind_t::PARAMETER_KIND_NUMBER },
		{ "integer", ParameterKind_t::PARAMETER_KIND_INTEGER },
		{ "color", ParameterKind_t::PARAMETER_KIND_COLOR },
		{ "vector", ParameterKind_t::PARAMETER_KIND_VECTOR },
		{ "transform", ParameterKind_t::PARAMETER_KIND_TRANSFORM },
		{ "text", ParameterKind_t::PARAMETER_KIND_TEXT },
		{ "surface", ParameterKind_t::PARAMETER_KIND_SURFACE },
		{ "choice", ParameterKind_t::PARAMETER_KIND_CHOICE },
	};
	const auto it = KINDS.find( szName );
	return it == KINDS.end() ? std::nullopt : std::optional{ it->second };
}

//-----------------------------------------------------------------------------
// Purpose: The value a kind starts with when the table gives none.
//-----------------------------------------------------------------------------
std::string UsualStart( ParameterKind_t nKind )
{
	switch ( nKind )
	{
		case ParameterKind_t::PARAMETER_KIND_BOOL: return "1";
		case ParameterKind_t::PARAMETER_KIND_NUMBER: return "1";
		case ParameterKind_t::PARAMETER_KIND_INTEGER: return "0";
		case ParameterKind_t::PARAMETER_KIND_COLOR: return "[1 1 1]";
		case ParameterKind_t::PARAMETER_KIND_VECTOR: return "[0 0 0]";
		case ParameterKind_t::PARAMETER_KIND_TRANSFORM: return "center .5 .5 scale 1 1 rotate 0 translate 0 0";
		case ParameterKind_t::PARAMETER_KIND_SURFACE: return "default";
		case ParameterKind_t::PARAMETER_KIND_CHOICE: return "0";
		default: return {};
	}
}

//-----------------------------------------------------------------------------
// Purpose: The numbers of "[1 2 3]", "{1 2 3}" or "1 2 3"; nullopt when something else is there.
//-----------------------------------------------------------------------------
std::optional<std::vector<float>> NumbersOf( std::string_view szValue )
{
	std::string sz{ szValue };
	for ( char &c : sz )
	{
		if ( c == '[' || c == ']' || c == '{' || c == '}' )
		{
			c = ' ';
		}
	}
	std::vector<float> numbers;
	for ( const std::string &szWord : Words( sz ) )
	{
		const auto number = ParseNumber<float>( szWord );
		if ( !number )
		{
			return std::nullopt;
		}
		numbers.push_back( *number );
	}
	return numbers;
}

//-----------------------------------------------------------------------------
// Purpose: How many single-letter edits turn one word into another.
//-----------------------------------------------------------------------------
std::size_t EditDistance( std::string_view a, std::string_view b )
{
	std::vector<std::size_t> row( b.size() + 1 );
	for ( std::size_t j = 0; j <= b.size(); j++ )
	{
		row[ j ] = j;
	}
	for ( std::size_t i = 1; i <= a.size(); i++ )
	{
		std::size_t nDiagonal = row[ 0 ];
		row[ 0 ] = i;
		for ( std::size_t j = 1; j <= b.size(); j++ )
		{
			const std::size_t nAbove = row[ j ];
			row[ j ] = std::min( { row[ j ] + 1, row[ j - 1 ] + 1, nDiagonal + ( a[ i - 1 ] == b[ j - 1 ] ? 0 : 1 ) } );
			nDiagonal = nAbove;
		}
	}
	return row[ b.size() ];
}

// The starts of each line of a text, to turn byte offsets into line numbers.
class CLines
{
public:
	explicit CLines( std::string_view szText )
	{
		m_Starts.push_back( 0 );
		for ( std::size_t i = 0; i < szText.size(); i++ )
		{
			if ( szText[ i ] == '\n' )
			{
				m_Starts.push_back( i + 1 );
			}
		}
	}
	[[nodiscard]] std::size_t LineOf( std::size_t nOffset ) const { return static_cast<std::size_t>( std::ranges::upper_bound( m_Starts, nOffset ) - m_Starts.begin() ); }

private:
	std::vector<std::size_t> m_Starts;
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Whether a block holds settings the engine reads as the material's own.
//-----------------------------------------------------------------------------
bool IsParameterBlock( std::string_view szKey )
{
	const std::string sz = Lower( szKey );
	return sz.starts_with( '<' ) || sz.starts_with( '>' ) || sz == "insert" || sz == "replace" || sz.find( "_dx" ) != std::string::npos || sz.ends_with( "_hdr" ) || sz.ends_with( "_nobump" ) || sz == "srgb" || sz == "srgb?" || sz == "hdr" || sz == "ldr" || sz == "gpu>=1" || sz == "gpu<1" || sz == "gpu>=2" || sz == "gpu<2" || sz == "gpu>=3" || sz == "gpu<3";
}

//-----------------------------------------------------------------------------
// Purpose: A shader's name as the table keys it.
//-----------------------------------------------------------------------------
std::string ShaderKind( std::string_view szShader )
{
	std::string sz = Lower( szShader );
	if ( const auto nAt = sz.rfind( "_dx" ); nAt != std::string::npos && nAt + 3 < sz.size() && std::isdigit( static_cast<unsigned char>( sz[ nAt + 3 ] ) ) )
	{
		sz.resize( nAt );
	}
	for ( const std::string_view szSuffix : { "_hdr", "_nobump" } )
	{
		if ( sz.ends_with( szSuffix ) )
		{
			sz.resize( sz.size() - szSuffix.size() );
		}
	}
	return sz;
}

//-----------------------------------------------------------------------------
// Purpose: Reads the table.
//-----------------------------------------------------------------------------
CParameterTable CParameterTable::Parse( std::string_view szText )
{
	CParameterTable table;
	const kv1::CParseResult parsed = kv1::Parse( szText );
	const kv1::CNode *pRoot = parsed.m_Document.Find( "material_parameters" );
	if ( !pRoot )
	{
		return table;
	}
	if ( const kv1::CNode *pShaders = pRoot->Find( "shaders" ); pShaders && pShaders->IsPair() )
	{
		table.m_Shaders = Words( pShaders->GetValue() );
	}
	if ( const kv1::CNode *pChecked = pRoot->Find( "checked" ); pChecked && pChecked->IsPair() )
	{
		for ( const std::string &szShader : Words( pChecked->GetValue() ) )
		{
			table.m_Checked.push_back( Lower( szShader ) );
		}
	}
	std::map<std::string, std::vector<std::string>> groups;
	if ( const kv1::CNode *pGroups = pRoot->Find( "groups" ) )
	{
		for ( const kv1::CNode &group : pGroups->GetChildren() )
		{
			if ( group.IsPair() )
			{
				for ( const std::string &szShader : Words( group.GetValue() ) )
				{
					groups[ Lower( group.GetKey() ) ].push_back( Lower( szShader ) );
				}
			}
		}
	}
	const kv1::CNode *pParameters = pRoot->Find( "parameters" );
	for ( const kv1::CNode &entry : pParameters ? pParameters->GetChildren() : std::vector<kv1::CNode>{} )
	{
		if ( !entry.IsPair() )
		{
			continue;
		}
		const std::vector<std::string> words = Words( entry.GetValue() );
		const auto nKind = words.empty() ? std::nullopt : KindByName( Lower( words.front() ) );
		if ( !nKind )
		{
			continue;
		}
		ParameterDef_t parameter;
		parameter.m_szKey = entry.GetKey();
		parameter.m_nKind = *nKind;
		parameter.m_szStart = UsualStart( *nKind );
		bool bEveryShader = false;
		for ( std::size_t i = 1; i < words.size(); i++ )
		{
			if ( words[ i ].starts_with( '=' ) )
			{
				parameter.m_szStart = words[ i ].substr( 1 );
				std::ranges::replace( parameter.m_szStart, ',', ' ' );
			}
			else if ( words[ i ] == "*" )
			{
				bEveryShader = true;
			}
			else if ( const auto it = groups.find( Lower( words[ i ] ) ); it != groups.end() )
			{
				parameter.m_Shaders.insert( parameter.m_Shaders.end(), it->second.begin(), it->second.end() );
			}
			else
			{
				parameter.m_Shaders.push_back( Lower( words[ i ] ) );
			}
		}
		if ( bEveryShader )
		{
			parameter.m_Shaders.clear();
		}
		table.m_Parameters.push_back( std::move( parameter ) );
	}
	return table;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const ParameterDef_t *CParameterTable::Find( std::string_view szKey ) const
{
	const auto it = std::ranges::find_if( m_Parameters, [ szKey ]( const ParameterDef_t &parameter ) { return kv1::EqualsIgnoreCase( parameter.m_szKey, szKey ); } );
	return it == m_Parameters.end() ? nullptr : &*it;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CParameterTable::IsKnownShader( std::string_view szShader ) const
{
	const std::string szKind = ShaderKind( szShader );
	return std::ranges::any_of( m_Shaders, [ &szKind ]( const std::string &szKnown ) { return Lower( szKnown ) == szKind; } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CParameterTable::IsChecked( std::string_view szShader ) const
{
	return std::ranges::find( m_Checked, ShaderKind( szShader ) ) != m_Checked.end();
}

//-----------------------------------------------------------------------------
// Purpose: Whether a shader uses the parameter.
//-----------------------------------------------------------------------------
bool CParameterTable::IsFor( const ParameterDef_t &parameter, std::string_view szShader )
{
	return parameter.m_Shaders.empty() || std::ranges::find( parameter.m_Shaders, ShaderKind( szShader ) ) != parameter.m_Shaders.end();
}

//-----------------------------------------------------------------------------
// Purpose: What is wrong with a value for its kind, or nullopt when it is fine.
//-----------------------------------------------------------------------------
std::optional<std::string> CheckParameterValue( ParameterKind_t nKind, std::string_view szValue )
{
	switch ( nKind )
	{
		case ParameterKind_t::PARAMETER_KIND_BOOL:
			// The engine reads a switch as a number; anything but 0 is on.
			if ( !ParseNumber<float>( szValue ) )
			{
				return "should be 0 or 1";
			}
			return std::nullopt;
		case ParameterKind_t::PARAMETER_KIND_NUMBER:
			if ( !ParseNumber<float>( szValue ) )
			{
				return "should be a number";
			}
			return std::nullopt;
		case ParameterKind_t::PARAMETER_KIND_INTEGER:
		case ParameterKind_t::PARAMETER_KIND_CHOICE:
			if ( !ParseNumber<int>( szValue ) )
			{
				return "should be a whole number";
			}
			return std::nullopt;
		case ParameterKind_t::PARAMETER_KIND_COLOR:
		{
			const auto numbers = NumbersOf( szValue );
			if ( !numbers || ( numbers->size() != 1 && numbers->size() != 3 ) )
			{
				return "should be a colour: [r g b] from 0 to 1, or {r g b} from 0 to 255";
			}
			return std::nullopt;
		}
		case ParameterKind_t::PARAMETER_KIND_VECTOR:
		{
			const auto numbers = NumbersOf( szValue );
			if ( !numbers || numbers->empty() || numbers->size() > 4 )
			{
				return "should be up to four numbers, as [a b c]";
			}
			return std::nullopt;
		}
		case ParameterKind_t::PARAMETER_KIND_TRANSFORM:
		{
			// A matrix in brackets is read too.
			if ( szValue.find( '[' ) != std::string_view::npos && NumbersOf( szValue ) )
			{
				return std::nullopt;
			}
			const std::vector<std::string> words = Words( Lower( szValue ) );
			for ( std::size_t i = 0; i < words.size(); i++ )
			{
				if ( !ParseNumber<float>( words[ i ] ) && words[ i ] != "center" && words[ i ] != "scale" && words[ i ] != "rotate" && words[ i ] != "translate" )
				{
					return "should read like \"center .5 .5 scale 1 1 rotate 0 translate 0 0\"";
				}
			}
			return std::nullopt;
		}
		case ParameterKind_t::PARAMETER_KIND_TEXTURE:
		case ParameterKind_t::PARAMETER_KIND_MATERIAL:
		{
			const std::string sz = Lower( szValue );
			if ( sz.starts_with( "materials/" ) || sz.starts_with( "materials\\" ) )
			{
				return "is written without materials/ in front";
			}
			return std::nullopt;
		}
		default: return std::nullopt;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Everything worth saying about a material's text, in the order of its lines.
//-----------------------------------------------------------------------------
std::vector<MaterialIssue_t> LintMaterial( std::string_view szText, const CParameterTable &table, const MaterialLintContext_t &context )
{
	using Severity_t = MaterialIssue_t::Severity_t;
	std::vector<MaterialIssue_t> issues;
	const CLines lines{ szText };
	const auto say = [ &issues ]( Severity_t nSeverity, std::size_t nLine, std::string szMessage ) { issues.push_back( { nSeverity, nLine, std::move( szMessage ) } ); };
	const auto lineOf = [ &lines ]( const kv1::CToken &token ) { return lines.LineOf( token.m_nOffset ); };

	const kv1::CParseResult parsed = kv1::Parse( szText );
	for ( const kv1::Diagnostic_t &diagnostic : parsed.m_Diagnostics )
	{
		say( diagnostic.m_nSeverity == kv1::Diagnostic_t::Severity_t::SEVERITY_ERROR ? Severity_t::SEVERITY_ERROR : Severity_t::SEVERITY_WARNING, diagnostic.m_nLine, diagnostic.m_szMessage );
	}
	const std::vector<kv1::CNode> &roots = parsed.m_Document.m_Roots;
	const auto first = std::ranges::find_if( roots, &kv1::CNode::IsBlock );
	if ( first == roots.end() )
	{
		say( Severity_t::SEVERITY_ERROR, 0, "There is no shader block: a material starts with its shader's name, then its settings in { }." );
		return issues;
	}
	const kv1::CNode &root = *first;
	for ( auto it = std::next( first ); it != roots.end(); ++it )
	{
		if ( it->IsBlock() )
		{
			say( Severity_t::SEVERITY_WARNING, lineOf( it->GetKeyToken() ), std::format( "Only the first block is read; \"{}\" is left out.", it->GetKey() ) );
		}
	}
	const std::string szShader = root.GetKey();
	const bool bPatch = ShaderKind( szShader ) == "patch";
	const bool bKnownShader = table.IsKnownShader( szShader );
	const bool bChecked = table.IsChecked( szShader );
	if ( !bKnownShader )
	{
		say( Severity_t::SEVERITY_INFO, lineOf( root.GetKeyToken() ), std::format( "{} isn't a shader this editor knows; check that the game has it.", szShader ) );
	}
	if ( bPatch && !root.Find( "include" ) )
	{
		say( Severity_t::SEVERITY_WARNING, lineOf( root.GetKeyToken() ), "A patch material needs \"include\", the material it changes." );
	}

	// The material's own settings, and those of its fallback and patch blocks.
	std::vector<const kv1::CNode *> settings;
	std::vector<const kv1::CNode *> blocks{ &root };
	while ( !blocks.empty() )
	{
		const kv1::CNode *pBlock = blocks.back();
		blocks.pop_back();
		for ( const kv1::CNode &child : pBlock->GetChildren() )
		{
			if ( child.IsPair() )
			{
				settings.push_back( &child );
			}
			else if ( child.IsBlock() && kv1::EqualsIgnoreCase( child.GetKey(), "proxies" ) )
			{
				for ( const kv1::CNode &proxy : child.GetChildren() )
				{
					if ( proxy.IsPair() )
					{
						say( Severity_t::SEVERITY_WARNING, lineOf( proxy.GetKeyToken() ), std::format( "\"{}\" in Proxies should be a proxy's block: {} {{ ... }}.", proxy.GetKey(), proxy.GetKey() ) );
					}
				}
			}
			else if ( child.IsBlock() && IsParameterBlock( child.GetKey() ) )
			{
				blocks.push_back( &child );
			}
		}
	}
	std::ranges::sort( settings, {}, []( const kv1::CNode *pNode ) { return pNode->GetKeyToken().m_nOffset; } );

	std::map<std::string, std::size_t> seen; // lowercase key -> line, for the material's own block
	for ( const kv1::CNode *pSetting : settings )
	{
		const std::string szKey = pSetting->GetKey();
		const std::string szValue = pSetting->GetValue();
		const std::size_t nLine = lineOf( pSetting->GetKeyToken() );
		const bool bOwn = std::ranges::any_of( root.GetChildren(), [ pSetting ]( const kv1::CNode &child ) { return &child == pSetting; } );
		if ( bOwn && !pSetting->GetCondition() )
		{
			const auto [ it, bNew ] = seen.emplace( Lower( szKey ), nLine );
			if ( !bNew )
			{
				say( Severity_t::SEVERITY_WARNING, nLine, std::format( "{} is set again (first on line {}); only one of them counts.", szKey, it->second ) );
			}
		}
		if ( bPatch && kv1::EqualsIgnoreCase( szKey, "include" ) )
		{
			if ( context.m_pfnRead && !context.m_pfnRead( vfs::NormalizePath( szValue ) ) )
			{
				say( Severity_t::SEVERITY_WARNING, nLine, std::format( "The included material {} isn't found.", szValue ) );
			}
			continue;
		}
		const ParameterDef_t *pDef = table.Find( szKey );
		if ( !pDef )
		{
			// Unknown ones are often fine (every game adds its own); a near miss of a known one isn't.
			if ( szKey.starts_with( '$' ) || szKey.starts_with( '%' ) )
			{
				const std::string szLower = Lower( szKey );
				// One slip, or two in a long name.
				const ParameterDef_t *pNearest = nullptr;
				std::size_t nNearest = szKey.size() >= 12 ? 3 : 2;
				for ( const ParameterDef_t &known : table.GetParameters() )
				{
					const std::size_t nDistance = EditDistance( szLower, Lower( known.m_szKey ) );
					if ( nDistance < nNearest )
					{
						nNearest = nDistance;
						pNearest = &known;
					}
				}
				if ( pNearest && szKey.size() > 5 && bChecked )
				{
					say( Severity_t::SEVERITY_WARNING, nLine, std::format( "{} isn't a parameter this editor knows; did you mean {}?", szKey, pNearest->m_szKey ) );
				}
			}
			else if ( bChecked && szKey.find( '?' ) == std::string::npos )
			{
				if ( ParseNumber<float>( szKey ) )
				{
					say( Severity_t::SEVERITY_WARNING, nLine, std::format( "A number, {}, stands where a parameter's name goes; is a value split in pieces? A list is written \"[1 2 3]\".", szKey ) );
				}
				else if ( const ParameterDef_t *pMeant = table.Find( "$" + szKey ) )
				{
					say( Severity_t::SEVERITY_WARNING, nLine, std::format( "\"{}\" needs its $ to be read: {}.", szKey, pMeant->m_szKey ) );
				}
				else
				{
					say( Severity_t::SEVERITY_INFO, nLine, std::format( "\"{}\" doesn't start with $ or %, so the shader won't read it as a parameter.", szKey ) );
				}
			}
			continue;
		}
		if ( bChecked && !CParameterTable::IsFor( *pDef, szShader ) )
		{
			say( Severity_t::SEVERITY_INFO, nLine, std::format( "{} isn't one of {}'s parameters, so it may do nothing.", pDef->m_szKey, szShader ) );
		}
		if ( const auto problem = CheckParameterValue( pDef->m_nKind, szValue ) )
		{
			say( Severity_t::SEVERITY_WARNING, nLine, std::format( "{} {}; it is \"{}\".", pDef->m_szKey, *problem, szValue ) );
			continue;
		}
		const std::string szLowerValue = Lower( szValue );
		if ( ( pDef->m_nKind == ParameterKind_t::PARAMETER_KIND_TEXTURE && szLowerValue.ends_with( ".vtf" ) ) || ( pDef->m_nKind == ParameterKind_t::PARAMETER_KIND_MATERIAL && szLowerValue.ends_with( ".vmt" ) ) )
		{
			say( Severity_t::SEVERITY_INFO, nLine, std::format( "{} is usually written without the ending ({}).", pDef->m_szKey, szLowerValue.substr( szLowerValue.size() - 4 ) ) );
		}
		if ( pDef->m_nKind == ParameterKind_t::PARAMETER_KIND_SURFACE && !context.m_SurfaceProps.empty() && std::ranges::find( context.m_SurfaceProps, Lower( szValue ) ) == context.m_SurfaceProps.end() )
		{
			say( Severity_t::SEVERITY_WARNING, nLine, std::format( "The game has no surface property \"{}\"; \"default\" is used instead.", szValue ) );
		}
		if ( context.m_pfnRead && !szValue.empty() && ( pDef->m_nKind == ParameterKind_t::PARAMETER_KIND_TEXTURE || pDef->m_nKind == ParameterKind_t::PARAMETER_KIND_MATERIAL ) )
		{
			if ( pDef->m_nKind == ParameterKind_t::PARAMETER_KIND_TEXTURE && TextureKind( szValue ) != TextureKind_t::TEXTURE_KIND_FILE )
			{
				continue;
			}
			std::string szPath = TexturePath( szValue );
			if ( pDef->m_nKind == ParameterKind_t::PARAMETER_KIND_MATERIAL )
			{
				szPath.replace( szPath.size() - 4, 4, ".vmt" );
			}
			if ( !context.m_pfnRead( szPath ) )
			{
				say( Severity_t::SEVERITY_WARNING, nLine, std::format( "{} isn't found ({}).", szPath, pDef->m_szKey ) );
			}
		}
	}

	// What the alpha settings ask of the base texture.
	const kv1::CNode *pTranslucent = root.Find( "$translucent" );
	const kv1::CNode *pAlphaTest = root.Find( "$alphatest" );
	const auto isOn = []( const kv1::CNode *pNode ) { return pNode && pNode->IsPair() && pNode->GetValue() != "0" && !pNode->GetValue().empty(); };
	if ( isOn( pTranslucent ) && isOn( pAlphaTest ) )
	{
		say( Severity_t::SEVERITY_INFO, lineOf( pAlphaTest->GetKeyToken() ), "Both $translucent and $alphatest are on: texels under the threshold are cut away, the rest blend by their alpha." );
	}
	const kv1::CNode *pBase = root.Find( "$basetexture" );
	if ( bChecked && !pBase && table.Find( "$basetexture" ) && CParameterTable::IsFor( *table.Find( "$basetexture" ), szShader ) && ShaderKind( szShader ) != "water" && ShaderKind( szShader ) != "refract" )
	{
		say( Severity_t::SEVERITY_INFO, lineOf( root.GetKeyToken() ), "There is no $basetexture, so the surface has no picture." );
	}
	if ( pBase && pBase->IsPair() && context.m_pfnRead && TextureKind( pBase->GetValue() ) == TextureKind_t::TEXTURE_KIND_FILE )
	{
		const auto bytes = context.m_pfnRead( TexturePath( pBase->GetValue() ) );
		const auto vtf = bytes ? texture::ReadVtf( *bytes ) : std::nullopt;
		if ( vtf )
		{
			// DXT1 can hold one-bit alpha without saying so.
			const texture::VtfFormat_t nFormat = vtf->m_Info.m_nFormat;
			const bool bAlpha = texture::HasAlpha( nFormat ) || nFormat == texture::VtfFormat_t::DXT1 || ( vtf->m_Info.m_nFlags & ( texture::vtf_flag::ONEBITALPHA | texture::vtf_flag::EIGHTBITALPHA ) );
			for ( const kv1::CNode *pAlphaUser : { pTranslucent, pAlphaTest } )
			{
				if ( isOn( pAlphaUser ) && !bAlpha && !root.Find( "$alpha" ) && !root.Find( "$vertexalpha" ) )
				{
					say( Severity_t::SEVERITY_WARNING, lineOf( pAlphaUser->GetKeyToken() ),
						std::format( "{} uses the base texture's alpha, but {} ({}) has none.", pAlphaUser->GetKey(), pBase->GetValue(), texture::FormatName( vtf->m_Info.m_nFormat ) ) );
				}
			}
			say( Severity_t::SEVERITY_INFO, lineOf( pBase->GetKeyToken() ), std::format( "$basetexture is {}×{} {}{}.", vtf->m_Info.m_nWidth, vtf->m_Info.m_nHeight, texture::FormatName( vtf->m_Info.m_nFormat ), vtf->m_Info.m_nFrames > 1 ? std::format( ", {} frames", vtf->m_Info.m_nFrames ) : std::string{} ) );
		}
	}
	std::ranges::stable_sort( issues, {}, &MaterialIssue_t::m_nLine );
	return issues;
}

} // namespace xv::material
