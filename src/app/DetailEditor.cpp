//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See DetailEditor.h.
//===========================================================================//
#include "DetailEditor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QCompleter>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include "ImageFiles.h"
#include "InterfaceMode.h"
#include "SpinBoxes.h"
#include "ChoiceBoxes.h"
#include "DetailPreview.h"
#include "KvClipboard.h"
#include "MenuPicker.h"
#include "TextureCanvas.h"
#include "texture/Build.h"
#include "vfs/Vfs.h"

using namespace xv::detail;
using xv::kv1::CNode;

namespace
{
//-----------------------------------------------------------------------------
// Purpose: Store paths on widgets without exposing node pointers across edits.
//-----------------------------------------------------------------------------
QVariant PathData( const NodePath_t &path )
{
	QVariantList values;
	for ( const auto n : path )
	{
		values << QVariant::fromValue( static_cast<qulonglong>( n ) );
	}
	return values;
}
//-----------------------------------------------------------------------------
// Purpose: Restore a document path from a list item or a card.
//-----------------------------------------------------------------------------
NodePath_t DataPath( const QVariant &data )
{
	NodePath_t path;
	for ( const auto &value : data.toList() )
	{
		path.push_back( static_cast<std::size_t>( value.toULongLong() ) );
	}
	return path;
}
//-----------------------------------------------------------------------------
// Purpose: Give fields stable names so focus can survive rebuilding cards.
//-----------------------------------------------------------------------------
QString FieldName( const NodePath_t &path, const QString &szKey )
{
	QString szName = szKey;
	for ( auto n : path )
	{
		szName += u'/' + QString::number( n );
	}
	return szName;
}
//-----------------------------------------------------------------------------
// Purpose: Pick a name that does not collide with an existing child.
//-----------------------------------------------------------------------------
std::string UniqueName( const CNode &parent, std::string_view szBase )
{
	for ( int n = 1;; ++n )
	{
		std::string szName = std::string( szBase ) + std::to_string( n );
		if ( !parent.Find( szName ) )
		{
			return szName;
		}
	}
}
} // namespace

class CDetailSheet : public CTextureCanvas
{
public:
	struct Rect_t
	{
		NodePath_t m_Path;
		QRectF m_Rect;
	};
	std::vector<Rect_t> m_Rects;
	NodePath_t m_Selected;
	std::function<void( NodePath_t )> m_pfnSelect;
	std::function<void( QRectF )> m_pfnRect;
	//-----------------------------------------------------------------------------
	// Purpose: Draw selectable rectangles over the shared texture canvas; the selected one has
	//          handles to move and resize it.
	//-----------------------------------------------------------------------------
	CDetailSheet()
	{
		setObjectName( QStringLiteral( "detailSheet" ) );
		setMouseTracking( true );
		setToolTip( tr( "Click a rectangle to select its entry. Drag inside the selected one to move it, or its handles to resize it; "
						"drag elsewhere to draw it anew. (Sprites can't be turned: the file has no angle for them.) Middle-drag pans; the wheel zooms." ) );
		m_pfnOverlay = [ this ]( QPainter &painter ) {
			for ( const auto &entry : m_Rects )
			{
				const bool bSelected = entry.m_Path == m_Selected;
				QPen pen( bSelected ? QColor( 255, 180, 40 ) : QColor( 60, 190, 240 ) );
				pen.setCosmetic( true );
				pen.setWidth( bSelected ? 2 : 1 );
				painter.setPen( pen );
				painter.drawRect( bSelected && m_nMode != MODE_NONE && m_nMode != MODE_DRAW ? m_Live : entry.m_Rect );
			}
			if ( const QRectF *pSelected = Selected() )
			{
				// The handles, a fixed size on screen.
				const QRectF shown = m_nMode != MODE_NONE && m_nMode != MODE_DRAW ? m_Live : *pSelected;
				const double flHalf = 4.0 / GetScale();
				QPen pen( QColor( 40, 30, 10 ) );
				pen.setCosmetic( true );
				painter.setPen( pen );
				painter.setBrush( QColor( 255, 200, 70 ) );
				for ( const QPointF &at : Handles( shown ) )
				{
					painter.drawRect( QRectF( at.x() - flHalf, at.y() - flHalf, flHalf * 2, flHalf * 2 ) );
				}
				painter.setBrush( Qt::NoBrush );
			}
			if ( m_nMode == MODE_DRAW )
			{
				QPen pen( Qt::white );
				pen.setCosmetic( true );
				pen.setStyle( Qt::DashLine );
				painter.setPen( pen );
				painter.drawRect( QRectF( m_From, m_To ).normalized() );
			}
		};
	}

protected:
	//-----------------------------------------------------------------------------
	// Purpose: A handle of the selected rectangle, its inside (move), or elsewhere (draw anew).
	//-----------------------------------------------------------------------------
	void mousePressEvent( QMouseEvent *pEvent ) override
	{
		if ( pEvent->button() == Qt::LeftButton && !GetImage().isNull() )
		{
			m_From = ImagePoint( pEvent->position() );
			m_To = m_From;
			m_nMode = MODE_NONE;
			if ( const QRectF *pSelected = Selected() )
			{
				m_Start = *pSelected;
				m_Live = m_Start;
				m_nHandle = HandleAt( pEvent->position() );
				m_nMode = m_nHandle >= 0 ? MODE_RESIZE : pSelected->contains( m_From ) ? MODE_MOVE
																					   : MODE_NONE;
			}
			if ( m_nMode == MODE_NONE && QRectF( GetImage().rect() ).contains( m_From ) )
			{
				m_nMode = MODE_DRAW;
			}
			return;
		}
		CTextureCanvas::mousePressEvent( pEvent );
	}
	//-----------------------------------------------------------------------------
	// Purpose: Follow the drag; with no button, show what a press there would do.
	//-----------------------------------------------------------------------------
	void mouseMoveEvent( QMouseEvent *pEvent ) override
	{
		if ( pEvent->buttons() & Qt::MiddleButton )
		{
			QMouseEvent moved( pEvent->type(), pEvent->position(), pEvent->globalPosition(), Qt::NoButton, Qt::LeftButton, pEvent->modifiers() );
			CTextureCanvas::mouseMoveEvent( &moved );
			return;
		}
		if ( !( pEvent->buttons() & Qt::LeftButton ) )
		{
			static constexpr Qt::CursorShape CURSORS[] = { Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor, Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor };
			const int nHandle = Selected() ? HandleAt( pEvent->position() ) : -1;
			const QRectF *pSelected = Selected();
			setCursor( nHandle >= 0 ? CURSORS[ nHandle ] : pSelected && pSelected->contains( ImagePoint( pEvent->position() ) ) ? Qt::SizeAllCursor
																																 : Qt::CrossCursor );
			return;
		}
		m_To = ImagePoint( pEvent->position() );
		const QRectF bounds( GetImage().rect() );
		const QPointF delta = m_To - m_From;
		if ( m_nMode == MODE_MOVE )
		{
			// Kept whole on the sheet.
			QRectF moved = m_Start.translated( std::round( delta.x() ), std::round( delta.y() ) );
			moved.moveLeft( std::clamp( moved.left(), 0.0, bounds.width() - moved.width() ) );
			moved.moveTop( std::clamp( moved.top(), 0.0, bounds.height() - moved.height() ) );
			m_Live = moved;
		}
		else if ( m_nMode == MODE_RESIZE )
		{
			// Handles go round from the top left: 0 1 2 / 7 . 3 / 6 5 4.
			double flLeft = m_Start.left(), flTop = m_Start.top(), flRight = m_Start.right(), flBottom = m_Start.bottom();
			const double x = std::clamp( std::round( m_To.x() ), 0.0, bounds.width() );
			const double y = std::clamp( std::round( m_To.y() ), 0.0, bounds.height() );
			if ( m_nHandle == 0 || m_nHandle == 6 || m_nHandle == 7 )
			{
				flLeft = std::min( x, flRight - 1.0 );
			}
			if ( m_nHandle >= 2 && m_nHandle <= 4 )
			{
				flRight = std::max( x, flLeft + 1.0 );
			}
			if ( m_nHandle <= 2 )
			{
				flTop = std::min( y, flBottom - 1.0 );
			}
			if ( m_nHandle >= 4 && m_nHandle <= 6 )
			{
				flBottom = std::max( y, flTop + 1.0 );
			}
			m_Live = QRectF( QPointF( flLeft, flTop ), QPointF( flRight, flBottom ) );
		}
		update();
	}
	//-----------------------------------------------------------------------------
	// Purpose: Commit a move or resize, select a hit rectangle, or commit a drawn one.
	//-----------------------------------------------------------------------------
	void mouseReleaseEvent( QMouseEvent *pEvent ) override
	{
		if ( pEvent->button() != Qt::LeftButton || m_nMode == MODE_NONE )
		{
			return;
		}
		const Mode_t nMode = m_nMode;
		m_nMode = MODE_NONE;
		m_To = ImagePoint( pEvent->position() );
		const bool bClick = QLineF( m_From, m_To ).length() * GetScale() < 4;
		if ( ( nMode == MODE_MOVE || nMode == MODE_RESIZE ) && !bClick )
		{
			if ( m_pfnRect && m_Live != m_Start )
			{
				m_pfnRect( m_Live );
			}
		}
		else if ( bClick )
		{
			// Cycle overlapping rectangles so entries sharing a sprite remain reachable.
			std::vector<NodePath_t> hits;
			for ( const auto &entry : m_Rects )
			{
				if ( entry.m_Rect.contains( m_To ) )
				{
					hits.push_back( entry.m_Path );
				}
			}
			if ( !hits.empty() && m_pfnSelect )
			{
				auto it = std::ranges::find( hits, m_Selected );
				m_pfnSelect( it != hits.end() && ++it != hits.end() ? *it : hits.front() );
			}
		}
		else if ( m_pfnRect )
		{
			// Whole texels, as moving and resizing keep them.
			const auto texel = []( QPointF point ) { return QPointF( std::round( point.x() ), std::round( point.y() ) ); };
			const QRectF area = QRectF( texel( m_From ), texel( m_To ) ).normalized().intersected( QRectF( GetImage().rect() ) );
			if ( area.width() >= 1 && area.height() >= 1 )
			{
				m_pfnRect( area );
			}
		}
		update();
	}

private:
	enum Mode_t
	{
		MODE_NONE,
		MODE_DRAW,
		MODE_MOVE,
		MODE_RESIZE,
	};
	//-----------------------------------------------------------------------------
	// Purpose: The selected entry's rectangle on the sheet, if it has one.
	//-----------------------------------------------------------------------------
	[[nodiscard]] const QRectF *Selected() const
	{
		const auto it = std::ranges::find( m_Rects, m_Selected, &Rect_t::m_Path );
		return it == m_Rects.end() ? nullptr : &it->m_Rect;
	}
	//-----------------------------------------------------------------------------
	// Purpose: The eight handles, from the top left round.
	//-----------------------------------------------------------------------------
	[[nodiscard]] static std::array<QPointF, 8> Handles( const QRectF &r )
	{
		const QPointF c = r.center();
		return { r.topLeft(), QPointF( c.x(), r.top() ), r.topRight(), QPointF( r.right(), c.y() ), r.bottomRight(), QPointF( c.x(), r.bottom() ), r.bottomLeft(), QPointF( r.left(), c.y() ) };
	}
	//-----------------------------------------------------------------------------
	// Purpose: The handle of the selected rectangle under a point of the widget, or -1.
	//-----------------------------------------------------------------------------
	[[nodiscard]] int HandleAt( QPointF at ) const
	{
		const QRectF *pSelected = Selected();
		if ( !pSelected )
		{
			return -1;
		}
		const auto handles = Handles( *pSelected );
		for ( std::size_t i = 0; i < handles.size(); i++ )
		{
			const QPointF onScreen = ImageOrigin() + handles[ i ] * GetScale();
			if ( std::abs( onScreen.x() - at.x() ) <= 6 && std::abs( onScreen.y() - at.y() ) <= 6 )
			{
				return static_cast<int>( i );
			}
		}
		return -1;
	}

