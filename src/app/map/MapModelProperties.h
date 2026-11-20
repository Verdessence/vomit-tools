//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Inspect and edit the selected map entities' shared studio model.
//
//===========================================================================//
#pragma once

#include <optional>

#include <QElapsedTimer>
#include <QWidget>

#include "map/Mesh.h"
#include "model/Mdl.h"

class CMapDocument;
class CMapResources;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QTimer;
struct MapFrame_t;

namespace xv::map
{
class CScene;
struct Entity_t;
} // namespace xv::map

namespace xv::vfs
{
class CFileSystem;
}

class CMapModelProperties : public QWidget
{
	Q_OBJECT

public:
	explicit CMapModelProperties( CMapDocument &document, QWidget *pParent = nullptr );
	void SetFiles( std::shared_ptr<const xv::vfs::CFileSystem> pFiles );
	[[nodiscard]] bool RefreshSelection();
	void ApplyPreview( MapFrame_t &frame, const CMapResources &resources );

signals:
	void PreviewChanged();
	void OpenMaterialRequested( const QString &szPath );

protected:
	void showEvent( QShowEvent *pEvent ) override;
	void hideEvent( QHideEvent *pEvent ) override;

private:
	void ReadModel();
	void RefreshValues();
	void DrawNextSkin();
	void ChooseSequence();
	void Advance();
	void InvalidatePose();
	void ApplyKey( std::string_view szKey, const QString &szUndo, const std::function<std::string( const xv::map::Entity_t & )> &pfnValue );
	[[nodiscard]] const xv::model::CStudioFile *GetSequenceFile() const;
	[[nodiscard]] bool HasDefaultAnimation( const xv::map::Entity_t &entity ) const;

	CMapDocument &m_Document;
	std::shared_ptr<const xv::vfs::CFileSystem> m_pFiles;
	xv::model::Reader_t m_pfnRead;
	std::vector<xv::model::CStudioFile> m_Studios;
	std::vector<std::uint32_t> m_Objects;
	std::string m_szModel;
	std::string m_szLoadedModel;
	QLabel *m_pStatus = nullptr;
	QWidget *m_pPages = nullptr;
	QListWidget *m_pSkins = nullptr;
	QListWidget *m_pSequences = nullptr;
	QListWidget *m_pMaterials = nullptr;
	QFormLayout *m_pBodyGroups = nullptr;
	std::vector<std::pair<std::size_t, QComboBox *>> m_BodyChoices;
	QLabel *m_pModelInfo = nullptr;
	QPushButton *m_pPlay = nullptr;
	QSlider *m_pCycle = nullptr;
	QTimer *m_pAnimate = nullptr;
	QTimer *m_pPictures = nullptr;
	QElapsedTimer m_Clock;
	int m_nNextSkin = 0;
	int m_nFile = -1;
	int m_nSequence = -1;
	double m_flCycle = 0.0;
	double m_flDuration = 0.0;
	std::optional<std::pair<int, int>> m_MaterialSelection;
	std::weak_ptr<const xv::map::CScene> m_PosedScene;
	std::shared_ptr<const xv::map::MeshParts_t> m_pBaseParts;
	std::shared_ptr<const xv::map::MeshParts_t> m_pPreviewParts;
};
