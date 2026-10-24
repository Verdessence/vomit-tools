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
#include <QIcon>
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
	// The preview as drawn now (read back from the graphics card when it draws it).
	[[nodiscard]] QImage GetPreview() const;
	// Draws the preview again now, as at a time (seconds) for the proxies.
	void RenderAt( float flTime );
	// Only the preview and its controls, without the parameter, texture and text tabs (an editor
	// beside it shows those).
	void SetPreviewOnly( bool bOnly );
	// A small picture of the texture a parameter names, with what it is; a null icon when it
	// names none that was read.
	// The material on a flat square or a ball of side nSize, as at a time (seconds) for its proxies,
	// lit from vecLight; empty when the text has no shader block.
	[[nodiscard]] xv::texture::CImage RenderPicture( int nSize, xv::material::Shape_t nShape, xv::material::Backdrop_t nBackdrop, float flTime, xv::CVec3 vecLight ) const;
	// Whether a proxy changes it over time.
	[[nodiscard]] bool IsAnimated() const { return m_pProxies && m_pProxies->IsAnimated(); }
	[[nodiscard]] QIcon TextureIcon( std::string_view szParameter, int nSize, QString *pDescription = nullptr ) const;

signals:
	// A texture or included material was double-clicked: its game path ("materials/x.vtf").
	void OpenRequested( const QString &szGamePath );

private:
	void Render();
	// Draws on the graphics card, or in software.
	void SetUseGpu( bool bOn );
	void FillParameters();
	void FillTextures();
	void UpdateNotes();
	[[nodiscard]] xv::material::PreviewOptions_t Options() const;

	std::optional<xv::material::CMaterial> m_Material;
	xv::material::CTextureSet m_Textures;
	std::unique_ptr<xv::material::CProxies> m_pProxies;
	std::optional<xv::material::CMaterial> m_Now; // with the proxies' results at m_flTime
	QImage m_Preview;                             // drawn in software
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
	QWidget *m_pTabs = nullptr;
	QTimer *m_pTimer = nullptr;
};
