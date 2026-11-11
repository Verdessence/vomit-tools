//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapEntityFields.h.
//
//===========================================================================//
#include "MapEntityFields.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iterator>

#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

#include "map/Edit.h"
#include "map/Fgd.h"
#include "map/MapEditor.h"
#include "map/Primitives.h"
#include "map/Properties.h"

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Text( std::string_view sz )
{
	return QString::fromUtf8( sz.data(), static_cast<qsizetype>( sz.size() ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool SameText( std::string_view a, std::string_view b )
{
	return std::ranges::equal( a, b, []( char x, char y ) { return std::tolower( static_cast<unsigned char>( x ) ) == std::tolower( static_cast<unsigned char>( y ) ); } );
}

//-----------------------------------------------------------------------------
// Purpose: Whether a type is a colour: color255 is 0-255 (and a brightness after for lights),
//          color1 is 0-1.
//-----------------------------------------------------------------------------
bool IsColour( const std::string &szType )
{
	return szType == "color255" || szType == "color1";
}

//-----------------------------------------------------------------------------
// Purpose: A colour value's first three numbers as a colour (255 for those missing).
//-----------------------------------------------------------------------------
QColor ColourOf( const QString &szValue, bool bUnit )
{
	const QStringList parts = szValue.split( u' ', Qt::SkipEmptyParts );
	std::array<int, 3> rgb{ 255, 255, 255 };
	for ( qsizetype i = 0; i < 3 && i < parts.size(); i++ )
	{
		const double fl = parts[ i ].toDouble();
		rgb[ static_cast<std::size_t>( i ) ] = std::clamp( static_cast<int>( bUnit ? fl * 255.0 + 0.5 : fl ), 0, 255 );
	}
	if ( parts.size() == 1 )
	{
		rgb.fill( rgb[ 0 ] );
	}
	return QColor( rgb[ 0 ], rgb[ 1 ], rgb[ 2 ] );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapEntityFields::CMapEntityFields( CMapEditor &editor, std::vector<std::string> classes, std::vector<EntityField_t> fields, QWidget *pParent )
	: QWidget( pParent ),
	  m_Editor( editor ),
	  m_Classes( std::move( classes ) )
{
	setObjectName( QStringLiteral( "mapEntityFields_" ) + Text( m_Classes.front() ) );
	for ( EntityField_t &spec : fields )
	{
		m_Fields.push_back( { std::move( spec ), {}, nullptr, nullptr } );
	}
	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	auto *pHead = new QHBoxLayout;
	m_pTitle = new QLabel;
	m_pTitle->setWordWrap( true );
	m_pTitle->setTextInteractionFlags( Qt::TextSelectableByMouse );
	pHead->addWidget( m_pTitle, 1 );
	m_pSelect = new QPushButton( tr( "Select" ) );
	m_pSelect->setObjectName( QStringLiteral( "mapEntityFieldsSelect" ) );
	m_pSelect->setToolTip( tr( "Select the entity (its every key: Object Properties)" ) );
	connect( m_pSelect, &QPushButton::clicked, this, [ this ] {
		if ( const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene(); pScene && m_nEntity )
			m_Editor.GetDocument().SetSelection( { pScene->EntityObject( *m_nEntity ) } );
	} );
	pHead->addWidget( m_pSelect );
	m_pAdd = new QPushButton( tr( "Add" ) );
	m_pAdd->setObjectName( QStringLiteral( "mapEntityFieldsAdd" ) );
	m_pAdd->setToolTip( tr( "Make a %1 at the 3D camera" ).arg( Text( m_Classes.front() ) ) );
	connect( m_pAdd, &QPushButton::clicked, this, &CMapEntityFields::AddEntity );
	pHead->addWidget( m_pAdd );
	pLayout->addLayout( pHead );
	m_pForm = new QWidget;
	m_pFormLayout = new QFormLayout( m_pForm );
	m_pFormLayout->setContentsMargins( 0, 0, 0, 0 );
	m_pFormLayout->setFieldGrowthPolicy( QFormLayout::AllNonFixedFieldsGrow );
	pLayout->addWidget( m_pForm );
	connect( &m_Editor.GetDocument(), &CMapDocument::SceneEdited, this, &CMapEntityFields::Refresh );
	connect( &m_Editor.GetDocument(), &CMapDocument::Loaded, this, &CMapEntityFields::Refresh );
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEntityFields::SetSelectButton( bool bShown )
{
	m_bSelectButton = bShown;
	m_pSelect->setVisible( m_bSelectButton && m_nEntity.has_value() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEntityFields::SetEntityId( int nId )
{
	if ( nId == m_nId )
		return;
	m_nId = nId;
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose: The entity of that id while it is one of the classes; else the map's first.
//-----------------------------------------------------------------------------
std::optional<std::size_t> CMapEntityFields::FindEntity() const
{
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene )
		return std::nullopt;
	const auto &entities = std::as_const( pScene->m_Map.m_Entities );
	std::optional<std::size_t> first;
	for ( std::size_t e = 0; e < entities.size(); e++ )
	{
		const xv::map::Entity_t &entity = entities[ e ];
		if ( entity.m_bDeleted || std::ranges::none_of( m_Classes, [ & ]( const std::string &szClass ) { return SameText( entity.GetClassname(), szClass ); } ) )
			continue;
		if ( m_nId != 0 && entity.m_nId == m_nId )
			return e;
		if ( !first )
			first = e;
	}
	return first;
}

//-----------------------------------------------------------------------------
// Purpose: An output naming the entity (by a target that matches its name) with that input;
//          one from a logic_auto's OnMapSpawn first, as that is the value it starts with.
//-----------------------------------------------------------------------------
std::optional<CMapEntityFields::Sender_t> CMapEntityFields::FindSender( const std::string &szInput ) const
{
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene || !m_nEntity )
		return std::nullopt;
	const auto &entities = std::as_const( pScene->m_Map.m_Entities );
	const std::string_view szName = entities[ *m_nEntity ].GetValue( "targetname" );
	if ( szName.empty() )
		return std::nullopt;
	std::optional<Sender_t> any;
	for ( std::size_t e = 0; e < entities.size(); e++ )
	{
		const xv::map::Entity_t &entity = entities[ e ];
		if ( entity.m_bDeleted )
			continue;
		for ( std::size_t c = 0; c < entity.m_Connections.size(); c++ )
		{
			const xv::map::Connection_t &connection = entity.m_Connections[ c ];
			if ( !SameText( connection.m_szInput, szInput ) || !xv::map::TargetMatches( connection.m_szTarget, szName ) )
				continue;
			if ( SameText( entity.GetClassname(), "logic_auto" ) && SameText( connection.m_szOutput, "OnMapSpawn" ) )
				return Sender_t{ e, c };
			if ( !any )
				any = Sender_t{ e, c };
		}
	}
	return any;
}

//-----------------------------------------------------------------------------
// Purpose: The editors for the fields, by the FGD's types for the first class.
//-----------------------------------------------------------------------------
void CMapEntityFields::Build()
{
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	const xv::map::FgdClass_t *pClass = pScene && pScene->m_pFgd ? pScene->m_pFgd->Find( m_Classes.front() ) : nullptr;
	while ( m_pFormLayout->rowCount() > 0 )
		m_pFormLayout->removeRow( 0 );
	for ( Field_t &field : m_Fields )
	{
		const xv::map::FgdKey_t *pKey = pClass && !field.m_Spec.m_szKey.empty() ? pClass->FindKey( field.m_Spec.m_szKey ) : nullptr;
		field.m_szType = pKey ? pKey->m_szType : std::string{};
		QString szLabel = field.m_Spec.m_szLabel;
		if ( szLabel.isEmpty() )
			szLabel = pKey && !pKey->m_szDisplayName.empty() ? Text( pKey->m_szDisplayName ) : Text( field.m_Spec.m_szKey.empty() ? field.m_Spec.m_szInput : field.m_Spec.m_szKey );
		QString szHelp = field.m_Spec.m_szHelp;
		if ( szHelp.isEmpty() && pKey )
			szHelp = Text( pKey->m_szDescription );
		const std::string szId = field.m_Spec.m_szKey.empty() ? field.m_Spec.m_szInput : field.m_Spec.m_szKey;
		const QString szObject = QStringLiteral( "mapField_" ) + Text( szId );
		if ( pKey && pKey->m_szType == "choices" && !pKey->m_Choices.empty() )
		{
			auto *pCombo = new QComboBox;
			pCombo->setObjectName( szObject );
			for ( const xv::map::FgdChoice_t &choice : pKey->m_Choices )
				pCombo->addItem( Text( choice.m_szName.empty() ? choice.m_szValue : choice.m_szName ), Text( choice.m_szValue ) );
			connect( pCombo, &QComboBox::activated, this, [ this, pCombo, szId ]( int nIndex ) { CommitKey( szId, pCombo->itemData( nIndex ).toString().toStdString() ); } );
			field.m_pEditor = pCombo;
		}
		else
		{
			auto *pLine = new QLineEdit;
			pLine->setObjectName( szObject );
			const bool bInput = field.m_Spec.m_szKey.empty();
			connect( pLine, &QLineEdit::editingFinished, this, [ this, pLine, szId, bInput ] {
				if ( m_bFilling || !pLine->isModified() )
					return;
				pLine->setModified( false );
				const std::string szText = pLine->text().trimmed().toStdString();
				if ( bInput )
					CommitInput( szId, szText );
				else
					CommitKey( szId, szText );
			} );
			field.m_pEditor = pLine;
			if ( IsColour( field.m_szType ) )
			{
				// The colour button beside the numbers, which keep a light's brightness after them.
				auto *pRow = new QWidget;
				auto *pRowLayout = new QHBoxLayout( pRow );
				pRowLayout->setContentsMargins( 0, 0, 0, 0 );
				auto *pSwatch = new QToolButton;
				pSwatch->setObjectName( szObject + QStringLiteral( "Colour" ) );
				pSwatch->setToolTip( tr( "Pick the colour" ) );
				pSwatch->setFixedWidth( pSwatch->sizeHint().height() * 3 / 2 );
				const std::size_t nField = static_cast<std::size_t>( &field - m_Fields.data() );
				connect( pSwatch, &QToolButton::clicked, this, [ this, nField ] { PickColour( m_Fields[ nField ] ); } );
				pRowLayout->addWidget( pSwatch );
				pRowLayout->addWidget( pLine, 1 );
				field.m_pLabel = new QLabel( szLabel );
				field.m_pLabel->setToolTip( szHelp );
				pLine->setToolTip( szHelp );
				m_pFormLayout->addRow( field.m_pLabel, pRow );
				continue;
			}
		}
		field.m_pLabel = new QLabel( szLabel );
		field.m_pLabel->setToolTip( szHelp );
		field.m_pEditor->setToolTip( szHelp );
		m_pFormLayout->addRow( field.m_pLabel, field.m_pEditor );
	}
}

//-----------------------------------------------------------------------------
// Purpose: A key's value as the entity has it (empty when it hasn't the key), or the
//          parameter of the input the map sends it.
//-----------------------------------------------------------------------------
QString CMapEntityFields::ValueOf( const Field_t &field ) const
{
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene || !m_nEntity )
		return {};
	if ( !field.m_Spec.m_szKey.empty() )
		return Text( std::as_const( pScene->m_Map.m_Entities )[ *m_nEntity ].GetValue( field.m_Spec.m_szKey ) );
	const std::optional<Sender_t> sender = FindSender( field.m_Spec.m_szInput );
	return sender ? Text( std::as_const( pScene->m_Map.m_Entities )[ sender->m_nEntity ].m_Connections[ sender->m_nConnection ].m_szParameter ) : QString{};
}

//-----------------------------------------------------------------------------
// Purpose: The entity found again, and every field showing its value (the game's default,
//          greyed, where it hasn't the key).
//-----------------------------------------------------------------------------
void CMapEntityFields::Refresh()
{
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	const std::optional<std::size_t> nBefore = m_nEntity;
	m_nEntity = FindEntity();
	const xv::map::FgdClass_t *pClass = pScene && pScene->m_pFgd ? pScene->m_pFgd->Find( m_Classes.front() ) : nullptr;
	// The game data comes with the map: the editors are made by its types once it is there.
	const bool bTypesKnown = std::ranges::any_of( m_Fields, []( const Field_t &field ) { return !field.m_szType.empty(); } );
	if ( !m_Fields.empty() && ( !m_Fields.front().m_pEditor || ( pClass && !bTypesKnown ) ) )
		Build();
	m_bFilling = true;
	if ( m_nEntity )
	{
		const xv::map::Entity_t &entity = std::as_const( pScene->m_Map.m_Entities )[ *m_nEntity ];
		const std::string_view szName = entity.GetValue( "targetname" );
		m_pTitle->setText( szName.empty() ? Text( entity.GetClassname() ) : tr( "%1 “%2”" ).arg( Text( entity.GetClassname() ), Text( szName ) ) );
	}
	else
	{
		m_pTitle->setText( tr( "The map has no %1." ).arg( Text( m_Classes.front() ) ) );
	}
	m_pSelect->setVisible( m_bSelectButton && m_nEntity.has_value() );
	m_pAdd->setVisible( !m_nEntity && pScene );
	// Without the entity there is nothing to fill in: Add first.
	m_pForm->setVisible( m_nEntity.has_value() );
	for ( const Field_t &field : m_Fields )
	{
		const QString szValue = ValueOf( field );
		const xv::map::FgdKey_t *pKey = pClass && !field.m_Spec.m_szKey.empty() ? pClass->FindKey( field.m_Spec.m_szKey ) : nullptr;
		const QString szDefault = pKey ? Text( pKey->m_szDefault ) : QString{};
		if ( auto *pCombo = qobject_cast<QComboBox *>( field.m_pEditor ) )
		{
			const QString szShown = szValue.isEmpty() ? szDefault : szValue;
			int nIndex = pCombo->findData( szShown );
			if ( nIndex < 0 && !szShown.isEmpty() )
			{
				// A value the game data doesn't list: shown as it is.
				pCombo->addItem( szShown, szShown );
				nIndex = pCombo->count() - 1;
			}
			const QSignalBlocker block( pCombo );
			pCombo->setCurrentIndex( nIndex );
		}
		else if ( auto *pLine = qobject_cast<QLineEdit *>( field.m_pEditor ) )
		{
			if ( !pLine->hasFocus() || !pLine->isModified() )
			{
				const QSignalBlocker block( pLine );
				pLine->setText( szValue );
				pLine->setModified( false );
			}
			pLine->setPlaceholderText( field.m_Spec.m_szKey.empty() ? tr( "not sent" ) : szDefault );
			if ( IsColour( field.m_szType ) )
			{
				if ( auto *pSwatch = field.m_pEditor->parentWidget()->findChild<QToolButton *>() )
				{
					const QColor colour = ColourOf( szValue.isEmpty() ? szDefault : szValue, field.m_szType == "color1" );
					pSwatch->setStyleSheet( QStringLiteral( "QToolButton { background: %1; border: 1px solid palette(mid); border-radius: 2px; }" ).arg( colour.name() ) );
				}
			}
		}
	}
	m_bFilling = false;
	if ( nBefore != m_nEntity )
		emit Changed();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEntityFields::CommitKey( const std::string &szKey, const std::string &szValue )
{
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene || !m_nEntity )
		return;
	xv::map::Entity_t entity = std::as_const( pScene->m_Map.m_Entities )[ *m_nEntity ];
	if ( entity.GetValue( szKey ) == szValue )
		return;
	if ( szValue.empty() )
		xv::map::RemoveKey( entity, szKey );
	else
		xv::map::SetKeyValue( pScene->m_Map, entity, szKey, szValue );
	const auto field = std::ranges::find_if( m_Fields, [ & ]( const Field_t &one ) { return one.m_Spec.m_szKey == szKey; } );
	const QString szName = field != m_Fields.end() && field->m_pLabel ? field->m_pLabel->text() : Text( szKey );
	xv::map::MapEdit_t edit;
	edit.m_Entities.emplace_back( static_cast<std::uint32_t>( *m_nEntity ), std::move( entity ) );
	m_Editor.GetDocument().Edit( std::move( edit ), tr( "%1: %2" ).arg( Text( m_Classes.front() ), szName ) );
	emit Changed();
}

//-----------------------------------------------------------------------------
// Purpose: The output sending the input changed; with none, one added to a logic_auto's
//          OnMapSpawn (a logic_auto made, and the entity named, when the map needs them), all
//          one step to undo. Empty takes the output out.
//-----------------------------------------------------------------------------
void CMapEntityFields::CommitInput( const std::string &szInput, const std::string &szParameter )
{
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene || !m_nEntity )
		return;
	const xv::map::CMap &map = pScene->m_Map;
	const auto &entities = std::as_const( map.m_Entities );
	const QString szUndo = tr( "%1: %2" ).arg( Text( m_Classes.front() ), Text( szInput ) );
	if ( const std::optional<Sender_t> sender = FindSender( szInput ) )
	{
		xv::map::Entity_t owner = entities[ sender->m_nEntity ];
		std::vector<xv::map::Connection_t> connections = owner.m_Connections;
		if ( connections[ sender->m_nConnection ].m_szParameter == szParameter )
			return;
		if ( szParameter.empty() )
			connections.erase( connections.begin() + static_cast<std::ptrdiff_t>( sender->m_nConnection ) );
		else
			connections[ sender->m_nConnection ].m_szParameter = szParameter;
		xv::map::SetConnections( map, owner, std::move( connections ) );
		xv::map::MapEdit_t edit;
		edit.m_Entities.emplace_back( static_cast<std::uint32_t>( sender->m_nEntity ), std::move( owner ) );
		m_Editor.GetDocument().Edit( std::move( edit ), szUndo );
		emit Changed();
		return;
	}
	if ( szParameter.empty() )
		return;

	xv::map::MapEdit_t edit;
	// The output needs a name to send to.
	std::string szName{ entities[ *m_nEntity ].GetValue( "targetname" ) };
	if ( szName.empty() )
	{
		szName = m_Classes.front();
		xv::map::Entity_t named = entities[ *m_nEntity ];
		xv::map::SetKeyValue( map, named, "targetname", szName );
		edit.m_Entities.emplace_back( static_cast<std::uint32_t>( *m_nEntity ), std::move( named ) );
	}
	// The map's own separator, as Hammer writes outputs.
	char chSeparator = '\x1b';
	for ( const xv::map::Entity_t &entity : entities )
	{
		if ( !entity.m_Connections.empty() )
		{
			chSeparator = entity.m_Connections.front().m_chSeparator;
			break;
		}
	}
	xv::map::Connection_t connection;
	connection.m_szOutput = "OnMapSpawn";
	connection.m_szTarget = szName;
	connection.m_szInput = szInput;
	connection.m_szParameter = szParameter;
	connection.m_chSeparator = chSeparator;
	const auto logicAuto = std::ranges::find_if( entities, []( const xv::map::Entity_t &entity ) { return !entity.m_bDeleted && SameText( entity.GetClassname(), "logic_auto" ); } );
	if ( logicAuto != entities.end() )
	{
		xv::map::Entity_t owner = *logicAuto;
		std::vector<xv::map::Connection_t> connections = owner.m_Connections;
		connections.push_back( connection );
		xv::map::SetConnections( map, owner, std::move( connections ) );
		edit.m_Entities.emplace_back( static_cast<std::uint32_t>( logicAuto - entities.begin() ), std::move( owner ) );
		m_Editor.GetDocument().Edit( std::move( edit ), szUndo );
		emit Changed();
		return;
	}
	// No logic_auto: one made beside the entity, then the edits to existing entities joined to it.
	xv::map::Point_t origin{};
	const QStringList parts = Text( entities[ *m_nEntity ].GetValue( "origin" ) ).split( u' ', Qt::SkipEmptyParts );
	for ( qsizetype i = 0; i < 3 && i < parts.size(); i++ )
		origin[ static_cast<std::size_t>( i ) ] = parts[ i ].toDouble();
	origin[ 0 ] += 16.0;
	xv::map::Entity_t made = xv::map::MakePointEntity( map, pScene->m_pFgd.get(), "logic_auto", origin );
	xv::map::SetConnections( map, made, { connection } );
	xv::map::MapEdit_t added = xv::map::AddObjects( *pScene, {}, { std::move( made ) }, nullptr );
	// Existing entities first: the new one's index is past the map's last.
	added.m_Entities.insert( added.m_Entities.begin(), std::make_move_iterator( edit.m_Entities.begin() ), std::make_move_iterator( edit.m_Entities.end() ) );
	m_Editor.GetDocument().Edit( std::move( added ), szUndo );
	emit Changed();
}

//-----------------------------------------------------------------------------
// Purpose: The first class's entity made at the 3D camera (where the map has no camera, at
//          the middle of the world), with the game data's keys, and selected.
//-----------------------------------------------------------------------------
void CMapEntityFields::AddEntity()
{
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene )
		return;
	xv::map::Point_t origin{};
	if ( const std::optional<xv::map::CViewCamera> camera = m_Editor.GetCamera() )
	{
		for ( std::size_t i = 0; i < 3; i++ )
			origin[ i ] = std::round( camera->m_Eye[ i ] );
	}
	std::vector<std::uint32_t> added;
	xv::map::MapEdit_t edit = xv::map::AddObjects( *pScene, {}, { xv::map::MakePointEntity( pScene->m_Map, pScene->m_pFgd.get(), m_Classes.front(), origin ) }, &added );
	m_nId = 0;
	m_Editor.GetDocument().Edit( std::move( edit ), tr( "Add %1" ).arg( Text( m_Classes.front() ) ), std::move( added ) );
	emit Changed();
}

//-----------------------------------------------------------------------------
// Purpose: The colour dialog for a colour field: the numbers after the colour (a light's
//          brightness) stay as they were.
//-----------------------------------------------------------------------------
void CMapEntityFields::PickColour( const Field_t &field )
{
	const bool bUnit = field.m_szType == "color1";
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	const xv::map::FgdClass_t *pClass = pScene && pScene->m_pFgd ? pScene->m_pFgd->Find( m_Classes.front() ) : nullptr;
	const xv::map::FgdKey_t *pKey = pClass ? pClass->FindKey( field.m_Spec.m_szKey ) : nullptr;
	QString szValue = ValueOf( field );
	if ( szValue.isEmpty() && pKey )
		szValue = Text( pKey->m_szDefault );
	const QColor colour = QColorDialog::getColor( ColourOf( szValue, bUnit ), this, field.m_pLabel ? field.m_pLabel->text() : QString{} );
	if ( !colour.isValid() )
		return;
	QStringList parts = szValue.split( u' ', Qt::SkipEmptyParts );
	const QStringList rgb = bUnit ? QStringList{ QString::number( colour.redF(), 'g', 3 ), QString::number( colour.greenF(), 'g', 3 ), QString::number( colour.blueF(), 'g', 3 ) }
								  : QStringList{ QString::number( colour.red() ), QString::number( colour.green() ), QString::number( colour.blue() ) };
	const QStringList rest = parts.size() > 3 ? parts.mid( 3 ) : QStringList{};
	CommitKey( field.m_Spec.m_szKey, ( rgb + rest ).join( u' ' ).toStdString() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CMapEntityFields::GetFieldText( const std::string &szKeyOrInput ) const
{
	for ( const Field_t &field : m_Fields )
	{
		if ( field.m_Spec.m_szKey == szKeyOrInput || ( field.m_Spec.m_szKey.empty() && field.m_Spec.m_szInput == szKeyOrInput ) )
		{
			if ( const auto *pCombo = qobject_cast<const QComboBox *>( field.m_pEditor ) )
				return pCombo->currentData().toString();
			if ( const auto *pLine = qobject_cast<const QLineEdit *>( field.m_pEditor ) )
				return pLine->text();
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QWidget *MakeFoldSection( const QString &szTitle, QWidget *pContent, const QString &szName, bool bOpen )
{
	auto *pSection = new QWidget;
	pSection->setObjectName( szName );
	auto *pLayout = new QVBoxLayout( pSection );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->setSpacing( 2 );
	auto *pHeader = new QToolButton;
	pHeader->setObjectName( szName + QStringLiteral( "Header" ) );
	pHeader->setText( szTitle );
	pHeader->setCheckable( true );
	pHeader->setAutoRaise( true );
	pHeader->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
	pHeader->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
	QFont font = pHeader->font();
	font.setBold( true );
	pHeader->setFont( font );
	const QString szKey = QStringLiteral( "mapFoldSections/" ) + szName;
	const bool bShown = QSettings{}.value( szKey, bOpen ).toBool();
	pHeader->setChecked( bShown );
	pHeader->setArrowType( bShown ? Qt::DownArrow : Qt::RightArrow );
	pContent->setVisible( bShown );
	QObject::connect( pHeader, &QToolButton::toggled, pSection, [ pHeader, pContent, szKey ]( bool bOn ) {
		pHeader->setArrowType( bOn ? Qt::DownArrow : Qt::RightArrow );
		pContent->setVisible( bOn );
		QSettings{}.setValue( szKey, bOn );
	} );
	pLayout->addWidget( pHeader );
	pLayout->addWidget( pContent );
	return pSection;
}
