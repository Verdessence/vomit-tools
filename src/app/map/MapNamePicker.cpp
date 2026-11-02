//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapNamePicker.h.
//
//===========================================================================//
#include "MapNamePicker.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QSortFilterProxyModel>
#include <QStringListModel>
#include <QVBoxLayout>

namespace
{

// Every word typed is somewhere in the name, in any order and case.
class CWordsFilter : public QSortFilterProxyModel
{
public:
	using QSortFilterProxyModel::QSortFilterProxyModel;

	void SetWords( const QString &szText )
	{
#if QT_VERSION >= QT_VERSION_CHECK( 6, 10, 0 )
		beginFilterChange();
		m_Words = szText.split( u' ', Qt::SkipEmptyParts );
		endFilterChange( QSortFilterProxyModel::Direction::Rows );
#else
		m_Words = szText.split( u' ', Qt::SkipEmptyParts );
		invalidateRowsFilter(); // CI builds on Qt 6.8, before begin/endFilterChange
#endif
	}

protected:
	//-----------------------------------------------------------------------------
	// Purpose:
	//-----------------------------------------------------------------------------
	bool filterAcceptsRow( int nRow, const QModelIndex &parent ) const override
	{
		const QString szName = sourceModel()->index( nRow, 0, parent ).data().toString();
		return std::ranges::all_of( m_Words, [ & ]( const QString &szWord ) { return szName.contains( szWord, Qt::CaseInsensitive ); } );
	}

private:
	QStringList m_Words;
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The names sorted, the current one picked and in view.
//-----------------------------------------------------------------------------
CMapNamePicker::CMapNamePicker( const QString &szTitle, QStringList names, const QString &szCurrent, QWidget *pParent )
	: QDialog( pParent )
{
	setObjectName( QStringLiteral( "mapNamePicker" ) );
	setWindowTitle( szTitle );
	resize( 520, 600 );
	names.sort( Qt::CaseInsensitive );
	names.removeDuplicates();
	m_pModel = new QStringListModel( names, this );
	auto *pFilter = new CWordsFilter( this );
	pFilter->setSourceModel( m_pModel );
	m_pProxy = pFilter;
	m_pFilter = new QLineEdit;
	m_pFilter->setObjectName( QStringLiteral( "mapNamePickerFilter" ) );
	m_pFilter->setPlaceholderText( tr( "Words to look for" ) );
	m_pFilter->setClearButtonEnabled( true );
	m_pView = new QListView;
	m_pView->setObjectName( QStringLiteral( "mapNamePickerList" ) );
	m_pView->setModel( m_pProxy );
	m_pView->setUniformItemSizes( true );
	auto *pCount = new QLabel;
	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
	auto *pLayout = new QVBoxLayout( this );
	pLayout->addWidget( m_pFilter );
	pLayout->addWidget( m_pView, 1 );
	pLayout->addWidget( pCount );
	pLayout->addWidget( pButtons );
	const auto count = [ this, pCount ] { pCount->setText( tr( "%n name(s)", nullptr, m_pProxy->rowCount() ) ); };
	connect( m_pFilter, &QLineEdit::textChanged, this, [ pFilter, count ]( const QString &szText ) {
		pFilter->SetWords( szText );
		count();
	} );
	connect( m_pView, &QListView::doubleClicked, this, &QDialog::accept );
	connect( pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept );
	connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
	count();
	if ( const qsizetype nAt = names.indexOf( szCurrent ); nAt >= 0 )
	{
		const QModelIndex index = m_pProxy->mapFromSource( m_pModel->index( static_cast<int>( nAt ) ) );
		m_pView->setCurrentIndex( index );
		m_pView->scrollTo( index, QAbstractItemView::PositionAtCenter );
	}
	m_pFilter->setFocus();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<QString> CMapNamePicker::Pick( QWidget *pParent, const QString &szTitle, QStringList names, const QString &szCurrent )
{
	CMapNamePicker picker( szTitle, std::move( names ), szCurrent, pParent );
	if ( picker.exec() != QDialog::Accepted || picker.GetPicked().isEmpty() )
		return std::nullopt;
	return picker.GetPicked();
}

//-----------------------------------------------------------------------------
// Purpose: The name highlighted, else the only one left by the filter.
//-----------------------------------------------------------------------------
QString CMapNamePicker::GetPicked() const
{
	const QModelIndex current = m_pView->currentIndex();
	if ( current.isValid() )
		return current.data().toString();
	return m_pProxy->rowCount() == 1 ? m_pProxy->index( 0, 0 ).data().toString() : QString{};
}
