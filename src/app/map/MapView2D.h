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
#include <string>
#include <vector>

#include <QPointF>
#include <QImage>
#include <QPixmap>
#include <QRectF>
#include <QRegion>
#include <QSize>

#include "map/Vertex.h"
#include "map/MapView.h"
#include "map/Mesh.h"
#include "map/Instance.h"
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
	// Hammer++'s pivot, where this view shows it: with the selection tool.
	[[nodiscard]] std::optional<xv::map::Point_t> GetShownPivot() const;
	void SetType( MapViewType_t nType ) override;
	bool Run( MapAction_t nAction ) override;
	void CenterOn( const xv::map::Box_t &box ) override;

	// The scene changed (loaded, or another one): projects it again; after edits (ObjectsChanged
	// says which objects), only those.
	void CancelCameraDrag();
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
	[[nodiscard]] double GetLastPatchTime() const { return m_flPatchMs; }
	[[nodiscard]] double GetLastLayerTime() const { return m_flLayerMs; }
	[[nodiscard]] double GetLastPaintTime() const { return m_flPaintMs; }
	[[nodiscard]] int GetPaintCount() const { return m_nPaints; }
	// The layer's redraws, and of those the ones of only the part that changed.
	[[nodiscard]] int GetLayerPaintCount() const { return m_nLayerPaints; }
	[[nodiscard]] int GetPartLayerPaintCount() const { return m_nPartLayers; }
	// For the tests: the layer as drawn, and the next paint drawing all of it.
	[[nodiscard]] QImage GetLayerImage() const { return m_Layer.toImage(); }
	[[nodiscard]] const QImage &GetModelsPicture() const { return m_ModelsPicture; }
	void RedrawWholeLayer()
	{
		m_bLayerWhole = true;
		m_Layer = {};
		update();
	}
	[[nodiscard]] QString DescribeLayerChanges( bool bAdvance = false );

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
	void hideEvent( QHideEvent *pEvent ) override;

