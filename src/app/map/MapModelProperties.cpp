//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The Model tab in Object Properties.
//
//===========================================================================//
#include "MapModelProperties.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "ModelBrowser.h"
#include "kv1/Kv1.h"
#include "map/MapDocument.h"
#include "map/MapRenderData.h"
#include "map/MapResources.h"
#include "model/Anim.h"
#include "util/Numbers.h"

namespace
{
constexpr int CYCLE_STEPS = 1000;
constexpr int SEQUENCE_FILE = Qt::UserRole;
constexpr int SEQUENCE_NUMBER = Qt::UserRole + 1;
constexpr int STUDIO_LOOPING = 1;
} // namespace

//-----------------------------------------------------------------------------
// Purpose: Arrange the skin gallery, animation controls, bodygroups and model information.
//-----------------------------------------------------------------------------
CMapModelProperties::CMapModelProperties( CMapDocument &document, QWidget *pParent )
	: QWidget( pParent ),
	  m_Document( document )
{
	setObjectName( QStringLiteral( "mapModelProperties" ) );
	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 2, 2, 2, 2 );
	m_pStatus = new QLabel;
	m_pStatus->setWordWrap( true );
	pLayout->addWidget( m_pStatus );
	auto *pTabs = new QTabWidget;
	m_pPages = pTabs;
	pLayout->addWidget( pTabs, 1 );

	m_pSkins = new QListWidget;
	m_pSkins->setObjectName( QStringLiteral( "mapModelSkins" ) );
	m_pSkins->setViewMode( QListView::IconMode );
	m_pSkins->setResizeMode( QListView::Adjust );
	m_pSkins->setMovement( QListView::Static );
	m_pSkins->setIconSize( QSize( 80, 80 ) );
	m_pSkins->setGridSize( QSize( 100, 105 ) );
	pTabs->addTab( m_pSkins, tr( "Skin" ) );

	auto *pSequences = new QWidget;
	auto *pSequenceLayout = new QVBoxLayout( pSequences );
	auto *pFilter = new QLineEdit;
	pFilter->setObjectName( QStringLiteral( "mapModelSequenceFilter" ) );
	pFilter->setPlaceholderText( tr( "Filter sequences" ) );
	pFilter->setClearButtonEnabled( true );
	pSequenceLayout->addWidget( pFilter );
	m_pSequences = new QListWidget;
	m_pSequences->setObjectName( QStringLiteral( "mapModelSequences" ) );
	pSequenceLayout->addWidget( m_pSequences, 1 );
	auto *pPlayback = new QHBoxLayout;
	m_pPlay = new QPushButton( tr( "Play" ) );
	m_pPlay->setObjectName( QStringLiteral( "mapModelPlaySequence" ) );
	m_pPlay->setEnabled( false );
	pPlayback->addWidget( m_pPlay );
	m_pCycle = new QSlider( Qt::Horizontal );
	m_pCycle->setObjectName( QStringLiteral( "mapModelSequenceTime" ) );
	m_pCycle->setAccessibleName( tr( "Sequence time" ) );
	m_pCycle->setRange( 0, CYCLE_STEPS );
	m_pCycle->setEnabled( false );
	pPlayback->addWidget( m_pCycle, 1 );
	pSequenceLayout->addLayout( pPlayback );
	pTabs->addTab( pSequences, tr( "Sequences" ) );

	auto *pBody = new QWidget;
	m_pBodyGroups = new QFormLayout( pBody );
	auto *pScroll = new QScrollArea;
	pScroll->setWidgetResizable( true );
	pScroll->setWidget( pBody );
	pTabs->addTab( pScroll, tr( "Body Groups" ) );

	auto *pInfo = new QWidget;
	auto *pInfoLayout = new QVBoxLayout( pInfo );
	m_pModelInfo = new QLabel;
	m_pModelInfo->setWordWrap( true );
	m_pModelInfo->setTextFormat( Qt::PlainText );
	pInfoLayout->addWidget( m_pModelInfo );
	m_pMaterials = new QListWidget;
	m_pMaterials->setObjectName( QStringLiteral( "mapModelMaterials" ) );
	m_pMaterials->setToolTip( tr( "Double-click a material to open it" ) );
	pInfoLayout->addWidget( m_pMaterials, 1 );
	pTabs->addTab( pInfo, tr( "Info" ) );

	m_pAnimate = new QTimer( this );
	m_pAnimate->setInterval( 33 );
	connect( m_pAnimate, &QTimer::timeout, this, &CMapModelProperties::Advance );
	m_pPictures = new QTimer( this );
	m_pPictures->setInterval( 20 );
	connect( m_pPictures, &QTimer::timeout, this, &CMapModelProperties::DrawNextSkin );

	connect( m_pSkins, &QListWidget::itemClicked, this, [ this ]( QListWidgetItem *pItem ) {
		const std::string szSkin = std::to_string( m_pSkins->row( pItem ) );
		ApplyKey( "skin", tr( "Change Skin" ), [ & ]( const auto & ) { return szSkin; } );
	} );
	connect( pFilter, &QLineEdit::textChanged, this, [ this ]( const QString &szFilter ) {
		for ( int i = 0; i < m_pSequences->count(); ++i )
			m_pSequences->item( i )->setHidden( !m_pSequences->item( i )->text().contains( szFilter, Qt::CaseInsensitive ) );
	} );
	connect( m_pSequences, &QListWidget::itemActivated, this, &CMapModelProperties::ChooseSequence );
	connect( m_pSequences, &QListWidget::itemClicked, this, &CMapModelProperties::ChooseSequence );
	connect( m_pPlay, &QPushButton::clicked, this, [ this ] {
		if ( m_pAnimate->isActive() )
		{
			m_pAnimate->stop();
			m_pPlay->setText( tr( "Play" ) );
		}
		else if ( m_flDuration > 0.0 )
		{
			if ( m_flCycle >= 1.0 )
				m_flCycle = 0.0;
			m_Clock.start();
			m_pAnimate->start();
			m_pPlay->setText( tr( "Pause" ) );
		}
	} );
	connect( m_pCycle, &QSlider::valueChanged, this, [ this ]( int nCycle ) {
		m_flCycle = static_cast<double>( nCycle ) / CYCLE_STEPS;
		m_Clock.start();
		InvalidatePose();
	} );
	connect( m_pMaterials, &QListWidget::itemActivated, this, [ this ]( QListWidgetItem *pItem ) {
		emit OpenMaterialRequested( pItem->data( Qt::UserRole ).toString() );
	} );
}

