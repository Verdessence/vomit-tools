//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What the editor knows about the mounted game, for defaults, checks and material
//          completion.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <QIcon>
#include <QStringList>
#include <QWidget>

#include "particles/Particles.h"
#include "particles/Render.h"

class CParticleDocument;
class CParticlePreview;
class QAction;
class QLabel;
class QLineEdit;
class QListWidget;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

struct ParticleGame_t
{
	std::shared_ptr<const xv::particles::CSchema> m_pSchema;      // what the game's particle files use; null without a game
	std::function<bool( std::string_view szGamePath )> m_pfnFileExists; // unset without a game
	std::optional<std::vector<std::string>> m_Manifest;          // the files particles_manifest.txt lists
	QStringList m_Materials;                                      // "effects/fire_cloud1.vmt", for completion
	xv::particles::FileReader_t m_pfnReadFile;                         // game files, for the preview's materials
	std::vector<xv::particles::SystemSummary_t> m_Systems;          // every system in the game's particle files
};

// Editor for one particle file: its systems on the left, the selected system's operators in the
// middle (emitters, initializers, operators, renderers, forces, constraints and child systems), and
// the selected item's properties on the right, with the checks underneath.
class CParticleEditor : public QWidget
{
	Q_OBJECT

public:
	// Takes ownership of document.
	explicit CParticleEditor( CParticleDocument *pDocument, QWidget *pParent = nullptr );

	[[nodiscard]] CParticleDocument *GetDocument() const { return m_pDocument; }
	void SetGame( ParticleGame_t game );
	[[nodiscard]] const std::vector<xv::particles::Issue_t> &GetIssues() const { return m_Issues; }
	[[nodiscard]] bool GetListedInManifest() const;
	// The game's schema plus whatever this file uses: what Add offers and where defaults come from.
	[[nodiscard]] const xv::particles::CSchema &GetSchema() const { return m_Schema; }
	[[nodiscard]] CParticlePreview *GetPreview() const { return m_pPreview; }

	// Selection, by element index. selectElement takes an operator or child link of the current
	// system, or -1 for the system's own properties.
	void SelectSystem( std::int32_t nSystem );
	[[nodiscard]] std::int32_t GetCurrentSystem() const { return m_nSystem; }
	void SelectElement( std::int32_t nElement );
	[[nodiscard]] std::int32_t GetCurrentElement() const { return m_nElement; }

	// Editing, as the toolbar and menus do it. Each is one undo step.
	std::int32_t NewSystem( const QString &szName = {} );
	void DuplicateSystem();
	void DeleteSystem();
	void RenameSystem( std::int32_t nSystem, const QString &szName );
	std::int32_t AddOperator( xv::particles::Category_t nCategory, const QString &szFunction );
	std::int32_t AddChild( std::int32_t nChild );
	void RemoveSelected();
	void MoveSelected( int nDelta );
	bool SetValue( std::int32_t nElement, const QString &szAttribute, const xv::dmx::Value_t &value );
	void CopySystems();
	void PasteSystems();

signals:
	void AddToManifestRequested( CParticleDocument *pDocument );
	// Look for systems in every particle file (starting with the selected system's name).
	void SearchRequested( const QString &szQuery );

private:
	struct StackPosition_t
	{
		QString m_szList; // "emitters", ..., "children"; empty for the system's properties
		std::size_t m_nPosition = 0;
	};

	void ScheduleRefresh();
	void Refresh();
	void RebuildSystems();
	void RebuildStack();
	void RebuildProperties();
	void Relint();
	void ShowIssues();
	void RebuildSchema();
	void ShowInPreview();
	[[nodiscard]] std::optional<StackPosition_t> PositionOf( std::int32_t nElement ) const;
	void FillAddMenu();
	void ShowStackMenu( const QPoint &pos );
	void ShowPropertyMenu( const QPoint &pos );
	void ShowSystemMenu( QWidget *pView, const QPoint &pos );
	// Systems view as a grid of pictures: built when shown, pictures drawn a few at a time.
	void SetGridView( bool bGrid );
	void RebuildGrid();
	void DrawThumbnails();
	[[nodiscard]] std::vector<std::int32_t> GetSelectedSystems() const;
	[[nodiscard]] const xv::particles::AttributeInfo_t *DefaultFor( std::int32_t nElement, const std::string &szAttribute ) const;
	[[nodiscard]] QWidget *ValueEditor( std::int32_t nElement, const xv::dmx::Attribute_t &attribute );

	CParticleDocument *m_pDocument;
	ParticleGame_t m_Game;
	xv::particles::CSchema m_Schema;
	std::vector<xv::particles::Issue_t> m_Issues;

	QLineEdit *m_pFilter = nullptr;
	QTreeWidget *m_pSystems = nullptr;
	QListWidget *m_pGrid = nullptr;
	QToolButton *m_pGridToggle = nullptr;
	QTimer *m_pThumbnailTimer = nullptr;
	std::map<std::int32_t, QIcon> m_Thumbnails;   // by system element
	std::vector<std::int32_t> m_ThumbnailQueue;   // systems whose picture is missing or out of date
	QTreeWidget *m_pStack = nullptr;
	QTreeWidget *m_pProperties = nullptr;
	QLabel *m_pPropertiesTitle = nullptr;
	QListWidget *m_pIssueList = nullptr;
	QLabel *m_pSummary = nullptr;
	QToolButton *m_pAdd = nullptr;
	CParticlePreview *m_pPreview = nullptr;
	QAction *m_pAddToManifest = nullptr;

	std::int32_t m_nSystem = -1;
	std::int32_t m_nElement = -1;
	QString m_szList; // the stack list selected when m_nElement is -1 ("emitters"...), empty for the properties
	bool m_bRefreshQueued = false;
};