private:
	// The grid and the objects, kept as a picture between repaints: a camera move or a drag
	// only draws over it. Made again when what it shows changes.
	void PaintLayer();
	void PaintGrid( class QPainter &painter, const QRectF &visible ) const;
	// Props big enough on screen, textured (Preferences > Maps), drawn in software from this axis.
	// The textured models of the whole view (a part of the layer clips them).
	void PaintModels( QPainter &painter ) const;
	// Whether a prop is drawn textured rather than as its edges or box.
	[[nodiscard]] bool IsTexturedModel( const MapFlatObject_t &object, const QRectF &box ) const;
	// bEverySelected: the selected objects drawn wherever they are (their helpers can reach far
	// past their boxes); names: where an entity's name may be drawn (it sits above its box).
	void PaintObjects( QPainter &painter, const QRectF &visible, bool bPreviewOnly = false, bool bEverySelected = false, std::optional<QRectF> names = std::nullopt ) const;
	// What an object may draw in the layer, in map units: its box, and an entity's helpers.
	[[nodiscard]] QRectF LooksOf( const MapFlatObject_t &object ) const;
	// The part of the layer the objects whose looks changed since it was drawn cover, in the
	// widget's pixels; nullopt when it is easier to draw all of it again.
	[[nodiscard]] std::optional<QRegion> ChangedPart( bool bPreviewFlipped );
	// A selected entity's FGD helpers: its radii (with their handles), lines to its targets, cones.
	void PaintEntityHelpers( QPainter &painter, std::uint32_t nObject ) const;
	void PaintSelection( QPainter &painter ) const;
	// The block tool's box and the entity tool's marker.
	void PaintTools( QPainter &painter ) const;
	void PaintCamera( QPainter &painter ) const;
	void PaintSavedCameras( QPainter &painter ) const;
	void PressCamera( QPointF at, Qt::KeyboardModifiers modifiers );
	void DragCamera( QPointF at, Qt::KeyboardModifiers modifiers );
	void EndCameraDrag( bool bCommit );
	std::shared_ptr<const xv::map::CScene> m_pCameraDragBase;
	std::vector<xv::map::Camera_t> m_CameraDragList;
	xv::map::MapEdit_t m_CameraEdit;
	QPointF m_CameraPressMap;
	int m_nCameraDrag = -1;
	bool m_bCameraTarget = false;
	bool m_bCameraNew = false;
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
	// The clipping tool: a press starts a line (or takes one of its ends; Ctrl takes the whole).
	void PressClip( QPointF at, Qt::KeyboardModifiers modifiers );
	void DragClip( QPointF at, Qt::KeyboardModifiers modifiers );
	// The vertex tool: the handle under a point, a press picking or starting to drag one (Ctrl
	// adds and removes), the drag previewing the move, the release making it (or picking the
	// handles a box was dragged round).
	[[nodiscard]] std::optional<xv::map::VertexRef_t> VertexHandleAt( QPointF at ) const;
	void PressVertex( QPointF at, Qt::KeyboardModifiers modifiers );
	void DragVertex( QPointF at, Qt::KeyboardModifiers modifiers );
	void ReleaseVertex( QPointF at, Qt::KeyboardModifiers modifiers );
	void PaintVertexHandles( QPainter &painter ) const;
	// The block tool: a press starts a new box, moves the one there or pulls one of its handles.
	void PressBlock( QPointF at );
	void DragBlock( QPointF at, Qt::KeyboardModifiers modifiers );
	// The selection tool's handles: what pulling the one held to at does to the selection, in
	// the edit mode it was taken in (resize, rotate or shear), snapped to the grid or to the
	// rotation step (Alt: neither).
	[[nodiscard]] xv::map::Transform_t HandleTransform( QPointF at, Qt::KeyboardModifiers modifiers ) const;
	void PreviewHandle( QPointF at, Qt::KeyboardModifiers modifiers );
	// The cursor for what a press at at would do.
	void UpdateCursor( QPointF at );
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
	// What the layer has to draw again since it was drawn: where objects the edits changed were
	// and are (map units), or all of it when the objects were built again.
	std::vector<std::pair<QRectF, bool>> m_ChangedLooks; // with whether it is a point entity's (a name above it)
	bool m_bLayerWhole = true;
	// The selection and what was shown when the layer was drawn, to find what changed since.
	std::vector<std::uint32_t> m_LayerSelection;
	std::vector<std::uint8_t> m_LayerShown;
	std::vector<std::uint32_t> m_LayerPreview; // the objects a drag left out of it
	std::vector<std::uint32_t> m_PreviewSeen;  // of a drag's objects, those whose first place is kept
	// What the layer drew past objects' boxes as it was drawn (path lines, the selection's
	// helpers), by object: an edit can take those away before the view hears of it.
	std::vector<std::pair<std::uint32_t, QRectF>> m_LayerExtras;
	void AddLayerExtras( std::uint32_t nObject, std::vector<std::pair<QRectF, bool>> &out ) const;
	int m_nPartLayers = 0; // of the layer's redraws, those of only a part
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
	// Pulling one of the selection's handles: which (-1, 0 or 1 across and down the widget), in
	// which mode, from where (map units), and the selection's box then.
	std::optional<QPoint> m_HandleDrag;
	MapEditMode_t m_nHandleMode = MapEditMode_t::MAP_EDIT_MODE_RESIZE;
	QPointF m_HandleFrom;
	xv::map::Box_t m_DragBounds;
	xv::map::Point_t m_DragPivot{}; // what a turn by the handles is about
	bool m_bPivotDrag = false;      // the pivot itself being dragged
	// A selected entity's sphere() radius: a handle on its circle sets the key it is read from.
	struct RadiusHandle_t
	{
		std::uint32_t m_nObject = 0;
		std::string m_szKey;
		QPointF m_Centre; // map units, in this view
		QPointF m_At;     // the handle, in the widget
	};
	[[nodiscard]] std::vector<RadiusHandle_t> GetRadiusHandles() const;
	[[nodiscard]] std::optional<RadiusHandle_t> RadiusHandleAt( QPointF at ) const;
	// The key's value for the mouse at at: its distance from the centre, in whole units.
	[[nodiscard]] std::string RadiusAt( QPointF at ) const;
	std::optional<RadiusHandle_t> m_RadiusDrag;
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
	int m_nClipDrag = -1;   // the clipping tool: which end is dragged (0, 1), or 2 for the whole line
	// The vertex tool: a handle being dragged from where (map units), or a box being dragged round
	// handles from where.
	bool m_bVertexDrag = false;
	QPointF m_VertexDragFrom;
	xv::map::Point_t m_VertexAnchor{}; // the handle pressed, which lands on the grid
	[[nodiscard]] xv::map::Point_t VertexOffset( QPointF at, Qt::KeyboardModifiers modifiers ) const;
	std::optional<QPointF> m_VertexBoxFrom;
	QPointF m_ClipDragFrom;
	QPoint m_PressAt;
	double m_flPatchMs = 0.0;
	double m_flLayerMs = 0.0;
	double m_flPaintMs = 0.0;
	int m_nPaints = 0;
	int m_nLayersAtLog = 0;
	QString m_szLayerReason;
	int m_nLayerPaints = 0;      // how many of those drew the layer again

	// What the layer was drawn for.
	struct LayerKey_t
	{
		const void *m_pMesh = nullptr;
		bool m_bPreview = false;
		std::uint64_t m_nProps = 0;
		std::uint64_t m_nPropMaterials = 0; // only textures used by models invalidate their picture
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
	// The textured models' picture as last drawn, and what it was drawn for: drawing only part
	// of the layer takes it again while the same models show the same way.
	struct ModelsKey_t
	{
		std::vector<std::uint8_t> m_Flags;
		const void *m_pMesh = nullptr;
		std::uint64_t m_nMaterials = 0; // the props' materials' revision, as the layer's
		std::shared_ptr<const std::vector<std::array<std::uint8_t, 3>>> m_pColors; // made again by each edit: the same when they say the same
		QPointF m_Center;
		double m_flZoom = 0.0;
		QSize m_Size;
		qreal m_flRatio = 1.0;
		friend bool operator==( const ModelsKey_t &a, const ModelsKey_t &b )
		{
			const bool bColors = a.m_pColors == b.m_pColors || ( a.m_pColors && b.m_pColors && *a.m_pColors == *b.m_pColors );
			return bColors && a.m_Flags == b.m_Flags && a.m_pMesh == b.m_pMesh && a.m_nMaterials == b.m_nMaterials && a.m_Center == b.m_Center && a.m_flZoom == b.m_flZoom && a.m_Size == b.m_Size && a.m_flRatio == b.m_flRatio;
		}
	};
	mutable ModelsKey_t m_ModelsKey;
	mutable QImage m_ModelsPicture;
};