//-----------------------------------------------------------------------------
// Purpose: Refresh the studio files when the map's game mounts are available or change.
//-----------------------------------------------------------------------------
void CMapModelProperties::SetFiles( std::shared_ptr<const xv::vfs::CFileSystem> pFiles )
{
	if ( m_pFiles == pFiles )
		return;
	m_pFiles = std::move( pFiles );
	m_pfnRead = [ this ]( std::string_view szPath ) { return m_pFiles ? m_pFiles->Read( szPath ) : std::nullopt; };
	m_szLoadedModel.clear();
	if ( isVisible() )
		ReadModel();
}

//-----------------------------------------------------------------------------
// Purpose: Only selections made entirely of entities sharing a studio model can use the tab.
//-----------------------------------------------------------------------------
bool CMapModelProperties::RefreshSelection()
{
	const auto *pScene = m_Document.GetScene();
	std::string szModel;
	auto objects = m_Document.GetSelection();
	for ( const auto nObject : objects )
	{
		if ( !pScene || nObject >= pScene->GetObjectCount() || pScene->IsSolidObject( nObject ) || pScene->IsDeleted( nObject ) )
		{
			szModel.clear();
			break;
		}
		const std::string szPath = xv::vfs::NormalizePath( pScene->m_Entities[ pScene->GetIndex( nObject ) ].m_szModel );
		if ( !szPath.ends_with( ".mdl" ) || ( !szModel.empty() && szModel != szPath ) )
		{
			szModel.clear();
			break;
		}
		szModel = szPath;
	}
	if ( szModel.empty() )
		objects.clear();
	const bool bChanged = m_szModel != szModel || m_Objects != objects;
	m_szModel = std::move( szModel );
	m_Objects = std::move( objects );
	if ( bChanged )
	{
		m_pAnimate->stop();
		m_nFile = m_nSequence = -1;
		m_pPlay->setText( tr( "Play" ) );
		m_pPlay->setEnabled( false );
		m_pCycle->setEnabled( false );
		InvalidatePose();
	}
	if ( isVisible() )
		ReadModel();
	return !m_szModel.empty();
}

