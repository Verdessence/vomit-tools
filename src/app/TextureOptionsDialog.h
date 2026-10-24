//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The choices for making a VTF: from pictures (import) or from the texture itself in a
//          new format (convert). Format, version, size, mipmaps and flags; for an import also the
//          order of the pictures and whether they are one image, animation frames or a cube map.
//
//===========================================================================//
#pragma once

#include <vector>

#include <QDialog>
#include <QString>

#include "texture/Build.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QSpinBox;
class QTreeWidget;

// A picture offered for import, by the name it is listed under.
struct NamedSource_t
{
	QString m_szName;
	xv::texture::SourceImage_t m_Source;
};

// A list of the texture flags to tick, with what each does. bHeaderFlags also lists the ones
// made from the pictures (alpha, cube map); the cube map flag can't be changed.
[[nodiscard]] QTreeWidget *MakeFlagList( std::uint32_t nFlags, bool bHeaderFlags );
// The flags ticked in a MakeFlagList list, over the ones it doesn't list (nOthers).
[[nodiscard]] std::uint32_t FlagsFrom( const QTreeWidget *pFlags, std::uint32_t nOthers );

class CTextureOptionsDialog : public QDialog
{
	Q_OBJECT

public:
	enum Mode_t
	{
		MODE_IMPORT,
		MODE_CONVERT,
		MODE_BATCH, // settings for every picture of a folder: no pictures shown, each one image
	};
	enum Kind_t
	{
		KIND_SINGLE,
		KIND_FRAMES,
		KIND_CUBE,
		KIND_VOLUME,
		KIND_PANORAMA, // one equirectangular picture made into a cube map
	};

	// initial gives the starting choices; for an import, pictures named like a sky's faces
	// (…rt, …lf, …) are put in cube map order and six of them start as a cube map.
	CTextureOptionsDialog( Mode_t nMode, std::vector<NamedSource_t> sources, const xv::texture::BuildOptions_t &initial, QWidget *pParent = nullptr );

	[[nodiscard]] xv::texture::BuildOptions_t GetOptions() const;
	// The pictures in the order they go in (for a single image, only the first).
	[[nodiscard]] std::vector<xv::texture::SourceImage_t> GetSources() const;
	[[nodiscard]] Kind_t GetKind() const;

	// For tests: the controls by role.
	[[nodiscard]] QComboBox *GetFormatBox() const { return m_pFormat; }
	[[nodiscard]] QComboBox *GetKindBox() const { return m_pKind; }
	[[nodiscard]] QComboBox *GetPresetBox() const { return m_pPreset; }
	[[nodiscard]] QComboBox *GetAlphaFormatBox() const { return m_pAlphaFormat; }
	// Sets every control from a set of choices.
	void ApplyOptions( const xv::texture::BuildOptions_t &options );

	// The starting choices for pictures: DXT5 when any has see-through parts, else DXT1; 7.2;
	// mipmaps, a thumbnail, sized to the nearest power of two.
	[[nodiscard]] static xv::texture::BuildOptions_t DefaultsFor( const std::vector<NamedSource_t> &sources );

private:
	void Update();
	void MoveSelected( int nBy );
	void FillPresets( const QString &szCurrent );
	[[nodiscard]] QString Notes() const;

	Mode_t m_nMode;
	std::vector<NamedSource_t> m_Sources;
	xv::texture::BuildOptions_t m_Initial;
	QListWidget *m_pList = nullptr;
	QComboBox *m_pKind = nullptr;
	QComboBox *m_pPreset = nullptr;
	QComboBox *m_pFormat = nullptr;
	QComboBox *m_pAlphaFormat = nullptr;
	QComboBox *m_pMipFilter = nullptr;
	QCheckBox *m_pKeepAspect = nullptr;
	QCheckBox *m_pDistanceAlpha = nullptr;
	QDoubleSpinBox *m_pDistanceSpread = nullptr;
	QComboBox *m_pPanoramaSize = nullptr;
	QDoubleSpinBox *m_pPanoramaYaw = nullptr;
	QLabel *m_pPanoramaLabel = nullptr;
	std::uint32_t m_FlagsNotListed = 0; // flags the list doesn't show, kept as they were
	QLabel *m_pFormatNote = nullptr;
	QComboBox *m_pVersion = nullptr;
	QComboBox *m_pResize = nullptr;
	QComboBox *m_pMaxSize = nullptr;
	QLabel *m_pSize = nullptr;
	QCheckBox *m_pMipmaps = nullptr;
	QCheckBox *m_pLowRes = nullptr;
	QCheckBox *m_pAlphaFlags = nullptr;
	QDoubleSpinBox *m_pBumpScale = nullptr;
	QSpinBox *m_pFirstFrame = nullptr;
	QCheckBox *m_pStripAlpha = nullptr;
	QCheckBox *m_pHeightToNormal = nullptr;
	QDoubleSpinBox *m_pNormalScale = nullptr;
	QCheckBox *m_pGreenUp = nullptr;
	QTreeWidget *m_pFlags = nullptr;
	QLabel *m_pNotes = nullptr;
};
