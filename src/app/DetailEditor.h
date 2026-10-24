//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Detail types, group and entry cards, messages and an interactive sprite sheet.
//===========================================================================//
#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <QWidget>
#include <QImage>
#include "DetailDocument.h"
#include "detail/Detail.h"
#include "texture/Sheet.h"

class QAction;
class QComboBox;
class QLabel;
class QListWidget;
class QStackedWidget;
class QToolButton;
class CDetailPreview;
class QTreeWidget;
class QLineEdit;
class QScrollArea;
class QVBoxLayout;
class CDetailSheet;

class CDetailEditor : public QWidget
{
	Q_OBJECT
public:
	explicit CDetailEditor( CDetailDocument *pDocument, QWidget *pParent = nullptr );
	[[nodiscard]] CDetailDocument *GetDocument() const { return m_pDocument; }
	void SetContext( xv::material::Reader_t pfnRead, std::vector<std::string> materials, bool bHaveGameFiles = true );
	void SelectType( const QString &szName );
	void SelectNode( xv::detail::NodePath_t path );
	void SetField( const xv::detail::NodePath_t &path, const QString &szKey, const QString &szValue );
	void SetSpriteRect( const xv::detail::NodePath_t &path, const QRectF &rect );
	// A copy of the type, group or entry at path, right after it, its sprites moved by delta (none:
	// beside the original's where there is room). The copy is selected. Ctrl+D, or Ctrl-drag on the sheet.
	void DuplicateNode( const xv::detail::NodePath_t &path, std::optional<QPointF> delta = std::nullopt );
	// Moves every sprite of the type, group or entry at path by delta texels, as one step.
	void MoveSprites( const xv::detail::NodePath_t &path, QPointF delta );
	// Puts copied, pasted or dropped blocks at target: inside it when they belong one level down (an
	// entry on a group, a field on anything), else beside it. With moveFrom, the block moves from
	// there in this file instead. Returns false when they don't fit there.
	bool InsertNodes( std::vector<xv::kv1::CNode> nodes, const xv::detail::NodePath_t &target, bool bAfter, std::optional<xv::detail::NodePath_t> moveFrom = std::nullopt );
	// Ctrl+C and Ctrl+V on the selection.
	void CopySelection();
	void PasteAtSelection();
	bool PackPictures( const xv::detail::NodePath_t &group, const std::map<std::string, xv::texture::CImage> &pictures, const QString &szMaterial, QString *pError = nullptr );
	[[nodiscard]] const auto &GetIssues() const { return m_Issues; }
	[[nodiscard]] QString GetSpriteMaterial() const;
	void SetSpriteMaterial( const QString &szMaterial );
	void Rebuild();
	void Check();
	// The material the 3D preview's ground is drawn in.
	void SetGround( const QString &szMaterial );
	// The right side: the sprite sheet (false) or the 3D preview (true).
	void ShowPreview( bool b3d );
	[[nodiscard]] CDetailPreview *GetPreview() const { return m_pPreview; }
signals:
	void RefreshRequested();
	void OpenTextureRequested( const QString &szPath );
	void PackedTexture( const QString &szMaterial, const std::vector<std::byte> &bytes );

private:
	QWidget *BuildCard( const xv::detail::NodePath_t &path );
	void AddType();
	void AddBlock( const xv::detail::NodePath_t &parent, bool bModel );
	void RemoveNode( const xv::detail::NodePath_t &path );
	void RenameNode( const xv::detail::NodePath_t &path );
	void ShowCardMenu( const xv::detail::NodePath_t &path, const QPoint &globalAt );
	void ShowTypesMenu( const QPoint &at );
	// Where a block of nDepth (0: a field) goes when put at target; nullopt where it can't.
	struct Placement_t
	{
		xv::detail::NodePath_t m_Parent;
		std::size_t m_nIndex = 0;
		bool m_bInto = false;
	};
	[[nodiscard]] std::optional<Placement_t> PlaceAt( std::size_t nDepth, const xv::detail::NodePath_t &target, bool bAfter ) const;
	// Whether the block at from can go there: not into itself, and somewhere else than where it is.
	[[nodiscard]] static bool CanMove( const xv::detail::NodePath_t &from, const Placement_t &place );
	// The selection shown on the cards, sheet and preview, without making the cards again.
	void ApplySelection();
	void LoadSheet();
	void ChoosePictures();
	void UpdateSheetRects();
	void QueueRebuild();
	[[nodiscard]] QWidget *EntryPicture( const xv::detail::NodePath_t &path );
	// The "sprite" and "spritesize" values that put an entry's picture at rect on the sheet.
	[[nodiscard]] std::optional<std::pair<std::string, std::string>> SpriteValues( const xv::kv1::CNode &node, const QRectF &rect ) const;
	void FillGrounds();

	CDetailDocument *m_pDocument;
	xv::detail::CSchema m_Schema;
	xv::material::Reader_t m_pfnRead;
	xv::detail::LintContext_t m_Context;
	std::vector<xv::detail::Issue_t> m_Issues;
	xv::detail::NodePath_t m_Selected;
	QListWidget *m_pTypes;
	QScrollArea *m_pCards;
	QTreeWidget *m_pMessages;
	QLineEdit *m_pMaterial;
	CDetailSheet *m_pSheet;
	QAction *m_pDuplicate = nullptr;
	QToolButton *m_pDuplicateButton = nullptr;
	CDetailPreview *m_pPreview = nullptr;
	QStackedWidget *m_pRight = nullptr;
	QToolButton *m_pSheetButton = nullptr;
	QToolButton *m_p3dButton = nullptr;
	QComboBox *m_pGround = nullptr;
	QLabel *m_pGroundPicture = nullptr;
	QString m_szGroundType;          // the type the ground list was made for
	bool m_bGroundChosen = false;    // picked by hand, so a new type doesn't change it
	QString m_szTexturePath;
	std::map<QString, QImage> m_PackedImages;
	bool m_bQueued = false;
	bool m_bFilling = false;
};
