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

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QStandardItemModel>
#include <QTimer>
#include <QTreeView>

#include "SoundDocument.h"
#include "Language.h"
#include "SoundIndex.h"
#include "SpinBoxes.h"
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

	// Typing searches the choices like a search box, in a list that also says what each means.
	QCompleter *pCompleter = pCombo->completer();
	pCompleter->setCompletionMode( QCompleter::PopupCompletion );
	pCompleter->setFilterMode( Qt::MatchContains );
	pCompleter->setCaseSensitivity( Qt::CaseInsensitive );
	pCompleter->setMaxVisibleItems( 14 );
	auto *pPopup = new QTreeView;
	pPopup->setHeaderHidden( true );
	pPopup->setRootIsDecorated( false );
	pPopup->setUniformRowHeights( true );
	pPopup->setAllColumnsShowFocus( true );
	pPopup->setMinimumWidth( 460 );
	pCompleter->setPopup( pPopup );
	pPopup->header()->setSectionResizeMode( QHeaderView::ResizeToContents );
	return pCombo;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString BetweenText( const QString &szLow, const QString &szHigh )
{
	return QObject::tr( "random from %1 to %2" ).arg( szLow, szHigh );
}

// How the simple interface takes a number for a key.
struct NumberInput_t
{
	double m_flMin;
	double m_flMax;
	double m_flStep;
	int m_nDecimals;
	const char *m_pszSuffix;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
NumberInput_t NumberInputFor( ValueKind_t nKind, const QString &szKey )
{
	if ( nKind == ValueKind_t::VALUE_KIND_SOUND_LEVEL )
	{
		return { 0.0, 180.0, 5.0, 0, " dB" };
	}
	if ( nKind == ValueKind_t::VALUE_KIND_ATTENUATION )
	{
		return { 0.0, 4.0, 0.05, 4, "" };
	}
	const QString szLower = szKey.toLower();
	if ( szLower == u"volume" || szLower == u"dsp_volume" )
	{
		return { 0.0, 1.0, 0.05, 4, "" };
	}
	if ( szLower == u"pitch" )
	{
		return { 0.0, 255.0, 5.0, 2, "%" };
	}
	if ( szLower == u"time" )
	{
		return { 0.0, 3600.0, 1.0, 2, " s" };
	}
	if ( szLower == u"delay_msec" )
	{
		return { 0.0, 60000.0, 10.0, 0, " ms" };
	}
	return { -1e9, 1e9, 1.0, 4, "" };
}

//-----------------------------------------------------------------------------
// Purpose: One number of a value (one end of a range), named constants read as their numbers.
//-----------------------------------------------------------------------------
std::optional<double> PartNumber( ValueKind_t nKind, const QString &szPart )
{
	const std::string szUtf8 = szPart.toStdString();
	const Constant_t *pConstant = nullptr;
	switch ( nKind )
	{
		case ValueKind_t::VALUE_KIND_SOUND_LEVEL: return ParseSoundLevel( szUtf8 );
		case ValueKind_t::VALUE_KIND_VOLUME: pConstant = FindConstant( Volumes(), szUtf8 ); break;
		case ValueKind_t::VALUE_KIND_PITCH: pConstant = FindConstant( Pitches(), szUtf8 ); break;
		case ValueKind_t::VALUE_KIND_ATTENUATION: pConstant = FindConstant( Attenuations(), szUtf8 ); break;
		default: break;
	}
	return pConstant ? std::optional{ pConstant->m_flValue } : ToNumber( szPart );
}

//-----------------------------------------------------------------------------
// Purpose: A number as the file gets it; sound levels in the stock files' SNDLVL_<n>dB form.
//-----------------------------------------------------------------------------
QString PartText( ValueKind_t nKind, double flValue )
{
	if ( nKind == ValueKind_t::VALUE_KIND_SOUND_LEVEL )
	{
		const long nDecibels = std::lround( flValue );
		return nDecibels == 0 ? QStringLiteral( "SNDLVL_NONE" ) : QStringLiteral( "SNDLVL_%1dB" ).arg( nDecibels );
	}
	return QString::number( flValue, 'g', 6 );
}

//-----------------------------------------------------------------------------
// Purpose: A number box, or a "from … to" pair for keys that take a random range.
//-----------------------------------------------------------------------------
QWidget *NumberEditor( ValueKind_t nKind, const QString &szKey, const QString &szValue, const std::function<void( const QString & )> &pfnCommit )
{
	// Keys that are ranges by nature get a pair; a soundscript's volume, pitch or level only when
	// the file already has one there.
	const bool bCanRange = nKind == ValueKind_t::VALUE_KIND_NUMBER_RANGE || nKind == ValueKind_t::VALUE_KIND_VOLUME || nKind == ValueKind_t::VALUE_KIND_PITCH ||
		nKind == ValueKind_t::VALUE_KIND_SOUND_LEVEL;
	const QStringList parts = RangeParts( szValue.trimmed() );
	if ( parts.size() > ( bCanRange ? 2 : 1 ) )
	{
		return nullptr;
	}
	const bool bRange = nKind == ValueKind_t::VALUE_KIND_NUMBER_RANGE || parts.size() == 2;
	std::vector<double> values;
	for ( const QString &szPart : parts )
	{
		const auto value = PartNumber( nKind, szPart );
		if ( !value )
		{
			return nullptr;
		}
		values.push_back( *value );
	}

	const NumberInput_t input = NumberInputFor( nKind, szKey );
	const auto makeSpin = [ & ]( double flValue ) {
		auto *pSpin = new CFloatSpin;
		pSpin->setDecimals( input.m_nDecimals );
		pSpin->setRange( std::min( input.m_flMin, flValue ), std::max( input.m_flMax, flValue ) );
		pSpin->setSingleStep( input.m_flStep );
		pSpin->setSuffix( QString::fromLatin1( input.m_pszSuffix ) );
		if ( nKind == ValueKind_t::VALUE_KIND_SOUND_LEVEL )
		{
			pSpin->setSpecialValueText( QObject::tr( "None" ) );
		}
		pSpin->setValue( flValue );
		return pSpin;
	};
	if ( !bRange )
	{
		CFloatSpin *pSpin = makeSpin( values.front() );
		QObject::connect( pSpin, &QDoubleSpinBox::valueChanged, pSpin, [ pfnCommit, nKind ]( double flValue ) { pfnCommit( PartText( nKind, flValue ) ); } );
		return pSpin;
	}

	auto *pBox = new QWidget;
	auto *pLayout = new QHBoxLayout( pBox );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->setSpacing( 4 );
	CFloatSpin *pLow = makeSpin( values.front() );
	CFloatSpin *pHigh = makeSpin( values.back() );
	pLayout->addWidget( pLow );
	auto *pTo = new QLabel( QObject::tr( "to" ) );
	pTo->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Preferred );
	pLayout->addWidget( pTo );
	pLayout->addWidget( pHigh );
	pBox->setToolTip( QObject::tr( "Picked at random between the two each time. The same number twice is a fixed value." ) );
	const auto commit = [ pfnCommit, nKind, pLow, pHigh ] {
		const double flLow = pLow->value();
		const double flHigh = pHigh->value();
		pfnCommit( flLow == flHigh ? PartText( nKind, flLow ) : PartText( nKind, flLow ) + u',' + PartText( nKind, flHigh ) );
	};
	QObject::connect( pLow, &QDoubleSpinBox::valueChanged, pBox, commit );
	QObject::connect( pHigh, &QDoubleSpinBox::valueChanged, pBox, commit );
	return pBox;
}