//-----------------------------------------------------------------------------
// Purpose: Read studio metadata only when the user opens the Model tab.
//-----------------------------------------------------------------------------
void CMapModelProperties::showEvent( QShowEvent *pEvent )
{
	QWidget::showEvent( pEvent );
	ReadModel();
	InvalidatePose();
}

//-----------------------------------------------------------------------------
// Purpose: Stop updates to the map when the Model tab is closed or hidden.
//-----------------------------------------------------------------------------
void CMapModelProperties::hideEvent( QHideEvent *pEvent )
{
	QWidget::hideEvent( pEvent );
	m_pPictures->stop();
	m_pAnimate->stop();
	m_pPlay->setText( tr( "Play" ) );
	InvalidatePose();
}

//-----------------------------------------------------------------------------
// Purpose: Populate the controls from the model and its included sequences.
//-----------------------------------------------------------------------------
void CMapModelProperties::ReadModel()
{
	if ( !m_szModel.empty() && m_szLoadedModel == m_szModel )
	{
		RefreshValues();
		if ( m_nNextSkin < m_pSkins->count() )
			m_pPictures->start();
		return;
	}
	m_pAnimate->stop();
	m_pPictures->stop();
	m_nFile = m_nSequence = -1;
	m_flCycle = m_flDuration = 0.0;
	m_pPlay->setText( tr( "Play" ) );
	m_pPlay->setEnabled( false );
	m_pCycle->setEnabled( false );
	InvalidatePose();
	m_szLoadedModel.clear();
	m_Studios.clear();
	m_pSkins->clear();
	m_pSequences->clear();
	m_pMaterials->clear();
	m_MaterialSelection.reset();
	m_BodyChoices.clear();
	while ( m_pBodyGroups->rowCount() )
		m_pBodyGroups->removeRow( 0 );
	auto studio = m_pFiles && !m_szModel.empty() ? xv::model::ReadStudio( m_szModel, m_pfnRead ) : std::nullopt;
	m_pPages->setVisible( studio.has_value() );
	m_pStatus->setVisible( !studio );
	if ( !studio )
	{
		m_pStatus->setText( !m_pFiles ? tr( "Open the map’s game to inspect its models." ) : tr( "Could not read %1." ).arg( QString::fromStdString( m_szModel ) ) );
		return;
	}
	m_szLoadedModel = m_szModel;
	std::vector<std::string> problems;
	auto includes = xv::model::LoadIncludes( *studio, m_pfnRead, problems );
	m_Studios.push_back( std::move( *studio ) );
	for ( auto &included : includes )
		m_Studios.push_back( std::move( included ) );

	const auto &main = m_Studios.front();
	const int nSkins = static_cast<int>( std::max<std::size_t>( 1, main.m_Skins.size() ) );
	for ( int nSkin = 0; nSkin < nSkins; ++nSkin )
		m_pSkins->addItem( tr( "Skin %1" ).arg( nSkin ) );
	m_nNextSkin = 0;
	m_pPictures->start();
	for ( std::size_t f = 0; f < m_Studios.size(); ++f )
		for ( std::size_t s = 0; s < m_Studios[ f ].m_Sequences.size(); ++s )
		{
			const auto &sequence = m_Studios[ f ].m_Sequences[ s ];
			auto *pItem = new QListWidgetItem( QString::fromStdString( sequence.m_szName ), m_pSequences );
			pItem->setData( SEQUENCE_FILE, static_cast<int>( f ) );
			pItem->setData( SEQUENCE_NUMBER, static_cast<int>( s ) );
			pItem->setToolTip( QString::fromStdString( m_Studios[ f ].m_szPath ) );
		}
	for ( std::size_t g = 0; g < main.m_Bodygroups.size(); ++g )
	{
		const auto &group = main.m_Bodygroups[ g ];
		if ( group.m_Models.size() < 2 || group.m_nBase <= 0 )
			continue;
		auto *pParts = new QComboBox;
		pParts->setObjectName( QStringLiteral( "mapModelBodyGroup%1" ).arg( g ) );
		for ( const auto &szPart : group.m_Models )
			pParts->addItem( szPart.empty() ? tr( "(empty)" ) : QString::fromStdString( szPart ) );
		m_pBodyGroups->addRow( QString::fromStdString( group.m_szName ), pParts );
		m_BodyChoices.emplace_back( g, pParts );
		connect( pParts, &QComboBox::activated, this, [ this, g ]( int nPart ) {
			const auto &group = m_Studios.front().m_Bodygroups[ g ];
			const int nBase = group.m_nBase;
			const int nCount = static_cast<int>( group.m_Models.size() );
			ApplyKey( "body", tr( "Change Body Group" ), [ nBase, nCount, nPart ]( const xv::map::Entity_t &entity ) {
				const int nBody = std::max( 0, xv::ParseNumber<int>( entity.GetValue( "body" ) ).value_or( xv::ParseNumber<int>( entity.GetValue( "SetBodyGroup" ) ).value_or( 0 ) ) );
				const auto nValue = static_cast<qint64>( nBody ) + static_cast<qint64>( nPart - nBody / nBase % nCount ) * nBase;
				return std::to_string( std::min<qint64>( nValue, std::numeric_limits<int>::max() ) );
			} );
		} );
	}
	if ( m_BodyChoices.empty() )
		m_pBodyGroups->addRow( new QLabel( tr( "This model has no selectable body groups." ) ) );
	const auto size = main.m_vecHullMax - main.m_vecHullMin;
	const bool bPhysics = m_pFiles->Resolve( m_szModel.substr( 0, m_szModel.size() - 4 ) + ".phy" ) != nullptr;
	m_pModelInfo->setText( tr( "%1 × %2 × %3 units\n%4" ).arg( size.m_flX, 0, 'g', 4 ).arg( size.m_flY, 0, 'g', 4 ).arg( size.m_flZ, 0, 'g', 4 ).arg( bPhysics ? tr( "Physics file available" ) : tr( "No physics file" ) ) );
	RefreshValues();
}

