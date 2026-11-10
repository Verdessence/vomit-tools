//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The side's Recent panel: the textures, entity classes and models used lately, a
//          click away. A texture becomes the current one, a class the entity tool's, and a
//          model goes on the selected entities. The textures are the map's own (as the Texture
//          panel keeps them); classes and models are kept between maps.
//
//===========================================================================//
#pragma once

#include <QStringList>
#include <QWidget>

class QListWidget;

class CMapRecentPanel : public QWidget
{
	Q_OBJECT

public:
	explicit CMapRecentPanel( QWidget *pParent = nullptr );

	void SetTextures( const QStringList &textures );
	// Moves it to the top, or adds it there.
	void AddClass( const QString &szClass );
	void AddModel( const QString &szModel );
	[[nodiscard]] QListWidget *GetTextures() const { return m_pTextures; }
	[[nodiscard]] QListWidget *GetClasses() const { return m_pClasses; }
	[[nodiscard]] QListWidget *GetModels() const { return m_pModels; }

	static constexpr int MAX_RECENT = 12;

signals:
	void TexturePicked( const QString &szTexture );
	void ClassPicked( const QString &szClass );
	void ModelPicked( const QString &szModel );

private:
	static void Fill( QListWidget *pList, const QStringList &items );
	static void Raise( QListWidget *pList, const QString &szItem, const char *pszSetting );

	QListWidget *m_pTextures = nullptr;
	QListWidget *m_pClasses = nullptr;
	QListWidget *m_pModels = nullptr;
};
