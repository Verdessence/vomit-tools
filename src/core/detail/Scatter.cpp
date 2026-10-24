//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Scatter.h.
//
//===========================================================================//
#include "Scatter.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace xv::detail
{

//-----------------------------------------------------------------------------
// Purpose: The blend alpha at a point of the ground.
//-----------------------------------------------------------------------------
float BlendAlphaAt( float x, const ScatterOptions_t &options )
{
	const float flT = std::clamp( x / std::max( 1.0f, options.m_flSize ) + 0.5f, 0.0f, 1.0f );
	return options.m_flAlphaLow + ( options.m_flAlphaHigh - options.m_flAlphaLow ) * flT;
}

//-----------------------------------------------------------------------------
// Purpose: The props of one detail type on the square.
//-----------------------------------------------------------------------------
std::vector<Placed_t> Scatter( const kv1::CDocument &document, const Type_t &type, const ScatterOptions_t &options )
{
	std::vector<Placed_t> placed;
	if ( type.m_Groups.empty() || !std::isfinite( type.m_flDensity ) || type.m_flDensity <= 0.0 )
	{
		return placed;
	}
	// The density is per million square units.
	const double flArea = static_cast<double>( options.m_flSize ) * options.m_flSize;
	const auto nCount = std::min( options.m_nMaxProps, static_cast<std::size_t>( std::lround( flArea * type.m_flDensity / 1e6 ) ) );
	std::vector<const Group_t *> groups;
	for ( const Group_t &group : type.m_Groups )
	{
		if ( std::isfinite( group.m_flAlpha ) )
		{
			groups.push_back( &group );
		}
	}
	if ( groups.empty() )
	{
		return placed;
	}
	std::ranges::stable_sort( groups, {}, &Group_t::m_flAlpha );
	std::mt19937 random{ options.m_nSeed };
	std::uniform_real_distribution<float> unit{ 0.0f, 1.0f };
	for ( std::size_t i = 0; i < nCount; i++ )
	{
		const float x = ( unit( random ) - 0.5f ) * options.m_flSize;
		const float y = ( unit( random ) - 0.5f ) * options.m_flSize;
		const float flAlpha = BlendAlphaAt( x, options );
		// The group: below the first or above the last, that one; between two, the nearer more often.
		const Group_t *pGroup = groups.front();
		if ( flAlpha >= groups.back()->m_flAlpha )
		{
			pGroup = groups.back();
		}
		else
		{
			for ( std::size_t g = 0; g + 1 < groups.size(); g++ )
			{
				const double flLow = groups[ g ]->m_flAlpha;
				const double flHigh = groups[ g + 1 ]->m_flAlpha;
				if ( flAlpha >= flLow && flAlpha < flHigh )
				{
					const double flToward = flHigh > flLow ? ( flAlpha - flLow ) / ( flHigh - flLow ) : 0.0;
					pGroup = unit( random ) < flToward ? groups[ g + 1 ] : groups[ g ];
					break;
				}
			}
		}
		// The entry, by its amount; what is left over places nothing.
		float flPick = unit( random );
		const Entry_t *pEntry = nullptr;
		for ( const Entry_t &entry : pGroup->m_Entries )
		{
			if ( !std::isfinite( entry.m_flAmount ) || entry.m_flAmount <= 0.0 )
			{
				continue;
			}
			flPick -= static_cast<float>( entry.m_flAmount );
			if ( flPick <= 0.0f )
			{
				pEntry = &entry;
				break;
			}
		}
		const float flYaw = unit( random ) * 360.0f;
		const float flScaleRoll = unit( random ) * 2.0f - 1.0f;
		if ( !pEntry )
		{
			continue;
		}
		float flScale = 1.0f;
		if ( const kv1::CNode *pNode = Node( document, pEntry->m_Path ) )
		{
			if ( const kv1::CNode *pRandom = pNode->Find( "spriterandomscale" ); pRandom && pRandom->IsPair() )
			{
				const auto numbers = Numbers( pRandom->GetValue() );
				if ( numbers.size() == 1 )
				{
					flScale = std::max( 0.05f, 1.0f + flScaleRoll * static_cast<float>( numbers.front() ) );
				}
			}
		}
		placed.push_back( { pEntry->m_Path, { x, y, 0.0f }, flYaw, flScale } );
	}
	return placed;
}

} // namespace xv::detail
