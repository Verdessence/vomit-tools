//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The spline tool: points clicked on surfaces in 3D (or on the grid in 2D) make a smooth
//          path, drawn as it will be swept (its middle, its edges, where each piece ends);
//          Enter makes the strip of displacement pieces (map/Spline.h). The points can be
//          dragged, inserted on the curve and removed. Picking a spline's piece and choosing the
//          tool edits that spline: every change makes its pieces again, as one undo step each.
//          The side panel holds its width, piece length, power, thickness, material and loop.
//
//===========================================================================//
#pragma once

#include <optional>

#include <QWidget>

#include "map/Spline.h"

class CMapEditor;
class CMapView;
class QCheckBox;
class QDoubleSpinBox;
class QEvent;
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

	// The path as it stands (its id is the spline's when editing one, else 0).
	[[nodiscard]] const xv::map::Spline_t &GetSpline() const { return m_Spline; }
	[[nodiscard]] bool IsEditing() const { return m_Spline.m_nId != 0; }
	// A point at the end of the path, and one between two, as clicks make them.
	void AddPoint( const xv::map::Point_t &point );
	void InsertPoint( std::size_t nAfter, const xv::map::Point_t &point );
	void MovePoint( std::size_t nPoint, const xv::map::Point_t &point );
	void RemovePoint( std::size_t nPoint );
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
	[[nodiscard]] std::optional<QPointF> Project( CMapView &view, const xv::map::Point_t &point ) const;
	// Where a click puts a point: on a surface in 3D, on the grid in 2D (the hidden axis from
	// the nearest point, or 0).
	[[nodiscard]] std::optional<xv::map::Point_t> PointerPoint( CMapView &view, QPointF at, bool bSnap, std::optional<std::size_t> nKeepFrom ) const;
	// The point whose handle is under the pointer, if any.
	[[nodiscard]] std::optional<std::size_t> PointAt( CMapView &view, QPointF at ) const;
	// The point the pointer is over on the curve's middle, and after which point it would go.
	[[nodiscard]] std::optional<std::pair<std::size_t, xv::map::Point_t>> CurveAt( CMapView &view, QPointF at ) const;

	CMapEditor &m_Editor;
	xv::map::Spline_t m_Spline;
	std::optional<std::size_t> m_nSelected;
	std::optional<std::size_t> m_nDragging;
	bool m_bMoved = false; // the drag moved its point
	bool m_bActive = false;
	bool m_bPanelUpdating = false;

	QDoubleSpinBox *m_pWidth = nullptr;
	QDoubleSpinBox *m_pPieceLength = nullptr;
	QSpinBox *m_pPower = nullptr;
	QDoubleSpinBox *m_pThickness = nullptr;
	QDoubleSpinBox *m_pScale = nullptr;
	QCheckBox *m_pClosed = nullptr;
	QLineEdit *m_pMaterial = nullptr;
	QPushButton *m_pBuild = nullptr;
	QPushButton *m_pDelete = nullptr;
	QLabel *m_pStatus = nullptr;
};
