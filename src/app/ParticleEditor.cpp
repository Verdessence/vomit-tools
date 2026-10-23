//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See ParticleEditor.h.
//
//===========================================================================//
#include "ParticleEditor.h"

#include <algorithm>
#include <climits>
#include <functional>
#include <memory>
#include <set>
#include <unordered_map>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPointer>
#include <QPainter>
#include <QRegularExpressionValidator>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStringListModel>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "ParticleDocument.h"
#include "InterfaceMode.h"
#include "MenuPicker.h"
#include "ParticlePreview.h"
#include "QtPaths.h"
#include "particles/Names.h"
#include "particles/Simulation.h"
#include "util/Numbers.h"
#include "vfs/Vfs.h"

namespace particles = xv::particles;
using particles::Category_t;
using xv::dmx::Attribute_t;
using xv::dmx::AttrType_t;
using xv::dmx::CElement;
using xv::dmx::CElementRef;
using xv::dmx::Document_t;
using Severity_t = particles::Issue_t::Severity_t;

namespace
{

constexpr auto CLIPBOARD_TYPE = "application/x-vomit-tools-particles";
constexpr auto GRID_VIEW_KEY = "particleGridView";
constexpr int THUMBNAIL_SIZE = 96;
constexpr qint64 THUMBNAIL_BUDGET_MS = 30; // drawing pictures per turn of the event loop

enum Role_t
{
	ROLE_ELEMENT_ROLE = Qt::UserRole,     // element index, -1 for headers and the properties item
	ROLE_LIST_ROLE,                       // "emitters", ..., "children"; empty for the properties item
	ROLE_SYSTEM_ROLE,                     // in the systems tree: the system's element index
	ROLE_ATTRIBUTE_ROLE,                  // in the properties tree: the attribute name
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Qs( std::string_view szText )
{
	return QString::fromUtf8( szText.data(), static_cast<qsizetype>( szText.size() ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CategoryTitle( Category_t nCategory )
{
	switch ( nCategory )
	{
		case Category_t::CATEGORY_EMITTERS: return QObject::tr( "Emitters" );
		case Category_t::CATEGORY_INITIALIZERS: return QObject::tr( "Initializers" );
		case Category_t::CATEGORY_OPERATORS: return QObject::tr( "Operators" );
		case Category_t::CATEGORY_RENDERERS: return QObject::tr( "Renderers" );
		case Category_t::CATEGORY_FORCES: return QObject::tr( "Forces" );
		case Category_t::CATEGORY_CONSTRAINTS: return QObject::tr( "Constraints" );
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CategorySingular( Category_t nCategory )
{
	switch ( nCategory )
	{
		case Category_t::CATEGORY_EMITTERS: return QObject::tr( "Emitter" );
		case Category_t::CATEGORY_INITIALIZERS: return QObject::tr( "Initializer" );
		case Category_t::CATEGORY_OPERATORS: return QObject::tr( "Operator" );
		case Category_t::CATEGORY_RENDERERS: return QObject::tr( "Renderer" );
		case Category_t::CATEGORY_FORCES: return QObject::tr( "Force" );
		case Category_t::CATEGORY_CONSTRAINTS: return QObject::tr( "Constraint" );
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString ListTitle( const QString &szList )
{
	if ( szList == Qs( particles::CHILDREN ) )
	{
		return QObject::tr( "Child systems" );
	}
	const auto category = particles::CategoryOf( szList.toStdString() );
	return category ? CategoryTitle( *category ) : szList;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsSimple()
{
	return CInterfaceMode::Instance().IsSimple();
}

//-----------------------------------------------------------------------------
// Purpose: A function as the interface shows it: its plain name in the simple interface (when it
//          has one), else its own; the tooltip gives the other name and what it does.
//-----------------------------------------------------------------------------
std::pair<QString, QString> FunctionLabel( Category_t nCategory, const QString &szFunction )
{
	const particles::PlainName_t plain = particles::PlainFunction( nCategory, szFunction.toStdString() );
	QString szTip;
	if ( plain.m_szName.empty() )
	{
		return { szFunction, szTip };
	}
	if ( IsSimple() )
	{
		szTip = szFunction + u'\n' + Qs( plain.m_szMeaning );
		return { Qs( plain.m_szName ), szTip };
	}
	szTip = Qs( plain.m_szName ) + QStringLiteral( ": " ) + Qs( plain.m_szMeaning );
	return { szFunction, szTip };
}

// The order the selected system's lists are shown and offered in: what it looks like first, then
// how particles change, start and are made, then what spawns with them and what pushes them.
constexpr std::array<std::string_view, 7> STACK_ORDER{
	"renderers",
	"operators",
	"initializers",
	"emitters",
	"children",
	"forces",
	"constraints",
};

//-----------------------------------------------------------------------------
// Purpose: What a new system starts with when the game has these functions: particles that appear,
//          drift
//          and fade.
//-----------------------------------------------------------------------------
const std::vector<std::pair<Category_t, std::string_view>> &StarterOperators()
{
	static const std::vector<std::pair<Category_t, std::string_view>> starter{
		{ Category_t::CATEGORY_EMITTERS, "emit_continuously" },
		{ Category_t::CATEGORY_INITIALIZERS, "Position Within Sphere Random" },
		{ Category_t::CATEGORY_INITIALIZERS, "Lifetime Random" },
		{ Category_t::CATEGORY_INITIALIZERS, "Radius Random" },
		{ Category_t::CATEGORY_INITIALIZERS, "Color Random" },
		{ Category_t::CATEGORY_INITIALIZERS, "Alpha Random" },
		{ Category_t::CATEGORY_OPERATORS, "Movement Basic" },
		{ Category_t::CATEGORY_OPERATORS, "Lifespan Decay" },
		{ Category_t::CATEGORY_OPERATORS, "Alpha Fade Out Random" },
		{ Category_t::CATEGORY_RENDERERS, "render_animated_sprites" },
	};
	return starter;
}

// System properties by what they're about; anything else goes under "Other".
struct PropertyGroup_t
{
	const char *m_pszTitle;
	std::vector<std::string_view> m_Names;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const std::vector<PropertyGroup_t> &SystemGroups()
{
	static const std::vector<PropertyGroup_t> groups{
		{ QT_TRANSLATE_NOOP( "ParticleEditor", "Particles" ), { "max_particles", "initial_particles", "material", "color", "radius", "rotation", "rotation_speed", "sequence_number", "sequence_number 1" } },
		{ QT_TRANSLATE_NOOP( "ParticleEditor", "Bounds and culling" ), { "bounding_box_min", "bounding_box_max", "bounding_box_control_point", "cull_radius", "cull_cost", "cull_control_point", "cull_replacement_definition", "maximum draw distance", "control point to disable rendering if it is the camera" } },
		{ QT_TRANSLATE_NOOP( "ParticleEditor", "Simulation" ), { "maximum time step", "maximum sim tick rate", "minimum sim tick rate", "minimum rendered frames", "time to sleep when not drawn", "Sort particles", "batch particle systems", "view model effect", "group id", "preventNameBasedLookup" } },
	};
	return groups;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QIcon SeverityIcon( const QStyle *pStyle, Severity_t nSeverity )
{
	return pStyle->standardIcon( nSeverity == Severity_t::SEVERITY_ERROR ? QStyle::SP_MessageBoxCritical
			: nSeverity == Severity_t::SEVERITY_WARNING                  ? QStyle::SP_MessageBoxWarning
																		 : QStyle::SP_MessageBoxInformation );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QIcon Swatch( const xv::dmx::CColor &color )
{
	QPixmap pixmap( 16, 16 );
	pixmap.fill( Qt::transparent );
	QPainter painter( &pixmap );
	painter.fillRect( 0, 0, 8, 8, Qt::lightGray );
	painter.fillRect( 8, 8, 8, 8, Qt::lightGray );
	painter.fillRect( 8, 0, 8, 8, Qt::white );
	painter.fillRect( 0, 8, 8, 8, Qt::white );
	painter.fillRect( pixmap.rect(), QColor( color.m_nR, color.m_nG, color.m_nB, color.m_nA ) );
	painter.setPen( Qt::darkGray );
	painter.drawRect( pixmap.rect().adjusted( 0, 0, -1, -1 ) );
	return QIcon{ pixmap };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const QRegularExpression &FloatPattern()
{
	static const QRegularExpression pattern{ QStringLiteral( R"(^\s*[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?\s*$)" ) };
	return pattern;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<float> ParseFloat( const QString &szText )
{
	return xv::ParseNumber<float>( szText.trimmed().toStdString() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QLineEdit *FloatField( float flValue )
{
	auto *pField = new QLineEdit( Qs( xv::dmx::ValueText( xv::dmx::Value_t{ flValue } ) ) );
	pField->setValidator( new QRegularExpressionValidator( FloatPattern(), pField ) );
	return pField;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
template <std::size_t N>
std::span<float, N> Components( xv::dmx::Value_t &value )
{
	if constexpr ( N == 2 )
	{
		return std::get<xv::dmx::Vec2_t>( value );
	}
	else if constexpr ( N == 3 )
	{
		return std::get<xv::dmx::Vec3_t>( value );
	}
	else
	{
		return std::get<xv::dmx::Vec4_t>( value );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::size_t ComponentCount( AttrType_t nType )
{
	switch ( nType )
	{
		case AttrType_t::ATTR_TYPE_VECTOR2: return 2;
		case AttrType_t::ATTR_TYPE_VECTOR3:
		case AttrType_t::ATTR_TYPE_QANGLE: return 3;
		case AttrType_t::ATTR_TYPE_VECTOR4:
		case AttrType_t::ATTR_TYPE_QUATERNION: return 4;
		default: return 0;
	}
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Takes ownership of document.
//-----------------------------------------------------------------------------
CParticleEditor::CParticleEditor( CParticleDocument *pDocument, QWidget *pParent )
	: QWidget( pParent ),
	  m_pDocument( pDocument )
{
	m_pDocument->setParent( this );

	auto *pToolbar = new QToolBar;
	pToolbar->setToolButtonStyle( Qt::ToolButtonTextOnly );
	pToolbar->addAction( tr( "New System" ), this, [ this ] { NewSystem(); } )->setToolTip( tr( "Adds a system with a basic set of operators" ) );
	pToolbar->addAction( tr( "Duplicate" ), this, &CParticleEditor::DuplicateSystem )->setToolTip( tr( "Copies the selected system, operators and all" ) );
	pToolbar->addAction( tr( "Delete" ), this, &CParticleEditor::DeleteSystem )->setToolTip( tr( "Deletes the selected system and the child links to it" ) );
	pToolbar->addSeparator();
	pToolbar->addAction( tr( "Copy" ), this, &CParticleEditor::CopySystems )->setToolTip( tr( "Copies the selected systems, with the systems they spawn, for pasting into any particle file" ) );
	pToolbar->addAction( tr( "Paste" ), this, &CParticleEditor::PasteSystems )->setToolTip( tr( "Adds copied systems to this file" ) );
	pToolbar->addSeparator();
	pToolbar->addAction( tr( "Search All Files" ), this, [ this ] {
				emit SearchRequested( m_nSystem >= 0 ? Qs( m_pDocument->GetDocument().m_Elements[ static_cast<std::size_t>( m_nSystem ) ].m_szName ) : QString{} );
			} )
		->setToolTip( tr( "Finds systems in every particle file of the game by name, material or operator (Ctrl+Shift+F)" ) );
	pToolbar->addSeparator();
	m_pAddToManifest = pToolbar->addAction( tr( "Add to Manifest" ), this, [ this ] { emit AddToManifestRequested( m_pDocument ); } );
	m_pAddToManifest->setToolTip( tr( "Lists this file in particles/particles_manifest.txt so the game loads it" ) );
	QAction *pMapManifest = pToolbar->addAction( tr( "Add to a Map's Manifest…" ), this, [ this ] { emit AddToMapManifestRequested( m_pDocument ); } );
	pMapManifest->setObjectName( QStringLiteral( "particleMapManifest" ) );
	pMapManifest->setToolTip( tr( "Lists this file in maps/<map>_particles.txt, so the game loads it with that map only" ) );
	m_pSummary = new QLabel;
	m_pSummary->setContentsMargins( 12, 0, 6, 0 );
	pToolbar->addWidget( m_pSummary );

	// Systems
	m_pFilter = new QLineEdit;
	m_pFilter->setObjectName( QStringLiteral( "particleFilter" ) );
	m_pFilter->setPlaceholderText( tr( "Filter systems…" ) );
	m_pFilter->setClearButtonEnabled( true );
	connect( m_pFilter, &QLineEdit::textChanged, this, [ this ] {
		RebuildSystems();
		RebuildGrid();
	} );
	m_pSystems = new QTreeWidget;
	m_pSystems->setObjectName( QStringLiteral( "particleSystems" ) );
	m_pSystems->setHeaderHidden( true );
	m_pSystems->setSelectionMode( QAbstractItemView::ExtendedSelection );
	m_pSystems->setEditTriggers( QAbstractItemView::EditKeyPressed | QAbstractItemView::DoubleClicked );
	m_pSystems->setContextMenuPolicy( Qt::CustomContextMenu );
	m_pSystems->setToolTip( tr( "The file's systems; child systems sit under the systems that spawn them. Double-click or F2 renames" ) );
	connect( m_pSystems, &QTreeWidget::customContextMenuRequested, this, [ this ]( const QPoint &pos ) { ShowSystemMenu( m_pSystems, pos ); } );
	connect( m_pSystems, &QTreeWidget::currentItemChanged, this, [ this ]( QTreeWidgetItem *pItem ) {
		if ( pItem && pItem->data( 0, ROLE_SYSTEM_ROLE ).toInt() != m_nSystem )
		{
			m_nSystem = pItem->data( 0, ROLE_SYSTEM_ROLE ).toInt();
			m_nElement = -1;
			m_szList.clear();
			RebuildStack();
			RebuildProperties();
			ShowInPreview();
		}
	} );
	connect( m_pSystems, &QTreeWidget::itemChanged, this, [ this ]( QTreeWidgetItem *pItem ) {
		const std::int32_t nSystem = pItem->data( 0, ROLE_SYSTEM_ROLE ).toInt();
		const QString szName = pItem->text( 0 ).trimmed();
		if ( szName.isEmpty() || szName.toStdString() == m_pDocument->GetDocument().m_Elements[ static_cast<std::size_t>( nSystem ) ].m_szName )
		{
			ScheduleRefresh(); // put the old name back
			return;
		}
		RenameSystem( nSystem, szName );
	} );
	auto *pLeft = new QWidget;
	m_pGrid = new QListWidget;
	m_pGrid->setObjectName( QStringLiteral( "particleGrid" ) );
	m_pGrid->setViewMode( QListView::IconMode );
	m_pGrid->setIconSize( { THUMBNAIL_SIZE, THUMBNAIL_SIZE } );
	m_pGrid->setGridSize( { THUMBNAIL_SIZE + 24, THUMBNAIL_SIZE + 40 } );
	m_pGrid->setResizeMode( QListView::Adjust );
	m_pGrid->setMovement( QListView::Static );
	m_pGrid->setWordWrap( true );
	m_pGrid->setUniformItemSizes( true );
	m_pGrid->setSelectionMode( QAbstractItemView::ExtendedSelection );
	m_pGrid->setEditTriggers( QAbstractItemView::EditKeyPressed );
	m_pGrid->setContextMenuPolicy( Qt::CustomContextMenu );
	m_pGrid->hide();
	connect( m_pGrid, &QListWidget::customContextMenuRequested, this, [ this ]( const QPoint &pos ) { ShowSystemMenu( m_pGrid, pos ); } );
	connect( m_pGrid, &QListWidget::currentItemChanged, this, [ this ]( QListWidgetItem *pItem ) {
		if ( pItem && pItem->data( ROLE_SYSTEM_ROLE ).toInt() != m_nSystem )
		{
			SelectSystem( pItem->data( ROLE_SYSTEM_ROLE ).toInt() );
		}
	} );
	connect( m_pGrid, &QListWidget::itemChanged, this, [ this ]( QListWidgetItem *pItem ) {
		const std::int32_t nSystem = pItem->data( ROLE_SYSTEM_ROLE ).toInt();
		const QString szName = pItem->text().trimmed();
		if ( !szName.isEmpty() && szName.toStdString() != m_pDocument->GetDocument().m_Elements[ static_cast<std::size_t>( nSystem ) ].m_szName )
		{
			RenameSystem( nSystem, szName );
		}
		else
		{
			ScheduleRefresh();
		}
	} );
	m_pThumbnailTimer = new QTimer( this );
	m_pThumbnailTimer->setInterval( 0 );
	connect( m_pThumbnailTimer, &QTimer::timeout, this, &CParticleEditor::DrawThumbnails );

	m_pGridToggle = new QToolButton;
	m_pGridToggle->setObjectName( QStringLiteral( "particleGridToggle" ) );
	m_pGridToggle->setText( tr( "Grid" ) );
	m_pGridToggle->setCheckable( true );
	m_pGridToggle->setToolTip( tr( "Show the systems as pictures" ) );
	connect( m_pGridToggle, &QToolButton::toggled, this, [ this ]( bool bGrid ) {
		SetGridView( bGrid );
		QSettings{}.setValue( GRID_VIEW_KEY, bGrid );
	} );
	auto *pFilterRow = new QHBoxLayout;
	pFilterRow->setContentsMargins( 0, 0, 0, 0 );
	pFilterRow->addWidget( m_pFilter, 1 );
	pFilterRow->addWidget( m_pGridToggle );
	auto *pLeftLayout = new QVBoxLayout( pLeft );
	pLeftLayout->setContentsMargins( 6, 6, 0, 6 );
	pLeftLayout->addLayout( pFilterRow );
	pLeftLayout->addWidget( m_pSystems );
	pLeftLayout->addWidget( m_pGrid );

	// The selected system's lists
	m_pStack = new QTreeWidget;
	m_pStack->setObjectName( QStringLiteral( "particleStack" ) );
	m_pStack->setHeaderHidden( true );
	m_pStack->setContextMenuPolicy( Qt::CustomContextMenu );
	connect( m_pStack, &QTreeWidget::customContextMenuRequested, this, &CParticleEditor::ShowStackMenu );
	connect( m_pStack, &QTreeWidget::currentItemChanged, this, [ this ]( QTreeWidgetItem *pItem ) {
		if ( !pItem )
		{
			return;
		}
		const std::int32_t nElement = pItem->data( 0, ROLE_ELEMENT_ROLE ).toInt();
		const QString szList = pItem->data( 0, ROLE_LIST_ROLE ).toString();
		if ( nElement != m_nElement || szList != m_szList )
		{
			m_nElement = nElement;
			m_szList = szList;
			RebuildProperties();
		}
	} );
	m_pAdd = new QToolButton;
	m_pAdd->setObjectName( QStringLiteral( "particleAdd" ) );
	m_pAdd->setText( tr( "Add" ) );
	m_pAdd->setToolTip( tr( "Adds an operator of any kind, or a child system" ) );
	m_pAdd->setPopupMode( QToolButton::InstantPopup );
	m_pAdd->setMenu( new QMenu( m_pAdd ) );
	connect( m_pAdd->menu(), &QMenu::aboutToShow, this, &CParticleEditor::FillAddMenu );
	auto *pRemove = new QToolButton;
	pRemove->setText( tr( "Remove" ) );
	connect( pRemove, &QToolButton::clicked, this, &CParticleEditor::RemoveSelected );
	auto *pUp = new QToolButton;
	pUp->setText( QStringLiteral( "▲" ) );
	pUp->setToolTip( tr( "Move up: operators run in list order" ) );
	connect( pUp, &QToolButton::clicked, this, [ this ] { MoveSelected( -1 ); } );
	auto *pDown = new QToolButton;
	pDown->setText( QStringLiteral( "▼" ) );
	pDown->setToolTip( tr( "Move down" ) );
	connect( pDown, &QToolButton::clicked, this, [ this ] { MoveSelected( 1 ); } );
	auto *pStackBar = new QHBoxLayout;
	pStackBar->addWidget( m_pAdd );
	pStackBar->addWidget( pRemove );
	pStackBar->addStretch();
	pStackBar->addWidget( pUp );
	pStackBar->addWidget( pDown );
	auto *pMiddle = new QWidget;
	auto *pMiddleLayout = new QVBoxLayout( pMiddle );
	pMiddleLayout->setContentsMargins( 0, 6, 0, 6 );
	pMiddleLayout->addWidget( m_pStack );
	pMiddleLayout->addLayout( pStackBar );

	// The selected item's properties
	m_pPropertiesTitle = new QLabel;
	m_pPropertiesTitle->setWordWrap( true );
	m_pProperties = new QTreeWidget;
	m_pProperties->setObjectName( QStringLiteral( "particleProperties" ) );
	m_pProperties->setColumnCount( 2 );
	m_pProperties->setHeaderLabels( { tr( "Property" ), tr( "Value" ) } );
	m_pProperties->header()->setSectionResizeMode( 0, QHeaderView::Interactive );
	m_pProperties->header()->setStretchLastSection( true );
	m_pProperties->header()->resizeSection( 0, 220 );
	m_pProperties->setSelectionMode( QAbstractItemView::NoSelection );
	m_pProperties->setFocusPolicy( Qt::NoFocus );
	m_pProperties->setContextMenuPolicy( Qt::CustomContextMenu );
	connect( m_pProperties, &QTreeWidget::customContextMenuRequested, this, &CParticleEditor::ShowPropertyMenu );
	auto *pPropertyPanel = new QWidget;
	auto *pPropertyLayout = new QVBoxLayout( pPropertyPanel );
	pPropertyLayout->setContentsMargins( 0, 0, 0, 0 );
	pPropertyLayout->addWidget( m_pPropertiesTitle );
	pPropertyLayout->addWidget( m_pProperties );
	m_pPreview = new CParticlePreview;
	m_pPreview->setObjectName( QStringLiteral( "particlePreview" ) );
	// Dragged control points: the file's, through Set Control Point Positions; the view's, as undo
	// steps that leave the file unmodified.
	m_pPreview->SetPlacer( [ this ]( int nIndex, xv::CVec3 vecLocation, std::int32_t nOperator, int nSlot ) {
		if ( m_nSystem < 0 )
		{
			return false;
		}
		std::int32_t nPlaced = -1;
		const bool bChanged = m_pDocument->Edit( tr( "Move CP%1" ).arg( nIndex ), [ & ]( Document_t &doc ) {
			nPlaced = particles::PlaceControlPoint( doc, m_nSystem, nIndex, { vecLocation.m_flX, vecLocation.m_flY, vecLocation.m_flZ }, nOperator, nSlot, m_Schema );
		} );
		if ( bChanged && nOperator < 0 )
		{
			SelectElement( nPlaced ); // show the operator that was added
		}
		return bChanged;
	} );
	connect( m_pPreview, &CParticlePreview::ControlPointsMoved, this,
		[ this ]( const CParticlePreview::ControlPoints_t &before, const CParticlePreview::ControlPoints_t &after, const QString &szDescription ) {
			const QPointer<CParticlePreview> pPreview = m_pPreview;
			m_pDocument->AddViewStep(
				szDescription,
				[ pPreview, before ] {
					if ( pPreview )
					{
						pPreview->SetControlPoints( before );
					}
				},
				[ pPreview, after ] {
					if ( pPreview )
					{
						pPreview->SetControlPoints( after );
					}
				} );
		} );
	auto *pRight = new QSplitter( Qt::Vertical );
	pRight->setContentsMargins( 0, 6, 6, 6 );
	pRight->addWidget( m_pPreview );
	pRight->addWidget( pPropertyPanel );
	pRight->setStretchFactor( 0, 5 );
	pRight->setStretchFactor( 1, 4 );

	auto *pColumns = new QSplitter;
	pColumns->addWidget( pLeft );
	pColumns->addWidget( pMiddle );
	pColumns->addWidget( pRight );
	pColumns->setStretchFactor( 0, 3 );
	pColumns->setStretchFactor( 1, 3 );
	pColumns->setStretchFactor( 2, 5 );

	m_pIssueList = new QListWidget;
	m_pIssueList->setObjectName( QStringLiteral( "particleIssues" ) );
	m_pIssueList->setWordWrap( true );
	connect( m_pIssueList, &QListWidget::itemActivated, this, [ this ]( QListWidgetItem *pItem ) {
		const int nRow = m_pIssueList->row( pItem );
		if ( nRow < 0 || nRow >= static_cast<int>( m_Issues.size() ) )
		{
			return;
		}
		const particles::Issue_t &issue = m_Issues[ static_cast<std::size_t>( nRow ) ];
		if ( issue.m_nSystem >= 0 )
		{
			m_pFilter->clear();
			SelectSystem( issue.m_nSystem );
			SelectElement( issue.m_nElement );
		}
	} );

	auto *pRows = new QSplitter( Qt::Vertical );
	pRows->addWidget( pColumns );
	pRows->addWidget( m_pIssueList );
	pRows->setStretchFactor( 0, 5 );
	pRows->setStretchFactor( 1, 1 );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->setSpacing( 0 );
	pLayout->addWidget( pToolbar );
	pLayout->addWidget( pRows, 1 );

	connect( m_pDocument, &CParticleDocument::Changed, this, &CParticleEditor::ScheduleRefresh );
	connect( &CInterfaceMode::Instance(), &CInterfaceMode::Changed, this, [ this ] {
		RebuildStack();
		RebuildProperties();
	} );
	SetGridView( QSettings{}.value( GRID_VIEW_KEY, false ).toBool() );
	RebuildSchema();
	const auto all = particles::Systems( m_pDocument->GetDocument() );
	m_nSystem = all.empty() ? -1 : all.front();
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::SetGame( ParticleGame_t game )
{
	m_Game = std::move( game );
	// Materials come from the game, else from the mod folder the file sits in (<mod>/particles/x.pcf).
	std::filesystem::path modFolder;
	if ( const auto &disk = m_pDocument->GetDiskPath(); !disk.empty() && disk.parent_path().filename() == "particles" )
	{
		modFolder = disk.parent_path().parent_path();
	}
	m_pPreview->SetReader( [ pfnRead = m_Game.m_pfnReadFile, modFolder ]( std::string_view szPath ) -> std::optional<std::vector<std::byte>> {
		if ( pfnRead )
		{
			if ( auto data = pfnRead( szPath ) )
			{
				return data;
			}
		}
		if ( modFolder.empty() )
		{
			return std::nullopt;
		}
		QFile file{ ToQString( xv::vfs::FindCaseInsensitive( modFolder / xv::vfs::FromUtf8( szPath ) ) ) };
		if ( !file.open( QIODevice::ReadOnly ) )
		{
			return std::nullopt;
		}
		const QByteArray bytes = file.readAll();
		const auto *pBegin = reinterpret_cast<const std::byte *>( bytes.constData() );
		return std::vector<std::byte>( pBegin, pBegin + bytes.size() );
	} );
	m_pPreview->SetModelList( m_Game.m_Models );
	RebuildSchema();
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::ShowInPreview()
{
	if ( m_nSystem >= 0 )
	{
		m_pPreview->SetSystem( m_pDocument->GetDocument().m_Elements[ static_cast<std::size_t>( m_nSystem ) ].m_szName );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::RebuildSchema()
{
	m_Schema = m_Game.m_pSchema ? *m_Game.m_pSchema : particles::CSchema{};
	m_Schema.Add( m_pDocument->GetDocument() );
	m_Schema.Finish();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CParticleEditor::GetListedInManifest() const
{
	if ( !m_Game.m_Manifest )
	{
		return true;
	}
	const std::string szPath = xv::vfs::NormalizePath( m_pDocument->GetGamePath().toStdString() );
	return std::ranges::find( *m_Game.m_Manifest, szPath ) != m_Game.m_Manifest->end();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::ScheduleRefresh()
{
	if ( !m_bRefreshQueued )
	{
		m_bRefreshQueued = true;
		QTimer::singleShot( 0, this, &CParticleEditor::Refresh );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::Refresh()
{
	m_bRefreshQueued = false;
	const Document_t &doc = m_pDocument->GetDocument();
	const auto all = particles::Systems( doc );
	if ( std::ranges::find( all, m_nSystem ) == all.end() )
	{
		m_nSystem = all.empty() ? -1 : all.front();
		m_nElement = -1;
	}
	if ( m_nElement >= 0 && !PositionOf( m_nElement ) )
	{
		m_nElement = -1;
	}
	std::size_t operators = 0;
	for ( std::int32_t nSystem : all )
	{
		for ( Category_t nCategory : particles::CATEGORIES )
		{
			operators += particles::References( doc.m_Elements[ static_cast<std::size_t>( nSystem ) ], particles::AttributeName( nCategory ) ).size();
		}
	}
	m_pSummary->setText( tr( "binary %1 · %2 %3 · %n system(s)", nullptr, static_cast<int>( all.size() ) )
							 .arg( doc.m_nEncodingVersion )
							 .arg( Qs( doc.m_szFormat ) )
							 .arg( doc.m_nFormatVersion ) +
		tr( " · %n operator(s)", nullptr, static_cast<int>( operators ) ) );
	m_pAddToManifest->setVisible( m_Game.m_Manifest && !GetListedInManifest() );
	Relint();
	RebuildSystems();
	RebuildStack();
	RebuildProperties();
	m_pPreview->SetDocument( doc );
	ShowInPreview();
	// The edited system's picture, and those of the systems that spawn it, are out of date.
	if ( m_nSystem >= 0 )
	{
		const auto stale = [ this ]( std::int32_t nSystem ) {
			if ( std::ranges::find( m_ThumbnailQueue, nSystem ) == m_ThumbnailQueue.end() )
			{
				m_ThumbnailQueue.push_back( nSystem );
			}
		};
		stale( m_nSystem );
		for ( std::int32_t nOther : all )
		{
			for ( const auto &link : particles::Children( doc, nOther ) )
			{
				if ( link.m_nSystem == m_nSystem )
				{
					stale( nOther );
				}
			}
		}
	}
	RebuildGrid();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::Relint()
{
	particles::LintContext_t context;
	context.m_pGame = m_Game.m_pSchema.get();
	context.m_szPath = xv::vfs::NormalizePath( m_pDocument->GetGamePath().toStdString() );
	context.m_pfnFileExists = m_Game.m_pfnFileExists;
	if ( m_Game.m_Manifest )
	{
		context.m_ListedInManifest = GetListedInManifest();
	}
	m_Issues = particles::Lint( m_pDocument->GetDocument(), context );
	ShowIssues();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::ShowIssues()
{
	m_pIssueList->clear();
	for ( const particles::Issue_t &issue : m_Issues )
	{
		auto *pItem = new QListWidgetItem( SeverityIcon( style(), issue.m_nSeverity ), Qs( issue.m_szMessage ) );
		if ( issue.m_nSystem >= 0 )
		{
			pItem->setToolTip( tr( "Double-click to show it" ) );
		}
		m_pIssueList->addItem( pItem );
	}
	if ( m_Issues.empty() )
	{
		m_pIssueList->addItem( new QListWidgetItem( style()->standardIcon( QStyle::SP_DialogApplyButton ), tr( "No problems found." ) ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::RebuildSystems()
{
	const Document_t &doc = m_pDocument->GetDocument();
	const auto all = particles::Systems( doc );
	// The worst issue of each system, for its icon.
	std::unordered_map<std::int32_t, Severity_t> worst;
	for ( const particles::Issue_t &issue : m_Issues )
	{
		if ( issue.m_nSystem >= 0 && ( !worst.contains( issue.m_nSystem ) || issue.m_nSeverity > worst[ issue.m_nSystem ] ) )
		{
			worst[ issue.m_nSystem ] = issue.m_nSeverity;
		}
	}
	const QSignalBlocker blocker{ m_pSystems };
	m_pSystems->clear();
	QTreeWidgetItem *pCurrent = nullptr;
	const auto makeItem = [ & ]( std::int32_t nSystem ) {
		auto *pItem = new QTreeWidgetItem;
		const CElement &element = doc.m_Elements[ static_cast<std::size_t>( nSystem ) ];
		pItem->setText( 0, Qs( element.m_szName ) );
		pItem->setData( 0, ROLE_SYSTEM_ROLE, nSystem );
		pItem->setFlags( pItem->flags() | Qt::ItemIsEditable );
		if ( const auto it = worst.find( nSystem ); it != worst.end() && it->second != Severity_t::SEVERITY_INFO )
		{
			pItem->setIcon( 0, SeverityIcon( style(), it->second ) );
		}
		if ( nSystem == m_nSystem && !pCurrent )
		{
			pCurrent = pItem;
		}
		return pItem;
	};

	const QString szFilter = m_pFilter->text().trimmed();
	if ( !szFilter.isEmpty() )
	{
		for ( std::int32_t nSystem : all )
		{
			if ( Qs( doc.m_Elements[ static_cast<std::size_t>( nSystem ) ].m_szName ).contains( szFilter, Qt::CaseInsensitive ) )
			{
				m_pSystems->addTopLevelItem( makeItem( nSystem ) );
			}
		}
	}
	else
	{
		// Systems nobody spawns at the top, the ones they spawn underneath.
		std::set<std::int32_t> spawned;
		for ( std::int32_t nSystem : all )
		{
			for ( const auto &link : particles::Children( doc, nSystem ) )
			{
				if ( link.m_nSystem >= 0 && link.m_nSystem != nSystem )
				{
					spawned.insert( link.m_nSystem );
				}
			}
		}
		std::set<std::int32_t> placed;
		std::function<void( QTreeWidgetItem *, std::int32_t, std::set<std::int32_t> & )> pfnAddChildren;
		pfnAddChildren = [ & ]( QTreeWidgetItem *pParent, std::int32_t nSystem, std::set<std::int32_t> &path ) {
			for ( const auto &link : particles::Children( doc, nSystem ) )
			{
				if ( link.m_nSystem < 0 || path.contains( link.m_nSystem ) )
				{
					continue;
				}
				QTreeWidgetItem *pItem = makeItem( link.m_nSystem );
				pParent->addChild( pItem );
				placed.insert( link.m_nSystem );
				path.insert( link.m_nSystem );
				pfnAddChildren( pItem, link.m_nSystem, path );
				path.erase( link.m_nSystem );
			}
		};
		for ( std::int32_t nSystem : all )
		{
			if ( !spawned.contains( nSystem ) )
			{
				QTreeWidgetItem *pItem = makeItem( nSystem );
				m_pSystems->addTopLevelItem( pItem );
				placed.insert( nSystem );
				std::set<std::int32_t> path{ nSystem };
				pfnAddChildren( pItem, nSystem, path );
			}
		}
		// Systems that only spawn each other in a loop have no top; show them anyway.
		for ( std::int32_t nSystem : all )
		{
			if ( !placed.contains( nSystem ) )
			{
				m_pSystems->addTopLevelItem( makeItem( nSystem ) );
			}
		}
		m_pSystems->expandToDepth( 0 );
	}
	if ( pCurrent )
	{
		m_pSystems->setCurrentItem( pCurrent );
		m_pSystems->scrollToItem( pCurrent );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::RebuildStack()
{
	const QSignalBlocker blocker{ m_pStack };
	m_pStack->clear();
	const Document_t &doc = m_pDocument->GetDocument();
	if ( m_nSystem < 0 )
	{
		return;
	}
	const CElement &system = doc.m_Elements[ static_cast<std::size_t>( m_nSystem ) ];
	std::unordered_map<std::int32_t, Severity_t> worst;
	for ( const particles::Issue_t &issue : m_Issues )
	{
		if ( issue.m_nElement >= 0 && ( !worst.contains( issue.m_nElement ) || issue.m_nSeverity > worst[ issue.m_nElement ] ) )
		{
			worst[ issue.m_nElement ] = issue.m_nSeverity;
		}
	}
	QTreeWidgetItem *pCurrent = nullptr;
	QFont bold = m_pStack->font();
	bold.setBold( true );

	auto *pProperties = new QTreeWidgetItem( { tr( "System properties" ) } );
	pProperties->setData( 0, ROLE_ELEMENT_ROLE, -1 );
	pProperties->setData( 0, ROLE_LIST_ROLE, QString{} );
	pProperties->setFont( 0, bold );
	m_pStack->addTopLevelItem( pProperties );
	if ( m_nElement < 0 && m_szList.isEmpty() )
	{
		pCurrent = pProperties;
	}

	const auto addList = [ & ]( const QString &szList, const std::vector<QString> &labels, const std::vector<std::int32_t> &elements ) {
		const QString szTitle = IsSimple() ? Qs( particles::PlainListTitle( szList.toStdString() ) ) : ListTitle( szList );
		auto *pHeader = new QTreeWidgetItem( { QStringLiteral( "%1 (%2)" ).arg( szTitle ).arg( elements.size() ) } );
		pHeader->setData( 0, ROLE_ELEMENT_ROLE, -1 );
		pHeader->setData( 0, ROLE_LIST_ROLE, szList );
		pHeader->setFont( 0, bold );
		m_pStack->addTopLevelItem( pHeader );
		if ( m_nElement < 0 && m_szList == szList )
		{
			pCurrent = pHeader;
		}
		const auto category = particles::CategoryOf( szList.toStdString() );
		for ( std::size_t i = 0; i < elements.size(); i++ )
		{
			// Operators by plain or real name; their function name decides the rest.
			const auto [ szLabel, szAbout ] = category ? FunctionLabel( *category, labels[ i ] ) : std::pair{ labels[ i ], QString{} };
			auto *pItem = new QTreeWidgetItem( pHeader, { szLabel } );
			pItem->setData( 0, ROLE_ELEMENT_ROLE, elements[ i ] );
			pItem->setData( 0, ROLE_LIST_ROLE, szList );
			QStringList tips;
			if ( !szAbout.isEmpty() )
			{
				tips << szAbout;
			}
			if ( category && !particles::IsSimulated( *category, labels[ i ].toStdString() ) )
			{
				QFont italic = pItem->font( 0 );
				italic.setItalic( true );
				pItem->setFont( 0, italic );
				pItem->setForeground( 0, palette().color( QPalette::PlaceholderText ) );
				const std::string_view szWhy = particles::PreviewNote( *category, labels[ i ].toStdString() );
				tips << ( szWhy.empty() ? tr( "Not in the preview yet: it plays without this one" ) : tr( "Not in the preview: %1. It plays without this one" ).arg( Qs( szWhy ) ) );
			}
			else if ( const std::string_view szNote = category ? particles::PreviewNote( *category, labels[ i ].toStdString() ) : std::string_view{}; !szNote.empty() )
			{
				tips << tr( "In the preview: %1" ).arg( Qs( szNote ) );
			}
			pItem->setToolTip( 0, tips.join( u'\n' ) );
			if ( const auto it = worst.find( elements[ i ] ); it != worst.end() )
			{
				pItem->setIcon( 0, SeverityIcon( style(), it->second ) );
			}
			if ( elements[ i ] == m_nElement )
			{
				pCurrent = pItem;
			}
		}
		pHeader->setExpanded( true );
	};
	for ( std::string_view szList : STACK_ORDER )
	{
		std::vector<QString> labels;
		std::vector<std::int32_t> elements;
		if ( szList == particles::CHILDREN )
		{
			for ( const auto &link : particles::Children( doc, m_nSystem ) )
			{
				const QString szName = link.m_nSystem >= 0 ? Qs( doc.m_Elements[ static_cast<std::size_t>( link.m_nSystem ) ].m_szName ) : tr( "(missing)" );
				labels.push_back( link.m_flDelay != 0.0f ? tr( "%1 · after %2 s" ).arg( szName, Qs( xv::dmx::ValueText( xv::dmx::Value_t{ link.m_flDelay } ) ) ) : szName );
				elements.push_back( link.m_nLink );
			}
		}
		else
		{
			for ( std::int32_t nOp : particles::References( system, szList ) )
			{
				const QString szFunction = Qs( particles::FunctionName( doc.m_Elements[ static_cast<std::size_t>( nOp ) ] ) );
				labels.push_back( szFunction.isEmpty() ? tr( "(no function)" ) : szFunction );
				elements.push_back( nOp );
			}
		}
		addList( Qs( szList ), labels, elements );
	}

	if ( pCurrent )
	{
		m_pStack->setCurrentItem( pCurrent );
		m_pStack->scrollToItem( pCurrent );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<CParticleEditor::StackPosition_t> CParticleEditor::PositionOf( std::int32_t nElement ) const
{
	if ( m_nSystem < 0 )
	{
		return std::nullopt;
	}
	const CElement &system = m_pDocument->GetDocument().m_Elements[ static_cast<std::size_t>( m_nSystem ) ];
	const auto find = [ & ]( std::string_view szList ) -> std::optional<StackPosition_t> {
		const auto refs = particles::References( system, szList );
		const auto it = std::ranges::find( refs, nElement );
		if ( it == refs.end() )
		{
			return std::nullopt;
		}
		// Position in the attribute's own list, which may hold null references too.
		const auto &raw = std::get<std::vector<CElementRef>>( system.Find( szList )->m_Value );
		const auto at = std::ranges::find( raw, nElement, &CElementRef::m_nIndex );
		return StackPosition_t{ Qs( szList ), static_cast<std::size_t>( at - raw.begin() ) };
	};
	for ( Category_t nCategory : particles::CATEGORIES )
	{
		if ( auto found = find( particles::AttributeName( nCategory ) ) )
		{
			return found;
		}
	}
	return find( particles::CHILDREN );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const particles::AttributeInfo_t *CParticleEditor::DefaultFor( std::int32_t nElement, const std::string &szAttribute ) const
{
	const CElement &target = m_pDocument->GetDocument().m_Elements[ static_cast<std::size_t>( nElement ) ];
	if ( target.m_szType == particles::SYSTEM_TYPE )
	{
		return m_Schema.SystemAttribute( szAttribute );
	}
	if ( target.m_szType == particles::OPERATOR_TYPE )
	{
		const auto position = PositionOf( nElement );
		const auto category = position ? particles::CategoryOf( position->m_szList.toStdString() ) : std::nullopt;
		const particles::CFunctionInfo *pFunction = category ? m_Schema.Function( *category, particles::FunctionName( target ) ) : nullptr;
		return pFunction ? pFunction->Attribute( szAttribute ) : nullptr;
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::RebuildProperties()
{
	m_pPreview->SetSelectedElement( m_nElement );
	// Keep the keyboard where it was across the rebuild (Tab from one field to the next commits the
	// first, which rebuilds the panel).
	QString szFocusedAttribute;
	int nFocusedField = 0;
	if ( QWidget *pFocus = QApplication::focusWidget(); pFocus && m_pProperties->isAncestorOf( pFocus ) )
	{
		for ( QTreeWidgetItemIterator it{ m_pProperties }; *it; ++it )
		{
			QWidget *pEditor = m_pProperties->itemWidget( *it, 1 );
			if ( pEditor && ( pEditor == pFocus || pEditor->isAncestorOf( pFocus ) ) )
			{
				szFocusedAttribute = ( *it )->data( 0, ROLE_ATTRIBUTE_ROLE ).toString();
				const auto fields = pEditor->findChildren<QLineEdit *>();
				nFocusedField = std::max<int>( 0, static_cast<int>( fields.indexOf( qobject_cast<QLineEdit *>( pFocus ) ) ) );
				break;
			}
		}
	}

	m_pProperties->clear();
	const Document_t &doc = m_pDocument->GetDocument();
	if ( m_nSystem < 0 )
	{
		m_pPropertiesTitle->setText( tr( "This file has no systems yet. Use <b>New System</b> or paste one." ) );
		return;
	}
	const std::int32_t nTarget = m_nElement >= 0 ? m_nElement : m_nSystem;
	const CElement &element = doc.m_Elements[ static_cast<std::size_t>( nTarget ) ];
	QFont bold = m_pProperties->font();
	bold.setBold( true );

	// The function the shown settings belong to, for their plain names; empty for a system.
	const std::string szOwner = element.m_szType == particles::OPERATOR_TYPE ? std::string{ particles::FunctionName( element ) } : std::string{};
	const bool bSimple = IsSimple();
	const auto addRow = [ & ]( QTreeWidgetItem *pParent, const Attribute_t &attribute ) -> QTreeWidgetItem * {
		const particles::AttributeInfo_t *pFallback = DefaultFor( nTarget, attribute.m_szName );
		const bool bChanged = pFallback && pFallback->m_nType == attribute.m_nType && !xv::dmx::SameValue( pFallback->m_Value, attribute.m_Value );
		// The simple interface leaves advanced settings out while they have their usual value.
		if ( bSimple && !bChanged && particles::IsAdvancedSetting( szOwner, attribute.m_szName ) )
		{
			return nullptr;
		}
		const particles::PlainName_t plain = particles::PlainSetting( szOwner, attribute.m_szName );
		auto *pItem = new QTreeWidgetItem( pParent, { bSimple ? Qs( plain.m_szName ) : Qs( attribute.m_szName ) } );
		pItem->setData( 0, ROLE_ATTRIBUTE_ROLE, Qs( attribute.m_szName ) );
		QString szTip = tr( "%1 (%2)" ).arg( Qs( attribute.m_szName ), Qs( xv::dmx::TypeName( attribute.m_nType ) ) );
		if ( !bSimple && !plain.m_szName.empty() && plain.m_szName != attribute.m_szName )
		{
			szTip += u'\n' + Qs( plain.m_szName ) + ( plain.m_szMeaning.empty() ? QString{} : QStringLiteral( ": " ) + Qs( plain.m_szMeaning ) );
		}
		else if ( !plain.m_szMeaning.empty() )
		{
			szTip += u'\n' + Qs( plain.m_szMeaning );
		}
		if ( pFallback && pFallback->m_nType == attribute.m_nType )
		{
			szTip += u'\n' + tr( "Most used in the game's files: %1" ).arg( Qs( xv::dmx::ValueText( pFallback->m_Value ) ) );
			if ( bChanged )
			{
				pItem->setFont( 0, bold ); // changed from the usual value
			}
		}
		pItem->setToolTip( 0, szTip );
		m_pProperties->setItemWidget( pItem, 1, ValueEditor( nTarget, attribute ) );
		return pItem;
	};
	const auto addGroup = [ & ]( const QString &szTitle, bool bExpanded ) {
		auto *pGroup = new QTreeWidgetItem( m_pProperties, { szTitle } );
		pGroup->setFirstColumnSpanned( true );
		pGroup->setFont( 0, bold );
		pGroup->setFlags( Qt::ItemIsEnabled );
		pGroup->setExpanded( bExpanded );
		return pGroup;
	};

	if ( element.m_szType == particles::SYSTEM_TYPE )
	{
		m_pPropertiesTitle->setText( tr( "<b>%1</b> · particle system" ).arg( Qs( element.m_szName ).toHtmlEscaped() ) );
		auto *pName = new QTreeWidgetItem( m_pProperties, { bSimple ? tr( "Name" ) : tr( "name" ) } );
		pName->setData( 0, ROLE_ATTRIBUTE_ROLE, QStringLiteral( "name" ) );
		pName->setToolTip( 0, tr( "Maps and code use the system by this name" ) );
		auto *pNameField = new QLineEdit( Qs( element.m_szName ) );
		connect( pNameField, &QLineEdit::editingFinished, this, [ this, pNameField, nSystem = nTarget ] {
			if ( !pNameField->text().trimmed().isEmpty() )
			{
				RenameSystem( nSystem, pNameField->text().trimmed() );
			}
		} );
		m_pProperties->setItemWidget( pName, 1, pNameField );

		std::set<std::string> shown;
		for ( const PropertyGroup_t &group : SystemGroups() )
		{
			QTreeWidgetItem *pParent = nullptr;
			for ( std::string_view szAttributeName : group.m_Names )
			{
				if ( const Attribute_t *pAttribute = element.Find( szAttributeName ) )
				{
					pParent = pParent ? pParent : addGroup( tr( group.m_pszTitle ), true );
					addRow( pParent, *pAttribute );
					shown.insert( pAttribute->m_szName );
				}
			}
			if ( pParent && pParent->childCount() == 0 )
			{
				delete pParent; // everything in it tucked away
			}
		}
		QTreeWidgetItem *pOther = nullptr;
		for ( const Attribute_t &attribute : element.m_Attributes )
		{
			if ( shown.contains( attribute.m_szName ) || particles::CategoryOf( attribute.m_szName ) || attribute.m_szName == particles::CHILDREN )
			{
				continue;
			}
			pOther = pOther ? pOther : addGroup( tr( "Other" ), true );
			addRow( pOther, attribute );
		}
	}
	else if ( element.m_szType == particles::OPERATOR_TYPE )
	{
		const auto position = PositionOf( nTarget );
		const auto category = position ? particles::CategoryOf( position->m_szList.toStdString() ) : std::nullopt;
		const QString szFunction = Qs( particles::FunctionName( element ) );
		const particles::CFunctionInfo *pInfo = category ? m_Schema.Function( *category, particles::FunctionName( element ) ) : nullptr;
		const int nUses = pInfo && m_Game.m_pSchema ? [ & ] {
			const auto *pKnown = m_Game.m_pSchema->Function( *category, particles::FunctionName( element ) );
			return pKnown ? pKnown->m_nUses : 0;
		}()
													: 0;
		const QString szShown = category && bSimple && !particles::PlainFunction( *category, szFunction.toStdString() ).m_szName.empty()
			? QStringLiteral( "%1</b> (%2)<b>" ).arg( Qs( particles::PlainFunction( *category, szFunction.toStdString() ).m_szName ).toHtmlEscaped(), szFunction.toHtmlEscaped() )
			: szFunction.toHtmlEscaped();
		m_pPropertiesTitle->setText( tr( "<b>%1</b> · %2 of %3%4" )
				.arg( szShown, category ? CategorySingular( *category ).toLower() : tr( "operator" ),
					Qs( doc.m_Elements[ static_cast<std::size_t>( m_nSystem ) ].m_szName ).toHtmlEscaped(),
					nUses > 0 ? tr( " · used %n time(s) in the game's files", nullptr, nUses ) : QString{} ) );
		auto *pSettings = addGroup( tr( "Settings" ), true );
		auto *pFade = addGroup( tr( "When it runs (every operator has these)" ), false );
		for ( const Attribute_t &attribute : element.m_Attributes )
		{
			if ( attribute.m_szName == particles::FUNCTION_NAME )
			{
				continue;
			}
			addRow( particles::IsCommonOperatorAttribute( attribute.m_szName ) ? pFade : pSettings, attribute );
		}
		if ( pSettings->childCount() == 0 )
		{
			pSettings->setText( 0, bSimple ? tr( "Settings (none to show: switch the simple interface off to see them all)" ) : tr( "Settings (none: this one has nothing to set)" ) );
		}
		if ( pFade->childCount() == 0 )
		{
			delete pFade;
		}
	}
	else if ( element.m_szType == particles::CHILD_TYPE )
	{
		m_pPropertiesTitle->setText( tr( "<b>%1</b> · child system of %2" ).arg( Qs( element.m_szName ).toHtmlEscaped(), Qs( doc.m_Elements[ static_cast<std::size_t>( m_nSystem ) ].m_szName ).toHtmlEscaped() ) );
		for ( const Attribute_t &attribute : element.m_Attributes )
		{
			addRow( m_pProperties->invisibleRootItem(), attribute );
		}
	}
	else
	{
		m_pPropertiesTitle->setText( tr( "<b>%1</b> · %2" ).arg( Qs( element.m_szName ).toHtmlEscaped(), Qs( element.m_szType ).toHtmlEscaped() ) );
		for ( const Attribute_t &attribute : element.m_Attributes )
		{
			addRow( m_pProperties->invisibleRootItem(), attribute );
		}
	}
	m_pProperties->expandItem( m_pProperties->topLevelItem( 0 ) );

	if ( !szFocusedAttribute.isEmpty() )
	{
		for ( QTreeWidgetItemIterator it{ m_pProperties }; *it; ++it )
		{
			if ( ( *it )->data( 0, ROLE_ATTRIBUTE_ROLE ).toString() == szFocusedAttribute )
			{
				if ( QWidget *pEditor = m_pProperties->itemWidget( *it, 1 ) )
				{
					const auto fields = pEditor->findChildren<QLineEdit *>();
					QWidget *pTarget = fields.isEmpty() ? pEditor : fields.value( nFocusedField, fields.front() );
					pTarget->setFocus();
				}
				break;
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QWidget *CParticleEditor::ValueEditor( std::int32_t nElement, const Attribute_t &attribute )
{
	const QString szName = Qs( attribute.m_szName );
	const AttrType_t nType = attribute.m_nType;
	const xv::dmx::Value_t current = attribute.m_Value;
	const auto commit = [ this, nElement, szName ]( xv::dmx::Value_t value ) { SetValue( nElement, szName, value ); };

	switch ( nType )
	{
		case AttrType_t::ATTR_TYPE_INT:
		{
			if ( particles::IsFieldSetting( attribute.m_szName ) )
			{
				// A particle field by number: listed by name.
				auto *pCombo = new QComboBox;
				const int nCurrent = std::get<std::int32_t>( current );
				for ( int nField = 0; nField < particles::FIELD_COUNT; nField++ )
				{
					const QString szField = Qs( particles::FieldName( nField ) );
					pCombo->addItem( IsSimple() ? szField : QStringLiteral( "%1 · %2" ).arg( nField ).arg( szField ), nField );
				}
				if ( pCombo->findData( nCurrent ) < 0 )
				{
					pCombo->addItem( QString::number( nCurrent ), nCurrent );
				}
				pCombo->setCurrentIndex( pCombo->findData( nCurrent ) );
				connect( pCombo, &QComboBox::activated, this, [ commit, pCombo ] { commit( std::int32_t{ pCombo->currentData().toInt() } ); } );
				return pCombo;
			}
			auto *pSpin = new QSpinBox;
			pSpin->setRange( INT_MIN, INT_MAX );
			pSpin->setValue( std::get<std::int32_t>( current ) );
			pSpin->setKeyboardTracking( false );
			connect( pSpin, &QSpinBox::valueChanged, this, [ commit ]( int nValue ) { commit( std::int32_t{ nValue } ); } );
			return pSpin;
		}
		case AttrType_t::ATTR_TYPE_FLOAT:
		{
			QLineEdit *pField = FloatField( std::get<float>( current ) );
			connect( pField, &QLineEdit::editingFinished, this, [ commit, pField ] {
				if ( const auto value = ParseFloat( pField->text() ) )
				{
					commit( *value );
				}
			} );
			return pField;
		}
		case AttrType_t::ATTR_TYPE_BOOL:
		{
			auto *pBox = new QCheckBox;
			pBox->setChecked( std::get<bool>( current ) );
			connect( pBox, &QCheckBox::toggled, this, [ commit ]( bool bOn ) { commit( bOn ); } );
			return pBox;
		}
		case AttrType_t::ATTR_TYPE_STRING:
		{
			auto *pField = new QLineEdit( Qs( std::get<std::string>( current ) ) );
			QStringList suggestions;
			if ( attribute.m_szName == "material" )
			{
				suggestions = m_Game.m_Materials;
				pField->setToolTip( tr( "A material under materials/, e.g. particle/smoke1/smoke1.vmt" ) );
			}
			else if ( attribute.m_szName == "cull_replacement_definition" )
			{
				for ( std::int32_t nSystem : particles::Systems( m_pDocument->GetDocument() ) )
				{
					suggestions << Qs( m_pDocument->GetDocument().m_Elements[ static_cast<std::size_t>( nSystem ) ].m_szName );
				}
			}
			if ( !suggestions.isEmpty() )
			{
				auto *pCompleter = new QCompleter( suggestions, pField );
				pCompleter->setCaseSensitivity( Qt::CaseInsensitive );
				pCompleter->setFilterMode( Qt::MatchContains );
				pField->setCompleter( pCompleter );
			}
			pField->setCursorPosition( 0 ); // long paths show their start
			connect( pField, &QLineEdit::editingFinished, this, [ commit, pField ] { commit( pField->text().toStdString() ); } );
			return pField;
		}
		case AttrType_t::ATTR_TYPE_COLOR:
		{
			const auto color = std::get<xv::dmx::CColor>( current );
			auto *pButton = new QToolButton;
			pButton->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
			pButton->setIcon( Swatch( color ) );
			pButton->setText( Qs( xv::dmx::ValueText( current ) ) );
			pButton->setAutoRaise( true );
			connect( pButton, &QToolButton::clicked, this, [ this, commit, color, szName ] {
				const QColor picked = QColorDialog::getColor( QColor( color.m_nR, color.m_nG, color.m_nB, color.m_nA ), this, szName, QColorDialog::ShowAlphaChannel );
				if ( picked.isValid() )
				{
					commit( xv::dmx::CColor{ static_cast<std::uint8_t>( picked.red() ), static_cast<std::uint8_t>( picked.green() ),
						static_cast<std::uint8_t>( picked.blue() ), static_cast<std::uint8_t>( picked.alpha() ) } );
				}
			} );
			return pButton;
		}
		case AttrType_t::ATTR_TYPE_ELEMENT:
		{
			// A child link's system.
			auto *pCombo = new QComboBox;
			const Document_t &doc = m_pDocument->GetDocument();
			const CElementRef ref = std::get<CElementRef>( current );
			pCombo->addItem( tr( "(none)" ), CElementRef::NONE );
			for ( std::int32_t nSystem : particles::Systems( doc ) )
			{
				pCombo->addItem( Qs( doc.m_Elements[ static_cast<std::size_t>( nSystem ) ].m_szName ), nSystem );
			}
			pCombo->setCurrentIndex( std::max( 0, pCombo->findData( ref.m_nIndex ) ) );
			connect( pCombo, &QComboBox::activated, this, [ this, pCombo, nElement ] {
				const std::int32_t nSystem = pCombo->currentData().toInt();
				m_pDocument->Edit( tr( "Change child system" ), [ nElement, nSystem ]( Document_t &doc ) {
					CElement &link = doc.m_Elements[ static_cast<std::size_t>( nElement ) ];
					particles::SetAttribute( link, "child", AttrType_t::ATTR_TYPE_ELEMENT, CElementRef{ nSystem, {} } );
					if ( nSystem >= 0 )
					{
						link.m_szName = doc.m_Elements[ static_cast<std::size_t>( nSystem ) ].m_szName;
					}
				} );
			} );
			return pCombo;
		}
		default:
			break;
	}

	if ( const std::size_t nCount = ComponentCount( nType ); nCount > 0 )
	{
		auto *pBox = new QWidget;
		auto *pLayout = new QHBoxLayout( pBox );
		pLayout->setContentsMargins( 0, 0, 0, 0 );
		pLayout->setSpacing( 2 );
		std::vector<QLineEdit *> fields;
		xv::dmx::Value_t value = current;
		const auto read = [ &value, nCount ]( std::size_t i ) {
			switch ( nCount )
			{
				case 2: return Components<2>( value )[ i ];
				case 3: return Components<3>( value )[ i ];
				default: return Components<4>( value )[ i ];
			}
		};
		for ( std::size_t i = 0; i < nCount; i++ )
		{
			QLineEdit *pField = FloatField( read( i ) );
			pField->setMinimumWidth( 48 );
			pLayout->addWidget( pField );
			fields.push_back( pField );
		}
		for ( QLineEdit *pField : fields )
		{
			connect( pField, &QLineEdit::editingFinished, this, [ commit, fields, current ] {
				xv::dmx::Value_t edited = current;
				for ( std::size_t i = 0; i < fields.size(); i++ )
				{
					const auto parsed = ParseFloat( fields[ i ]->text() );
					if ( !parsed )
					{
						return;
					}
					switch ( fields.size() )
					{
						case 2: Components<2>( edited )[ i ] = *parsed; break;
						case 3: Components<3>( edited )[ i ] = *parsed; break;
						default: Components<4>( edited )[ i ] = *parsed; break;
					}
				}
				commit( edited );
			} );
		}
		return pBox;
	}

	// Lists, matrices and binary data are shown as stored; nothing is lost on saving.
	auto *pLabel = new QLabel( Qs( xv::dmx::ValueText( current ) ).left( 300 ) );
	pLabel->setTextInteractionFlags( Qt::TextSelectableByMouse );
	pLabel->setToolTip( tr( "Shown as stored. This kind of value can't be edited here yet, and is saved unchanged." ) );
	return pLabel;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::FillAddMenu()
{
	QMenu *pMenu = m_pAdd->menu();
	pMenu->clear();
	if ( m_nSystem < 0 )
	{
		pMenu->addAction( tr( "Add a system first" ) )->setEnabled( false );
		return;
	}
	const std::optional<Category_t> selected = m_szList.isEmpty() && m_nElement >= 0 && PositionOf( m_nElement )
		? particles::CategoryOf( PositionOf( m_nElement )->m_szList.toStdString() )
		: particles::CategoryOf( m_szList.toStdString() );
	for ( std::string_view szList : STACK_ORDER )
	{
		if ( szList == particles::CHILDREN )
		{
			std::vector<PickerItem_t> systems;
			const Document_t &doc = m_pDocument->GetDocument();
			for ( std::int32_t nSystem : particles::Systems( doc ) )
			{
				if ( nSystem != m_nSystem )
				{
					systems.push_back( { Qs( doc.m_Elements[ static_cast<std::size_t>( nSystem ) ].m_szName ), {}, [ this, nSystem ] { AddChild( nSystem ); } } );
				}
			}
			AddPicker( pMenu, tr( "Child System" ), std::move( systems ), tr( "No other systems in this file" ) );
			continue;
		}
		const Category_t nCategory = *particles::CategoryOf( szList );
		std::vector<PickerItem_t> items;
		for ( const particles::CFunctionInfo *pFunction : m_Schema.Functions( nCategory ) )
		{
			const auto *pKnown = m_Game.m_pSchema ? m_Game.m_pSchema->Function( nCategory, pFunction->m_szName ) : nullptr;
			QString szTip = pKnown ? tr( "Used %n time(s) in the game's files", nullptr, pKnown->m_nUses ) : tr( "Only used in this file" );
			if ( !particles::IsSimulated( nCategory, pFunction->m_szName ) )
			{
				szTip += u'\n' + tr( "Not in the preview yet" );
			}
			else if ( const std::string_view szNote = particles::PreviewNote( nCategory, pFunction->m_szName ); !szNote.empty() )
			{
				szTip += u'\n' + tr( "In the preview: %1" ).arg( Qs( szNote ) );
			}
			const auto [ szLabel, szAbout ] = FunctionLabel( nCategory, Qs( pFunction->m_szName ) );
			if ( !szAbout.isEmpty() )
			{
				szTip = szAbout + u'\n' + szTip;
			}
			items.push_back( { szLabel, szTip, [ this, nCategory, szName = Qs( pFunction->m_szName ) ] { AddOperator( nCategory, szName ); } } );
		}
		QMenu *pSub = AddPicker( pMenu, CategorySingular( nCategory ), std::move( items ), tr( "No examples to copy from: open a game" ) );
		if ( selected == nCategory )
		{
			QFont font = pSub->menuAction()->font();
			font.setBold( true );
			pSub->menuAction()->setFont( font );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::ShowStackMenu( const QPoint &pos )
{
	QMenu menu;
	if ( QTreeWidgetItem *pItem = m_pStack->itemAt( pos ); pItem && pItem->data( 0, ROLE_ELEMENT_ROLE ).toInt() >= 0 )
	{
		menu.addAction( tr( "Remove" ), this, &CParticleEditor::RemoveSelected );
		menu.addAction( tr( "Move Up" ), this, [ this ] { MoveSelected( -1 ); } );
		menu.addAction( tr( "Move Down" ), this, [ this ] { MoveSelected( 1 ); } );
		menu.addSeparator();
	}
	FillAddMenu();
	menu.addMenu( m_pAdd->menu() )->setText( tr( "Add" ) );
	menu.exec( m_pStack->viewport()->mapToGlobal( pos ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::ShowPropertyMenu( const QPoint &pos )
{
	QTreeWidgetItem *pItem = m_pProperties->itemAt( pos );
	const QString szAttribute = pItem ? pItem->data( 0, ROLE_ATTRIBUTE_ROLE ).toString() : QString{};
	if ( szAttribute.isEmpty() || szAttribute == u"name" )
	{
		return;
	}
	const std::int32_t nTarget = m_nElement >= 0 ? m_nElement : m_nSystem;
	const CElement &element = m_pDocument->GetDocument().m_Elements[ static_cast<std::size_t>( nTarget ) ];
	const Attribute_t *pCurrent = element.Find( szAttribute.toStdString() );
	const particles::AttributeInfo_t *pFallback = DefaultFor( nTarget, szAttribute.toStdString() );
	QMenu menu;
	QAction *pReset = menu.addAction( tr( "Reset to the Game's Usual Value" ), this, [ this, nTarget, szAttribute, value = pFallback ? pFallback->m_Value : xv::dmx::Value_t{} ] {
		SetValue( nTarget, szAttribute, value );
	} );
	pReset->setEnabled( pCurrent && pFallback && pFallback->m_nType == pCurrent->m_nType && !xv::dmx::SameValue( pFallback->m_Value, pCurrent->m_Value ) );
	menu.addAction( tr( "Copy Value" ), this, [ szValue = pCurrent ? Qs( xv::dmx::ValueText( pCurrent->m_Value ) ) : QString{} ] {
		QApplication::clipboard()->setText( szValue );
	} );
	menu.exec( m_pProperties->viewport()->mapToGlobal( pos ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::ShowSystemMenu( QWidget *pView, const QPoint &pos )
{
	QMenu menu;
	menu.addAction( tr( "New System" ), this, [ this ] { NewSystem(); } );
	const bool bOnItem = pView == m_pGrid ? m_pGrid->itemAt( pos ) != nullptr : m_pSystems->itemAt( pos ) != nullptr;
	if ( bOnItem )
	{
		menu.addAction( tr( "Duplicate" ), this, &CParticleEditor::DuplicateSystem );
		menu.addAction( tr( "Rename" ), this, [ this, pView ] {
			if ( pView == m_pGrid )
			{
				m_pGrid->editItem( m_pGrid->currentItem() );
			}
			else
			{
				m_pSystems->editItem( m_pSystems->currentItem() );
			}
		} );
		menu.addAction( tr( "Delete" ), this, &CParticleEditor::DeleteSystem );
		menu.addSeparator();
		menu.addAction( tr( "Copy" ), this, &CParticleEditor::CopySystems );
	}
	menu.addAction( tr( "Paste" ), this, &CParticleEditor::PasteSystems )->setEnabled( QApplication::clipboard()->mimeData()->hasFormat( QString::fromLatin1( CLIPBOARD_TYPE ) ) );
	menu.exec( ( pView == m_pGrid ? m_pGrid->viewport() : m_pSystems->viewport() )->mapToGlobal( pos ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::int32_t> CParticleEditor::GetSelectedSystems() const
{
	std::vector<std::int32_t> picked;
	const auto add = [ &picked ]( std::int32_t nSystem ) {
		if ( std::ranges::find( picked, nSystem ) == picked.end() )
		{
			picked.push_back( nSystem );
		}
	};
	if ( m_pGrid->isVisible() || m_pGridToggle->isChecked() )
	{
		for ( QListWidgetItem *pItem : m_pGrid->selectedItems() )
		{
			add( pItem->data( ROLE_SYSTEM_ROLE ).toInt() );
		}
	}
	else
	{
		for ( QTreeWidgetItem *pItem : m_pSystems->selectedItems() )
		{
			add( pItem->data( 0, ROLE_SYSTEM_ROLE ).toInt() );
		}
	}
	return picked;
}

//-----------------------------------------------------------------------------
// Purpose: Systems view as a grid of pictures: built when shown, pictures drawn a few at a time.
//-----------------------------------------------------------------------------
void CParticleEditor::SetGridView( bool bGrid )
{
	{
		const QSignalBlocker blocker{ m_pGridToggle };
		m_pGridToggle->setChecked( bGrid );
	}
	m_pSystems->setVisible( !bGrid );
	m_pGrid->setVisible( bGrid );
	if ( bGrid )
	{
		RebuildGrid();
	}
	else
	{
		m_pThumbnailTimer->stop();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::RebuildGrid()
{
	if ( !m_pGridToggle->isChecked() )
	{
		return;
	}
	const Document_t &doc = m_pDocument->GetDocument();
	const QString szFilter = m_pFilter->text().trimmed();
	const QSignalBlocker blocker{ m_pGrid };
	m_pGrid->clear();
	QPixmap blank( THUMBNAIL_SIZE, THUMBNAIL_SIZE );
	blank.fill( QColor( 33, 36, 38 ) );
	for ( std::int32_t nSystem : particles::Systems( doc ) )
	{
		const QString szName = Qs( doc.m_Elements[ static_cast<std::size_t>( nSystem ) ].m_szName );
		if ( !szFilter.isEmpty() && !szName.contains( szFilter, Qt::CaseInsensitive ) )
		{
			continue;
		}
		auto *pItem = new QListWidgetItem( szName, m_pGrid );
		pItem->setData( ROLE_SYSTEM_ROLE, nSystem );
		pItem->setToolTip( szName );
		pItem->setFlags( pItem->flags() | Qt::ItemIsEditable );
		const auto it = m_Thumbnails.find( nSystem );
		pItem->setIcon( it != m_Thumbnails.end() ? it->second : QIcon{ blank } );
		if ( nSystem == m_nSystem )
		{
			m_pGrid->setCurrentItem( pItem );
		}
	}
	if ( !m_ThumbnailQueue.empty() || m_Thumbnails.size() < static_cast<std::size_t>( m_pGrid->count() ) )
	{
		m_pThumbnailTimer->start();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::DrawThumbnails()
{
	if ( !m_pGrid->isVisible() && !m_pGridToggle->isChecked() )
	{
		m_pThumbnailTimer->stop();
		return;
	}
	// The ones in the grid that have no picture yet go first, then the out-of-date ones.
	if ( m_ThumbnailQueue.empty() )
	{
		for ( int i = 0; i < m_pGrid->count(); i++ )
		{
			const std::int32_t nSystem = m_pGrid->item( i )->data( ROLE_SYSTEM_ROLE ).toInt();
			if ( !m_Thumbnails.contains( nSystem ) )
			{
				m_ThumbnailQueue.push_back( nSystem );
			}
		}
		if ( m_ThumbnailQueue.empty() )
		{
			m_pThumbnailTimer->stop();
			return;
		}
	}
	QElapsedTimer clock;
	clock.start();
	const Document_t &doc = m_pDocument->GetDocument();
	while ( !m_ThumbnailQueue.empty() && clock.elapsed() < THUMBNAIL_BUDGET_MS )
	{
		const std::int32_t nSystem = m_ThumbnailQueue.front();
		m_ThumbnailQueue.erase( m_ThumbnailQueue.begin() );
		if ( nSystem < 0 || static_cast<std::size_t>( nSystem ) >= doc.m_Elements.size() )
		{
			continue;
		}
		const QImage image = m_pPreview->Thumbnail( doc.m_Elements[ static_cast<std::size_t>( nSystem ) ].m_szName, THUMBNAIL_SIZE );
		if ( image.isNull() )
		{
			continue;
		}
		m_Thumbnails[ nSystem ] = QIcon{ QPixmap::fromImage( image ) };
		for ( int i = 0; i < m_pGrid->count(); i++ )
		{
			if ( m_pGrid->item( i )->data( ROLE_SYSTEM_ROLE ).toInt() == nSystem )
			{
				const QSignalBlocker blocker{ m_pGrid };
				m_pGrid->item( i )->setIcon( m_Thumbnails[ nSystem ] );
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Selection, by element index. selectElement takes an operator or child link of the
//          current
//          system, or -1 for the system's own properties.
//-----------------------------------------------------------------------------
void CParticleEditor::SelectSystem( std::int32_t nSystem )
{
	m_nSystem = nSystem;
	m_nElement = -1;
	m_szList.clear();
	RebuildSystems();
	RebuildStack();
	RebuildProperties();
	ShowInPreview();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::SelectElement( std::int32_t nElement )
{
	m_nElement = nElement >= 0 && PositionOf( nElement ) ? nElement : -1;
	m_szList = m_nElement >= 0 ? PositionOf( m_nElement )->m_szList : QString{};
	RebuildStack();
	RebuildProperties();
}

//-----------------------------------------------------------------------------
// Purpose: Editing, as the toolbar and menus do it. Each is one undo step.
//-----------------------------------------------------------------------------
std::int32_t CParticleEditor::NewSystem( const QString &szName )
{
	const QString szBase = !szName.isEmpty() ? szName : QFileInfo{ m_pDocument->GetFileName() }.completeBaseName();
	std::int32_t nAdded = -1;
	m_pDocument->Edit( tr( "New system" ), [ & ]( Document_t &doc ) {
		nAdded = particles::AddSystem( doc, particles::UniqueSystemName( doc, szBase.toStdString() ), m_Schema );
		for ( const auto &[ category, function ] : StarterOperators() )
		{
			if ( const particles::CFunctionInfo *pInfo = m_Schema.Function( category, function ) )
			{
				particles::AddOperator( doc, nAdded, category, *pInfo );
			}
		}
	} );
	m_pFilter->clear();
	SelectSystem( nAdded );
	return nAdded;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::DuplicateSystem()
{
	if ( m_nSystem < 0 )
	{
		return;
	}
	std::int32_t nCopy = -1;
	const std::string szName = m_pDocument->GetDocument().m_Elements[ static_cast<std::size_t>( m_nSystem ) ].m_szName;
	m_pDocument->Edit( tr( "Duplicate %1" ).arg( Qs( szName ) ), [ & ]( Document_t &doc ) {
		nCopy = particles::DuplicateSystem( doc, m_nSystem, particles::UniqueSystemName( doc, szName ) );
	} );
	SelectSystem( nCopy );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::DeleteSystem()
{
	if ( m_nSystem < 0 )
	{
		return;
	}
	const std::string szName = m_pDocument->GetDocument().m_Elements[ static_cast<std::size_t>( m_nSystem ) ].m_szName;
	m_pDocument->Edit( tr( "Delete %1" ).arg( Qs( szName ) ), [ nSystem = m_nSystem ]( Document_t &doc ) { particles::RemoveSystem( doc, nSystem ); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::RenameSystem( std::int32_t nSystem, const QString &szName )
{
	m_pDocument->Edit( tr( "Rename to %1" ).arg( szName ), [ nSystem, szName = szName.toStdString() ]( Document_t &doc ) { particles::RenameSystem( doc, nSystem, szName ); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::int32_t CParticleEditor::AddOperator( Category_t nCategory, const QString &szFunction )
{
	const particles::CFunctionInfo *pInfo = m_Schema.Function( nCategory, szFunction.toStdString() );
	if ( m_nSystem < 0 || !pInfo )
	{
		return -1;
	}
	// After the selected operator of the same kind, else at the end.
	std::optional<std::size_t> position;
	if ( const auto selected = m_nElement >= 0 ? PositionOf( m_nElement ) : std::nullopt; selected && selected->m_szList == Qs( particles::AttributeName( nCategory ) ) )
	{
		position = selected->m_nPosition + 1;
	}
	std::int32_t nAdded = -1;
	m_pDocument->Edit( tr( "Add %1" ).arg( szFunction ), [ & ]( Document_t &doc ) { nAdded = particles::AddOperator( doc, m_nSystem, nCategory, *pInfo, position ); } );
	SelectElement( nAdded );
	return nAdded;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::int32_t CParticleEditor::AddChild( std::int32_t nChild )
{
	if ( m_nSystem < 0 )
	{
		return -1;
	}
	std::int32_t nAdded = -1;
	m_pDocument->Edit( tr( "Add child system" ), [ & ]( Document_t &doc ) { nAdded = particles::AddChild( doc, m_nSystem, nChild ); } );
	SelectElement( nAdded );
	return nAdded;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::RemoveSelected()
{
	const auto position = m_nElement >= 0 ? PositionOf( m_nElement ) : std::nullopt;
	if ( !position )
	{
		return;
	}
	m_pDocument->Edit( tr( "Remove %1" ).arg( ListTitle( position->m_szList ).toLower() ), [ this, &position ]( Document_t &doc ) {
		particles::RemoveReference( doc, m_nSystem, position->m_szList.toStdString(), position->m_nPosition );
	} );
	m_nElement = -1;
	m_szList = position->m_szList;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::MoveSelected( int nDelta )
{
	const auto position = m_nElement >= 0 ? PositionOf( m_nElement ) : std::nullopt;
	if ( !position || ( nDelta < 0 && position->m_nPosition == 0 ) )
	{
		return;
	}
	const std::size_t nTo = position->m_nPosition + static_cast<std::size_t>( nDelta );
	m_pDocument->Edit( tr( "Move" ), [ this, &position, nTo ]( Document_t &doc ) {
		particles::MoveReference( doc, m_nSystem, position->m_szList.toStdString(), position->m_nPosition, nTo );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CParticleEditor::SetValue( std::int32_t nElement, const QString &szAttribute, const xv::dmx::Value_t &value )
{
	return m_pDocument->Edit( tr( "Set %1" ).arg( szAttribute ), [ nElement, szName = szAttribute.toStdString(), &value ]( Document_t &doc ) {
		CElement& target = doc.m_Elements[static_cast<std::size_t>(nElement)];
		if (Attribute_t* pExisting = target.Find(szName)) {
			pExisting->m_Value = value;
		} }, QStringLiteral( "value:%1:%2" ).arg( nElement ).arg( szAttribute ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::CopySystems()
{
	std::vector<std::int32_t> picked = GetSelectedSystems();
	if ( picked.empty() && m_nSystem >= 0 )
	{
		picked.push_back( m_nSystem );
	}
	if ( picked.empty() )
	{
		return;
	}
	const Document_t clip = particles::Extract( m_pDocument->GetDocument(), picked );
	const auto bytes = xv::dmx::WriteBinary( clip );
	if ( !bytes )
	{
		return;
	}
	auto *pData = new QMimeData;
	pData->setData( QString::fromLatin1( CLIPBOARD_TYPE ), QByteArray( reinterpret_cast<const char *>( bytes->data() ), static_cast<qsizetype>( bytes->size() ) ) );
	pData->setText( Qs( xv::dmx::WriteKeyValues2( clip ) ) );
	QApplication::clipboard()->setMimeData( pData );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleEditor::PasteSystems()
{
	const QMimeData *pData = QApplication::clipboard()->mimeData();
	if ( !pData || !pData->hasFormat( QString::fromLatin1( CLIPBOARD_TYPE ) ) )
	{
		return;
	}
	const QByteArray bytes = pData->data( QString::fromLatin1( CLIPBOARD_TYPE ) );
	const auto read = xv::dmx::Read( { reinterpret_cast<const std::byte *>( bytes.constData() ), static_cast<std::size_t>( bytes.size() ) } );
	if ( !read.m_Document )
	{
		return;
	}
	std::vector<std::int32_t> pasted;
	m_pDocument->Edit( tr( "Paste systems" ), [ & ]( Document_t &doc ) { pasted = particles::Paste( doc, *read.m_Document ); } );
	if ( !pasted.empty() )
	{
		m_pFilter->clear();
		SelectSystem( pasted.front() );
	}
}
