//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The model tab's edit panels. Each control commits one edit through the model document
//          when it's finished with (a cell left, a box left, a button pressed); the panels fill
//          themselves again from the files after every change, undo and redo.
//
//===========================================================================//
#include "ModelEditor.h"

#include <cmath>
#include <numbers>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "ModelDocument.h"
#include "ModelView.h"
#include "model/Binary.h"
#include "model/Phy.h"
#include "model/Transform.h"

using namespace xv;

namespace
{

using Vector_t = std::array<QDoubleSpinBox *, 3>;

// Model flags worth switching after compiling; the others say how it was built.
struct Flag_t
{
	int m_nBit;
	const char *m_pszName;
};
constexpr Flag_t MODEL_FLAGS[] = {
	{ 0x00000010, QT_TRANSLATE_NOOP( "CModelEditor", "Static prop" ) },
	{ 0x00000004, QT_TRANSLATE_NOOP( "CModelEditor", "Force opaque" ) },
	{ 0x00000008, QT_TRANSLATE_NOOP( "CModelEditor", "Translucent, two passes" ) },
	{ 0x00000100, QT_TRANSLATE_NOOP( "CModelEditor", "Shadow detail level uses real materials" ) },
	{ 0x00000800, QT_TRANSLATE_NOOP( "CModelEditor", "No forced fade" ) },
	{ 0x00001000, QT_TRANSLATE_NOOP( "CModelEditor", "Force phoneme crossfade" ) },
	{ 0x00002000, QT_TRANSLATE_NOOP( "CModelEditor", "Constant directional light" ) },
	{ 0x00010000, QT_TRANSLATE_NOOP( "CModelEditor", "Ambient boost" ) },
	{ 0x00020000, QT_TRANSLATE_NOOP( "CModelEditor", "Doesn't cast shadows" ) },
	{ 0x00040000, QT_TRANSLATE_NOOP( "CModelEditor", "Casts texture shadows (props_static)" ) },
};

// Hit groups as the games number them.
struct HitGroup_t
{
	int m_nGroup;
	const char *m_pszName;
};
constexpr HitGroup_t HIT_GROUPS[] = {
	{ 0, QT_TRANSLATE_NOOP( "CModelEditor", "Generic" ) },
	{ 1, QT_TRANSLATE_NOOP( "CModelEditor", "Head" ) },
	{ 2, QT_TRANSLATE_NOOP( "CModelEditor", "Chest" ) },
	{ 3, QT_TRANSLATE_NOOP( "CModelEditor", "Stomach" ) },
	{ 4, QT_TRANSLATE_NOOP( "CModelEditor", "Left arm" ) },
	{ 5, QT_TRANSLATE_NOOP( "CModelEditor", "Right arm" ) },
	{ 6, QT_TRANSLATE_NOOP( "CModelEditor", "Left leg" ) },
	{ 7, QT_TRANSLATE_NOOP( "CModelEditor", "Right leg" ) },
	{ 8, QT_TRANSLATE_NOOP( "CModelEditor", "Neck" ) },
	{ 10, QT_TRANSLATE_NOOP( "CModelEditor", "Gear" ) },
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QDoubleSpinBox *Number( double flMin, double flMax, int nDecimals = 3 )
{
	auto *p = new QDoubleSpinBox;
	p->setRange( flMin, flMax );
	p->setDecimals( nDecimals );
	p->setKeyboardTracking( false );
	p->setAccelerated( true );
	return p;
}

//-----------------------------------------------------------------------------
// Purpose: Three boxes in a row for a position.
//-----------------------------------------------------------------------------
QWidget *VectorRow( Vector_t &boxes )
{
	auto *pRow = new QWidget;
	auto *pLayout = new QHBoxLayout( pRow );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	for ( auto *&p : boxes )
	{
		p = Number( -65536, 65536 );
		p->setButtonSymbols( QAbstractSpinBox::NoButtons );
		p->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Fixed );
		p->setMinimumWidth( 48 );
		pLayout->addWidget( p );
	}
	return pRow;
}

CVec3 ValueOf( const Vector_t &boxes )
{
	return { static_cast<float>( boxes[ 0 ]->value() ), static_cast<float>( boxes[ 1 ]->value() ), static_cast<float>( boxes[ 2 ]->value() ) };
}

void Show( const Vector_t &boxes, CVec3 vec )
{
	const float values[ 3 ] = { vec.m_flX, vec.m_flY, vec.m_flZ };
	for ( std::size_t i = 0; i < 3; i++ )
	{
		const QSignalBlocker block( boxes[ i ] );
		boxes[ i ]->setValue( values[ i ] );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Table cells: text, or numbers shown short.
//-----------------------------------------------------------------------------
QTableWidgetItem *Cell( const QString &szText, bool bEditable = true )
{
	auto *p = new QTableWidgetItem( szText );
	if ( !bEditable )
		p->setFlags( p->flags() & ~Qt::ItemIsEditable );
	return p;
}

QTableWidgetItem *Cell( float flValue )
{
	return Cell( QString::number( flValue, 'g', 6 ) );
}

float CellFloat( const QTableWidget *pTable, int nRow, int nColumn, bool *pOk )
{
	const QTableWidgetItem *p = pTable->item( nRow, nColumn );
	bool bOk = false;
	const float flValue = p ? p->text().toFloat( &bOk ) : 0.0f;
	if ( !bOk || !std::isfinite( flValue ) )
		*pOk = false;
	return flValue;
}

QTableWidget *Table( const QStringList &headers )
{
	auto *p = new QTableWidget( 0, static_cast<int>( headers.size() ) );
	p->setHorizontalHeaderLabels( headers );
	p->verticalHeader()->setVisible( false );
	p->horizontalHeader()->setSectionResizeMode( QHeaderView::Interactive );
	p->horizontalHeader()->setStretchLastSection( true );
	p->setSelectionBehavior( QAbstractItemView::SelectRows );
	return p;
}

//-----------------------------------------------------------------------------
// Purpose: Source angles (pitch, yaw, roll in degrees) of a rotation, the inverse of
//          CTransform::Rotation.
//-----------------------------------------------------------------------------
CVec3 AnglesOf( const model::CTransform &transform )
{
	const auto &m = transform.m_Values;
	constexpr float DEGREES = 180.0f / std::numbers::pi_v<float>;
	const float flForwardXY = std::sqrt( m[ 0 ] * m[ 0 ] + m[ 4 ] * m[ 4 ] );
	if ( flForwardXY > 0.001f )
		return { std::atan2( -m[ 8 ], flForwardXY ) * DEGREES, std::atan2( m[ 4 ], m[ 0 ] ) * DEGREES, std::atan2( m[ 9 ], m[ 10 ] ) * DEGREES };
	return { std::atan2( -m[ 8 ], flForwardXY ) * DEGREES, std::atan2( -m[ 1 ], m[ 5 ] ) * DEGREES, 0.0f };
}

model::CTransform Placed( CVec3 vecOrigin, CVec3 vecAngles )
{
	model::CTransform t = model::CTransform::Rotation( vecAngles.m_flX, vecAngles.m_flY, vecAngles.m_flZ );
	t.m_Values[ 3 ] = vecOrigin.m_flX;
	t.m_Values[ 7 ] = vecOrigin.m_flY;
	t.m_Values[ 11 ] = vecOrigin.m_flZ;
	return t;
}

std::vector<std::string> Lines( const QString &szText )
{
	std::vector<std::string> lines;
	for ( const QString &szLine : szText.split( u'\n' ) )
		if ( !szLine.trimmed().isEmpty() )
			lines.push_back( szLine.trimmed().toStdString() );
	return lines;
}

QString Joined( const std::vector<std::string> &lines )
{
	QStringList out;
	for ( const auto &szLine : lines )
		out << QString::fromStdString( szLine );
	return out.join( u'\n' );
}

//-----------------------------------------------------------------------------
// Purpose: Text that wraps to the panel's width.
//-----------------------------------------------------------------------------
QLabel *Note( const QString &szText )
{
	auto *p = new QLabel( szText );
	p->setWordWrap( true );
	return p;
}

//-----------------------------------------------------------------------------
// Purpose: A text box with an Apply button, for blocks edited whole (keyvalues, folders...).
//-----------------------------------------------------------------------------
QPlainTextEdit *TextBlock( QFormLayout *pForm, const QString &szLabel, const std::function<void( const QString & )> &pfnApply, int nHeight = 90 )
{
	auto *pText = new QPlainTextEdit;
	pText->setMinimumHeight( nHeight );
	pText->setLineWrapMode( QPlainTextEdit::NoWrap );
	auto *pApply = new QPushButton( CModelEditor::tr( "Apply" ) );
	QObject::connect( pApply, &QPushButton::clicked, pText, [ pText, pfnApply ] { pfnApply( pText->toPlainText() ); } );
	pForm->addRow( Note( szLabel ) );
	pForm->addRow( pText );
	pForm->addRow( {}, pApply );
	return pText;
}

QWidget *Scrolled( QWidget *pPage )
{
	auto *pScroll = new QScrollArea;
	pScroll->setWidgetResizable( true );
	pScroll->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
	pScroll->setWidget( pPage );
	return pScroll;
}

//-----------------------------------------------------------------------------
// Purpose: A bone picker; the current one is kept when it's out of range.
//-----------------------------------------------------------------------------
QComboBox *BonePicker( const model::CStudioFile &file, int nBone )
{
	auto *p = new QComboBox;
	for ( const auto &bone : file.m_Bones )
		p->addItem( QString::fromStdString( bone.m_szName ) );
	p->setCurrentIndex( nBone );
	return p;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Name, surface, mass, contents, flags, the header's positions and boxes, includes,
//          keyvalues and the collision hull's text.
//-----------------------------------------------------------------------------
QWidget *CModelEditor::BuildModelPanel()
{
	auto *pPage = new QWidget;
	auto *pForm = new QFormLayout( pPage );
	const auto mdl = []( model::Files_t &files ) -> model::Bytes_t & { return files.at( ".mdl" ); };

	auto *pName = new QLineEdit;
	pName->setMaxLength( 63 );
	pName->setToolTip( tr( "The name inside the file; the game finds the model by its path, not this." ) );
	connect( pName, &QLineEdit::editingFinished, this, [ this, pName, mdl ] { Commit( tr( "Change Internal Name" ), [ szName = pName->text().toStdString(), mdl ]( model::Files_t &f ) { model::SetInternalName( mdl( f ), szName ); } ); } );
	pForm->addRow( tr( "Internal name" ), pName );

	m_pSurfaceProp = new QComboBox;
	m_pSurfaceProp->setEditable( true );
	m_pSurfaceProp->setInsertPolicy( QComboBox::NoInsert );
	const auto commitSurface = [ this, mdl ] {
		const std::string szName = m_pSurfaceProp->currentText().trimmed().toStdString();
		if ( !m_bRefreshing && !szName.empty() )
			Commit( tr( "Change Surface Property" ), [ szName, mdl ]( model::Files_t &f ) { model::SetSurfaceProp( mdl( f ), szName ); } );
	};
	connect( m_pSurfaceProp, &QComboBox::activated, this, commitSurface );
	connect( m_pSurfaceProp->lineEdit(), &QLineEdit::editingFinished, this, commitSurface );
	pForm->addRow( tr( "Surface property" ), m_pSurfaceProp );

	auto *pMass = Number( 0, 1e6, 3 );
	pMass->setSuffix( tr( " kg" ) );
	connect( pMass, &QDoubleSpinBox::editingFinished, this, [ this, pMass, mdl ] { Commit( tr( "Change Mass" ), [ fl = static_cast<float>( pMass->value() ), mdl ]( model::Files_t &f ) { model::SetMass( mdl( f ), fl ); } ); } );
	pForm->addRow( tr( "Mass" ), pMass );

	auto *pContents = new QSpinBox;
	pContents->setRange( 0, 0x7fffffff );
	pContents->setDisplayIntegerBase( 16 );
	pContents->setPrefix( QStringLiteral( "0x" ) );
	pContents->setToolTip( tr( "$contents: what the model is for traces (1 solid, 0x2000 debris...)." ) );
	connect( pContents, &QSpinBox::editingFinished, this, [ this, pContents, mdl ] { Commit( tr( "Change Contents" ), [ n = pContents->value(), mdl ]( model::Files_t &f ) { model::SetContents( mdl( f ), n ); } ); } );
	pForm->addRow( tr( "Contents" ), pContents );

	auto *pFlags = new QListWidget;
	pFlags->setMaximumHeight( 150 );
	for ( const Flag_t &flag : MODEL_FLAGS )
	{
		auto *pItem = new QListWidgetItem( tr( flag.m_pszName ), pFlags );
		pItem->setData( Qt::UserRole, flag.m_nBit );
		pItem->setFlags( pItem->flags() | Qt::ItemIsUserCheckable );
		pItem->setCheckState( Qt::Unchecked );
	}
	connect( pFlags, &QListWidget::itemChanged, this, [ this, pFlags, mdl ] {
		if ( m_bRefreshing )
			return;
		int nFlags = m_Files[ 0 ].m_nFlags;
		for ( int i = 0; i < pFlags->count(); i++ )
		{
			const int nBit = pFlags->item( i )->data( Qt::UserRole ).toInt();
			nFlags = pFlags->item( i )->checkState() == Qt::Checked ? nFlags | nBit : nFlags & ~nBit;
		}
		Commit( tr( "Change Model Flags" ), [ nFlags, mdl ]( model::Files_t &f ) { model::SetFlags( mdl( f ), nFlags ); } );
	} );
	pForm->addRow( tr( "Flags" ), pFlags );

	// The header's positions and boxes.
	struct HeaderRow_t
	{
		model::HeaderVector_t m_nWhich;
		const char *m_pszName;
		Vector_t m_Boxes{};
	};
	auto rows = std::make_shared<std::vector<HeaderRow_t>>( std::vector<HeaderRow_t>{
		{ model::HeaderVector_t::HEADER_VECTOR_EYE, QT_TR_NOOP( "Eye position" ) },
		{ model::HeaderVector_t::HEADER_VECTOR_ILLUMINATION, QT_TR_NOOP( "Lighting origin" ) },
		{ model::HeaderVector_t::HEADER_VECTOR_HULL_MIN, QT_TR_NOOP( "Hull min" ) },
		{ model::HeaderVector_t::HEADER_VECTOR_HULL_MAX, QT_TR_NOOP( "Hull max" ) },
		{ model::HeaderVector_t::HEADER_VECTOR_VIEW_MIN, QT_TR_NOOP( "View box min" ) },
		{ model::HeaderVector_t::HEADER_VECTOR_VIEW_MAX, QT_TR_NOOP( "View box max" ) } } );
	for ( auto &row : *rows )
	{
		pForm->addRow( tr( row.m_pszName ), VectorRow( row.m_Boxes ) );
		for ( auto *pBox : row.m_Boxes )
			connect( pBox, &QDoubleSpinBox::editingFinished, this, [ this, &row, mdl ] {
				Commit( tr( "Change %1" ).arg( tr( row.m_pszName ) ), [ nWhich = row.m_nWhich, vec = ValueOf( row.m_Boxes ), mdl ]( model::Files_t &f ) { model::SetHeaderVector( mdl( f ), nWhich, vec ); } );
			} );
	}

	auto *pIncludes = TextBlock( pForm, tr( "Included models ($includemodel), one per line" ), [ this, mdl ]( const QString &szText ) { Commit( tr( "Change Included Models" ), [ lines = Lines( szText ), mdl ]( model::Files_t &f ) { model::SetIncludes( mdl( f ), lines ); } ); }, 60 );
	auto *pKeyValues = TextBlock( pForm, tr( "Keyvalues ($keyvalues: prop_data, physgun_interactions...)" ), [ this, mdl ]( const QString &szText ) { Commit( tr( "Change Keyvalues" ), [ szText = szText.toStdString(), mdl ]( model::Files_t &f ) { model::SetKeyValues( mdl( f ), szText ); } ); } );
	QPlainTextEdit *pPhysics = nullptr;
	if ( m_pDocument->Has( ".phy" ) )
		pPhysics = TextBlock( pForm, tr( "Collision hull properties (the .phy's text: mass, damping, ragdoll limits)" ), [ this ]( const QString &szText ) { Commit( tr( "Change Hull Properties" ), [ szText = szText.toStdString() ]( model::Files_t &f ) { model::SetPhysicsText( f.at( ".phy" ), szText ); } ); }, 140 );

	m_Refreshers.push_back( [ =, this ] {
		const model::CStudioFile &file = m_Files[ 0 ];
		{
			const QSignalBlocker block( pName );
			pName->setText( QString::fromStdString( file.m_szName ) );
		}
		{
			const QSignalBlocker block( m_pSurfaceProp );
			m_pSurfaceProp->clear();
			m_pSurfaceProp->addItems( m_SurfaceProps );
			m_pSurfaceProp->setCurrentText( QString::fromStdString( file.m_szSurface ) );
		}
		{
			const QSignalBlocker block( pMass );
			pMass->setValue( file.m_flMass );
		}
		{
			const QSignalBlocker block( pContents );
			pContents->setValue( file.m_nContents );
		}
		{
			const QSignalBlocker block( pFlags );
			for ( int i = 0; i < pFlags->count(); i++ )
				pFlags->item( i )->setCheckState( file.m_nFlags & pFlags->item( i )->data( Qt::UserRole ).toInt() ? Qt::Checked : Qt::Unchecked );
		}
		const CVec3 values[] = { file.m_vecEye, file.m_vecIllumination, file.m_vecHullMin, file.m_vecHullMax, file.m_vecViewMin, file.m_vecViewMax };
		for ( std::size_t i = 0; i < rows->size(); i++ )
			Show( ( *rows )[ i ].m_Boxes, values[ i ] );
		pIncludes->setPlainText( Joined( file.m_Includes ) );
		pKeyValues->setPlainText( QString::fromStdString( file.m_szKeyValues ) );
		if ( pPhysics )
			pPhysics->setPlainText( m_Physics ? QString::fromStdString( m_Physics->m_szText ) : QString{} );
	} );
	return Scrolled( pPage );
}

//-----------------------------------------------------------------------------
// Purpose: The boxes of a hitbox set: bone, hit group and bounds by number; added, removed or
//          copied from another model (matched by bone name).
//-----------------------------------------------------------------------------
QWidget *CModelEditor::BuildHitboxPanel()
{
	auto *pPage = new QWidget;
	auto *pLayout = new QVBoxLayout( pPage );
	auto *pSet = new QComboBox;
	pLayout->addWidget( pSet );
	auto *pSetName = new QLineEdit;
	pSetName->setPlaceholderText( tr( "Set name" ) );
	pLayout->addWidget( pSetName );
	auto *pTable = Table( { tr( "Name" ), tr( "Bone" ), tr( "Group" ), tr( "Min X" ), tr( "Min Y" ), tr( "Min Z" ), tr( "Max X" ), tr( "Max Y" ), tr( "Max Z" ) } );
	pTable->setObjectName( QStringLiteral( "modelHitboxes" ) );
	pLayout->addWidget( pTable, 1 );
	auto *pButtons = new QHBoxLayout;
	auto *pAdd = new QPushButton( tr( "Add" ) );
	auto *pRemove = new QPushButton( tr( "Remove" ) );
	auto *pCopy = new QPushButton( tr( "Copy from Model…" ) );
	pButtons->addWidget( pAdd );
	pButtons->addWidget( pRemove );
	pButtons->addStretch( 1 );
	pButtons->addWidget( pCopy );
	pLayout->addLayout( pButtons );

	const auto currentSet = [ pSet ] { return std::max( 0, pSet->currentIndex() ); };
	// A row as a box; nullopt (after saying so) when a number doesn't read.
	const auto rowBox = [ this, pTable ]( int nRow ) -> std::optional<model::Hitbox_t> {
		model::Hitbox_t box;
		bool bOk = true;
		box.m_szName = pTable->item( nRow, 0 ) ? pTable->item( nRow, 0 )->text().toStdString() : std::string{};
		box.m_nBone = static_cast<QComboBox *>( pTable->cellWidget( nRow, 1 ) )->currentIndex();
		box.m_nGroup = static_cast<QComboBox *>( pTable->cellWidget( nRow, 2 ) )->currentData().toInt();
		box.m_vecMin = { CellFloat( pTable, nRow, 3, &bOk ), CellFloat( pTable, nRow, 4, &bOk ), CellFloat( pTable, nRow, 5, &bOk ) };
		box.m_vecMax = { CellFloat( pTable, nRow, 6, &bOk ), CellFloat( pTable, nRow, 7, &bOk ), CellFloat( pTable, nRow, 8, &bOk ) };
		if ( !bOk )
		{
			m_pStatus->setText( tr( "Not changed: a bound isn't a number." ) );
			RefreshPanels();
			return std::nullopt;
		}
		return box;
	};
	const auto commitRow = [ this, rowBox, currentSet ]( int nRow ) {
		if ( m_bRefreshing )
			return;
		if ( const auto box = rowBox( nRow ) )
			Commit( tr( "Change Hitbox" ), [ nSet = currentSet(), nRow, box = *box ]( model::Files_t &f ) { model::SetHitbox( f.at( ".mdl" ), nSet, nRow, box ); } );
	};
	connect( pTable, &QTableWidget::itemChanged, this, [ commitRow ]( QTableWidgetItem *pItem ) { commitRow( pItem->row() ); } );
	connect( pTable, &QTableWidget::itemSelectionChanged, this, [ this, pTable ] {
		const auto rows = pTable->selectionModel()->selectedRows();
		m_pView->SetHighlight( rows.isEmpty() ? -1 : rows.front().row(), -1 );
	} );
	connect( pSet, &QComboBox::currentIndexChanged, this, [ this ]( int nIndex ) {
		if ( !m_bRefreshing )
			m_pHitbox->setCurrentIndex( nIndex );
	} );
	connect( pSetName, &QLineEdit::editingFinished, this, [ this, pSetName, currentSet ] { Commit( tr( "Rename Hitbox Set" ), [ nSet = currentSet(), szName = pSetName->text().toStdString() ]( model::Files_t &f ) { model::Rename( f.at( ".mdl" ), model::Name_t::NAME_HITBOX_SET, nSet, szName ); } ); } );
	connect( pAdd, &QPushButton::clicked, this, [ this, pTable, currentSet ] {
		const int nSet = currentSet();
		if ( nSet >= static_cast<int>( m_Files[ 0 ].m_HitboxSets.size() ) )
			return;
		auto boxes = m_Files[ 0 ].m_HitboxSets[ static_cast<std::size_t>( nSet ) ].m_Boxes;
		const auto rows = pTable->selectionModel()->selectedRows();
		model::Hitbox_t box{ 0, 0, { -4, -4, -4 }, { 4, 4, 4 }, "" };
		if ( !rows.isEmpty() && rows.front().row() < static_cast<int>( boxes.size() ) )
			box = boxes[ static_cast<std::size_t>( rows.front().row() ) ];
		boxes.push_back( box );
		Commit( tr( "Add Hitbox" ), [ nSet, boxes ]( model::Files_t &f ) { model::SetHitboxes( f.at( ".mdl" ), nSet, boxes ); } );
	} );
	connect( pRemove, &QPushButton::clicked, this, [ this, pTable, currentSet ] {
		const int nSet = currentSet();
		if ( nSet >= static_cast<int>( m_Files[ 0 ].m_HitboxSets.size() ) )
			return;
		auto boxes = m_Files[ 0 ].m_HitboxSets[ static_cast<std::size_t>( nSet ) ].m_Boxes;
		std::vector<int> rows;
		for ( const auto &index : pTable->selectionModel()->selectedRows() )
			rows.push_back( index.row() );
		std::ranges::sort( rows, std::greater{} );
		for ( const int nRow : rows )
			if ( nRow < static_cast<int>( boxes.size() ) )
				boxes.erase( boxes.begin() + nRow );
		if ( !rows.empty() )
			Commit( tr( "Remove Hitbox" ), [ nSet, boxes ]( model::Files_t &f ) { model::SetHitboxes( f.at( ".mdl" ), nSet, boxes ); } );
	} );
	connect( pCopy, &QPushButton::clicked, this, [ this, currentSet ] {
		const QString szPath = QInputDialog::getText( this, tr( "Copy Hitboxes" ), tr( "Model to copy the first hitbox set from (a game path):" ), QLineEdit::Normal, m_szPath );
		if ( szPath.isEmpty() )
			return;
		QString szError;
		const auto other = ReadOther( szPath, &szError );
		if ( !other || other->m_HitboxSets.empty() )
		{
			m_pStatus->setText( other ? tr( "That model has no hitboxes." ) : szError );
			return;
		}
		std::vector<model::Hitbox_t> boxes;
		int nSkipped = 0;
		for ( auto box : other->m_HitboxSets[ 0 ].m_Boxes )
		{
			const std::string &szBone = other->m_Bones[ static_cast<std::size_t>( box.m_nBone ) ].m_szName;
			const auto found = std::ranges::find_if( m_Files[ 0 ].m_Bones, [ &szBone ]( const auto &bone ) { return bone.m_szName == szBone; } );
			if ( found == m_Files[ 0 ].m_Bones.end() )
			{
				nSkipped++;
				continue;
			}
			box.m_nBone = static_cast<int>( found - m_Files[ 0 ].m_Bones.begin() );
			boxes.push_back( box );
		}
		if ( Commit( tr( "Copy Hitboxes" ), [ nSet = currentSet(), boxes ]( model::Files_t &f ) { model::SetHitboxes( f.at( ".mdl" ), nSet, boxes ); } ) && nSkipped )
			m_pStatus->setText( tr( "Copied %1 hitboxes; %2 were on bones this model doesn't have." ).arg( boxes.size() ).arg( nSkipped ) );
	} );

	m_Refreshers.push_back( [ =, this ] {
		const model::CStudioFile &file = m_Files[ 0 ];
		{
			const QSignalBlocker block( pSet );
			pSet->clear();
			for ( const auto &set : file.m_HitboxSets )
				pSet->addItem( QString::fromStdString( set.m_szName ) );
			pSet->setCurrentIndex( std::clamp( m_pHitbox->currentIndex(), 0, std::max( 0, static_cast<int>( file.m_HitboxSets.size() ) - 1 ) ) );
		}
		const int nSet = pSet->currentIndex();
		const bool bHasSet = nSet >= 0 && nSet < static_cast<int>( file.m_HitboxSets.size() );
		pAdd->setEnabled( bHasSet && !file.m_Bones.empty() );
		pRemove->setEnabled( bHasSet );
		pCopy->setEnabled( bHasSet );
		{
			const QSignalBlocker block( pSetName );
			pSetName->setText( bHasSet ? QString::fromStdString( file.m_HitboxSets[ static_cast<std::size_t>( nSet ) ].m_szName ) : QString{} );
		}
		const QSignalBlocker block( pTable );
		const int nKeep = pTable->currentRow();
		pTable->setRowCount( 0 );
		if ( !bHasSet )
			return;
		const auto &boxes = file.m_HitboxSets[ static_cast<std::size_t>( nSet ) ].m_Boxes;
		pTable->setRowCount( static_cast<int>( boxes.size() ) );
		for ( int i = 0; i < static_cast<int>( boxes.size() ); i++ )
		{
			const auto &box = boxes[ static_cast<std::size_t>( i ) ];
			pTable->setItem( i, 0, Cell( QString::fromStdString( box.m_szName ) ) );
			auto *pBone = BonePicker( file, box.m_nBone );
			connect( pBone, &QComboBox::activated, this, [ commitRow, i ] { commitRow( i ); } );
			pTable->setCellWidget( i, 1, pBone );
			auto *pGroup = new QComboBox;
			for ( const HitGroup_t &group : HIT_GROUPS )
				pGroup->addItem( tr( group.m_pszName ), group.m_nGroup );
			if ( pGroup->findData( box.m_nGroup ) < 0 )
				pGroup->addItem( QString::number( box.m_nGroup ), box.m_nGroup );
			pGroup->setCurrentIndex( pGroup->findData( box.m_nGroup ) );
			connect( pGroup, &QComboBox::activated, this, [ commitRow, i ] { commitRow( i ); } );
			pTable->setCellWidget( i, 2, pGroup );
			const float values[] = { box.m_vecMin.m_flX, box.m_vecMin.m_flY, box.m_vecMin.m_flZ, box.m_vecMax.m_flX, box.m_vecMax.m_flY, box.m_vecMax.m_flZ };
			for ( int k = 0; k < 6; k++ )
				pTable->setItem( i, 3 + k, Cell( values[ k ] ) );
		}
		if ( nKeep >= 0 && nKeep < pTable->rowCount() )
			pTable->selectRow( nKeep );
	} );
	return pPage;
}

//-----------------------------------------------------------------------------
// Purpose: Attachments: bone, position and angles in the bone's space; renamed, added, removed or
//          copied from another model.
//-----------------------------------------------------------------------------
QWidget *CModelEditor::BuildAttachmentPanel()
{
	auto *pPage = new QWidget;
	auto *pLayout = new QVBoxLayout( pPage );
	auto *pTable = Table( { tr( "Name" ), tr( "Bone" ), tr( "X" ), tr( "Y" ), tr( "Z" ), tr( "Pitch" ), tr( "Yaw" ), tr( "Roll" ) } );
	pTable->setObjectName( QStringLiteral( "modelAttachments" ) );
	pLayout->addWidget( pTable, 1 );
	auto *pButtons = new QHBoxLayout;
	auto *pAdd = new QPushButton( tr( "Add" ) );
	auto *pRemove = new QPushButton( tr( "Remove" ) );
	auto *pCopy = new QPushButton( tr( "Copy from Model…" ) );
	pButtons->addWidget( pAdd );
	pButtons->addWidget( pRemove );
	pButtons->addStretch( 1 );
	pButtons->addWidget( pCopy );
	pLayout->addLayout( pButtons );

	const auto commitRow = [ this, pTable ]( int nRow, int nColumn ) {
		if ( m_bRefreshing )
			return;
		if ( nColumn == 0 )
		{
			const std::string szName = pTable->item( nRow, 0 )->text().toStdString();
			Commit( tr( "Rename Attachment" ), [ nRow, szName ]( model::Files_t &f ) { model::Rename( f.at( ".mdl" ), model::Name_t::NAME_ATTACHMENT, nRow, szName ); } );
			return;
		}
		bool bOk = true;
		const CVec3 vecOrigin{ CellFloat( pTable, nRow, 2, &bOk ), CellFloat( pTable, nRow, 3, &bOk ), CellFloat( pTable, nRow, 4, &bOk ) };
		const CVec3 vecAngles{ CellFloat( pTable, nRow, 5, &bOk ), CellFloat( pTable, nRow, 6, &bOk ), CellFloat( pTable, nRow, 7, &bOk ) };
		if ( !bOk )
		{
			m_pStatus->setText( tr( "Not changed: a value isn't a number." ) );
			RefreshPanels();
			return;
		}
		const int nBone = static_cast<QComboBox *>( pTable->cellWidget( nRow, 1 ) )->currentIndex();
		Commit( tr( "Move Attachment" ), [ nRow, nBone, local = Placed( vecOrigin, vecAngles ) ]( model::Files_t &f ) { model::SetAttachment( f.at( ".mdl" ), nRow, nBone, local ); } );
	};
	connect( pTable, &QTableWidget::itemChanged, this, [ commitRow ]( QTableWidgetItem *pItem ) { commitRow( pItem->row(), pItem->column() ); } );
	connect( pTable, &QTableWidget::itemSelectionChanged, this, [ this, pTable ] {
		const auto rows = pTable->selectionModel()->selectedRows();
		m_pView->SetHighlight( -1, rows.isEmpty() ? -1 : rows.front().row() );
	} );
	connect( pAdd, &QPushButton::clicked, this, [ this ] {
		const QString szName = QInputDialog::getText( this, tr( "Add Attachment" ), tr( "Name:" ) );
		if ( szName.isEmpty() )
			return;
		auto attachments = model::ReadAttachments( m_pDocument->GetMdl() );
		attachments.push_back( { szName.toStdString(), 0, {}, 0 } );
		Commit( tr( "Add Attachment" ), [ attachments ]( model::Files_t &f ) { model::SetAttachments( f.at( ".mdl" ), attachments ); } );
	} );
	connect( pRemove, &QPushButton::clicked, this, [ this, pTable ] {
		auto attachments = model::ReadAttachments( m_pDocument->GetMdl() );
		std::vector<int> rows;
		for ( const auto &index : pTable->selectionModel()->selectedRows() )
			rows.push_back( index.row() );
		std::ranges::sort( rows, std::greater{} );
		for ( const int nRow : rows )
			if ( nRow < static_cast<int>( attachments.size() ) )
				attachments.erase( attachments.begin() + nRow );
		if ( !rows.empty() )
			Commit( tr( "Remove Attachment" ), [ attachments ]( model::Files_t &f ) { model::SetAttachments( f.at( ".mdl" ), attachments ); } );
	} );
	connect( pCopy, &QPushButton::clicked, this, [ this ] {
		const QString szPath = QInputDialog::getText( this, tr( "Copy Attachments" ), tr( "Model to copy attachments from (a game path):" ), QLineEdit::Normal, m_szPath );
		if ( szPath.isEmpty() )
			return;
		QString szError;
		const auto other = ReadOther( szPath, &szError );
		if ( !other )
		{
			m_pStatus->setText( szError );
			return;
		}
		auto attachments = model::ReadAttachments( m_pDocument->GetMdl() );
		int nSkipped = 0;
		for ( auto attachment : model::ReadAttachments( *other->m_pBytes ) )
		{
			const std::string &szBone = other->m_Bones[ static_cast<std::size_t>( attachment.m_nBone ) ].m_szName;
			const auto found = std::ranges::find_if( m_Files[ 0 ].m_Bones, [ &szBone ]( const auto &bone ) { return bone.m_szName == szBone; } );
			const bool bHave = std::ranges::any_of( attachments, [ &attachment ]( const auto &a ) { return a.m_szName == attachment.m_szName; } );
			if ( found == m_Files[ 0 ].m_Bones.end() || bHave )
			{
				nSkipped++;
				continue;
			}
			attachment.m_nBone = static_cast<int>( found - m_Files[ 0 ].m_Bones.begin() );
			attachments.push_back( attachment );
		}
		if ( Commit( tr( "Copy Attachments" ), [ attachments ]( model::Files_t &f ) { model::SetAttachments( f.at( ".mdl" ), attachments ); } ) && nSkipped )
			m_pStatus->setText( tr( "%1 attachments weren't copied: this model has them already or lacks their bones." ).arg( nSkipped ) );
	} );

	m_Refreshers.push_back( [ =, this ] {
		const model::CStudioFile &file = m_Files[ 0 ];
		pAdd->setEnabled( !file.m_Bones.empty() );
		pCopy->setEnabled( !file.m_Bones.empty() );
		const QSignalBlocker block( pTable );
		const int nKeep = pTable->currentRow();
		const auto attachments = model::ReadAttachments( m_pDocument->GetMdl() );
		pTable->setRowCount( static_cast<int>( attachments.size() ) );
		for ( int i = 0; i < static_cast<int>( attachments.size() ); i++ )
		{
			const auto &attachment = attachments[ static_cast<std::size_t>( i ) ];
			pTable->setItem( i, 0, Cell( QString::fromStdString( attachment.m_szName ) ) );
			auto *pBone = BonePicker( file, attachment.m_nBone );
			connect( pBone, &QComboBox::activated, this, [ commitRow, i ] { commitRow( i, 1 ); } );
			pTable->setCellWidget( i, 1, pBone );
			const CVec3 vecOrigin = attachment.m_Local.GetOrigin(), vecAngles = AnglesOf( attachment.m_Local );
			const float values[] = { vecOrigin.m_flX, vecOrigin.m_flY, vecOrigin.m_flZ, vecAngles.m_flX, vecAngles.m_flY, vecAngles.m_flZ };
			for ( int k = 0; k < 6; k++ )
				pTable->setItem( i, 2 + k, Cell( values[ k ] ) );
		}
		if ( nKeep >= 0 && nKeep < pTable->rowCount() )
			pTable->selectRow( nKeep );
	} );
	return pPage;
}

//-----------------------------------------------------------------------------
// Purpose: Retexturing without a compile: material folders, texture names, the skin table, which
//          slot each mesh uses, and taking meshes out.
//-----------------------------------------------------------------------------
QWidget *CModelEditor::BuildMaterialPanel()
{
	auto *pPage = new QWidget;
	auto *pForm = new QFormLayout( pPage );
	auto *pFolders = TextBlock( pForm, tr( "Material folders ($cdmaterials), one per line" ), [ this ]( const QString &szText ) { Commit( tr( "Change Material Folders" ), [ lines = Lines( szText ) ]( model::Files_t &f ) { model::SetFolders( f.at( ".mdl" ), lines ); } ); }, 60 );

	auto *pTextures = Table( { tr( "Texture" ), tr( "Material found" ) } );
	pTextures->setObjectName( QStringLiteral( "modelTextures" ) );
	pTextures->setMinimumHeight( 140 );
	pForm->addRow( Note( tr( "Textures (names under the material folders)" ) ) );
	pForm->addRow( pTextures );
	auto *pTextureButtons = new QHBoxLayout;
	auto *pAddTexture = new QPushButton( tr( "Add Texture…" ) );
	auto *pRemoveTexture = new QPushButton( tr( "Remove Texture" ) );
	pTextureButtons->addWidget( pAddTexture );
	pTextureButtons->addWidget( pRemoveTexture );
	pTextureButtons->addStretch( 1 );
	pForm->addRow( pTextureButtons );
	connect( pTextures, &QTableWidget::itemChanged, this, [ this ]( QTableWidgetItem *pItem ) {
		if ( m_bRefreshing || pItem->column() != 0 )
			return;
		Commit( tr( "Rename Texture" ), [ nRow = pItem->row(), szName = pItem->text().trimmed().toStdString() ]( model::Files_t &f ) { model::Rename( f.at( ".mdl" ), model::Name_t::NAME_TEXTURE, nRow, szName ); } );
	} );
	connect( pAddTexture, &QPushButton::clicked, this, [ this ] {
		const QString szName = QInputDialog::getText( this, tr( "Add Texture" ), tr( "Texture name (a material under one of the folders, without .vmt):" ) );
		if ( !szName.trimmed().isEmpty() )
			Commit( tr( "Add Texture" ), [ szName = szName.trimmed().toStdString() ]( model::Files_t &f ) { model::AddTexture( f.at( ".mdl" ), szName ); } );
	} );
	connect( pRemoveTexture, &QPushButton::clicked, this, [ this, pTextures ] {
		if ( pTextures->currentRow() >= 0 )
			Commit( tr( "Remove Texture" ), [ nRow = pTextures->currentRow() ]( model::Files_t &f ) { model::RemoveTexture( f.at( ".mdl" ), nRow ); } );
	} );

	auto *pSkins = new QTableWidget;
	pSkins->setObjectName( QStringLiteral( "modelSkins" ) );
	pSkins->setMinimumHeight( 120 );
	pForm->addRow( Note( tr( "Skins: the texture each slot uses in each skin" ) ) );
	pForm->addRow( pSkins );
	auto *pSkinButtons = new QHBoxLayout;
	auto *pAddSkin = new QPushButton( tr( "Add Skin" ) );
	auto *pRemoveSkin = new QPushButton( tr( "Remove Skin" ) );
	pSkinButtons->addWidget( pAddSkin );
	pSkinButtons->addWidget( pRemoveSkin );
	pSkinButtons->addStretch( 1 );
	pForm->addRow( pSkinButtons );
	connect( pAddSkin, &QPushButton::clicked, this, [ this, pSkins ] { Commit( tr( "Add Skin" ), [ nFrom = std::max( 0, pSkins->currentRow() ) ]( model::Files_t &f ) { model::AddSkinFamily( f.at( ".mdl" ), nFrom ); } ); } );
	connect( pRemoveSkin, &QPushButton::clicked, this, [ this, pSkins ] { Commit( tr( "Remove Skin" ), [ nRow = pSkins->currentRow() ]( model::Files_t &f ) { model::RemoveSkinFamily( f.at( ".mdl" ), nRow ); } ); } );

	auto *pMeshes = Table( { tr( "Bodygroup model" ), tr( "Mesh" ), tr( "Slot" ) } );
	pMeshes->setObjectName( QStringLiteral( "modelMeshes" ) );
	pMeshes->setMinimumHeight( 120 );
	pForm->addRow( Note( tr( "Meshes: the skin slot each one draws with" ) ) );
	pForm->addRow( pMeshes );
	auto *pRemoveMesh = new QPushButton( tr( "Remove Mesh" ) );
	pRemoveMesh->setToolTip( tr( "Takes the mesh out of every detail level of the .vtx files; the .mdl and .vvd keep its data." ) );
	pForm->addRow( {}, pRemoveMesh );
	connect( pRemoveMesh, &QPushButton::clicked, this, [ this, pMeshes ] {
		const int nRow = pMeshes->currentRow();
		if ( nRow < 0 )
			return;
		const auto meshes = model::ListMeshes( m_pDocument->GetMdl() );
		if ( nRow >= static_cast<int>( meshes.size() ) )
			return;
		const auto &mesh = meshes[ static_cast<std::size_t>( nRow ) ];
		Commit( tr( "Remove Mesh" ), [ mesh ]( model::Files_t &f ) { model::RemoveMesh( f, mesh.m_nBodygroup, mesh.m_nModel, mesh.m_nMesh ); } );
	} );

	m_Refreshers.push_back( [ =, this ] {
		const model::CStudioFile &file = m_Files[ 0 ];
		pFolders->setPlainText( Joined( file.m_Folders ) );
		// Textures and whether a material is found for each.
		QStringList names;
		const int nTextures = static_cast<int>( model::CBinary( m_pDocument->GetMdl() ).Int( 204 ) );
		for ( int i = 0; i < nTextures; i++ )
			names << QString::fromStdString( model::GetName( m_pDocument->GetMdl(), model::Name_t::NAME_TEXTURE, i ) );
		{
			const QSignalBlocker block( pTextures );
			const int nKeep = pTextures->currentRow();
			pTextures->setRowCount( static_cast<int>( names.size() ) );
			for ( int i = 0; i < static_cast<int>( names.size() ); i++ )
			{
				pTextures->setItem( i, 0, Cell( names[ i ] ) );
				QString szFound = tr( "not found" );
				for ( const auto &szFolder : file.m_Folders.empty() ? std::vector<std::string>{ "" } : file.m_Folders )
				{
					std::string szMaterial = szFolder + names[ i ].toStdString();
					std::ranges::replace( szMaterial, '\\', '/' );
					if ( m_pfnRead( "materials/" + szMaterial + ".vmt" ) )
					{
						szFound = QString::fromStdString( "materials/" + szMaterial + ".vmt" );
						break;
					}
				}
				pTextures->setItem( i, 1, Cell( szFound, false ) );
			}
			if ( nKeep >= 0 && nKeep < pTextures->rowCount() )
				pTextures->selectRow( nKeep );
		}
		// The skin table: one picker per slot of each skin.
		{
			const int nKeep = pSkins->currentRow();
			const int nSlots = file.m_Skins.empty() ? 0 : static_cast<int>( file.m_Skins[ 0 ].size() );
			pSkins->clear();
			pSkins->setRowCount( static_cast<int>( file.m_Skins.size() ) );
			pSkins->setColumnCount( nSlots );
			QStringList headers;
			for ( int j = 0; j < nSlots; j++ )
				headers << tr( "Slot %1" ).arg( j );
			pSkins->setHorizontalHeaderLabels( headers );
			for ( int i = 0; i < static_cast<int>( file.m_Skins.size() ); i++ )
			{
				pSkins->setVerticalHeaderItem( i, new QTableWidgetItem( tr( "Skin %1" ).arg( i ) ) );
				for ( int j = 0; j < nSlots; j++ )
				{
					auto *pPick = new QComboBox;
					pPick->addItems( names );
					pPick->setCurrentIndex( file.m_Skins[ static_cast<std::size_t>( i ) ][ static_cast<std::size_t>( j ) ] );
					connect( pPick, &QComboBox::activated, this, [ this, i, j ]( int nTexture ) { Commit( tr( "Change Skin Texture" ), [ i, j, nTexture ]( model::Files_t &f ) { model::SetSkinTexture( f.at( ".mdl" ), i, j, nTexture ); } ); } );
					pSkins->setCellWidget( i, j, pPick );
				}
			}
			if ( nKeep >= 0 && nKeep < pSkins->rowCount() )
				pSkins->selectRow( nKeep );
			pAddSkin->setEnabled( nSlots > 0 );
		}
		// Meshes and their slots.
		{
			const auto meshes = model::ListMeshes( m_pDocument->GetMdl() );
			const int nSlots = file.m_Skins.empty() ? 0 : static_cast<int>( file.m_Skins[ 0 ].size() );
			const int nKeep = pMeshes->currentRow();
			pMeshes->setRowCount( static_cast<int>( meshes.size() ) );
			for ( int i = 0; i < static_cast<int>( meshes.size() ); i++ )
			{
				const auto &mesh = meshes[ static_cast<std::size_t>( i ) ];
				const QString szGroup = mesh.m_nBodygroup < static_cast<int>( file.m_Bodygroups.size() ) ? QString::fromStdString( file.m_Bodygroups[ static_cast<std::size_t>( mesh.m_nBodygroup ) ].m_szName ) : QString::number( mesh.m_nBodygroup );
				pMeshes->setItem( i, 0, Cell( QStringLiteral( "%1 / %2" ).arg( szGroup, QString::fromStdString( mesh.m_szModel ) ), false ) );
				pMeshes->setItem( i, 1, Cell( QString::number( mesh.m_nMesh ), false ) );
				auto *pSlot = new QComboBox;
				for ( int j = 0; j < nSlots; j++ )
				{
					const int nTexture = file.m_Skins[ 0 ][ static_cast<std::size_t>( j ) ];
					pSlot->addItem( QStringLiteral( "%1 · %2" ).arg( j ).arg( nTexture >= 0 && nTexture < static_cast<int>( names.size() ) ? names[ nTexture ] : QString{} ) );
				}
				pSlot->setCurrentIndex( mesh.m_nSlot );
				connect( pSlot, &QComboBox::activated, this, [ this, mesh ]( int nSlot ) { Commit( tr( "Change Mesh Slot" ), [ mesh, nSlot ]( model::Files_t &f ) { model::SetMeshMaterial( f.at( ".mdl" ), mesh.m_nBodygroup, mesh.m_nModel, mesh.m_nMesh, nSlot ); } ); } );
				pMeshes->setCellWidget( i, 2, pSlot );
			}
			if ( nKeep >= 0 && nKeep < pMeshes->rowCount() )
				pMeshes->selectRow( nKeep );
			pRemoveMesh->setEnabled( !meshes.empty() && m_pDocument->Has( ".vvd" ) );
		}
	} );
	return Scrolled( pPage );
}

//-----------------------------------------------------------------------------
// Purpose: Names of bones, bodygroups and their models; pose parameter and controller ranges.
//-----------------------------------------------------------------------------
QWidget *CModelEditor::BuildSkeletonPanel()
{
	auto *pPage = new QWidget;
	auto *pForm = new QFormLayout( pPage );
	auto *pBones = Table( { tr( "Bone" ), tr( "Parent" ) } );
	pBones->setObjectName( QStringLiteral( "modelBones" ) );
	pBones->setMinimumHeight( 160 );
	pForm->addRow( Note( tr( "Bones (renaming one can break animations shared by name with other models)" ) ) );
	pForm->addRow( pBones );
	connect( pBones, &QTableWidget::itemChanged, this, [ this ]( QTableWidgetItem *pItem ) {
		if ( !m_bRefreshing && pItem->column() == 0 )
			Commit( tr( "Rename Bone" ), [ nRow = pItem->row(), szName = pItem->text().toStdString() ]( model::Files_t &f ) { model::Rename( f.at( ".mdl" ), model::Name_t::NAME_BONE, nRow, szName ); } );
	} );

	auto *pGroups = Table( { tr( "Bodygroup" ), tr( "Model" ) } );
	pGroups->setObjectName( QStringLiteral( "modelBodygroups" ) );
	pGroups->setMinimumHeight( 100 );
	pForm->addRow( Note( tr( "Bodygroups and their choices" ) ) );
	pForm->addRow( pGroups );
	connect( pGroups, &QTableWidget::itemChanged, this, [ this ]( QTableWidgetItem *pItem ) {
		if ( m_bRefreshing )
			return;
		const QPoint at = pItem->data( Qt::UserRole ).toPoint();
		const std::string szName = pItem->text().toStdString();
		if ( pItem->column() == 0 )
			Commit( tr( "Rename Bodygroup" ), [ nGroup = at.x(), szName ]( model::Files_t &f ) { model::Rename( f.at( ".mdl" ), model::Name_t::NAME_BODYGROUP, nGroup, szName ); } );
		else
			Commit( tr( "Rename Bodygroup Model" ), [ at, szName ]( model::Files_t &f ) { model::Rename( f.at( ".mdl" ), model::Name_t::NAME_BODYGROUP_MODEL, at.y(), szName, at.x() ); } );
	} );

	auto *pParameters = Table( { tr( "Pose parameter" ), tr( "Start" ), tr( "End" ), tr( "Loop" ) } );
	pParameters->setObjectName( QStringLiteral( "modelPoseParameters" ) );
	pParameters->setMinimumHeight( 90 );
	pForm->addRow( Note( tr( "Pose parameters" ) ) );
	pForm->addRow( pParameters );
	connect( pParameters, &QTableWidget::itemChanged, this, [ this, pParameters ]( QTableWidgetItem *pItem ) {
		if ( m_bRefreshing )
			return;
		const int nRow = pItem->row();
		if ( pItem->column() == 0 )
		{
			Commit( tr( "Rename Pose Parameter" ), [ nRow, szName = pItem->text().toStdString() ]( model::Files_t &f ) { model::Rename( f.at( ".mdl" ), model::Name_t::NAME_POSE_PARAMETER, nRow, szName ); } );
			return;
		}
		bool bOk = true;
		const float flStart = CellFloat( pParameters, nRow, 1, &bOk ), flEnd = CellFloat( pParameters, nRow, 2, &bOk ), flLoop = CellFloat( pParameters, nRow, 3, &bOk );
		if ( !bOk )
		{
			m_pStatus->setText( tr( "Not changed: a value isn't a number." ) );
			RefreshPanels();
			return;
		}
		Commit( tr( "Change Pose Parameter" ), [ nRow, flStart, flEnd, flLoop ]( model::Files_t &f ) { model::SetPoseParameter( f.at( ".mdl" ), nRow, flStart, flEnd, flLoop ); } );
	} );

	auto *pControllers = Table( { tr( "Controller bone" ), tr( "Start" ), tr( "End" ) } );
	pControllers->setMinimumHeight( 70 );
	pForm->addRow( Note( tr( "Bone controllers" ) ) );
	pForm->addRow( pControllers );
	connect( pControllers, &QTableWidget::itemChanged, this, [ this, pControllers ]( QTableWidgetItem *pItem ) {
		if ( m_bRefreshing )
			return;
		bool bOk = true;
		const int nRow = pItem->row();
		const float flStart = CellFloat( pControllers, nRow, 1, &bOk ), flEnd = CellFloat( pControllers, nRow, 2, &bOk );
		if ( !bOk )
		{
			m_pStatus->setText( tr( "Not changed: a value isn't a number." ) );
			RefreshPanels();
			return;
		}
		Commit( tr( "Change Bone Controller" ), [ nRow, flStart, flEnd ]( model::Files_t &f ) { model::SetController( f.at( ".mdl" ), nRow, flStart, flEnd ); } );
	} );

	m_Refreshers.push_back( [ =, this ] {
		const model::CStudioFile &file = m_Files[ 0 ];
		{
			const QSignalBlocker block( pBones );
			pBones->setRowCount( static_cast<int>( file.m_Bones.size() ) );
			for ( int i = 0; i < static_cast<int>( file.m_Bones.size() ); i++ )
			{
				const auto &bone = file.m_Bones[ static_cast<std::size_t>( i ) ];
				pBones->setItem( i, 0, Cell( QString::fromStdString( bone.m_szName ) ) );
				const bool bParent = bone.m_nParent >= 0 && bone.m_nParent < static_cast<int>( file.m_Bones.size() );
				pBones->setItem( i, 1, Cell( bParent ? QString::fromStdString( file.m_Bones[ static_cast<std::size_t>( bone.m_nParent ) ].m_szName ) : QString{}, false ) );
			}
		}
		{
			const QSignalBlocker block( pGroups );
			pGroups->setRowCount( 0 );
			for ( int g = 0; g < static_cast<int>( file.m_Bodygroups.size() ); g++ )
				for ( int m = 0; m < static_cast<int>( file.m_Bodygroups[ static_cast<std::size_t>( g ) ].m_Models.size() ); m++ )
				{
					const int nRow = pGroups->rowCount();
					pGroups->insertRow( nRow );
					auto *pGroup = Cell( m == 0 ? QString::fromStdString( file.m_Bodygroups[ static_cast<std::size_t>( g ) ].m_szName ) : QString{}, m == 0 );
					pGroup->setData( Qt::UserRole, QPoint( g, m ) );
					pGroups->setItem( nRow, 0, pGroup );
					auto *pModel = Cell( QString::fromStdString( file.m_Bodygroups[ static_cast<std::size_t>( g ) ].m_Models[ static_cast<std::size_t>( m ) ] ) );
					pModel->setData( Qt::UserRole, QPoint( g, m ) );
					pGroups->setItem( nRow, 1, pModel );
				}
		}
		{
			const QSignalBlocker block( pParameters );
			pParameters->setRowCount( static_cast<int>( file.m_Parameters.size() ) );
			for ( int i = 0; i < static_cast<int>( file.m_Parameters.size() ); i++ )
			{
				const auto &parameter = file.m_Parameters[ static_cast<std::size_t>( i ) ];
				pParameters->setItem( i, 0, Cell( QString::fromStdString( parameter.m_szName ) ) );
				pParameters->setItem( i, 1, Cell( parameter.m_flStart ) );
				pParameters->setItem( i, 2, Cell( parameter.m_flEnd ) );
				pParameters->setItem( i, 3, Cell( parameter.m_flLoop ) );
			}
		}
		{
			const QSignalBlocker block( pControllers );
			pControllers->setRowCount( static_cast<int>( file.m_Controllers.size() ) );
			for ( int i = 0; i < static_cast<int>( file.m_Controllers.size() ); i++ )
			{
				const auto &controller = file.m_Controllers[ static_cast<std::size_t>( i ) ];
				const bool bBone = controller.m_nBone >= 0 && controller.m_nBone < static_cast<int>( file.m_Bones.size() );
				pControllers->setItem( i, 0, Cell( bBone ? QString::fromStdString( file.m_Bones[ static_cast<std::size_t>( controller.m_nBone ) ].m_szName ) : QString::number( controller.m_nBone ), false ) );
				pControllers->setItem( i, 1, Cell( controller.m_flStart ) );
				pControllers->setItem( i, 2, Cell( controller.m_flEnd ) );
			}
		}
	} );
	return Scrolled( pPage );
}

//-----------------------------------------------------------------------------
// Purpose: Whole-model edits that reach the vertex files: scale, quarter turns and moves, detail
//          levels and normals.
//-----------------------------------------------------------------------------
QWidget *CModelEditor::BuildTransformPanel()
{
	auto *pPage = new QWidget;
	auto *pLayout = new QVBoxLayout( pPage );
	const bool bVertices = m_pDocument->Has( ".vvd" );

	auto *pScaleBox = new QGroupBox( tr( "Scale" ) );
	auto *pScaleForm = new QFormLayout( pScaleBox );
	auto *pScale = Number( 0.01, 100, 4 );
	pScale->setValue( 1 );
	pScale->setSingleStep( 0.1 );
	pScale->setObjectName( QStringLiteral( "modelScale" ) );
	auto *pApplyScale = new QPushButton( tr( "Scale Model" ) );
	pScaleForm->addRow( tr( "Factor" ), pScale );
	pScaleForm->addRow( Note( tr( "Bones, hitboxes, attachments, bounds, animations, vertices and the collision hull all scale together." ) ) );
	pScaleForm->addRow( {}, pApplyScale );
	connect( pApplyScale, &QPushButton::clicked, this, [ this, pScale ] { Commit( tr( "Scale Model" ), [ fl = static_cast<float>( pScale->value() ) ]( model::Files_t &f ) { model::ScaleModel( f, fl ); } ); } );
	pLayout->addWidget( pScaleBox );

	auto *pPlaceBox = new QGroupBox( tr( "Turn and move" ) );
	auto *pPlaceForm = new QFormLayout( pPlaceBox );
	std::array<QComboBox *, 3> turns{};
	const char *turnNames[] = { QT_TR_NOOP( "Pitch" ), QT_TR_NOOP( "Yaw" ), QT_TR_NOOP( "Roll" ) };
	for ( std::size_t i = 0; i < 3; i++ )
	{
		turns[ i ] = new QComboBox;
		for ( const int nDegrees : { 0, 90, 180, 270 } )
			turns[ i ]->addItem( QStringLiteral( "%1°" ).arg( nDegrees ) );
		pPlaceForm->addRow( tr( turnNames[ i ] ), turns[ i ] );
	}
	Vector_t move{};
	pPlaceForm->addRow( tr( "Move" ), VectorRow( move ) );
	auto *pWhy = new QLabel;
	pWhy->setWordWrap( true );
	pPlaceForm->addRow( pWhy );
	auto *pApplyPlace = new QPushButton( tr( "Turn and Move Model" ) );
	pPlaceForm->addRow( {}, pApplyPlace );
	connect( pApplyPlace, &QPushButton::clicked, this, [ this, turns, move ] {
		model::Placement_t placement{ turns[ 0 ]->currentIndex(), turns[ 1 ]->currentIndex(), turns[ 2 ]->currentIndex(), ValueOf( move ) };
		Commit( tr( "Turn and Move Model" ), [ placement ]( model::Files_t &f ) { model::PlaceModel( f, placement ); } );
	} );
	pLayout->addWidget( pPlaceBox );

	auto *pLodBox = new QGroupBox( tr( "Detail levels" ) );
	auto *pLodForm = new QFormLayout( pLodBox );
	auto *pLods = new QLabel;
	pLods->setWordWrap( true );
	auto *pKeep = new QSpinBox;
	pKeep->setMinimum( 1 );
	auto *pApplyLods = new QPushButton( tr( "Limit Detail Levels" ) );
	pLodForm->addRow( pLods );
	pLodForm->addRow( tr( "Levels to keep" ), pKeep );
	pLodForm->addRow( {}, pApplyLods );
	connect( pApplyLods, &QPushButton::clicked, this, [ this, pKeep ] { Commit( tr( "Limit Detail Levels" ), [ n = pKeep->value() ]( model::Files_t &f ) { model::LimitLods( f, n ); } ); } );
	pLayout->addWidget( pLodBox );

	auto *pNormalBox = new QGroupBox( tr( "Normals" ) );
	auto *pNormalForm = new QFormLayout( pNormalBox );
	auto *pAngle = Number( 0, 180, 1 );
	pAngle->setValue( 60 );
	pAngle->setSuffix( QStringLiteral( "°" ) );
	pAngle->setToolTip( tr( "Faces meeting at a sharper angle than this keep a hard edge." ) );
	auto *pApplyNormals = new QPushButton( tr( "Smooth Normals Again" ) );
	pNormalForm->addRow( tr( "Hard edge above" ), pAngle );
	pNormalForm->addRow( {}, pApplyNormals );
	connect( pApplyNormals, &QPushButton::clicked, this, [ this, pAngle ] { Commit( tr( "Smooth Normals" ), [ fl = static_cast<float>( pAngle->value() ) ]( model::Files_t &f ) { model::RecomputeNormals( f, fl ); } ); } );
	pLayout->addWidget( pNormalBox );
	pLayout->addStretch( 1 );

	m_Refreshers.push_back( [ =, this ] {
		const std::string szWhy = model::CanPlace( m_pDocument->GetFiles() );
		pWhy->setText( szWhy.empty() ? tr( "Quarter turns about the model's origin, then the move. For props: the bone stays, and the vertices, hitboxes, attachments and hull turn around it." ) : QString::fromStdString( szWhy ) );
		pApplyPlace->setEnabled( szWhy.empty() && bVertices );
		pApplyScale->setEnabled( bVertices );
		pApplyNormals->setEnabled( bVertices );
		const auto points = model::ReadLodSwitchPoints( m_pDocument->GetFiles() );
		QStringList shown;
		int nLevels = 0, nUsed = 0;
		for ( std::size_t i = 0; i < points.size(); i++ )
		{
			if ( points[ i ] < 0 )
				shown << tr( "%1: shadow" ).arg( i );
			else
			{
				shown << ( points[ i ] >= 1e30f ? tr( "%1: never" ).arg( i ) : tr( "%1: from %2" ).arg( i ).arg( points[ i ] ) );
				nLevels++;
				if ( points[ i ] < 1e30f && nUsed == nLevels - 1 )
					nUsed++;
			}
		}
		pLods->setText( shown.isEmpty() ? tr( "No detail levels." ) : tr( "Levels and the distance each starts at: %1" ).arg( shown.join( QStringLiteral( " · " ) ) ) );
		pKeep->setMaximum( std::max( 1, nLevels ) );
		pKeep->setValue( std::max( 1, nUsed ) );
		pApplyLods->setEnabled( nLevels > 1 );
	} );
	return Scrolled( pPage );
}

//-----------------------------------------------------------------------------
// Purpose: The picked sequence's name, activity, timing and switches, and its events.
//-----------------------------------------------------------------------------
QWidget *CModelEditor::BuildSequenceEditor()
{
	auto *pBox = new QGroupBox( tr( "Edit sequence" ) );
	auto *pForm = new QFormLayout( pBox );
	auto *pNote = new QLabel;
	pNote->setWordWrap( true );
	pForm->addRow( pNote );
	auto *pName = new QLineEdit;
	pName->setObjectName( QStringLiteral( "modelSequenceName" ) );
	auto *pActivity = new QLineEdit;
	auto *pWeight = new QSpinBox;
	pWeight->setRange( -1000000, 1000000 );
	auto *pFadeIn = Number( 0, 60, 3 );
	auto *pFadeOut = Number( 0, 60, 3 );
	auto *pFps = Number( 0.1, 1000, 2 );
	pFps->setObjectName( QStringLiteral( "modelSequenceFps" ) );
	pForm->addRow( tr( "Name" ), pName );
	pForm->addRow( tr( "Activity" ), pActivity );
	pForm->addRow( tr( "Activity weight" ), pWeight );
	pForm->addRow( tr( "Fade in" ), pFadeIn );
	pForm->addRow( tr( "Fade out" ), pFadeOut );
	pForm->addRow( tr( "Frame rate" ), pFps );
	struct Switch_t
	{
		int m_nBit;
		const char *m_pszName;
		QCheckBox *m_pBox = nullptr;
	};
	auto switches = std::make_shared<std::vector<Switch_t>>( std::vector<Switch_t>{
		{ model::SEQUENCE_LOOPING, QT_TR_NOOP( "Looping" ) },
		{ model::SEQUENCE_SNAP, QT_TR_NOOP( "Snap (no blending in)" ) },
		{ model::SEQUENCE_AUTOPLAY, QT_TR_NOOP( "Always playing (autoplay)" ) },
		{ model::SEQUENCE_REALTIME, QT_TR_NOOP( "Real time (cycle from the clock)" ) },
		{ model::SEQUENCE_HIDDEN, QT_TR_NOOP( "Hidden" ) } } );
	auto *pSwitches = new QWidget;
	auto *pSwitchLayout = new QVBoxLayout( pSwitches );
	pSwitchLayout->setContentsMargins( 0, 0, 0, 0 );
	for ( auto &entry : *switches )
	{
		entry.m_pBox = new QCheckBox( tr( entry.m_pszName ) );
		pSwitchLayout->addWidget( entry.m_pBox );
	}
	pForm->addRow( pSwitches );
	auto *pEvents = Table( { tr( "Frame" ), tr( "Event" ), tr( "Name" ), tr( "Options" ) } );
	pEvents->setObjectName( QStringLiteral( "modelSequenceEvents" ) );
	pEvents->setMinimumHeight( 110 );
	pForm->addRow( pEvents );
	auto *pEventButtons = new QHBoxLayout;
	auto *pAddEvent = new QPushButton( tr( "Add Event" ) );
	auto *pRemoveEvent = new QPushButton( tr( "Remove Event" ) );
	pEventButtons->addWidget( pAddEvent );
	pEventButtons->addWidget( pRemoveEvent );
	pEventButtons->addStretch( 1 );
	pForm->addRow( pEventButtons );

	// The picked sequence of this model (not an included one), its frames and first animation.
	const auto picked = [ this ]() -> int { return m_nSource == 0 && m_nSequence >= 0 && m_nSequence < static_cast<int>( m_Files[ 0 ].m_Sequences.size() ) ? m_nSequence : -1; };
	const auto animationOf = [ this ]( int nSequence ) -> int {
		const auto &seq = m_Files[ 0 ].m_Sequences[ static_cast<std::size_t>( nSequence ) ];
		return !seq.m_Animations.empty() && seq.m_Animations[ 0 ] >= 0 && seq.m_Animations[ 0 ] < static_cast<int>( m_Files[ 0 ].m_Animations.size() ) ? seq.m_Animations[ 0 ] : -1;
	};
	const auto framesOf = [ this, animationOf ]( int nSequence ) {
		const int nAnimation = animationOf( nSequence );
		return nAnimation < 0 ? 1 : std::max( 1, m_Files[ 0 ].m_Animations[ static_cast<std::size_t>( nAnimation ) ].m_nFrames );
	};
	const auto commitFields = [ this, picked, pWeight, pFadeIn, pFadeOut, switches ] {
		const int nSequence = picked();
		if ( m_bRefreshing || nSequence < 0 )
			return;
		model::SequenceFields_t fields{ 0, pWeight->value(), static_cast<float>( pFadeIn->value() ), static_cast<float>( pFadeOut->value() ) };
		for ( const auto &entry : *switches )
			if ( entry.m_pBox->isChecked() )
				fields.m_nFlags |= entry.m_nBit;
		Commit( tr( "Change Sequence" ), [ nSequence, fields ]( model::Files_t &f ) { model::SetSequence( f.at( ".mdl" ), nSequence, fields ); } );
	};
	connect( pWeight, &QSpinBox::editingFinished, this, commitFields );
	connect( pFadeIn, &QDoubleSpinBox::editingFinished, this, commitFields );
	connect( pFadeOut, &QDoubleSpinBox::editingFinished, this, commitFields );
	for ( const auto &entry : *switches )
		connect( entry.m_pBox, &QCheckBox::clicked, this, commitFields );
	const auto rename = [ this, picked ]( model::Name_t nWhat, QLineEdit *pEdit, const QString &szName ) {
		connect( pEdit, &QLineEdit::editingFinished, this, [ this, picked, nWhat, pEdit, szName ] {
			if ( const int nSequence = picked(); nSequence >= 0 && !m_bRefreshing )
				Commit( szName, [ nSequence, nWhat, szText = pEdit->text().toStdString() ]( model::Files_t &f ) { model::Rename( f.at( ".mdl" ), nWhat, nSequence, szText ); } );
		} );
	};
	rename( model::Name_t::NAME_SEQUENCE, pName, tr( "Rename Sequence" ) );
	rename( model::Name_t::NAME_ACTIVITY, pActivity, tr( "Change Activity" ) );
	connect( pFps, &QDoubleSpinBox::editingFinished, this, [ this, picked, animationOf, pFps ] {
		const int nSequence = picked();
		if ( m_bRefreshing || nSequence < 0 || animationOf( nSequence ) < 0 )
			return;
		Commit( tr( "Change Frame Rate" ), [ nAnimation = animationOf( nSequence ), fl = static_cast<float>( pFps->value() ) ]( model::Files_t &f ) { model::SetAnimationFps( f.at( ".mdl" ), nAnimation, fl ); } );
	} );
	connect( pEvents, &QTableWidget::itemChanged, this, [ this, picked, framesOf, pEvents ]( QTableWidgetItem *pItem ) {
		const int nSequence = picked();
		if ( m_bRefreshing || nSequence < 0 )
			return;
		const int nRow = pItem->row();
		const auto &events = m_Files[ 0 ].m_Sequences[ static_cast<std::size_t>( nSequence ) ].m_Events;
		if ( nRow >= static_cast<int>( events.size() ) )
			return;
		model::Event_t event = events[ static_cast<std::size_t>( nRow ) ];
		bool bOk = true, bNumber = true;
		const float flFrame = CellFloat( pEvents, nRow, 0, &bOk );
		event.m_nEvent = pEvents->item( nRow, 1 )->text().toInt( &bNumber );
		event.m_szName = pEvents->item( nRow, 2 )->text().toStdString();
		event.m_szOptions = pEvents->item( nRow, 3 )->text().toStdString();
		if ( !bOk || !bNumber )
		{
			m_pStatus->setText( tr( "Not changed: the frame and event are numbers." ) );
			RefreshPanels();
			return;
		}
		const int nFrames = framesOf( nSequence );
		event.m_flCycle = nFrames > 1 ? std::clamp( flFrame / static_cast<float>( nFrames - 1 ), 0.0f, 1.0f ) : 0.0f;
		Commit( tr( "Change Event" ), [ nSequence, nRow, event ]( model::Files_t &f ) { model::SetEvent( f.at( ".mdl" ), nSequence, nRow, event ); } );
	} );
	connect( pAddEvent, &QPushButton::clicked, this, [ this, picked ] {
		const int nSequence = picked();
		if ( nSequence < 0 )
			return;
		auto events = m_Files[ 0 ].m_Sequences[ static_cast<std::size_t>( nSequence ) ].m_Events;
		std::erase_if( events, []( const model::Event_t &event ) { return !event.m_bValidCycle; } );
		// New-style events are named (AE_CL_PLAYSOUND...) and flagged so the engine looks the name up.
		events.push_back( { m_flCycle, 0, true, "AE_CL_PLAYSOUND", "", 1 << 10 } );
		Commit( tr( "Add Event" ), [ nSequence, events ]( model::Files_t &f ) { model::SetEvents( f.at( ".mdl" ), nSequence, events ); } );
	} );
	connect( pRemoveEvent, &QPushButton::clicked, this, [ this, picked, pEvents ] {
		const int nSequence = picked();
		const int nRow = pEvents->currentRow();
		if ( nSequence < 0 || nRow < 0 )
			return;
		auto events = m_Files[ 0 ].m_Sequences[ static_cast<std::size_t>( nSequence ) ].m_Events;
		if ( nRow >= static_cast<int>( events.size() ) )
			return;
		events.erase( events.begin() + nRow );
		std::erase_if( events, []( const model::Event_t &event ) { return !event.m_bValidCycle; } );
		Commit( tr( "Remove Event" ), [ nSequence, events ]( model::Files_t &f ) { model::SetEvents( f.at( ".mdl" ), nSequence, events ); } );
	} );

	m_RefreshSequence = [ =, this ] {
		const int nSequence = picked();
		pBox->setEnabled( nSequence >= 0 );
		pNote->setText( m_nSource != 0 ? tr( "This sequence comes from an included model; open that model to change it." ) : nSequence < 0 ? tr( "Pick a sequence to change it." )
																																		   : QString{} );
		pNote->setVisible( !pNote->text().isEmpty() );
		if ( nSequence < 0 )
			return;
		const auto &seq = m_Files[ 0 ].m_Sequences[ static_cast<std::size_t>( nSequence ) ];
		const QSignalBlocker blockName( pName ), blockActivity( pActivity ), blockWeight( pWeight ), blockIn( pFadeIn ), blockOut( pFadeOut ), blockFps( pFps ), blockEvents( pEvents );
		pName->setText( QString::fromStdString( seq.m_szName ) );
		pActivity->setText( QString::fromStdString( seq.m_szActivity ) );
		pWeight->setValue( seq.m_nWeight );
		pFadeIn->setValue( seq.m_flFadeIn );
		pFadeOut->setValue( seq.m_flFadeOut );
		const int nAnimation = animationOf( nSequence );
		pFps->setEnabled( nAnimation >= 0 );
		pFps->setValue( nAnimation >= 0 ? m_Files[ 0 ].m_Animations[ static_cast<std::size_t>( nAnimation ) ].m_flFps : 30 );
		for ( const auto &entry : *switches )
		{
			const QSignalBlocker block( entry.m_pBox );
			entry.m_pBox->setChecked( seq.m_nFlags & entry.m_nBit );
		}
		const int nFrames = framesOf( nSequence );
		const int nKeep = pEvents->currentRow();
		pEvents->setRowCount( static_cast<int>( seq.m_Events.size() ) );
		for ( int i = 0; i < static_cast<int>( seq.m_Events.size() ); i++ )
		{
			const auto &event = seq.m_Events[ static_cast<std::size_t>( i ) ];
			pEvents->setItem( i, 0, Cell( event.m_flCycle * static_cast<float>( std::max( 0, nFrames - 1 ) ) ) );
			pEvents->setItem( i, 1, Cell( QString::number( event.m_nEvent ) ) );
			pEvents->setItem( i, 2, Cell( QString::fromStdString( event.m_szName ) ) );
			pEvents->setItem( i, 3, Cell( QString::fromStdString( event.m_szOptions ) ) );
		}
		if ( nKeep >= 0 && nKeep < pEvents->rowCount() )
			pEvents->selectRow( nKeep );
	};
	m_Refreshers.push_back( [ this ] { m_RefreshSequence(); } );
	return pBox;
}
