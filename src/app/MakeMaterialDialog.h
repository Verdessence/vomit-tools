//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The choices for a material made for a texture: the shader, the texture and a normal
//          map, how alpha is used, two-sided, reflections, glow, phong, the surface property and
//          Hammer's keywords. Anything else is added in the material editor afterwards.
//
//===========================================================================//
#pragma once

#include <vector>

#include <QDialog>

#include "ChoiceBoxes.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;

class CMakeMaterialDialog : public QDialog
{
	Q_OBJECT

public:
	// szGamePath is the texture's ("materials/brick/wall01.vtf"); bAlpha whether it has an alpha
	// channel worth using; bNormal whether it is a normal map; surfaceProps the game's (with what
	// each is based on), if known.
	CMakeMaterialDialog( const QString &szGamePath, bool bAlpha, bool bNormal, const std::vector<Choice_t> &surfaceProps, QWidget *pParent = nullptr );

	// The material's text.
	[[nodiscard]] QString GetText() const;

	// $basetexture for a texture's game path: without materials/ and the ending.
	[[nodiscard]] static QString TexturePathFor( const QString &szGamePath );

	// For tests.
	[[nodiscard]] QComboBox *GetShaderBox() const { return m_pShader; }
	[[nodiscard]] QComboBox *GetAlphaBox() const { return m_pAlpha; }
	[[nodiscard]] QComboBox *GetSurfaceBox() const { return m_pSurface; }

private:
	void Update();

	QFormLayout *m_pForm = nullptr;
	QComboBox *m_pShader = nullptr;
	QLineEdit *m_pTexture = nullptr;
	QLineEdit *m_pBump = nullptr;
	QComboBox *m_pAlpha = nullptr;
	QDoubleSpinBox *m_pThreshold = nullptr;
	QCheckBox *m_pTwoSided = nullptr;
	QCheckBox *m_pReflect = nullptr;
	QComboBox *m_pReflectMask = nullptr;
	QCheckBox *m_pGlow = nullptr;
	QCheckBox *m_pPhong = nullptr;
	QComboBox *m_pSurface = nullptr;
	QLineEdit *m_pKeywords = nullptr;
	bool m_bNormal = false;
};
