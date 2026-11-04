//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Brush part selection, transforms and mesh operations shared by the map views.
//===========================================================================//
#pragma once

#include <QWidget>
#include <QPointF>
#include <memory>
#include <optional>
#include "map/BrushMesh.h"
#include "map/MapModalTransform.h"

class CMapEditor;
class CMapView;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QPainter;

class CMapBrushEdit : public QWidget
{
	Q_OBJECT
public:
	explicit CMapBrushEdit( CMapEditor &editor );
	[[nodiscard]] const std::vector<xv::map::BrushPart_t> &GetParts() const { return m_Parts; }
	void SetParts( std::vector<xv::map::BrushPart_t> parts );
	void SetMode( int nMode );
	void Paint( CMapView &view, QPainter &painter ) const;
	bool HandleEvent( CMapView &view, QEvent *pEvent );
	void Cancel();
	void Transform( const xv::map::Transform_t &transform );
	void BeginTransform( MapGizmoMode_t nMode );
	void ApplyOperation( int nOperation );

private:
	struct Handle_t
	{
		xv::map::BrushPart_t m_Part;
		xv::map::Point_t m_Position;
		QPointF m_Screen;
	};
	[[nodiscard]] std::vector<Handle_t> Handles( CMapView &view ) const;
	void Refresh();
	[[nodiscard]] std::optional<CMapGizmo> Gizmo() const;
	[[nodiscard]] xv::map::CViewCamera Camera( CMapView &view ) const;
	void PreviewTransform( CMapView &view, Qt::KeyboardModifiers modifiers );
	void Commit( xv::map::MapEdit_t edit, const QString &szName );
	[[nodiscard]] xv::map::BrushConcave_t GetConcave() const;

	CMapEditor &m_Editor;
	QComboBox *m_pMode = nullptr;
	QComboBox *m_pConcave = nullptr;
	QCheckBox *m_pXray = nullptr;
	QDoubleSpinBox *m_pDistance = nullptr;
	std::vector<xv::map::BrushPart_t> m_Parts;
	std::shared_ptr<const xv::map::CScene> m_pBase;
	std::optional<QPointF> m_From;
	QPointF m_At;
	xv::map::Point_t m_Anchor{};
	xv::map::Transform_t m_Transform;
	std::optional<CMapModalTransform> m_Modal;
	std::optional<CMapGizmo> m_DragGizmo;
	int m_nGizmoPart = -1;
	bool m_bBox = false;
	bool m_bApplying = false;
};
