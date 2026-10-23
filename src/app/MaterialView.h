//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A material (VMT) drawn the way it would look, beside its parameters, the textures it
//          names and its text. The preview can be a flat square or a ball, lit by a light that
//          follows a drag, with its proxies animating and, for HDR skies, an exposure.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>

#include <QElapsedTimer>
#include <QImage>
#include <QWidget>

#include "material/Material.h"
#include "material/Preview.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;
class QTreeWidget;
class CMaterialCanvas;

class CMaterialView : public QWidget
{
	Q_OBJECT

public:
	// pfnRead reads the textures and included materials the material names (game paths); it may be
	// empty, when nothing can be looked up.
	CMaterialView( const QString &szPath, const QString &szSource, std::span<const std::byte> bytes, xv::material::Reader_t pfnRead, QWidget *pParent = nullptr );
	~CMaterialView() override;

	// nullopt when the text has no shader block.
	[[nodiscard]] const std::optional<xv::material::CMaterial> &GetMaterial() const { return m_Material; }
	[[nodiscard]] const xv::material::CTextureSet &GetTextures() const { return m_Textures; }
	[[nodiscard]] const QImage &GetPreview() const { return m_Preview; }
	// Draws the preview again now, as at a time (seconds) for the proxies.
	void RenderAt( float flTime );

signals:
	// A texture or included material was double-clicked: its game path ("materials/x.vtf").
	void OpenRequested( const QString &szGamePath );

private:
	void Render();
	void FillParameters();
	void FillTextures();
	void UpdateNotes();
	[[nodiscard]] xv::material::PreviewOptions_t Options() const;

	std::optional<xv::material::CMaterial> m_Material;
	xv::material::CTextureSet m_Textures;
	std::unique_ptr<xv::material::CProxies> m_pProxies;
	QImage m_Preview;
	float m_flTime = 0.0f;
	xv::CVec3 m_vecLight{ -0.45f, 0.55f, 0.7f };
	QElapsedTimer m_Clock;
	float m_flClockStart = 0.0f;

	CMaterialCanvas *m_pCanvas = nullptr;
	QComboBox *m_pShape = nullptr;
	QSpinBox *m_pRepeat = nullptr;
	QComboBox *m_pBackdrop = nullptr;
	QPushButton *m_pPlay = nullptr;
	QCheckBox *m_pHdr = nullptr;
	QSlider *m_pExposure = nullptr;
	QLabel *m_pExposureLabel = nullptr;
	QLabel *m_pSummary = nullptr;
	QLabel *m_pNotes = nullptr;
	QTreeWidget *m_pParameters = nullptr;
	QListWidget *m_pTextureList = nullptr;
	QTimer *m_pTimer = nullptr;
};