	QPointF m_From;
	QPointF m_To;
	QRectF m_Start; // the selected rectangle when the drag began
	QRectF m_Live;  // it as the drag has it now
	Mode_t m_nMode = MODE_NONE;
	int m_nHandle = -1;
};

//-----------------------------------------------------------------------------
// Purpose: Lay out type selection, editable cards, a sprite sheet and navigable messages.
//-----------------------------------------------------------------------------
CDetailEditor::CDetailEditor( CDetailDocument *pDocument, QWidget *pParent )
	: QWidget( pParent ),
	  m_pDocument( pDocument )
{
	pDocument->setParent( this );
	QFile schema( QStringLiteral( ":/data/detail_parameters.txt" ) );
	if ( schema.open( QIODevice::ReadOnly ) )
	{
		m_Schema = CSchema::Parse( schema.readAll().toStdString() );
	}
	auto *pLayout = new QVBoxLayout( this );
	auto *pTools = new QHBoxLayout;
	auto *pNew = new QPushButton( tr( "Add detail type" ) );
	connect( pNew, &QPushButton::clicked, this, &CDetailEditor::AddType );
	pTools->addWidget( pNew );
	pTools->addWidget( new QLabel( tr( "Sprite material:" ) ) );
	m_pMaterial = new QLineEdit( QStringLiteral( "detail/detailsprites" ) );
	m_pMaterial->setToolTip( tr( "The map's detailmaterial value. This is a preview setting; it is not stored in detail.vbsp." ) );
	pTools->addWidget( m_pMaterial, 1 );
	connect( m_pMaterial, &QLineEdit::editingFinished, this, &CDetailEditor::LoadSheet );
	auto *pOpen = new QPushButton( tr( "Open texture" ) );
	connect( pOpen, &QPushButton::clicked, this, [ this ] {
		// A missing material has no texture path to open.
		if ( !m_szTexturePath.isEmpty() )
		{
			emit OpenTextureRequested( m_szTexturePath );
		}
	} );
	pTools->addWidget( pOpen );
	auto *pRefresh = new QToolButton;
	pRefresh->setText( tr( "Refresh" ) );
	pRefresh->setToolTip( tr( "Reload the sprite sheet and scan material references." ) );
	connect( pRefresh, &QToolButton::clicked, this, &CDetailEditor::RefreshRequested );
	pTools->addWidget( pRefresh );
	auto *pPack = new QPushButton( tr( "Pack pictures…" ) );
	connect( pPack, &QPushButton::clicked, this, &CDetailEditor::ChoosePictures );
	pTools->addWidget( pPack );
	pLayout->addLayout( pTools );
	auto *pColumns = new QSplitter;
	auto *pTypesHost = new QWidget;
	auto *pTypesLayout = new QVBoxLayout( pTypesHost );
	pTypesLayout->setContentsMargins( 0, 0, 0, 0 );
	auto *pFilter = new QLineEdit;
	pFilter->setObjectName( QStringLiteral( "detailFilter" ) );
	pFilter->setPlaceholderText( tr( "Filter detail types…" ) );
	pTypesLayout->addWidget( pFilter );
	m_pTypes = new QListWidget;
	m_pTypes->setObjectName( QStringLiteral( "detailTypes" ) );
	pTypesLayout->addWidget( m_pTypes, 1 );
	// Below the types: the ground the 3D preview stands them on.
	auto *pGroundLabel = new QLabel( tr( "Ground (for the 3D preview)" ) );
	m_pGround = MakeSearchBox( {} );
	m_pGround->setObjectName( QStringLiteral( "detailGround" ) );
	m_pGround->lineEdit()->setPlaceholderText( tr( "a material, e.g. nature/blendgrassdirt01" ) );
	m_pGround->setToolTip( tr( "Materials whose %detailtype is this type are listed; any other can be typed" ) );
	m_pGroundPicture = new QLabel;
	m_pGroundPicture->setObjectName( QStringLiteral( "detailGroundPicture" ) );
	m_pGroundPicture->setFixedHeight( 96 );
	m_pGroundPicture->setAlignment( Qt::AlignCenter );
	pTypesLayout->addWidget( pGroundLabel );
	pTypesLayout->addWidget( m_pGround );
	pTypesLayout->addWidget( m_pGroundPicture );
	const auto chooseGround = [ this ] {
		m_bGroundChosen = true;
		SetGround( m_pGround->currentText().trimmed() );
	};
	connect( m_pGround->lineEdit(), &QLineEdit::editingFinished, this, chooseGround );
	connect( m_pGround->completer(), qOverload<const QString &>( &QCompleter::activated ), this, [ this, chooseGround ]( const QString &szText ) {
		m_pGround->setEditText( szText );
		chooseGround();
	} );
	connect( pFilter, &QLineEdit::textChanged, this, [ this ]( const QString &szText ) {
		for ( int i = 0; i < m_pTypes->count(); ++i )
		{
			m_pTypes->item( i )->setHidden( !m_pTypes->item( i )->text().contains( szText, Qt::CaseInsensitive ) );
		}
	} );
	connect( m_pTypes, &QListWidget::currentItemChanged, this, [ this ]( QListWidgetItem *pItem ) {
		if ( pItem && !m_bFilling )
		{
			m_Selected = DataPath( pItem->data( Qt::UserRole ) );
			Rebuild();
		}
	} );
	m_pCards = new QScrollArea;
	m_pCards->setWidgetResizable( true );
	m_pSheet = new CDetailSheet;
	m_pSheet->m_pfnSelect = [ this ]( NodePath_t path ) { SelectNode( std::move( path ) ); };
	m_pSheet->m_pfnRect = [ this ]( QRectF area ) { SetSpriteRect( m_Selected, area ); };
	// The right: the sheet or the 3D preview.
	m_pPreview = new CDetailPreview;
	m_pSheetButton = new QToolButton;
	m_pSheetButton->setText( tr( "Sprite Sheet" ) );
	m_p3dButton = new QToolButton;
	m_p3dButton->setObjectName( QStringLiteral( "detail3dButton" ) );
	m_p3dButton->setText( tr( "3D Preview" ) );
	auto *pViews = new QButtonGroup( this );
	auto *pSwitch = new QHBoxLayout;
	pSwitch->addStretch();
	for ( QToolButton *pButton : { m_pSheetButton, m_p3dButton } )
	{
		pButton->setCheckable( true );
		pButton->setAutoRaise( true );
		pViews->addButton( pButton );
		pSwitch->addWidget( pButton );
	}
	pSwitch->addStretch();
	m_pSheetButton->setChecked( true );
	m_pRight = new QStackedWidget;
	m_pRight->addWidget( m_pSheet );
	m_pRight->addWidget( m_pPreview );
	auto *pRightHost = new QWidget;
	auto *pRightLayout = new QVBoxLayout( pRightHost );
	pRightLayout->setContentsMargins( 0, 0, 0, 0 );
	pRightLayout->addLayout( pSwitch );
	pRightLayout->addWidget( m_pRight, 1 );
	connect( m_pSheetButton, &QToolButton::clicked, this, [ this ] { ShowPreview( false ); } );
	connect( m_p3dButton, &QToolButton::clicked, this, [ this ] { ShowPreview( true ); } );
	pColumns->addWidget( pTypesHost );
	pColumns->addWidget( m_pCards );
	pColumns->addWidget( pRightHost );
	pColumns->setSizes( { 180, 520, 400 } );
	auto *pRows = new QSplitter( Qt::Vertical );
	pRows->addWidget( pColumns );
	m_pMessages = new QTreeWidget;
	m_pMessages->setObjectName( QStringLiteral( "detailMessages" ) );
	m_pMessages->setHeaderLabels( { tr( "Detail type / group / entry" ), tr( "Messages" ) } );
	m_pMessages->setColumnWidth( 0, 270 );
	m_pMessages->header()->setStretchLastSection( true );
	connect( m_pMessages, &QTreeWidget::itemActivated, this, [ this ]( QTreeWidgetItem *pItem ) { SelectNode( DataPath( pItem->data( 0, Qt::UserRole ) ) ); } );
	pRows->addWidget( m_pMessages );
	pRows->setSizes( { 650, 140 } );
	pLayout->addWidget( pRows );
	connect( pDocument, &CDetailDocument::Changed, this, &CDetailEditor::QueueRebuild );
	connect( &CInterfaceMode::Instance(), &CInterfaceMode::Changed, this, &CDetailEditor::QueueRebuild );
	Rebuild();
}

