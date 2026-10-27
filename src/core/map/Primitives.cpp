//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Primitives.h.
//
//===========================================================================//
#include "Primitives.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <string>

#include "map/Edit.h"
#include "map/Entities.h"
#include "map/VmfValues.h"
#include "util/Numbers.h"

namespace xv::map
{

//-----------------------------------------------------------------------------
// Purpose: Each side's plane points go p0, p1, p2 with (p0 - p1) x (p2 - p1) facing out of the
//          solid, as the VMF wants them: from a corner p1 along two of the face's edges a and b,
//          where a x b is the outward normal.
//-----------------------------------------------------------------------------
Solid_t MakeBlock( const CMap &map, const Box_t &box, const FaceDefaults_t &faces )
{
	const std::string_view szMaterial = map.Store( std::string{ faces.m_szMaterial } );
	const Point_t &n = box.m_Mins;
	const Point_t &x = box.m_Maxs;
	const Point_t size = Subtract( x, n );
	struct Face_t
	{
		Point_t m_Corner;
		Point_t m_A; // edges from the corner
		Point_t m_B;
		Point_t m_U; // texture axes
		Point_t m_V;
	};
	const std::array<Face_t, 6> sides = { {
		{ { n[ 0 ], n[ 1 ], x[ 2 ] }, { size[ 0 ], 0, 0 }, { 0, size[ 1 ], 0 }, { 1, 0, 0 }, { 0, -1, 0 } }, // top
		{ { n[ 0 ], n[ 1 ], n[ 2 ] }, { 0, size[ 1 ], 0 }, { size[ 0 ], 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 } }, // bottom
		{ { n[ 0 ], n[ 1 ], n[ 2 ] }, { 0, 0, size[ 2 ] }, { 0, size[ 1 ], 0 }, { 0, 1, 0 }, { 0, 0, -1 } }, // -x
		{ { x[ 0 ], n[ 1 ], n[ 2 ] }, { 0, size[ 1 ], 0 }, { 0, 0, size[ 2 ] }, { 0, 1, 0 }, { 0, 0, -1 } }, // +x
		{ { n[ 0 ], x[ 1 ], n[ 2 ] }, { 0, 0, size[ 2 ] }, { size[ 0 ], 0, 0 }, { 1, 0, 0 }, { 0, 0, -1 } }, // +y
		{ { n[ 0 ], n[ 1 ], n[ 2 ] }, { size[ 0 ], 0, 0 }, { 0, 0, size[ 2 ] }, { 1, 0, 0 }, { 0, 0, -1 } }, // -y
	} };
	Solid_t solid;
	for ( const Face_t &face : sides )
	{
		Side_t side;
		side.m_Plane = { Add( face.m_Corner, face.m_A ), face.m_Corner, Add( face.m_Corner, face.m_B ) };
		side.m_bPlaneRead = true;
		side.m_szMaterial = szMaterial;
		side.m_nMaterial = NEW_MATERIAL;
		side.m_UAxis = { face.m_U, 0.0, faces.m_flTextureScale };
		side.m_VAxis = { face.m_V, 0.0, faces.m_flTextureScale };
		side.m_nLightmapScale = faces.m_nLightmapScale;
		solid.m_Sides.push_back( std::move( side ) );
	}
	solid.m_Editor.m_Color = faces.m_Color;
	solid.m_Editor.m_bHasColor = true;
	return solid;
}

//-----------------------------------------------------------------------------
// Purpose: Hammer writes a new entity's keys sorted by name, whatever order the FGD gives them
//          in, with the origin after them all (as the maps it saved show).
//-----------------------------------------------------------------------------
Entity_t MakePointEntity( const CMap &map, const CFgd *pFgd, std::string_view szClass, const Point_t &origin )
{
	Entity_t entity;
	entity.m_Editor.m_Color = ENTITY_COLOR;
	entity.m_Editor.m_bHasColor = true;
	const FgdClass_t *pClass = pFgd ? pFgd->Find( szClass ) : nullptr;
	entity.m_Keys.push_back( { "classname", map.Store( std::string{ pClass ? std::string_view{ pClass->m_szName } : szClass } ), {} } );
	if ( pClass )
	{
		// By lower-case name: a class that gives a base's key again gives its own default.
		std::map<std::string, std::pair<std::string_view, std::string>> defaults;
		for ( const FgdKey_t &key : pClass->m_Keys )
		{
			std::string szValue = key.m_szDefault;
			if ( key.m_szType == "flags" )
			{
				long long nFlags = 0;
				for ( const FgdChoice_t &choice : key.m_Choices )
				{
					nFlags += choice.m_bDefault ? ParseNumber<long long>( choice.m_szValue ).value_or( 0 ) : 0;
				}
				szValue = std::to_string( nFlags );
			}
			std::string szLower = key.m_szName;
			std::ranges::transform( szLower, szLower.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
			if ( szValue.empty() || szLower == "classname" || szLower == "origin" )
			{
				continue;
			}
			defaults[ std::move( szLower ) ] = { key.m_szName, std::move( szValue ) };
		}
		for ( auto &[ szLower, value ] : defaults )
		{
			entity.m_Keys.push_back( { map.Store( std::string{ value.first } ), map.Store( std::move( value.second ) ), {} } );
		}
		if ( const FgdHelper_t *pColor = pClass->FindHelper( "color" ); pColor && !pColor->m_Arguments.empty() )
		{
			entity.m_Editor.m_Color = ParseColor( pColor->m_Arguments[ 0 ] ).value_or( ENTITY_COLOR );
		}
	}
	entity.m_Keys.push_back( { "origin", map.Store( FormatPoint( origin ) ), {} } );
	return entity;
}

} // namespace xv::map
