//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The choices for a material made for a texture: which shader, how alpha is used, the
//          surface property, and the $basetexture path (from the texture's place under materials/).
//
//===========================================================================//
#pragma once

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLineEdit;

class CMakeMaterialDialog : public QDialog
{
	Q_OBJECT

public:
	// szGamePath is the texture's ("materials/brick/wall01.vtf"); bAlpha whether it has an alpha
	// channel worth using; bNormal whether it is a normal map; surfaceProps the game's names, if
	// known.
	CMakeMaterialDialog( const QString &szGamePath, bool bAlpha, bool bNormal, const QStringList &surfaceProps, QWidget *pParent = nullptr );

	// The material's text.
	[[nodiscard]] QString GetText() const;

	// $basetexture for a texture's game path: without materials/ and the ending.
	[[nodiscard]] static QString TexturePathFor( const QString &szGamePath );

private:
	QComboBox *m_pShader = nullptr;
	QLineEdit *m_pTexture = nullptr;
	QComboBox *m_pAlpha = nullptr;
	QComboBox *m_pSurface = nullptr;
	bool m_bNormal = false;
};