//-----------------------------------------------------------------------------
// Purpose: Defer rebuilding until the value editor has finished emitting its edit signal.
//-----------------------------------------------------------------------------
void CDetailEditor::QueueRebuild()
{
	if ( m_bQueued )
	{
		return;
	}
	m_bQueued = true;
	QTimer::singleShot( 0, this, [ this ] { m_bQueued = false; Rebuild(); } );
}

//-----------------------------------------------------------------------------
// Purpose: Recreate the selected type's cards, retaining scroll and keyboard focus.
//-----------------------------------------------------------------------------
void CDetailEditor::Rebuild()
{
	const int nScroll = m_pCards->verticalScrollBar()->value();
	QString szFocus;
	if ( auto *pFocus = QApplication::focusWidget(); pFocus && m_pCards->isAncestorOf( pFocus ) )
	{
		szFocus = pFocus->objectName();
	}
	m_bFilling = true;
	const auto types = Types( m_pDocument->GetDocument() );
	m_pTypes->clear();
	if ( m_Selected.size() < 2 && !types.empty() )
	{
		m_Selected = types.front().m_Path;
	}
	NodePath_t typePath;
	for ( const auto &type : types )
	{
		auto *pItem = new QListWidgetItem( QString::fromStdString( type.m_szName ), m_pTypes );
		pItem->setData( Qt::UserRole, PathData( type.m_Path ) );
		pItem->setHidden( !pItem->text().contains( findChild<QLineEdit *>( QStringLiteral( "detailFilter" ) )->text(), Qt::CaseInsensitive ) );
		if ( m_Selected.size() >= 2 && type.m_Path[ 0 ] == m_Selected[ 0 ] && type.m_Path[ 1 ] == m_Selected[ 1 ] )
		{
			m_pTypes->setCurrentItem( pItem );
			typePath = type.m_Path;
		}
	}
	if ( typePath.empty() && !types.empty() )
	{
		typePath = types.front().m_Path;
		m_Selected = typePath;
		m_pTypes->setCurrentRow( 0 );
	}
	auto *pHost = new QWidget;
	auto *pLayout = new QVBoxLayout( pHost );
	if ( !typePath.empty() )
	{
		pLayout->addWidget( BuildCard( typePath ) );
	}
	else
	{
		pLayout->addWidget( new QLabel( tr( "Add a detail type to begin." ) ) );
	}
	pLayout->addStretch();
	m_pCards->setWidget( pHost );
	m_pCards->verticalScrollBar()->setValue( nScroll );
	if ( !szFocus.isEmpty() )
	{
		if ( auto *pFocus = pHost->findChild<QWidget *>( szFocus ) )
		{
			pFocus->setFocus();
		}
	}
	m_bFilling = false;
	UpdateSheetRects();
	FillGrounds();
	m_pPreview->SetScene( &m_pDocument->GetDocument(), typePath, m_Selected );
	Check();
}

