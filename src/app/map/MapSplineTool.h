//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The spline tool: points clicked on surfaces in 3D (or on the grid in 2D) make a smooth
//          path, drawn as it will be swept (each piece's outline, the middle line); Enter makes
//          the strip of displacement pieces (map/Spline.h). The points can be dragged, inserted
//          on the curve and removed, picked several at a time (Shift) and moved, turned or
//          scaled with Brush Edit's keys (G, R, E, then X/Y/Z to lock); the picked point's
//          handle sets how the curve leaves it. Picking a spline's piece and choosing the tool
//          edits that spline: every change makes its pieces again, as one undo step each. While
//          a new path is laid out, Ctrl+Z takes its points back one at a time.
//          The side panel holds its width, piece length, power, thickness, material, loop and
//          grouping.
//
//===========================================================================//
#pragma once

#include <optional>
#include <set>
#include <utility>
#include <vector>

#include <QWidget>

#include "map/MapModalTransform.h"
#include "map/Spline.h"

class CMapEditor;
class CMapView;
class QCheckBox;
class QDoubleSpinBox;
class QEvent;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QPainter;
class QPushButton;
class QSpinBox;

class CMapSplineTool : public QWidget
{
	Q_OBJECT

public:
	explicit CMapSplineTool( CMapEditor &editor );

	// The tool chosen (or left): on choosing, a selected spline piece's spline is taken up.
	void SetActive( bool bActive );
	bool HandleEvent( CMapView &view, QEvent *pEvent );
	void Paint( CMapView &view, QPainter &painter ) const;
	// The keys the tool takes before the window's shortcuts see them (Ctrl+Z on a new path,
	// Ctrl+X, the transform keys with points picked, anything while a transform is typed).
	[[nodiscard]] bool WantsKey( const QKeyEvent &key ) const;

	// The path as it stands (its id is the spline's when editing one, else 0).
	[[nodiscard]] const xv::map::Spline_t &GetSpline() const { return m_Spline; }
	[[nodiscard]] bool IsEditing() const { return m_Spline.m_nId != 0; }
	// The picked points (the last one picked is the one whose handle shows).
	[[nodiscard]] const std::set<std::size_t> &GetPicked() const { return m_Picked; }
	void Pick( std::size_t nPoint, bool bAdd );
	// A point at the end of the path, and one between two, as clicks make them.
	void AddPoint( const xv::map::Point_t &point );
	void InsertPoint( std::size_t nAfter, const xv::map::Point_t &point );
	void MovePoint( std::size_t nPoint, const xv::map::Point_t &point );
	void RemovePoint( std::size_t nPoint );
	// The picked points moved, turned or scaled together (one rebuild of an edited spline).
	void TransformPicked( const xv::map::Transform_t &transform );
	// A point's handle: its own (across the ground), or none for the curve's own.
	void SetHandle( std::size_t nPoint, std::optional<xv::map::Point_t> handle );
	// Where a point's handle reaches: its own, else the curve's own way out of it.
	[[nodiscard]] xv::map::Point_t HandleOf( std::size_t nPoint ) const;
	// A new path's points back as they were before the last change, and forward again.
	bool Undo();
	bool Redo();
	// Makes the pieces: a new spline's (then edits it), or the edited one's again. False with a
	// status message when they can't be made.
	bool Build();
	// Drops the points of a new path; for an edited one, puts the panel back as the map has it.
	void Cancel();
	// Takes up the spline a solid (by object number) is a piece of.
	bool EditSplineOf( std::uint32_t nObject );
	// Deletes the edited spline and its pieces.
	void DeleteSpline();

private:
	void ReadPanel();
	void WritePanel();
	void Rebuild();
	void SceneEdited();
	void Refresh();
	void UpdateStatus();
	// Before a new path's points change: kept for Ctrl+Z.
	void Remember();
	void ClearPicked();
	void BrowseMaterial();
	[[nodiscard]] std::optional<QPointF> Project( CMapView &view, const xv::map::Point_t &point ) const;
	// Where a click puts a point: on a surface in 3D (on a displacement, snapped across the
	// ground and traced down onto it), on the grid in 2D (the hidden axis from the nearest
	// point, or 0).
	[[nodiscard]] std::optional<xv::map::Point_t> PointerPoint( CMapView &view, QPointF at, bool bSnap, std::optional<std::size_t> nKeepFrom ) const;
	// The pointer on the level of a height (a handle's end is dragged across the ground).
	[[nodiscard]] std::optional<xv::map::Point_t> GroundPoint( CMapView &view, QPointF at, double flHeight ) const;
	// The point whose handle is under the pointer, if any.
	[[nodiscard]] std::optional<std::size_t> PointAt( CMapView &view, QPointF at ) const;
	// The picked point's handle end under the pointer: the point and which end (1 ahead, -1
	// behind).
	[[nodiscard]] std::optional<std::pair<std::size_t, int>> HandleEndAt( CMapView &view, QPointF at ) const;
	// The point the pointer is over on the curve's middle, and after which point it would go.
	[[nodiscard]] std::optional<std::pair<std::size_t, xv::map::Point_t>> CurveAt( CMapView &view, QPointF at ) const;
	// A typed move, turn or scale of the picked points (Brush Edit's keys).
	void BeginModal( CMapView &view, MapGizmoMode_t nMode );
	void PreviewModal( CMapView &view, Qt::KeyboardModifiers modifiers );
	void EndModal( bool bApply );
	bool HandleModalEvent( CMapView &view, QEvent *pEvent );

	CMapEditor &m_Editor;
	xv::map::Spline_t m_Spline;
	std::optional<std::size_t> m_nSelected; // the last picked: its handle shows
	std::set<std::size_t> m_Picked;
	std::optional<std::size_t> m_nDragging;
	std::optional<std::pair<std::size_t, int>> m_DraggingHandle;
	bool m_bMoved = false; // the drag moved its point
	bool m_bActive = false;
	bool m_bPanelUpdating = false;
	// A new path's points before each change, and those undone.
	std::vector<std::vector<xv::map::SplinePoint_t>> m_Undo;
	std::vector<std::vector<xv::map::SplinePoint_t>> m_Redo;
	// A typed transform under way: the points as it started, and the view it was started in.
	std::optional<CMapModalTransform> m_Modal;
	std::vector<xv::map::SplinePoint_t> m_ModalFrom;
	CMapView *m_pModalView = nullptr;
	xv::map::Transform_t m_ModalTransform;

	QDoubleSpinBox *m_pWidth = nullptr;
	QDoubleSpinBox *m_pPieceLength = nullptr;
	QSpinBox *m_pPower = nullptr;
	QDoubleSpinBox *m_pThickness = nullptr;
	QDoubleSpinBox *m_pScale = nullptr;
	QCheckBox *m_pClosed = nullptr;
	QCheckBox *m_pGroup = nullptr;
	QLineEdit *m_pMaterial = nullptr;
	QPushButton *m_pBuild = nullptr;
	QPushButton *m_pDelete = nullptr;
	QPushButton *m_pAutoHandle = nullptr;
	QLabel *m_pStatus = nullptr;
};
