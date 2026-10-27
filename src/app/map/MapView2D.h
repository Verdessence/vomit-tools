//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A 2D view of the map, as Hammer draws one: top (x/y), front (y/z) or side (x/z),
//          with its grid, wireframe brushes, entity boxes or models and the selection, panned
//          and zoomed as in Hammer.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <QPointF>
#include <QRectF>

#include "map/MapView.h"
#include "map/Mesh.h"

class QScrollBar;

// The grid lines of a 2D view, worked out from the zoom, by kind; pure, so tests can check
// Hammer's rules.
struct GridPlan_t
{
	double m_flBase = 0.0;      // spacing of the plain lines; 0 when they're too close to draw
	double m_flHighlight = 0.0; // every 8th line
	double m_flSixtyFour = 0.0; // every 64 units, when that differs from the above
	double m_fl1024 = 0.0;      // every 1024 units
};
[[nodiscard]] GridPlan_t PlanGrid( double flPixelsPerUnit, int nGridSize );

// One object as a 2D view draws it, projected once onto the view's two axes.
struct MapFlatObject_t
{
	std::uint32_t m_nObject = 0;
	QRectF m_Bounds;                    // in map units, y up
	std::uint32_t m_nFirstSegment = 0;  // into the view's segments
	std::uint32_t m_nSegmentCount = 0;
	bool m_bPointEntity = false;        // drawn as its box
	bool m_bInvalid = false;
	std::uint32_t m_nFirstModelLine = 0; // a prop's model: its edges in the props' mesh lines
	std::uint32_t m_nModelLineCount = 0;
	std::uint32_t m_nFirstInstanceLine = 0; // a func_instance's contents: in the scene's instance mesh
	std::uint32_t m_nInstanceLineCount = 0;
};

class CMapView2D : public CMapView
{
	Q_OBJECT

public:
	CMapView2D( CMapEditor &editor, MapViewType_t nType, QWidget *pParent = nullptr );

	[[nodiscard]] MapViewType_t GetType() const override { return m_nType; }
	void SetType( MapViewType_t nType ) override;
	bool Run( MapAction_t nAction ) override;
	void CenterOn( const xv::map::Box_t &box ) override;

	// The scene changed (loaded, or another one): projects it again.
	void Rebuild();
	// Fits the whole map in the view.
	void Fit();
	void Zoom( double flFactor, std::optional<QPointF> at = std::nullopt );
	[[nodiscard]] double GetZoom() const { return m_flZoom; }
	void SetZoom( double flZoom );
	[[nodiscard]] QPointF GetCenter() const { return m_Center; }
	void SetCenter( QPointF center );
	// Map units on the view's axes for a point in the widget, and back.
	[[nodiscard]] QPointF ToMap( QPointF at ) const;
	[[nodiscard]] QPointF ToWidget( QPointF map ) const;
	// The view's two axes of a map point (x/y, y/z or x/z).
	[[nodiscard]] QPointF Flatten( const xv::map::Point_t &p ) const;
	// What a click at a widget point would pick: the nearest object whose edge or centre
	// handle is under it; nullopt for none.
	[[nodiscard]] std::optional<std::uint32_t> HitAt( QPointF at ) const;
	// The objects a dragged box (in map units) takes in: those whose bounds cross it.
	[[nodiscard]] std::vector<std::uint32_t> ObjectsIn( const QRectF &box ) const;
	[[nodiscard]] std::optional<QRectF> GetPendingBox() const { return m_PendingBox; }
	// Milliseconds the last full repaint took.
	[[nodiscard]] double GetLastPaintTime() const { return m_flPaintMs; }

protected:
	void paintEvent( QPaintEvent *pEvent ) override;
	void resizeEvent( QResizeEvent *pEvent ) override;
	void mousePressEvent( QMouseEvent *pEvent ) override;
	void mouseMoveEvent( QMouseEvent *pEvent ) override;
	void mouseReleaseEvent( QMouseEvent *pEvent ) override;
	void wheelEvent( QWheelEvent *pEvent ) override;
	void keyPressEvent( QKeyEvent *pEvent ) override;
	void keyReleaseEvent( QKeyEvent *pEvent ) override;
	void leaveEvent( QEvent *pEvent ) override;

private:
	void PaintGrid( class QPainter &painter, const QRectF &visible ) const;
	void PaintObjects( QPainter &painter, const QRectF &visible ) const;
	void PaintSelection( QPainter &painter ) const;
	void PaintCamera( QPainter &painter ) const;
	[[nodiscard]] QColor ColorOf( const MapFlatObject_t &object ) const;
	void UpdateScrollBars();
	void ReportCursor( QPointF at );
	[[nodiscard]] QRectF GetVisible() const;
	[[nodiscard]] int GetAxis( int nIndex ) const; // 0 or 1: which of x, y, z

	MapViewType_t m_nType;
	QPointF m_Center;
	double m_flZoom = 0.25;            // pixels per unit
	std::vector<MapFlatObject_t> m_Objects;
	std::vector<QLineF> m_Segments;    // in map units
	std::vector<std::uint32_t> m_ObjectIndex; // object number -> index into m_Objects, or ~0
	const void *m_pBuiltFor = nullptr;
	std::shared_ptr<const xv::map::CMapMesh> m_pProps; // the props the objects were built with

	QScrollBar *m_pHorizontal = nullptr;
	QScrollBar *m_pVertical = nullptr;
	bool m_bUpdatingScrollBars = false;

	bool m_bSpaceHeld = false;
	std::optional<QPoint> m_PanFrom;
	std::optional<QPointF> m_BoxFrom;  // a box selection being dragged, in map units
	std::optional<QRectF> m_PendingBox; // dragged and waiting for Enter
	QPoint m_PressAt;
	double m_flPaintMs = 0.0;
};
