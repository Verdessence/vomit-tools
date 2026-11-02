//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See DetailProps.h.
//
//===========================================================================//
#include "DetailProps.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "detail/Detail.h"
#include "map/Displacement.h"

namespace xv::map
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose: A pair of an entry's block, if it has one.
//-----------------------------------------------------------------------------
std::string ValueOf( const kv1::CNode *pNode, std::string_view szKey )
{
	const kv1::CNode *pValue = pNode ? pNode->Find( szKey ) : nullptr;
	return pValue && pValue->IsPair() ? std::string{ pValue->GetValue() } : std::string{};
}

//-----------------------------------------------------------------------------
// Purpose: A pair's first number, or the default when it is missing or doesn't read.
//-----------------------------------------------------------------------------
float NumberOf( const kv1::CNode *pNode, std::string_view szKey, float flDefault )
{
	const std::vector<double> numbers = detail::Numbers( ValueOf( pNode, szKey ) );
	return numbers.empty() ? flDefault : static_cast<float>( numbers.front() );
}

//-----------------------------------------------------------------------------
// Purpose: A vector turned about an axis (of unit length) by degrees.
//-----------------------------------------------------------------------------
Point_t Turned( Point_t vec, Point_t axis, double flDegrees )
{
	const double flRadians = flDegrees * std::numbers::pi / 180.0;
	const double c = std::cos( flRadians );
	const double s = std::sin( flRadians );
	return Add( Add( Scale( vec, c ), Scale( Cross( axis, vec ), s ) ), Scale( axis, Dot( axis, vec ) * ( 1.0 - c ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: The group for a blend alpha: between the two whose alphas bracket it, the nearer
//          the likelier, as vbsp picks.
//-----------------------------------------------------------------------------
const DetailGroup_t &PickGroup( const DetailType_t &type, float flAlpha, float flRoll )
{
	std::size_t nStart = 0;
	while ( nStart + 1 < type.m_Groups.size() && flAlpha >= type.m_Groups[ nStart + 1 ].m_flAlpha )
		nStart++;
	const std::size_t nEnd = std::min( nStart + 1, type.m_Groups.size() - 1 );
	if ( nStart == nEnd )
		return type.m_Groups[ nStart ];
	const float flSpan = type.m_Groups[ nEnd ].m_flAlpha - type.m_Groups[ nStart ].m_flAlpha;
	const float flToward = flSpan != 0.0f ? ( flAlpha - type.m_Groups[ nStart ].m_flAlpha ) / flSpan : 0.0f;
	return flRoll > flToward ? type.m_Groups[ nStart ] : type.m_Groups[ nEnd ];
}

class CPlacer
{
public:
	CPlacer( std::uint32_t nSeed, std::vector<DetailProp_t> &out )
		: m_Random( nSeed ),
		  m_Out( out )
	{
	}

	[[nodiscard]] float Unit() { return std::uniform_real_distribution<float>{ 0.0f, 1.0f }( m_Random ); }

	//-----------------------------------------------------------------------------
	// Purpose: One sample of a type at a point: its group and entry, then the prop if the
	//          slope allows it.
	//-----------------------------------------------------------------------------
	void Sample( const DetailType_t &type, Point_t at, Point_t normal, float flAlpha, std::uint32_t nObject )
	{
		const DetailGroup_t &group = PickGroup( type, flAlpha, Unit() );
		const float flPick = Unit();
		const auto entry = std::ranges::find_if( group.m_Entries, [ & ]( const DetailEntry_t &e ) { return flPick <= e.m_flAmount; } );
		if ( entry == group.m_Entries.end() )
			return;
		Place( *entry, at, normal, nObject );
	}

private:
	//-----------------------------------------------------------------------------
	// Purpose: Too steep (past maxAngle) places nothing; between minAngle and maxAngle, the
	//          steeper the less likely. Upright props get a random yaw; the others lie along
	//          the surface, turned at random about its normal.
	//-----------------------------------------------------------------------------
	void Place( const DetailEntry_t &entry, Point_t at, Point_t normal, std::uint32_t nObject )
	{
		if ( m_Out.size() >= CDetailProps::MAX_PROPS )
			return;
		const float flCos = static_cast<float>( normal[ 2 ] );
		if ( flCos < entry.m_flMaxCos )
			return;
		if ( flCos < entry.m_flMinCos )
		{
			const float flChance = ( flCos - entry.m_flMaxCos ) / ( entry.m_flMinCos - entry.m_flMaxCos );
			if ( Unit() > flChance )
				return;
		}
		DetailProp_t prop;
		prop.m_Origin = at;
		prop.m_pEntry = &entry;
		prop.m_nObject = nObject;
		const double flTurn = 360.0 * Unit();
		if ( entry.m_bUpright )
		{
			const double flYaw = flTurn * std::numbers::pi / 180.0;
			prop.m_Right = { std::sin( flYaw ), -std::cos( flYaw ), 0.0 };
			prop.m_Up = { 0, 0, 1 };
		}
		else
		{
			const Point_t up = Normalized( normal );
			const Point_t side = std::abs( up[ 0 ] ) > 1.0 - 1e-3 ? Point_t{ 0, 1, 0 } : Point_t{ 1, 0, 0 };
			const Point_t left = Normalized( Cross( up, side ) );
			prop.m_Right = Turned( Scale( left, -1.0 ), up, flTurn );
			prop.m_Up = up;
		}
		if ( entry.m_flScaleSpread != 0.0f )
			prop.m_flScale = std::abs( std::normal_distribution<float>{ 1.0f, entry.m_flScaleSpread }( m_Random ) );
		m_Out.push_back( prop );
	}

	std::mt19937 m_Random;
	std::vector<DetailProp_t> &m_Out;
};

//-----------------------------------------------------------------------------
// Purpose: A flat face: each triangle of its fan gets the samples its area is worth (rounded
//          down, as vbsp does), at blend alpha 1.
//-----------------------------------------------------------------------------
void PlaceOnFace( const DetailType_t &type, const FaceGeometry_t &face, std::uint32_t nObject, CPlacer &placer )
{
	const std::vector<Point_t> &points = face.m_Points;
	for ( std::size_t i = 1; i + 1 < points.size(); i++ )
	{
		const Point_t e1 = Subtract( points[ i ], points[ 0 ] );
		const Point_t e2 = Subtract( points[ i + 1 ], points[ 0 ] );
		const double flArea = 0.5 * Length( Cross( e1, e2 ) );
		const int nSamples = static_cast<int>( flArea * type.m_flDensity * 1e-6 );
		for ( int n = 0; n < nSamples; n++ )
		{
			float u = placer.Unit();
			float v = placer.Unit();
			if ( v > 1.0f - u )
			{
				u = 1.0f - u;
				v = 1.0f - v;
			}
			placer.Sample( type, Add( points[ 0 ], Add( Scale( e1, u ), Scale( e2, v ) ) ), face.m_Plane.m_Normal, 1.0f, nObject );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: A displacement: samples for its base face's area, anywhere on it, each at the
//          surface's point, normal and blend alpha there.
//-----------------------------------------------------------------------------
void PlaceOnDisplacement( const DetailType_t &type, const DispMesh_t &disp, std::uint32_t nObject, CPlacer &placer )
{
	const auto &c = disp.m_Corners;
	const double flArea = 0.5 * Length( Cross( Subtract( c[ 1 ], c[ 0 ] ), Subtract( c[ 2 ], c[ 0 ] ) ) ) + 0.5 * Length( Cross( Subtract( c[ 2 ], c[ 0 ] ), Subtract( c[ 3 ], c[ 0 ] ) ) );
	const int nSamples = static_cast<int>( flArea * type.m_flDensity * 1e-6 );
	const int nSize = disp.m_nSize;
	if ( nSize < 2 || disp.m_Points.size() < static_cast<std::size_t>( nSize * nSize ) )
		return;
	for ( int n = 0; n < nSamples; n++ )
	{
		const float flU = placer.Unit() * static_cast<float>( nSize - 1 );
		const float flV = placer.Unit() * static_cast<float>( nSize - 1 );
		const int nX = std::min( static_cast<int>( flU ), nSize - 2 );
		const int nY = std::min( static_cast<int>( flV ), nSize - 2 );
		const double fx = flU - static_cast<float>( nX );
		const double fy = flV - static_cast<float>( nY );
		const auto at = [ & ]( int x, int y ) { return static_cast<std::size_t>( y * nSize + x ); };
		const auto mix = [ & ]( const auto &values ) {
			const auto top = Add( Scale( values[ at( nX, nY ) ], 1.0 - fx ), Scale( values[ at( nX + 1, nY ) ], fx ) );
			const auto bottom = Add( Scale( values[ at( nX, nY + 1 ) ], 1.0 - fx ), Scale( values[ at( nX + 1, nY + 1 ) ], fx ) );
			return Add( Scale( top, 1.0 - fy ), Scale( bottom, fy ) );
		};
		const Point_t point = mix( disp.m_Points );
		const Point_t normal = disp.m_Normals.size() == disp.m_Points.size() ? Normalized( mix( disp.m_Normals ) ) : Point_t{ 0, 0, 1 };
		float flAlpha = 0.0f;
		if ( disp.m_Alphas.size() == disp.m_Points.size() )
		{
			const float flTop = disp.m_Alphas[ at( nX, nY ) ] * static_cast<float>( 1.0 - fx ) + disp.m_Alphas[ at( nX + 1, nY ) ] * static_cast<float>( fx );
			const float flBottom = disp.m_Alphas[ at( nX, nY + 1 ) ] * static_cast<float>( 1.0 - fx ) + disp.m_Alphas[ at( nX + 1, nY + 1 ) ] * static_cast<float>( fx );
			flAlpha = flTop * static_cast<float>( 1.0 - fy ) + flBottom * static_cast<float>( fy );
		}
		placer.Sample( type, point, normal, flAlpha, nObject );
	}
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CLessIgnoreCase::operator()( std::string_view a, std::string_view b ) const
{
	return std::ranges::lexicographical_compare( a, b, []( unsigned char x, unsigned char y ) { return std::tolower( x ) < std::tolower( y ); } );
}

//-----------------------------------------------------------------------------
// Purpose: A group's amounts become running totals (scaled down when they add up past one);
//          minAngle and maxAngle default to 180 (any slope), and minAngle is never steeper
//          than maxAngle.
//-----------------------------------------------------------------------------
DetailTypes_t ReadDetailTypes( const kv1::CDocument &document )
{
	DetailTypes_t types;
	for ( const detail::Type_t &read : detail::Types( document ) )
	{
		DetailType_t &type = types[ read.m_szName ];
		type.m_flDensity = std::isfinite( read.m_flDensity ) ? static_cast<float>( read.m_flDensity ) : 0.0f;
		type.m_Groups.clear();
		for ( const detail::Group_t &readGroup : read.m_Groups )
		{
			DetailGroup_t group;
			group.m_flAlpha = std::isfinite( readGroup.m_flAlpha ) ? static_cast<float>( readGroup.m_flAlpha ) : 1.0f;
			float flTotal = 0.0f;
			for ( const detail::Entry_t &readEntry : readGroup.m_Entries )
			{
				const kv1::CNode *pNode = detail::Node( document, readEntry.m_Path );
				DetailEntry_t entry;
				if ( !readEntry.m_szModel.empty() )
				{
					entry.m_nShape = DetailShape_t::DETAIL_SHAPE_MODEL;
					entry.m_szModel = readEntry.m_szModel;
				}
				else
				{
					const std::string szShape = ValueOf( pNode, "sprite_shape" );
					entry.m_nShape = kv1::EqualsIgnoreCase( szShape, "cross" ) ? DetailShape_t::DETAIL_SHAPE_CROSS : kv1::EqualsIgnoreCase( szShape, "tri" ) ? DetailShape_t::DETAIL_SHAPE_TRI
																																							 : DetailShape_t::DETAIL_SHAPE_SPRITE;
					if ( readEntry.m_Sprite )
					{
						// "x y width height texture size", in texels; half a texel in at each edge.
						const auto &s = *readEntry.m_Sprite;
						const double flSize = std::max( s[ 4 ], 1.0 );
						entry.m_Rect = { static_cast<float>( ( s[ 0 ] + 0.5 ) / flSize ), static_cast<float>( ( s[ 1 ] + 0.5 ) / flSize ), static_cast<float>( ( s[ 0 ] + s[ 2 ] - 0.5 ) / flSize ), static_cast<float>( ( s[ 1 ] + s[ 3 ] - 0.5 ) / flSize ) };
					}
					if ( readEntry.m_Size )
					{
						// "origin x, origin y, width, height": the origin measured from the bottom left.
						const auto &z = *readEntry.m_Size;
						entry.m_Extent = { static_cast<float>( -z[ 0 ] ), static_cast<float>( z[ 3 ] - z[ 1 ] ), static_cast<float>( z[ 2 ] - z[ 0 ] ), static_cast<float>( -z[ 1 ] ) };
					}
					entry.m_flScaleSpread = NumberOf( pNode, "spriterandomscale", 0.0f );
				}
				const float flAmount = std::isfinite( readEntry.m_flAmount ) ? static_cast<float>( readEntry.m_flAmount ) : 1.0f;
				flTotal += flAmount;
				entry.m_flAmount = flTotal;
				entry.m_bUpright = NumberOf( pNode, "upright", 0.0f ) != 0.0f;
				entry.m_nOrientation = static_cast<int>( NumberOf( pNode, "detailOrientation", 0.0f ) );
				const auto cosine = []( float flDegrees ) { return static_cast<float>( std::cos( flDegrees * std::numbers::pi / 180.0 ) ); };
				entry.m_flMinCos = cosine( NumberOf( pNode, "minAngle", 180.0f ) );
				entry.m_flMaxCos = cosine( NumberOf( pNode, "maxAngle", 180.0f ) );
				entry.m_flMinCos = std::max( entry.m_flMinCos, entry.m_flMaxCos );
				group.m_Entries.push_back( std::move( entry ) );
			}
			if ( flTotal > 1.0f )
			{
				for ( DetailEntry_t &entry : group.m_Entries )
					entry.m_flAmount /= flTotal;
			}
			type.m_Groups.push_back( std::move( group ) );
		}
		std::ranges::stable_sort( type.m_Groups, {}, &DetailGroup_t::m_flAlpha );
	}
	return types;
}

//-----------------------------------------------------------------------------
// Purpose: Each face is seeded by its solid's and side's ids, so an edit elsewhere leaves its
//          props as they were. A solid that holds a displacement grows only on that.
//-----------------------------------------------------------------------------
CDetailProps::CDetailProps( std::shared_ptr<const DetailTypes_t> pTypes, const CScene &scene, const std::function<std::string( std::uint32_t nMaterial )> &pfnDetailType )
	: m_pTypes( std::move( pTypes ) )
{
	if ( !m_pTypes || m_pTypes->empty() || !pfnDetailType )
	{
		return;
	}
	const auto &solids = scene.m_Map.m_Solids;
	const auto &entities = scene.m_Map.m_Entities;
	for ( std::size_t s = 0; s < solids.size() && s < scene.m_Solids.size(); s++ )
	{
		const Solid_t &solid = solids[ s ];
		if ( solid.m_bDeleted )
			continue;
		// vbsp makes the world's faces and func_detail's into the world; brush entities keep theirs.
		if ( solid.m_nEntity >= 0 && ( static_cast<std::size_t>( solid.m_nEntity ) >= entities.size() || !kv1::EqualsIgnoreCase( entities[ static_cast<std::size_t>( solid.m_nEntity ) ].GetClassname(), "func_detail" ) ) )
			continue;
		const bool bHostsDisplacement = std::ranges::any_of( solid.m_Sides, []( const Side_t &side ) { return static_cast<bool>( side.m_pDisp ); } );
		for ( const FaceGeometry_t &face : scene.m_Solids[ s ].m_Faces )
		{
			if ( face.m_nSide >= solid.m_Sides.size() )
				continue;
			const Side_t &side = solid.m_Sides[ face.m_nSide ];
			if ( bHostsDisplacement && !side.m_pDisp )
				continue;
			const std::string szType = pfnDetailType( side.m_nMaterial );
			const auto found = szType.empty() ? m_pTypes->end() : m_pTypes->find( szType );
			if ( found == m_pTypes->end() || found->second.m_Groups.empty() || found->second.m_flDensity <= 0.0f )
				continue;
			CPlacer placer( static_cast<std::uint32_t>( solid.m_nId ) * 7919u + static_cast<std::uint32_t>( side.m_nId ), m_Props );
			if ( side.m_pDisp )
			{
				if ( const auto disp = BuildDisplacement( *side.m_pDisp, face ) )
					PlaceOnDisplacement( found->second, *disp, scene.SolidObject( s ), placer );
			}
			else
			{
				PlaceOnFace( found->second, face, scene.SolidObject( s ), placer );
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Far to near, fading over the last of the distance as the game does. A sprite
//          spans its extent along its right and up: its own as placed, the screen's, or the
//          screen's turned about z; cross and tri shapes add copies turned about its up.
//-----------------------------------------------------------------------------
void CDetailProps::Layout( Point_t eye, Point_t right, Point_t up, const DetailView_t &view, std::uint32_t nMaterial, std::array<int, 2> textureSize, std::vector<ParticleSprite_t> &out ) const
{
	struct Seen_t
	{
		double m_flDistance;
		const DetailProp_t *m_pProp;
	};
	std::vector<Seen_t> seen;
	const double flMost = view.m_flDistance;
	for ( const DetailProp_t &prop : m_Props )
	{
		if ( prop.m_pEntry->m_nShape == DetailShape_t::DETAIL_SHAPE_MODEL )
			continue;
		const Point_t offset = Subtract( prop.m_Origin, eye );
		const double flDistanceSquared = Dot( offset, offset );
		if ( flDistanceSquared < flMost * flMost )
			seen.push_back( { std::sqrt( flDistanceSquared ), &prop } );
	}
	std::ranges::sort( seen, []( const Seen_t &a, const Seen_t &b ) { return a.m_flDistance > b.m_flDistance; } );
	const double flFade = std::max( 1.0f, view.m_flFade );
	for ( const Seen_t &at : seen )
	{
		const DetailProp_t &prop = *at.m_pProp;
		const DetailEntry_t &entry = *prop.m_pEntry;
		const float flAlpha = static_cast<float>( std::clamp( ( flMost - at.m_flDistance ) / flFade, 0.0, 1.0 ) );
		Point_t across = prop.m_Right;
		Point_t above = prop.m_Up;
		if ( entry.m_nOrientation == 1 )
		{
			across = right;
			above = up;
		}
		else if ( entry.m_nOrientation == 2 )
		{
			above = { 0, 0, 1 };
			const Point_t facing = Cross( Subtract( prop.m_Origin, eye ), above );
			across = Length( facing ) > 1e-6 ? Normalized( facing ) : right;
		}
		const int nCopies = entry.m_nShape == DetailShape_t::DETAIL_SHAPE_CROSS ? 2 : entry.m_nShape == DetailShape_t::DETAIL_SHAPE_TRI ? 3
																																		: 1;
		const double s = prop.m_flScale;
		const auto &e = entry.m_Extent;
		for ( int n = 0; n < nCopies; n++ )
		{
			const Point_t side = n == 0 ? across : Turned( across, above, 180.0 * n / nCopies );
			const auto corner = [ & ]( float x, float y ) { return ToVec3( Add( prop.m_Origin, Add( Scale( side, x * s ), Scale( above, y * s ) ) ) ); };
			ParticleSprite_t sprite;
			sprite.m_Corners = { corner( e[ 0 ], e[ 1 ] ), corner( e[ 2 ], e[ 1 ] ), corner( e[ 2 ], e[ 3 ] ), corner( e[ 0 ], e[ 3 ] ) };
			sprite.m_Rect = entry.m_Rect;
			sprite.m_Color = { 1.0f, 1.0f, 1.0f, flAlpha };
			sprite.m_nMaterial = nMaterial;
			sprite.m_TextureSize = textureSize;
			sprite.m_nObject = prop.m_nObject;
			out.push_back( sprite );
		}
	}
}

} // namespace xv::map
