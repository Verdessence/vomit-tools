//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Replace Textures, the Entity Report and Check for Problems.
//
//===========================================================================//
#include "MapReports.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include "map/MapDocument.h"
#include "map/MapEditor.h"
#include "map/MapKeys.h"
#include "map/MaterialBrowser.h"

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapReplaceTexturesDialog::CMapReplaceTexturesDialog( CMapEditor &editor )
	: QDialog( &editor ),
	  m_Editor( editor )
{
	setWindowTitle( tr( "Replace Textures" ) );
	setObjectName( QStringLiteral( "mapReplaceTexturesDialog" ) );
	auto *pLayout = new QVBoxLayout( this );
	auto *pForm = new QFormLayout;
	auto field = [ & ]( QLineEdit *&pField, const QString &szName, const QString &szLabel ) {
		pField = new QLineEdit;
		pField->setObjectName( szName );
		auto *pRow = new QWidget;
		auto *pRowLayout = new QHBoxLayout( pRow );
		pRowLayout->setContentsMargins( 0, 0, 0, 0 );
		pRowLayout->addWidget( pField, 1 );
		auto *pBrowse = new QPushButton( tr( "Browse…" ) );
		connect( pBrowse, &QPushButton::clicked, this, [ this, pField ] { Browse( pField ); } );
		pRowLayout->addWidget( pBrowse );
		pForm->addRow( szLabel, pRow );
	};
	field( m_pFind, QStringLiteral( "mapReplaceFind" ), tr( "Find" ) );
	field( m_pReplace, QStringLiteral( "mapReplaceWith" ), tr( "Replace" ) );
	pLayout->addLayout( pForm );

	auto *pIn = new QGroupBox( tr( "Replace in" ) );
	auto *pInLayout = new QVBoxLayout( pIn );
	m_pIn = new QButtonGroup( this );
	auto *pEverything = new QRadioButton( tr( "Everything" ) );
	pEverything->setObjectName( QStringLiteral( "mapReplaceEverything" ) );
	auto *pSelected = new QRadioButton( tr( "Selected objects" ) );
	pSelected->setObjectName( QStringLiteral( "mapReplaceSelected" ) );
	m_pIn->addButton( pEverything, 0 );
	m_pIn->addButton( pSelected, 1 );
	pEverything->setChecked( true );
	m_pHidden = new QCheckBox( tr( "Hidden objects too" ) );
	m_pHidden->setObjectName( QStringLiteral( "mapReplaceHidden" ) );
	pInLayout->addWidget( pEverything );
	pInLayout->addWidget( pSelected );
	pInLayout->addWidget( m_pHidden );
	pLayout->addWidget( pIn );

	auto *pAction = new QGroupBox( tr( "Action" ) );
	auto *pActionLayout = new QVBoxLayout( pAction );
	m_pAction = new QButtonGroup( this );
	const struct
	{
		int m_nId;
		QString m_szText;
		QString m_szName;
	} actions[] = {
		{ 0, tr( "Replace exact matches" ), QStringLiteral( "mapReplaceExact" ) },
		{ 1, tr( "Replace partial matches" ), QStringLiteral( "mapReplacePartial" ) },
		{ 2, tr( "Substitute partial matches" ), QStringLiteral( "mapReplaceSubstitute" ) },
		{ 3, tr( "Do not replace textures (mark the faces found)" ), QStringLiteral( "mapReplaceMark" ) },
	};
	for ( const auto &action : actions )
	{
		auto *pRadio = new QRadioButton( action.m_szText );
		pRadio->setObjectName( action.m_szName );
		m_pAction->addButton( pRadio, action.m_nId );
		pActionLayout->addWidget( pRadio );
	}
	m_pAction->button( 0 )->setChecked( true );
	m_pRescale = new QCheckBox( tr( "Rescale texture coordinates for the new size" ) );
	m_pRescale->setObjectName( QStringLiteral( "mapReplaceRescale" ) );
	m_pRescale->setToolTip( tr( "A 256-texel texture at 0.5 replaced by a 512 one goes to 0.25, so it covers the same wall" ) );
	pActionLayout->addWidget( m_pRescale );
	pLayout->addWidget( pAction );
	connect( m_pAction, &QButtonGroup::idClicked, this, [ this ]( int nId ) {
		m_pReplace->setEnabled( nId != 3 );
		m_pRescale->setEnabled( nId != 3 );
	} );

	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
	connect( pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept );
	connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
	pLayout->addWidget( pButtons );
}

