//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See SoundValues.h.
//
//===========================================================================//
#include "SoundValues.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include <QComboBox>
#include <QCompleter>
#include <QHeaderView>
#include <QLineEdit>
#include <QStandardItemModel>
#include <QTreeView>

#include "SoundDocument.h"
#include "SoundIndex.h"
#include "sound/SoundConstants.h"

using namespace xv::sound;

namespace
{

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
QString FormatNumber( double flValue )
{
	return QString::number( flValue, 'g', 4 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<double> ToNumber( const QString &szText )
{
	bool bOk = false;
	const double flValue = szText.trimmed().toDouble( &bOk );
	return bOk ? std::optional{ flValue } : std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QStringList RangeParts( const QString &szText )
{
	QStringList parts = szText.split( u',' );
	for ( QString &szPart : parts )
	{
		szPart = szPart.trimmed();
	}
	return parts;
}

struct Choice_t
{
	QString m_szValue;
	QString m_szMeaning;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<Choice_t> ConstantChoices( std::span<const Constant_t> table )
{
	std::vector<Choice_t> choices;
	for ( const Constant_t &constant : table )
	{
		choices.push_back( { Qs( constant.m_szName ), Qs( constant.m_szNote ) } );
	}
	return choices;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<Choice_t> ChoicesFor( ValueKind_t nKind )
{
	switch ( nKind )
	{
		case ValueKind_t::VALUE_KIND_CHANNEL:
			return ConstantChoices( Channels() );
		case ValueKind_t::VALUE_KIND_VOLUME:
			return ConstantChoices( Volumes() );
		case ValueKind_t::VALUE_KIND_PITCH:
			return ConstantChoices( Pitches() );
		case ValueKind_t::VALUE_KIND_ATTENUATION:
			return ConstantChoices( Attenuations() );
		case ValueKind_t::VALUE_KIND_SOUND_LEVEL:
		{
			std::vector<Choice_t> choices;
			for ( const Constant_t &level : SoundLevels() )
			{
				const QString szNote = level.m_szNote.empty() ? QString{} : QStringLiteral( " · %1" ).arg( Qs( level.m_szNote ) );
				choices.push_back( { Qs( level.m_szName ), QStringLiteral( "%1 dB%2" ).arg( level.m_flValue ).arg( szNote ) } );
			}
			return choices;
		}
		case ValueKind_t::VALUE_KIND_DSP_PRESET:
		{
			std::vector<Choice_t> choices;
			for ( const Constant_t &preset : DspPresets() )
			{
				choices.push_back( { QString::number( static_cast<int>( preset.m_flValue ) ), Qs( preset.m_szName ) } );
			}
			return choices;
		}
		case ValueKind_t::VALUE_KIND_POSITION:
		case ValueKind_t::VALUE_KIND_RANDOM_POSITION:
		{
			std::vector<Choice_t> choices;
			for ( int i = 0; i < 8; i++ )
			{
				choices.push_back( { QString::number( i ), QObject::tr( "env_soundscape's position%1 target" ).arg( i ) } );
			}
			if ( nKind == ValueKind_t::VALUE_KIND_RANDOM_POSITION )
			{
				choices.push_back( { QStringLiteral( "random" ), QObject::tr( "Random spots around the listener" ) } );
			}
			return choices;
		}
		case ValueKind_t::VALUE_KIND_BOOL:
			return { { QStringLiteral( "0" ), QObject::tr( "No" ) }, { QStringLiteral( "1" ), QObject::tr( "Yes" ) } };
		default:
			return {};
	}
}

//-----------------------------------------------------------------------------
// Purpose: An editable dropdown whose popup lists each choice next to what it means.
//-----------------------------------------------------------------------------
QComboBox *MakeChoiceCombo( const std::vector<Choice_t> &choices, QWidget *pParent )
{
	auto *pCombo = new QComboBox( pParent );
	pCombo->setEditable( true );
	pCombo->setInsertPolicy( QComboBox::NoInsert );

	auto *pModel = new QStandardItemModel( 0, 2, pCombo );
	for ( const Choice_t &choice : choices )
	{
		auto *pMeaning = new QStandardItem( choice.m_szMeaning );
		pMeaning->setEnabled( false );
		pModel->appendRow( { new QStandardItem( choice.m_szValue ), pMeaning } );
	}
	auto *pView = new QTreeView;
	pView->setHeaderHidden( true );
	pView->setRootIsDecorated( false );
	pView->setUniformRowHeights( true );
	pView->setAllColumnsShowFocus( true );
	pCombo->setModel( pModel );
	pCombo->setView( pView );
	pCombo->setModelColumn( 0 );
	pView->header()->setSectionResizeMode( QHeaderView::ResizeToContents );
	pView->setMinimumWidth( 460 );

	pCombo->completer()->setCompletionMode( QCompleter::PopupCompletion );
	pCombo->completer()->setFilterMode( Qt::MatchContains );
	pCombo->completer()->setCaseSensitivity( Qt::CaseInsensitive );
	return pCombo;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString BetweenText( const QString &szLow, const QString &szHigh )
{
	return QObject::tr( "random from %1 to %2" ).arg( szLow, szHigh );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: What a value means, for the grey column next to it: "140 dB · attenuation 0.22",
//          "Tunnel Medium", ...
//-----------------------------------------------------------------------------
QString DescribeValue( ValueKind_t nKind, const QString &szValue )
{
	const QString szText = szValue.trimmed();
	const QStringList range = RangeParts( szText );
	const bool bIsRange = range.size() == 2;
	const std::string szUtf8 = szText.toStdString();

	switch ( nKind )
	{
		case ValueKind_t::VALUE_KIND_CHANNEL:
		{
			if ( const Constant_t *pChannel = FindConstant( Channels(), szUtf8 ) )
			{
				return Qs( pChannel->m_szNote );
			}
			if ( const auto number = ToNumber( szText ) )
			{
				for ( const Constant_t &channel : Channels() )
				{
					if ( channel.m_flValue == *number )
					{
						return Qs( channel.m_szName );
					}
				}
			}
			return {};
		}
		case ValueKind_t::VALUE_KIND_VOLUME:
		case ValueKind_t::VALUE_KIND_PITCH:
		{
			const auto table = nKind == ValueKind_t::VALUE_KIND_VOLUME ? Volumes() : Pitches();
			const auto resolve = [ table ]( const QString &szPart ) {
				const Constant_t *pConstant = FindConstant( table, szPart.toStdString() );
				return pConstant ? FormatNumber( pConstant->m_flValue ) : szPart;
			};
			if ( bIsRange )
			{
				return BetweenText( resolve( range[ 0 ] ), resolve( range[ 1 ] ) );
			}
			if ( const Constant_t *pConstant = FindConstant( table, szUtf8 ) )
			{
				return FormatNumber( pConstant->m_flValue );
			}
			return {};
		}
		case ValueKind_t::VALUE_KIND_NUMBER_RANGE:
			return bIsRange ? BetweenText( range[ 0 ], range[ 1 ] ) : QString{};
		case ValueKind_t::VALUE_KIND_SOUND_LEVEL:
		{
			const auto decibels = []( const QString &szPart ) {
				const auto level = ParseSoundLevel( szPart.toStdString() );
				return level ? QObject::tr( "%1 dB" ).arg( *level ) : szPart;
			};
			if ( bIsRange )
			{
				return BetweenText( decibels( range[ 0 ] ), decibels( range[ 1 ] ) );
			}
			if ( const auto level = ParseSoundLevel( szUtf8 ) )
			{
				if ( *level == 0 )
				{
					return QObject::tr( "no falloff, heard everywhere" );
				}
				return QObject::tr( "%1 dB · attenuation %2" ).arg( *level ).arg( FormatNumber( SoundLevelToAttenuation( *level ) ) );
			}
			return {};
		}
		case ValueKind_t::VALUE_KIND_ATTENUATION:
		{
			const Constant_t *pConstant = FindConstant( Attenuations(), szUtf8 );
			const auto attenuation = pConstant ? std::optional{ pConstant->m_flValue } : ToNumber( szText );
			if ( !attenuation )
			{
				return {};
			}
			if ( *attenuation == 0 )
			{
				return QObject::tr( "no falloff, heard everywhere" );
			}
			return QObject::tr( "about %1 dB" ).arg( std::round( AttenuationToSoundLevel( *attenuation ) ) );
		}
		case ValueKind_t::VALUE_KIND_DSP_PRESET:
		{
			const auto number = ToNumber( szText );
			if ( !number )
			{
				return {};
			}
			for ( const Constant_t &preset : DspPresets() )
			{
				if ( preset.m_flValue == *number )
				{
					return Qs( preset.m_szName );
				}
			}
			return QObject::tr( "not a stock preset" );
		}
		case ValueKind_t::VALUE_KIND_POSITION:
		case ValueKind_t::VALUE_KIND_RANDOM_POSITION:
			if ( szText.compare( u"random", Qt::CaseInsensitive ) == 0 )
			{
				return QObject::tr( "random spots around the listener" );
			}
			return ToNumber( szText ) ? QObject::tr( "position%1 target" ).arg( szText ) : QString{};
		case ValueKind_t::VALUE_KIND_WAVE:
		{
			QStringList meanings;
			for ( qsizetype i = 0; i < 2 && i < szText.size(); i++ )
			{
				const auto flags = PathFlags();
				const auto it = std::ranges::find( flags, szText[ i ].toLatin1(), &PathFlag_t::m_chCharacter );
				if ( it == flags.end() )
				{
					break;
				}
				meanings << Qs( it->m_szMeaning );
			}
			return meanings.join( QStringLiteral( " · " ) );
		}
		case ValueKind_t::VALUE_KIND_BOOL:
			return szText == u"1" ? QObject::tr( "yes" ) : szText == u"0" ? QObject::tr( "no" )
																		  : QString{};
		default:
			return {};
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CSoundValueDelegate::CSoundValueDelegate( const BlockDef_t *pBlock, const CSoundIndex &index, const CSoundDocument &document, QObject *pParent )
	: QStyledItemDelegate( pParent ),
	  m_pBlock( pBlock ),
	  m_Index( index ),
	  m_Document( document )
{}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
ValueKind_t CSoundValueDelegate::KindOf( const BlockDef_t *pBlock, const QString &szKey )
{
	if ( !pBlock )
	{
		return ValueKind_t::VALUE_KIND_TEXT;
	}
	const KeyDef_t *pDef = FindKey( *pBlock, szKey.toStdString() );
	return pDef ? pDef->m_nKind : ValueKind_t::VALUE_KIND_TEXT;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QWidget *CSoundValueDelegate::createEditor( QWidget *pParent, const QStyleOptionViewItem &, const QModelIndex &index ) const
{
	if ( index.column() == COLUMN_KEY_COLUMN )
	{
		std::vector<Choice_t> keys;
		if ( m_pBlock )
		{
			for ( const KeyDef_t &key : m_pBlock->m_Keys )
			{
				keys.push_back( { Qs( key.m_szKey ), Qs( key.m_szHelp ) } );
			}
		}
		return MakeChoiceCombo( keys, pParent );
	}
	if ( index.column() != COLUMN_VALUE_COLUMN )
	{
		return nullptr;
	}

	const ValueKind_t nKind = KindOf( m_pBlock, index.siblingAtColumn( COLUMN_KEY_COLUMN ).data().toString() );
	if ( nKind == ValueKind_t::VALUE_KIND_WAVE )
	{
		auto *pEdit = new QLineEdit( pParent );
		auto *pCompleter = new QCompleter( m_Index.GetSoundFiles(), pEdit );
		pCompleter->setCaseSensitivity( Qt::CaseInsensitive );
		pCompleter->setFilterMode( Qt::MatchContains );
		pCompleter->setMaxVisibleItems( 16 );
		pEdit->setCompleter( pCompleter );
		pEdit->setPlaceholderText( tr( "Type to search sound files" ) );
		return pEdit;
	}
	if ( nKind == ValueKind_t::VALUE_KIND_SOUNDSCAPE_NAME )
	{
		std::vector<Choice_t> names;
		for ( const QString &szName : m_Index.SoundscapeNames( &m_Document ) )
		{
			names.push_back( { szName, {} } );
		}
		return MakeChoiceCombo( names, pParent );
	}
	if ( const auto choices = ChoicesFor( nKind ); !choices.empty() )
	{
		return MakeChoiceCombo( choices, pParent );
	}
	return new QLineEdit( pParent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundValueDelegate::setEditorData( QWidget *pEditor, const QModelIndex &index ) const
{
	const QString szText = index.data( Qt::EditRole ).toString();
	if ( auto *pCombo = qobject_cast<QComboBox *>( pEditor ) )
	{
		pCombo->setCurrentIndex( -1 );
		pCombo->setEditText( szText );
	}
	else if ( auto *pEdit = qobject_cast<QLineEdit *>( pEditor ) )
	{
		pEdit->setText( szText );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundValueDelegate::setModelData( QWidget *pEditor, QAbstractItemModel *pModel, const QModelIndex &index ) const
{
	if ( auto *pCombo = qobject_cast<QComboBox *>( pEditor ) )
	{
		pModel->setData( index, pCombo->currentText(), Qt::EditRole );
	}
	else if ( auto *pEdit = qobject_cast<QLineEdit *>( pEditor ) )
	{
		pModel->setData( index, pEdit->text(), Qt::EditRole );
	}
}
