//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See ParticleSearch.h.
//
//===========================================================================//
#include "ParticleSearch.h"

#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace
{

constexpr int MAX_RESULTS = 500;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Qs( std::string_view szText )
{
	return QString::fromUtf8( szText.data(), static_cast<qsizetype>( szText.size() ) );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: source is asked again each time the dialog is shown, so it stays current.
//-----------------------------------------------------------------------------
CParticleSearch::CParticleSearch( Source_t pfnSource, QWidget *pParent )
	: QDialog( pParent ),
	  m_pfnSource( std::move( pfnSource ) )
{
	setWindowTitle( tr( "Find Particle System" ) );
	resize( 760, 480 );

	m_pQuery = new QLineEdit;
	m_pQuery->setObjectName( QStringLiteral( "particleQuery" ) );
	m_pQuery->setPlaceholderText( tr( "System name, material, operator or child system… (all words must match)" ) );
	m_pQuery->setClearButtonEnabled( true );
	connect( m_pQuery, &QLineEdit::textChanged, this, &CParticleSearch::update );

	m_pResults = new QTreeWidget;
	m_pResults->setObjectName( QStringLiteral( "particleResults" ) );
	m_pResults->setHeaderLabels( { tr( "System" ), tr( "File" ), tr( "Found in" ), tr( "Material" ) } );
	m_pResults->setRootIsDecorated( false );
	m_pResults->setUniformRowHeights( true );
	m_pResults->setSortingEnabled( false );
	m_pResults->header()->setSectionResizeMode( QHeaderView::Interactive );
	m_pResults->header()->resizeSection( 0, 220 );
	m_pResults->header()->resizeSection( 1, 170 );
	m_pResults->header()->resizeSection( 2, 170 );
	connect( m_pResults, &QTreeWidget::itemActivated, this, [ this ]( QTreeWidgetItem *pItem ) {
		emit OpenRequested( pItem->text( 1 ), pItem->text( 0 ) );
	} );

	m_pSummary = new QLabel;

	auto *pLayout = new QVBoxLayout( this );
	pLayout->addWidget( m_pQuery );
	pLayout->addWidget( m_pResults, 1 );
	pLayout->addWidget( m_pSummary );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleSearch::showEvent( QShowEvent *pEvent )
{
	m_Systems = m_pfnSource ? m_pfnSource() : std::vector<xv::particles::SystemSummary_t>{};
	update();
	m_pQuery->setFocus();
	m_pQuery->selectAll();
	QDialog::showEvent( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleSearch::SetQuery( const QString &szQuery )
{
	if ( m_Systems.empty() && m_pfnSource )
	{
		m_Systems = m_pfnSource();
	}
	m_pQuery->setText( szQuery );
	update();
}

//-----------------------------------------------------------------------------
// Purpose: The systems listed, as (file, system).
//-----------------------------------------------------------------------------
std::vector<std::pair<QString, QString>> CParticleSearch::GetResults() const
{
	std::vector<std::pair<QString, QString>> out;
	for ( int i = 0; i < m_pResults->topLevelItemCount(); i++ )
	{
		out.emplace_back( m_pResults->topLevelItem( i )->text( 1 ), m_pResults->topLevelItem( i )->text( 0 ) );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleSearch::update()
{
	m_pResults->clear();
	const QString szQuery = m_pQuery->text().trimmed();
	if ( szQuery.isEmpty() )
	{
		m_pSummary->setText( tr( "%n system(s) to search", nullptr, static_cast<int>( m_Systems.size() ) ) );
		return;
	}
	const auto hits = xv::particles::Search( m_Systems, szQuery.toStdString() );
	for ( std::size_t i = 0; i < hits.size() && i < MAX_RESULTS; i++ )
	{
		const auto &hit = hits[ i ];
		auto *pItem = new QTreeWidgetItem( m_pResults, { Qs( hit.m_pSystem->m_szName ), Qs( hit.m_pSystem->m_szFile ), Qs( hit.m_szMatched ), Qs( hit.m_pSystem->m_szMaterial ) } );
		pItem->setToolTip( 0, tr( "Double-click to open it" ) );
	}
	m_pSummary->setText( hits.size() > MAX_RESULTS
			? tr( "%1 of %2 systems found shown" ).arg( MAX_RESULTS ).arg( hits.size() )
			: tr( "%n system(s) found", nullptr, static_cast<int>( hits.size() ) ) );
	if ( m_pResults->topLevelItemCount() > 0 )
	{
		m_pResults->setCurrentItem( m_pResults->topLevelItem( 0 ) );
	}
}
