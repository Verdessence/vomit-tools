//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: See ModelEditor.h.
//===========================================================================//
#include "ModelEditor.h"
#include "ModelWorkflow.h"
#include "ModelDocument.h"
#include "ModelView.h"
#include "ImageFiles.h"
#include "model/Anim.h"
#include "model/Binary.h"
#include "material/Material.h"
#include "material/Preview.h"
#include "vfs/Vfs.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QPainter>
#include <QStyleOptionSlider>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QUndoStack>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
using namespace xv;
namespace
{

class CModelTimeline : public QSlider
{
public:
	//-----------------------------------------------------------------------------
	// Purpose: Keeps timeline events visible while the playhead is scrubbed.
	//-----------------------------------------------------------------------------
	CModelTimeline()
		: QSlider( Qt::Horizontal )
	{}
	std::vector<float> m_Events;

protected:
	//-----------------------------------------------------------------------------
	// Purpose: Marks sequence events in the groove with small amber ticks.
	//-----------------------------------------------------------------------------
	void paintEvent( QPaintEvent *pEvent ) override
	{
		QSlider::paintEvent( pEvent );
		QStyleOptionSlider option;
		initStyleOption( &option );
		const auto groove = style()->subControlRect( QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this );
		const int nHandle = style()->pixelMetric( QStyle::PM_SliderLength, &option, this );
		QPainter painter( this );
		painter.setPen( QColor( 255, 195, 90 ) );
		for ( float flCycle : m_Events )
		{
			const int x = nHandle / 2 + qRound( flCycle * std::max( 1, width() - nHandle ) );
			painter.drawLine( x, groove.bottom() + 2, x, groove.bottom() + 7 );
		}
	}
};
//-----------------------------------------------------------------------------
// Purpose: Formats world coordinates consistently in the inspection panel.
//-----------------------------------------------------------------------------
QString VectorText( CVec3 v )
{
	return QStringLiteral( "%1 %2 %3" ).arg( v.m_flX ).arg( v.m_flY ).arg( v.m_flZ );
}
//-----------------------------------------------------------------------------
// Purpose: Adds a two-column inspection row without interpreting file strings as markup.
//-----------------------------------------------------------------------------
QTreeWidgetItem *Row( QTreeWidget *pTree, QString szName, QString szValue )
{
	return new QTreeWidgetItem( pTree, { szName, szValue } );
}
} // namespace
//-----------------------------------------------------------------------------
// Purpose: Opens a model, its files as an editable document, and constructs the controls.
//-----------------------------------------------------------------------------
CModelEditor::CModelEditor( QString szPath, model::Reader_t pfnRead, QWidget *pParent )
	: QWidget( pParent ),
	  m_szPath( std::move( szPath ) ),
	  m_pfnRead( std::move( pfnRead ) )
{
	setObjectName( QStringLiteral( "modelEditor" ) );
	const auto pCache = std::make_shared<std::map<std::string, std::optional<std::vector<std::byte>>>>();
	m_pfnRead = [ pCache, pfnSource = m_pfnRead ]( std::string_view szFile ) {auto [found,bNew]=pCache->try_emplace(std::string(szFile));if(bNew)found->second=pfnSource(szFile);return found->second; };
	std::string szError;
	auto file = model::ReadStudio( m_szPath.toStdString(), m_pfnRead, &szError );
	if ( !file )
	{
		m_szError = QString::fromStdString( szError );
		return;
	}
	// The files edits change; the viewer reads them through the document from here on.
	{
		const std::string szMdl = m_szPath.toStdString(), szStem = szMdl.substr( 0, szMdl.size() - 4 );
		model::Files_t files{ { ".mdl", *file->m_pBytes } };
		std::map<std::string, std::string> paths{ { ".mdl", szMdl } };
		for ( const std::string szKind : { ".vvd", ".dx90.vtx", ".dx80.vtx", ".sw.vtx", ".vtx", ".phy" } )
			if ( auto bytes = m_pfnRead( szStem + szKind ) )
				files[ szKind ] = std::move( *bytes );
		if ( file->m_pAnimationBytes )
		{
			files[ ".ani" ] = *file->m_pAnimationBytes;
			paths[ ".ani" ] = m_pfnRead( file->m_szAnimationFile ) ? file->m_szAnimationFile : "models/" + file->m_szAnimationFile;
		}
		m_pDocument = new CModelDocument( szMdl, std::move( files ), std::move( paths ), this );
		m_pfnRead = [ this, pfnCached = m_pfnRead ]( std::string_view szFile ) {
			if ( auto bytes = m_pDocument->Read( szFile ) )
				return bytes;
			return pfnCached( szFile );
		};
		connect( m_pDocument, &CModelDocument::Changed, this, &CModelEditor::Reload );
		connect( m_pDocument, &CModelDocument::ModifiedChanged, this, &CModelEditor::ModifiedChanged );
	}
	m_Files = model::LoadIncludes( *file, m_pfnRead, m_Problems );
	m_Files.insert( m_Files.begin(), std::move( *file ) );
	m_pView = new CModelView;
	m_pStatus = new QLabel;
	m_pStatus->setWordWrap( true );
	m_pStatus->setTextFormat( Qt::PlainText );
	auto *pRoot = new QVBoxLayout( this );
	auto *pSplitter = new QSplitter;
	pRoot->addWidget( pSplitter, 1 );
	pRoot->addWidget( m_pStatus );
	auto *pLeft = new QWidget;
	auto *pViewLayout = new QVBoxLayout( pLeft );
	pViewLayout->setContentsMargins( 0, 0, 0, 0 );
	pViewLayout->addWidget( m_pView, 1 );
	auto *pOverlays = new QHBoxLayout;
	std::vector<QCheckBox *> overlays;
	for ( const auto *szName : { "Wireframe", "Normals", "Bones", "Hitboxes", "Attachments", "Bounds", "Eye / light", "Grid" } )
	{
		auto *p = new QCheckBox( tr( szName ) );
		overlays.push_back( p );
		pOverlays->addWidget( p );
	}
	overlays.back()->setChecked( true );
	for ( auto *p : overlays )
		connect( p, &QCheckBox::toggled, this, [ this, overlays ] { m_pView->SetOverlays( overlays[ 0 ]->isChecked(), overlays[ 1 ]->isChecked(), overlays[ 2 ]->isChecked(), overlays[ 3 ]->isChecked(), overlays[ 4 ]->isChecked(), overlays[ 5 ]->isChecked(), overlays[ 6 ]->isChecked(), overlays[ 7 ]->isChecked() ); } );
	pViewLayout->addLayout( pOverlays );
	auto *pTransport = new QHBoxLayout;
	m_pPlay = new QPushButton( tr( "Play" ) );
	m_pPlay->setCheckable( true );
	m_pPlay->setObjectName( QStringLiteral( "modelPlay" ) );
	m_pTimeline = new CModelTimeline;
	m_pTimeline->setRange( 0, 10000 );
	m_pTimeline->setObjectName( QStringLiteral( "modelTimeline" ) );
	m_pLoop = new QCheckBox( tr( "Loop" ) );
	m_pRate = new QDoubleSpinBox;
	m_pRate->setRange( 0.05, 4 );
	m_pRate->setSingleStep( 0.1 );
	m_pRate->setValue( 1 );
	m_pRate->setSuffix( tr( "×" ) );
	m_pPlayback = new QLabel;
	m_pPlayback->setMinimumWidth( 180 );
	pTransport->addWidget( m_pPlay );
	pTransport->addWidget( m_pTimeline, 1 );
	pTransport->addWidget( m_pPlayback );
	pTransport->addWidget( m_pLoop );
	pTransport->addWidget( m_pRate );
	pViewLayout->addLayout( pTransport );
	m_pTimer = new QTimer( this );
	m_pTimer->setInterval( 33 );
	connect( m_pTimer, &QTimer::timeout, this, &CModelEditor::Tick );
	connect( m_pPlay, &QPushButton::toggled, this, [ this ]( bool bPlay ) {if(bPlay){m_PlaybackClock.restart();m_pTimer->start();}else m_pTimer->stop();m_pPlay->setText(bPlay?tr("Pause"):tr("Play")); } );
	connect( m_pTimeline, &QSlider::valueChanged, this, [ this ]( int nValue ) {m_flCycle=nValue/10000.0f;Sample(); } );
	auto *pTabs = new QTabWidget;
	pTabs->setMinimumWidth( 310 );
	auto *pBody = new QWidget;
	auto *pBodyForm = new QFormLayout( pBody );
	m_pSkin = new QComboBox;
	m_pSkin->setObjectName( QStringLiteral( "modelSkin" ) );
	for ( std::size_t i = 0; i < std::max<std::size_t>( 1, m_Files[ 0 ].m_Skins.size() ); i++ )
		m_pSkin->addItem( tr( "Skin %1" ).arg( i ) );
	pBodyForm->addRow( tr( "Skin" ), m_pSkin );
	m_pLod = new QComboBox;
	m_pLod->setObjectName( QStringLiteral( "modelLod" ) );
	int nLods = 1;
	for ( const auto *szExt : { ".dx90.vtx", ".dx80.vtx", ".sw.vtx", ".vtx" } )
		if ( auto bytes = m_pfnRead( m_szPath.toStdString().substr( 0, m_szPath.size() - 4 ) + szExt ) )
		{
			try
			{
				nLods = std::clamp( model::CBinary( *bytes ).Int( 20 ), 1, 8 );
			}
			catch ( const std::exception & )
			{}
			break;
		}
	for ( int i = 0; i < nLods; i++ )
		m_pLod->addItem( tr( "LOD %1" ).arg( i ) );
	pBodyForm->addRow( tr( "Detail level" ), m_pLod );
	for ( const auto &group : m_Files[ 0 ].m_Bodygroups )
	{
		auto *pChoice = new QComboBox;
		for ( const auto &szName : group.m_Models )
			pChoice->addItem( QString::fromStdString( szName ) );
		m_Bodygroups.push_back( pChoice );
		pBodyForm->addRow( QString::fromStdString( group.m_szName ), pChoice );
		connect( pChoice, &QComboBox::currentIndexChanged, this, &CModelEditor::ReloadMesh );
	}
	std::string szPhysicsError;
	if ( auto bytes = m_pfnRead( m_szPath.toStdString().substr( 0, m_szPath.size() - 4 ) + ".phy" ) )
	{
		m_Physics = model::ReadPhysics( *bytes, &szPhysicsError );
		if ( !m_Physics )
			m_Problems.push_back( "PHY: " + szPhysicsError );
	}
	m_pPhysicsShown = new QCheckBox( tr( "Physics hull" ) );
	m_pPhysicsShown->setEnabled( m_Physics.has_value() );
	pBodyForm->addRow( m_pPhysicsShown );
	connect( m_pPhysicsShown, &QCheckBox::toggled, this, [ this ]( bool bShow ) { m_pView->SetPhysics( m_Physics ? &*m_Physics : nullptr, bShow ); } );
	m_pHitbox = new QComboBox;
	for ( const auto &set : m_Files[ 0 ].m_HitboxSets )
		m_pHitbox->addItem( QString::fromStdString( set.m_szName ) );
	pBodyForm->addRow( tr( "Hitbox set" ), m_pHitbox );
	connect( m_pHitbox, &QComboBox::currentIndexChanged, this, &CModelEditor::Sample );
	connect( m_pHitbox, &QComboBox::currentIndexChanged, this, [ this ] {
		m_pView->SetHighlight( -1, -1 );
		RefreshPanels();
	} );
	connect( m_pSkin, &QComboBox::currentIndexChanged, this, &CModelEditor::ReloadMesh );
	connect( m_pLod, &QComboBox::currentIndexChanged, this, &CModelEditor::ReloadMesh );
	auto *pFrame = new QPushButton( tr( "Frame Model" ) );
	pBodyForm->addRow( pFrame );
	connect( pFrame, &QPushButton::clicked, m_pView, &CModelView::Frame );
	auto *pScreenshot = new QPushButton( tr( "Save Screenshot…" ) );
	pBodyForm->addRow( pScreenshot );
	connect( pScreenshot, &QPushButton::clicked, this, [ this ] {const QString szFile=QFileDialog::getSaveFileName(this,tr("Save Model Screenshot"),{},tr("PNG image (*.png)"));if(!szFile.isEmpty()&&!m_pView->GetImage(m_pView->size()).save(szFile))m_pStatus->setText(tr("Could not save screenshot.")); } );
	auto *pWorkflow = new QPushButton( tr( "Sources, Build and Export…" ) );
	pWorkflow->setObjectName( QStringLiteral( "modelWorkflowButton" ) );
	pBodyForm->addRow( pWorkflow );
	connect( pWorkflow, &QPushButton::clicked, this, &CModelEditor::WorkflowRequested );
	pTabs->addTab( pBody, tr( "Body" ) );
	auto *pSequences = new QWidget;
	auto *pSequenceLayout = new QVBoxLayout( pSequences );
	auto *pSearch = new QLineEdit;
	pSearch->setPlaceholderText( tr( "Search sequences or activities" ) );
	pSequenceLayout->addWidget( pSearch );
	m_pSequences = new QListWidget;
	m_pSequences->setObjectName( QStringLiteral( "modelSequences" ) );
	// The list, its events and the editor share the height; the divider moves.
	auto *pSequenceSplit = new QSplitter( Qt::Vertical );
	pSequenceSplit->setChildrenCollapsible( false );
	pSequenceSplit->addWidget( m_pSequences );
	pSequenceLayout->addWidget( pSequenceSplit, 1 );
	for ( std::size_t f = 0; f < m_Files.size(); f++ )
		for ( std::size_t i = 0; i < m_Files[ f ].m_Sequences.size(); i++ )
		{
			const auto &seq = m_Files[ f ].m_Sequences[ i ];
			auto *pItem = new QListWidgetItem( QString::fromStdString( seq.m_szName ), m_pSequences );
			pItem->setData( Qt::UserRole, QPoint( f, i ) );
			pItem->setToolTip( QString::fromStdString( m_Files[ f ].m_szPath + "\n" + seq.m_szActivity ) );
		}
	connect( pSearch, &QLineEdit::textChanged, this, [ this ]( const QString &szText ) {for(int i=0;i<m_pSequences->count();i++){auto *p=m_pSequences->item(i);p->setHidden(!p->text().contains(szText,Qt::CaseInsensitive)&&!p->toolTip().contains(szText,Qt::CaseInsensitive));} } );
	connect( m_pSequences, &QListWidget::currentRowChanged, this, &CModelEditor::SelectSequence );
	m_pEvents = new QTreeWidget;
	m_pEvents->setHeaderLabels( { tr( "Frame / event" ), tr( "Options" ) } );
	pSequenceSplit->addWidget( m_pEvents );
	auto *pSequenceEditor = new QScrollArea;
	pSequenceEditor->setWidgetResizable( true );
	pSequenceEditor->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
	pSequenceEditor->setWidget( BuildSequenceEditor() );
	pSequenceSplit->addWidget( pSequenceEditor );
	pSequenceSplit->setSizes( { 220, 90, 380 } );
	auto *pPose = new QWidget;
	auto *pPoseForm = new QFormLayout( pPose );
	// Sliders retain named values across included models; different files can have different tables.
	for ( const auto &source : m_Files )
		for ( const auto &parameter : source.m_Parameters )
			if ( std::ranges::none_of( m_PoseControls, [ & ]( const auto &control ) { return control.m_szName == parameter.m_szName; } ) )
				m_PoseControls.push_back( parameter );
	for ( const auto &param : m_PoseControls )
	{
		auto *p = new QDoubleSpinBox;
		p->setRange( std::min( param.m_flStart, param.m_flEnd ), std::max( param.m_flStart, param.m_flEnd ) );
		p->setValue( std::clamp( 0.0, p->minimum(), p->maximum() ) );
		p->setSingleStep( ( p->maximum() - p->minimum() ) / 100 );
		m_Parameters.push_back( p );
		auto *pRow = new QWidget;
		auto *pRowLayout = new QHBoxLayout( pRow );
		pRowLayout->setContentsMargins( 0, 0, 0, 0 );
		auto *pSlider = new QSlider( Qt::Horizontal );
		pSlider->setRange( 0, 1000 );
		const double flStart = p->minimum(), flSpan = p->maximum() - flStart;
		pSlider->setValue( flSpan > 0 ? qRound( ( p->value() - flStart ) / flSpan * 1000 ) : 0 );
		connect( pSlider, &QSlider::valueChanged, p, [ p, flStart, flSpan ]( int n ) { p->setValue( flStart + flSpan * n / 1000.0 ); } );
		connect( p, qOverload<double>( &QDoubleSpinBox::valueChanged ), pSlider, [ pSlider, flStart, flSpan ]( double flValue ) {const QSignalBlocker block(pSlider);pSlider->setValue(flSpan>0?qRound((flValue-flStart)/flSpan*1000):0); } );
		pRowLayout->addWidget( pSlider, 1 );
		pRowLayout->addWidget( p );
		pPoseForm->addRow( QString::fromStdString( param.m_szName ), pRow );
		connect( p, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, &CModelEditor::Sample );
	}
	auto *pPoseScroll = new QScrollArea;
	pPoseScroll->setWidgetResizable( true );
	pPoseScroll->setWidget( pPose );
	pPoseScroll->setMaximumHeight( 210 );
	pPoseScroll->setMinimumHeight( m_PoseControls.empty() ? 0 : 80 );
	pPoseScroll->setVisible( !m_PoseControls.empty() );
	pSequenceLayout->addWidget( pPoseScroll );
	pTabs->addTab( pSequences, tr( "Sequences" ) );
	pTabs->addTab( BuildModelPanel(), tr( "Model" ) );
	pTabs->addTab( BuildMaterialPanel(), tr( "Materials" ) );
	pTabs->addTab( BuildHitboxPanel(), tr( "Hitboxes" ) );
	pTabs->addTab( BuildAttachmentPanel(), tr( "Attachments" ) );
	pTabs->addTab( BuildSkeletonPanel(), tr( "Skeleton" ) );
	pTabs->addTab( BuildTransformPanel(), tr( "Transform" ) );
	m_pInfo = new QTreeWidget;
	m_pInfo->setHeaderLabels( { tr( "Property" ), tr( "Value" ) } );
	m_pInfo->setColumnWidth( 0, 150 );
	pTabs->addTab( m_pInfo, tr( "Info" ) );
	connect( m_pInfo, &QTreeWidget::itemDoubleClicked, this, [ this ]( QTreeWidgetItem *pItem, int ) {const QString szMaterial=pItem->data(0,Qt::UserRole).toString();if(!szMaterial.isEmpty())emit OpenMaterialRequested(szMaterial); } );
	m_pChecks = new QTreeWidget;
	m_pChecks->setHeaderLabels( { tr( "Check" ), tr( "Details" ) } );
	pTabs->addTab( m_pChecks, tr( "Checks" ) );
	pSplitter->addWidget( pLeft );
	pSplitter->addWidget( pTabs );
	pSplitter->setStretchFactor( 0, 1 );
	pSplitter->setSizes( { 800, 360 } );
	ReloadMesh();
	PopulateInfo();
	m_pView->Frame();
	if ( m_Files[ 0 ].m_Bodygroups.empty() )
		overlays[ 2 ]->setChecked( true );
	m_pPlay->setEnabled( false );
	m_pTimeline->setEnabled( m_pSequences->count() > 0 );
	RefreshPanels();
}
//-----------------------------------------------------------------------------
// Purpose: Loads only the selected body, skin and LOD, leaving file bytes untouched.
//-----------------------------------------------------------------------------
void CModelEditor::ReloadMesh()
{
	m_Selection.m_nSkin = m_pSkin->currentIndex();
	m_Selection.m_nLod = m_pLod->currentIndex();
	m_Selection.m_Bodygroups.clear();
	for ( auto *p : m_Bodygroups )
		m_Selection.m_Bodygroups.push_back( p->currentIndex() );
	std::string szError;
	m_Model = model::LoadModel( m_szPath.toStdString(), m_pfnRead, &szError, m_Selection );
	if ( !m_Model && m_Files[ 0 ].m_Bodygroups.empty() )
	{
		model::CModel skeleton;
		for ( const auto &bone : m_Files[ 0 ].m_Bones )
			skeleton.m_Bones.push_back( { bone.m_szName, bone.m_nParent, bone.m_PoseToBone.Inverse().value_or( model::CTransform{} ) } );
		if ( !m_Files[ 0 ].m_Sequences.empty() )
			if ( auto pose = model::SampleSequence( m_Files[ 0 ], 0, 0, {}, skeleton ) )
				for ( std::size_t i = 0; i < pose->size(); i++ )
					skeleton.m_Bones[ i ].m_ToModel = ( *pose )[ i ];
		m_Model = std::move( skeleton );
	}
	m_pView->ForgetTextures();
	m_Sprites.clear();
	m_Materials.clear();
	if ( m_Model )
		LoadModelMaterials( *m_Model, m_pfnRead, m_Sprites, m_Materials );
	m_pView->SetResources( m_Model ? &*m_Model : nullptr, &m_Materials );
	m_pStatus->setText( m_Model ? tr( "%1 vertices · %2 triangles" ).arg( m_Model->m_Vertices.size() ).arg( m_Model->GetTriangleCount() ) : QString::fromStdString( szError ) );
	Sample();
}
//-----------------------------------------------------------------------------
// Purpose: Resets the timeline and presents the selected sequence's event markers.
//-----------------------------------------------------------------------------
void CModelEditor::SelectSequence()
{
	const auto *pItem = m_pSequences->currentItem();
	if ( !pItem )
		return;
	const QPoint index = pItem->data( Qt::UserRole ).toPoint();
	m_pPlay->setEnabled( true );
	m_nSource = index.x();
	m_nSequence = index.y();
	m_flCycle = 0;
	const auto &file = m_Files[ m_nSource ];
	const auto &seq = file.m_Sequences[ m_nSequence ];
	m_pLoop->setChecked( seq.m_nFlags & 1 );
	m_pEvents->clear();
	int nFrames = 1;
	if ( !seq.m_Animations.empty() && seq.m_Animations[ 0 ] >= 0 && seq.m_Animations[ 0 ] < static_cast<int>( file.m_Animations.size() ) )
		nFrames = file.m_Animations[ seq.m_Animations[ 0 ] ].m_nFrames;
	auto *pTimeline = static_cast<CModelTimeline *>( m_pTimeline );
	pTimeline->m_Events.clear();
	for ( const auto &event : seq.m_Events )
	{
		if ( event.m_bValidCycle )
			pTimeline->m_Events.push_back( event.m_flCycle );
		auto *p = Row( m_pEvents, QStringLiteral( "%1 · %2" ).arg( event.m_flCycle * std::max( 0, nFrames - 1 ), 0, 'f', 1 ).arg( event.m_szName.empty() ? QString::number( event.m_nEvent ) : QString::fromStdString( event.m_szName ) ), QString::fromStdString( event.m_szOptions ) );
		p->setData( 0, Qt::UserRole, event.m_flCycle );
		if ( !event.m_bValidCycle )
			p->setText( 0, tr( "Unspecified frame · %1" ).arg( event.m_nEvent ) );
	}
	disconnect( m_pEvents, &QTreeWidget::itemClicked, this, nullptr );
	connect( m_pEvents, &QTreeWidget::itemClicked, this, [ this ]( QTreeWidgetItem *p, int ) { m_pTimeline->setValue( qRound( p->data( 0, Qt::UserRole ).toFloat() * 10000 ) ); } );
	if ( m_RefreshSequence )
	{
		m_bRefreshing = true;
		m_RefreshSequence();
		m_bRefreshing = false;
	}
	Sample();
}
//-----------------------------------------------------------------------------
// Purpose: Samples the pose and updates the mesh and overlays from the same skeleton.
//-----------------------------------------------------------------------------
void CModelEditor::Sample()
{
	if ( !m_Model )
		return;
	if ( m_pHitbox->currentIndex() >= 0 && m_pHitbox->currentIndex() < static_cast<int>( m_Files[ 0 ].m_HitboxSets.size() ) )
		m_Model->m_Hitboxes = m_Files[ 0 ].m_HitboxSets[ m_pHitbox->currentIndex() ].m_Boxes;
	m_pStatus->setText( tr( "%1 vertices · %2 triangles" ).arg( m_Model->m_Vertices.size() ).arg( m_Model->GetTriangleCount() ) );
	m_Posed = *m_Model;
	if ( m_nSequence >= 0 )
	{
		const auto &file = m_Files[ m_nSource ];
		std::vector<float> parameters;
		for ( const auto &param : file.m_Parameters )
		{
			float flValue = std::clamp( 0.0f, std::min( param.m_flStart, param.m_flEnd ), std::max( param.m_flStart, param.m_flEnd ) );
			for ( std::size_t i = 0; i < m_PoseControls.size(); i++ )
				if ( m_PoseControls[ i ].m_szName == param.m_szName )
					flValue = m_Parameters[ i ]->value();
			parameters.push_back( flValue );
		}
		std::string szError;
		if ( auto pose = model::SampleSequence( file, m_nSequence, m_flCycle, parameters, *m_Model, &szError ) )
			m_Posed = model::Deform( *m_Model, *pose );
		else
		{
			m_pPlay->setChecked( false );
			m_pStatus->setText( QString::fromStdString( szError ) );
		}
		const auto &seq = file.m_Sequences[ m_nSequence ];
		int nFrames = 1;
		float flFps = 0;
		if ( !seq.m_Animations.empty() && seq.m_Animations[ 0 ] >= 0 && seq.m_Animations[ 0 ] < static_cast<int>( file.m_Animations.size() ) )
		{
			const auto &anim = file.m_Animations[ seq.m_Animations[ 0 ] ];
			nFrames = anim.m_nFrames;
			flFps = anim.m_flFps;
		}
		m_pPlayback->setText( tr( "Frame %1 / %2 · %3 fps" ).arg( m_flCycle * std::max( 0, nFrames - 1 ), 0, 'f', 1 ).arg( std::max( 0, nFrames - 1 ) ).arg( flFps ) );
		if ( seq.m_bHasLayers )
			m_pStatus->setText( tr( "This sequence has animation layers; only its base blend is displayed." ) );
	}
	const QSignalBlocker blocker( m_pTimeline );
	m_pTimeline->setValue( qRound( m_flCycle * 10000 ) );
	m_pView->SetModel( &*m_Posed, &m_Files[ 0 ], &m_Sprites );
}
//-----------------------------------------------------------------------------
// Purpose: Advances by real elapsed time so slow software drawing does not alter playback speed.
//-----------------------------------------------------------------------------
void CModelEditor::Tick()
{
	const float flStep = static_cast<float>( m_PlaybackClock.restart() ) / 1000.0f;
	if ( m_nSequence < 0 )
		return;
	const auto &file = m_Files[ m_nSource ];
	const auto &seq = file.m_Sequences[ m_nSequence ];
	if ( seq.m_Animations.empty() )
		return;
	const int i = seq.m_Animations[ 0 ];
	if ( i < 0 || i >= static_cast<int>( file.m_Animations.size() ) )
		return;
	const auto &anim = file.m_Animations[ i ];
	m_flCycle += flStep * anim.m_flFps * m_pRate->value() / std::max( 1, anim.m_nFrames - 1 );
	if ( m_flCycle > 1 )
	{
		if ( m_pLoop->isChecked() )
			m_flCycle = std::fmod( m_flCycle, 1.0f );
		else
		{
			m_flCycle = 1;
			m_pPlay->setChecked( false );
		}
	}
	Sample();
}
//-----------------------------------------------------------------------------
// Purpose: Reports file provenance, materials and invalid references without suppressing the mesh.
//-----------------------------------------------------------------------------
void CModelEditor::PopulateInfo()
{
	const auto &file = m_Files[ 0 ];
	if ( m_Physics )
	{
		Row( m_pInfo, tr( "Physics solids" ), QString::number( m_Physics->m_nSolids ) );
		Row( m_pInfo, tr( "Physics convex pieces" ), QString::number( m_Physics->m_Convexes.size() ) );
		Row( m_pInfo, tr( "Physics properties" ), QString::fromStdString( m_Physics->m_szText ) );
	}
	Row( m_pInfo, tr( "Name" ), QString::fromStdString( file.m_szName ) );
	Row( m_pInfo, tr( "MDL version" ), QString::number( file.m_nVersion ) );
	Row( m_pInfo, tr( "Checksum" ), QString::number( static_cast<std::uint32_t>( file.m_nChecksum ), 16 ) );
	Row( m_pInfo, tr( "Flags" ), QStringLiteral( "0x%1%2" ).arg( file.m_nFlags, 0, 16 ).arg( file.m_nFlags & 16 ? tr( " · static prop" ) : QString{} ) );
	Row( m_pInfo, tr( "Surface property" ), QString::fromStdString( file.m_szSurface ) );
	Row( m_pInfo, tr( "Mass" ), QString::number( file.m_flMass ) );
	Row( m_pInfo, tr( "Contents" ), QString::number( file.m_nContents, 16 ) );
	Row( m_pInfo, tr( "Eye" ), VectorText( file.m_vecEye ) );
	Row( m_pInfo, tr( "Illumination" ), VectorText( file.m_vecIllumination ) );
	Row( m_pInfo, tr( "Hull" ), VectorText( file.m_vecHullMin ) + " → " + VectorText( file.m_vecHullMax ) );
	Row( m_pInfo, tr( "View bounds" ), VectorText( file.m_vecViewMin ) + " → " + VectorText( file.m_vecViewMax ) );
	for ( const auto &szFolder : file.m_Folders )
		Row( m_pInfo, tr( "Material folder" ), QString::fromStdString( szFolder ) );
	for ( const auto &source : m_Files )
		Row( m_pInfo, QString::fromStdString( source.m_szPath ), tr( "%1 bytes · %2 bones · %3 sequences" ).arg( source.m_pBytes->size() ).arg( source.m_Bones.size() ).arg( source.m_Sequences.size() ) );
	const std::string szBase = m_szPath.toStdString().substr( 0, m_szPath.size() - 4 );
	for ( const auto *szExt : { ".vvd", ".dx90.vtx", ".dx80.vtx", ".sw.vtx", ".vtx", ".phy" } )
		if ( auto bytes = m_pfnRead( szBase + szExt ) )
		{
			Row( m_pInfo, QString::fromUtf8( szExt ), tr( "%1 bytes" ).arg( bytes->size() ) );
			try
			{
				const model::CBinary r( *bytes );
				const int nChecksum = r.Int( std::string_view( szExt ) == ".vvd" ? 8 : std::string_view( szExt ) == ".phy" ? 12
																														   : 16 );
				if ( nChecksum != file.m_nChecksum )
					Row( m_pChecks, tr( "Checksum mismatch" ), QString::fromUtf8( szExt ) );
			}
			catch ( const std::exception &e )
			{
				Row( m_pChecks, QString::fromUtf8( szExt ), QString::fromUtf8( e.what() ) );
			}
		}
	if ( m_Model )
	{
		for ( std::size_t i = 0; i < m_Model->m_Materials.size(); i++ )
		{
			const auto szPath = "materials/" + m_Model->m_Materials[ i ];
			const bool bExists = bool( m_pfnRead( szPath ) );
			auto *p = Row( m_pInfo, tr( "Material %1" ).arg( i ), QString::fromStdString( szPath ) + ( bExists ? QString{} : tr( " · missing" ) ) );
			p->setData( 0, Qt::UserRole, QString::fromStdString( szPath ) );
			if ( !bExists )
				Row( m_pChecks, tr( "Missing material" ), QString::fromStdString( szPath ) );
			if ( auto bytes = m_pfnRead( szPath ) )
				if ( auto material = material::ParseMaterial( std::string_view( reinterpret_cast<const char *>( bytes->data() ), bytes->size() ), m_pfnRead ) )
				{
					const auto textures = material::CTextureSet::Load( *material, m_pfnRead, 32, 1 );
					for ( const auto &texture : textures.GetTextures() )
						Row( m_pInfo, QString::fromStdString( texture.m_szParameter ), QString::fromStdString( texture.m_szPath ) );
				}
		}
		for ( const auto &vertex : m_Model->m_Vertices )
			for ( int i = 0; i < vertex.m_nBones; i++ )
				if ( vertex.m_Bones[ i ] < 0 || vertex.m_Bones[ i ] >= static_cast<int>( m_Model->m_Bones.size() ) )
				{
					Row( m_pChecks, tr( "Invalid vertex bone" ), QString::number( vertex.m_Bones[ i ] ) );
					break;
				}
	}
	for ( const auto &set : file.m_HitboxSets )
		for ( const auto &box : set.m_Boxes )
			if ( box.m_nBone < 0 || box.m_nBone >= static_cast<int>( file.m_Bones.size() ) )
				Row( m_pChecks, tr( "Invalid hitbox bone" ), QString::fromStdString( set.m_szName ) + ": " + QString::number( box.m_nBone ) );
	for ( const auto &skin : file.m_Skins )
	{
		if ( skin.empty() )
			Row( m_pChecks, tr( "Empty skin" ), {} );
		for ( int nMaterial : skin )
			if ( nMaterial < 0 || ( m_Model && nMaterial >= static_cast<int>( m_Model->m_Materials.size() ) ) )
				Row( m_pChecks, tr( "Invalid skin material" ), QString::number( nMaterial ) );
	}
	if ( ( file.m_nFlags & 16 ) && std::ranges::any_of( file.m_Animations, []( const auto &a ) { return a.m_nFrames > 1; } ) )
		Row( m_pChecks, tr( "Static prop has animation" ), {} );
	for ( const auto &szProblem : file.m_Problems )
		Row( m_pChecks, tr( "Sequence" ), QString::fromStdString( szProblem ) );
	for ( const auto &szProblem : m_Problems )
		Row( m_pChecks, tr( "Include" ), QString::fromStdString( szProblem ) );
	for ( const auto &source : m_Files )
		for ( const auto &seq : source.m_Sequences )
		{
			auto *p = Row( m_pInfo, tr( "Sequence" ), QString::fromStdString( seq.m_szName ) );
			new QTreeWidgetItem( p, { tr( "Activity" ), QString::fromStdString( seq.m_szActivity ) } );
			new QTreeWidgetItem( p, { tr( "Activity weight" ), QString::number( seq.m_nWeight ) } );
			new QTreeWidgetItem( p, { tr( "Fades" ), tr( "%1 in · %2 out" ).arg( seq.m_flFadeIn ).arg( seq.m_flFadeOut ) } );
			new QTreeWidgetItem( p, { tr( "Blends" ), tr( "%1 × %2" ).arg( seq.m_Size[ 0 ] ).arg( seq.m_Size[ 1 ] ) } );
			for ( int nAnim : seq.m_Animations )
				if ( nAnim < 0 || nAnim >= static_cast<int>( source.m_Animations.size() ) )
				{
					Row( m_pChecks, tr( "Invalid sequence animation" ), QString::fromStdString( source.m_szPath + ": " + seq.m_szName ) );
					break;
				}
		}
	for ( int i = 0; i < m_pLod->count(); i++ )
	{
		auto choice = m_Selection;
		choice.m_nLod = i;
		std::string szError;
		if ( auto lod = model::LoadModel( m_szPath.toStdString(), m_pfnRead, &szError, choice ) )
		{
			std::set<std::uint32_t> indices;
			for ( const auto &mesh : lod->m_Meshes )
				indices.insert( mesh.m_Indices.begin(), mesh.m_Indices.end() );
			Row( m_pInfo, tr( "LOD %1" ).arg( i ), tr( "%1 vertices · %2 triangles" ).arg( indices.size() ).arg( lod->GetTriangleCount() ) );
		}
	}

	for ( const auto &szFlex : file.m_Flexes )
		Row( m_pInfo, tr( "Flex" ), QString::fromStdString( szFlex ) );
	for ( const auto &eye : file.m_Eyeballs )
		Row( m_pInfo, tr( "Eyeball" ), QString::fromStdString( eye.m_szName ) + " · " + VectorText( eye.m_vecOrigin ) );
	for ( const auto &bone : file.m_Bones )
		if ( bone.m_nProcedure )
			Row( m_pInfo, tr( "Procedural bone" ), QString::fromStdString( bone.m_szName ) + " · " + QString::number( bone.m_nProcedure ) );
	for ( const auto &c : file.m_Controllers )
		Row( m_pInfo, tr( "Bone controller" ), tr( "Bone %1 · type %2 · %3 to %4" ).arg( c.m_nBone ).arg( c.m_nType ).arg( c.m_flStart ).arg( c.m_flEnd ) );
	if ( !file.m_szKeyValues.empty() )
		Row( m_pInfo, tr( "KeyValues" ), QString::fromStdString( file.m_szKeyValues ) );
	if ( !m_pChecks->topLevelItemCount() )
		Row( m_pChecks, tr( "No problems found" ), {} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CModelEditor::IsModified() const
{
	return m_pDocument && m_pDocument->IsModified();
}

QString CModelEditor::GetStatus() const
{
	return m_pStatus->text();
}

void CModelEditor::SetSurfaceProps( const QStringList &names )
{
	m_SurfaceProps = names;
	RefreshPanels();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CModelEditor::Commit( const QString &szName, const std::function<void( model::Files_t & )> &pfnEdit )
{
	const QString szError = m_pDocument->Edit( szName, pfnEdit );
	if ( szError.isEmpty() )
		return true;
	m_pStatus->setText( tr( "Not changed: %1" ).arg( szError ) );
	// Puts back what the panels showed before the refused value.
	RefreshPanels();
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Each panel fills itself from the files again; their signals wait until it's done.
//-----------------------------------------------------------------------------
void CModelEditor::RefreshPanels()
{
	if ( m_bRefreshing )
		return;
	m_bRefreshing = true;
	for ( const auto &pfnRefresh : m_Refreshers )
		pfnRefresh();
	m_bRefreshing = false;
}

//-----------------------------------------------------------------------------
// Purpose: After an edit, undo or redo: the model read again from the document's files, keeping
//          what's picked (skin, bodygroups, sequence and its frame).
//-----------------------------------------------------------------------------
void CModelEditor::Reload()
{
	std::string szError;
	auto file = model::ReadStudio( m_szPath.toStdString(), m_pfnRead, &szError );
	if ( !file )
	{
		m_pStatus->setText( tr( "The edited model doesn't read: %1" ).arg( QString::fromStdString( szError ) ) );
		return;
	}
	m_Files[ 0 ] = std::move( *file );
	const model::CStudioFile &studio = m_Files[ 0 ];
	m_Physics.reset();
	if ( m_pDocument->Has( ".phy" ) )
		m_Physics = model::ReadPhysics( m_pDocument->GetFiles().at( ".phy" ) );
	m_pView->SetPhysics( m_Physics ? &*m_Physics : nullptr, m_pPhysicsShown->isChecked() );
	{
		const QSignalBlocker block( m_pSkin );
		const int nSkin = m_pSkin->currentIndex();
		m_pSkin->clear();
		for ( std::size_t i = 0; i < std::max<std::size_t>( 1, studio.m_Skins.size() ); i++ )
			m_pSkin->addItem( tr( "Skin %1" ).arg( i ) );
		m_pSkin->setCurrentIndex( std::clamp( nSkin, 0, m_pSkin->count() - 1 ) );
	}
	for ( std::size_t g = 0; g < m_Bodygroups.size() && g < studio.m_Bodygroups.size(); g++ )
		for ( int m = 0; m < m_Bodygroups[ g ]->count() && m < static_cast<int>( studio.m_Bodygroups[ g ].m_Models.size() ); m++ )
			m_Bodygroups[ g ]->setItemText( m, QString::fromStdString( studio.m_Bodygroups[ g ].m_Models[ static_cast<std::size_t>( m ) ] ) );
	for ( int i = 0; i < m_pHitbox->count() && i < static_cast<int>( studio.m_HitboxSets.size() ); i++ )
		m_pHitbox->setItemText( i, QString::fromStdString( studio.m_HitboxSets[ static_cast<std::size_t>( i ) ].m_szName ) );
	for ( int i = 0; i < m_pSequences->count(); i++ )
	{
		auto *pItem = m_pSequences->item( i );
		const QPoint at = pItem->data( Qt::UserRole ).toPoint();
		if ( at.x() == 0 && at.y() < static_cast<int>( studio.m_Sequences.size() ) )
		{
			const auto &seq = studio.m_Sequences[ static_cast<std::size_t>( at.y() ) ];
			pItem->setText( QString::fromStdString( seq.m_szName ) );
			pItem->setToolTip( QString::fromStdString( studio.m_szPath + "\n" + seq.m_szActivity ) );
		}
	}
	ReloadMesh();
	m_pInfo->clear();
	m_pChecks->clear();
	PopulateInfo();
	if ( m_nSequence >= 0 )
	{
		const float flCycle = m_flCycle;
		SelectSequence();
		m_flCycle = flCycle;
		Sample();
	}
	RefreshPanels();
}

//-----------------------------------------------------------------------------
// Purpose: Another model, for copying hitboxes or attachments from.
//-----------------------------------------------------------------------------
std::optional<model::CStudioFile> CModelEditor::ReadOther( const QString &szPath, QString *pError ) const
{
	std::string szError;
	auto file = model::ReadStudio( vfs::NormalizePath( szPath.trimmed().toStdString() ), m_pfnRead, &szError );
	if ( !file )
		*pError = tr( "Can't read %1: %2" ).arg( szPath, QString::fromStdString( szError ) );
	return file;
}

//-----------------------------------------------------------------------------
// Purpose: Snapshots the current model and selected sequence for an external-source workflow.
//-----------------------------------------------------------------------------
CModelWorkflow *CModelEditor::CreateWorkflow( std::optional<xv::vfs::CGameInfo> game )
{
	if ( !m_Model )
		return nullptr;
	xv::model::Clip_t clip;
	clip.m_nSequence = m_nSequence;
	const auto &source = m_Files[ m_nSource ];
	for ( const auto &parameter : source.m_Parameters )
	{
		float flValue = parameter.m_flStart;
		for ( std::size_t i = 0; i < m_PoseControls.size(); i++ )
			if ( m_PoseControls[ i ].m_szName == parameter.m_szName )
				flValue = m_Parameters[ i ]->value();
		clip.m_Parameters.push_back( flValue );
	}
	auto *pWorkflow = new CModelWorkflow( m_szPath, m_pfnRead, *m_Model, source, clip, std::move( game ), this );
	pWorkflow->SetSelection( m_Selection );
	return pWorkflow;
}