//-----------------------------------------------------------------------------
// Purpose: A list of named values (m_szValue as the file has it, m_szMeaning shown); a value the
//          list doesn't have is added as it is.
//-----------------------------------------------------------------------------
QComboBox *ListEditor( const std::vector<Choice_t> &choices, const QString &szValue, const std::function<void( const QString & )> &pfnCommit )
{
	auto *pCombo = new QComboBox;
	for ( const Choice_t &choice : choices )
	{
		pCombo->addItem( choice.m_szMeaning, choice.m_szValue );
	}
	int nIndex = pCombo->findData( szValue.trimmed(), Qt::UserRole, Qt::MatchFixedString );
	if ( nIndex < 0 )
	{
		pCombo->addItem( szValue, szValue );
		nIndex = pCombo->count() - 1;
	}
	pCombo->setCurrentIndex( nIndex );
	QObject::connect( pCombo, &QComboBox::activated, pCombo, [ pCombo, pfnCommit ]( int nPicked ) { pfnCommit( pCombo->itemData( nPicked ).toString() ); } );
	return pCombo;
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
// Purpose: The simple interface's easier way in for a value; nullptr where it has none.
//-----------------------------------------------------------------------------
QWidget *SimpleValueEditor( ValueKind_t nKind, const QString &szKey, const QString &szValue, std::function<void( const QString & )> pfnCommit )
{
	const QString szText = szValue.trimmed();
	switch ( nKind )
	{
		case ValueKind_t::VALUE_KIND_NUMBER:
		case ValueKind_t::VALUE_KIND_NUMBER_RANGE:
		case ValueKind_t::VALUE_KIND_VOLUME:
		case ValueKind_t::VALUE_KIND_PITCH:
		case ValueKind_t::VALUE_KIND_SOUND_LEVEL:
		case ValueKind_t::VALUE_KIND_ATTENUATION:
			return NumberEditor( nKind, szKey, szText, pfnCommit );
		case ValueKind_t::VALUE_KIND_CHANNEL:
		{
			std::vector<Choice_t> choices;
			QString szCurrent = szText;
			const auto number = ToNumber( szText );
			for ( const Constant_t &channel : Channels() )
			{
				choices.push_back( { Qs( channel.m_szName ), Qs( ParticleNames().Sound( channel.m_szName ) ) } );
				if ( number && channel.m_flValue == *number )
				{
					szCurrent = Qs( channel.m_szName );
				}
			}
			QComboBox *pCombo = ListEditor( choices, szCurrent, pfnCommit );
			for ( std::size_t i = 0; i < Channels().size(); i++ )
			{
				pCombo->setItemData( static_cast<int>( i ), Qs( Channels()[ i ].m_szNote ), Qt::ToolTipRole );
			}
			return pCombo;
		}
		case ValueKind_t::VALUE_KIND_DSP_PRESET:
		{
			std::vector<Choice_t> choices;
			for ( const Constant_t &preset : DspPresets() )
			{
				choices.push_back( { QString::number( static_cast<int>( preset.m_flValue ) ), Qs( preset.m_szName ) } );
			}
			return ListEditor( choices, szText, pfnCommit );
		}
		case ValueKind_t::VALUE_KIND_POSITION:
		case ValueKind_t::VALUE_KIND_RANDOM_POSITION:
		{
			std::vector<Choice_t> choices;
			for ( int i = 0; i < 8; i++ )
			{
				choices.push_back( { QString::number( i ), QObject::tr( "Position %1" ).arg( i ) } );
			}
			if ( nKind == ValueKind_t::VALUE_KIND_RANDOM_POSITION )
			{
				choices.push_back( { QStringLiteral( "random" ), QObject::tr( "Random" ) } );
			}
			return ListEditor( choices, szText, pfnCommit );
		}
		case ValueKind_t::VALUE_KIND_BOOL:
		{
			if ( szText != u"0" && szText != u"1" )
			{
				return nullptr;
			}
			auto *pBox = new QCheckBox;
			pBox->setChecked( szText == u"1" );
			QObject::connect( pBox, &QCheckBox::toggled, pBox, [ pfnCommit ]( bool bOn ) { pfnCommit( bOn ? QStringLiteral( "1" ) : QStringLiteral( "0" ) ); } );
			return pBox;
		}
		case ValueKind_t::VALUE_KIND_ORIGIN:
		{
			const QStringList parts = szText.split( u' ', Qt::SkipEmptyParts );
			std::vector<double> values;
			for ( const QString &szPart : parts )
			{
				if ( const auto value = ToNumber( szPart ) )
				{
					values.push_back( *value );
				}
			}
			if ( parts.size() != 3 || values.size() != 3 )
			{
				return nullptr;
			}
			auto *pBox = new QWidget;
			auto *pLayout = new QHBoxLayout( pBox );
			pLayout->setContentsMargins( 0, 0, 0, 0 );
			pLayout->setSpacing( 2 );
			std::vector<CFloatSpin *> boxes;
			for ( std::size_t i = 0; i < 3; i++ )
			{
				auto *pSpin = new CFloatSpin;
				pSpin->setDecimals( 2 );
				pSpin->setRange( std::min( -65536.0, values[ i ] ), std::max( 65536.0, values[ i ] ) );
				pSpin->setSingleStep( 8.0 );
				pSpin->setPrefix( QStringLiteral( "XYZ" ).mid( static_cast<int>( i ), 1 ) + u' ' );
				pSpin->setValue( values[ i ] );
				pLayout->addWidget( pSpin );
				boxes.push_back( pSpin );
			}
			for ( CFloatSpin *pSpin : boxes )
			{
				QObject::connect( pSpin, &QDoubleSpinBox::valueChanged, pBox, [ pfnCommit, boxes ] {
					pfnCommit( QStringLiteral( "%1 %2 %3" ).arg( PartText( ValueKind_t::VALUE_KIND_NUMBER, boxes[ 0 ]->value() ), PartText( ValueKind_t::VALUE_KIND_NUMBER, boxes[ 1 ]->value() ), PartText( ValueKind_t::VALUE_KIND_NUMBER, boxes[ 2 ]->value() ) ) );
				} );
			}
			return pBox;
		}
		default:
			return nullptr;
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
QString CSoundValueDelegate::KeyOf( const QModelIndex &index )
{
	const QModelIndex key = index.siblingAtColumn( COLUMN_KEY_COLUMN );
	const QVariant name = key.data( Qt::UserRole );
	return name.isValid() ? name.toString() : key.data().toString();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QWidget *CSoundValueDelegate::createEditor( QWidget *pParent, const QStyleOptionViewItem &, const QModelIndex &index ) const
{
	QWidget *pEditor = MakeEditor( pParent, index );
	// Picking from a list is the edit: it goes in and the editor closes, with no Enter needed. It
	// waits a turn: the combo also says "activated" when it loses focus (to its own dropdown, or
	// to a click elsewhere that the view handles itself), and those must not close it.
	auto *pSelf = const_cast<CSoundValueDelegate *>( this );
	auto *pCombo = qobject_cast<QComboBox *>( pEditor );
	const auto finish = [ pSelf, pEditor, pCombo ] {
		QTimer::singleShot( 0, pEditor, [ pSelf, pEditor, pCombo ] {
			if ( !pEditor->isVisible() || ( pCombo && pCombo->view()->isVisible() ) )
			{
				return;
			}
			emit pSelf->commitData( pEditor );
			emit pSelf->closeEditor( pEditor );
		} );
	};
	if ( pCombo )
	{
		connect( pCombo, &QComboBox::activated, pCombo, finish );
		connect( pCombo->completer(), qOverload<const QString &>( &QCompleter::activated ), pCombo, [ pCombo, finish ]( const QString &szText ) {
			pCombo->setEditText( szText );
			finish();
		} );
	}
	else if ( auto *pEdit = qobject_cast<QLineEdit *>( pEditor ); pEdit && pEdit->completer() )
	{
		connect( pEdit->completer(), qOverload<const QString &>( &QCompleter::activated ), pEdit, [ pEdit, finish ]( const QString &szText ) {
			pEdit->setText( szText );
			finish();
		} );
	}
	return pEditor;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QWidget *CSoundValueDelegate::MakeEditor( QWidget *pParent, const QModelIndex &index ) const
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

	const ValueKind_t nKind = KindOf( m_pBlock, KeyOf( index ) );
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
		// Every choice drops down at once; typing (over the selected value) narrows them.
		QTimer::singleShot( 0, pCombo, [ pCombo ] {
			if ( !pCombo->isVisible() || pCombo->count() == 0 )
			{
				return;
			}
			pCombo->lineEdit()->selectAll();
			pCombo->completer()->setCompletionPrefix( {} );
			pCombo->completer()->complete();
		} );
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
