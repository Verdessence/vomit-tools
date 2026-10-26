//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: See ModelDetails.h.
//===========================================================================//
#include "ModelDetails.h"
#include "ModelView.h"
#include "ModelFlags.h"
#include "ModelRenderer.h"
#include "model/Anim.h"
#include "model/Edit.h"
#include "model/Transform.h"
#include "kv1/Kv1.h"
#include "QtPaths.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCursor>
#include <QFormLayout>
#include <QGroupBox>
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <set>

struct ModelDetailsData_t
{
	std::optional<xv::model::CStudioFile> m_Studio;
	std::optional<xv::model::CModel> m_Model;
	std::optional<xv::model::Physics_t> m_Physics;
	std::vector<xv::particles::Sprite_t> m_Sprites;
	std::vector<ModelMaterial_t> m_Materials;
	QStringList m_Problems;
	QString m_szLocation;
	struct Material_t
	{
		QString m_szPath;
		bool m_bVmt = false, m_bTexture = false;
		QStringList m_Textures;
	};
	std::vector<Material_t> m_MaterialInfo;
	std::vector<std::pair<QString, qint64>> m_Files;
	std::vector<float> m_Lods;
};
namespace
{
//-----------------------------------------------------------------------------
// Purpose: Counts only vertices actually referenced by the selected body and detail level.
//-----------------------------------------------------------------------------
std::size_t UsedVertices( const xv::model::CModel &model )
{
	std::set<std::uint32_t> used;
	for ( const auto &mesh : model.m_Meshes )
		used.insert( mesh.m_Indices.begin(), mesh.m_Indices.end() );
	return used.size();
}
//-----------------------------------------------------------------------------
// Purpose: Formats model dimensions in Source units without rich-text interpretation.
//-----------------------------------------------------------------------------
QString Dimensions( xv::CVec3 v )
{
	return QStringLiteral( "%1 × %2 × %3" ).arg( v.m_flX, 0, 'g', 5 ).arg( v.m_flY, 0, 'g', 5 ).arg( v.m_flZ, 0, 'g', 5 );
}
//-----------------------------------------------------------------------------
// Purpose: Adds selectable, wrapped facts which do not force a horizontal scrollbar.
//-----------------------------------------------------------------------------
void Fact( QFormLayout *pForm, const QString &szLabel, const QString &szValue, const char *pszObject = nullptr )
{
	auto *p = new QLabel( szValue );
	p->setTextFormat( Qt::PlainText );
	p->setWordWrap( true );
	p->setTextInteractionFlags( Qt::TextSelectableByMouse );
	p->setMinimumWidth( 0 );
	if ( pszObject )
		p->setObjectName( QString::fromUtf8( pszObject ) );
	pForm->addRow( szLabel, p );
}
} // namespace
//-----------------------------------------------------------------------------
// Purpose: Constructs a scrollable inspector and starts its private work queue.
//-----------------------------------------------------------------------------
CModelDetails::CModelDetails( QWidget *pParent )
	: QWidget( pParent )
{
	setObjectName( QStringLiteral( "modelDetails" ) );
	auto *pRoot = new QVBoxLayout( this );
	pRoot->setContentsMargins( 0, 0, 0, 0 );
	auto *pScroll = new QScrollArea;
	pScroll->setWidgetResizable( true );
	pScroll->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
	pRoot->addWidget( pScroll );
	auto *pContent = new QWidget;
	auto *pLayout = new QVBoxLayout( pContent );
	pScroll->setWidget( pContent );
	m_pWarning = new QLabel;
	m_pWarning->setObjectName( QStringLiteral( "modelDetailsWarnings" ) );
	m_pWarning->setWordWrap( true );
	m_pWarning->setTextFormat( Qt::PlainText );
	m_pWarning->setStyleSheet( QStringLiteral( "QLabel { color: #d99a42; padding: 6px; }" ) );
	m_pWarning->hide();
	pLayout->addWidget( m_pWarning );
	m_pName = new QLabel( tr( "Select a model" ) );
	m_pName->setWordWrap( true );
	m_pName->setTextFormat( Qt::PlainText );
	pLayout->addWidget( m_pName );
	m_pView = new CModelView;
	m_pView->setFixedHeight( 230 );
	pLayout->addWidget( m_pView );
	m_pSkin = new QComboBox;
	m_pSkin->setObjectName( QStringLiteral( "modelDetailsSkin" ) );
	pLayout->addWidget( m_pSkin );
	m_pSkin->hide();
	m_pBody = new QWidget;
	m_pBodyLayout = new QVBoxLayout( m_pBody );
	m_pBodyLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->addWidget( m_pBody );
	connect( m_pSkin, &QComboBox::currentIndexChanged, this, [ this ]( int n ) {if(n>=0&&n!=m_Selection.m_nSkin){m_Selection.m_nSkin=n;StartLoad();} } );
	auto *pOpen = new QPushButton( tr( "Open in Model Viewer" ) );
	pOpen->setObjectName( QStringLiteral( "modelDetailsOpen" ) );
	pLayout->addWidget( pOpen );
	connect( pOpen, &QPushButton::clicked, this, [ this ] {if(!m_szPath.isEmpty())emit OpenModelRequested(m_szPath); } );
	auto *pButtons = new QHBoxLayout;
	auto *pCopy = new QPushButton( tr( "Copy Path" ) );
	pCopy->setObjectName( QStringLiteral( "modelDetailsCopy" ) );
	auto *pShow = new QPushButton( tr( "Show in Game Browser" ) );
	pShow->setObjectName( QStringLiteral( "modelDetailsShow" ) );
	pButtons->addWidget( pCopy );
	pButtons->addWidget( pShow );
	pLayout->addLayout( pButtons );
	connect( pCopy, &QPushButton::clicked, this, [ this ] { QApplication::clipboard()->setText( m_szPath ); } );
	connect( pShow, &QPushButton::clicked, this, [ this ] {if(!m_szPath.isEmpty())emit ShowInGameBrowser(m_szPath); } );
	m_pFacts = new QWidget;
	m_pFactLayout = new QVBoxLayout( m_pFacts );
	m_pFactLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->addWidget( m_pFacts );
	pLayout->addStretch();
	m_pDebounce = new QTimer( this );
	m_pDebounce->setSingleShot( true );
	m_pDebounce->setInterval( 120 );
	connect( m_pDebounce, &QTimer::timeout, this, &CModelDetails::StartLoad );
	m_pOrbit = new QTimer( this );
	m_pOrbit->setInterval( 33 );
	connect( m_pOrbit, &QTimer::timeout, this, [ this ] {if(m_pData&&isVisible()&&!m_pView->rect().contains(m_pView->mapFromGlobal(QCursor::pos())))m_pView->Turn(0.45f); } );
	m_Worker = std::thread( [ this ] { Run(); } );
}
//-----------------------------------------------------------------------------
// Purpose: Stops posting results before Qt destroys the inspector and its view.
//-----------------------------------------------------------------------------
CModelDetails::~CModelDetails()
{
	{
		std::lock_guard lock( m_Mutex );
		m_bStop = true;
		m_Job.reset();
	}
	m_Wake.notify_one();
	m_Worker.join();
}
//-----------------------------------------------------------------------------
// Purpose: Invalidates work from an earlier mount while keeping saved disk changes visible.
//-----------------------------------------------------------------------------
void CModelDetails::SetGame( const xv::vfs::CGameInfo &game, xv::model::Reader_t pfnRead, Location_t pfnWhere )
{
	m_Game = game;
	m_pfnRead = std::move( pfnRead );
	m_pfnWhere = std::move( pfnWhere );
	const QString szPath = m_szPath;
	m_szPath.clear();
	SelectModel( szPath );
}
//-----------------------------------------------------------------------------
// Purpose: Coalesces rapid keyboard selections and immediately invalidates stale worker results.
//-----------------------------------------------------------------------------
void CModelDetails::SelectModel( const QString &szPath )
{
	if ( m_szPath == szPath )
		return;
	++m_nGeneration;
	m_szPath = szPath;
	m_Selection = {};
	Clear();
	m_pName->setText( szPath.isEmpty() ? tr( "Select a model" ) : tr( "Loading %1…" ).arg( szPath ) );
	m_bLoading = !szPath.isEmpty();
	m_pDebounce->stop();
	if ( m_bLoading )
		m_pDebounce->start();
}
//-----------------------------------------------------------------------------
// Purpose: Releases view pointers before their backing data and resets selection-specific facts.
//-----------------------------------------------------------------------------
void CModelDetails::Clear()
{
	m_pView->SetModel( nullptr, nullptr, nullptr );
	m_pView->SetResources( nullptr, nullptr );
	m_pView->ForgetTextures();
	m_pData.reset();
	m_pSkin->hide();
	m_pBody->hide();
	m_pWarning->clear();
	m_pWarning->hide();
	while ( auto *p = m_pFactLayout->takeAt( 0 ) )
	{
		delete p->widget();
		delete p;
	}
}
//-----------------------------------------------------------------------------
// Purpose: Submits an immutable request; the worker never reads the window's mutable mount.
//-----------------------------------------------------------------------------
void CModelDetails::StartLoad()
{
	if ( m_szPath.isEmpty() )
		return;
	Job_t job;
	job.m_Game = m_Game;
	job.m_szPath = m_szPath;
	job.m_nGeneration = ++m_nGeneration;
	job.m_Selection = m_Selection;
	if ( m_pfnWhere )
		job.m_szLocation = m_pfnWhere( m_szPath.toStdString() );
	if ( m_pfnRead )
		job.m_Bytes = m_pfnRead( m_szPath.toStdString() );
	m_bLoading = true;
	++m_nLoads;
	{
		std::lock_guard lock( m_Mutex );
		m_Job = std::move( job );
	}
	m_Wake.notify_one();
}
//-----------------------------------------------------------------------------
// Purpose: Loads mesh, textures and checked facts off the GUI thread with its own filesystem.
//-----------------------------------------------------------------------------
void CModelDetails::Run()
{
	for ( ;; )
	{
		Job_t job;
		{
			std::unique_lock lock( m_Mutex );
			m_Wake.wait( lock, [ this ] { return m_bStop || m_Job.has_value(); } );
			if ( m_bStop )
				return;
			job = std::move( *m_Job );
			m_Job.reset();
		}
		auto data = std::make_shared<ModelDetailsData_t>();
		try
		{
			xv::vfs::CFileSystem fs;
			fs.Mount( job.m_Game );
			const std::string szPath = job.m_szPath.toStdString();
			const xv::model::Reader_t read = [ & ]( std::string_view szFile ) { return szFile == szPath && job.m_Bytes ? job.m_Bytes : fs.Read( szFile ); };
			data->m_szLocation = job.m_szLocation;
			if ( data->m_szLocation.isEmpty() )
				if ( const auto *p = fs.Resolve( szPath ) )
					data->m_szLocation = ToQString( p->GetLocation() );
			std::string szError;
			data->m_Studio = xv::model::ReadStudio( szPath, read, &szError );
			if ( !data->m_Studio )
				throw std::runtime_error( szError );
			const auto &studio = *data->m_Studio;
			data->m_Model = xv::model::LoadModel( szPath, read, &szError, job.m_Selection );
			if ( !data->m_Model && studio.m_Bodygroups.empty() )
			{
				xv::model::CModel skeleton;
				for ( const auto &bone : studio.m_Bones )
					skeleton.m_Bones.push_back( { bone.m_szName, bone.m_nParent, bone.m_PoseToBone.Inverse().value_or( xv::model::CTransform{} ) } );
				if ( !studio.m_Sequences.empty() )
					if ( auto pose = xv::model::SampleSequence( studio, 0, 0, {}, skeleton ) )
						for ( std::size_t i = 0; i < pose->size(); i++ )
							skeleton.m_Bones[ i ].m_ToModel = ( *pose )[ i ];
				data->m_Model = std::move( skeleton );
				data->m_Problems << tr( "Animation-only model: no mesh." );
			}
			else if ( !data->m_Model )
				data->m_Problems << QString::fromStdString( szError );
			const auto szBase = szPath.substr( 0, szPath.size() - 4 );
			xv::model::Files_t files;
			for ( const auto *szKind : { ".mdl", ".vvd", ".dx90.vtx", ".dx80.vtx", ".sw.vtx", ".phy" } )
			{
				if ( auto bytes = read( szBase + szKind ) )
				{
					data->m_Files.emplace_back( QString::fromUtf8( szKind ), bytes->size() );
					files.emplace( szKind, std::move( *bytes ) );
				}
				else
					data->m_Files.emplace_back( QString::fromUtf8( szKind ), -1 );
			}
			if ( !studio.m_szAnimationFile.empty() )
			{
				data->m_Files.emplace_back( QString::fromStdString( studio.m_szAnimationFile ), studio.m_pAnimationBytes ? static_cast<qint64>( studio.m_pAnimationBytes->size() ) : -1 );
				if ( !studio.m_pAnimationBytes )
					data->m_Problems << tr( "External animation file is missing." );
			}
			else
				data->m_Files.emplace_back( QStringLiteral( ".ani" ), -1 );
			data->m_Lods = xv::model::ReadLodSwitchPoints( files );
			if ( auto found = files.find( ".phy" ); found != files.end() )
			{
				data->m_Physics = xv::model::ReadPhysics( found->second, &szError );
				if ( !data->m_Physics )
					data->m_Problems << QString::fromStdString( szError );
				else if ( data->m_Physics->m_nChecksum != studio.m_nChecksum )
					data->m_Problems << tr( "PHY checksum does not match the MDL." );
			}
			for ( const auto &szProblem : studio.m_Problems )
				data->m_Problems << QString::fromStdString( szProblem );
			if ( data->m_Model )
			{
				LoadModelMaterials( *data->m_Model, read, data->m_Sprites, data->m_Materials, 512 );
				std::set<int> used;
				for ( const auto &mesh : data->m_Model->m_Meshes )
					used.insert( mesh.m_nMaterial );
				for ( int nMaterial : used )
					if ( nMaterial >= 0 && nMaterial < static_cast<int>( data->m_Model->m_Materials.size() ) )
					{
						ModelDetailsData_t::Material_t material;
						material.m_szPath = QString::fromStdString( "materials/" + data->m_Model->m_Materials[ nMaterial ] );
						material.m_bVmt = read( material.m_szPath.toStdString() ).has_value();
						const auto &textures = data->m_Materials[ nMaterial ].m_Textures;
						const auto *pBase = textures.Find( "$basetexture" );
						material.m_bTexture = !pBase || pBase->m_Info.has_value();
						for ( const auto &texture : textures.GetTextures() )
						{
							material.m_Textures << QString::fromStdString( texture.m_szPath );
							if ( !texture.m_szProblem.empty() )
								data->m_Problems << QString::fromStdString( texture.m_szProblem );
						}
						if ( !material.m_bVmt )
							data->m_Problems << tr( "Missing material: %1" ).arg( material.m_szPath );
						else if ( !material.m_bTexture )
							data->m_Problems << tr( "Missing base texture: %1" ).arg( material.m_szPath );
						data->m_MaterialInfo.push_back( std::move( material ) );
					}
			}
		}
		catch ( const std::exception &e )
		{
			data->m_Problems << QString::fromUtf8( e.what() );
		}
		QMetaObject::invokeMethod( this, [ this, data, nGeneration = job.m_nGeneration ] { Present( data, nGeneration ); }, Qt::QueuedConnection );
	}
}
//-----------------------------------------------------------------------------
// Purpose: Publishes only the current selection, with translated and selectable facts and actions.
//-----------------------------------------------------------------------------
void CModelDetails::Present( std::shared_ptr<ModelDetailsData_t> data, int nGeneration )
{
	if ( nGeneration != m_nGeneration )
		return;
	const bool bFirst = !m_pData;
	Clear();
	m_pData = std::move( data );
	m_bLoading = false;
	m_pName->setText( m_szPath );
	m_pWarning->setText( m_pData->m_Problems.join( u'\n' ) );
	m_pWarning->setVisible( !m_pData->m_Problems.empty() );
	if ( !m_pData->m_Studio )
	{
		emit Loaded( m_szPath );
		return;
	}
	const auto &file = *m_pData->m_Studio;
	{
		const QSignalBlocker block( m_pSkin );
		m_pSkin->clear();
		for ( std::size_t i = 0; i < file.m_Skins.size(); i++ )
			m_pSkin->addItem( tr( "Skin %1" ).arg( i ) );
		m_pSkin->setCurrentIndex( m_Selection.m_nSkin );
		m_pSkin->setVisible( file.m_Skins.size() > 1 );
	}
	while ( auto *p = m_pBodyLayout->takeAt( 0 ) )
	{
		delete p->widget();
		delete p;
	}
	m_pBody->show();
	m_Selection.m_Bodygroups.resize( file.m_Bodygroups.size() );
	for ( std::size_t i = 0; i < file.m_Bodygroups.size(); i++ )
		if ( file.m_Bodygroups[ i ].m_Models.size() > 1 )
		{
			auto *p = new QComboBox;
			for ( const auto &szName : file.m_Bodygroups[ i ].m_Models )
				p->addItem( QString::fromStdString( szName ) );
			p->setToolTip( QString::fromStdString( file.m_Bodygroups[ i ].m_szName ) );
			p->setCurrentIndex( m_Selection.m_Bodygroups[ i ] );
			m_pBodyLayout->addWidget( p );
			connect( p, &QComboBox::currentIndexChanged, this, [ this, i ]( int n ) {m_Selection.m_Bodygroups[i]=n;StartLoad(); } );
		}
	if ( m_pData->m_Model )
	{
		m_pView->SetResources( &*m_pData->m_Model, &m_pData->m_Materials );
		m_pView->SetModel( &*m_pData->m_Model, &file, &m_pData->m_Sprites );
		m_pView->SetOverlays( false, false, m_pData->m_Model->m_Vertices.empty(), false, false, false, false, true );
		if ( bFirst )
			m_pView->Frame();
	}
	const auto group = [ & ]( const QString &szTitle ) {auto *p=new QGroupBox(szTitle);auto *pForm=new QFormLayout(p);pForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);pForm->setRowWrapPolicy(QFormLayout::WrapLongRows);m_pFactLayout->addWidget(p);return pForm; };
	auto *pIdentity = group( tr( "Identity" ) );
	Fact( pIdentity, tr( "Path" ), m_szPath, "modelDetailsPath" );
	Fact( pIdentity, tr( "Source" ), m_pData->m_szLocation );
	Fact( pIdentity, tr( "MDL version" ), QString::number( file.m_nVersion ) );
	Fact( pIdentity, tr( "Checksum" ), QStringLiteral( "0x%1" ).arg( static_cast<quint32>( file.m_nChecksum ), 8, 16, QChar( '0' ) ) );
	if ( QString::fromStdString( file.m_szName ) != m_szPath )
		Fact( pIdentity, tr( "Internal name" ), QString::fromStdString( file.m_szName ) );
	auto *pShape = group( tr( "Size and shape" ) );
	if ( m_pData->m_Model )
	{
		Fact( pShape, tr( "Triangles" ), QString::number( m_pData->m_Model->GetTriangleCount() ), "modelDetailsTriangles" );
		Fact( pShape, tr( "Vertices" ), QString::number( UsedVertices( *m_pData->m_Model ) ) );
		Fact( pShape, tr( "Bounds (units)" ), Dimensions( m_pData->m_Model->m_vecMax - m_pData->m_Model->m_vecMin ) );
	}
	Fact( pShape, tr( "Hull (units)" ), Dimensions( file.m_vecHullMax - file.m_vecHullMin ) );
	Fact( pShape, tr( "Bones / attachments" ), tr( "%1 / %2" ).arg( file.m_Bones.size() ).arg( m_pData->m_Model ? m_pData->m_Model->m_Attachments.size() : 0 ) );
	std::size_t nBoxes = 0;
	for ( const auto &set : file.m_HitboxSets )
		nBoxes += set.m_Boxes.size();
	Fact( pShape, tr( "Hitboxes / sets" ), tr( "%1 / %2" ).arg( nBoxes ).arg( file.m_HitboxSets.size() ) );
	Fact( pShape, tr( "Bodygroups / skins" ), tr( "%1 / %2" ).arg( file.m_Bodygroups.size() ).arg( file.m_Skins.size() ) );
	QStringList lods;
	for ( std::size_t i = 0; i < m_pData->m_Lods.size(); i++ )
		lods << tr( "LOD %1: %2" ).arg( i ).arg( m_pData->m_Lods[ i ] );
	Fact( pShape, tr( "Detail levels" ), lods.join( u'\n' ) );
	auto *pPhysics = group( tr( "Physics" ) );
	Fact( pPhysics, tr( "Surface" ), QString::fromStdString( file.m_szSurface ), "modelDetailsSurface" );
	Fact( pPhysics, tr( "Mass" ), QString::number( file.m_flMass ), "modelDetailsMass" );
	Fact( pPhysics, tr( "Static prop" ), file.m_nFlags & 16 ? tr( "Yes" ) : tr( "No" ) );
	Fact( pPhysics, tr( "Solids" ), m_pData->m_Physics ? QString::number( m_pData->m_Physics->m_nSolids ) : tr( "No physics hull" ) );
	QStringList flags;
	for ( const auto &flag : MODEL_FLAGS )
		if ( file.m_nFlags & flag.m_nBit )
			flags << QCoreApplication::translate( "CModelEditor", flag.m_pszName );
	Fact( pPhysics, tr( "Flags" ), flags.join( QStringLiteral( ", " ) ) );
	const auto kv = xv::kv1::Parse( file.m_szKeyValues );
	const xv::kv1::CNode *pProp = kv.m_Document.Find( "prop_data" );
	if ( !pProp )
		for ( const auto &root : kv.m_Document.m_Roots )
			if ( root.IsBlock() && ( pProp = root.Find( "prop_data" ) ) )
				break;
	if ( pProp )
	{
		for ( const auto *key : { "base", "health" } )
			if ( const auto *p = pProp->Find( key ) )
				Fact( pPhysics, QString::fromUtf8( key ), QString::fromStdString( p->GetValue() ) );
	}
	auto *pAnimation = group( tr( "Animation" ) );
	Fact( pAnimation, tr( "Sequences" ), QString::number( file.m_Sequences.size() ) );
	for ( std::size_t i = 0; i < std::min<std::size_t>( 8, file.m_Sequences.size() ); i++ )
	{
		const auto &seq = file.m_Sequences[ i ];
		QString szValue = tr( "No local animation" );
		if ( !seq.m_Animations.empty() )
		{
			const int n = seq.m_Animations[ 0 ];
			if ( n >= 0 && n < static_cast<int>( file.m_Animations.size() ) )
				szValue = tr( "%1 frames · %2 fps" ).arg( file.m_Animations[ n ].m_nFrames ).arg( file.m_Animations[ n ].m_flFps );
		}
		Fact( pAnimation, QString::fromStdString( seq.m_szName ), szValue );
	}
	for ( const auto &szInclude : file.m_Includes )
		Fact( pAnimation, tr( "Includes" ), QString::fromStdString( szInclude ) );
	if ( !file.m_szAnimationFile.empty() )
		Fact( pAnimation, tr( "Animation file" ), QString::fromStdString( file.m_szAnimationFile ) );
	auto *pMaterials = group( tr( "Materials" ) );
	for ( const auto &material : m_pData->m_MaterialInfo )
	{
		auto *p = new QLabel( QStringLiteral( "<span style='color:%1'>●</span> <a href='open'>%2</a>" ).arg( material.m_bVmt && material.m_bTexture ? QStringLiteral( "#62ad78" ) : QStringLiteral( "#de675c" ), material.m_szPath.toHtmlEscaped() ) );
		p->setWordWrap( true );
		p->setMinimumWidth( 0 );
		p->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Preferred );
		p->setToolTip( tr( "VMT: %1 · Base texture: %2\n%3" ).arg( material.m_bVmt ? tr( "present" ) : tr( "missing" ), material.m_bTexture ? tr( "present" ) : tr( "missing" ), material.m_Textures.join( u'\n' ) ) );
		pMaterials->addRow( p );
		connect( p, &QLabel::linkActivated, this, [ this, szPath = material.m_szPath ] { emit OpenMaterialRequested( szPath ); } );
	}
	for ( const auto &szFolder : file.m_Folders )
		Fact( pMaterials, tr( "Folder" ), QString::fromStdString( szFolder ) );
	auto *pFiles = group( tr( "Files" ) );
	for ( const auto &[ szKind, nSize ] : m_pData->m_Files )
		Fact( pFiles, szKind, nSize >= 0 ? tr( "%1 bytes" ).arg( nSize ) : tr( "Missing" ) );
	emit Loaded( m_szPath );
}
//-----------------------------------------------------------------------------
// Purpose: Exposes warnings for tests and accessibility.
//-----------------------------------------------------------------------------
QString CModelDetails::GetWarnings() const
{
	return m_pWarning->text();
}
//-----------------------------------------------------------------------------
// Purpose: Returns the actual material paths shown in the inspector.
//-----------------------------------------------------------------------------
QStringList CModelDetails::GetMaterials() const
{
	QStringList paths;
	if ( m_pData )
		for ( const auto &material : m_pData->m_MaterialInfo )
			paths << material.m_szPath;
	return paths;
}
//-----------------------------------------------------------------------------
// Purpose: Only visible previews spend time turning and redrawing.
//-----------------------------------------------------------------------------
void CModelDetails::showEvent( QShowEvent *pEvent )
{
	QWidget::showEvent( pEvent );
	m_pOrbit->start();
}
//-----------------------------------------------------------------------------
// Purpose: Suspends the orbit when the browser tab or collapsed panel is hidden.
//-----------------------------------------------------------------------------
void CModelDetails::hideEvent( QHideEvent *pEvent )
{
	m_pOrbit->stop();
	QWidget::hideEvent( pEvent );
}