//-----------------------------------------------------------------------------
// Purpose: A mixed value has no selection. Picking a value applies it to each prop.
//-----------------------------------------------------------------------------
void CMapModelProperties::RefreshValues()
{
	const auto *pScene = m_Document.GetScene();
	if ( !pScene || m_Objects.empty() || m_Studios.empty() )
		return;
	const bool bEditable = !m_Document.IsReadOnly();
	const auto &first = pScene->m_Entities[ pScene->GetIndex( m_Objects.front() ) ];
	const bool bSameSkin = std::ranges::all_of( m_Objects, [ & ]( auto nObject ) {
		return pScene->m_Entities[ pScene->GetIndex( nObject ) ].m_nSkin == first.m_nSkin;
	} );
	m_pSkins->setEnabled( bEditable );
	m_pSkins->setCurrentRow( bSameSkin ? std::clamp( first.m_nSkin, 0, m_pSkins->count() - 1 ) : -1 );
	for ( const auto &[ nGroup, pParts ] : m_BodyChoices )
	{
		const auto &group = m_Studios.front().m_Bodygroups[ nGroup ];
		const int nCount = static_cast<int>( group.m_Models.size() );
		const int nPart = first.m_nBody / group.m_nBase % nCount;
		const bool bSamePart = std::ranges::all_of( m_Objects, [ & ]( auto nObject ) {
			return pScene->m_Entities[ pScene->GetIndex( nObject ) ].m_nBody / group.m_nBase % nCount == nPart;
		} );
		pParts->setEnabled( bEditable );
		pParts->setCurrentIndex( bSamePart ? nPart : -1 );
	}
	if ( m_nSequence < 0 )
	{
		const auto szDefault = pScene->m_Map.m_Entities[ pScene->GetIndex( m_Objects.front() ) ].GetValue( "DefaultAnim" );
		const bool bSameSequence = !szDefault.empty() && std::ranges::all_of( m_Objects, [ & ]( auto nObject ) {
			return xv::kv1::EqualsIgnoreCase( pScene->m_Map.m_Entities[ pScene->GetIndex( nObject ) ].GetValue( "DefaultAnim" ), szDefault );
		} );
		const auto matches = bSameSequence ? m_pSequences->findItems( QString::fromStdString( std::string( szDefault ) ), Qt::MatchFixedString ) : QList<QListWidgetItem *>{};
		m_pSequences->setCurrentItem( matches.empty() ? nullptr : matches.front() );
	}

	const std::pair selection{ first.m_nSkin, first.m_nBody };
	if ( m_MaterialSelection == selection )
		return;
	m_MaterialSelection = selection;
	m_pMaterials->clear();
	xv::model::Selection_t options;
	options.m_nSkin = selection.first;
	options.m_nBody = selection.second;
	if ( auto model = xv::model::LoadModel( m_szModel, m_pfnRead, nullptr, options ) )
		for ( const auto &szMaterial : model->m_Materials )
		{
			const QString szName = QString::fromStdString( szMaterial );
			QString szPath = szName.startsWith( QStringLiteral( "materials/" ) ) ? szName : QStringLiteral( "materials/" ) + szName;
			if ( !szPath.endsWith( QStringLiteral( ".vmt" ), Qt::CaseInsensitive ) )
				szPath += QStringLiteral( ".vmt" );
			auto *pItem = new QListWidgetItem( szName, m_pMaterials );
			pItem->setData( Qt::UserRole, szPath );
		}
}