//-----------------------------------------------------------------------------
// Purpose: Build nested cards using schema-driven names, value boxes and add menus.
//-----------------------------------------------------------------------------
QWidget *CDetailEditor::BuildCard( const NodePath_t &path )
{
	const auto *pNode = Node( m_pDocument->GetDocument(), path );
	auto *pCard = new QGroupBox( QString::fromStdString( pNode->GetKey() ) );
	pCard->setObjectName( FieldName( path, QStringLiteral( "card" ) ) );
	pCard->setProperty( "selected", path == m_Selected );
	if ( path == m_Selected )
	{
		pCard->setStyleSheet( QStringLiteral( "QGroupBox[selected=\"true\"] { border: 1px solid palette(highlight); margin-top: 12px; padding-top: 8px; } QGroupBox::title { subcontrol-origin: margin; }" ) );
	}
	// Right-click, as everywhere: copy and paste, add, rename, delete.
	pCard->setContextMenuPolicy( Qt::CustomContextMenu );
	connect( pCard, &QWidget::customContextMenuRequested, this, [ this, pCard, path ]( const QPoint &at ) { ShowCardMenu( path, pCard->mapToGlobal( at ) ); } );
	auto *pLayout = new QVBoxLayout( pCard );
	auto *pTools = new QHBoxLayout;
	const auto button = [ & ]( const QString &szLabel, const std::function<void()> &pfnAction ) {
		auto *pButton = new QToolButton;
		pButton->setText( szLabel );
		connect( pButton, &QToolButton::clicked, this, pfnAction );
		pTools->addWidget( pButton );
	};
	button( tr( "Select" ), [ this, path ] { SelectNode( path ); } );
	button( tr( "Rename…" ), [ this, path ] { RenameNode( path ); } );
	button( tr( "Delete" ), [ this, path ] { RemoveNode( path ); } );
	if ( path.size() == 2 )
	{
		button( tr( "Add group" ), [ this, path ] { AddBlock( path, false ); } );
	}
	if ( path.size() == 3 )
	{
		button( tr( "Add sprite" ), [ this, path ] { AddBlock( path, false ); } );
		button( tr( "Add model" ), [ this, path ] { AddBlock( path, true ); } );
	}
	auto *pAdd = new QToolButton;
	pAdd->setText( tr( "Add field" ) );
	pAdd->setPopupMode( QToolButton::InstantPopup );
	auto *pMenu = new QMenu( pAdd );
	for ( const auto &key : m_Schema.GetKeys() )
	{
		if ( key.m_nDepth != static_cast<int>( path.size() ) || pNode->Find( key.m_szKey ) )
		{
			continue;
		}
		pMenu->addAction( QString::fromStdString( CInterfaceMode::Instance().IsSimple() ? key.m_szLabel : key.m_szKey ), this, [ this, path, key ] { SetField( path, QString::fromStdString( key.m_szKey ), QString::fromStdString( key.m_szDefault ) ); } );
	}
	pMenu->addAction( tr( "Custom field…" ), this, [ this, path ] {
		const QString szKey = QInputDialog::getText( this, tr( "Add field" ), tr( "Key:" ) );
		if ( !szKey.trimmed().isEmpty() && !szKey.contains( u'"' ) )
		{
			SetField( path, szKey, {} );
		}
	} );
	pAdd->setMenu( pMenu );
	pTools->addWidget( pAdd );
	pTools->addStretch();
	pLayout->addLayout( pTools );
	auto *pForm = new QFormLayout;
	pForm->setRowWrapPolicy( QFormLayout::WrapLongRows );
	for ( std::size_t i = 0; i < pNode->GetChildren().size(); ++i )
	{
		const auto &pair = pNode->Child( i );
		if ( !pair.IsPair() )
		{
			continue;
		}
		const QString szKey = QString::fromStdString( pair.GetKey() );
		const QString szValue = QString::fromStdString( pair.GetValue() );
		const auto *pDef = m_Schema.Find( pair.GetKey(), static_cast<int>( path.size() ) );
		auto *pRow = new QWidget;
		auto *pRowLayout = new QHBoxLayout( pRow );
		pRowLayout->setContentsMargins( 0, 0, 0, 0 );
		const auto commit = [ this, path, i ]( const QString &szText ) {
			m_pDocument->Edit( tr( "Edit detail field" ), [ & ]( auto &document ) {
				if ( auto *pBlock = Node( document, path ); pBlock && i < pBlock->GetChildren().size() )
				{
					pBlock->Child( i ).SetValue( szText.toStdString() );
				}
			} );
		};
		const auto numbers = Numbers( pair.GetValue() );
		if ( pDef && ( pDef->m_szKind == "orientation" || pDef->m_szKind == "shape" ) )
		{
			auto *pBox = new QComboBox;
			if ( pDef->m_szKind == "shape" )
			{
				pBox->addItem( tr( "Cross" ), QStringLiteral( "cross" ) );
				pBox->addItem( tr( "Triangle" ), QStringLiteral( "tri" ) );
			}
			else
			{
				pBox->addItem( tr( "Fixed" ), QStringLiteral( "0" ) );
				pBox->addItem( tr( "Face camera" ), QStringLiteral( "1" ) );
				pBox->addItem( tr( "Face camera, stay vertical" ), QStringLiteral( "2" ) );
			}
			int nIndex = pBox->findData( szValue );
			if ( nIndex < 0 )
			{
				pBox->addItem( szValue, szValue );
				nIndex = pBox->count() - 1;
			}
			pBox->setCurrentIndex( nIndex );
			connect( pBox, &QComboBox::activated, this, [ pBox, commit ] { commit( pBox->currentData().toString() ); } );
			pBox->setObjectName( FieldName( path, szKey ) );
			pRowLayout->addWidget( pBox );
		}
		else if ( pDef && CInterfaceMode::Instance().IsSimple() && !numbers.empty() && pDef->m_szKind != "presence" && pDef->m_szKind != "model" )
		{
			QList<QDoubleSpinBox *> boxes;
			for ( std::size_t n = 0; n < numbers.size(); ++n )
			{
				auto *pBox = new CFloatSpin;
				pBox->setDecimals( 6 );
				pBox->setRange( -1e12, 1e12 );
				pBox->setValue( numbers[ n ] );
				pBox->setKeyboardTracking( false );
				pBox->setMinimumWidth( 80 );
				pBox->setMaximumWidth( numbers.size() == 1 ? 190 : 110 );
				if ( pDef->m_szKind == "positive" )
					pBox->setSuffix( tr( " /1M u²" ) );
				if ( pDef->m_szKind == "rect" && n < 5 )
				{
					const QStringList labels{ tr( "X " ), tr( "Y " ), tr( "W " ), tr( "H " ), tr( "Sheet " ) };
					pBox->setPrefix( labels[ static_cast<qsizetype>( n ) ] );
					if ( n == 4 )
					{
						pBox->setMinimumWidth( 115 );
						pBox->setMaximumWidth( 140 );
					}
				}
				if ( pDef->m_szKind == "size" && n < 4 )
				{
					const QStringList labels{ tr( "U " ), tr( "V " ), tr( "W " ), tr( "H " ) };
					pBox->setPrefix( labels[ static_cast<qsizetype>( n ) ] );
					if ( n >= 2 )
					{
						pBox->setSuffix( tr( " u" ) );
					}
				}
				if ( pDef->m_szUnits == "degrees" )
				{
					pBox->setSuffix( QStringLiteral( "°" ) );
				}
				pBox->setObjectName( FieldName( path, szKey ) + QString::number( n ) );
				pRowLayout->addWidget( pBox );
				boxes << pBox;
			}
			std::vector<double> initial;
			for ( auto *pBox : boxes )
			{
				initial.push_back( pBox->value() );
			}
			for ( auto *pBox : boxes )
			{
				connect( pBox, &QDoubleSpinBox::editingFinished, this, [ boxes, commit, numbers, initial ] {
					bool bChanged = false;
					QStringList values;
					for ( qsizetype n = 0; n < boxes.size(); ++n )
					{
						const bool bEdited = boxes[ n ]->value() != initial[ static_cast<std::size_t>( n ) ];
						bChanged |= bEdited;
						values << QString::number( bEdited ? boxes[ n ]->value() : numbers[ static_cast<std::size_t>( n ) ], 'g', 17 );
					}
					if ( bChanged )
					{
						commit( values.join( u' ' ) );
					}
				} );
			}
		}
		else
		{
			auto *pText = new QLineEdit( szValue );
			pText->setObjectName( FieldName( path, szKey ) );
			connect( pText, &QLineEdit::editingFinished, this, [ pText, commit ] { commit( pText->text() ); } );
			pRowLayout->addWidget( pText );
		}

		auto *pRemove = new QToolButton;
		pRemove->setText( QStringLiteral( "×" ) );
		pRemove->setToolTip( tr( "Remove field" ) );
		connect( pRemove, &QToolButton::clicked, this, [ this, path, i ] { auto child = path; child.push_back(i); RemoveNode(child); } );
		pRowLayout->addWidget( pRemove );
		pRow->setToolTip( szKey + ( pDef && !pDef->m_szUnits.empty() ? QStringLiteral( " — " ) + QString::fromStdString( pDef->m_szUnits ) : QString{} ) );
		pForm->addRow( pDef && CInterfaceMode::Instance().IsSimple() ? QString::fromStdString( pDef->m_szLabel ) : szKey, pRow );
	}
	// An entry's sprite (or model) beside its fields.
	if ( path.size() == 4 )
	{
		auto *pBeside = new QHBoxLayout;
		pBeside->addLayout( pForm, 1 );
		pBeside->addWidget( EntryPicture( path ), 0, Qt::AlignTop );
		pLayout->addLayout( pBeside );
	}
	else
	{
		pLayout->addLayout( pForm );
	}
	for ( std::size_t i = 0; i < pNode->GetChildren().size(); ++i )
	{
		if ( pNode->Child( i ).IsBlock() )
		{
			auto child = path;
			child.push_back( i );
			pLayout->addWidget( BuildCard( child ) );
		}
	}
	return pCard;
}

