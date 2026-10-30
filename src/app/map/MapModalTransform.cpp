//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapModalTransform.h.
//
//===========================================================================//
#include "MapModalTransform.h"

#include <cmath>
#include <numbers>

using xv::map::Point_t;
using xv::map::Transform_t;

namespace
{
//-----------------------------------------------------------------------------
// Purpose: The mouse ray's point on a plane through the transform pivot.
//-----------------------------------------------------------------------------
std::optional<Point_t> PlanePoint( const xv::map::CViewCamera &camera, QPointF at, Point_t pivot, Point_t normal )
{
	const auto [ origin, direction ] = camera.Ray( at.x(), at.y() );
	const double flDenominator = xv::map::Dot( direction, normal );
	if ( std::abs( flDenominator ) < 1e-8 )
		return std::nullopt;
	const double flDistance = xv::map::Dot( xv::map::Subtract( pivot, origin ), normal ) / flDenominator;
	if ( flDistance <= 0.0 )
		return std::nullopt;
	return xv::map::Add( origin, xv::map::Scale( direction, flDistance ) );
}

//-----------------------------------------------------------------------------
// Purpose: Snap only mouse input; typed amounts are exact.
//-----------------------------------------------------------------------------
double SnapAmount( double flValue, double flStep )
{
	return flStep > 0.0 ? std::round( flValue / flStep ) * flStep : flValue;
}
} // namespace

//-----------------------------------------------------------------------------
// Purpose: Retain the starting camera so flying does not move the transform's anchor.
//-----------------------------------------------------------------------------
CMapModalTransform::CMapModalTransform( MapGizmoMode_t nMode, Point_t pivot, const std::array<Point_t, 3> &localAxes, const xv::map::CViewCamera &camera, QPointF from )
	: m_nMode( nMode ),
	  m_Pivot( pivot ),
	  m_LocalAxes( localAxes ),
	  m_StartCamera( camera ),
	  m_From( from ),
	  m_At( from ),
	  m_LastPointer( from )
{
}

//-----------------------------------------------------------------------------
// Purpose: Accumulate precise movement without jumping when Shift is pressed or released.
//-----------------------------------------------------------------------------
void CMapModalTransform::MovePointer( QPointF at, bool bPrecision )
{
	m_At += ( at - m_LastPointer ) * ( bPrecision ? 0.1 : 1.0 );
	m_LastPointer = at;
}

