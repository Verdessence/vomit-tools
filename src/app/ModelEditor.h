//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Compiled model tab: body, skin, LOD, sequence playback and inspection, and editing the
//          compiled files in place (see CModelDocument and model/Edit.h).
//===========================================================================//
#pragma once
#include <functional>
#include <QWidget>
#include <QElapsedTimer>
#include "model/Edit.h"
#include "model/Phy.h"
#include "ModelRenderer.h"
#include "model/Mdl.h"
#include "particles/Render.h"
class CModelView;
class CModelDocument;
class QTableWidget;
class QVBoxLayout;
class QComboBox;
class QSlider;
class QListWidget;
class QLabel;
class QCheckBox;
class QDoubleSpinBox;
class QTreeWidget;
class QTimer;
class QPushButton;
class CModelEditor : public QWidget
{
	Q_OBJECT
public:
	CModelEditor( QString szPath, xv::model::Reader_t pfnRead, QWidget *pParent = nullptr );
	[[nodiscard]] bool IsValid() const { return !m_Files.empty(); }
	[[nodiscard]] const QString &GetPath() const { return m_szPath; }
	[[nodiscard]] const QString &GetError() const { return m_szError; }
	[[nodiscard]] CModelView *GetView() const { return m_pView; }
	[[nodiscard]] const std::optional<xv::model::CModel> &GetModel() const { return m_Model; }
	[[nodiscard]] std::optional<std::vector<std::byte>> ReadFile( std::string_view szFile ) const { return m_pfnRead( szFile ); }
	void SetOrigin( QString szOrigin ) { m_szOrigin = std::move( szOrigin ); }
	[[nodiscard]] const QString &GetOrigin() const { return m_szOrigin; }
	[[nodiscard]] CModelDocument *GetDocument() const { return m_pDocument; }
	[[nodiscard]] bool IsModified() const;
	// The surface properties offered for $surfaceprop (the game's).
	void SetSurfaceProps( const QStringList &names );
	// Runs an edit as one undo step; a refused edit says why in the status line. For tests and the
	// panels.
	bool Commit( const QString &szName, const std::function<void( xv::model::Files_t & )> &pfnEdit );
	[[nodiscard]] QString GetStatus() const;
signals:
	void OpenMaterialRequested( const QString &szPath );
	void ModifiedChanged( bool bModified );

private:
	// The edit panels (ModelEditorPanels.cpp): each fills itself again through m_Refreshers.
	QWidget *BuildModelPanel();
	QWidget *BuildHitboxPanel();
	QWidget *BuildAttachmentPanel();
	QWidget *BuildMaterialPanel();
	QWidget *BuildSkeletonPanel();
	QWidget *BuildTransformPanel();
	QWidget *BuildSequenceEditor();
	void RefreshPanels();
	// Reads the edited files again and shows them.
	void Reload();
	[[nodiscard]] std::optional<xv::model::CStudioFile> ReadOther( const QString &szPath, QString *pError ) const;
	void ReloadMesh();
	void SelectSequence();
	void Sample();
	void Tick();
	void PopulateInfo();
	QElapsedTimer m_PlaybackClock;
	std::optional<xv::model::Physics_t> m_Physics;
	QString m_szPath, m_szOrigin, m_szError;
	xv::model::Reader_t m_pfnRead;
	std::vector<xv::model::CStudioFile> m_Files;
	std::vector<std::string> m_Problems;
	std::optional<xv::model::CModel> m_Model, m_Posed;
	xv::model::Selection_t m_Selection;
	std::vector<xv::particles::Sprite_t> m_Sprites;
	std::vector<ModelMaterial_t> m_Materials;
	std::vector<QComboBox *> m_Bodygroups;
	std::vector<QDoubleSpinBox *> m_Parameters;
	std::vector<xv::model::PoseParameter_t> m_PoseControls;
	CModelView *m_pView = nullptr;
	QComboBox *m_pSkin = nullptr, *m_pLod = nullptr, *m_pHitbox = nullptr;
	QListWidget *m_pSequences = nullptr;
	QSlider *m_pTimeline = nullptr;
	QLabel *m_pPlayback = nullptr, *m_pStatus = nullptr;
	QPushButton *m_pPlay = nullptr;
	QCheckBox *m_pLoop = nullptr;
	QDoubleSpinBox *m_pRate = nullptr;
	QTreeWidget *m_pInfo = nullptr, *m_pChecks = nullptr, *m_pEvents = nullptr;
	QTimer *m_pTimer = nullptr;
	int m_nSource = 0, m_nSequence = -1;
	float m_flCycle = 0;
	CModelDocument *m_pDocument = nullptr;
	std::vector<std::function<void()>> m_Refreshers;
	std::function<void()> m_RefreshSequence; // the sequence editor, when another sequence is picked
	QCheckBox *m_pPhysicsShown = nullptr;
	QStringList m_SurfaceProps;
	QComboBox *m_pSurfaceProp = nullptr;
	bool m_bRefreshing = false;
};
