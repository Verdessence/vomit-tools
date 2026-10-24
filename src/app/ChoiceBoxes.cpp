//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See ChoiceBoxes.h.
//
//===========================================================================//
#include "ChoiceBoxes.h"

#include <algorithm>

#include <QComboBox>
#include <QCompleter>
#include <QEvent>
#include <QFontMetrics>
#include <QHeaderView>
#include <QLineEdit>
#include <QStandardItemModel>
#include <QTimer>
#include <QTreeView>

namespace
{

// The widest a list's dropdown gets; longer meanings are cut short, with the whole in the tooltip.
constexpr int MAX_POPUP_WIDTH = 640;

//-----------------------------------------------------------------------------
// Purpose: A list without branches or a header, rows picked whole: a choice, then what it means.
//-----------------------------------------------------------------------------
QTreeView *MakeTwoColumnView()
{
	auto *pView = new QTreeView;
	pView->setHeaderHidden( true );
	pView->setRootIsDecorated( false );
	pView->setUniformRowHeights( true );
	pView->setAllColumnsShowFocus( true );
	pView->setTextElideMode( Qt::ElideRight );
	return pView;
}

//-----------------------------------------------------------------------------
// Purpose: The choices as two columns; meanings are greyed and can't be picked on their own.
//-----------------------------------------------------------------------------
QStandardItemModel *MakeModel( const std::vector<Choice_t> &choices, QObject *pParent )
{
	auto *pModel = new QStandardItemModel( 0, 2, pParent );
	for ( const Choice_t &choice : choices )
	{
		auto *pValue = new QStandardItem( choice.m_szValue );
		auto *pMeaning = new QStandardItem( choice.m_szMeaning );
		pMeaning->setEnabled( false );
		if ( !choice.m_szMeaning.isEmpty() )
		{
			pValue->setToolTip( choice.m_szMeaning );
			pMeaning->setToolTip( choice.m_szMeaning );
		}
		if ( choice.m_Data.isValid() )
		{
			pValue->setData( choice.m_Data, Qt::UserRole );
		}
		if ( choice.m_bHeading )
		{
			QFont bold = pValue->font();
			bold.setBold( true );
			pValue->setFont( bold );
			pValue->setFlags( Qt::NoItemFlags );
		}
		pModel->appendRow( { pValue, pMeaning } );
	}
	return pModel;
}

//-----------------------------------------------------------------------------
// Purpose: Wide enough for the names and as much of the meanings as fits.
//-----------------------------------------------------------------------------
void FitWidth( QTreeView *pView, const std::vector<Choice_t> &choices )
{
	QFont bold = pView->font();
	bold.setBold( true );
	int nNames = 0;
	int nMeanings = 0;
	for ( const Choice_t &choice : choices )
	{
		nNames = std::max( nNames, QFontMetrics{ choice.m_bHeading ? bold : pView->font() }.horizontalAdvance( choice.m_szValue ) );
		nMeanings = std::max( nMeanings, pView->fontMetrics().horizontalAdvance( choice.m_szMeaning ) );
	}
	pView->header()->setSectionResizeMode( 0, QHeaderView::Fixed );
	pView->setColumnWidth( 0, nNames + 24 );
	pView->setMinimumWidth( std::clamp( nNames + nMeanings + 64, 200, MAX_POPUP_WIDTH ) );
}

// Clicking a search box's text drops its choices down.
class CRevealOnClick : public QObject
{
public:
	explicit CRevealOnClick( QComboBox *pBox )
		: QObject( pBox ),
		  m_pBox( pBox )
	{}

protected:
	bool eventFilter( QObject *pObject, QEvent *pEvent ) override
	{
		if ( pEvent->type() == QEvent::MouseButtonRelease && !m_pBox->completer()->popup()->isVisible() && !m_pBox->view()->isVisible() )
		{
			// After the click has placed the cursor, so the selection stays.
			QTimer::singleShot( 0, m_pBox, [ pBox = m_pBox ] { RevealChoices( pBox ); } );
		}
		return QObject::eventFilter( pObject, pEvent );
	}

private:
	QComboBox *m_pBox;
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose: An editable box that works like a search box.
//-----------------------------------------------------------------------------
QComboBox *MakeSearchBox( const std::vector<Choice_t> &choices, QWidget *pParent )
{
	auto *pBox = new QComboBox( pParent );
	pBox->setEditable( true );
	pBox->setInsertPolicy( QComboBox::NoInsert );
	pBox->setModel( MakeModel( choices, pBox ) );
	QTreeView *pView = MakeTwoColumnView();
	pBox->setView( pView );
	pBox->setModelColumn( 0 );
	FitWidth( pView, choices );

	// Typing searches the choices, in a list that also says what each means.
	QCompleter *pCompleter = pBox->completer();
	pCompleter->setCompletionMode( QCompleter::PopupCompletion );
	pCompleter->setFilterMode( Qt::MatchContains );
	pCompleter->setCaseSensitivity( Qt::CaseInsensitive );
	pCompleter->setMaxVisibleItems( 14 );
	QTreeView *pPopup = MakeTwoColumnView();
	pCompleter->setPopup( pPopup );
	pPopup->header()->setSectionResizeMode( 0, QHeaderView::Fixed );
	pPopup->setColumnWidth( 0, pView->columnWidth( 0 ) );
	pPopup->setMinimumWidth( pView->minimumWidth() );
	pBox->lineEdit()->installEventFilter( new CRevealOnClick( pBox ) );
	return pBox;
}

//-----------------------------------------------------------------------------
// Purpose: Drops down every choice of a search box now.
//-----------------------------------------------------------------------------
void RevealChoices( QComboBox *pBox )
{
	if ( !pBox->isVisible() || pBox->count() == 0 || !pBox->lineEdit() )
	{
		return;
	}
	pBox->lineEdit()->selectAll();
	pBox->completer()->setCompletionPrefix( {} );
	pBox->completer()->complete();
}

//-----------------------------------------------------------------------------
// Purpose: Fills a fixed list whose dropdown shows each choice next to what it means.
//-----------------------------------------------------------------------------
void FillListBox( QComboBox *pBox, const std::vector<Choice_t> &choices, int nVisible )
{
	pBox->setModel( MakeModel( choices, pBox ) );
	QTreeView *pView = MakeTwoColumnView();
	pBox->setView( pView );
	pBox->setModelColumn( 0 );
	FitWidth( pView, choices );
	pBox->setMaxVisibleItems( nVisible );
	// Styles that drop a list down as a menu (Breeze, macOS) make it as tall as the screen and
	// ignore the row count; this keeps the plain scrolling list.
	pBox->setStyleSheet( QStringLiteral( "QComboBox { combobox-popup: 0; }" ) );
}
