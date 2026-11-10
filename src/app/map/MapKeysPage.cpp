//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapKeysPage.h.
//
//===========================================================================//
#include "MapKeysPage.h"

#include "Preferences.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace
{

enum Column_t
{
	COLUMN_ACTION,
	COLUMN_KEY,
	COLUMN_ALTERNATIVE,
	COLUMN_NOTE,
};

//-----------------------------------------------------------------------------
// Purpose: An action's first two keys (the editor shows two; a third is dropped on change).
//-----------------------------------------------------------------------------
std::pair<QKeyCombination, QKeyCombination> KeysOf( const std::vector<MapKeyBinding_t> &keys, MapAction_t nAction )
{
	std::vector<QKeyCombination> found;
	for ( const MapKeyBinding_t &binding : keys )
	{
		if ( binding.m_nAction == nAction )
			found.push_back( binding.m_Key );
	}
	return { found.size() > 0 ? found[ 0 ] : QKeyCombination{ Qt::Key_unknown }, found.size() > 1 ? found[ 1 ] : QKeyCombination{ Qt::Key_unknown } };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Text( QKeyCombination key )
{
	return key.key() == Qt::Key_unknown ? QString{} : MapKeyText( key );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapKeysPage::CMapKeysPage( QWidget *pParent )
	: QWidget( pParent )
{
	setObjectName( QStringLiteral( "preferencesKeys" ) );
	m_pPreset = new QComboBox;
	m_pPreset->setObjectName( QStringLiteral( "preferencesKeyPreset" ) );
	for ( int i = 0; i < static_cast<int>( MapKeyPreset_t::MAP_KEY_PRESET_COUNT ); i++ )
		m_pPreset->addItem( MapKeyPresetName( static_cast<MapKeyPreset_t>( i ) ), i );
	m_pCopy = new QPushButton( tr( "Copy to My Own" ) );
	m_pCopy->setObjectName( QStringLiteral( "preferencesKeyCopy" ) );
	m_pCopy->setToolTip( tr( "Start your own keys from this preset" ) );
	auto *pTop = new QHBoxLayout;
	pTop->addWidget( new QLabel( tr( "Keys:" ) ) );
	pTop->addWidget( m_pPreset, 1 );
	pTop->addWidget( m_pCopy );

	m_pFilter = new QLineEdit;
	m_pFilter->setObjectName( QStringLiteral( "preferencesKeyFilter" ) );
	m_pFilter->setPlaceholderText( tr( "Find an action or a key" ) );
	m_pFilter->setClearButtonEnabled( true );

	m_pTree = new QTreeWidget;
	m_pTree->setObjectName( QStringLiteral( "preferencesKeyTree" ) );
	m_pTree->setHeaderLabels( { tr( "Action" ), tr( "Key" ), tr( "Alternative" ), tr( "Note" ) } );
	m_pTree->setRootIsDecorated( true );
	m_pTree->setUniformRowHeights( true );
	m_pTree->header()->setSectionResizeMode( COLUMN_ACTION, QHeaderView::Stretch );
	m_pTree->header()->setStretchLastSection( false );

	m_pKey = new QKeySequenceEdit;
	m_pKey->setObjectName( QStringLiteral( "preferencesKeyEdit" ) );
	m_pKey->setMaximumSequenceLength( 1 );
	m_pKey->setClearButtonEnabled( true );
	m_pAlternative = new QKeySequenceEdit;
	m_pAlternative->setObjectName( QStringLiteral( "preferencesKeyAlternative" ) );
	m_pAlternative->setMaximumSequenceLength( 1 );
	m_pAlternative->setClearButtonEnabled( true );
	auto *pEdits = new QFormLayout;
	pEdits->addRow( tr( "Key" ), m_pKey );
	pEdits->addRow( tr( "Alternative" ), m_pAlternative );

	m_pClashes = new QLabel;
	m_pClashes->setObjectName( QStringLiteral( "preferencesKeyClashes" ) );
	m_pClashes->setWordWrap( true );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->addLayout( pTop );
	pLayout->addWidget( m_pFilter );
	pLayout->addWidget( m_pTree, 1 );
	pLayout->addLayout( pEdits );
	pLayout->addWidget( m_pClashes );

	connect( m_pPreset, &QComboBox::currentIndexChanged, this, [ this ] {
		if ( m_bFilling )
			return;
		MapPreferences_t preferences = CPreferences::Instance().GetMapPreferences();
		preferences.m_nKeyPreset = static_cast<MapKeyPreset_t>( m_pPreset->currentData().toInt() );
		CPreferences::Instance().SetMapPreferences( preferences );
	} );
	connect( m_pCopy, &QPushButton::clicked, this, [] {
		MapPreferences_t preferences = CPreferences::Instance().GetMapPreferences();
		preferences.m_CustomKeys = MapKeysToSettings( MapPresetKeys( preferences.m_nKeyPreset, preferences.m_CustomKeys ) );
		preferences.m_nKeyPreset = MapKeyPreset_t::MAP_KEY_PRESET_CUSTOM;
		CPreferences::Instance().SetMapPreferences( preferences );
	} );
	connect( m_pFilter, &QLineEdit::textChanged, this, &CMapKeysPage::ApplyFilter );
	connect( m_pTree, &QTreeWidget::currentItemChanged, this, &CMapKeysPage::ShowSelected );
	connect( m_pKey, &QKeySequenceEdit::editingFinished, this, &CMapKeysPage::KeyEdited );
	connect( m_pAlternative, &QKeySequenceEdit::editingFinished, this, &CMapKeysPage::KeyEdited );
	// The clear buttons don't finish editing; an emptied box is a change all the same.
	connect( m_pKey, &QKeySequenceEdit::keySequenceChanged, this, [ this ]( const QKeySequence &key ) {if(key.isEmpty())KeyEdited(); } );
	connect( m_pAlternative, &QKeySequenceEdit::keySequenceChanged, this, [ this ]( const QKeySequence &key ) {if(key.isEmpty())KeyEdited(); } );
	connect( &CPreferences::Instance(), &CPreferences::MapPreferencesChanged, this, &CMapKeysPage::Fill );
	Fill();
}

//-----------------------------------------------------------------------------
// Purpose: The tree again from the preferences, the selection kept.
//-----------------------------------------------------------------------------
void CMapKeysPage::Fill()
{
	const std::optional<MapAction_t> nSelected = GetSelectedAction();
	m_bFilling = true;
	const MapPreferences_t &preferences = CPreferences::Instance().GetMapPreferences();
	m_pPreset->setCurrentIndex( std::max( 0, m_pPreset->findData( static_cast<int>( preferences.m_nKeyPreset ) ) ) );
	m_pCopy->setEnabled( preferences.m_nKeyPreset != MapKeyPreset_t::MAP_KEY_PRESET_CUSTOM );
	const std::vector<MapKeyBinding_t> keys = MapPresetKeys( preferences.m_nKeyPreset, preferences.m_CustomKeys );
	const std::vector<MapKeyClash_t> clashes = FindMapKeyClashes( keys );

	m_pTree->clear();
	QTreeWidgetItem *pGroup = nullptr;
	QString szGroup;
	int nClashes = 0;
	for ( const MapActionInfo_t &info : MapActions() )
	{
		const QString szThisGroup = QCoreApplication::translate( "MapKeys", info.m_pszGroup );
		if ( !pGroup || szThisGroup != szGroup )
		{
			pGroup = new QTreeWidgetItem( m_pTree, { szThisGroup } );
			pGroup->setFlags( Qt::ItemIsEnabled );
			pGroup->setFirstColumnSpanned( true );
			pGroup->setExpanded( true );
			szGroup = szThisGroup;
		}
		const auto [ key, alternative ] = KeysOf( keys, info.m_nAction );
		auto *pItem = new QTreeWidgetItem( pGroup, { MapActionName( info.m_nAction ), Text( key ), Text( alternative ) } );
		pItem->setData( COLUMN_ACTION, Qt::UserRole, static_cast<int>( info.m_nAction ) );
		QStringList notes;
		bool bClash = false;
		for ( const MapKeyClash_t &clash : clashes )
		{
			if ( clash.m_nAction != info.m_nAction && clash.m_nOther != info.m_nAction )
				continue;
			const MapAction_t nOther = clash.m_nAction == info.m_nAction ? clash.m_nOther : clash.m_nAction;
			const bool bBrushHere = MapActionInfo( info.m_nAction ).m_nContexts == MapKeyContextBit( MapKeyContext_t::MAP_KEY_CONTEXT_BRUSH );
			if ( clash.m_bOverride )
				notes.append( bBrushHere ? tr( "Over %1 in Brush Edit" ).arg( MapActionName( nOther ) ) : tr( "Brush Edit's %1 there" ).arg( MapActionName( nOther ) ) );
			else
			{
				notes.append( tr( "Clashes with %1 (%2)" ).arg( MapActionName( nOther ), MapKeyText( clash.m_Key ) ) );
				bClash = true;
			}
		}
		pItem->setText( COLUMN_NOTE, notes.join( QStringLiteral( "; " ) ) );
		pItem->setToolTip( COLUMN_NOTE, notes.join( QLatin1Char( '\n' ) ) );
		if ( bClash )
		{
			nClashes++;
			for ( int nColumn = 0; nColumn <= COLUMN_NOTE; nColumn++ )
				pItem->setForeground( nColumn, QColor( 220, 60, 60 ) );
		}
		else if ( !notes.isEmpty() )
			pItem->setForeground( COLUMN_NOTE, palette().color( QPalette::PlaceholderText ) );
		if ( nSelected == info.m_nAction )
			m_pTree->setCurrentItem( pItem );
	}
	m_pTree->resizeColumnToContents( COLUMN_KEY );
	m_pTree->resizeColumnToContents( COLUMN_ALTERNATIVE );
	m_pClashes->setText( nClashes == 0 ? QString{} : tr( "%n action(s) share a key with another action; only one of them gets it.", nullptr, nClashes ) );
	m_bFilling = false;
	ApplyFilter();
	ShowSelected();
}

//-----------------------------------------------------------------------------
// Purpose: Rows whose action or keys hold the text; groups with none hidden.
//-----------------------------------------------------------------------------
void CMapKeysPage::ApplyFilter()
{
	const QString szFilter = m_pFilter->text().trimmed();
	for ( int i = 0; i < m_pTree->topLevelItemCount(); i++ )
	{
		QTreeWidgetItem *pGroup = m_pTree->topLevelItem( i );
		bool bAny = false;
		for ( int j = 0; j < pGroup->childCount(); j++ )
		{
			QTreeWidgetItem *pItem = pGroup->child( j );
			const bool bShown = szFilter.isEmpty() || pItem->text( COLUMN_ACTION ).contains( szFilter, Qt::CaseInsensitive ) || pItem->text( COLUMN_KEY ).compare( szFilter, Qt::CaseInsensitive ) == 0 || pItem->text( COLUMN_ALTERNATIVE ).compare( szFilter, Qt::CaseInsensitive ) == 0 || pGroup->text( COLUMN_ACTION ).contains( szFilter, Qt::CaseInsensitive );
			pItem->setHidden( !bShown );
			bAny = bAny || bShown;
		}
		pGroup->setHidden( !bAny );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<MapAction_t> CMapKeysPage::GetSelectedAction() const
{
	const QTreeWidgetItem *pItem = m_pTree ? m_pTree->currentItem() : nullptr;
	const QVariant action = pItem ? pItem->data( COLUMN_ACTION, Qt::UserRole ) : QVariant{};
	return action.isValid() ? std::optional<MapAction_t>{ static_cast<MapAction_t>( action.toInt() ) } : std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: The edits show the selected action's keys.
//-----------------------------------------------------------------------------
void CMapKeysPage::ShowSelected()
{
	const std::optional<MapAction_t> nAction = GetSelectedAction();
	const bool bWas = m_bFilling;
	m_bFilling = true;
	m_pKey->setEnabled( nAction.has_value() );
	m_pAlternative->setEnabled( nAction.has_value() );
	const MapPreferences_t &preferences = CPreferences::Instance().GetMapPreferences();
	const auto [ key, alternative ] = nAction ? KeysOf( MapPresetKeys( preferences.m_nKeyPreset, preferences.m_CustomKeys ), *nAction ) : std::pair{ QKeyCombination{ Qt::Key_unknown }, QKeyCombination{ Qt::Key_unknown } };
	m_pKey->setKeySequence( key.key() == Qt::Key_unknown ? QKeySequence{} : QKeySequence{ key } );
	m_pAlternative->setKeySequence( alternative.key() == Qt::Key_unknown ? QKeySequence{} : QKeySequence{ alternative } );
	m_bFilling = bWas;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapKeysPage::KeyEdited()
{
	const std::optional<MapAction_t> nAction = GetSelectedAction();
	if ( m_bFilling || !nAction )
		return;
	const auto first = []( const QKeySequence &sequence ) { return sequence.isEmpty() ? QKeyCombination{ Qt::Key_unknown } : sequence[ 0 ]; };
	SetKeys( *nAction, first( m_pKey->keySequence() ), first( m_pAlternative->keySequence() ) );
}

//-----------------------------------------------------------------------------
// Purpose: A preset in use is copied into the user's own keys first, so editing never
//          changes a preset.
//-----------------------------------------------------------------------------
void CMapKeysPage::SetKeys( MapAction_t nAction, QKeyCombination key, QKeyCombination alternative )
{
	MapPreferences_t preferences = CPreferences::Instance().GetMapPreferences();
	std::vector<MapKeyBinding_t> keys = MapPresetKeys( preferences.m_nKeyPreset, preferences.m_CustomKeys );
	if ( KeysOf( keys, nAction ) == std::pair{ key, alternative } )
		return;
	std::erase_if( keys, [ & ]( const MapKeyBinding_t &binding ) { return binding.m_nAction == nAction; } );
	for ( const QKeyCombination combination : { key, alternative } )
	{
		if ( combination.key() != Qt::Key_unknown )
			keys.push_back( { nAction, combination } );
	}
	preferences.m_CustomKeys = MapKeysToSettings( keys );
	preferences.m_nKeyPreset = MapKeyPreset_t::MAP_KEY_PRESET_CUSTOM;
	CPreferences::Instance().SetMapPreferences( preferences );
}
