//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A detail type on a square of ground in 3D, scattered the way the map compiler does it:
//          the ground in its material, blended from one end to the other so each group shows
//          where its alpha is, sprites from the sheet in their orientation and shape, and models.
//          Drag to turn around it, right-drag to move, the wheel zooms; the picked entry is tinted.
//          Sprites can sway in the wind, by each entry's sway.
//
//===========================================================================//
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>

#include <QElapsedTimer>
#include <QImage>
#include <QWidget>

#include "detail/Detail.h"
#include "detail/Scatter.h"
#include "material/Material.h"
#include "model/Model.h"
#include "particles/Render.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QTimer;
class QLabel;
class QSpinBox;
class CDetailView;

class CDetailPreview : public QWidget
{
	Q_OBJECT

public:
	explicit CDetailPreview( QWidget *pParent = nullptr );

	// What is drawn: the file, the type (its path) and the entry picked, which is tinted.
	void SetScene( const xv::kv1::CDocument *pDocument, xv::detail::NodePath_t type, xv::detail::NodePath_t selected );
	// The sprite sheet the sprites come from.
	void SetSheet( const QImage &sheet );
	// Reads the ground material, the models and their materials.
	void SetReader( xv::material::Reader_t pfnRead );
	// The material the ground is drawn in ("nature/blendgrassdirt01"); empty for plain grey.
	void SetGround( const QString &szMaterial );

	// The ground's texture, for a thumbnail; null when there is none.
	[[nodiscard]] QImage GetGroundPicture() const;

	// For tests.
	[[nodiscard]] QImage GetImage() const { return m_Image; }
	[[nodiscard]] std::size_t GetPlacedCount() const { return m_nPlaced; }
	// Draws now rather than at the next turn of the event loop.
	void RenderNow();

signals:
	void OpenModelRequested( const QString &szPath );

protected:
	void showEvent( QShowEvent *pEvent ) override;

private:
	friend class CDetailView;
	struct Model_t
	{
		std::optional<xv::model::CModel> m_Model;
		std::vector<std::shared_ptr<xv::particles::Sprite_t>> m_Sprites;
	};
	void Redraw();
	void Render();
	const Model_t &ModelFor( const std::string &szPath );
	[[nodiscard]] xv::CVec3 SwayOffset( const xv::CVec3 &vecOrigin, const std::vector<double> &sway ) const;

	const xv::kv1::CDocument *m_pDocument = nullptr;
	xv::detail::NodePath_t m_Type;
	xv::detail::NodePath_t m_Selected;
	xv::material::Reader_t m_pfnRead;
	xv::particles::Sprite_t m_Sheet;
	std::optional<xv::particles::Sprite_t> m_Ground;
	QString m_szGround;
	std::map<std::string, Model_t> m_Models;
	xv::particles::CCamera m_Camera;
	QImage m_Image;
	struct ModelTriangle_t
	{
		std::array<QPointF, 3> m_Corners;
		QString m_szPath;
		float m_flDepth = 0;
	};
	std::vector<ModelTriangle_t> m_ModelTriangles;
	std::size_t m_nPlaced = 0;
	bool m_bQueued = false;
	CDetailView *m_pView = nullptr;
	QSpinBox *m_pSeed = nullptr;
	QComboBox *m_pSize = nullptr;
	QComboBox *m_pBlend = nullptr;
	QLabel *m_pNotes = nullptr;
	QCheckBox *m_pSway = nullptr;
	QDoubleSpinBox *m_pMaxSway = nullptr;
	QSpinBox *m_pWind = nullptr;
	QTimer *m_pSwayTimer = nullptr;
	QElapsedTimer m_SwayClock;
};
