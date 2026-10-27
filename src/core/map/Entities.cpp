//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Entities.h. Which keys and helpers change the drawing follows the VDC's FGD
//          article.
//
//===========================================================================//
#include "Entities.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "util/Numbers.h"
#include "util/Scan.h"

namespace xv::map
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose: A material path as the renderer looks it up: no "materials/", no extension.
//-----------------------------------------------------------------------------
std::string MaterialName( std::string_view szPath )
{
	std::string sz{ szPath };
	std::ranges::replace( sz, '\\', '/' );
	std::string szLower = sz;
	std::ranges::transform( szLower, szLower.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	if ( szLower.starts_with( "materials/" ) )
	{
		sz.erase( 0, 10 );
		szLower.erase( 0, 10 );
	}
	if ( szLower.ends_with( ".vmt" ) || szLower.ends_with( ".spr" ) )
	{
		sz.resize( sz.size() - 4 );
	}
	return sz;
}

//-----------------------------------------------------------------------------
// Purpose: "r g b" (and anything after, such as a brightness), clamped to 0-255.
//-----------------------------------------------------------------------------
std::optional<Color_t> ParseColor( std::string_view szText )
{
	CScanner scanner{ szText };
	std::array<double, 3> values{};
	for ( double &flValue : values )
	{
		const auto value = scanner.Double();
		if ( !value || !std::isfinite( *value ) )
		{
			return std::nullopt;
		}
		flValue = std::clamp( *value, 0.0, 255.0 );
	}
	return Color_t{ static_cast<std::uint8_t>( values[ 0 ] ), static_cast<std::uint8_t>( values[ 1 ] ), static_cast<std::uint8_t>( values[ 2 ] ) };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsModelPath( std::string_view szPath )
{
	if ( szPath.size() < 4 )
	{
		return false;
	}
	std::string szEnd{ szPath.substr( szPath.size() - 4 ) };
	std::ranges::transform( szEnd, szEnd.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	return szEnd == ".mdl";
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::array<double, 3>> ParseTriple( std::string_view szText )
{
	CScanner scanner{ szText };
	std::array<double, 3> values{};
	for ( double &flValue : values )
	{
		const auto value = scanner.Double();
		if ( !value || !std::isfinite( *value ) )
		{
			return std::nullopt;
		}
		flValue = *value;
	}
	return values;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
EntityVisual_t DescribeEntity( const Entity_t &entity, const CFgd *pFgd )
{
	EntityVisual_t visual;
	visual.m_pClass = pFgd ? pFgd->Find( entity.GetClassname() ) : nullptr;
	visual.m_bPoint = entity.m_Solids.empty();
	visual.m_szName = entity.GetValue( "targetname" );
	if ( const auto origin = ParseTriple( entity.GetValue( "origin" ) ) )
	{
		visual.m_Origin = *origin;
	}
	if ( const auto angles = ParseTriple( entity.GetValue( "angles" ) ) )
	{
		visual.m_Angles = *angles;
	}
	else if ( const auto flAngle = ParseNumber<double>( entity.GetValue( "angle" ) ) )
	{
		// The single "angle" is a yaw, with -1 and -2 meaning straight up and straight down.
		if ( *flAngle == -1.0 )
		{
			visual.m_Angles = { -90.0, 0.0, 0.0 };
		}
		else if ( *flAngle == -2.0 )
		{
			visual.m_Angles = { 90.0, 0.0, 0.0 };
		}
		else
		{
			visual.m_Angles = { 0.0, *flAngle, 0.0 };
		}
	}
	// Lights keep their pitch in a key of its own, measured upwards.
	if ( const auto flPitch = ParseNumber<double>( entity.GetValue( "pitch" ) ); flPitch && entity.GetClassname().starts_with( "light" ) )
	{
		visual.m_Angles[ 0 ] = -*flPitch;
	}
	visual.m_Box.m_Mins = { -8, -8, -8 };
	visual.m_Box.m_Maxs = { 8, 8, 8 };
	visual.m_nSkin = ParseNumber<int>( entity.GetValue( "skin" ) ).value_or( 0 );
	for ( const char *szKey : { "modelscale", "uniformscale" } )
	{
		if ( const auto flScale = ParseNumber<double>( entity.GetValue( szKey ) ); flScale && *flScale > 0.0 )
		{
			visual.m_flModelScale = *flScale;
			break;
		}
	}

	const FgdClass_t *pClass = visual.m_pClass;
	if ( !pClass )
	{
		return visual;
	}
	if ( const FgdHelper_t *pColor = pClass->FindHelper( "color" ); pColor && !pColor->m_Arguments.empty() )
	{
		if ( const auto color = ParseColor( pColor->m_Arguments[ 0 ] ) )
		{
			visual.m_Color = *color;
			visual.m_bClassColor = true;
		}
	}
	if ( const FgdHelper_t *pSize = pClass->FindHelper( "size" ) )
	{
		if ( pSize->m_Arguments.size() == 2 )
		{
			const auto mins = ParseTriple( pSize->m_Arguments[ 0 ] );
			const auto maxs = ParseTriple( pSize->m_Arguments[ 1 ] );
			if ( mins && maxs )
			{
				visual.m_Box = {};
				visual.m_Box.Add( *mins );
				visual.m_Box.Add( *maxs );
			}
		}
		else if ( pSize->m_Arguments.size() == 1 )
		{
			// One size: a box that big, around the origin.
			if ( const auto size = ParseTriple( pSize->m_Arguments[ 0 ] ) )
			{
				visual.m_Box.m_Mins = Scale( *size, -0.5 );
				visual.m_Box.m_Maxs = Scale( *size, 0.5 );
			}
		}
	}
	for ( const FgdHelper_t &helper : pClass->m_Helpers )
	{
		const std::string_view szArgument = helper.m_Arguments.empty() ? std::string_view{} : std::string_view{ helper.m_Arguments[ 0 ] };
		if ( helper.m_szName == "studio" || helper.m_szName == "studioprop" || helper.m_szName == "lightprop" )
		{
			const std::string_view szModel = !szArgument.empty() ? szArgument : entity.GetValue( "model" );
			if ( IsModelPath( szModel ) )
			{
				visual.m_szModel = std::string{ szModel };
				std::ranges::replace( visual.m_szModel, '\\', '/' );
				visual.m_bModelBox = helper.m_szName != "studio";
				visual.m_bInvertPitch = helper.m_szName == "lightprop";
			}
		}
		else if ( helper.m_szName == "iconsprite" || helper.m_szName == "sprite" )
		{
			const std::string_view szSprite = !szArgument.empty() ? szArgument : entity.GetValue( "model" );
			if ( !szSprite.empty() && !IsModelPath( szSprite ) )
			{
				visual.m_szSprite = MaterialName( szSprite );
			}
			if ( const auto flScale = ParseNumber<double>( entity.GetValue( "scale" ) ); flScale && *flScale > 0.0 )
			{
				visual.m_flSpriteScale = *flScale;
			}
			else
			{
				visual.m_flSpriteScale = helper.m_szName == "sprite" ? 1.0 : 0.25;
			}
			const std::string_view szTint = entity.HasKey( "_light" ) ? entity.GetValue( "_light" ) : entity.GetValue( "rendercolor" );
			if ( const auto tint = ParseColor( szTint ) )
			{
				visual.m_SpriteTint = *tint;
			}
		}
		else if ( helper.m_szName == "decal" )
		{
			visual.m_bDecal = true;
		}
		else if ( helper.m_szName == "overlay" )
		{
			visual.m_bOverlay = true;
		}
		else if ( helper.m_szName == "instance" )
		{
			visual.m_bInstance = true;
		}
		else if ( helper.m_szName == "skycamera" )
		{
			visual.m_bSkyCamera = true;
		}
		else if ( helper.m_szName == "sphere" )
		{
			const std::string_view szKey = szArgument.empty() ? std::string_view{ "radius" } : szArgument;
			if ( const auto flRadius = ParseNumber<double>( entity.GetValue( szKey ) ); flRadius && *flRadius > 0.0 )
			{
				visual.m_Spheres.push_back( *flRadius );
			}
		}
	}
	return visual;
}

} // namespace xv::map
