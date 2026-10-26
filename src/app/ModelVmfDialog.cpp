//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Export current model geometry as VMF brushes without blocking the editor.
//===========================================================================//
#include "ModelVmfDialog.h"
#include "FileDialogs.h"
#include "model/Mdl.h"
#include "material/Material.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QThread>
#include <QVBoxLayout>
#include <memory>
#include <stdexcept>
namespace
{
struct Result_t
{
	xv::model::Export_t m_Export;
	std::string m_szError;
};
} // namespace
//-----------------------------------------------------------------------------
// Purpose: Offers a small set of brush geometry and texturing choices for the current model.
//-----------------------------------------------------------------------------
CModelVmfDialog::CModelVmfDialog( QString szPath, xv::model::Reader_t pfnRead, xv::model::CModel model, const xv::model::Selection_t &selection, QWidget *pParent )
	: QDialog( pParent ),
	  m_szPath( std::move( szPath ) ),
	  m_pfnRead( std::move( pfnRead ) ),
	  m_Model( std::move( model ) ),
	  m_Selection( selection )
{
	setObjectName( QStringLiteral( "modelVmfDialog" ) );
	setWindowTitle( tr( "VMF (brushes)" ) );
	auto *pRoot = new QVBoxLayout( this );
	auto *pForm = new QFormLayout;
	pRoot->addLayout( pForm );
	m_pSource = new QComboBox;
	m_pSource->setObjectName( QStringLiteral( "vmfHullSource" ) );
	m_pSource->addItems( { tr( "Automatic (visible mesh)" ), tr( "Physics hulls (collision shape)" ), tr( "Visible mesh decomposition" ), tr( "Exact extruded cutout" ) } );
	pForm->addRow( tr( "Hull source" ), m_pSource );
	m_pTexture = new QComboBox;
	m_pTexture->setObjectName( QStringLiteral( "vmfTexture" ) );
	m_pTexture->addItem( tr( "Nodraw" ), static_cast<int>( xv::model::VmfOptions_t::Texturing_t::TEXTURING_NODRAW ) );
	m_pTexture->addItem( tr( "Developer grid" ), static_cast<int>( xv::model::VmfOptions_t::Texturing_t::TEXTURING_DEV ) );
	m_pTexture->addItem( tr( "Model materials (approximate)" ), static_cast<int>( xv::model::VmfOptions_t::Texturing_t::TEXTURING_MODEL ) );
	pForm->addRow( tr( "Materials" ), m_pTexture );
	m_pNodraw = new QCheckBox( tr( "Replace incompatible or missing materials with nodraw" ) );
	m_pNodraw->setChecked( true );
	m_pNodraw->setEnabled( false );
	pForm->addRow( m_pNodraw );
	connect( m_pTexture, &QComboBox::currentIndexChanged, this, [ this ]( int nIndex ) { m_pNodraw->setEnabled( nIndex == 2 ); } );
	m_pGrid = new QDoubleSpinBox;
	m_pGrid->setDecimals( 3 );
	m_pGrid->setRange( 0, 1024 );
	m_pGrid->setSingleStep( 0.125 );
	m_pGrid->setSpecialValueText( tr( "No snapping" ) );
	pForm->addRow( tr( "Grid step" ), m_pGrid );
	m_pHulls = new QSpinBox;
	m_pHulls->setRange( 1, 1024 );
	m_pHulls->setValue( 128 );
	pForm->addRow( tr( "Maximum decomposition hulls" ), m_pHulls );
	m_pSides = new QSpinBox;
	m_pSides->setRange( 4, 128 );
	m_pSides->setValue( 64 );
	pForm->addRow( tr( "Maximum brush sides" ), m_pSides );
	m_pHullVertices = new QSpinBox;
	m_pHullVertices->setRange( 4, 64 );
	m_pHullVertices->setValue( 32 );
	pForm->addRow( tr( "Maximum vertices per decomposition hull" ), m_pHullVertices );
	m_pConcavity = new QDoubleSpinBox;
	m_pConcavity->setDecimals( 3 );
	m_pConcavity->setRange( 0.01, 1 );
	m_pConcavity->setSingleStep( 0.01 );
	m_pConcavity->setValue( 0.01 );
	pForm->addRow( tr( "Concavity (closed meshes)" ), m_pConcavity );
	m_pVoxels = new QSpinBox;
	m_pVoxels->setObjectName( QStringLiteral( "vmfVoxelResolution" ) );
	m_pVoxels->setRange( 100000, 8000000 );
	m_pVoxels->setSingleStep( 100000 );
	m_pVoxels->setValue( 1000000 );
	pForm->addRow( tr( "Voxel resolution (open meshes)" ), m_pVoxels );
	const auto pfnSource = [ this ]( int nIndex ) { for ( QWidget *p : std::initializer_list<QWidget *>{ m_pHulls, m_pHullVertices, m_pConcavity, m_pVoxels } ) p->setEnabled( nIndex == 0 || nIndex == 2 ); };
	connect( m_pSource, &QComboBox::currentIndexChanged, this, pfnSource );
	for ( int i = 0; i < 3; i++ )
	{
		m_pOffset[ i ] = new QDoubleSpinBox;
		m_pOffset[ i ]->setRange( -16383, 16383 );
		m_pOffset[ i ]->setDecimals( 3 );
		pForm->addRow( tr( "Offset %1" ).arg( QString( QChar( 'X' + i ) ) ), m_pOffset[ i ] );
	}
	m_pDetail = new QCheckBox( tr( "Group brushes as func_detail" ) );
	m_pDetail->setChecked( true );
	pForm->addRow( m_pDetail );
	m_pReference = new QCheckBox( tr( "Include a reference prop when supported" ) );
	m_pReference->setChecked( true );
	pForm->addRow( m_pReference );
	m_pVertices = new QCheckBox( tr( "Include Hammer++ vertex data" ) );
	m_pVertices->setChecked( true );
	pForm->addRow( m_pVertices );
	auto *pHint = new QLabel( tr( "Automatic exports the visible mesh, retaining exact cutout caps when recognized. Open meshes use voxel surface approximation; higher resolution retains finer detail. Physics exports collision shapes, which may look very different. Invalid pieces are reported; detail brushes need a sealed map." ) );
	pHint->setWordWrap( true );
	pRoot->addWidget( pHint );
	m_pStatus = new QLabel;
	m_pStatus->setObjectName( QStringLiteral( "vmfStatus" ) );
	m_pStatus->setWordWrap( true );
	m_pStatus->setTextFormat( Qt::PlainText );
	pRoot->addWidget( m_pStatus );
	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Close );
	m_pExport = pButtons->addButton( tr( "Export VMF…" ), QDialogButtonBox::ActionRole );
	m_pExport->setObjectName( QStringLiteral( "exportVmf" ) );
	connect( m_pExport, &QPushButton::clicked, this, &CModelVmfDialog::Export );
	connect( pButtons, &QDialogButtonBox::rejected, this, &CModelVmfDialog::reject );
	pRoot->addWidget( pButtons );
}
//-----------------------------------------------------------------------------
// Purpose: Joins a running export before freeing its snapshot during application shutdown.
//-----------------------------------------------------------------------------
CModelVmfDialog::~CModelVmfDialog()
{
	if ( m_pWorker )
		m_pWorker->wait();
}
//-----------------------------------------------------------------------------
// Purpose: Keeps the export result destination alive while its worker finishes.
//-----------------------------------------------------------------------------
void CModelVmfDialog::reject()
{
	if ( !m_pWorker )
		QDialog::reject();
}
//-----------------------------------------------------------------------------
// Purpose: Snapshots document reads on the UI thread and computes brush geometry in a worker.
//-----------------------------------------------------------------------------
void CModelVmfDialog::Export()
{
	using namespace xv;
	if ( m_pWorker )
		return;
	const auto szTarget = AskSaveFile( this, tr( "Export VMF" ), QFileInfo( m_szPath ).completeBaseName() + ".vmf", tr( "Valve Maps (*.vmf)" ) );
	if ( szTarget.isEmpty() )
		return;
	try
	{
		model::VmfOptions_t options;
		options.m_Selection = m_Selection;
		options.m_nHullSource = static_cast<model::VmfOptions_t::HullSource_t>( m_pSource->currentIndex() );
		options.m_nTexturing = static_cast<model::VmfOptions_t::Texturing_t>( m_pTexture->currentData().toInt() );
		options.m_flGrid = m_pGrid->value();
		options.m_flConcavity = m_pConcavity->value();
		options.m_nMaxHulls = m_pHulls->value();
		options.m_nMaxSides = m_pSides->value();
		options.m_nMaxVerticesPerHull = m_pHullVertices->value();
		options.m_nVoxelResolution = m_pVoxels->value();
		options.m_bNodrawIncompatibleMaterials = m_pNodraw->isChecked();
		options.m_bHammerPlusPlusVertices = m_pVertices->isChecked();
		options.m_bDetail = m_pDetail->isChecked();
		options.m_bReferenceProp = m_pReference->isChecked();
		options.m_vecOffset = { static_cast<float>( m_pOffset[ 0 ]->value() ), static_cast<float>( m_pOffset[ 1 ]->value() ), static_cast<float>( m_pOffset[ 2 ]->value() ) };
		std::string szError, szReference;
		const auto studio = model::ReadStudio( m_szPath.toStdString(), m_pfnRead, &szError );
		if ( !studio )
			throw std::runtime_error( szError );
		if ( model::CanUseStaticReference( *studio ) )
			szReference = m_szPath.toStdString();
		std::vector<model::Convex_t> convexes;
		const bool bDecompose = m_pSource->currentIndex() == 0 || m_pSource->currentIndex() == 2;
		if ( m_pSource->currentIndex() == 1 )
		{
			const auto szPath = m_szPath.toStdString();
			const auto bytes = m_pfnRead( szPath.substr( 0, szPath.size() - 4 ) + ".phy" );
			if ( !bytes )
			{
				throw std::runtime_error( "Model has no PHY collision hulls" );
			}
			else
			{
				const auto physics = model::ReadPhysics( *bytes, &szError );
				if ( !physics )
					throw std::runtime_error( szError );
				if ( physics->m_nChecksum != studio->m_nChecksum )
					throw std::runtime_error( "PHY checksum does not match MDL" );
				convexes = physics->m_Convexes;
			}
		}
		// Document readers may capture QObjects or mutable caches. Resolve material includes here.
		auto pFiles = std::make_shared<std::map<std::string, std::optional<model::Bytes_t>>>();
		const model::Reader_t snapshot = [ this, pFiles ]( std::string_view szPath ) {
			auto [ it, bNew ] = pFiles->try_emplace( std::string( szPath ) );
			if ( bNew )
				it->second = m_pfnRead( szPath );
			return it->second;
		};
		if ( options.m_nTexturing == model::VmfOptions_t::Texturing_t::TEXTURING_MODEL )
			for ( const auto &szMaterial : m_Model.m_Materials )
				if ( const auto bytes = snapshot( "materials/" + szMaterial ) )
					(void)material::ParseMaterial( { reinterpret_cast<const char *>( bytes->data() ), bytes->size() }, snapshot );
		const model::Reader_t frozen = [ pFiles ]( std::string_view szPath ) -> std::optional<model::Bytes_t> { const auto it = pFiles->find( std::string( szPath ) ); return it == pFiles->end() ? std::nullopt : it->second; };
		auto pResult = std::make_shared<Result_t>();
		m_pWorker = QThread::create( [ model = m_Model, convexes = std::move( convexes ), frozen, options, bDecompose, szReference, pResult ]() mutable {
			try
			{
				bool bCutout = false;
				std::vector<std::string> notes;
				if ( options.m_nHullSource == model::VmfOptions_t::HullSource_t::HULL_SOURCE_AUTO || options.m_nHullSource == model::VmfOptions_t::HullSource_t::HULL_SOURCE_CUTOUT )
				{
					std::string szError;
					if ( auto cutout = model::ExtractCutoutHulls( model, options, &szError ) )
					{
						convexes = std::move( *cutout );
						bCutout = true;
					}
					else if ( options.m_nHullSource == model::VmfOptions_t::HullSource_t::HULL_SOURCE_CUTOUT )
						throw std::runtime_error( szError );
				}
				if ( bDecompose && !bCutout )
					convexes = model::DecomposeModel( model, options, &notes );
				auto brushOptions = options;
				if ( bCutout )
					brushOptions.m_nHullSource = model::VmfOptions_t::HullSource_t::HULL_SOURCE_CUTOUT;
				else if ( bDecompose )
					brushOptions.m_nHullSource = model::VmfOptions_t::HullSource_t::HULL_SOURCE_DECOMPOSE;
				pResult->m_Export = model::ExportVmf( model, convexes, frozen, brushOptions, szReference );
				pResult->m_Export.m_Notes.insert( pResult->m_Export.m_Notes.end(), notes.begin(), notes.end() );
				if ( !bDecompose && !bCutout )
					pResult->m_Export.m_Notes.push_back( "Physics collision hulls exported; these may differ substantially from the visible model." );
				if ( bCutout )
					pResult->m_Export.m_Notes.push_back( "Cutout brushes follow the render caps directly, retaining their outline and holes; any omitted pieces are reported above." );
				if ( options.m_bReferenceProp && szReference.empty() )
					pResult->m_Export.m_Notes.push_back( "Reference prop omitted: model is not eligible for prop_static." );
			}
			catch ( const std::exception &e )
			{
				pResult->m_szError = e.what();
			}
		} );
		m_pWorker->setParent( this );
		m_pExport->setEnabled( false );
		m_pStatus->setText( tr( "Building brushes…" ) );
		connect( m_pWorker, &QThread::finished, this, [ this, pResult, szTarget ] {
			m_pWorker->deleteLater();
			m_pWorker = nullptr;
			m_pExport->setEnabled( true );
			if ( !pResult->m_szError.empty() )
			{
				m_pStatus->setText( QString::fromStdString( pResult->m_szError ) );
				return;
			}
			const auto &bytes = pResult->m_Export.m_Files.at( "model.vmf" );
			QSaveFile file( szTarget );
			if ( !file.open( QIODevice::WriteOnly ) || file.write( reinterpret_cast<const char *>( bytes.data() ), bytes.size() ) != static_cast<qint64>( bytes.size() ) || !file.commit() )
			{
				m_pStatus->setText( file.errorString() );
				return;
			}
			QStringList notes{ tr( "Exported %1" ).arg( szTarget ) };
			for ( const auto &szNote : pResult->m_Export.m_Notes )
				notes.push_back( QString::fromStdString( szNote ) );
			m_pStatus->setText( notes.join( '\n' ) );
		} );
		m_pWorker->start();
	}
	catch ( const std::exception &e )
	{
		m_pStatus->setText( QString::fromUtf8( e.what() ) );
	}
}
