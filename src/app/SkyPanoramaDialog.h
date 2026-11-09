//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Import Panorama: how an equirectangular panorama (an HDRI, or an 8-bit picture)
//          becomes the six sky faces: face size, which way it faces, and for light values how
//          bright the HDR set is and how the LDR faces are tone mapped, with a preview to look
//          around in.
//
//===========================================================================//
#pragma once

#include <QDialog>
#include <QString>

#include "sky/Sky.h"
#include "texture/Image.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QTimer;
class CSkyView;

class CSkyPanoramaDialog : public QDialog
{
	Q_OBJECT

public:
	// bHdr: the panorama holds light values (.hdr, .exr, .pfm).
	CSkyPanoramaDialog( xv::texture::CFloatImage panorama, bool bHdr, const QString &szFileName, QWidget *pParent = nullptr );

	[[nodiscard]] xv::sky::PanoramaImport_t GetOptions() const;
	[[nodiscard]] bool IsHdr() const { return m_bHdr; }
	// The panorama made into faces at full size (a few seconds for big faces).
	[[nodiscard]] xv::sky::PanoramaSky_t MakeSky() const;

	void accept() override;

private:
	void UpdatePreview();

	xv::texture::CFloatImage m_Panorama;
	// A smaller copy for the preview, so a large panorama keeps the controls quick.
	xv::texture::CFloatImage m_PreviewPanorama;
	bool m_bHdr = false;
	QComboBox *m_pSize = nullptr;
	QDoubleSpinBox *m_pYaw = nullptr;
	QDoubleSpinBox *m_pBrightness = nullptr;
	QDoubleSpinBox *m_pExposure = nullptr;
	QComboBox *m_pCurve = nullptr;
	QLabel *m_pLevel = nullptr;
	CSkyView *m_pView = nullptr;
	xv::sky::CCube m_PreviewCube;
	QTimer *m_pPreviewTimer = nullptr;
};
