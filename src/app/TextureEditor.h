//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Editor for one texture (.vtf) and the material (.vmt) that goes with it. The texture:
//          every mipmap, frame, face and slice, its channels on their own, zoomed and tiled, a cube
//          map unwrapped as a cross, beside what the file holds (header, thumbnail, resources) and
//          its flags to tick. Import makes it from pictures; Convert writes it again in another
//          format; Properties changes the rest of the header; Export saves pictures. The material:
//          its editor in the side panel's Material tab, drawn in place of the texture while that
//          tab is open, with its Messages below; it sees the texture as edited, saved or not.
//
//===========================================================================//
#pragma once

#include <vector>

#include <QImage>
#include <QWidget>

#include "texture/Build.h"
#include "util/Vec3.h"

class QAction;
class QComboBox;
class QLabel;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class CMaterialEditor;
class CTextureCanvas;
class CTextureDocument;

// Every frame, face and slice of a texture's full-size image as picture files in a folder, named
// <base>[_frame][_face][_slice].<extension> (png, tga, bmp, jpg, hdr). The number saved.
int ExportPictures( const xv::texture::CVtf &vtf, const QString &szFolder, const QString &szBaseName, const QString &szExtension );
// A picture file as sources for a texture: light values too when it is HDR; every frame of an
// animated GIF; a VTF gives its full-size images. Empty, with the reason, when it can't be read.
[[nodiscard]] std::vector<xv::texture::SourceImage_t> ReadSources( const QString &szPath, QString *pError );

// The open dialog's filter for pictures: ours and what Qt's image plugins read.
[[nodiscard]] QString PictureFilter( bool bWithVtf );
// The picture files read: ours, and what Qt's plugins read ("*.png", "*.webp"...).
[[nodiscard]] QStringList PicturePatterns();
// The types pictures can be saved as: ours (png tga bmp jpg hdr qoi pfm), then what Qt's plugins
// add (webp, exr, jxl, avif, tiff...).
[[nodiscard]] QStringList PictureTypesForSaving();

class CTextureEditor : public QWidget
{
	Q_OBJECT

public:
	explicit CTextureEditor( CTextureDocument *pDocument, QWidget *pParent = nullptr );

	[[nodiscard]] CTextureDocument *GetDocument() const { return m_pDocument; }

	// Makes the texture from pictures on disk (every frame of a GIF), after asking how; one undo
	// step. False when the pictures can't be read or the dialog is cancelled.
	bool ImportFiles( const QStringList &files );
	// Writes the texture again with other choices, after asking which; one undo step.
	bool Convert();
	// Changes the header (version, bump scale...) without touching the pictures, after asking;
	// one undo step.
	bool EditProperties();
	// Turns a flag on or off; one undo step. False when nothing changed.
	bool SetFlag( std::uint32_t nBit, bool bOn );
	// Makes it again as a spray that fits (under 512 KB, at most 4096 on a side, no mipmaps);
	// one undo step. False, after saying why, when it can't be.
	bool MakeSpray();

	// The material that goes with the texture; the editor takes it (and deletes the one it had).
	// Null takes it away.
	void SetMaterial( CMaterialEditor *pMaterial );
	[[nodiscard]] CMaterialEditor *GetMaterial() const { return m_pMaterial; }
	// Turns the side panel to the material or the texture. Turning to the material draws the
	// texture as the material does; the viewer's Material switch does that either way.
	void ShowMaterial();
	void ShowTexture();
	[[nodiscard]] bool IsShowingMaterial() const;
	void SetShowAsMaterial( bool bOn );
	[[nodiscard]] bool IsShowingAsMaterial() const;
	// Whether the texture or its material has changes not saved.
	[[nodiscard]] bool IsModified() const;
	// The sprite sheet editor (starting from the texture's own sheet when it has one); one undo
	// step.
	bool EditSheet();
	// Saves the picked image (not the channels picked for showing) as a picture file, by the
	// path's ending: png, tga, bmp, jpg, or hdr (light values for HDR textures).
	bool ExportCurrent( const QString &szPath );
	// Saves every frame, face and slice of the full-size image into a folder as <name>_..., with
	// the given ending. The number saved.
	int ExportAll( const QString &szFolder, const QString &szExtension );

	// What is shown, for tests: the image as drawn (channels applied) and the picked image.
	[[nodiscard]] QImage GetShownImage() const { return m_Shown; }
	void ShowImage( int nMip, int nFrame, int nFace, int nSlice = 0 );
	// Which channels are drawn: "rgba" for all, "a" for alpha alone...
	void SetChannels( const QString &szChannels );
	// A cube map's six faces as a cross, joined as they meet.
	void SetCrossView( bool bCross );

signals:
	// Create VMT was picked: the window writes a material for this texture.
	void MakeMaterialRequested();
	// The material's menu: save it elsewhere, or put it away.
	void SaveMaterialAsRequested();
	void CloseMaterialRequested();
	// The texture or its material became changed or saved; a material came or went.
	void ModifiedChanged();

protected:
	void dragEnterEvent( QDragEnterEvent *pEvent ) override;
	void dropEvent( QDropEvent *pEvent ) override;

private:
	void Refresh();
	void FillSelectors();
	void FillInfo();
	void UpdateView();
	void UpdateMaterialTab();
	[[nodiscard]] QString BaseName() const;

	CTextureDocument *m_pDocument;
	CTextureCanvas *m_pCanvas = nullptr;
	QTreeWidget *m_pInfo = nullptr;
	QStackedWidget *m_pSide = nullptr; // Texture, Material
	QToolButton *m_pTextureSide = nullptr;
	QToolButton *m_pMaterialSide = nullptr;
	QAction *m_pAsMaterial = nullptr; // the viewer draws the material
	QComboBox *m_pBackdrop = nullptr;
	QComboBox *m_pShape = nullptr; // flat or a ball
	QAction *m_pShapeAction = nullptr;
	xv::CVec3 m_vecLight{ -0.45f, 0.55f, 0.7f }; // towards the light, for the material
	bool m_bLightMoving = false;
	QAction *m_pBackdropAction = nullptr;
	float m_flMaterialTime = 0.0f;     // for the material's proxies while playing
	QWidget *m_pPreviewPage = nullptr; // keeps the material's own preview, unshown
	QStackedWidget *m_pMaterialPage = nullptr; // the offer to create one, or the editor
	QWidget *m_pMaterialHolder = nullptr;      // the editor with its own bar
	QLabel *m_pMaterialName = nullptr;
	QWidget *m_pMessagesHost = nullptr;
	CMaterialEditor *m_pMaterial = nullptr;
	QList<QAction *> m_ViewActions; // only for the texture itself (mipmaps, frames, faces...)
	QComboBox *m_pMip = nullptr;
	QSpinBox *m_pFrame = nullptr;
	QComboBox *m_pFace = nullptr;
	QSpinBox *m_pSlice = nullptr;
	QSpinBox *m_pRate = nullptr;
	QAction *m_pPlay = nullptr;
	// The toolbar shows and hides widgets through these.
	QAction *m_pMipAction = nullptr;
	QAction *m_pFrameAction = nullptr;
	QAction *m_pRateAction = nullptr;
	QAction *m_pFaceAction = nullptr;
	QAction *m_pSliceAction = nullptr;
	QAction *m_pCross = nullptr;
	QAction *m_pFullSize = nullptr;
	QTimer *m_pPlayTimer = nullptr;
	QToolButton *m_pChannels[ 4 ] = {};
	QLabel *m_pStatus = nullptr;
	QImage m_Shown;
	bool m_bFilling = false;
};
