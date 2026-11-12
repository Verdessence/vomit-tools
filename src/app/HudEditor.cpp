//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See HudEditor.h.
//
//===========================================================================//
#include "HudEditor.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTreeWidget>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

#include "FileDialogs.h"
#include "vfs/Vfs.h"

namespace
{

// The screen sizes offered, the usual ones of each shape.
struct Resolution_t
{
	int m_nWide;
	int m_nTall;
};
constexpr Resolution_t RESOLUTIONS[] = {
	{ 640, 480 },
	{ 1024, 768 },
	{ 1280, 1024 },
	{ 1280, 720 },
	{ 1600, 900 },
	{ 1920, 1080 },
	{ 2560, 1440 },
	{ 3840, 2160 },
	{ 1680, 1050 },
	{ 1920, 1200 },
	{ 2560, 1080 },
	{ 3440, 1440 },
};

//-----------------------------------------------------------------------------
// Purpose: "16:9" for 1920x1080.
//-----------------------------------------------------------------------------
QString AspectName( int nWide, int nTall )
{
	const int nGcd = std::gcd( nWide, nTall );
	int nW = nWide / nGcd;
	int nH = nTall / nGcd;
	if ( nW == 8 && nH == 5 )
	{
		nW = 16;
		nH = 10;
	}
	if ( nW == 43 && nH == 18 )
		return QStringLiteral( "21:9" );
	if ( nW == 64 && nH == 27 )
		return QStringLiteral( "21:9" );
	return QStringLiteral( "%1:%2" ).arg( nW ).arg( nH );
}

//-----------------------------------------------------------------------------
// Purpose: Qt's colour for a HUD one.
//-----------------------------------------------------------------------------
QColor ToQColor( const xv::hud::Color_t &color )
{
	return QColor( color.m_nR, color.m_nG, color.m_nB, color.m_nA );
}

// One edit of the text, undone by putting the old text back.
class CHudTextCommand : public QUndoCommand
{
public:
	CHudTextCommand( CHudEditor &editor, std::string szBefore, std::string szAfter, const QString &szWhat )
		: QUndoCommand( szWhat ),
		  m_Editor( editor ),
		  m_szBefore( std::move( szBefore ) ),
		  m_szAfter( std::move( szAfter ) )
	{
	}
	void undo() override { m_Editor.ApplyText( m_szBefore ); }
	void redo() override { m_Editor.ApplyText( m_szAfter ); }

private:
	CHudEditor &m_Editor;
	std::string m_szBefore;
	std::string m_szAfter;
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CHudCanvas::CHudCanvas( CHudEditor &editor, QWidget *pParent )
	: QWidget( pParent ),
	  m_Editor( editor )
{
	setObjectName( QStringLiteral( "hudCanvas" ) );
	setFocusPolicy( Qt::StrongFocus );
	setMouseTracking( true );
	setMinimumSize( 320, 200 );
}

//-----------------------------------------------------------------------------
// Purpose: The screen's rectangle in the widget: as big as fits, centred.
//-----------------------------------------------------------------------------
QRectF CHudCanvas::GetScreenRect() const
{
	const auto &screen = m_Editor.GetScreen();
	const double flZoom = GetZoom();
	const double flW = screen.m_nWide * flZoom;
	const double flH = screen.m_nTall * flZoom;
	return { ( width() - flW ) / 2.0, ( height() - flH ) / 2.0, flW, flH };
}

//-----------------------------------------------------------------------------
// Purpose: Widget pixels per screen pixel.
//-----------------------------------------------------------------------------
double CHudCanvas::GetZoom() const
{
	const auto &screen = m_Editor.GetScreen();
	const double flMargin = 16.0;
	return std::max( 0.01, std::min( ( width() - flMargin ) / screen.m_nWide, ( height() - flMargin ) / screen.m_nTall ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QPointF CHudCanvas::ToScreen( QPointF at ) const
{
	const QRectF screen = GetScreenRect();
	return ( at - screen.topLeft() ) / GetZoom();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QRectF CHudCanvas::ToWidget( const xv::hud::Rect_t &rect ) const
{
	const QRectF screen = GetScreenRect();
	const double flZoom = GetZoom();
	return { screen.left() + rect.m_nX * flZoom, screen.top() + rect.m_nY * flZoom, rect.m_nWide * flZoom, rect.m_nTall * flZoom };
}

//-----------------------------------------------------------------------------
// Purpose: A screenshot of the game to lay the HUD over.
//-----------------------------------------------------------------------------
void CHudCanvas::SetBackground( const QImage &image )
{
	m_Background = image;
	update();
}

//-----------------------------------------------------------------------------
// Purpose: The screen, then the elements from the lowest zpos up, the selected one marked
//          with what its position is measured from.
//-----------------------------------------------------------------------------
void CHudCanvas::paintEvent( QPaintEvent *pEvent )
{
	(void)pEvent;
	QPainter painter( this );
	painter.fillRect( rect(), palette().window() );
	const QRectF screen = GetScreenRect();
	if ( !m_Background.isNull() )
	{
		painter.drawImage( screen, m_Background );
	}
	else
	{
		QLinearGradient sky( screen.topLeft(), screen.bottomLeft() );
		sky.setColorAt( 0.0, QColor( 70, 78, 90 ) );
		sky.setColorAt( 0.55, QColor( 120, 116, 104 ) );
		sky.setColorAt( 0.56, QColor( 82, 74, 62 ) );
		sky.setColorAt( 1.0, QColor( 48, 44, 38 ) );
		painter.fillRect( screen, sky );
	}
	painter.setClipRect( screen );

	const auto &elements = m_Editor.GetElements();
	std::vector<int> order( elements.size() );
	std::iota( order.begin(), order.end(), 0 );
	std::ranges::stable_sort( order, [ & ]( int a, int b ) { return elements[ a ].m_nZ < elements[ b ].m_nZ; } );
	const bool bHidden = m_Editor.IsShowingHidden();
	QFont font = painter.font();
	font.setPixelSize( std::max( 8, static_cast<int>( 11 * std::max( 0.6, GetZoom() * m_Editor.GetScreen().GetScale() ) ) ) );
	painter.setFont( font );
	for ( int i : order )
	{
		const auto &element = elements[ i ];
		if ( !element.m_bVisible && !bHidden )
			continue;
		xv::hud::Rect_t rect = element.m_Rect;
		if ( i == m_Editor.GetSelected() && m_DragFrom )
		{
			rect.m_nX += m_DragOffset.x();
			rect.m_nY += m_DragOffset.y();
		}
		const QRectF box = ToWidget( rect );
		QColor fill = element.m_BgColor ? ToQColor( *element.m_BgColor ) : QColor( 0, 0, 0, 90 );
		if ( !element.m_bVisible )
			fill.setAlpha( fill.alpha() / 3 );
		// Panels the size of the screen (scoreboards, overlays) would hide the rest: outlines only.
		const double flCover = static_cast<double>( rect.m_nWide ) * rect.m_nTall / ( static_cast<double>( m_Editor.GetScreen().m_nWide ) * m_Editor.GetScreen().m_nTall );
		if ( flCover < 0.4 || i == m_Editor.GetSelected() )
			painter.fillRect( box, fill );
		QPen pen( element.m_bVisible ? QColor( 255, 255, 255, 110 ) : QColor( 255, 255, 255, 50 ) );
		pen.setStyle( element.m_bVisible ? Qt::SolidLine : Qt::DotLine );
		painter.setPen( pen );
		painter.drawRect( box.adjusted( 0.5, 0.5, -0.5, -0.5 ) );
		const QColor text = element.m_FgColor ? ToQColor( *element.m_FgColor ) : QColor( 255, 220, 160 );
		painter.setPen( text );
		const QString szText = element.m_szLabel.empty() ? QString::fromStdString( element.m_szName ) : QString::fromStdString( element.m_szLabel );
		painter.drawText( box.adjusted( 3, 2, -3, -2 ), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, szText );
	}

	if ( const int nSelected = m_Editor.GetSelected(); nSelected >= 0 && nSelected < static_cast<int>( elements.size() ) )
	{
		xv::hud::Rect_t rect = elements[ nSelected ].m_Rect;
		if ( m_DragFrom )
		{
			rect.m_nX += m_DragOffset.x();
			rect.m_nY += m_DragOffset.y();
		}
		const QRectF box = ToWidget( rect );
		painter.setPen( QPen( QColor( 255, 200, 0 ), 2 ) );
		painter.drawRect( box );
		// A line from the point the position is measured from, so "c-50" and "r40" read at a glance.
		const xv::kv1::CNode *pRoot = xv::hud::LayoutRoot( m_Editor.GetDocument() );
		if ( pRoot && elements[ nSelected ].m_nIndex < pRoot->GetChildren().size() )
		{
			const auto &node = pRoot->Child( elements[ nSelected ].m_nIndex );
			const auto x = xv::hud::ParseCoord( xv::hud::GetKey( node, "xpos" ).value_or( "0" ) );
			const auto y = xv::hud::ParseCoord( xv::hud::GetKey( node, "ypos" ).value_or( "0" ) );
			auto from = [ & ]( xv::hud::Anchor_t nAnchor, double flStart, double flLength ) {
				return nAnchor == xv::hud::Anchor_t::ANCHOR_CENTER ? flStart + flLength / 2 : nAnchor == xv::hud::Anchor_t::ANCHOR_END ? flStart + flLength
																																	   : flStart;
			};
			painter.setPen( QPen( QColor( 255, 200, 0, 160 ), 1, Qt::DashLine ) );
			const double flAnchorX = from( x.m_nAnchor, screen.left(), screen.width() );
			const double flAnchorY = from( y.m_nAnchor, screen.top(), screen.height() );
			painter.drawLine( QPointF( flAnchorX, box.top() ), QPointF( box.left(), box.top() ) );
			painter.drawLine( QPointF( box.left(), flAnchorY ), QPointF( box.left(), box.top() ) );
		}
	}
	painter.setClipping( false );
	painter.setPen( QColor( 255, 255, 255, 60 ) );
	painter.drawRect( screen.adjusted( -0.5, -0.5, 0.5, 0.5 ) );
}

//-----------------------------------------------------------------------------
// Purpose: A click selects the topmost element under it and starts dragging it.
//-----------------------------------------------------------------------------
void CHudCanvas::mousePressEvent( QMouseEvent *pEvent )
{
	setFocus( Qt::MouseFocusReason );
	if ( pEvent->button() != Qt::LeftButton )
		return;
	const QPointF at = ToScreen( pEvent->position() );
	const int nElement = m_Editor.ElementAt( at );
	m_Editor.Select( nElement );
	if ( nElement >= 0 )
	{
		m_DragFrom = at;
		m_DragOffset = {};
	}
}

//-----------------------------------------------------------------------------
// Purpose: Dragging shows the element at its new place; the text changes on release.
//-----------------------------------------------------------------------------
void CHudCanvas::mouseMoveEvent( QMouseEvent *pEvent )
{
	if ( !m_DragFrom )
	{
		const int nUnder = m_Editor.ElementAt( ToScreen( pEvent->position() ) );
		setCursor( nUnder >= 0 ? Qt::OpenHandCursor : Qt::ArrowCursor );
		return;
	}
	const QPointF delta = ToScreen( pEvent->position() ) - *m_DragFrom;
	m_DragOffset = delta.toPoint();
	setCursor( Qt::ClosedHandCursor );
	update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudCanvas::mouseReleaseEvent( QMouseEvent *pEvent )
{
	if ( pEvent->button() != Qt::LeftButton || !m_DragFrom )
		return;
	const int nSelected = m_Editor.GetSelected();
	const QPoint offset = m_DragOffset;
	m_DragFrom.reset();
	m_DragOffset = {};
	setCursor( Qt::OpenHandCursor );
	if ( nSelected >= 0 && !offset.isNull() )
	{
		const auto &rect = m_Editor.GetElements()[ nSelected ].m_Rect;
		m_Editor.MoveElement( nSelected, QPoint( rect.m_nX, rect.m_nY ) + offset );
	}
	update();
}

//-----------------------------------------------------------------------------
// Purpose: The arrows nudge the selected element a base unit (Shift: ten).
//-----------------------------------------------------------------------------
void CHudCanvas::keyPressEvent( QKeyEvent *pEvent )
{
	const int nSelected = m_Editor.GetSelected();
	QPoint step;
	switch ( pEvent->key() )
	{
		case Qt::Key_Left:
			step = { -1, 0 };
			break;
		case Qt::Key_Right:
			step = { 1, 0 };
			break;
		case Qt::Key_Up:
			step = { 0, -1 };
			break;
		case Qt::Key_Down:
			step = { 0, 1 };
			break;
		default:
			QWidget::keyPressEvent( pEvent );
			return;
	}
	if ( nSelected < 0 )
		return;
	const double flUnit = m_Editor.GetScreen().GetScale() * ( pEvent->modifiers().testFlag( Qt::ShiftModifier ) ? 10.0 : 1.0 );
	const auto &rect = m_Editor.GetElements()[ nSelected ].m_Rect;
	m_Editor.MoveElement( nSelected, QPoint( rect.m_nX + static_cast<int>( std::lround( step.x() * flUnit ) ), rect.m_nY + static_cast<int>( std::lround( step.y() * flUnit ) ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: The list on the left, the screen in the middle, the keys on the right.
//-----------------------------------------------------------------------------
CHudEditor::CHudEditor( const QString &szName, const std::string &szText, const std::filesystem::path &disk, bool bFromGame, QWidget *pParent )
	: QWidget( pParent ),
	  m_szName( szName ),
	  m_Disk( disk ),
	  m_bFromGame( bFromGame ),
	  m_Document( xv::kv1::Parse( szText ).m_Document )
{
	setObjectName( QStringLiteral( "hudEditor" ) );
	m_pUndo = new QUndoStack( this );
	connect( m_pUndo, &QUndoStack::cleanChanged, this, [ this ]( bool bClean ) { emit ModifiedChanged( !bClean ); } );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 4, 4, 4, 4 );

	auto *pBar = new QHBoxLayout;
	pBar->addWidget( new QLabel( tr( "Screen:" ) ) );
	m_pResolution = new QComboBox;
	m_pResolution->setObjectName( QStringLiteral( "hudResolution" ) );
	for ( const auto &resolution : RESOLUTIONS )
		m_pResolution->addItem( QStringLiteral( "%1 × %2 (%3)" ).arg( resolution.m_nWide ).arg( resolution.m_nTall ).arg( AspectName( resolution.m_nWide, resolution.m_nTall ) ), QSize( resolution.m_nWide, resolution.m_nTall ) );
	m_pResolution->setCurrentIndex( m_pResolution->findData( QSize( 1920, 1080 ) ) );
	m_pResolution->setToolTip( tr( "The screen the HUD is laid out on. VGUI scales it by the screen's height, so wider screens move what is anchored right or to the middle" ) );
	connect( m_pResolution, &QComboBox::currentIndexChanged, this, [ this ] {
		const QSize size = m_pResolution->currentData().toSize();
		SetScreen( { size.width(), size.height(), m_Screen.m_bProportional } );
	} );
	pBar->addWidget( m_pResolution );
	m_pHidden = new QCheckBox( tr( "Show hidden" ) );
	m_pHidden->setObjectName( QStringLiteral( "hudShowHidden" ) );
	m_pHidden->setToolTip( tr( "Also draw the elements whose visible is 0, dimmed" ) );
	connect( m_pHidden, &QCheckBox::toggled, this, [ this ] { m_pCanvas->update(); } );
	pBar->addWidget( m_pHidden );
	auto *pBackground = new QPushButton( tr( "Background…" ) );
	pBackground->setToolTip( tr( "Lay the HUD over a screenshot of the game" ) );
	connect( pBackground, &QPushButton::clicked, this, [ this ] {
		const QString szFile = AskOpenFile( this, tr( "Background Picture" ), {}, tr( "Pictures (*.png *.jpg *.jpeg *.tga *.bmp);;All files (*)" ) );
		if ( !szFile.isEmpty() )
			m_pCanvas->SetBackground( QImage( szFile ) );
	} );
	pBar->addWidget( pBackground );
	pBar->addStretch( 1 );
	m_pStatus = new QLabel;
	m_pStatus->setObjectName( QStringLiteral( "hudStatus" ) );
	pBar->addWidget( m_pStatus );
	pLayout->addLayout( pBar );

	auto *pSplitter = new QSplitter( Qt::Horizontal );
	auto *pLeft = new QWidget;
	auto *pLeftLayout = new QVBoxLayout( pLeft );
	pLeftLayout->setContentsMargins( 0, 0, 0, 0 );
	m_pFilter = new QLineEdit;
	m_pFilter->setPlaceholderText( tr( "Filter elements" ) );
	m_pFilter->setClearButtonEnabled( true );
	connect( m_pFilter, &QLineEdit::textChanged, this, &CHudEditor::FillList );
	pLeftLayout->addWidget( m_pFilter );
	m_pList = new QTreeWidget;
	m_pList->setObjectName( QStringLiteral( "hudElements" ) );
	m_pList->setHeaderLabels( { tr( "Element" ), tr( "Control" ) } );
	m_pList->setRootIsDecorated( false );
	connect( m_pList, &QTreeWidget::currentItemChanged, this, [ this ]( QTreeWidgetItem *pItem ) {
		if ( !m_bFilling && pItem )
			Select( pItem->data( 0, Qt::UserRole ).toInt() );
	} );
	pLeftLayout->addWidget( m_pList );
	pSplitter->addWidget( pLeft );

	m_pCanvas = new CHudCanvas( *this );
	pSplitter->addWidget( m_pCanvas );

	m_pSheet = new QTableWidget( 0, 2 );
	m_pSheet->setObjectName( QStringLiteral( "hudKeys" ) );
	m_pSheet->setHorizontalHeaderLabels( { tr( "Key" ), tr( "Value" ) } );
	m_pSheet->horizontalHeader()->setStretchLastSection( true );
	m_pSheet->verticalHeader()->hide();
	connect( m_pSheet, &QTableWidget::itemChanged, this, [ this ]( QTableWidgetItem *pItem ) {
		if ( m_bFilling || pItem->column() != 1 || m_nSelected < 0 )
			return;
		SetValue( m_nSelected, m_pSheet->item( pItem->row(), 0 )->text(), pItem->text() );
	} );
	pSplitter->addWidget( m_pSheet );
	pSplitter->setStretchFactor( 0, 0 );
	pSplitter->setStretchFactor( 1, 1 );
	pSplitter->setStretchFactor( 2, 0 );
	pSplitter->setSizes( { 220, 900, 280 } );
	pLayout->addWidget( pSplitter, 1 );

	Rebuild();
}

//-----------------------------------------------------------------------------
// Purpose: The game's scheme, for the colours elements name.
//-----------------------------------------------------------------------------
void CHudEditor::SetScheme( xv::hud::CScheme scheme )
{
	m_Scheme = std::move( scheme );
	Rebuild();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudEditor::SetScreen( const xv::hud::Screen_t &screen )
{
	m_Screen = screen;
	const int nIndex = m_pResolution->findData( QSize( screen.m_nWide, screen.m_nTall ) );
	if ( nIndex >= 0 && nIndex != m_pResolution->currentIndex() )
	{
		m_pResolution->setCurrentIndex( nIndex ); // comes back here through the signal
		return;
	}
	Rebuild();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string CHudEditor::GetText() const
{
	return xv::kv1::Write( m_Document );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CHudEditor::IsShowingHidden() const
{
	return m_pHidden->isChecked();
}

//-----------------------------------------------------------------------------
// Purpose: Selects in the list, the sheet and on the screen together.
//-----------------------------------------------------------------------------
void CHudEditor::Select( int nElement )
{
	if ( nElement < -1 || nElement >= static_cast<int>( m_Elements.size() ) )
		nElement = -1;
	m_nSelected = nElement;
	m_bFilling = true;
	m_pList->setCurrentItem( nullptr );
	for ( int i = 0; i < m_pList->topLevelItemCount(); i++ )
	{
		if ( m_pList->topLevelItem( i )->data( 0, Qt::UserRole ).toInt() == nElement )
		{
			m_pList->setCurrentItem( m_pList->topLevelItem( i ) );
			break;
		}
	}
	m_bFilling = false;
	FillSheet();
	m_pCanvas->update();
}

//-----------------------------------------------------------------------------
// Purpose: The topmost (highest zpos, then latest in the file) element containing at.
//-----------------------------------------------------------------------------
int CHudEditor::ElementAt( QPointF at ) const
{
	int nBest = -1;
	for ( int i = 0; i < static_cast<int>( m_Elements.size() ); i++ )
	{
		const auto &element = m_Elements[ i ];
		if ( !element.m_bVisible && !IsShowingHidden() )
			continue;
		const auto &rect = element.m_Rect;
		if ( at.x() < rect.m_nX || at.y() < rect.m_nY || at.x() >= rect.m_nX + rect.m_nWide || at.y() >= rect.m_nY + rect.m_nTall )
			continue;
		if ( nBest < 0 || element.m_nZ >= m_Elements[ nBest ].m_nZ )
			nBest = i;
	}
	return nBest;
}

//-----------------------------------------------------------------------------
// Purpose: Changes the last applicable pair of that key, or adds one at the block's end.
//-----------------------------------------------------------------------------
void CHudEditor::SetValue( int nElement, const QString &szKey, const QString &szValue )
{
	if ( nElement < 0 || nElement >= static_cast<int>( m_Elements.size() ) )
		return;
	xv::kv1::CDocument document = m_Document;
	xv::kv1::CNode *pRoot = xv::hud::LayoutRoot( document );
	if ( !pRoot )
		return;
	xv::kv1::CNode &block = pRoot->Child( m_Elements[ nElement ].m_nIndex );
	const std::string szK = szKey.toStdString();
	const std::string szV = szValue.toStdString();
	xv::kv1::CNode *pPair = nullptr;
	for ( std::size_t i = 0; i < block.GetChildren().size(); i++ )
	{
		xv::kv1::CNode &child = block.Child( i );
		if ( child.IsPair() && xv::kv1::EqualsIgnoreCase( child.GetKey(), szK ) && !child.GetCondition() )
			pPair = &child;
	}
	if ( pPair )
	{
		if ( pPair->GetValue() == szV || !pPair->SetValue( szV ) )
			return;
	}
	else
	{
		block.AppendChild( xv::kv1::CNode::MakePair( szK, szV ) );
	}
	const std::string szText = xv::kv1::Write( document );
	Edit( szText, tr( "Set %1" ).arg( szKey ) );
}

//-----------------------------------------------------------------------------
// Purpose: Rewrites xpos and ypos as one step.
//-----------------------------------------------------------------------------
void CHudEditor::MoveElement( int nElement, QPoint at )
{
	if ( nElement < 0 || nElement >= static_cast<int>( m_Elements.size() ) )
		return;
	xv::kv1::CDocument document = m_Document;
	xv::kv1::CNode *pRoot = xv::hud::LayoutRoot( document );
	if ( !pRoot )
		return;
	xv::kv1::CNode &block = pRoot->Child( m_Elements[ nElement ].m_nIndex );
	const auto moved = xv::hud::MoveTo( block, at.x(), at.y(), m_Screen );
	auto set = [ & ]( const char *pszKey, const std::string &szValue ) {
		xv::kv1::CNode *pPair = nullptr;
		for ( std::size_t i = 0; i < block.GetChildren().size(); i++ )
		{
			if ( block.Child( i ).IsPair() && xv::kv1::EqualsIgnoreCase( block.Child( i ).GetKey(), pszKey ) && !block.Child( i ).GetCondition() )
				pPair = &block.Child( i );
		}
		if ( pPair )
			pPair->SetValue( szValue );
		else
			block.AppendChild( xv::kv1::CNode::MakePair( pszKey, szValue ) );
	};
	set( "xpos", moved.m_szX );
	set( "ypos", moved.m_szY );
	const std::string szText = xv::kv1::Write( document );
	Edit( szText, tr( "Move %1" ).arg( QString::fromStdString( m_Elements[ nElement ].m_szName ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: One undo step from the current text to szText.
//-----------------------------------------------------------------------------
void CHudEditor::Edit( const std::string &szText, const QString &szWhat )
{
	const std::string szBefore = GetText();
	if ( szBefore == szText )
		return;
	m_pUndo->push( new CHudTextCommand( *this, szBefore, szText, szWhat ) );
}

//-----------------------------------------------------------------------------
// Purpose: Reads the text again, keeping the selection on the same block.
//-----------------------------------------------------------------------------
void CHudEditor::ApplyText( const std::string &szText )
{
	const std::optional<std::size_t> selectedBlock = m_nSelected >= 0 ? std::optional<std::size_t>( m_Elements[ m_nSelected ].m_nIndex ) : std::nullopt;
	m_Document = xv::kv1::Parse( szText ).m_Document;
	Rebuild();
	if ( selectedBlock )
	{
		for ( int i = 0; i < static_cast<int>( m_Elements.size() ); i++ )
		{
			if ( m_Elements[ i ].m_nIndex == *selectedBlock )
			{
				Select( i );
				break;
			}
		}
	}
	emit Changed();
}

//-----------------------------------------------------------------------------
// Purpose: The elements laid out again, and everything showing them refilled.
//-----------------------------------------------------------------------------
void CHudEditor::Rebuild()
{
	m_Elements = xv::hud::LayOut( m_Document, m_Screen, m_Scheme.GetCount() ? &m_Scheme : nullptr );
	if ( m_nSelected >= static_cast<int>( m_Elements.size() ) )
		m_nSelected = -1;
	const auto nHidden = std::ranges::count_if( m_Elements, []( const auto &element ) { return !element.m_bVisible; } );
	m_pStatus->setText( tr( "%1 elements (%2 hidden), scale %3" ).arg( m_Elements.size() ).arg( nHidden ).arg( m_Screen.GetScale(), 0, 'f', 3 ) );
	FillList();
	FillSheet();
	m_pCanvas->update();
}

//-----------------------------------------------------------------------------
// Purpose: The elements matching the filter, by name or control.
//-----------------------------------------------------------------------------
void CHudEditor::FillList()
{
	m_bFilling = true;
	m_pList->clear();
	const QString szFilter = m_pFilter->text().trimmed();
	for ( int i = 0; i < static_cast<int>( m_Elements.size() ); i++ )
	{
		const QString szName = QString::fromStdString( m_Elements[ i ].m_szName );
		const QString szControl = QString::fromStdString( m_Elements[ i ].m_szControl );
		if ( !szFilter.isEmpty() && !szName.contains( szFilter, Qt::CaseInsensitive ) && !szControl.contains( szFilter, Qt::CaseInsensitive ) )
			continue;
		auto *pItem = new QTreeWidgetItem( m_pList, { szName, szControl } );
		pItem->setData( 0, Qt::UserRole, i );
		if ( !m_Elements[ i ].m_bVisible )
			pItem->setForeground( 0, palette().placeholderText() );
		if ( i == m_nSelected )
			m_pList->setCurrentItem( pItem );
	}
	m_bFilling = false;
}

//-----------------------------------------------------------------------------
// Purpose: The selected element's pairs, values editable. Conditional ones show their
//          condition and stay read-only here.
//-----------------------------------------------------------------------------
void CHudEditor::FillSheet()
{
	m_bFilling = true;
	m_pSheet->setRowCount( 0 );
	const xv::kv1::CNode *pRoot = xv::hud::LayoutRoot( m_Document );
	if ( m_nSelected >= 0 && pRoot )
	{
		const xv::kv1::CNode &block = pRoot->Child( m_Elements[ m_nSelected ].m_nIndex );
		for ( const xv::kv1::CNode &child : block.GetChildren() )
		{
			if ( !child.IsPair() )
				continue;
			const int nRow = m_pSheet->rowCount();
			m_pSheet->insertRow( nRow );
			QString szKey = QString::fromStdString( child.GetKey() );
			auto *pKey = new QTableWidgetItem( szKey );
			pKey->setFlags( pKey->flags() & ~Qt::ItemIsEditable );
			auto *pValue = new QTableWidgetItem( QString::fromStdString( child.GetValue() ) );
			if ( const auto condition = child.GetCondition() )
			{
				pKey->setText( szKey + QStringLiteral( " " ) + QString::fromUtf8( condition->data(), static_cast<qsizetype>( condition->size() ) ) );
				pValue->setFlags( pValue->flags() & ~Qt::ItemIsEditable );
				pValue->setForeground( palette().placeholderText() );
			}
			m_pSheet->setItem( nRow, 0, pKey );
			m_pSheet->setItem( nRow, 1, pValue );
		}
	}
	m_pSheet->resizeColumnToContents( 0 );
	m_bFilling = false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CHudEditor::IsModified() const
{
	return !m_pUndo->isClean();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CHudEditor::GetTabTitle() const
{
	const QString szFile = m_Disk.empty() ? m_szName.section( u'/', -1 ) : QString::fromStdU16String( m_Disk.filename().u16string() );
	return ( IsModified() ? QStringLiteral( "*" ) : QString{} ) + szFile;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CHudEditor::GetSessionEntry() const
{
	if ( m_bFromGame )
		return QStringLiteral( "hud\tgame\t%1" ).arg( m_szName );
	if ( m_Disk.empty() )
		return {};
	return QStringLiteral( "hud\tdisk\t%1" ).arg( QString::fromStdU16String( m_Disk.u16string() ) );
}

//-----------------------------------------------------------------------------
// Purpose: Save, drop the changes, or keep the tab.
//-----------------------------------------------------------------------------
bool CHudEditor::ConfirmClose()
{
	if ( !IsModified() )
		return true;
	const auto nAnswer = QMessageBox::question( this, tr( "Unsaved HUD Layout" ), tr( "Save the changes to %1?" ).arg( GetTabTitle().remove( 0, 1 ) ), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel );
	if ( nAnswer == QMessageBox::Save )
		return Save();
	return nAnswer == QMessageBox::Discard;
}

//-----------------------------------------------------------------------------
// Purpose: Back to its file; a game file (in a VPK, maybe) asks where instead.
//-----------------------------------------------------------------------------
bool CHudEditor::Save()
{
	if ( m_Disk.empty() || m_bFromGame )
		return SaveAs();
	return WriteTo( m_Disk );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CHudEditor::SaveAs()
{
	const QString szStart = m_Disk.empty() ? m_szName.section( u'/', -1 ) : QString::fromStdU16String( m_Disk.u16string() );
	const QString szFile = AskSaveFile( this, tr( "Save HUD Layout" ), szStart, tr( "Resource files (*.res);;All files (*)" ) );
	if ( szFile.isEmpty() )
		return false;
	return WriteTo( std::filesystem::path( szFile.toStdU16String() ) );
}

//-----------------------------------------------------------------------------
// Purpose: Writes the text as it is; the tab then belongs to that file.
//-----------------------------------------------------------------------------
bool CHudEditor::WriteTo( const std::filesystem::path &file )
{
	const std::string szText = GetText();
	std::ofstream stream( file, std::ios::binary | std::ios::trunc );
	if ( !stream || !stream.write( szText.data(), static_cast<std::streamsize>( szText.size() ) ) )
	{
		QMessageBox::warning( this, tr( "Cannot save" ), tr( "Could not write %1." ).arg( QString::fromStdU16String( file.u16string() ) ) );
		return false;
	}
	m_Disk = file;
	m_bFromGame = false;
	m_pUndo->setClean();
	emit ModifiedChanged( false );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: A .res laid out as VGUI panels, not a scheme or a menu.
//-----------------------------------------------------------------------------
bool CHudEditor::IsHudLayout( const std::filesystem::path &file, std::string_view szText )
{
	if ( !xv::kv1::EqualsIgnoreCase( xv::vfs::ToUtf8( file.extension() ), ".res" ) )
		return false;
	return xv::hud::IsLayout( xv::kv1::Parse( szText ).m_Document );
}