//-----------------------------------------------------------------------------
// Purpose: Render skin icons incrementally using the browser's picture cache.
//-----------------------------------------------------------------------------
void CMapModelProperties::DrawNextSkin()
{
	if ( m_Studios.empty() || m_nNextSkin >= m_pSkins->count() )
	{
		m_pPictures->stop();
		return;
	}
	const int nSkin = m_nNextSkin++;
	const QImage image = CModelBrowser::SkinPicture( QString::fromStdString( m_szLoadedModel ), m_Studios.front().m_nChecksum, nSkin, m_pfnRead );
	if ( !image.isNull() )
		m_pSkins->item( nSkin )->setIcon( QIcon( QPixmap::fromImage( image ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: DefaultAnim is written only for classes which accept a named default sequence.
//-----------------------------------------------------------------------------
bool CMapModelProperties::HasDefaultAnimation( const xv::map::Entity_t &entity ) const
{
	const auto szClass = entity.GetClassname();
	if ( xv::kv1::EqualsIgnoreCase( szClass, "prop_dynamic" ) || xv::kv1::EqualsIgnoreCase( szClass, "prop_dynamic_override" ) )
		return true;
	const auto *pScene = m_Document.GetScene();
	const auto *pClass = pScene && pScene->m_pFgd ? pScene->m_pFgd->Find( szClass ) : nullptr;
	return pClass && pClass->FindKey( "DefaultAnim" );
}

//-----------------------------------------------------------------------------
// Purpose: Keep each group change or sequence choice together in the undo stack.
//-----------------------------------------------------------------------------
void CMapModelProperties::ApplyKey( std::string_view szKey, const QString &szUndo, const std::function<std::string( const xv::map::Entity_t & )> &pfnValue )
{
	const auto *pScene = m_Document.GetScene();
	if ( !pScene || m_Document.IsReadOnly() )
		return;
	xv::map::MapEdit_t edit;
	for ( const auto nObject : m_Objects )
	{
		const auto nEntity = pScene->GetIndex( nObject );
		const auto &entity = pScene->m_Map.m_Entities[ nEntity ];
		if ( szKey == "DefaultAnim" && !HasDefaultAnimation( entity ) )
			continue;
		const std::string szValue = pfnValue( entity );
		if ( entity.GetValue( szKey ) == szValue )
			continue;
		auto changed = entity;
		xv::map::SetKeyValue( pScene->m_Map, changed, szKey, szValue );
		edit.m_Entities.emplace_back( nEntity, std::move( changed ) );
	}
	if ( !edit.IsEmpty() )
		m_Document.Edit( std::move( edit ), szUndo );
}

//-----------------------------------------------------------------------------
// Purpose: Start the chosen sequence immediately and remember the named default on dynamic props.
//-----------------------------------------------------------------------------
void CMapModelProperties::ChooseSequence()
{
	const auto *pItem = m_pSequences->currentItem();
	if ( !pItem )
		return;
	const int nFile = pItem->data( SEQUENCE_FILE ).toInt();
	const int nSequence = pItem->data( SEQUENCE_NUMBER ).toInt();
	if ( m_nFile == nFile && m_nSequence == nSequence && m_pAnimate->isActive() )
		return;
	m_nFile = nFile;
	m_nSequence = nSequence;
	m_flCycle = m_flDuration = 0.0;
	{
		const QSignalBlocker block( m_pCycle );
		m_pCycle->setValue( 0 );
	}
	const auto &file = m_Studios[ m_nFile ];
	const auto &sequence = file.m_Sequences[ m_nSequence ];
	const auto szSequence = sequence.m_szName;
	if ( !sequence.m_Animations.empty() )
	{
		const int nAnimation = sequence.m_Animations.front();
		if ( nAnimation >= 0 && static_cast<std::size_t>( nAnimation ) < file.m_Animations.size() )
		{
			const auto &anim = file.m_Animations[ nAnimation ];
			if ( anim.m_flFps > 0.0f )
				m_flDuration = static_cast<double>( std::max( 1, anim.m_nFrames - 1 ) ) / anim.m_flFps;
		}
	}
	m_pAnimate->stop();
	m_pCycle->setEnabled( true );
	m_pPlay->setEnabled( m_flDuration > 0.0 );
	m_pPlay->setText( tr( "Play" ) );
	if ( m_pPlay->isEnabled() )
		m_pPlay->click();
	ApplyKey( "DefaultAnim", tr( "Change Default Animation" ), [ & ]( const auto & ) { return szSequence; } );
	InvalidatePose();
}

//-----------------------------------------------------------------------------
// Purpose: Advance in model time, wrapping the sequences marked as looping.
//-----------------------------------------------------------------------------
void CMapModelProperties::Advance()
{
	const auto *pFile = GetSequenceFile();
	if ( !pFile || m_flDuration <= 0.0 )
		return;
	m_flCycle += m_Clock.restart() / ( 1000.0 * m_flDuration );
	if ( m_flCycle >= 1.0 )
	{
		if ( pFile->m_Sequences[ m_nSequence ].m_nFlags & STUDIO_LOOPING )
			m_flCycle = std::fmod( m_flCycle, 1.0 );
		else
		{
			m_flCycle = 1.0;
			m_pAnimate->stop();
			m_pPlay->setText( tr( "Play" ) );
		}
	}
	const QSignalBlocker block( m_pCycle );
	m_pCycle->setValue( static_cast<int>( std::lround( m_flCycle * CYCLE_STEPS ) ) );
	InvalidatePose();
}

//-----------------------------------------------------------------------------
// Purpose: Redraw when a pose, selection or the preview's visibility changes.
//-----------------------------------------------------------------------------
void CMapModelProperties::InvalidatePose()
{
	m_PosedScene.reset();
	m_pBaseParts.reset();
	m_pPreviewParts.reset();
	emit PreviewChanged();
}

//-----------------------------------------------------------------------------
// Purpose: Locate the selected sequence in the model or one of its included files.
//-----------------------------------------------------------------------------
const xv::model::CStudioFile *CMapModelProperties::GetSequenceFile() const
{
	return m_nFile >= 0 && static_cast<std::size_t>( m_nFile ) < m_Studios.size() && m_nSequence >= 0 ? &m_Studios[ m_nFile ] : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Replace only the selected props in this frame with the same model in its sampled pose.
//-----------------------------------------------------------------------------
void CMapModelProperties::ApplyPreview( MapFrame_t &frame, const CMapResources &resources )
{
	const auto *pFile = GetSequenceFile();
	if ( !isVisible() || !pFile || !frame.m_pScene || !frame.m_pPropParts || m_Objects.empty() || frame.m_bEntityPreview )
		return;
	if ( m_PosedScene.lock() != frame.m_pScene || m_pBaseParts != frame.m_pPropParts )
	{
		const auto &scene = *frame.m_pScene;
		m_PosedScene = frame.m_pScene;
		m_pBaseParts = frame.m_pPropParts;
		auto pParts = std::make_shared<xv::map::MeshParts_t>( *m_pBaseParts );
		std::vector<float> parameters;
		for ( const auto &parameter : pFile->m_Parameters )
			parameters.push_back( std::clamp( 0.0f, std::min( parameter.m_flStart, parameter.m_flEnd ), std::max( parameter.m_flStart, parameter.m_flEnd ) ) );
		bool bPosed = false;
		bool bHasAllModels = true;
		for ( const auto nObject : m_Objects )
		{
			if ( nObject >= scene.GetObjectCount() || scene.IsSolidObject( nObject ) || scene.IsDeleted( nObject ) )
				continue;
			const auto &visual = scene.m_Entities[ scene.GetIndex( nObject ) ];
			if ( xv::vfs::NormalizePath( visual.m_szModel ) != m_szLoadedModel )
				continue;
			const auto *pModel = resources.FindPropModel( { m_szLoadedModel, visual.m_nSkin, visual.m_nBody } );
			if ( !pModel )
			{
				bHasAllModels = false;
				continue;
			}
			const auto pose = xv::model::SampleSequence( *pFile, m_nSequence, static_cast<float>( m_flCycle ), parameters, *pModel->m_pModel );
			if ( !pose )
				continue;
			auto posed = *pModel;
			posed.m_pModel = std::make_shared<xv::model::CModel>( xv::model::Deform( *pModel->m_pModel, *pose ) );
			auto angles = visual.m_Angles;
			angles[ 0 ] = visual.m_bInvertPitch ? -angles[ 0 ] : angles[ 0 ];
			const xv::map::PropPlacement_t placement{ nObject, 0, xv::map::Transform_t::FromAngles( visual.m_Origin, angles, visual.m_flModelScale ) };
			auto mesh = xv::map::BuildPropMesh( { &posed, 1 }, { &placement, 1 } );
			( *pParts )[ nObject ] = std::make_shared<xv::map::CMapMesh>( std::move( mesh ) );
			bPosed = true;
		}
		m_pPreviewParts = bPosed ? std::move( pParts ) : nullptr;
		if ( !bHasAllModels )
			m_pBaseParts.reset();
	}
	if ( m_pPreviewParts )
	{
		frame.m_pPropParts = m_pPreviewParts;
		frame.m_pPropMesh.reset();
	}
}