//-----------------------------------------------------------------------------
// Purpose: Cycle an axis or plane through world, local and unconstrained movement.
//-----------------------------------------------------------------------------
void CMapModalTransform::LockAxis( int nAxis, bool bPlane )
{
	if ( m_nAxis != nAxis || m_bPlane != bPlane )
		m_nSpace = 0;
	else
		++m_nSpace;
	m_nAxis = m_nSpace >= 2 ? -1 : nAxis;
	m_bPlane = bPlane;
	if ( m_nAxis < 0 )
		m_nSpace = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Accept decimal amounts, including negative moves, turns and mirrored scales.
//-----------------------------------------------------------------------------
bool CMapModalTransform::Type( QChar ch )
{
	if ( ch == '-' )
	{
		if ( m_szNumber.startsWith( '-' ) )
			m_szNumber.remove( 0, 1 );
		else
			m_szNumber.prepend( ch );
		return true;
	}
	if ( ( ch >= '0' && ch <= '9' ) || ( ch == '.' && !m_szNumber.contains( '.' ) ) )
	{
		if ( m_szNumber.size() < 32 )
			m_szNumber += ch;
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Removing the amount completely returns to mouse input.
//-----------------------------------------------------------------------------
void CMapModalTransform::Backspace()
{
	m_szNumber.chop( 1 );
}

//-----------------------------------------------------------------------------
// Purpose: Select a world basis vector or the original entity's corresponding axis.
//-----------------------------------------------------------------------------
Point_t CMapModalTransform::GetAxis( int nAxis ) const
{
	if ( m_nSpace == 1 )
		return m_LocalAxes[ nAxis ];
	Point_t axis{};
	axis[ nAxis ] = 1.0;
	return axis;
}

//-----------------------------------------------------------------------------
// Purpose: Turn pointer movement and an optional typed amount into one map transform.
//-----------------------------------------------------------------------------
Transform_t CMapModalTransform::Evaluate( const xv::map::CViewCamera &camera, double flGrid, double flDegreesStep ) const
{
	bool bTyped = false;
	const double flTyped = m_szNumber.toDouble( &bTyped );
	bTyped &= std::isfinite( flTyped );
	const Point_t axis = m_nAxis >= 0 ? GetAxis( m_nAxis ) : xv::map::Scale( m_StartCamera.GetForward(), -1.0 );
	if ( m_nMode == MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE )
	{
		const Point_t normal = m_nAxis >= 0 && m_bPlane ? axis : m_StartCamera.GetForward();
		const auto from = PlanePoint( m_StartCamera, m_From, m_Pivot, normal );
		const auto to = PlanePoint( camera, m_At, m_Pivot, normal );
		Point_t delta = from && to ? xv::map::Subtract( *to, *from ) : Point_t{};
		if ( m_bPlane && m_nAxis >= 0 && ( !from || !to ) )
		{
			const auto screenFrom = PlanePoint( m_StartCamera, m_From, m_Pivot, m_StartCamera.GetForward() );
			const auto screenTo = PlanePoint( camera, m_At, m_Pivot, m_StartCamera.GetForward() );
			if ( screenFrom && screenTo )
				delta = xv::map::Subtract( *screenTo, *screenFrom );
			delta = xv::map::Subtract( delta, xv::map::Scale( axis, xv::map::Dot( delta, axis ) ) );
		}
		if ( m_nAxis >= 0 && !m_bPlane )
		{
			const Point_t normal = xv::map::Subtract( m_StartCamera.GetForward(), xv::map::Scale( axis, xv::map::Dot( m_StartCamera.GetForward(), axis ) ) );
			const auto axisFrom = PlanePoint( m_StartCamera, m_From, m_Pivot, normal );
			const auto axisTo = PlanePoint( camera, m_At, m_Pivot, normal );
			double flAmount = axisFrom && axisTo ? xv::map::Dot( xv::map::Subtract( *axisTo, *axisFrom ), axis ) : 0.0;
			if ( !axisFrom || !axisTo )
			{
				// A view-aligned axis has no screen direction; vertical movement supplies one.
				CMapGizmo measure;
				measure.Place( m_Pivot, m_LocalAxes );
				flAmount = ( m_From.y() - m_At.y() ) * measure.GetLength( camera, 1.0 );
			}
			return Transform_t::Translation( xv::map::Scale( axis, bTyped ? flTyped : SnapAmount( flAmount, flGrid ) ) );
		}
		if ( bTyped )
		{
			Point_t direction = xv::map::Length( delta ) > 1e-8 ? xv::map::Normalized( delta ) : m_StartCamera.GetRight();
			if ( m_nAxis >= 0 )
				direction = xv::map::Normalized( xv::map::Subtract( direction, xv::map::Scale( axis, xv::map::Dot( direction, axis ) ) ) );
			return Transform_t::Translation( xv::map::Scale( direction, flTyped ) );
		}
		Point_t snapped{};
		for ( int i = 0; i < 3; ++i )
		{
			const Point_t basis = GetAxis( i );
			if ( i != m_nAxis )
				snapped = xv::map::Add( snapped, xv::map::Scale( basis, SnapAmount( xv::map::Dot( delta, basis ), flGrid ) ) );
		}
		return Transform_t::Translation( snapped );
	}
	const auto centre = m_StartCamera.Project( m_Pivot );
	if ( !centre )
		return {};
	QPointF from = m_From - QPointF{ ( *centre )[ 0 ], ( *centre )[ 1 ] };
	QPointF to = m_At - QPointF{ ( *centre )[ 0 ], ( *centre )[ 1 ] };
	if ( m_nMode == MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE )
	{
		double flDegrees = ( std::atan2( -to.y(), to.x() ) - std::atan2( -from.y(), from.x() ) ) * 180.0 / std::numbers::pi;
		flDegrees = std::remainder( flDegrees, 360.0 );
		if ( xv::map::Dot( axis, m_StartCamera.GetForward() ) > 0.0 )
			flDegrees = -flDegrees;
		return Transform_t::RotationAbout( m_Pivot, axis, bTyped ? flTyped : SnapAmount( flDegrees, flDegreesStep ) );
	}
	const double flRadius = std::hypot( from.x(), from.y() );
	const double flFactor = bTyped ? flTyped : flRadius > 1e-6 ? std::hypot( to.x(), to.y() ) / flRadius
															   : 1.0 + ( m_At.x() - m_From.x() ) / 100.0;
	if ( !std::isfinite( flFactor ) || std::abs( flFactor ) < 1e-6 )
		return {};
	if ( m_nAxis >= 0 )
	{
		const auto stretch = Transform_t::ScalingAlong( m_Pivot, axis, flFactor );
		return m_bPlane ? Transform_t::Scaling( m_Pivot, { flFactor, flFactor, flFactor } ) * Transform_t::ScalingAlong( m_Pivot, axis, 1.0 / flFactor ) : stretch;
	}
	return Transform_t::Scaling( m_Pivot, { flFactor, flFactor, flFactor } );
}

//-----------------------------------------------------------------------------
// Purpose: Describe the current constraint and typed amount in the status bar.
//-----------------------------------------------------------------------------
QString CMapModalTransform::GetDescription() const
{
	QString szText = m_nMode == MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE ? QStringLiteral( "Move" ) : m_nMode == MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE ? QStringLiteral( "Rotate" )
																																					   : QStringLiteral( "Scale" );
	if ( m_nAxis >= 0 )
		szText += QStringLiteral( " · %1%2%3" ).arg( m_bPlane ? QStringLiteral( "Plane " ) : QString{}, QString{ QChar( 'X' + m_nAxis ) }, m_nSpace == 1 ? QStringLiteral( " (local)" ) : QStringLiteral( " (world)" ) );
	if ( !m_szNumber.isEmpty() )
		szText += QStringLiteral( " · " ) + m_szNumber;
	return szText;
}

//-----------------------------------------------------------------------------
// Purpose: Draw the constrained axes through the original pivot while previewing.
//-----------------------------------------------------------------------------
std::vector<MapGizmoLine_t> CMapModalTransform::GetLockLines( double flLength ) const
{
	std::vector<MapGizmoLine_t> lines;
	for ( int i = 0; m_nAxis >= 0 && i < 3; ++i )
	{
		if ( m_bPlane ? i == m_nAxis : i != m_nAxis )
			continue;
		const Point_t extent = xv::map::Scale( GetAxis( i ), flLength );
		lines.push_back( { xv::map::Subtract( m_Pivot, extent ), xv::map::Add( m_Pivot, extent ), i } );
	}
	return lines;
}