//-----------------------------------------------------------------------------
// Purpose: The texture browser over the game's materials, into the field.
//-----------------------------------------------------------------------------
void CMapReplaceTexturesDialog::Browse( QLineEdit *pField )
{
	const std::optional<xv::vfs::CGameInfo> &game = m_Editor.GetDocument().GetGame();
	if ( !game )
	{
		return;
	}
	QStringList used;
	if ( const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene() )
	{
		for ( const std::string &szMaterial : pScene->m_Materials )
		{
			used << QString::fromStdString( szMaterial );
		}
	}
	if ( const std::optional<QString> szPicked = CMaterialBrowser::Pick( this, *game, used, pField->text() ) )
	{
		pField->setText( *szPicked );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::map::ReplaceTextures_t CMapReplaceTexturesDialog::GetValues() const
{
	xv::map::ReplaceTextures_t how;
	how.m_szFind = m_pFind->text().trimmed().toStdString();
	how.m_szReplace = m_pReplace->text().trimmed().toUpper().toStdString();
	switch ( m_pAction->checkedId() )
	{
		case 1:
			how.m_nHow = xv::map::ReplaceHow_t::REPLACE_PARTIAL;
			break;
		case 2:
			how.m_nHow = xv::map::ReplaceHow_t::REPLACE_SUBSTITUTE;
			break;
		default:
			how.m_nHow = xv::map::ReplaceHow_t::REPLACE_EXACT;
			break;
	}
	how.m_bRescale = m_pRescale->isChecked();
	return how;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapReplaceTexturesDialog::IsMarkOnly() const
{
	return m_pAction->checkedId() == 3;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapReplaceTexturesDialog::IsSelectedOnly() const
{
	return m_pIn->checkedId() == 1;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapReplaceTexturesDialog::IsHiddenToo() const
{
	return m_pHidden->isChecked();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapReplaceTexturesDialog::SetValues( const xv::map::ReplaceTextures_t &how, bool bMarkOnly, bool bSelectedOnly, bool bHiddenToo )
{
	m_pFind->setText( QString::fromStdString( how.m_szFind ) );
	m_pReplace->setText( QString::fromStdString( how.m_szReplace ) );
	const int nAction = bMarkOnly ? 3 : how.m_nHow == xv::map::ReplaceHow_t::REPLACE_SUBSTITUTE ? 2
		: how.m_nHow == xv::map::ReplaceHow_t::REPLACE_PARTIAL                                  ? 1
																								: 0;
	m_pAction->button( nAction )->setChecked( true );
	m_pReplace->setEnabled( !bMarkOnly );
	m_pRescale->setEnabled( !bMarkOnly );
	m_pRescale->setChecked( how.m_bRescale );
	m_pIn->button( bSelectedOnly ? 1 : 0 )->setChecked( true );
	m_pHidden->setChecked( bHiddenToo );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapEntityReport::CMapEntityReport( CMapEditor &editor )
	: QDialog( &editor ),
	  m_Editor( editor )
{
	setWindowTitle( tr( "Entity Report" ) );
	setObjectName( QStringLiteral( "mapEntityReport" ) );
	setAttribute( Qt::WA_DeleteOnClose );
	auto *pLayout = new QHBoxLayout( this );
	m_pList = new QListWidget;
	m_pList->setObjectName( QStringLiteral( "mapEntityReportList" ) );
	m_pList->setSelectionMode( QAbstractItemView::ExtendedSelection );
	pLayout->addWidget( m_pList, 1 );

	auto *pSide = new QVBoxLayout;
	auto *pGoTo = new QPushButton( tr( "Go to" ) );
	pGoTo->setToolTip( tr( "Centre the views on the entities picked" ) );
	connect( pGoTo, &QPushButton::clicked, this, &CMapEntityReport::GoTo );
	connect( m_pList, &QListWidget::itemDoubleClicked, this, &CMapEntityReport::GoTo );
	auto *pDelete = new QPushButton( tr( "Delete" ) );
	connect( pDelete, &QPushButton::clicked, this, [ this ] { m_Editor.RunAction( MapAction_t::MAP_ACTION_DELETE, nullptr ); } );
	pSide->addWidget( pGoTo );
	pSide->addWidget( pDelete );
	auto *pFilter = new QGroupBox( tr( "Filter" ) );
	auto *pFilterLayout = new QFormLayout( pFilter );
	m_pKind = new QButtonGroup( this );
	auto *pKinds = new QWidget;
	auto *pKindLayout = new QHBoxLayout( pKinds );
	pKindLayout->setContentsMargins( 0, 0, 0, 0 );
	const struct
	{
		int m_nId;
		QString m_szText;
		QString m_szName;
	} kinds[] = {
		{ 0, tr( "All" ), QStringLiteral( "mapEntityReportAll" ) },
		{ 1, tr( "Point" ), QStringLiteral( "mapEntityReportPoint" ) },
		{ 2, tr( "Brush" ), QStringLiteral( "mapEntityReportBrush" ) },
	};
	for ( const auto &kind : kinds )
	{
		auto *pRadio = new QRadioButton( kind.m_szText );
		pRadio->setObjectName( kind.m_szName );
		m_pKind->addButton( pRadio, kind.m_nId );
		pKindLayout->addWidget( pRadio );
	}
	m_pKind->button( 0 )->setChecked( true );
	pFilterLayout->addRow( tr( "Entities" ), pKinds );
	m_pHidden = new QCheckBox( tr( "Include hidden objects" ) );
	m_pHidden->setObjectName( QStringLiteral( "mapEntityReportHidden" ) );
	pFilterLayout->addRow( QString(), m_pHidden );
	m_pClass = new QLineEdit;
	m_pClass->setObjectName( QStringLiteral( "mapEntityReportClass" ) );
	m_pClass->setPlaceholderText( tr( "a part of the class name" ) );
	pFilterLayout->addRow( tr( "By class" ), m_pClass );
	m_pKey = new QLineEdit;
	m_pKey->setObjectName( QStringLiteral( "mapEntityReportKey" ) );
	m_pKey->setPlaceholderText( tr( "any key" ) );
	m_pValue = new QLineEdit;
	m_pValue->setObjectName( QStringLiteral( "mapEntityReportValue" ) );
	m_pExact = new QCheckBox( tr( "Exact" ) );
	m_pExact->setObjectName( QStringLiteral( "mapEntityReportExact" ) );
	auto *pKeyValue = new QWidget;
	auto *pKeyValueLayout = new QHBoxLayout( pKeyValue );
	pKeyValueLayout->setContentsMargins( 0, 0, 0, 0 );
	pKeyValueLayout->addWidget( m_pKey, 1 );
	pKeyValueLayout->addWidget( new QLabel( QStringLiteral( "=" ) ) );
	pKeyValueLayout->addWidget( m_pValue, 1 );
	pKeyValueLayout->addWidget( m_pExact );
	pFilterLayout->addRow( tr( "By key/value" ), pKeyValue );
	pSide->addWidget( pFilter );
	pSide->addStretch( 1 );
	auto *pClose = new QPushButton( tr( "Close" ) );
	connect( pClose, &QPushButton::clicked, this, &QDialog::close );
	pSide->addWidget( pClose );
	pLayout->addLayout( pSide );

	connect( m_pKind, &QButtonGroup::idClicked, this, &CMapEntityReport::Refresh );
	connect( m_pHidden, &QCheckBox::toggled, this, &CMapEntityReport::Refresh );
	connect( m_pExact, &QCheckBox::toggled, this, &CMapEntityReport::Refresh );
	for ( QLineEdit *pField : { m_pClass, m_pKey, m_pValue } )
	{
		connect( pField, &QLineEdit::textChanged, this, &CMapEntityReport::Refresh );
	}
	connect( m_pList, &QListWidget::itemSelectionChanged, this, &CMapEntityReport::FollowList );
	connect( &m_Editor.GetDocument(), &CMapDocument::SceneEdited, this, [ this ] { m_Editor.GetDocument().MeasureEditListener( tr( "Entity Report" ), [ this ] { Refresh(); } ); } );
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose: The rows are "class  name" (Hammer's report shows the class and the targetname); the
//          selection in the views is kept on the rows that still match.
//-----------------------------------------------------------------------------
void CMapEntityReport::Refresh()
{
	CMapDocument &document = m_Editor.GetDocument();
	const xv::map::CScene *pScene = document.GetScene();
	m_bFollowing = true;
	m_pList->clear();
	m_Listed.clear();
	if ( pScene )
	{
		xv::map::EntityFilter_t filter;
		filter.m_nKind = m_pKind->checkedId() == 1 ? xv::map::EntityFilter_t::Kind_t::KIND_POINT : m_pKind->checkedId() == 2 ? xv::map::EntityFilter_t::Kind_t::KIND_BRUSH
																															 : xv::map::EntityFilter_t::Kind_t::KIND_ALL;
		filter.m_szClass = m_pClass->text().trimmed().toStdString();
		filter.m_szKey = m_pKey->text().trimmed().toStdString();
		filter.m_szValue = m_pValue->text().trimmed().toStdString();
		filter.m_bExact = m_pExact->isChecked();
		const std::vector<std::uint32_t> &selection = document.GetSelection();
		for ( const std::uint32_t nObject : xv::map::FilterEntities( *pScene, filter ) )
		{
			if ( !m_pHidden->isChecked() && !document.IsShown( nObject ) )
			{
				continue;
			}
			const xv::map::Entity_t &entity = pScene->m_Map.m_Entities[ pScene->GetIndex( nObject ) ];
			const QString szName = QString::fromUtf8( entity.GetValue( "targetname" ).data(), static_cast<qsizetype>( entity.GetValue( "targetname" ).size() ) );
			auto *pItem = new QListWidgetItem( QString::fromUtf8( entity.GetClassname().data(), static_cast<qsizetype>( entity.GetClassname().size() ) ) + ( szName.isEmpty() ? QString() : QStringLiteral( "  " ) + szName ) );
			pItem->setSelected( std::ranges::find( selection, nObject ) != selection.end() );
			m_pList->addItem( pItem );
			m_Listed.push_back( nObject );
		}
	}
	m_bFollowing = false;
}

//-----------------------------------------------------------------------------
// Purpose: Picking rows selects those entities in the views.
//-----------------------------------------------------------------------------
void CMapEntityReport::FollowList()
{
	if ( m_bFollowing )
	{
		return;
	}
	std::vector<std::uint32_t> objects;
	for ( int i = 0; i < m_pList->count(); i++ )
	{
		if ( m_pList->item( i )->isSelected() && static_cast<std::size_t>( i ) < m_Listed.size() )
		{
			objects.push_back( m_Listed[ static_cast<std::size_t>( i ) ] );
		}
	}
	m_Editor.GetDocument().SetSelection( std::move( objects ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEntityReport::GoTo()
{
	FollowList();
	const xv::map::Box_t bounds = m_Editor.GetDocument().GetSelectionBounds();
	if ( !bounds.IsEmpty() )
	{
		m_Editor.CenterViewsOn( bounds.GetCenter() );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapCheckDialog::CMapCheckDialog( CMapEditor &editor )
	: QDialog( &editor ),
	  m_Editor( editor )
{
	setWindowTitle( tr( "Check for Problems" ) );
	setObjectName( QStringLiteral( "mapCheckDialog" ) );
	setAttribute( Qt::WA_DeleteOnClose );
	auto *pLayout = new QHBoxLayout( this );
	auto *pLeft = new QVBoxLayout;
	m_pList = new QListWidget;
	m_pList->setObjectName( QStringLiteral( "mapCheckList" ) );
	pLeft->addWidget( m_pList, 1 );
	m_pVisibleOnly = new QCheckBox( tr( "Check visible parts of the map only" ) );
	m_pVisibleOnly->setObjectName( QStringLiteral( "mapCheckVisibleOnly" ) );
	connect( m_pVisibleOnly, &QCheckBox::toggled, this, &CMapCheckDialog::Run );
	pLeft->addWidget( m_pVisibleOnly );
	pLayout->addLayout( pLeft, 1 );
	auto *pSide = new QVBoxLayout;
	m_pGoTo = new QPushButton( tr( "Go to error" ) );
	m_pGoTo->setObjectName( QStringLiteral( "mapCheckGoTo" ) );
	connect( m_pGoTo, &QPushButton::clicked, this, &CMapCheckDialog::GoTo );
	connect( m_pList, &QListWidget::itemDoubleClicked, this, &CMapCheckDialog::GoTo );
	m_pFix = new QPushButton( tr( "Fix" ) );
	m_pFix->setObjectName( QStringLiteral( "mapCheckFix" ) );
	connect( m_pFix, &QPushButton::clicked, this, &CMapCheckDialog::Fix );
	m_pFixAll = new QPushButton( tr( "Fix all (of type)" ) );
	m_pFixAll->setObjectName( QStringLiteral( "mapCheckFixAll" ) );
	connect( m_pFixAll, &QPushButton::clicked, this, &CMapCheckDialog::FixAll );
	pSide->addWidget( m_pGoTo );
	pSide->addWidget( m_pFix );
	pSide->addWidget( m_pFixAll );
	pSide->addStretch( 1 );
	auto *pClose = new QPushButton( tr( "Close" ) );
	connect( pClose, &QPushButton::clicked, this, &QDialog::close );
	pSide->addWidget( pClose );
	pLayout->addLayout( pSide );
	connect( m_pList, &QListWidget::currentRowChanged, this, &CMapCheckDialog::Pick );
	connect( &m_Editor.GetDocument(), &CMapDocument::SceneEdited, this, [ this ] { m_Editor.GetDocument().MeasureEditListener( tr( "Check for Problems" ), [ this ] { Run(); } ); } );
	Run();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapCheckDialog::Run()
{
	CMapDocument &document = m_Editor.GetDocument();
	const xv::map::CScene *pScene = document.GetScene();
	const int nRow = m_pList->currentRow();
	m_pList->clear();
	m_Problems.clear();
	if ( pScene )
	{
		std::function<bool( std::uint32_t )> pfnInclude;
		if ( m_pVisibleOnly->isChecked() )
		{
			pfnInclude = [ &document ]( std::uint32_t nObject ) { return document.IsShown( nObject ); };
		}
		m_Problems = xv::map::CheckMap( *pScene, pfnInclude );
	}
	for ( const xv::map::Problem_t &problem : m_Problems )
	{
		m_pList->addItem( QString::fromStdString( problem.m_szMessage ) );
	}
	if ( m_Problems.empty() )
	{
		m_pList->addItem( tr( "No problems found." ) );
	}
	m_pList->setCurrentRow( std::min( nRow, m_pList->count() - 1 ) );
	Pick();
}

//-----------------------------------------------------------------------------
// Purpose: The problem's object selected in the views, and the buttons that apply to it.
//-----------------------------------------------------------------------------
void CMapCheckDialog::Pick()
{
	const int nRow = m_pList->currentRow();
	const bool bProblem = nRow >= 0 && static_cast<std::size_t>( nRow ) < m_Problems.size();
	const xv::map::Problem_t *pProblem = bProblem ? &m_Problems[ static_cast<std::size_t>( nRow ) ] : nullptr;
	m_pGoTo->setEnabled( pProblem && pProblem->m_nObject.has_value() );
	m_pFix->setEnabled( pProblem && pProblem->m_bFixable );
	m_pFixAll->setEnabled( pProblem && pProblem->m_bFixable );
	if ( pProblem && pProblem->m_nObject )
	{
		m_Editor.GetDocument().SetSelection( { *pProblem->m_nObject } );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapCheckDialog::GoTo()
{
	Pick();
	const xv::map::Box_t bounds = m_Editor.GetDocument().GetSelectionBounds();
	if ( !bounds.IsEmpty() )
	{
		m_Editor.CenterViewsOn( bounds.GetCenter() );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The edit makes the document say SceneEdited, which runs the check again.
//-----------------------------------------------------------------------------
void CMapCheckDialog::Fix()
{
	const int nRow = m_pList->currentRow();
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene || nRow < 0 || static_cast<std::size_t>( nRow ) >= m_Problems.size() )
	{
		return;
	}
	xv::map::MapEdit_t edit = xv::map::FixProblem( *pScene, m_Problems[ static_cast<std::size_t>( nRow ) ] );
	if ( !edit.IsEmpty() )
	{
		m_Editor.GetDocument().Edit( std::move( edit ), tr( "Fix Problem" ), std::vector<std::uint32_t>{} );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapCheckDialog::FixAll()
{
	const int nRow = m_pList->currentRow();
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene || nRow < 0 || static_cast<std::size_t>( nRow ) >= m_Problems.size() )
	{
		return;
	}
	xv::map::MapEdit_t edit = xv::map::FixProblems( *pScene, m_Problems, m_Problems[ static_cast<std::size_t>( nRow ) ].m_nKind );
	if ( !edit.IsEmpty() )
	{
		m_Editor.GetDocument().Edit( std::move( edit ), tr( "Fix Problems" ), std::vector<std::uint32_t>{} );
	}
}
