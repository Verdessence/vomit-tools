//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Spline.h.
//
//===========================================================================//
#include "Spline.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

#include "map/Brushes.h"
#include "map/DispEdit.h"
#include "map/Displacement.h"
#include "map/Primitives.h"

namespace xv::map
{

namespace
{

// The pieces' faces other than the top: never drawn.
constexpr std::string_view NODRAW = "TOOLS/TOOLSNODRAW";
// Samples a segment of the curve gets: at least this many, and one every few units.
constexpr int MIN_SAMPLES = 16;
constexpr int MAX_SAMPLES = 512;
constexpr double SAMPLE_SPACING = 4.0;

//-----------------------------------------------------------------------------
// Purpose: A point's width, the spline's where the point has none.
//-----------------------------------------------------------------------------
double WidthOf( const Spline_t &spline, const SplinePoint_t &point )
{
	return point.m_flWidth > 0.0 ? point.m_flWidth : spline.m_flWidth;
}

//-----------------------------------------------------------------------------
// Purpose: How many segments the curve has: one between each pair of points, and one back to
//          the first when it is closed.
//-----------------------------------------------------------------------------
std::size_t SegmentCount( const Spline_t &spline )
{
	const std::size_t nPoints = spline.m_Points.size();
	if ( nPoints < 2 )
	{
		return 0;
	}
	return spline.m_bClosed && nPoints >= 3 ? nPoints : nPoints - 1;
}

//-----------------------------------------------------------------------------
// Purpose: The point a segment's curve leans on, by an index that may run off either end: round
//          the loop when closed, else carried on straight from the end.
//-----------------------------------------------------------------------------
Point_t ControlPoint( const Spline_t &spline, std::ptrdiff_t i )
{
	const auto nPoints = static_cast<std::ptrdiff_t>( spline.m_Points.size() );
	if ( spline.m_bClosed && nPoints >= 3 )
	{
		return spline.m_Points[ static_cast<std::size_t>( ( ( i % nPoints ) + nPoints ) % nPoints ) ].m_Position;
	}
	if ( i < 0 )
	{
		return Subtract( Scale( spline.m_Points[ 0 ].m_Position, 2.0 ), spline.m_Points[ 1 ].m_Position );
	}
	if ( i >= nPoints )
	{
		return Subtract( Scale( spline.m_Points[ static_cast<std::size_t>( nPoints - 1 ) ].m_Position, 2.0 ), spline.m_Points[ static_cast<std::size_t>( nPoints - 2 ) ].m_Position );
	}
	return spline.m_Points[ static_cast<std::size_t>( i ) ].m_Position;
}

//-----------------------------------------------------------------------------
// Purpose: A point's handle, by an index that may run off either end (round the loop when
//          closed; the ends' virtual neighbours have none).
//-----------------------------------------------------------------------------
const std::optional<Point_t> &HandleOf( const Spline_t &spline, std::ptrdiff_t i )
{
	static const std::optional<Point_t> NONE;
	const auto nPoints = static_cast<std::ptrdiff_t>( spline.m_Points.size() );
	if ( spline.m_bClosed && nPoints >= 3 )
	{
		return spline.m_Points[ static_cast<std::size_t>( ( ( i % nPoints ) + nPoints ) % nPoints ) ].m_Handle;
	}
	return i >= 0 && i < nPoints ? spline.m_Points[ static_cast<std::size_t>( i ) ].m_Handle : NONE;
}

//-----------------------------------------------------------------------------
// Purpose: Across the curve, level: a road on a slope stays flat from side to side. Where the
//          curve runs straight up, the last good one is kept.
//-----------------------------------------------------------------------------
Point_t RightOf( const Point_t &tangent, const Point_t &previous )
{
	const Point_t right = Cross( tangent, Point_t{ 0, 0, 1 } );
	return Length( right ) < 1e-6 ? previous : Normalized( right );
}

//-----------------------------------------------------------------------------
// Purpose: The signed area of a four-cornered outline seen from above, twice over, and whether
//          it turns one way all round (a convex outline).
//-----------------------------------------------------------------------------
bool IsConvexFromAbove( const std::array<Point_t, 4> &corners )
{
	int nSign = 0;
	for ( std::size_t i = 0; i < 4; i++ )
	{
		const Point_t &a = corners[ i ];
		const Point_t &b = corners[ ( i + 1 ) % 4 ];
		const Point_t &c = corners[ ( i + 2 ) % 4 ];
		const double flTurn = ( b[ 0 ] - a[ 0 ] ) * ( c[ 1 ] - b[ 1 ] ) - ( b[ 1 ] - a[ 1 ] ) * ( c[ 0 ] - b[ 0 ] );
		if ( std::abs( flTurn ) < 1e-3 )
		{
			return false;
		}
		const int nThis = flTurn > 0.0 ? 1 : -1;
		if ( nSign != 0 && nThis != nSign )
		{
			return false;
		}
		nSign = nThis;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: The ids a map's splines use, for the next free one.
//-----------------------------------------------------------------------------
int NextSplineId( const CMap &map )
{
	int nId = 0;
	for ( const Spline_t &spline : map.m_Splines )
	{
		nId = std::max( nId, spline.m_nId );
	}
	return nId + 1;
}

//-----------------------------------------------------------------------------
// Purpose: Edit entries deleting the solids with these ids (those still there).
//-----------------------------------------------------------------------------
std::vector<std::pair<std::uint32_t, Solid_t>> DeletePieces( const CMap &map, const std::vector<int> &ids )
{
	std::unordered_map<int, std::uint32_t> byId;
	for ( std::uint32_t i = 0; i < map.m_Solids.size(); i++ )
	{
		if ( !map.m_Solids[ i ].m_bDeleted )
		{
			byId[ map.m_Solids[ i ].m_nId ] = i;
		}
	}
	std::vector<std::pair<std::uint32_t, Solid_t>> deletions;
	for ( const int nId : ids )
	{
		if ( const auto found = byId.find( nId ); found != byId.end() )
		{
			Solid_t solid = map.m_Solids[ found->second ];
			solid.m_bDeleted = true;
			deletions.emplace_back( found->second, std::move( solid ) );
		}
	}
	return deletions;
}

//-----------------------------------------------------------------------------
// Purpose: The pieces just added put in the spline's group (made with the next free id, or the
//          one it has), or the group let go when the spline no longer wants one.
//-----------------------------------------------------------------------------
void GroupPieces( const CScene &scene, Spline_t &spline, MapEdit_t &edit )
{
	const auto existing = std::ranges::find( scene.m_Map.m_Groups, spline.m_nGroup, &Group_t::m_nId );
	if ( !spline.m_bGroup )
	{
		// A group already in the file stays (empty) until Hammer drops it; a new one goes.
		if ( spline.m_nGroup != 0 && existing != scene.m_Map.m_Groups.end() )
		{
			edit.m_RemovedGroups.push_back( spline.m_nGroup );
		}
		spline.m_nGroup = 0;
		return;
	}
	if ( spline.m_nGroup == 0 || existing == scene.m_Map.m_Groups.end() )
	{
		int nId = scene.m_nNextObjectId;
		for ( const auto &[ nIndex, solid ] : edit.m_Solids )
		{
			nId = std::max( nId, solid.m_nId + 1 );
		}
		Group_t group;
		group.m_nId = nId;
		group.m_Editor.m_Color = { 220, 220, 220 };
		group.m_Editor.m_bHasColor = true;
		edit.m_Groups.push_back( group );
		spline.m_nGroup = nId;
	}
	for ( auto &[ nIndex, solid ] : edit.m_Solids )
	{
		solid.m_Editor.m_nGroup = spline.m_nGroup;
		solid.m_Editor.m_bChanged = true;
	}
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Centripetal Catmull-Rom (Barry and Goldman's pyramid) for the path, knots spaced by
//          the square root of the distance between points, so close points can't make a loop;
//          the height separately, so it can't overshoot.
//-----------------------------------------------------------------------------
Point_t EvaluateSpline( const Spline_t &spline, std::size_t i, double t )
{
	const auto n = static_cast<std::ptrdiff_t>( i );
	const Point_t p0 = ControlPoint( spline, n - 1 );
	const Point_t p1 = ControlPoint( spline, n );
	const Point_t p2 = ControlPoint( spline, n + 1 );
	const Point_t p3 = ControlPoint( spline, n + 2 );
	const auto knot = []( const Point_t &a, const Point_t &b ) { return std::max( std::sqrt( Length( Subtract( b, a ) ) ), 1e-4 ); };
	const double t0 = 0.0;
	const double t1 = t0 + knot( p0, p1 );
	const double t2 = t1 + knot( p1, p2 );
	const double t3 = t2 + knot( p2, p3 );
	const std::optional<Point_t> &start = HandleOf( spline, n );
	const std::optional<Point_t> &end = HandleOf( spline, n + 1 );
	Point_t point;
	if ( !start && !end )
	{
		const double u = t1 + ( t2 - t1 ) * t;
		const auto mix = []( const Point_t &a, const Point_t &b, double ta, double tb, double tu ) { return Lerp( a, b, ( tu - ta ) / ( tb - ta ) ); };
		const Point_t a1 = mix( p0, p1, t0, t1, u );
		const Point_t a2 = mix( p1, p2, t1, t2, u );
		const Point_t a3 = mix( p2, p3, t2, t3, u );
		const Point_t b1 = mix( a1, a2, t0, t2, u );
		const Point_t b2 = mix( a2, a3, t1, t3, u );
		point = mix( b1, b2, t1, t2, u );
	}
	else
	{
		// A cubic Hermite across the ground: a handle's end is a Bezier control point, so the
		// tangent there is three times the handle; an end without one keeps the tangent the
		// centripetal curve has there, so its neighbour segment still meets it smoothly.
		const auto over = []( const Point_t &a, const Point_t &b, double flSpan ) { return Scale( Subtract( b, a ), 1.0 / flSpan ); };
		const Point_t autoStart = Scale( Add( Subtract( over( p0, p1, t1 - t0 ), over( p0, p2, t2 - t0 ) ), over( p1, p2, t2 - t1 ) ), t2 - t1 );
		const Point_t autoEnd = Scale( Add( Subtract( over( p1, p2, t2 - t1 ), over( p1, p3, t3 - t1 ) ), over( p2, p3, t3 - t2 ) ), t2 - t1 );
		const Point_t m1 = start ? Scale( *start, 3.0 ) : autoStart;
		const Point_t m2 = end ? Scale( *end, 3.0 ) : autoEnd;
		const double s2 = t * t;
		const double s3 = s2 * t;
		point = Add( Add( Scale( p1, 2 * s3 - 3 * s2 + 1 ), Scale( m1, s3 - 2 * s2 + t ) ), Add( Scale( p2, -2 * s3 + 3 * s2 ), Scale( m2, s3 - s2 ) ) );
	}

	// Height on its own, as a monotone cubic over the distance between the points seen from
	// above (Fritsch and Butland's slopes): it never rises past the higher of two points or dips
	// under the lower, so a road between two level points stays level.
	const auto run = []( const Point_t &a, const Point_t &b ) { return std::max( std::hypot( b[ 0 ] - a[ 0 ], b[ 1 ] - a[ 1 ] ), 1e-6 ); };
	const double h0 = run( p0, p1 );
	const double h1 = run( p1, p2 );
	const double h2 = run( p2, p3 );
	const double d0 = ( p1[ 2 ] - p0[ 2 ] ) / h0;
	const double d1 = ( p2[ 2 ] - p1[ 2 ] ) / h1;
	const double d2 = ( p3[ 2 ] - p2[ 2 ] ) / h2;
	const auto slope = []( double flBefore, double flAfter, double hBefore, double hAfter ) {
		if ( flBefore * flAfter <= 0.0 )
		{
			return 0.0;
		}
		return 3.0 * ( hBefore + hAfter ) / ( ( 2.0 * hAfter + hBefore ) / flBefore + ( hAfter + 2.0 * hBefore ) / flAfter );
	};
	const double m1 = slope( d0, d1, h0, h1 );
	const double m2 = slope( d1, d2, h1, h2 );
	const double t2s = t * t;
	const double t3s = t2s * t;
	point[ 2 ] = ( 2 * t3s - 3 * t2s + 1 ) * p1[ 2 ] + ( t3s - 2 * t2s + t ) * h1 * m1 + ( -2 * t3s + 3 * t2s ) * p2[ 2 ] + ( t3s - t2s ) * h1 * m2;
	return point;
}

//-----------------------------------------------------------------------------
// Purpose: Samples every segment, measures along them, and gives each sample its frame.
//-----------------------------------------------------------------------------
CSplineCurve::CSplineCurve( const Spline_t &spline )
{
	const std::size_t nSegments = SegmentCount( spline );
	if ( nSegments == 0 )
	{
		return;
	}
	const std::size_t nPoints = spline.m_Points.size();
	for ( std::size_t i = 0; i < nSegments; i++ )
	{
		const SplinePoint_t &from = spline.m_Points[ i ];
		const SplinePoint_t &to = spline.m_Points[ ( i + 1 ) % nPoints ];
		const double flChord = Length( Subtract( to.m_Position, from.m_Position ) );
		const int nSamples = std::clamp( static_cast<int>( std::ceil( flChord / SAMPLE_SPACING ) ), MIN_SAMPLES, MAX_SAMPLES );
		m_PointDistances.push_back( m_Samples.empty() ? 0.0 : m_Samples.back().m_flDistance );
		for ( int k = i == 0 ? 0 : 1; k <= nSamples; k++ )
		{
			const double t = static_cast<double>( k ) / nSamples;
			SplineFrame_t frame;
			// The points themselves exactly, not as the curve's arithmetic lands near them.
			frame.m_Position = k == 0 ? from.m_Position : k == nSamples ? to.m_Position
																		: EvaluateSpline( spline, i, t );
			frame.m_flWidth = WidthOf( spline, from ) + ( WidthOf( spline, to ) - WidthOf( spline, from ) ) * t;
			frame.m_flDistance = m_Samples.empty() ? 0.0 : m_Samples.back().m_flDistance + Length( Subtract( frame.m_Position, m_Samples.back().m_Position ) );
			m_Samples.push_back( frame );
		}
	}
	if ( !spline.m_bClosed || nPoints < 3 )
	{
		m_PointDistances.push_back( m_Samples.back().m_flDistance );
	}
	// Tangents from the samples either side (round the loop when closed); the right from each.
	const std::size_t nCount = m_Samples.size();
	const bool bLoop = spline.m_bClosed && nPoints >= 3;
	Point_t right{ 0, -1, 0 };
	for ( std::size_t i = 0; i < nCount; i++ )
	{
		std::size_t nBefore = i == 0 ? ( bLoop ? nCount - 2 : 0 ) : i - 1;
		std::size_t nAfter = i + 1 == nCount ? ( bLoop ? 1 : i ) : i + 1;
		Point_t tangent = Subtract( m_Samples[ nAfter ].m_Position, m_Samples[ nBefore ].m_Position );
		if ( Length( tangent ) < 1e-9 )
		{
			tangent = i > 0 ? m_Samples[ i - 1 ].m_Tangent : Point_t{ 1, 0, 0 };
		}
		m_Samples[ i ].m_Tangent = Normalized( tangent );
		right = RightOf( m_Samples[ i ].m_Tangent, right );
		m_Samples[ i ].m_Right = right;
	}
}

//-----------------------------------------------------------------------------
// Purpose: The frame between the two samples round a distance.
//-----------------------------------------------------------------------------
SplineFrame_t CSplineCurve::FrameAt( double flDistance ) const
{
	if ( m_Samples.empty() )
	{
		return {};
	}
	if ( flDistance <= 0.0 )
	{
		return m_Samples.front();
	}
	if ( flDistance >= GetLength() )
	{
		return m_Samples.back();
	}
	const auto after = std::ranges::upper_bound( m_Samples, flDistance, {}, &SplineFrame_t::m_flDistance );
	const SplineFrame_t &b = *after;
	const SplineFrame_t &a = *( after - 1 );
	const double flSpan = b.m_flDistance - a.m_flDistance;
	const double t = flSpan > 0.0 ? ( flDistance - a.m_flDistance ) / flSpan : 0.0;
	SplineFrame_t frame;
	frame.m_Position = Lerp( a.m_Position, b.m_Position, t );
	frame.m_Tangent = Normalized( Lerp( a.m_Tangent, b.m_Tangent, t ) );
	frame.m_Right = RightOf( frame.m_Tangent, Normalized( Lerp( a.m_Right, b.m_Right, t ) ) );
	frame.m_flWidth = a.m_flWidth + ( b.m_flWidth - a.m_flWidth ) * t;
	frame.m_flDistance = flDistance;
	return frame;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Point_t CSplineCurve::PointAt( double flDistance, double flAcross ) const
{
	const SplineFrame_t frame = FrameAt( flDistance );
	return Add( frame.m_Position, Scale( frame.m_Right, ( flAcross - 0.5 ) * frame.m_flWidth ) );
}

//-----------------------------------------------------------------------------
// Purpose: Cross-sections at even distances along the curve; between each two, a prism whose top
//          is level at its corners' mean height, the top a displacement moved onto the curve.
//-----------------------------------------------------------------------------
std::optional<std::vector<Solid_t>> SweepSpline( const CMap &map, const Spline_t &spline, std::string *pError )
{
	const auto fail = [ pError ]( std::string szReason ) -> std::optional<std::vector<Solid_t>> {
		if ( pError )
		{
			*pError = std::move( szReason );
		}
		return std::nullopt;
	};
	const CSplineCurve curve( spline );
	if ( !curve.IsValid() || curve.GetLength() < 1.0 )
	{
		return fail( "a spline needs two points apart" );
	}
	const int nPower = std::clamp( spline.m_nPower, 2, 4 );
	const auto nPieces = static_cast<std::size_t>( std::max( 1.0, std::round( curve.GetLength() / std::max( 1.0, spline.m_flPieceLength ) ) ) );
	const bool bLoop = spline.m_bClosed && spline.m_Points.size() >= 3;
	// A section's distance; the last of a loop is the first, so the loop closes exactly.
	std::vector<double> distances( nPieces + 1 );
	for ( std::size_t k = 0; k <= nPieces; k++ )
	{
		distances[ k ] = k == nPieces ? ( bLoop ? 0.0 : curve.GetLength() ) : curve.GetLength() * static_cast<double>( k ) / static_cast<double>( nPieces );
	}

	FaceDefaults_t faces;
	faces.m_szMaterial = NODRAW;
	faces.m_flTextureScale = spline.m_flTextureScale;
	const std::string_view szMaterial = map.Store( spline.m_szMaterial );
	std::vector<Solid_t> solids;
	for ( std::size_t k = 0; k < nPieces; k++ )
	{
		const double flFrom = distances[ k ];
		const double flTo = k + 1 == nPieces && bLoop ? curve.GetLength() : distances[ k + 1 ];
		// Left and right at the start, right and left at the end: round the top.
		const std::array<Point_t, 4> corners{ curve.PointAt( distances[ k ], 0.0 ), curve.PointAt( distances[ k ], 1.0 ), curve.PointAt( distances[ k + 1 ], 1.0 ), curve.PointAt( distances[ k + 1 ], 0.0 ) };
		if ( !IsConvexFromAbove( corners ) )
		{
			return fail( "piece " + std::to_string( k + 1 ) + " would fold over itself: the curve bends too tightly there for pieces this long and this wide (shorten them, narrow the spline, or move the points apart)" );
		}
		double flTop = 0.0;
		for ( const Point_t &corner : corners )
		{
			flTop += corner[ 2 ] / 4.0;
		}
		const double flBottom = flTop - std::max( 1.0, spline.m_flThickness );
		std::array<Point_t, 4> top{};
		std::array<Point_t, 4> bottom{};
		for ( std::size_t i = 0; i < 4; i++ )
		{
			top[ i ] = { corners[ i ][ 0 ], corners[ i ][ 1 ], flTop };
			bottom[ i ] = { corners[ i ][ 0 ], corners[ i ][ 1 ], flBottom };
		}
		std::vector<std::vector<Point_t>> polygons{ { top.begin(), top.end() }, { bottom.begin(), bottom.end() } };
		for ( std::size_t i = 0; i < 4; i++ )
		{
			polygons.push_back( { top[ i ], top[ ( i + 1 ) % 4 ], bottom[ ( i + 1 ) % 4 ], bottom[ i ] } );
		}
		Solid_t solid = SolidFromFaces( map, polygons, faces );
		const SolidGeometry_t geometry = BuildSolid( solid );
		const auto face = std::ranges::find_if( geometry.m_Faces, []( const FaceGeometry_t &f ) { return f.m_Plane.m_Normal[ 2 ] > 0.999 && f.m_Points.size() == 4; } );
		if ( geometry.m_nProblems != 0 || face == geometry.m_Faces.end() )
		{
			return fail( "piece " + std::to_string( k + 1 ) + " didn't make a sound brush" );
		}
		Side_t &side = solid.m_Sides[ face->m_nSide ];

		// The texture along the curve: u from the distance along, v from the left edge.
		const Point_t vecStartMid = Lerp( top[ 0 ], top[ 1 ], 0.5 );
		const Point_t vecEndMid = Lerp( top[ 3 ], top[ 2 ], 0.5 );
		const Point_t vecAlong = Normalized( Point_t{ vecEndMid[ 0 ] - vecStartMid[ 0 ], vecEndMid[ 1 ] - vecStartMid[ 1 ], 0.0 } );
		const Point_t vecAcross = Normalized( Point_t{ top[ 1 ][ 0 ] - top[ 0 ][ 0 ], top[ 1 ][ 1 ] - top[ 0 ][ 1 ], 0.0 } );
		const double flScale = spline.m_flTextureScale > 0.0 ? spline.m_flTextureScale : 0.25;
		side.m_szMaterial = szMaterial;
		side.m_nMaterial = NEW_MATERIAL;
		side.m_UAxis = { vecAlong, flFrom / flScale - Dot( vecStartMid, vecAlong ) / flScale, flScale };
		side.m_VAxis = { vecAcross, -Dot( top[ 0 ], vecAcross ) / flScale, flScale };
		side.m_flRotation = 0.0;

		// Each grid point onto the curve: its place on the piece (along, across) from the corner
		// it's interpolated between, as the grid is laid over the face.
		auto disp = MakeDisplacement( *face, nPower );
		const auto mesh = disp ? BuildDisplacement( *disp, *face ) : std::nullopt;
		if ( !mesh )
		{
			return fail( "piece " + std::to_string( k + 1 ) + " couldn't take a displacement" );
		}
		static constexpr std::array<std::array<double, 2>, 4> PLACES{ { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } } }; // (along, across) of top[ 0..3 ]
		std::array<std::array<double, 2>, 4> places{};
		for ( std::size_t c = 0; c < 4; c++ )
		{
			std::size_t nNearest = 0;
			double flNearest = std::numeric_limits<double>::infinity();
			for ( std::size_t i = 0; i < 4; i++ )
			{
				const Point_t d = Subtract( mesh->m_Corners[ c ], top[ i ] );
				if ( Dot( d, d ) < flNearest )
				{
					flNearest = Dot( d, d );
					nNearest = i;
				}
			}
			places[ c ] = PLACES[ nNearest ];
		}
		const auto nSize = static_cast<std::size_t>( mesh->m_nSize );
		for ( std::size_t r = 0; r < nSize; r++ )
		{
			const double flRow = static_cast<double>( r ) / static_cast<double>( nSize - 1 );
			for ( std::size_t c = 0; c < nSize; c++ )
			{
				const double flColumn = static_cast<double>( c ) / static_cast<double>( nSize - 1 );
				std::array<double, 2> place{};
				for ( std::size_t a = 0; a < 2; a++ )
				{
					const double flRowStart = places[ 0 ][ a ] + ( places[ 1 ][ a ] - places[ 0 ][ a ] ) * flRow;
					const double flRowEnd = places[ 3 ][ a ] + ( places[ 2 ][ a ] - places[ 3 ][ a ] ) * flRow;
					place[ a ] = flRowStart + ( flRowEnd - flRowStart ) * flColumn;
				}
				// The ends exactly, so neighbours meet on the same numbers.
				const double flDistance = place[ 0 ] <= 0.0 ? flFrom : place[ 0 ] >= 1.0 ? flTo
																						 : flFrom + ( flTo - flFrom ) * place[ 0 ];
				const Point_t wanted = curve.PointAt( flDistance >= curve.GetLength() && bLoop ? 0.0 : flDistance, place[ 1 ] );
				const std::size_t i = r * nSize + c;
				SetDispField( *disp, i, Subtract( wanted, mesh->m_Flat[ i ] ) );
			}
		}
		TagTriangles( *disp, *face );
		side.m_pDisp = CCopyPtr<DispInfo_t>( std::make_unique<DispInfo_t>( std::move( *disp ) ) );
		solids.push_back( std::move( solid ) );
	}
	return solids;
}

//-----------------------------------------------------------------------------
// Purpose: The pieces added, then the spline kept naming them.
//-----------------------------------------------------------------------------
std::optional<MapEdit_t> AddSpline( const CScene &scene, Spline_t spline, std::vector<std::uint32_t> *pAdded, std::string *pError )
{
	auto solids = SweepSpline( scene.m_Map, spline, pError );
	if ( !solids )
	{
		return std::nullopt;
	}
	MapEdit_t edit = AddObjects( scene, std::move( *solids ), {}, pAdded );
	spline.m_nId = NextSplineId( scene.m_Map );
	spline.m_Pieces.clear();
	for ( const auto &[ nIndex, solid ] : edit.m_Solids )
	{
		spline.m_Pieces.push_back( solid.m_nId );
	}
	spline.m_nGroup = 0;
	GroupPieces( scene, spline, edit );
	std::vector<Spline_t> splines = scene.m_Map.m_Splines;
	splines.push_back( std::move( spline ) );
	edit.m_Splines = std::move( splines );
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose: The old pieces deleted and new ones added, in one edit.
//-----------------------------------------------------------------------------
std::optional<MapEdit_t> UpdateSpline( const CScene &scene, Spline_t spline, std::vector<std::uint32_t> *pAdded, std::string *pError )
{
	std::vector<Spline_t> splines = scene.m_Map.m_Splines;
	const auto found = std::ranges::find( splines, spline.m_nId, &Spline_t::m_nId );
	if ( found == splines.end() )
	{
		if ( pError )
		{
			*pError = "no such spline";
		}
		return std::nullopt;
	}
	auto solids = SweepSpline( scene.m_Map, spline, pError );
	if ( !solids )
	{
		return std::nullopt;
	}
	MapEdit_t edit = AddObjects( scene, std::move( *solids ), {}, pAdded );
	spline.m_Pieces.clear();
	for ( const auto &[ nIndex, solid ] : edit.m_Solids )
	{
		spline.m_Pieces.push_back( solid.m_nId );
	}
	GroupPieces( scene, spline, edit );
	auto deletions = DeletePieces( scene.m_Map, found->m_Pieces );
	edit.m_Solids.insert( edit.m_Solids.begin(), std::make_move_iterator( deletions.begin() ), std::make_move_iterator( deletions.end() ) );
	*found = std::move( spline );
	edit.m_Splines = std::move( splines );
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapEdit_t DeleteSpline( const CScene &scene, int nId )
{
	MapEdit_t edit;
	std::vector<Spline_t> splines = scene.m_Map.m_Splines;
	const auto found = std::ranges::find( splines, nId, &Spline_t::m_nId );
	if ( found == splines.end() )
	{
		return edit;
	}
	edit.m_Solids = DeletePieces( scene.m_Map, found->m_Pieces );
	if ( found->m_nGroup != 0 && std::ranges::find( scene.m_Map.m_Groups, found->m_nGroup, &Group_t::m_nId ) != scene.m_Map.m_Groups.end() )
	{
		edit.m_RemovedGroups.push_back( found->m_nGroup );
	}
	splines.erase( found );
	edit.m_Splines = std::move( splines );
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const Spline_t *SplineOfSolid( const CMap &map, int nSolidId )
{
	for ( const Spline_t &spline : map.m_Splines )
	{
		if ( std::ranges::find( spline.m_Pieces, nSolidId ) != spline.m_Pieces.end() )
		{
			return &spline;
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const Spline_t *FindSpline( const CMap &map, int nId )
{
	const auto found = std::ranges::find( map.m_Splines, nId, &Spline_t::m_nId );
	return found == map.m_Splines.end() ? nullptr : &*found;
}

} // namespace xv::map