//-----------------------------------------------------------------------------
// Purpose: Refresh mounted resources and the complete material reference index.
//-----------------------------------------------------------------------------
void CDetailEditor::SetContext( xv::material::Reader_t pfnRead, std::vector<std::string> materials, bool bHaveGameFiles )
{
	m_pfnRead = std::move( pfnRead );
	m_Context.m_pfnExists = bHaveGameFiles && m_pfnRead ? std::function<bool( std::string_view )>( [ this ]( std::string_view szPath ) { return m_pfnRead( szPath ).has_value(); } ) : std::function<bool( std::string_view )>{};
	m_Context.m_Uses = bHaveGameFiles && m_pfnRead ? std::optional( ScanMaterials( materials, m_pfnRead ) ) : std::nullopt;
	m_pPreview->SetReader( m_pfnRead );
	m_szGroundType.clear();
	FillGrounds();
	LoadSheet();
}
//-----------------------------------------------------------------------------
// Purpose: Navigate from a material's detail type reference.
//-----------------------------------------------------------------------------
void CDetailEditor::SelectType( const QString &szName )
{
	for ( const auto &type : Types( m_pDocument->GetDocument() ) )
	{
		if ( QString::fromStdString( type.m_szName ).compare( szName, Qt::CaseInsensitive ) == 0 )
		{
			SelectNode( type.m_Path );
			return;
		}
	}
}
//-----------------------------------------------------------------------------
// Purpose: Synchronize selected cards and sprite rectangles.
//-----------------------------------------------------------------------------
void CDetailEditor::SelectNode( NodePath_t path )
{
	if ( path.size() < 2 || !Node( m_pDocument->GetDocument(), path ) )
	{
		return;
	}
	m_Selected = std::move( path );
	// Selection may originate in a card's button signal; keep that button alive until it returns.
	QueueRebuild();
	QTimer::singleShot( 0, this, [ this ] {
		if ( auto *pCard = m_pCards->widget()->findChild<QWidget *>( FieldName( m_Selected, QStringLiteral( "card" ) ) ) )
		{
			m_pCards->ensureVisible( 0, pCard->mapTo( m_pCards->widget(), QPoint( 0, 0 ) ).y(), 0, 12 );
		}
	} );
}
//-----------------------------------------------------------------------------
// Purpose: Apply one field edit to the original KeyValues tokens.
//-----------------------------------------------------------------------------
void CDetailEditor::SetField( const NodePath_t &path, const QString &szKey, const QString &szValue )
{
	m_pDocument->Edit( tr( "Set %1" ).arg( szKey ), [ & ]( auto &document ) { Set( document, path, szKey.toStdString(), szValue.toStdString() ); } );
}
//-----------------------------------------------------------------------------
// Purpose: Make a unique detail type inside the file's wrapper.
//-----------------------------------------------------------------------------
void CDetailEditor::AddType()
{
	const QString szName = QInputDialog::getText( this, tr( "Add detail type" ), tr( "Name used by materials:" ) );
	if ( szName.trimmed().isEmpty() || szName.contains( u'"' ) )
	{
		return;
	}
	for ( const auto &type : Types( m_pDocument->GetDocument() ) )
	{
		if ( xv::kv1::EqualsIgnoreCase( type.m_szName, szName.toStdString() ) )
		{
			SelectType( szName );
			return;
		}
	}
	m_pDocument->Edit( tr( "Add detail type" ), [ & ]( auto &document ) {
		auto *pRoot = document.Find( "detail" );
		if ( !pRoot )
		{
			pRoot = &document.AppendRoot( CNode::MakeBlock( "detail" ) );
		}
		if ( !pRoot->IsBlock() )
		{
			return;
		}
		auto type = CNode::MakeBlock( szName.toStdString() );
		type.AppendChild( CNode::MakePair( "density", "1000" ) );
		auto group = CNode::MakeBlock( "Group1" );
		group.AppendChild( CNode::MakePair( "alpha", "1" ) );
		type.AppendChild( std::move( group ) );
		pRoot->AppendChild( std::move( type ) );
	} );
	SelectType( szName );
}
//-----------------------------------------------------------------------------
// Purpose: Add a group, sprite or model with editable starting values.
//-----------------------------------------------------------------------------
void CDetailEditor::AddBlock( const NodePath_t &parent, bool bModel )
{
	m_pDocument->Edit( tr( "Add detail block" ), [ & ]( auto &document ) {
		auto *pParent = Node( document, parent );
		if ( !pParent || !pParent->IsBlock() )
		{
			return;
		}
		auto block = CNode::MakeBlock( UniqueName( *pParent, parent.size() == 2 ? "Group" : bModel ? "Model"
																								   : "Sprite" ) );
		if ( parent.size() == 2 )
		{
			block.AppendChild( CNode::MakePair( "alpha", "1" ) );
		}
		else
		{
			block.AppendChild( CNode::MakePair( "amount", "1" ) );
			if ( bModel )
			{
				block.AppendChild( CNode::MakePair( "model", "models/props/example.mdl" ) );
			}
			else
			{
				block.AppendChild( CNode::MakePair( "sprite", "0 0 128 128 512" ) );
				block.AppendChild( CNode::MakePair( "spritesize", "0.5 0 16 16" ) );
				block.AppendChild( CNode::MakePair( "detailOrientation", "2" ) );
			}
		}
		m_Selected = parent;
		m_Selected.push_back( pParent->GetChildren().size() );
		pParent->AppendChild( std::move( block ) );
	} );
}
//-----------------------------------------------------------------------------
// Purpose: Remove exactly the selected token subtree as one undoable change.
//-----------------------------------------------------------------------------
void CDetailEditor::RemoveNode( const NodePath_t &path )
{
	if ( path.size() < 2 )
	{
		return;
	}
	auto parent = path;
	parent.pop_back();
	m_pDocument->Edit( tr( "Delete detail item" ), [ & ]( auto &document ) {
		if ( auto *pParent = Node( document, parent ); pParent && path.back() < pParent->GetChildren().size() )
		{
			pParent->RemoveChild( path.back() );
		}
	} );
	m_Selected = parent;
}
//-----------------------------------------------------------------------------
// Purpose: Rename a block without rewriting its body or allowing ambiguous siblings.
//-----------------------------------------------------------------------------
void CDetailEditor::RenameNode( const NodePath_t &path )
{
	const auto *pNode = Node( m_pDocument->GetDocument(), path );
	if ( !pNode )
	{
		return;
	}
	const QString szName = QInputDialog::getText( this, tr( "Rename" ), tr( "Name:" ), QLineEdit::Normal, QString::fromStdString( pNode->GetKey() ) );
	if ( szName.trimmed().isEmpty() || szName.contains( u'"' ) )
	{
		return;
	}
	auto parent = path;
	parent.pop_back();
	const auto *pParent = Node( m_pDocument->GetDocument(), parent );
	if ( const auto *pOther = pParent ? pParent->Find( szName.toStdString() ) : nullptr; pOther && pOther != pNode )
	{
		return;
	}
	m_pDocument->Edit( tr( "Rename detail item" ), [ & ]( auto &document ) { if ( auto *pTarget = Node(document,path) ) { pTarget->SetKey(szName.toStdString()); 
} } );
}
//-----------------------------------------------------------------------------
// Purpose: Expose the map's chosen detail sprite material without adding non-format keys.
//-----------------------------------------------------------------------------
QString CDetailEditor::GetSpriteMaterial() const
{
	return m_pMaterial->text().trimmed();
}
//-----------------------------------------------------------------------------
// Purpose: Change the preview's map context and load its sprite texture.
//-----------------------------------------------------------------------------
void CDetailEditor::SetSpriteMaterial( const QString &szMaterial )
{
	m_pMaterial->setText( szMaterial );
	LoadSheet();
}
//-----------------------------------------------------------------------------
// Purpose: Load the chosen material's base texture through the active resource reader.
//-----------------------------------------------------------------------------
void CDetailEditor::LoadSheet()
{
	QString szMaterial = GetSpriteMaterial();
	if ( szMaterial.startsWith( QStringLiteral( "materials/" ), Qt::CaseInsensitive ) )
	{
		szMaterial.remove( 0, 10 );
	}
	if ( szMaterial.endsWith( QStringLiteral( ".vmt" ), Qt::CaseInsensitive ) )
	{
		szMaterial.chop( 4 );
	}
	m_pMaterial->setText( szMaterial );
	m_Context.m_szMaterial = szMaterial.toStdString();
	m_szTexturePath.clear();
	QImage image;
	if ( m_pfnRead )
	{
		const auto bytes = m_pfnRead( "materials/" + szMaterial.toStdString() + ".vmt" );
		if ( bytes )
		{
			const auto material = xv::material::ParseMaterial( std::string_view( reinterpret_cast<const char *>( bytes->data() ), bytes->size() ), m_pfnRead );
			if ( material && !material->Value( "$basetexture" ).empty() )
			{
				m_szTexturePath = QString::fromStdString( xv::material::TexturePath( material->Value( "$basetexture" ) ) );
				if ( const auto texture = m_pfnRead( m_szTexturePath.toStdString() ) )
				{
					if ( auto decoded = DecodeImageFile( m_szTexturePath, *texture ) )
					{
						image = ToQImage( *decoded );
					}
				}
			}
		}
	}
	if ( image.isNull() && m_PackedImages.contains( szMaterial ) )
	{
		image = m_PackedImages.at( szMaterial );
		m_szTexturePath = QString::fromStdString( xv::material::TexturePath( szMaterial.toStdString() ) );
	}
	m_pSheet->SetImage( image );
	m_pPreview->SetSheet( image );
	UpdateSheetRects();
	// The entries' pictures come from the sheet.
	QueueRebuild();
	Check();
}
//-----------------------------------------------------------------------------
// Purpose: Display every sprite rectangle using its own declared square coordinate scale.
//-----------------------------------------------------------------------------
void CDetailEditor::UpdateSheetRects()
{
	m_pSheet->m_Rects.clear();
	m_pSheet->m_Selected = m_Selected;
	const auto size = m_pSheet->GetImage().size();
	for ( const auto &type : Types( m_pDocument->GetDocument() ) )
	{
		for ( const auto &group : type.m_Groups )
		{
			for ( const auto &entry : group.m_Entries )
			{
				if ( !entry.m_Sprite || ( *entry.m_Sprite )[ 4 ] <= 0 )
				{
					continue;
				}
				const auto &r = *entry.m_Sprite;
				if ( r[ 0 ] < 0 || r[ 1 ] < 0 || r[ 2 ] <= 0 || r[ 3 ] <= 0 || r[ 0 ] + r[ 2 ] > r[ 4 ] || r[ 1 ] + r[ 3 ] > r[ 4 ] )
				{
					continue;
				}
				m_pSheet->m_Rects.push_back( { entry.m_Path, QRectF( r[ 0 ] / r[ 4 ] * size.width(), r[ 1 ] / r[ 4 ] * size.height(), r[ 2 ] / r[ 4 ] * size.width(), r[ 3 ] / r[ 4 ] * size.height() ) } );
			}
		}
	}
	m_pSheet->update();
}
//-----------------------------------------------------------------------------
// Purpose: Convert a pixel selection to square sheet coordinates; resize world dimensions together.
//-----------------------------------------------------------------------------
void CDetailEditor::SetSpriteRect( const NodePath_t &path, const QRectF &rect )
{
	const auto size = m_pSheet->GetImage().size();
	const auto *pNode = Node( m_pDocument->GetDocument(), path );
	if ( size.isEmpty() || path.size() != 4 || !pNode || !pNode->Find( "sprite" ) || rect.isEmpty() )
	{
		return;
	}
	const auto clipped = rect.intersected( QRectF( QPointF( 0, 0 ), QSizeF( size ) ) );
	if ( clipped.isEmpty() )
	{
		return;
	}
	const auto oldRect = Numbers( pNode->Find( "sprite" )->GetValue() );
	const auto *pSize = pNode->Find( "spritesize" );
	const auto oldSize = pSize ? Numbers( pSize->GetValue() ) : std::vector<double>{};
	const double flHeight = clipped.height() / size.height() * size.width();
	const double flY = clipped.y() / size.height() * size.width();
	const QString szRect = QStringLiteral( "%1 %2 %3 %4 %5" ).arg( clipped.x(), 0, 'g', 12 ).arg( flY, 0, 'g', 12 ).arg( clipped.width(), 0, 'g', 12 ).arg( flHeight, 0, 'g', 12 ).arg( size.width() );
	double flWidthUnits = clipped.width(), flHeightUnits = clipped.height();
	if ( oldSize.size() == 4 && oldRect.size() == 5 && oldRect[ 2 ] > 0 && oldRect[ 3 ] > 0 && oldRect[ 4 ] > 0 )
	{
		flWidthUnits = oldSize[ 2 ] * ( clipped.width() / size.width() ) / ( oldRect[ 2 ] / oldRect[ 4 ] );
		flHeightUnits = oldSize[ 3 ] * ( clipped.height() / size.height() ) / ( oldRect[ 3 ] / oldRect[ 4 ] );
	}
	const QString szSize = QStringLiteral( "%1 %2 %3 %4" ).arg( oldSize.size() == 4 ? oldSize[ 0 ] : .5 ).arg( oldSize.size() == 4 ? oldSize[ 1 ] : 0 ).arg( flWidthUnits, 0, 'g', 12 ).arg( flHeightUnits, 0, 'g', 12 );
	m_pDocument->Edit( tr( "Set sprite rectangle and size" ), [ & ]( auto &document ) {
		Set( document, path, "sprite", szRect.toStdString() );
		Set( document, path, "spritesize", szSize.toStdString() );
	} );
}
//-----------------------------------------------------------------------------
// Purpose: List syntax, value and resource issues, with paths back to their cards.
//-----------------------------------------------------------------------------
void CDetailEditor::Check()
{
	m_Issues = Lint( m_pDocument->GetDocument(), m_Schema, m_Context );
	m_pMessages->clear();
	for ( const auto &diagnostic : m_pDocument->GetParseDiagnostics() )
	{
		new QTreeWidgetItem( m_pMessages, { tr( "Line %1" ).arg( diagnostic.m_nLine ), QString::fromStdString( diagnostic.m_szMessage ) } );
	}
	for ( const auto &issue : m_Issues )
	{
		QStringList names;
		NodePath_t path;
		for ( const auto n : issue.m_Path )
		{
			path.push_back( n );
			if ( path.size() > 1 )
			{
				if ( const auto *pNode = Node( m_pDocument->GetDocument(), path ) )
				{
					names << QString::fromStdString( pNode->GetKey() );
				}
			}
		}
		auto *pItem = new QTreeWidgetItem( m_pMessages, { names.join( QStringLiteral( " / " ) ), QString::fromStdString( issue.m_szMessage ) } );
		pItem->setData( 0, Qt::UserRole, PathData( issue.m_Path ) );
	}
	if ( !m_szTexturePath.isEmpty() && m_pSheet->GetImage().isNull() )
	{
		new QTreeWidgetItem( m_pMessages, { {}, tr( "Cannot load sprite texture: %1" ).arg( m_szTexturePath ) } );
	}
}
//-----------------------------------------------------------------------------
// Purpose: Pack pictures into a new texture and add their rectangles in one undo step.
//-----------------------------------------------------------------------------
bool CDetailEditor::PackPictures( const NodePath_t &group, const std::map<std::string, xv::texture::CImage> &pictures, const QString &szMaterial, QString *pError )
{
	const auto szNormalized = QString::fromStdString( xv::vfs::NormalizePath( szMaterial.toStdString() ) );
	if ( szNormalized.isEmpty() || szNormalized != szMaterial || szMaterial.startsWith( u'/' ) || szMaterial.contains( u':' ) || szMaterial.contains( u'"' ) || szMaterial.contains( QStringLiteral( ".." ) ) || szMaterial.endsWith( u".vmt" ) || szMaterial.endsWith( u".vtf" ) || szMaterial.startsWith( u"materials/" ) )
	{
		if ( pError )
		{
			*pError = tr( "Use a material path without materials/ or a file extension." );
		}
		return false;
	}
	if ( m_PackedImages.contains( szMaterial ) || ( m_pfnRead && ( m_pfnRead( "materials/" + szMaterial.toStdString() + ".vmt" ) || m_pfnRead( "materials/" + szMaterial.toStdString() + ".vtf" ) ) ) )
	{
		if ( pError )
		{
			*pError = tr( "Choose an unused material name for the new sheet." );
		}
		return false;
	}
	const auto *pGroup = Node( m_pDocument->GetDocument(), group );
	if ( group.size() != 3 || !pGroup || !pGroup->IsBlock() || pictures.empty() )
	{
		return false;
	}
	struct PackedEntry_t
	{
		NodePath_t m_Path;
		xv::texture::CImage m_Image;
	};
	std::vector<PackedEntry_t> entries;
	const QImage &sheet = m_pSheet->GetImage();
	for ( const auto &type : Types( m_pDocument->GetDocument() ) )
	{
		for ( const auto &oldGroup : type.m_Groups )
		{
			for ( const auto &entry : oldGroup.m_Entries )
			{
				const auto *pEntry = Node( m_pDocument->GetDocument(), entry.m_Path );
				if ( !pEntry->Find( "sprite" ) )
				{
					continue;
				}
				if ( sheet.isNull() || !entry.m_Sprite || ( *entry.m_Sprite )[ 4 ] <= 0 )
				{
					if ( pError )
					{
						*pError = tr( "Load the current sprite sheet and correct its rectangles before repacking existing entries." );
					}
					return false;
				}
				const auto &r = *entry.m_Sprite;
				if ( r[ 0 ] < 0 || r[ 1 ] < 0 || r[ 2 ] <= 0 || r[ 3 ] <= 0 || r[ 0 ] + r[ 2 ] > r[ 4 ] || r[ 1 ] + r[ 3 ] > r[ 4 ] )
				{
					if ( pError )
					{
						*pError = tr( "An existing sprite rectangle lies outside the sheet." );
					}
					return false;
				}
				const QRect area = QRectF( r[ 0 ] / r[ 4 ] * sheet.width(), r[ 1 ] / r[ 4 ] * sheet.height(), r[ 2 ] / r[ 4 ] * sheet.width(), r[ 3 ] / r[ 4 ] * sheet.height() ).toAlignedRect();
				if ( area.isEmpty() || !sheet.rect().contains( area ) )
				{
					if ( pError )
					{
						*pError = tr( "An existing sprite rectangle lies outside the sheet." );
					}
					return false;
				}
				entries.push_back( { entry.m_Path, FromQImage( sheet.copy( area ) ) } );
			}
		}
	}
	for ( const auto &[ szName, picture ] : pictures )
	{
		(void)szName;
		entries.push_back( { {}, picture } );
	}
	xv::texture::CMks mks;
	std::map<std::string, xv::texture::CImage> allPictures;
	for ( std::size_t i = 0; i < entries.size(); ++i )
	{
		const auto szName = std::to_string( i );
		mks.m_Sequences.push_back( { static_cast<int>( i ), false, { { { szName }, 1 } } } );
		allPictures.emplace( szName, entries[ i ].m_Image );
	}
	std::string szError;
	const auto packed = xv::texture::PackSheet( mks, allPictures, &szError );
	if ( !packed )
	{
		if ( pError )
		{
			*pError = QString::fromStdString( szError );
		}
		return false;
	}
	// Stock clients use the default detail material's aspect ratio for custom sheets too.
	QSize reference = sheet.isNull() ? QSize( 1, 1 ) : sheet.size();
	if ( m_pfnRead )
	{
		if ( const auto materialBytes = m_pfnRead( "materials/detail/detailsprites.vmt" ) )
		{
			const auto material = xv::material::ParseMaterial( std::string_view( reinterpret_cast<const char *>( materialBytes->data() ), materialBytes->size() ), m_pfnRead );
			if ( material )
			{
				if ( const auto textureBytes = m_pfnRead( xv::material::TexturePath( material->Value( "$basetexture" ) ) ) )
				{
					if ( const auto image = DecodeImageFile( QStringLiteral( "sheet.vtf" ), *textureBytes ) )
					{
						reference = QSize( image->m_nWidth, image->m_nHeight );
					}
				}
			}
		}
	}
	int nWidth = packed->m_Atlas.m_nWidth, nHeight = packed->m_Atlas.m_nHeight;
	while ( static_cast<long long>( nWidth ) * reference.height() != static_cast<long long>( nHeight ) * reference.width() && nWidth <= 8192 && nHeight <= 8192 )
	{
		if ( static_cast<long long>( nWidth ) * reference.height() < static_cast<long long>( nHeight ) * reference.width() )
		{
			nWidth *= 2;
		}
		else
		{
			nHeight *= 2;
		}
	}
	if ( nWidth > 8192 || nHeight > 8192 )
	{
		if ( pError )
		{
			*pError = tr( "The packed sheet cannot fit the game's detail texture aspect ratio within 8192 pixels." );
		}
		return false;
	}
	xv::texture::CImage atlas( nWidth, nHeight );
	for ( int y = 0; y < packed->m_Atlas.m_nHeight; ++y )
	{
		std::copy_n( packed->m_Atlas.Pixel( 0, y ), static_cast<std::size_t>( packed->m_Atlas.m_nWidth ) * 4, atlas.Pixel( 0, y ) );
	}
	xv::texture::BuildOptions_t options;
	options.m_nResize = xv::texture::ResizeMode_t::RESIZE_KEEP;
	options.m_nAlphaFormat = xv::texture::VtfFormat_t::DXT5;
	const std::vector<xv::texture::SourceImage_t> sources{ { atlas, {} } };
	const auto bytes = xv::texture::BuildVtf( sources, options, &szError );
	if ( bytes.empty() )
	{
		if ( pError )
		{
			*pError = QString::fromStdString( szError );
		}
		return false;
	}
	const QString szBefore = GetSpriteMaterial();
	m_PackedImages[ szMaterial ] = ToQImage( atlas );
	const auto pfnPack = [ & ]( xv::kv1::CDocument &document ) {
		auto *pTarget = Node( document, group );
		for ( std::size_t i = 0; i < entries.size(); ++i )
		{
			const auto &r = packed->m_Sheet.m_Sequences[ i ].m_Frames.front().m_Rect;
			const int x = static_cast<int>( std::lround( r[ 0 ] * packed->m_Atlas.m_nWidth ) );
			const int y = static_cast<int>( std::lround( r[ 1 ] * packed->m_Atlas.m_nHeight ) );
			const auto &picture = entries[ i ].m_Image;
			const double flYScale = static_cast<double>( nWidth ) / nHeight;
			const auto szRect = QStringLiteral( "%1 %2 %3 %4 %5" ).arg( x ).arg( y * flYScale, 0, 'g', 12 ).arg( picture.m_nWidth ).arg( picture.m_nHeight * flYScale, 0, 'g', 12 ).arg( nWidth ).toStdString();
			if ( !entries[ i ].m_Path.empty() )
			{
				Set( document, entries[ i ].m_Path, "sprite", szRect );
				continue;
			}
			auto entry = CNode::MakeBlock( UniqueName( *pTarget, "Sprite" ) );
			entry.AppendChild( CNode::MakePair( "sprite", szRect ) );
			entry.AppendChild( CNode::MakePair( "spritesize", QStringLiteral( "0.5 0 %1 %2" ).arg( picture.m_nWidth / 8.0 ).arg( picture.m_nHeight / 8.0 ).toStdString() ) );
			// Adding art must not change the existing distribution silently.
			entry.AppendChild( CNode::MakePair( "amount", "0" ) );
			entry.AppendChild( CNode::MakePair( "detailOrientation", "2" ) );
			m_Selected = group;
			m_Selected.push_back( pTarget->GetChildren().size() );
			pTarget->AppendChild( std::move( entry ) );
		}
	};
	m_pDocument->Edit( tr( "Pack detail pictures" ), pfnPack, [ this, szBefore, szMaterial ]( bool bRedo ) { SetSpriteMaterial( bRedo ? szMaterial : szBefore ); } );
	emit PackedTexture( szMaterial, bytes );
	return true;
}
//-----------------------------------------------------------------------------
// Purpose: Gather picture inputs and name the new map sprite material before packing.
//-----------------------------------------------------------------------------
void CDetailEditor::ChoosePictures()
{
	auto group = m_Selected;
	if ( group.size() == 4 )
	{
		group.pop_back();
	}
	if ( group.size() != 3 )
	{
		QMessageBox::information( this, tr( "Pack pictures" ), tr( "Select a group or an entry first." ) );
		return;
	}
	const auto files = QFileDialog::getOpenFileNames( this, tr( "Pack detail pictures" ), {}, tr( "Pictures (*.png *.tga *.jpg *.bmp *.vtf);;All files (*)" ) );
	if ( files.isEmpty() )
	{
		return;
	}
	QString szMaterial = QInputDialog::getText( this, tr( "New sprite material" ), tr( "Material path, without materials/ or .vmt:" ), QLineEdit::Normal, QStringLiteral( "detail/customdetails" ) );
	szMaterial = QString::fromStdString( xv::vfs::NormalizePath( szMaterial.trimmed().toStdString() ) );
	if ( szMaterial.isEmpty() || szMaterial.contains( QStringLiteral( ".." ) ) || szMaterial.contains( u'"' ) || szMaterial.startsWith( u'/' ) || szMaterial.contains( u':' ) )
	{
		return;
	}
	std::map<std::string, xv::texture::CImage> pictures;
	for ( const auto &szFile : files )
	{
		QString szError;
		auto image = ReadImageFile( szFile, &szError );
		if ( !image )
		{
			QMessageBox::warning( this, tr( "Cannot read picture" ), szError );
			return;
		}
		pictures.emplace( szFile.toStdString(), std::move( *image ) );
	}
	QString szError;
	if ( !PackPictures( group, pictures, szMaterial, &szError ) )
	{
		QMessageBox::warning( this, tr( "Cannot pack pictures" ), szError );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The right side: the sprite sheet or the 3D preview.
//-----------------------------------------------------------------------------
void CDetailEditor::ShowPreview( bool b3d )
{
	m_pRight->setCurrentWidget( b3d ? static_cast<QWidget *>( m_pPreview ) : m_pSheet );
	( b3d ? m_p3dButton : m_pSheetButton )->setChecked( true );
}

//-----------------------------------------------------------------------------
// Purpose: The material the 3D preview's ground is drawn in, with its picture below the list.
//-----------------------------------------------------------------------------
void CDetailEditor::SetGround( const QString &szMaterial )
{
	QString szName = szMaterial.trimmed();
	if ( szName.startsWith( QStringLiteral( "materials/" ), Qt::CaseInsensitive ) )
	{
		szName.remove( 0, 10 );
	}
	if ( szName.endsWith( QStringLiteral( ".vmt" ), Qt::CaseInsensitive ) )
	{
		szName.chop( 4 );
	}
	if ( m_pGround->currentText() != szName )
	{
		m_pGround->setEditText( szName );
	}
	m_pPreview->SetGround( szName );
	const QImage picture = m_pPreview->GetGroundPicture();
	m_pGroundPicture->setPixmap( picture.isNull() ? QPixmap{} : QPixmap::fromImage( picture.scaled( 96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation ) ) );
	m_pGroundPicture->setText( picture.isNull() && !szName.isEmpty() ? tr( "can't read it" ) : QString{} );
}

//-----------------------------------------------------------------------------
// Purpose: The ground list: the materials that use the picked type. The first is used until one
//          is picked by hand.
//-----------------------------------------------------------------------------
void CDetailEditor::FillGrounds()
{
	const NodePath_t typePath = m_Selected.size() >= 2 ? NodePath_t{ m_Selected[ 0 ], m_Selected[ 1 ] } : NodePath_t{};
	const auto *pType = typePath.empty() ? nullptr : Node( m_pDocument->GetDocument(), typePath );
	const QString szType = pType ? QString::fromStdString( pType->GetKey() ).toLower() : QString{};
	if ( szType == m_szGroundType )
	{
		return;
	}
	m_szGroundType = szType;
	std::vector<Choice_t> choices;
	if ( m_Context.m_Uses )
	{
		if ( const auto it = m_Context.m_Uses->find( xv::vfs::NormalizePath( szType.toStdString() ) ); it != m_Context.m_Uses->end() )
		{
			for ( const std::string &szFile : it->second )
			{
				QString szName = QString::fromStdString( szFile );
				szName.remove( 0, szName.startsWith( QStringLiteral( "materials/" ) ) ? 10 : 0 );
				szName.chop( szName.endsWith( QStringLiteral( ".vmt" ) ) ? 4 : 0 );
				choices.push_back( { szName, tr( "uses %1" ).arg( szType ) } );
			}
		}
	}
	const QString szKept = m_pGround->currentText();
	auto *pModel = qobject_cast<QStandardItemModel *>( m_pGround->model() );
	pModel->removeRows( 0, pModel->rowCount() );
	for ( const Choice_t &choice : choices )
	{
		auto *pMeaning = new QStandardItem( choice.m_szMeaning );
		pMeaning->setEnabled( false );
		pModel->appendRow( { new QStandardItem( choice.m_szValue ), pMeaning } );
	}
	m_pGround->setCurrentIndex( -1 );
	m_pGround->setEditText( szKept );
	if ( !m_bGroundChosen )
	{
		SetGround( choices.empty() ? QString{} : choices.front().m_szValue );
	}
}

//-----------------------------------------------------------------------------
// Purpose: An entry's sprite as cut from the sheet, with its name and size; or its model.
//-----------------------------------------------------------------------------
QWidget *CDetailEditor::EntryPicture( const NodePath_t &path )
{
	auto *pBox = new QWidget;
	pBox->setObjectName( FieldName( path, QStringLiteral( "picture" ) ) );
	auto *pBoxLayout = new QVBoxLayout( pBox );
	pBoxLayout->setContentsMargins( 0, 0, 0, 0 );
	auto *pPicture = new QLabel;
	pPicture->setFixedSize( 128, 128 );
	pPicture->setAlignment( Qt::AlignCenter );
	pPicture->setFrameShape( QFrame::StyledPanel );
	auto *pCaption = new QLabel;
	pCaption->setAlignment( Qt::AlignCenter );
	pCaption->setWordWrap( true );
	pCaption->setMaximumWidth( 128 );
	pBoxLayout->addWidget( pPicture );
	pBoxLayout->addWidget( pCaption );
	const auto *pEntry = Node( m_pDocument->GetDocument(), path );
	if ( !pEntry )
	{
		return pBox;
	}
	QString szCaption = QStringLiteral( "<b>%1</b>" ).arg( QString::fromStdString( pEntry->GetKey() ).toHtmlEscaped() );
	if ( const auto *pModel = pEntry->Find( "model" ); pModel && pModel->IsPair() )
	{
		pPicture->setText( tr( "model" ) );
		pCaption->setText( szCaption + QStringLiteral( "<br>" ) + QFileInfo{ QString::fromStdString( pModel->GetValue() ) }.fileName().toHtmlEscaped() );
		pCaption->setToolTip( QString::fromStdString( pModel->GetValue() ) );
		return pBox;
	}
	if ( const auto *pSize = pEntry->Find( "spritesize" ); pSize && pSize->IsPair() )
	{
		if ( const auto size = Numbers( pSize->GetValue() ); size.size() == 4 )
		{
			szCaption += tr( " · %1 × %2 units" ).arg( size[ 2 ] ).arg( size[ 3 ] );
		}
	}
	pCaption->setText( szCaption );
	// Over the checkerboard, so see-through parts show as they are.
	const auto *pSprite = pEntry->Find( "sprite" );
	const auto rect = pSprite && pSprite->IsPair() ? Numbers( pSprite->GetValue() ) : std::vector<double>{};
	const QImage &sheet = m_pSheet->GetImage();
	const QRect area = rect.size() == 5 && rect[ 4 ] > 0 && !sheet.isNull()
		? QRectF( rect[ 0 ] / rect[ 4 ] * sheet.width(), rect[ 1 ] / rect[ 4 ] * sheet.height(), rect[ 2 ] / rect[ 4 ] * sheet.width(), rect[ 3 ] / rect[ 4 ] * sheet.height() ).toAlignedRect().intersected( sheet.rect() )
		: QRect{};
	if ( area.isEmpty() )
	{
		pPicture->setText( sheet.isNull() ? tr( "no sheet" ) : tr( "no sprite" ) );
		return pBox;
	}
	const QImage cut = sheet.copy( area ).scaled( 124, 124, Qt::KeepAspectRatio, Qt::SmoothTransformation );
	QPixmap shown( cut.size() );
	QPainter painter( &shown );
	for ( int y = 0; y < shown.height(); y += 8 )
	{
		for ( int x = 0; x < shown.width(); x += 8 )
		{
			painter.fillRect( x, y, 8, 8, ( x / 8 + y / 8 ) % 2 ? QColor( 200, 200, 200 ) : QColor( 240, 240, 240 ) );
		}
	}
	painter.drawImage( 0, 0, cut );
	painter.end();
	pPicture->setPixmap( shown );
	return pBox;
}

//-----------------------------------------------------------------------------
// Purpose: A card's right-click: copy and paste, add, rename, delete.
//-----------------------------------------------------------------------------
void CDetailEditor::ShowCardMenu( const NodePath_t &path, const QPoint &globalAt )
{
	const auto *pNode = Node( m_pDocument->GetDocument(), path );
	if ( !pNode )
	{
		return;
	}
	const QString szName = QString::fromStdString( pNode->GetKey() );
	QMenu menu( this );
	menu.addAction( tr( "Copy \"%1\"" ).arg( szName ), this, [ this, path ] {
		if ( const auto *pCopied = Node( m_pDocument->GetDocument(), path ) )
		{
			CopyNodes( { *pCopied } );
		}
	} );
	menu.addAction( tr( "Paste into \"%1\"" ).arg( szName ), this, [ this, path ] {
		const auto nodes = PastedNodes();
		if ( nodes.empty() )
		{
			return;
		}
		m_pDocument->Edit( tr( "Paste" ), [ & ]( auto &document ) {
			if ( auto *pBlock = Node( document, path ); pBlock && pBlock->IsBlock() )
			{
				for ( const CNode &node : nodes )
				{
					pBlock->AppendChild( node );
				}
			}
		} );
	} )->setEnabled( CanPasteNodes() );
	menu.addSeparator();
	QMenu *pAdd = menu.addMenu( tr( "Add" ) );
	if ( path.size() == 2 )
	{
		pAdd->addAction( tr( "Group" ), this, [ this, path ] { AddBlock( path, false ); } );
	}
	if ( path.size() == 3 )
	{
		pAdd->addAction( tr( "Sprite" ), this, [ this, path ] { AddBlock( path, false ); } );
		pAdd->addAction( tr( "Model" ), this, [ this, path ] { AddBlock( path, true ); } );
	}
	std::vector<PickerItem_t> fields;
	for ( const auto &key : m_Schema.GetKeys() )
	{
		if ( key.m_nDepth != static_cast<int>( path.size() ) || pNode->Find( key.m_szKey ) )
		{
			continue;
		}
		fields.push_back( { QString::fromStdString( CInterfaceMode::Instance().IsSimple() ? key.m_szLabel : key.m_szKey ), QString::fromStdString( key.m_szUnits ),
			[ this, path, key ] { SetField( path, QString::fromStdString( key.m_szKey ), QString::fromStdString( key.m_szDefault ) ); } } );
	}
	AddPicker( pAdd, tr( "Field" ), std::move( fields ), tr( "Every field is set" ) );
	menu.addSeparator();
	menu.addAction( tr( "Rename…" ), this, [ this, path ] { RenameNode( path ); } );
	menu.addSeparator();
	menu.addAction( tr( "Delete \"%1\"" ).arg( szName ), this, [ this, path ] { RemoveNode( path ); } );
	menu.exec( globalAt );
}
