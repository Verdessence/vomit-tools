//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Polygon outlines and surface-placed clipping points for the map's construction tools.
//===========================================================================//
#include "MapBrushEdit.h"
#include "map/MapEditor.h"
#include "map/MapView2D.h"
#include "map/MapView3D.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>
using namespace xv::map;

//-----------------------------------------------------------------------------
// Purpose: Collect grid-snapped polygon corners in one orthographic plane before creating a brush.
//-----------------------------------------------------------------------------
bool CMapBrushEdit::HandlePolygon( CMapView &view, QEvent *pEvent )
{
	auto *p2D = qobject_cast<CMapView2D *>( &view );
	if ( pEvent->type() == QEvent::KeyPress )
	{
		auto *pKey = static_cast<QKeyEvent *>( pEvent );
		if ( pKey->key() == Qt::Key_Escape )
		{
			if ( m_PolygonPoints.empty() )
				m_Editor.SetTool( MapTool_t::MAP_TOOL_SELECT );
			else
				Cancel();
			return true;
		}
		if ( pKey->key() == Qt::Key_Backspace )
		{
			if ( !m_PolygonPoints.empty() )
				m_PolygonPoints.pop_back();
			Refresh();
			return true;
		}
		if ( pKey->key() == Qt::Key_Return || pKey->key() == Qt::Key_Enter )
		{
			if ( m_Editor.CreatePolygon( m_PolygonPoints, m_nPolygonAxis, m_flPolygonDepth ) )
				Cancel();
			return true;
		}
		return false;
	}
	if ( !p2D )
		return false;
	if ( pEvent->type() != QEvent::MouseMove && pEvent->type() != QEvent::MouseButtonPress )
		return false;
	auto *pMouse = static_cast<QMouseEvent *>( pEvent );
	if ( pEvent->type() == QEvent::MouseButtonPress && pMouse->button() == Qt::RightButton )
	{
		Cancel();
		return true;
	}
	if ( pEvent->type() == QEvent::MouseButtonPress && pMouse->button() != Qt::LeftButton )
		return false;
	const int nAxis = 3 - p2D->GetAxisIndex( 0 ) - p2D->GetAxisIndex( 1 );
	if ( !m_PolygonPoints.empty() && m_nPolygonAxis != nAxis )
	{
		emit m_Editor.StatusMessage( tr( "Finish the polygon in the view where it was started." ) );
		return true;
	}
	const auto at = p2D->ToMap( pMouse->position() );
	Point_t point{};
	point[ p2D->GetAxisIndex( 0 ) ] = at.x();
	point[ p2D->GetAxisIndex( 1 ) ] = at.y();
	const auto &settings = m_Editor.GetSettings();
	if ( settings.m_bSnapToGrid && !pMouse->modifiers().testFlag( Qt::AltModifier ) && settings.m_nGridSize > 0 )
		for ( int i = 0; i < 3; ++i )
			if ( i != nAxis )
				point[ i ] = std::round( point[ i ] / settings.m_nGridSize ) * settings.m_nGridSize;
	if ( m_PolygonPoints.empty() )
	{
		if ( pEvent->type() != QEvent::MouseButtonPress )
			return true;
		m_nPolygonAxis = nAxis;
		m_flPolygonDepth = 64;
		const auto *pScene = m_Editor.GetDocument().GetScene();
		if ( pScene )
		{
			const auto selected = ObjectsOf( *pScene, m_Editor.GetDocument().GetSelection() );
			if ( !selected.m_Solids.empty() )
			{
				const auto &bounds = pScene->m_Solids[ selected.m_Solids.back() ].m_Bounds;
				point[ nAxis ] = bounds.m_Mins[ nAxis ];
				m_flPolygonDepth = std::max( 0.001, bounds.m_Maxs[ nAxis ] - bounds.m_Mins[ nAxis ] );
			}
		}
	}
	else
		point[ nAxis ] = m_PolygonPoints.front()[ nAxis ];
	m_PolygonHover = point;
	if ( pEvent->type() == QEvent::MouseButtonPress )
	{
		view.setFocus();
		m_Editor.SetActiveView( &view );
		if ( m_PolygonPoints.size() >= 3 && QLineF( p2D->ToWidget( p2D->Flatten( m_PolygonPoints.front() ) ), pMouse->position() ).length() < 8 )
		{
			if ( m_Editor.CreatePolygon( m_PolygonPoints, m_nPolygonAxis, m_flPolygonDepth ) )
				Cancel();
		}
		else if ( m_PolygonPoints.empty() || Length( Subtract( point, m_PolygonPoints.back() ) ) > 0.001 )
			m_PolygonPoints.push_back( point );
		emit m_Editor.StatusMessage( tr( "Polygon: click corners, Enter or the first corner to create; Backspace removes a corner, Escape cancels." ) );
	}
	Refresh();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Place two or three grid-snapped clipping points on visible brush surfaces.
//-----------------------------------------------------------------------------
bool CMapBrushEdit::HandleClip( CMapView &view, QEvent *pEvent )
{
	auto *p3D = qobject_cast<CMapView3D *>( &view );
	if ( !p3D )
		return false;
	if ( pEvent->type() == QEvent::KeyPress )
	{
		auto *pKey = static_cast<QKeyEvent *>( pEvent );
		if ( pKey->key() == Qt::Key_C && pKey->modifiers() == Qt::NoModifier )
		{
			m_bClipThree = !m_bClipThree;
			if ( auto line = m_Editor.GetClipLine() )
			{
				line->m_bThreePoints = m_bClipThree;
				line->m_Third.reset();
				m_Editor.SetClipLine( line );
			}
			emit m_Editor.StatusMessage( m_bClipThree ? tr( "Clip: place three surface points, then Enter; C switches to two points." ) : tr( "Clip: place two surface points, then Enter; C switches to three points." ) );
			return true;
		}
		return false;
	}
	if ( pEvent->type() != QEvent::MouseButtonPress )
		return false;
	auto *pMouse = static_cast<QMouseEvent *>( pEvent );
	if ( pMouse->button() == Qt::RightButton && m_Editor.GetClipLine() )
	{
		m_Editor.SetClipLine( std::nullopt );
		return true;
	}
	if ( pMouse->button() != Qt::LeftButton )
		return false;
	const auto hit = p3D->FaceAt( pMouse->position() );
	const auto *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene || !hit )
	{
		emit m_Editor.StatusMessage( tr( "Place the clip point on a brush surface." ) );
		return true;
	}
	const auto *pFace = pScene->m_Solids[ hit->m_nSolid ].FindFace( hit->m_nSide );
	if ( !pFace )
		return true;
	const auto camera = p3D->GetSizedCamera();
	const auto [ from, direction ] = camera.Ray( pMouse->position().x(), pMouse->position().y() );
	const double flDot = Dot( direction, pFace->m_Plane.m_Normal );
	if ( std::abs( flDot ) < 1e-8 )
		return true;
	Point_t point = Add( from, Scale( direction, -pFace->m_Plane.Distance( from ) / flDot ) );
	const auto &settings = m_Editor.GetSettings();
	if ( settings.m_bSnapToGrid && !pMouse->modifiers().testFlag( Qt::AltModifier ) && settings.m_nGridSize > 0 )
	{
		int nNormalAxis = 0;
		for ( int i = 1; i < 3; ++i )
			if ( std::abs( pFace->m_Plane.m_Normal[ i ] ) > std::abs( pFace->m_Plane.m_Normal[ nNormalAxis ] ) )
				nNormalAxis = i;
		double flRemaining = pFace->m_Plane.m_flDistance;
		for ( int i = 0; i < 3; ++i )
			if ( i != nNormalAxis )
			{
				point[ i ] = std::round( point[ i ] / settings.m_nGridSize ) * settings.m_nGridSize;
				flRemaining -= pFace->m_Plane.m_Normal[ i ] * point[ i ];
			}
		point[ nNormalAxis ] = flRemaining / pFace->m_Plane.m_Normal[ nNormalAxis ];
	}
	auto line = m_Editor.GetClipLine();
	if ( !line || line->m_Third || ( !line->m_bThreePoints && Length( Subtract( line->m_To, line->m_From ) ) > 0.001 ) )
	{
		line = CMapEditor::ClipLine_t{};
		line->m_From = point;
		line->m_To = point;
		line->m_NormalAxis = pFace->m_Plane.m_Normal;
		line->m_bThreePoints = m_bClipThree;
	}
	else if ( Length( Subtract( line->m_To, line->m_From ) ) <= 0.001 )
		line->m_To = point;
	else
		line->m_Third = point;
	view.setFocus();
	m_Editor.SetActiveView( &view );
	m_Editor.SetClipLine( line );
	emit m_Editor.StatusMessage( line->m_bThreePoints && !line->m_Third ? tr( "Place the third clip point; C switches to two points." ) : tr( "Clip ready: Enter applies, Shift+X cycles the kept side, Escape cancels." ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Draw construction outlines and the discarded half of a clip on every rendering backend.
//-----------------------------------------------------------------------------
void CMapBrushEdit::PaintConstruction( CMapView &view, QPainter &painter ) const
{
	const auto nTool = m_Editor.GetTool();
	if ( nTool != MapTool_t::MAP_TOOL_POLYGON && nTool != MapTool_t::MAP_TOOL_CLIP )
		return;
	painter.save();
	auto line = [ & ]( Point_t a, Point_t b ) {
		if ( auto *p2D = qobject_cast<CMapView2D *>( &view ) )
			painter.drawLine( p2D->ToWidget( p2D->Flatten( a ) ), p2D->ToWidget( p2D->Flatten( b ) ) );
		else if ( const auto projected = Camera( view ).ProjectLine( a, b ) )
			painter.drawLine( QPointF( projected->first[ 0 ], projected->first[ 1 ] ), QPointF( projected->second[ 0 ], projected->second[ 1 ] ) );
	};
	if ( nTool == MapTool_t::MAP_TOOL_POLYGON && !m_PolygonPoints.empty() )
	{
		painter.setPen( QPen( QColor( 80, 220, 255 ), 2 ) );
		auto points = m_PolygonPoints;
		if ( m_PolygonHover && Length( Subtract( *m_PolygonHover, points.back() ) ) > 0.001 )
			points.push_back( *m_PolygonHover );
		for ( std::size_t i = 0; i < points.size(); ++i )
		{
			const auto j = ( i + 1 ) % points.size();
			auto a = points[ i ], b = points[ j ];
			line( a, b );
			a[ m_nPolygonAxis ] += m_flPolygonDepth;
			b[ m_nPolygonAxis ] += m_flPolygonDepth;
			line( a, b );
			line( points[ i ], a );
			if ( auto at = Project( view, points[ i ] ) )
				painter.drawRect( QRectF( *at - QPointF( 3, 3 ), QSizeF( 6, 6 ) ) );
		}
	}
	if ( nTool == MapTool_t::MAP_TOOL_CLIP && qobject_cast<CMapView3D *>( &view ) )
	{
		const auto clip = m_Editor.GetClipLine();
		if ( clip )
		{
			const auto plane = m_Editor.GetClipPlane();
			const auto *pScene = m_Editor.GetDocument().GetScene();
			if ( plane && pScene && m_Editor.GetClipMode() != ClipKeep_t::CLIP_KEEP_BOTH )
			{
				painter.setPen( QPen( QColor( 255, 60, 60 ), 1.5 ) );
				painter.setBrush( QColor( 255, 40, 40, 85 ) );
				for ( auto nSolid : ObjectsOf( *pScene, m_Editor.GetDocument().GetSelection() ).m_Solids )
					for ( const auto &face : pScene->m_Solids[ nSolid ].m_Faces )
					{
						std::vector<Point_t> removed;
						for ( std::size_t i = 0; i < face.m_Points.size(); ++i )
						{
							const auto a = face.m_Points[ i ], b = face.m_Points[ ( i + 1 ) % face.m_Points.size() ];
							const double flSign = m_Editor.GetClipMode() == ClipKeep_t::CLIP_KEEP_FRONT ? -1 : 1;
							const double flA = plane->Distance( a ) * flSign, flB = plane->Distance( b ) * flSign;
							if ( flA >= 0 )
								removed.push_back( a );
							if ( ( flA > 0 ) != ( flB > 0 ) && std::abs( flA - flB ) > 1e-9 )
								removed.push_back( Add( a, Scale( Subtract( b, a ), flA / ( flA - flB ) ) ) );
						}
						QPolygonF polygon;
						for ( const auto &at : Camera( view ).ProjectPolygon( removed ) )
							polygon << QPointF( at[ 0 ], at[ 1 ] );
						if ( polygon.size() >= 3 )
							painter.drawPolygon( polygon );
					}
			}
			painter.setBrush( Qt::white );
			painter.setPen( QPen( Qt::white, 2 ) );
			line( clip->m_From, clip->m_To );
			std::vector<Point_t> points{ clip->m_From, clip->m_To };
			if ( clip->m_Third )
			{
				points.push_back( *clip->m_Third );
				line( clip->m_To, *clip->m_Third );
				line( *clip->m_Third, clip->m_From );
			}
			for ( std::size_t i = 0; i < points.size(); ++i )
				if ( auto at = Project( view, points[ i ] ) )
				{
					painter.drawEllipse( *at, 4, 4 );
					painter.drawText( *at + QPointF( 7, -7 ), QString::number( i + 1 ) );
				}
		}
	}
	painter.restore();
}
