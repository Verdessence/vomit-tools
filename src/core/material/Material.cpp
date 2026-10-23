//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Material.h.
//
//===========================================================================//
#include "Material.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <numbers>

#include "kv1/Kv1.h"
#include "util/Numbers.h"
#include "vfs/Vfs.h"

namespace xv::material
{

namespace
{

constexpr int MAX_INCLUDE_DEPTH = 8;

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
// Purpose: A number the way the engine's atof reads one: as much of the front as makes a number,
//          0 for none.
//-----------------------------------------------------------------------------
std::optional<float> ReadNumber( std::string_view szText )
{
	while ( !szText.empty() && ( szText.front() == ' ' || szText.front() == '\t' ) )
	{
		szText.remove_prefix( 1 );
	}
	for ( std::size_t nLength = szText.size(); nLength > 0; nLength-- )
	{
		if ( const auto value = ParseNumber<float>( szText.substr( 0, nLength ) ) )
		{
			return value;
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: Whether a value is only numbers ("1", "[0.5 0.5 1]"), not a name that starts with one.
//-----------------------------------------------------------------------------
bool IsNumbers( std::string_view szText )
{
	bool bAny = false;
	std::string szToken;
	for ( std::size_t i = 0; i <= szText.size(); i++ )
	{
		const char c = i < szText.size() ? szText[ i ] : ' ';
		if ( c == ' ' || c == '\t' || c == '[' || c == ']' || c == '{' || c == '}' )
		{
			if ( !szToken.empty() && !ParseNumber<float>( szToken ) )
			{
				return false;
			}
			bAny |= !szToken.empty();
			szToken.clear();
		}
		else
		{
			szToken += c;
		}
	}
	return bAny;
}

//-----------------------------------------------------------------------------
// Purpose: The numbers in a value, brackets and braces left out.
//-----------------------------------------------------------------------------
std::vector<float> SplitNumbers( std::string_view szText )
{
	std::vector<float> values;
	std::string szToken;
	const auto flush = [ & ] {
		if ( !szToken.empty() && values.size() < 4 )
		{
			values.push_back( ReadNumber( szToken ).value_or( 0.0f ) );
		}
		szToken.clear();
	};
	for ( const char c : szText )
	{
		if ( c == ' ' || c == '\t' || c == '[' || c == ']' || c == '{' || c == '}' )
		{
			flush();
		}
		else
		{
			szToken += c;
		}
	}
	flush();
	return values;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float GammaToLinear( float flValue )
{
	flValue = std::clamp( flValue, 0.0f, 1.0f );
	return flValue <= 0.04045f ? flValue / 12.92f : std::pow( ( flValue + 0.055f ) / 1.055f, 2.4f );
}

//-----------------------------------------------------------------------------
// Purpose: A KeyValues condition ("[$X360]", "[!$WIN32]") on a PC running Windows. $WIN32 stands
//          for any PC, as the engine reads it.
//-----------------------------------------------------------------------------
bool ConditionHolds( std::string_view szCondition )
{
	std::string sz = Lower( szCondition );
	std::erase_if( sz, []( char c ) { return c == '[' || c == ']' || c == ' '; } );
	const bool bNot = sz.starts_with( '!' );
	if ( bNot )
	{
		sz.erase( 0, 1 );
	}
	const bool bTrue = sz == "$win32" || sz == "$windows";
	return bTrue != bNot;
}

// A block of fallback parameters, by its name: "LightmappedGeneric_DX9", "Water_HDR_DX9", ">=dx90".
struct Fallback_t
{
	bool m_bIsFallback = false;
	bool m_bApplies = false;
	bool m_bHdr = false; // only with HDR on; applied after the others
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Fallback_t ReadFallback( std::string_view szBlock, Hardware_t hardware )
{
	const std::string sz = Lower( szBlock );
	// ">=dx90", "<dx90_20b": the DirectX level compared, and whether shader model 2.0b is asked for.
	if ( sz.starts_with( '<' ) || sz.starts_with( '>' ) )
	{
		std::size_t i = 0;
		while ( i < sz.size() && ( sz[ i ] == '<' || sz[ i ] == '>' || sz[ i ] == '=' ) )
		{
			i++;
		}
		const std::string szOperator = sz.substr( 0, i );
		std::string szRest = sz.substr( i );
		if ( !szRest.starts_with( "dx" ) )
		{
			return {};
		}
		szRest.erase( 0, 2 );
		const bool b20b = szRest.ends_with( "_20b" );
		if ( b20b )
		{
			szRest.resize( szRest.size() - 4 );
		}
		const auto level = ParseNumber<int>( szRest );
		if ( !level )
		{
			return {};
		}
		// 2.0b counts as a level just above 90.
		const int nWanted = *level * 10 + ( b20b ? 5 : 0 );
		const int nHave = hardware.m_nDxLevel * 10;
		bool bHolds = false;
		if ( szOperator == ">=" )
		{
			bHolds = nHave >= nWanted;
		}
		else if ( szOperator == ">" )
		{
			bHolds = nHave > nWanted;
		}
		else if ( szOperator == "<=" )
		{
			bHolds = nHave <= nWanted;
		}
		else if ( szOperator == "<" )
		{
			bHolds = nHave < nWanted;
		}
		return { true, bHolds, false };
	}
	// "<shader>_dx9", "<shader>_hdr_dx9", "<shader>_dx9_hdr", "<shader>_nobump_dx8".
	const std::size_t nDx = sz.rfind( "_dx" );
	if ( nDx == std::string::npos )
	{
		return {};
	}
	std::string szLevel = sz.substr( nDx + 3 );
	bool bHdr = sz.substr( 0, nDx ).ends_with( "_hdr" );
	if ( szLevel.ends_with( "_hdr" ) )
	{
		bHdr = true;
		szLevel.resize( szLevel.size() - 4 );
	}
	const auto level = ParseNumber<int>( szLevel );
	if ( !level )
	{
		return {};
	}
	const int nLevel = *level < 10 ? *level * 10 : *level; // dx9 is 90, dx81 is 81
	const bool bNoBump = sz.substr( 0, nDx ).ends_with( "_nobump" );
	return { true, nLevel >= 90 && nLevel <= hardware.m_nDxLevel && !bNoBump && ( !bHdr || hardware.m_bHdr ), bHdr };
}

//-----------------------------------------------------------------------------
// Purpose: Reads a block's parameters into the material.
//-----------------------------------------------------------------------------
void ReadParameters( CMaterial &material, const kv1::CNode &block, const std::string &szFrom, Hardware_t hardware, std::vector<std::pair<const kv1::CNode *, bool>> *pFallbacks )
{
	for ( const kv1::CNode &child : block.GetChildren() )
	{
		if ( const auto condition = child.GetCondition(); condition && !ConditionHolds( *condition ) )
		{
			material.m_Notes.push_back( std::format( "{} is left out: it is only for {}", child.GetKey(), *condition ) );
			continue;
		}
		if ( child.IsPair() )
		{
			std::string szKey = child.GetKey();
			// "srgb?$fogcolor" is for hardware that reads textures as sRGB, as DirectX 9 PCs do;
			// "360?$color2" is for the Xbox 360.
			if ( const std::size_t nQuestion = szKey.find( '?' ); nQuestion != std::string::npos )
			{
				const std::string szWhen = Lower( szKey.substr( 0, nQuestion ) );
				if ( szWhen != "srgb" )
				{
					material.m_Notes.push_back( std::format( "{} is left out: it is only for {}", szKey, szKey.substr( 0, nQuestion ) ) );
					continue;
				}
				szKey.erase( 0, nQuestion + 1 );
			}
			material.Set( szKey, child.GetValue() );
			if ( !szFrom.empty() )
			{
				std::ranges::find_if( material.m_Parameters, [ & ]( const Parameter_t &p ) { return kv1::EqualsIgnoreCase( p.m_szKey, szKey ); } )->m_szFrom = szFrom;
			}
			continue;
		}
		if ( !child.IsBlock() )
		{
			continue;
		}
		const std::string szBlock = Lower( child.GetKey() );
		if ( szBlock == "proxies" )
		{
			for ( const kv1::CNode &proxy : child.GetChildren() )
			{
				if ( !proxy.IsBlock() )
				{
					continue;
				}
				Proxy_t entry{ proxy.GetKey(), {} };
				for ( const kv1::CNode &setting : proxy.GetChildren() )
				{
					if ( setting.IsPair() )
					{
						entry.m_Settings.emplace_back( setting.GetKey(), setting.GetValue() );
					}
				}
				material.m_Proxies.push_back( std::move( entry ) );
			}
			continue;
		}
		const Fallback_t fallback = ReadFallback( child.GetKey(), hardware );
		if ( !fallback.m_bIsFallback )
		{
			material.m_Notes.push_back( std::format( "The block {} isn't read", child.GetKey() ) );
			continue;
		}
		if ( !fallback.m_bApplies )
		{
			material.m_Notes.push_back( std::format( "{} is left out: it is for other hardware or settings", child.GetKey() ) );
			continue;
		}
		if ( pFallbacks )
		{
			pFallbacks->emplace_back( &child, fallback.m_bHdr );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<CMaterial> Parse( std::string_view szText, const Reader_t &pfnRead, Hardware_t hardware, std::string *pError, int nDepth )
{
	const kv1::CDocument document = kv1::Parse( szText ).m_Document;
	const auto root = std::ranges::find_if( document.m_Roots, &kv1::CNode::IsBlock );
	if ( root == document.m_Roots.end() )
	{
		if ( pError )
		{
			*pError = "no shader block";
		}
		return std::nullopt;
	}
	CMaterial material;
	if ( Lower( root->GetKey() ) == "patch" )
	{
		const kv1::CNode *pInclude = root->Find( "include" );
		const std::string szInclude = pInclude && pInclude->IsPair() ? vfs::NormalizePath( pInclude->GetValue() ) : std::string{};
		std::optional<CMaterial> included;
		if ( szInclude.empty() )
		{
			material.m_Notes.push_back( "It is a patch that includes nothing" );
		}
		else if ( nDepth >= MAX_INCLUDE_DEPTH )
		{
			material.m_Notes.push_back( std::format( "{} isn't read: patches include each other too deeply", szInclude ) );
		}
		else if ( const auto bytes = pfnRead ? pfnRead( szInclude ) : std::nullopt; !bytes )
		{
			material.m_Notes.push_back( std::format( "It includes {}, which isn't there", szInclude ) );
		}
		else if ( !( included = Parse( std::string_view{ reinterpret_cast<const char *>( bytes->data() ), bytes->size() }, pfnRead, hardware, nullptr, nDepth + 1 ) ) )
		{
			material.m_Notes.push_back( std::format( "It includes {}, which has no shader block", szInclude ) );
		}
		if ( included )
		{
			material = std::move( *included );
			for ( Parameter_t &parameter : material.m_Parameters )
			{
				if ( parameter.m_szFrom.empty() )
				{
					parameter.m_szFrom = szInclude;
				}
			}
			material.m_Notes.insert( material.m_Notes.begin(), std::format( "It is a patch of {}", szInclude ) );
		}
		else
		{
			material.m_szShader = "patch";
		}
		for ( const char *pszBlock : { "insert", "replace" } )
		{
			if ( const kv1::CNode *pBlock = root->Find( pszBlock ); pBlock && pBlock->IsBlock() )
			{
				ReadParameters( material, *pBlock, {}, hardware, nullptr );
			}
		}
		return material;
	}
	material.m_szShader = root->GetKey();
	std::vector<std::pair<const kv1::CNode *, bool>> fallbacks;
	ReadParameters( material, *root, {}, hardware, &fallbacks );
	// The fallback blocks override the material's own parameters, HDR ones last.
	std::ranges::stable_partition( fallbacks, []( const auto &entry ) { return !entry.second; } );
	for ( const auto &[ pBlock, bHdr ] : fallbacks )
	{
		ReadParameters( material, *pBlock, pBlock->GetKey(), hardware, nullptr );
		material.m_Notes.push_back( std::format( "{} is used", pBlock->GetKey() ) );
	}
	return material;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const std::string *Setting( const Proxy_t &proxy, std::string_view szKey )
{
	const auto it = std::ranges::find_if( proxy.m_Settings, [ szKey ]( const auto &setting ) { return kv1::EqualsIgnoreCase( setting.first, szKey ); } );
	return it == proxy.m_Settings.end() ? nullptr : &it->second;
}

//-----------------------------------------------------------------------------
// Purpose: What a variable reads as when the material doesn't set it.
//-----------------------------------------------------------------------------
std::vector<float> DefaultOf( std::string_view szVariable )
{
	const std::string sz = Lower( szVariable );
	if ( sz == "$color" || sz == "$color2" || sz == "$envmaptint" || sz == "$selfillumtint" || sz == "$refracttint" || sz == "$detailtint" || sz == "$phongtint" )
	{
		return { 1, 1, 1 };
	}
	if ( sz == "$alpha" || sz == "$detailblendfactor" || sz == "$envmapsaturation" )
	{
		return { 1 };
	}
	return { 0 };
}

//-----------------------------------------------------------------------------
// Purpose: "$color[1]" is "$color" and 1.
//-----------------------------------------------------------------------------
std::pair<std::string, int> SplitIndex( std::string_view szVariable )
{
	const std::size_t nOpen = szVariable.find( '[' );
	if ( nOpen == std::string_view::npos || !szVariable.ends_with( ']' ) )
	{
		return { std::string{ szVariable }, -1 };
	}
	const auto index = ParseNumber<int>( szVariable.substr( nOpen + 1, szVariable.size() - nOpen - 2 ) );
	return { std::string{ szVariable.substr( 0, nOpen ) }, index && *index >= 0 && *index < 4 ? *index : -1 };
}

//-----------------------------------------------------------------------------
// Purpose: A variable's numbers, a literal number, or a component of a vector.
//-----------------------------------------------------------------------------
std::vector<float> ReadVariable( const CMaterial &material, std::string_view szVariable )
{
	if ( !szVariable.starts_with( '$' ) )
	{
		return { ReadNumber( szVariable ).value_or( 0.0f ) };
	}
	const auto [ szName, nIndex ] = SplitIndex( szVariable );
	std::vector<float> values = material.Has( szName ) ? material.Numbers( szName ) : DefaultOf( szName );
	if ( values.empty() )
	{
		values = DefaultOf( szName );
	}
	if ( nIndex >= 0 )
	{
		return { static_cast<std::size_t>( nIndex ) < values.size() ? values[ static_cast<std::size_t>( nIndex ) ] : 0.0f };
	}
	return values;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string FormatNumbers( const std::vector<float> &values )
{
	if ( values.size() == 1 )
	{
		return std::format( "{}", values[ 0 ] );
	}
	std::string szOut = "[";
	for ( std::size_t i = 0; i < values.size(); i++ )
	{
		szOut += std::format( "{}{}", i ? " " : "", values[ i ] );
	}
	return szOut + "]";
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void WriteVariable( CMaterial &material, std::string_view szVariable, const std::vector<float> &values )
{
	if ( szVariable.empty() || values.empty() )
	{
		return;
	}
	const auto [ szName, nIndex ] = SplitIndex( szVariable );
	if ( nIndex < 0 )
	{
		material.Set( szName, FormatNumbers( values ) );
		return;
	}
	std::vector<float> whole = ReadVariable( material, szName );
	whole.resize( std::max( whole.size(), static_cast<std::size_t>( nIndex ) + 1 ), 0.0f );
	whole[ static_cast<std::size_t>( nIndex ) ] = values[ 0 ];
	material.Set( szName, FormatNumbers( whole ) );
}

//-----------------------------------------------------------------------------
// Purpose: Applies op to each component; a single number goes with every component of the other.
//-----------------------------------------------------------------------------
template <typename Op_t>
std::vector<float> Combine( const std::vector<float> &a, const std::vector<float> &b, Op_t op )
{
	const std::size_t n = std::max( a.size(), b.size() );
	std::vector<float> out( n );
	for ( std::size_t i = 0; i < n; i++ )
	{
		out[ i ] = op( a[ a.size() == 1 ? 0 : std::min( i, a.size() - 1 ) ], b[ b.size() == 1 ? 0 : std::min( i, b.size() - 1 ) ] );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: A repeatable number in 0-1 for a moment and a proxy, so noise changes with time but a
//          frame drawn twice looks the same.
//-----------------------------------------------------------------------------
float Noise( float flTime, std::size_t nProxy, std::uint32_t nSalt )
{
	std::uint32_t n = static_cast<std::uint32_t>( std::floor( flTime * 30.0f ) ) * 747796405u + static_cast<std::uint32_t>( nProxy ) * 2891336453u + nSalt * 277803737u;
	n ^= n >> 16;
	n *= 0x7feb352du;
	n ^= n >> 15;
	n *= 0x846ca68bu;
	n ^= n >> 16;
	return static_cast<float>( n >> 8 ) / 16777216.0f;
}

//-----------------------------------------------------------------------------
// Purpose: Whether the preview runs a proxy, and if so whether it changes over time by itself.
//-----------------------------------------------------------------------------
std::optional<bool> RunsOverTime( std::string_view szProxy )
{
	static const std::map<std::string, bool, std::less<>> PROXIES = {
		{ "sine", true },
		{ "linearramp", true },
		{ "currenttime", true },
		{ "uniformnoise", true },
		{ "gaussiannoise", true },
		{ "animatedtexture", true },
		{ "texturescroll", true },
		{ "add", false },
		{ "subtract", false },
		{ "multiply", false },
		{ "divide", false },
		{ "equals", false },
		{ "abs", false },
		{ "frac", false },
		{ "int", false },
		{ "clamp", false },
		{ "lessorequal", false },
		{ "selectfirstifnonzero", false },
		{ "wrapminmax", false },
		{ "exponential", false },
		{ "entityrandom", false },
		{ "texturetransform", false },
		{ "waterlod", false },
	};
	const auto it = PROXIES.find( Lower( szProxy ) );
	return it == PROXIES.end() ? std::nullopt : std::optional{ it->second };
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const Parameter_t *CMaterial::Find( std::string_view szKey ) const
{
	const auto it = std::ranges::find_if( m_Parameters, [ szKey ]( const Parameter_t &p ) { return kv1::EqualsIgnoreCase( p.m_szKey, szKey ); } );
	return it == m_Parameters.end() ? nullptr : &*it;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string CMaterial::Value( std::string_view szKey ) const
{
	const Parameter_t *pParameter = Find( szKey );
	return pParameter ? pParameter->m_szValue : std::string{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float CMaterial::Number( std::string_view szKey, float flDefault ) const
{
	const std::vector<float> values = Numbers( szKey );
	return values.empty() ? flDefault : values[ 0 ];
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMaterial::Flag( std::string_view szKey ) const
{
	return Number( szKey, 0.0f ) != 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose: "[r g b]" (0-1, linear), "{r g b}" (0-255, gamma-space, made linear) or one number for
//          all three.
//-----------------------------------------------------------------------------
CVec3 CMaterial::Color( std::string_view szKey, CVec3 vecDefault ) const
{
	const Parameter_t *pParameter = Find( szKey );
	if ( !pParameter )
	{
		return vecDefault;
	}
	std::vector<float> values = SplitNumbers( pParameter->m_szValue );
	if ( values.empty() )
	{
		return vecDefault;
	}
	values.resize( 3, values.back() );
	if ( pParameter->m_szValue.find( '{' ) != std::string::npos )
	{
		for ( float &flValue : values )
		{
			flValue = GammaToLinear( flValue / 255.0f );
		}
	}
	return { values[ 0 ], values[ 1 ], values[ 2 ] };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<float> CMaterial::Numbers( std::string_view szKey ) const
{
	const Parameter_t *pParameter = Find( szKey );
	return pParameter ? SplitNumbers( pParameter->m_szValue ) : std::vector<float>{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterial::Set( std::string_view szKey, std::string_view szValue )
{
	const auto it = std::ranges::find_if( m_Parameters, [ szKey ]( const Parameter_t &p ) { return kv1::EqualsIgnoreCase( p.m_szKey, szKey ); } );
	if ( it != m_Parameters.end() )
	{
		it->m_szValue = szValue;
		it->m_szFrom.clear();
		return;
	}
	m_Parameters.push_back( { std::string{ szKey }, std::string{ szValue }, {} } );
}

//-----------------------------------------------------------------------------
// Purpose: The shader's name, lowercase, without a _dx9-style suffix.
//-----------------------------------------------------------------------------
std::string CMaterial::GetShaderKind() const
{
	std::string sz = Lower( m_szShader );
	const Fallback_t fallback = ReadFallback( sz, {} );
	if ( fallback.m_bIsFallback && !sz.starts_with( '<' ) && !sz.starts_with( '>' ) )
	{
		sz.resize( sz.rfind( "_dx" ) );
		for ( const char *pszSuffix : { "_hdr", "_nobump" } )
		{
			if ( sz.ends_with( pszSuffix ) )
			{
				sz.resize( sz.size() - std::string_view{ pszSuffix }.size() );
			}
		}
	}
	return sz;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<CMaterial> ParseMaterial( std::string_view szText, const Reader_t &pfnRead, Hardware_t hardware, std::string *pError )
{
	return Parse( szText, pfnRead, hardware, pError, 0 );
}

//-----------------------------------------------------------------------------
// Purpose: Whether a parameter names a texture: by its key, and a value that isn't a number.
//-----------------------------------------------------------------------------
bool IsTexture( std::string_view szKey, std::string_view szValue )
{
	if ( szValue.empty() || IsNumbers( szValue ) )
	{
		return false;
	}
	static const std::vector<std::string_view> TEXTURES = {
		"$basetexture", "$basetexture2", "$basetexture3", "$basetexture4", "$texture1", "$texture2", "$texture3",
		"$detail", "$detail2", "$bumpmap", "$bumpmap2", "$normalmap", "$normalmap2", "$envmap", "$envmapmask",
		"$envmapmask2", "$selfillummask", "$phongexponenttexture", "$phongwarptexture", "$lightwarptexture",
		"$hdrbasetexture", "$hdrcompressedtexture", "$hdrcompressedtexture0", "$hdrcompressedtexture1",
		"$hdrcompressedtexture2", "$blendmodulatetexture", "$iris", "$corneatexture", "$ambientoccltexture",
		"$ambientocclusiontexture", "$refracttinttexture", "$dudvmap", "$refracttexture", "$reflecttexture",
		"$tintmasktexture", "$flowmap", "$flow_noise_texture", "$bumpmask", "$parallaxmap", "$displacementmap",
		"$ramptexture", "$fbtexture"
	};
	const std::string sz = Lower( szKey );
	return std::ranges::find( TEXTURES, sz ) != TEXTURES.end() || ( sz.starts_with( '$' ) && ( sz.ends_with( "texture" ) || sz.ends_with( "map" ) || sz.ends_with( "mask" ) ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
TextureKind_t TextureKind( std::string_view szValue )
{
	const std::string sz = Lower( szValue );
	if ( sz == "env_cubemap" )
	{
		return TextureKind_t::TEXTURE_KIND_MAP_CUBEMAP;
	}
	if ( sz.starts_with( "_rt_" ) )
	{
		return TextureKind_t::TEXTURE_KIND_RENDER_TARGET;
	}
	return TextureKind_t::TEXTURE_KIND_FILE;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string TexturePath( std::string_view szValue )
{
	std::string szPath = vfs::NormalizePath( szValue );
	if ( szPath.starts_with( '/' ) )
	{
		szPath.erase( 0, 1 );
	}
	if ( !szPath.starts_with( "materials/" ) )
	{
		szPath = "materials/" + szPath;
	}
	if ( !szPath.ends_with( ".vtf" ) )
	{
		szPath += ".vtf";
	}
	return szPath;
}

//-----------------------------------------------------------------------------
// Purpose: Reads game paths from the game folder a material on disk sits in, then from the game.
//-----------------------------------------------------------------------------
Reader_t DiskReader( const std::filesystem::path &material, const vfs::CFileSystem *pFileSystem )
{
	std::filesystem::path root;
	for ( std::filesystem::path dir = material.parent_path(); !dir.empty() && dir != dir.parent_path(); dir = dir.parent_path() )
	{
		if ( Lower( vfs::ToUtf8( dir.filename() ) ) == "materials" )
		{
			root = dir.parent_path();
			break;
		}
	}
	return [ root, pFileSystem ]( std::string_view szGamePath ) -> std::optional<std::vector<std::byte>> {
		if ( !root.empty() )
		{
			if ( std::ifstream stream{ vfs::FindCaseInsensitive( root / vfs::FromUtf8( szGamePath ) ), std::ios::binary } )
			{
				const std::vector<char> chars{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
				std::vector<std::byte> bytes( chars.size() );
				std::ranges::transform( chars, bytes.begin(), []( char c ) { return static_cast<std::byte>( c ); } );
				return bytes;
			}
		}
		return pFileSystem ? pFileSystem->Read( szGamePath ) : std::nullopt;
	};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CProxies::CProxies( const CMaterial &material )
	: m_Material( material )
{
	for ( const Proxy_t &proxy : material.m_Proxies )
	{
		const auto overTime = RunsOverTime( proxy.m_szName );
		if ( !overTime )
		{
			m_Skipped.push_back( proxy.m_szName );
			continue;
		}
		const auto rate = [ & ]( std::string_view szKey ) {
			const std::string *pValue = Setting( proxy, szKey );
			return pValue ? ReadVariable( material, *pValue ).front() : 0.0f;
		};
		const std::string szName = Lower( proxy.m_szName );
		if ( szName == "animatedtexture" )
		{
			m_bAnimated |= rate( "animatedtextureframerate" ) != 0.0f;
		}
		else if ( szName == "texturescroll" )
		{
			m_bAnimated |= rate( "texturescrollrate" ) != 0.0f;
		}
		else
		{
			m_bAnimated |= *overTime;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: The material with the proxies' results at a time written into its parameters.
//-----------------------------------------------------------------------------
CMaterial CProxies::At( float flTime, const FrameCount_t &pfnFrames ) const
{
	CMaterial material = m_Material;
	for ( std::size_t nProxy = 0; nProxy < material.m_Proxies.size(); nProxy++ )
	{
		const Proxy_t &proxy = m_Material.m_Proxies[ nProxy ];
		const std::string szName = Lower( proxy.m_szName );
		const auto text = [ &proxy ]( std::string_view szKey ) {
			const std::string *pValue = Setting( proxy, szKey );
			return pValue ? *pValue : std::string{};
		};
		const auto read = [ & ]( std::string_view szKey, float flDefault = 0.0f ) {
			const std::string *pValue = Setting( proxy, szKey );
			return pValue ? ReadVariable( material, *pValue ) : std::vector<float>{ flDefault };
		};
		const auto first = [ & ]( std::string_view szKey, float flDefault = 0.0f ) { return read( szKey, flDefault ).front(); };
		const std::string szResult = text( "resultvar" );
		const auto write = [ & ]( const std::vector<float> &values ) { WriteVariable( material, szResult, values ); };
		if ( szName == "sine" )
		{
			const float flPeriod = first( "sineperiod", 1.0f );
			const float flMin = first( "sinemin" );
			const float flMax = first( "sinemax", 1.0f );
			const float flPhase = flPeriod != 0.0f ? 2.0f * std::numbers::pi_v<float> / flPeriod * ( flTime + first( "timeoffset" ) ) : 0.0f;
			write( { flMin + ( flMax - flMin ) * 0.5f * ( 1.0f + std::sin( flPhase ) ) } );
		}
		else if ( szName == "linearramp" )
		{
			write( { first( "rate", 1.0f ) * flTime + first( "initialvalue" ) } );
		}
		else if ( szName == "currenttime" )
		{
			write( { flTime } );
		}
		else if ( szName == "uniformnoise" )
		{
			const float flMin = first( "minval" );
			write( { flMin + ( first( "maxval", 1.0f ) - flMin ) * Noise( flTime, nProxy, 1 ) } );
		}
		else if ( szName == "gaussiannoise" )
		{
			// Box-Muller; halfWidth is where the curve falls to 30% of its peak (1.55 deviations).
			const float u1 = std::max( Noise( flTime, nProxy, 1 ), 1e-6f );
			const float u2 = Noise( flTime, nProxy, 2 );
			const float flGauss = std::sqrt( -2.0f * std::log( u1 ) ) * std::cos( 2.0f * std::numbers::pi_v<float> * u2 );
			const float flValue = first( "mean" ) + flGauss * first( "halfwidth", 1.0f ) / 1.55f;
			write( { std::clamp( flValue, first( "minval", -1e9f ), first( "maxval", 1e9f ) ) } );
		}
		else if ( szName == "add" || szName == "subtract" || szName == "multiply" || szName == "divide" )
		{
			const std::vector<float> a = read( "srcvar1" );
			const std::vector<float> b = read( "srcvar2" );
			if ( szName == "add" )
			{
				write( Combine( a, b, []( float x, float y ) { return x + y; } ) );
			}
			else if ( szName == "subtract" )
			{
				write( Combine( a, b, []( float x, float y ) { return x - y; } ) );
			}
			else if ( szName == "multiply" )
			{
				write( Combine( a, b, []( float x, float y ) { return x * y; } ) );
			}
			else
			{
				write( Combine( a, b, []( float x, float y ) { return y != 0.0f ? x / y : 0.0f; } ) );
			}
		}
		else if ( szName == "equals" || szName == "abs" || szName == "frac" || szName == "int" )
		{
			std::vector<float> values = read( "srcvar1" );
			for ( float &v : values )
			{
				v = szName == "abs" ? std::abs( v ) : szName == "frac" ? v - std::floor( v )
					: szName == "int"                                  ? std::trunc( v )
																	   : v;
			}
			write( values );
		}
		else if ( szName == "clamp" )
		{
			std::vector<float> values = read( "srcvar1" );
			const float flMin = first( "min" );
			const float flMax = std::max( flMin, first( "max", 1.0f ) );
			for ( float &v : values )
			{
				v = std::clamp( v, flMin, flMax );
			}
			write( values );
		}
		else if ( szName == "lessorequal" )
		{
			write( first( "srcvar1" ) <= first( "srcvar2" ) ? read( "lessequalvar" ) : read( "greatervar" ) );
		}
		else if ( szName == "selectfirstifnonzero" )
		{
			const std::vector<float> a = read( "srcvar1" );
			write( std::ranges::any_of( a, []( float v ) { return v != 0.0f; } ) ? a : read( "srcvar2" ) );
		}
		else if ( szName == "wrapminmax" )
		{
			const float flMin = first( "minval" );
			const float flMax = first( "maxval", 1.0f );
			const float flValue = first( "srcvar1" );
			write( { flMax <= flMin ? flMin : flMin + std::fmod( std::fmod( flValue - flMin, flMax - flMin ) + ( flMax - flMin ), flMax - flMin ) } );
		}
		else if ( szName == "exponential" )
		{
			const float flValue = first( "scale", 1.0f ) * std::exp( first( "srcvar1" ) + first( "offset" ) );
			write( { std::clamp( flValue, first( "minval", -1e9f ), first( "maxval", 1e9f ) ) } );
		}
		else if ( szName == "entityrandom" )
		{
			write( { 0.5f * first( "scale", 1.0f ) } ); // one entity's number; each gets its own in game
		}
		else if ( szName == "animatedtexture" )
		{
			const std::string szTexture = text( "animatedtexturevar" );
			const std::string szFrame = text( "animatedtextureframenumvar" );
			const int nFrames = pfnFrames && !szTexture.empty() ? pfnFrames( szTexture ) : 1;
			if ( nFrames > 1 && !szFrame.empty() )
			{
				const auto nFrame = static_cast<long long>( std::floor( flTime * first( "animatedtextureframerate", 15.0f ) ) );
				const long long nShown = first( "animationnowrap" ) != 0.0f ? std::clamp( nFrame, 0LL, static_cast<long long>( nFrames - 1 ) ) : ( ( nFrame % nFrames ) + nFrames ) % nFrames;
				WriteVariable( material, szFrame, { static_cast<float>( nShown ) } );
			}
		}
		else if ( szName == "texturescroll" )
		{
			const float flOffset = first( "texturescrollrate" ) * flTime;
			const float flAngle = first( "texturescrollangle" ) / 180.0f * std::numbers::pi_v<float>;
			const float flScale = first( "texturescale", 1.0f );
			const std::string szTarget = text( "texturescrollvar" );
			if ( !szTarget.empty() )
			{
				// Wrapped into 0-1, without the float error of cos(90) showing as 0.99999994.
				const auto wrap = []( float f ) {
					const float flWrapped = f - std::floor( f );
					return flWrapped < 1e-6f || flWrapped > 1.0f - 1e-6f ? 0.0f : flWrapped;
				};
				const float flU = wrap( std::cos( flAngle ) * flOffset );
				const float flV = wrap( std::sin( flAngle ) * flOffset );
				// A transform parameter gets a moving matrix; any other variable just the offset.
				if ( Lower( szTarget ).find( "transform" ) != std::string::npos )
				{
					material.Set( SplitIndex( szTarget ).first, std::format( "center 0 0 scale {} {} rotate 0 translate {} {}", flScale, flScale, flU, flV ) );
				}
				else
				{
					WriteVariable( material, szTarget, { flU, flV } );
				}
			}
		}
		else if ( szName == "texturetransform" )
		{
			const auto pair = [ & ]( std::string_view szKey, float flDefault ) {
				std::vector<float> values = Setting( proxy, szKey ) ? read( szKey ) : std::vector<float>{ flDefault };
				values.resize( 2, values.back() );
				return values;
			};
			const std::vector<float> centre = pair( "centervar", 0.5f );
			const std::vector<float> scale = pair( "scalevar", 1.0f );
			const std::vector<float> translate = pair( "translatevar", 0.0f );
			const float flRotate = Setting( proxy, "rotatevar" ) ? first( "rotatevar" ) : 0.0f;
			if ( !szResult.empty() )
			{
				material.Set( SplitIndex( szResult ).first, std::format( "center {} {} scale {} {} rotate {} translate {} {}", centre[ 0 ], centre[ 1 ], scale[ 0 ], scale[ 1 ], flRotate, translate[ 0 ], translate[ 1 ] ) );
			}
		}
	}
	return material;
}

} // namespace xv::material
