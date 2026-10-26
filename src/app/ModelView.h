//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Orbiting studio mesh view with software and RHI drawing and inspection overlays.
//===========================================================================//
#pragma once
#include <QWidget>
#include <QImage>
#include "model/Mdl.h"
#include "model/Phy.h"
#include "ModelRenderer.h"
#include "particles/Render.h"

class CModelView : public QWidget
{
	Q_OBJECT
public:
	explicit CModelView( QWidget *pParent = nullptr );
	void SetModel( const xv::model::CModel *pModel, const xv::model::CStudioFile *pStudio, const std::vector<xv::particles::Sprite_t> *pSprites );
	void SetOverlays( bool bWire, bool bNormals, bool bBones, bool bHitboxes, bool bAttachments, bool bBounds, bool bPositions, bool bGrid );
	void Frame();
	void Turn( float flDegrees );
	void SetPhysics( const xv::model::Physics_t *pPhysics, bool bShow );
	// Drawn brighter whatever the overlays: a hitbox of the shown set and an attachment (-1: none).
	void SetHighlight( int nHitbox, int nAttachment );
	void ForgetTextures();
	void SetResources( const xv::model::CModel *pReference, const std::vector<ModelMaterial_t> *pMaterials );
	[[nodiscard]] QImage GetImage( QSize size );
	[[nodiscard]] QImage GetGpuImage( QSize size );

protected:
	void paintEvent( QPaintEvent *pEvent ) override;
	void resizeEvent( QResizeEvent *pEvent ) override;
	bool eventFilter( QObject *pObject, QEvent *pEvent ) override;

private:
	void Scene( QSize size, std::vector<xv::particles::ScreenMesh_t> &meshes, std::vector<xv::particles::ScreenQuad_t> &quads );
	void Redraw();
	const xv::model::CModel *m_pModel = nullptr;
	const xv::model::CModel *m_pReference = nullptr;
	const std::vector<ModelMaterial_t> *m_pMaterials = nullptr;
	std::vector<xv::material::Shading_t> m_Shading;
	const xv::model::CStudioFile *m_pStudio = nullptr;
	const std::vector<xv::particles::Sprite_t> *m_pSprites = nullptr;
	xv::particles::CCamera m_Camera;
	QWidget *m_pGpu = nullptr;
	QWidget *m_pLabels = nullptr;
	const xv::model::Physics_t *m_pPhysics = nullptr;
	bool m_bPhysics = false;
	int m_nHitbox = -1, m_nAttachment = -1;
	QPointF m_From;
	bool m_bWire = false, m_bNormals = false, m_bBones = false, m_bHitboxes = false, m_bAttachments = false, m_bBounds = false, m_bPositions = false, m_bGrid = true;
};
