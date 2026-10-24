//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Shared texture canvas with zoom, pan and image-coordinate overlays.
//===========================================================================//
#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QWidget>
#include "util/Vec3.h"

// The picked image, scaled to fit (small ones enlarged, texels kept sharp) or zoomed (the wheel zooms at the pointer, a drag moves it, a
// double-click goes back to fitting), over a checkerboard; tiled 3×3 when asked.
class CTextureCanvas : public QWidget
{
public:
	explicit CTextureCanvas( QWidget *pParent = nullptr )
		: QWidget( pParent )
	{
		setMinimumSize( 200, 200 );
	}

	void SetImage( QImage image )
	{
		const bool bSameSize = image.size() == m_Image.size();
		m_Image = std::move( image );
		if ( !bSameSize )
		{
			m_flZoom = 0.0;
			m_Offset = {};
		}
		update();
	}
	void SetTiled( bool bTiled )
	{
		m_bTiled = bTiled;
		update();
	}
	void Fit()
	{
		m_flZoom = 0.0;
		m_Offset = {};
		update();
	}
	void SetZoom( double flZoom )
	{
		m_flZoom = flZoom;
		update();
	}
	// The scale it is drawn at now.
	[[nodiscard]] double GetScale() const
	{
		if ( m_flZoom > 0.0 || m_Image.isNull() )
		{
			return m_flZoom > 0.0 ? m_flZoom : 1.0;
		}
		const QSizeF content = QSizeF( m_Image.size() ) * ( m_bTiled ? 3.0 : 1.0 );
		return std::max( 1.0 / 64.0, std::min( ( width() - 16 ) / content.width(), ( height() - 16 ) / content.height() ) );
	}

	[[nodiscard]] QPointF ImagePoint( QPointF at ) const
	{
		return ( at - ImageOrigin() ) / GetScale();
	}
	[[nodiscard]] QPointF ImageOrigin() const
	{
		const QSizeF tile = QSizeF( m_Image.size() ) * GetScale();
		return QPointF( width() - tile.width(), height() - tile.height() ) / 2.0 + m_Offset;
	}
	[[nodiscard]] const QImage &GetImage() const { return m_Image; }
	std::function<void( QPainter & )> m_pfnOverlay;

protected:
	void paintEvent( QPaintEvent * ) override
	{
		QPainter painter( this );
		painter.fillRect( rect(), palette().color( QPalette::Window ).darker( 115 ) );
		if ( m_Image.isNull() )
		{
			return;
		}
		const double flScale = GetScale();
		const QSizeF tile = QSizeF( m_Image.size() ) * flScale;
		const int nTiles = m_bTiled ? 3 : 1;
		const QPointF origin = QPointF( width() - tile.width() * nTiles, height() - tile.height() * nTiles ) / 2.0 + m_Offset;
		const QRectF all{ origin, tile * nTiles };
		painter.save();
		painter.setClipRect( all );
		for ( int y = 0; y * 8 < all.height(); y++ )
		{
			for ( int x = 0; x * 8 < all.width(); x++ )
			{
				painter.fillRect( QRectF( all.left() + x * 8, all.top() + y * 8, 8, 8 ), ( x + y ) % 2 ? QColor( 200, 200, 200 ) : QColor( 240, 240, 240 ) );
			}
		}
		painter.restore();
		// Enlarged texels stay sharp, so each one can be seen.
		painter.setRenderHint( QPainter::SmoothPixmapTransform, flScale < 1.0 );
		for ( int ty = 0; ty < nTiles; ty++ )
		{
			for ( int tx = 0; tx < nTiles; tx++ )
			{
				painter.drawImage( QRectF( origin + QPointF( tx * tile.width(), ty * tile.height() ), tile ), m_Image );
			}
		}
		if ( m_pfnOverlay )
		{
			painter.save();
			painter.translate( ImageOrigin() );
			painter.scale( flScale, flScale );
			m_pfnOverlay( painter );
			painter.restore();
		}
	}
	void wheelEvent( QWheelEvent *pEvent ) override
	{
		const double flOld = GetScale();
		const double flNew = std::clamp( flOld * ( pEvent->angleDelta().y() > 0 ? 1.25 : 0.8 ), 1.0 / 64.0, 64.0 );
		// Keep the point under the pointer where it is.
		const QPointF centre = QPointF( width(), height() ) / 2.0;
		const QPointF from = pEvent->position() - centre - m_Offset;
		m_Offset += from - from * ( flNew / flOld );
		m_flZoom = flNew;
		update();
		pEvent->accept();
	}
	void mousePressEvent( QMouseEvent *pEvent ) override
	{
		m_DragFrom = pEvent->position();
		if ( pEvent->button() == Qt::RightButton )
		{
			PointLight( pEvent->position() );
		}
	}
	void mouseMoveEvent( QMouseEvent *pEvent ) override
	{
		if ( pEvent->buttons() & Qt::RightButton )
		{
			PointLight( pEvent->position() );
			return;
		}
		if ( pEvent->buttons() & Qt::LeftButton )
		{
			m_Offset += pEvent->position() - m_DragFrom;
			m_DragFrom = pEvent->position();
			update();
		}
	}
	void mouseDoubleClickEvent( QMouseEvent * ) override { Fit(); }
	void mouseReleaseEvent( QMouseEvent *pEvent ) override
	{
		if ( pEvent->button() == Qt::RightButton && m_pfnLightDone )
		{
			m_pfnLightDone();
		}
	}

public:
	// A right-drag points the light (for a material): towards the spot over the picture, as on a
	// ball facing out of the screen. Then the drag's end.
	std::function<void( xv::CVec3 )> m_pfnLight;
	std::function<void()> m_pfnLightDone;

private:
	void PointLight( QPointF at )
	{
		if ( !m_pfnLight || m_Image.isNull() )
		{
			return;
		}
		const double flScale = GetScale();
		const QSizeF tile = QSizeF( m_Image.size() ) * flScale;
		const int nTiles = m_bTiled ? 3 : 1;
		const QPointF origin = QPointF( width() - tile.width() * nTiles, height() - tile.height() * nTiles ) / 2.0 + m_Offset;
		const float x = std::clamp( static_cast<float>( ( at.x() - origin.x() ) / std::max( 1.0, tile.width() * nTiles ) ) * 2.0f - 1.0f, -1.0f, 1.0f );
		const float y = std::clamp( 1.0f - static_cast<float>( ( at.y() - origin.y() ) / std::max( 1.0, tile.height() * nTiles ) ) * 2.0f, -1.0f, 1.0f );
		m_pfnLight( xv::Normalized( { x, y, std::sqrt( std::max( 0.08f, 1.0f - x * x - y * y ) ) } ) );
	}

	QImage m_Image;
	double m_flZoom = 0.0; // 0: fit
	QPointF m_Offset;
	QPointF m_DragFrom;
	bool m_bTiled = false;
};
