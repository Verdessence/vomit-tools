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
#include <QPixmap>
#include <QRectF>
#include <QSize>

#include "map/MapView.h"
#include "map/Mesh.h"
#include "map/Scene.h"

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
[[nodiscard]] GridPlan_t PlanGrid( double flPixelsPerUnit, int nGridSize, int nHighlightEvery = 8, bool bHideSmall = true, double flHideBelowPixels = 4.0 );

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

	// The scene changed (loaded, or another one): projects it again; after edits (ObjectsChanged
	// says which objects), only those.
	void Rebuild();
	void ObjectsChanged( const std::vector<std::uint32_t> &objects );
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
	// Which of x, y and z the view's horizontal (0) and vertical (1) axes are.
	[[nodiscard]] int GetAxisIndex( int nIndex ) const { return GetAxis( nIndex ); }
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
	[[nodiscard]] int GetPaintCount() const { return m_nPaints; }
	[[nodiscard]] int GetLayerPaintCount() const { return m_nLayerPaints; }

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
	// The grid and the objects, kept as a picture between repaints: a camera move or a drag
	// only draws over it. Made again when what it shows changes.
	void PaintLayer();
	void PaintGrid( class QPainter &painter, const QRectF &visible ) const;
	// Props big enough on screen, textured (Preferences > Maps), drawn in software from this axis.
	void PaintModels( QPainter &painter, const QRectF &visible ) const;
	// Whether a prop is drawn textured rather than as its edges or box.
	[[nodiscard]] bool IsTexturedModel( const MapFlatObject_t &object, const QRectF &box ) const;
	void PaintObjects( QPainter &painter, const QRectF &visible ) const;
	void PaintSelection( QPainter &painter ) const;
	// The block tool's box and the entity tool's marker.
	void PaintTools( QPainter &painter ) const;
	void PaintCamera( QPainter &painter ) const;
	[[nodiscard]] QColor ColorOf( const MapFlatObject_t &object ) const;
	void PlaceSolid( const xv::map::CScene &scene, std::uint32_t nSolid, const xv::map::CMapMesh &mesh, xv::map::MeshRange_t range );
	void PlaceSolid( const xv::map::CScene &scene, std::uint32_t nSolid );
	void PlaceEntity( const xv::map::CScene &scene, std::size_t nEntity );
	void Place( MapFlatObject_t object );
	void Patch( const xv::map::CScene &scene );
	void UpdateScrollBars();
	[[nodiscard]] xv::map::Point_t DragOffset( QPointF at, Qt::KeyboardModifiers modifiers ) const;
	// A map coordinate on the grid, unless snapping is off or Alt is held.
	[[nodiscard]] double Snap( double flValue, Qt::KeyboardModifiers modifiers ) const;
	// A box as this view shows it, in the widget's pixels.
	[[nodiscard]] QRectF RectOf( const xv::map::Box_t &box ) const;
	// The block tool: a press starts a new box, moves the one there or pulls one of its handles.
	void PressBlock( QPointF at );
	void DragBlock( QPointF at, Qt::KeyboardModifiers modifiers );
	void PreviewMove( QPointF at, Qt::KeyboardModifiers modifiers );
	void CancelMove();
	void ReportCursor( QPointF at );
	[[nodiscard]] QRectF GetVisible() const;
	[[nodiscard]] int GetAxis( int nIndex ) const; // 0 or 1: which of x, y, z
	[[nodiscard]] double GetWorldLimit() const;    // where the grid and the scrolling stop

	MapViewType_t m_nType;
	QPointF m_Center;
	double m_flZoom = 0.25;            // pixels per unit
	std::vector<MapFlatObject_t> m_Objects;
	std::vector<QLineF> m_Segments;    // in map units
	std::vector<std::uint32_t> m_ObjectIndex; // object number -> index into m_Objects, or ~0
	const void *m_pBuiltFor = nullptr;
	const void *m_pBuiltMesh = nullptr;          // the mesh as read of the map built for
	std::vector<std::uint32_t> m_PendingObjects; // changed by edits since
	std::size_t m_nStaleSegments = 0;            // segments no object uses any more
	std::shared_ptr<const xv::map::CMapMesh> m_pProps; // the props the objects were built with
	std::shared_ptr<const std::vector<std::array<std::uint8_t, 3>>> m_pLineColors; // each object's, for this paint

	QScrollBar *m_pHorizontal = nullptr;
	QScrollBar *m_pVertical = nullptr;
	bool m_bUpdatingScrollBars = false;

	bool m_bSpaceHeld = false;
	std::optional<QPoint> m_PanFrom;
	std::optional<QPointF> m_BoxFrom;  // a box selection being dragged, in map units
	std::optional<QRectF> m_PendingBox; // dragged and waiting for Enter
	// Moving the selection by dragging inside its box: where the drag began (map units), the
	// box's lowest corner then, and (once it moves) the scene and selection it moves from.
	std::optional<QPointF> m_MoveFrom;
	QPointF m_MoveCorner;
	std::shared_ptr<const xv::map::CScene> m_pMoveScene;
	std::vector<std::uint32_t> m_MoveSelection;
	// The block tool's drag: what it does, where it began (map units) and the box then.
	enum class BoxDrag_t
	{
		BOX_DRAG_NONE,
		BOX_DRAG_NEW,
		BOX_DRAG_MOVE,
		BOX_DRAG_HANDLE,
	};
	BoxDrag_t m_nBoxDrag = BoxDrag_t::BOX_DRAG_NONE;
	bool m_bBoxDragMoved = false; // a new box starts once the mouse has moved a little
	QPointF m_BoxDragFrom;
	xv::map::Box_t m_BoxDragStart;
	QPoint m_BoxHandle; // -1, 0 or 1 across and down the widget: which side or corner it pulls
	QPoint m_PressAt;
	double m_flPaintMs = 0.0;
	int m_nPaints = 0;
	int m_nLayerPaints = 0;      // how many of those drew the layer again

	// What the layer was drawn for.
	struct LayerKey_t
	{
		const void *m_pScene = nullptr;
		const void *m_pProps = nullptr;
		const void *m_pMaterials = nullptr; // textures arriving draw the models again
		std::uint64_t m_nState = ~0ull;
		MapViewSettings_t m_Settings;
		QPointF m_Center;
		double m_flZoom = 0.0;
		QSize m_Size;
		qreal m_flRatio = 1.0;
		std::uint64_t m_nRevision = 0; // the preferences'
		friend bool operator==( const LayerKey_t &, const LayerKey_t & ) = default;
	};
	QPixmap m_Layer;
	LayerKey_t m_LayerKey;
};
