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

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

#include "FileDialogs.h"
#include "ImageFiles.h"
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

//-----------------------------------------------------------------------------
// Purpose: A small square of a colour, for the scheme's lists.
//-----------------------------------------------------------------------------
QIcon Swatch( const xv::hud::Color_t &color )
{
	QPixmap pixmap( 14, 14 );
	pixmap.fill( Qt::transparent );
	QPainter painter( &pixmap );
	// Over a checker, so the alpha shows.
	painter.fillRect( 0, 0, 14, 14, Qt::white );
	painter.fillRect( 0, 0, 7, 7, QColor( 190, 190, 190 ) );
	painter.fillRect( 7, 7, 7, 7, QColor( 190, 190, 190 ) );
	painter.fillRect( 0, 0, 14, 14, ToQColor( color ) );
	painter.setPen( QColor( 0, 0, 0, 120 ) );
	painter.drawRect( 0, 0, 13, 13 );
	return QIcon( pixmap );
}

//-----------------------------------------------------------------------------
// Purpose: The pointer, for the list's Pick column, in one colour (the text's): whole when a
//          click on the screen picks the element, faint and struck through when it goes
//          through it.
//-----------------------------------------------------------------------------
QIcon PickIcon( bool bPickable, const QColor &color )
{
	QPixmap pixmap( 16, 16 );
	pixmap.fill( Qt::transparent );
	QPainter painter( &pixmap );
	painter.setRenderHint( QPainter::Antialiasing, true );
	QPolygonF arrow;
	arrow << QPointF( 4.0, 1.5 ) << QPointF( 4.0, 12.5 ) << QPointF( 6.7, 9.9 ) << QPointF( 8.5, 14.2 ) << QPointF( 10.3, 13.4 ) << QPointF( 8.5, 9.2 ) << QPointF( 12.2, 9.2 );
	painter.setOpacity( bPickable ? 1.0 : 0.35 );
	painter.setPen( Qt::NoPen );
	painter.setBrush( color );
	painter.drawPolygon( arrow );
	if ( !bPickable )
	{
		painter.setOpacity( 1.0 );
		painter.setPen( QPen( color, 1.6, Qt::SolidLine, Qt::RoundCap ) );
		painter.drawLine( QPointF( 2.5, 13.5 ), QPointF( 13.5, 2.5 ) );
	}
	return QIcon( pixmap );
}

//-----------------------------------------------------------------------------
// Purpose: An eye, for the list's column of what is drawn on the screen, in the same one
//          colour: open, or faint and struck through for an element left out.
//-----------------------------------------------------------------------------
QIcon EyeIcon( bool bShown, const QColor &color )
{
	QPixmap pixmap( 16, 16 );
	pixmap.fill( Qt::transparent );
	QPainter painter( &pixmap );
	painter.setRenderHint( QPainter::Antialiasing, true );
	painter.setOpacity( bShown ? 1.0 : 0.35 );
	QPainterPath lids;
	lids.moveTo( 1.5, 8.0 );
	lids.quadTo( 8.0, 1.0, 14.5, 8.0 );
	lids.quadTo( 8.0, 15.0, 1.5, 8.0 );
	painter.setPen( QPen( color, 1.3 ) );
	painter.setBrush( Qt::NoBrush );
	painter.drawPath( lids );
	painter.setPen( Qt::NoPen );
	painter.setBrush( color );
	painter.drawEllipse( QPointF( 8.0, 8.0 ), 2.3, 2.3 );
	if ( !bShown )
	{
		painter.setOpacity( 1.0 );
		painter.setPen( QPen( color, 1.6, Qt::SolidLine, Qt::RoundCap ) );
		painter.drawLine( QPointF( 2.5, 13.5 ), QPointF( 13.5, 2.5 ) );
	}
	return QIcon( pixmap );
}

//-----------------------------------------------------------------------------
// Purpose: A game path with its "..", "." and backslashes worked out ("vgui/../hud/x" is
//          "hud/x"), lower-case as the game's files are looked up.
//-----------------------------------------------------------------------------
std::string CleanGamePath( std::string_view szPath )
{
	std::vector<std::string> parts;
	std::string szPart;
	const auto push = [ & ] {
		if ( szPart == ".." )
		{
			if ( !parts.empty() )
				parts.pop_back();
		}
		else if ( !szPart.empty() && szPart != "." )
		{
			parts.push_back( szPart );
		}
		szPart.clear();
	};
	for ( const char ch : szPath )
	{
		if ( ch == '/' || ch == '\\' )
			push();
		else
			szPart += static_cast<char>( std::tolower( static_cast<unsigned char>( ch ) ) );
	}
	push();
	std::string szOut;
	for ( const std::string &sz : parts )
		szOut += ( szOut.empty() ? "" : "/" ) + sz;
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose: A text file's bytes as UTF-8: the game's language files are UTF-16 with a mark.
//-----------------------------------------------------------------------------
std::string TextOf( const std::vector<std::byte> &bytes )
{
	const auto *pData = reinterpret_cast<const char *>( bytes.data() );
	if ( bytes.size() >= 2 && static_cast<unsigned char>( pData[ 0 ] ) == 0xFF && static_cast<unsigned char>( pData[ 1 ] ) == 0xFE )
		return QString::fromUtf16( reinterpret_cast<const char16_t *>( pData + 2 ), static_cast<qsizetype>( ( bytes.size() - 2 ) / 2 ) ).toStdString();
	return std::string( pData, bytes.size() );
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
	else if ( const QImage backdrop = m_Editor.GetBackdrop(); !backdrop.isNull() )
	{
		painter.setRenderHint( QPainter::SmoothPixmapTransform, true );
		painter.drawImage( screen, backdrop );
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
	const bool bRender = m_Editor.IsRendering();
	QFont font = painter.font();
	font.setPixelSize( std::max( 8, static_cast<int>( 11 * std::max( 0.6, GetZoom() * m_Editor.GetScreen().GetScale() ) ) ) );
	painter.setFont( font );
	for ( int i : order )
	{
		const auto &element = elements[ i ];
		if ( ( !element.m_bVisible && !bHidden ) || !m_Editor.IsShown( i ) )
			continue;
		xv::hud::Rect_t rect = element.m_Rect;
		if ( i == m_Editor.GetSelected() && m_DragFrom )
		{
			rect.m_nX += m_DragOffset.x();
			rect.m_nY += m_DragOffset.y();
		}
		const QRectF box = ToWidget( rect );
		if ( bRender )
		{
			// As the game draws it; a hidden one fainter. What draws nothing the files tell of
			// (the game's code paints it) keeps a faint outline to be found by, and its name
			// under the pointer.
			painter.save();
			painter.setOpacity( element.m_bVisible ? 1.0 : 0.35 );
			const bool bDrawn = PaintRendered( painter, element, box );
			painter.restore();
			painter.setFont( font );
			if ( !bDrawn || i == m_nHover )
			{
				painter.setPen( QPen( QColor( 255, 255, 255, i == m_nHover ? 150 : 45 ), 1, Qt::DotLine ) );
				painter.drawRect( box.adjusted( 0.5, 0.5, -0.5, -0.5 ) );
			}
			if ( i == m_nHover && i != m_Editor.GetSelected() )
			{
				const QString szName = QString::fromStdString( element.m_szName );
				const QRectF label = QRectF( box.topLeft() + QPointF( 0, -painter.fontMetrics().height() - 2 ), QSizeF( painter.fontMetrics().horizontalAdvance( szName ) + 8, painter.fontMetrics().height() + 2 ) ).translated( 0, box.top() < screen.top() + painter.fontMetrics().height() + 2 ? painter.fontMetrics().height() + 2 : 0 );
				painter.fillRect( label, QColor( 0, 0, 0, 170 ) );
				painter.setPen( QColor( 255, 255, 255 ) );
				painter.drawText( label, Qt::AlignCenter, szName );
			}
			continue;
		}
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
	// The lines a dragged element has caught on, across the whole screen.
	painter.setPen( QPen( QColor( 255, 0, 220 ), 1 ) );
	for ( const int nX : m_GuidesX )
	{
		const double flX = std::floor( screen.left() + nX * GetZoom() ) + 0.5;
		painter.drawLine( QPointF( flX, screen.top() ), QPointF( flX, screen.bottom() ) );
	}
	for ( const int nY : m_GuidesY )
	{
		const double flY = std::floor( screen.top() + nY * GetZoom() ) + 0.5;
		painter.drawLine( QPointF( screen.left(), flY ), QPointF( screen.right(), flY ) );
	}
	painter.setClipping( false );
	painter.setPen( QColor( 255, 255, 255, 60 ) );
	painter.drawRect( screen.adjusted( -0.5, -0.5, 0.5, 0.5 ) );
}

//-----------------------------------------------------------------------------
// Purpose: An element as the game draws it, from what the files say of it: its background
//          (rounded for PaintBackgroundType 2), its image, its label in the scheme's font,
//          and for the well-known panels a sample of what they show (an icon, digits, a
//          caption) where the layout places each.
//-----------------------------------------------------------------------------
bool CHudCanvas::PaintRendered( QPainter &painter, const xv::hud::Element_t &element, const QRectF &box ) const
{
	const xv::hud::CScheme &scheme = m_Editor.GetScheme();
	const double flZoom = GetZoom();
	const double flScale = m_Editor.GetScreen().GetScale();
	bool bDrawn = false;
	if ( box.width() <= 0.0 || box.height() <= 0.0 )
		return false;
	// Half-Life 2's panels are in Counter-Strike's layout too, and never shown there.
	if ( m_Editor.IsCounterStrike() && ( xv::kv1::EqualsIgnoreCase( element.m_szName, "HudSuit" ) || xv::kv1::EqualsIgnoreCase( element.m_szName, "HudAmmoSecondary" ) ) )
		return false;

	// A control's colours it doesn't name itself are the scheme's for its kind:
	// "Button.BgColor", "RichText.TextColor".
	const auto ofControl = [ & ]( std::initializer_list<const char *> suffixes ) -> std::optional<xv::hud::Color_t> {
		for ( const char *pszSuffix : suffixes )
		{
			if ( const auto color = element.m_szControl.empty() ? std::nullopt : scheme.Resolve( element.m_szControl + "." + pszSuffix ) )
				return color;
		}
		return std::nullopt;
	};
	// A panel that says how its background is painted has the scheme's when it names none; a
	// dialog has its frame's.
	std::optional<xv::hud::Color_t> background = element.m_BgColor;
	if ( !background )
		background = ofControl( { "BgColor" } );
	if ( !background && element.m_bDialog )
		background = scheme.Resolve( "Frame.BgColor" );
	if ( !background && element.m_nPaintBackground >= 0 )
		background = scheme.Resolve( "BgColor" );
	// Panels over much of the screen (scoreboards, menus: the game shows them now and then)
	// would bury the rest: no fill unless picked. That is the HUD's panels; a panel file's
	// controls are all part of the one thing it shows.
	const xv::hud::Screen_t &screen = m_Editor.GetScreen();
	const double flCover = static_cast<double>( element.m_Rect.m_nWide ) * element.m_Rect.m_nTall / ( static_cast<double>( screen.m_nWide ) * screen.m_nTall );
	const bool bPicked = &element == &m_Editor.GetElements()[ static_cast<std::size_t>( std::max( m_Editor.GetSelected(), 0 ) ) ] && m_Editor.GetSelected() >= 0;
	const bool bButton = QString::fromStdString( element.m_szControl ).contains( QStringLiteral( "button" ), Qt::CaseInsensitive );
	if ( element.m_bMenu && ( !background || background->m_nA == 0 ) )
	{
		// The game's full-screen menus (team, class, buy) paint a backdrop of their own that
		// no file tells of: a rounded dark panel a little inside the dialog, split under its
		// title. Measured off the game: 20 units in, the split at 71 to 74.
		const xv::hud::Color_t dark = scheme.Resolve( "BgColor" ).value_or( xv::hud::Color_t{ 0, 0, 0, 196 } );
		const double flUnit = flScale * flZoom;
		const QRectF panel = box.adjusted( 20 * flUnit, 20 * flUnit, -20 * flUnit, -20 * flUnit );
		if ( dark.m_nA > 0 && panel.height() > 80 * flUnit )
		{
			QPainterPath shape;
			shape.addRoundedRect( panel, 8 * flUnit, 8 * flUnit );
			QPainterPath split;
			split.addRect( QRectF( panel.left(), box.top() + 71 * flUnit, panel.width(), 3 * flUnit ) );
			painter.setRenderHint( QPainter::Antialiasing, true );
			painter.fillPath( shape.subtracted( split ), ToQColor( dark ) );
			painter.setRenderHint( QPainter::Antialiasing, false );
			bDrawn = true;
		}
	}
	else if ( background && background->m_nA > 0 && ( flCover < 0.2 || bPicked || !element.m_szControl.empty() ) )
	{
		painter.setPen( Qt::NoPen );
		painter.setBrush( ToQColor( *background ) );
		if ( element.m_nPaintBackground == 2 )
		{
			const double flCorner = std::min( { 8.0 * flScale * flZoom, box.width() / 2.0, box.height() / 2.0 } );
			painter.setRenderHint( QPainter::Antialiasing, true );
			painter.drawRoundedRect( box, flCorner, flCorner );
			painter.setRenderHint( QPainter::Antialiasing, false );
		}
		else
		{
			painter.drawRect( box );
		}
		painter.setBrush( Qt::NoBrush );
		bDrawn = true;
	}

	if ( !element.m_szImage.empty() )
	{
		if ( const QImage image = m_Editor.ImageFor( element.m_szImage ); !image.isNull() )
		{
			painter.setRenderHint( QPainter::SmoothPixmapTransform, true );
			painter.drawImage( element.m_bScaleImage ? box : QRectF( box.topLeft(), QSizeF( image.width() * flZoom, image.height() * flZoom ) ), image );
			bDrawn = true;
		}
	}

	// Its own colour. Else, a control: the scheme's for its kind, a label's, as they are (a
	// scheme that blanks them hides the text in the game too). A HUD panel: what
	// hudanimations.txt leaves it at, else the scheme's Panel.FgColor (the game's panels all
	// start from it), FgColor, and for a scheme that blanks those, its orange. The caption
	// and the icon have colours of their own when the layout names them.
	std::optional<xv::hud::Color_t> text = element.m_FgColor;
	if ( !element.m_szControl.empty() )
	{
		if ( !text )
			text = ofControl( { "TextColor", "FgColor" } );
		if ( !text )
			text = scheme.Resolve( "Label.TextColor" );
		if ( !text )
			text = scheme.Resolve( "FgColor" );
	}
	else
	{
		if ( !text || text->m_nA == 0 )
			text = m_Editor.AnimatedColor( element.m_szName, "FgColor" );
		for ( const char *pszName : { "Panel.FgColor", "FgColor", "Orange" } )
		{
			if ( !text || text->m_nA == 0 )
				text = scheme.Resolve( pszName );
		}
		if ( !text || text->m_nA == 0 )
			text = xv::hud::Color_t{ 255, 176, 0, 255 };
	}
	const QColor foreground = ToQColor( text.value_or( xv::hud::Color_t{ 255, 176, 0, 255 } ) );
	if ( bButton && foreground.alpha() > 0 )
	{
		// A button's outline, in its text's colour.
		painter.setPen( QPen( foreground, std::max( 1.0, flScale * flZoom ) ) );
		painter.drawRect( box.adjusted( 0.5, 0.5, -0.5, -0.5 ) );
		bDrawn = true;
	}
	std::optional<xv::hud::Color_t> caption = element.m_TextColor;
	if ( !caption )
		caption = m_Editor.AnimatedColor( element.m_szName, "TextColor" );
	const QColor captionColor = caption && caption->m_nA > 0 ? ToQColor( *caption ) : foreground;
	const QColor iconColor = element.m_IconColor && element.m_IconColor->m_nA > 0 ? ToQColor( *element.m_IconColor ) : foreground;
	painter.save();
	painter.setClipRect( box, Qt::IntersectClip );
	// Text from a point down and right of the element's corner, in a scheme font.
	const auto textAt = [ & ]( std::pair<int, int> at, const QString &szText, std::string_view szFont, const QColor &color ) {
		bool bAdditive = false;
		const QFont textFont = m_Editor.FontFor( szFont, flZoom, &bAdditive );
		painter.setFont( textFont );
		painter.setPen( color );
		painter.setCompositionMode( bAdditive ? QPainter::CompositionMode_Plus : QPainter::CompositionMode_SourceOver );
		painter.drawText( box.topLeft() + QPointF( at.first * flZoom, at.second * flZoom + QFontMetricsF( textFont ).ascent() ), szText );
		painter.setCompositionMode( QPainter::CompositionMode_SourceOver );
		bDrawn = true;
	};
	const auto iconAt = [ & ]( std::pair<int, int> at, std::string_view szName ) {
		const xv::hud::Icon_t *pIcon = m_Editor.IconFor( szName );
		if ( !pIcon )
			return;
		if ( !pIcon->m_szFont.empty() )
		{
			textAt( at, QString::fromStdString( pIcon->m_szCharacter ), pIcon->m_szFont, iconColor );
		}
		else if ( const QImage sheet = m_Editor.ImageFor( "../" + pIcon->m_szFile ); !sheet.isNull() && pIcon->m_Rect.m_nWide > 0 && pIcon->m_Rect.m_nTall > 0 )
		{
			// A piece of a sprite sheet, at the 640-wide HUD's size scaled as the layout is.
			const QRectF from( pIcon->m_Rect.m_nX, pIcon->m_Rect.m_nY, pIcon->m_Rect.m_nWide, pIcon->m_Rect.m_nTall );
			painter.drawImage( QRectF( box.topLeft() + QPointF( at.first * flZoom, at.second * flZoom ), from.size() * flScale * flZoom ), sheet, from );
			bDrawn = true;
		}
	};
	if ( xv::kv1::EqualsIgnoreCase( element.m_szName, "HudCrosshair" ) )
	{
		// Where the crosshair is: a small cross at its middle.
		const double flArm = 5.0 * flScale * flZoom;
		painter.setPen( QPen( foreground, std::max( 1.0, flScale * flZoom * 0.6 ) ) );
		painter.drawLine( box.center() - QPointF( flArm, 0 ), box.center() + QPointF( flArm, 0 ) );
		painter.drawLine( box.center() - QPointF( 0, flArm ), box.center() + QPointF( 0, flArm ) );
		bDrawn = true;
	}
	if ( const xv::hud::Sample_t *pSample = xv::hud::SampleFor( element.m_szName ) )
	{
		if ( element.m_Bar && element.m_Bar->m_nWide > 0 && element.m_Bar->m_nTall > 0 && !pSample->m_szDigits2.empty() )
		{
			// The line between what is loaded and what is carried.
			painter.fillRect( QRectF( box.left() + element.m_Bar->m_nX * flZoom, box.top() + element.m_Bar->m_nY * flZoom, element.m_Bar->m_nWide * flZoom, element.m_Bar->m_nTall * flZoom ), foreground );
			bDrawn = true;
		}
		if ( element.m_Icon && !pSample->m_szIcon.empty() )
			iconAt( *element.m_Icon, pSample->m_szIcon );
		const std::string_view szNumbers = element.m_szNumberFont.empty() ? std::string_view{ "HudNumbers" } : std::string_view{ element.m_szNumberFont };
		const std::string_view szCaption = element.m_szTextFont.empty() ? std::string_view{ "Default" } : std::string_view{ element.m_szTextFont };
		if ( element.m_Digit && !pSample->m_szDigits.empty() )
			textAt( *element.m_Digit, QString::fromUtf8( pSample->m_szDigits.data(), static_cast<qsizetype>( pSample->m_szDigits.size() ) ), szNumbers, foreground );
		if ( element.m_Icon2 && !pSample->m_szIcon2.empty() )
			iconAt( *element.m_Icon2, pSample->m_szIcon2 );
		if ( element.m_Digit2 && !pSample->m_szDigits2.empty() )
			textAt( *element.m_Digit2, QString::fromUtf8( pSample->m_szDigits2.data(), static_cast<qsizetype>( pSample->m_szDigits2.size() ) ), element.m_Icon2 || !element.m_szNumberFont.empty() ? szNumbers : std::string_view{ "HudNumbersSmall" }, foreground );
		if ( element.m_Text && !pSample->m_szLabel.empty() )
			textAt( *element.m_Text, QString::fromUtf8( pSample->m_szLabel.data(), static_cast<qsizetype>( pSample->m_szLabel.size() ) ), szCaption, captionColor );
	}
	if ( !element.m_szLabel.empty() )
	{
		// A label: its text where textAlignment puts it in the panel.
		bool bAdditive = false;
		const QFont textFont = m_Editor.FontFor( element.m_szFont.empty() ? std::string_view{ "Default" } : std::string_view{ element.m_szFont }, flZoom, &bAdditive );
		const std::string &szAlign = element.m_szAlignment;
		Qt::Alignment nAlign = Qt::AlignVCenter | Qt::AlignLeft;
		if ( szAlign.find( "north" ) != std::string::npos )
			nAlign = ( nAlign & ~Qt::AlignVertical_Mask ) | Qt::AlignTop;
		else if ( szAlign.find( "south" ) != std::string::npos )
			nAlign = ( nAlign & ~Qt::AlignVertical_Mask ) | Qt::AlignBottom;
		if ( szAlign.find( "east" ) != std::string::npos )
			nAlign = ( nAlign & ~Qt::AlignHorizontal_Mask ) | Qt::AlignRight;
		else if ( szAlign.find( "west" ) == std::string::npos && ( szAlign == "center" || szAlign == "north" || szAlign == "south" ) )
			nAlign = ( nAlign & ~Qt::AlignHorizontal_Mask ) | Qt::AlignHCenter;
		painter.setFont( textFont );
		painter.setPen( foreground );
		painter.setCompositionMode( bAdditive ? QPainter::CompositionMode_Plus : QPainter::CompositionMode_SourceOver );
		// "&" marks the key that presses a button; a button's text starts a little in.
		QString szLabel = m_Editor.TextFor( element.m_szLabel );
		szLabel.replace( QStringLiteral( "&&" ), QStringLiteral( "\x01" ) ).remove( u'&' ).replace( QStringLiteral( "\x01" ), QStringLiteral( "&" ) );
		const double flInset = bButton ? 6.0 * flScale * flZoom : 0.0;
		painter.drawText( box.adjusted( flInset, 0, -flInset, 0 ), static_cast<int>( nAlign ) | Qt::TextSingleLine, szLabel );
		painter.setCompositionMode( QPainter::CompositionMode_SourceOver );
		bDrawn = true;
	}
	painter.restore();
	return bDrawn;
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
		if ( nUnder != m_nHover )
		{
			m_nHover = nUnder;
			update();
		}
		return;
	}
	const QPointF delta = ToScreen( pEvent->position() ) - *m_DragFrom;
	m_DragOffset = delta.toPoint();
	m_GuidesX.clear();
	m_GuidesY.clear();
	// Catching on what is around, from six pixels of the widget away; Alt lets it go free.
	if ( m_Editor.IsSnapping() && !pEvent->modifiers().testFlag( Qt::AltModifier ) )
		m_DragOffset = m_Editor.SnapOffset( m_Editor.GetSelected(), m_DragOffset, std::max( 1, static_cast<int>( std::lround( 6.0 / GetZoom() ) ) ), &m_GuidesX, &m_GuidesY );
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
	m_GuidesX.clear();
	m_GuidesY.clear();
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
// Purpose:
//-----------------------------------------------------------------------------
void CHudCanvas::leaveEvent( QEvent *pEvent )
{
	QWidget::leaveEvent( pEvent );
	if ( m_nHover >= 0 )
	{
		m_nHover = -1;
		update();
	}
}

//-----------------------------------------------------------------------------
// Purpose: The element under the pointer can be kept from being picked here (a panel over
//          the whole screen is in the way of everything under it), and all let back.
//-----------------------------------------------------------------------------
void CHudCanvas::contextMenuEvent( QContextMenuEvent *pEvent )
{
	const int nElement = m_Editor.ElementAt( ToScreen( pEvent->pos() ) );
	QMenu menu( this );
	if ( nElement >= 0 )
	{
		const QString szName = QString::fromStdString( m_Editor.GetElements()[ nElement ].m_szName );
		menu.addAction( tr( "Don't Pick %1 on the Screen" ).arg( szName ), this, [ this, nElement ] { m_Editor.SetPickable( nElement, false ); } );
	}
	QAction *pAll = menu.addAction( tr( "Pick Every Element Again" ), this, [ this ] {
		for ( int i = 0; i < static_cast<int>( m_Editor.GetElements().size() ); i++ )
			m_Editor.SetPickable( i, true );
	} );
	bool bAny = false;
	for ( int i = 0; i < static_cast<int>( m_Editor.GetElements().size() ); i++ )
		bAny = bAny || !m_Editor.IsPickable( i );
	pAll->setEnabled( bAny );
	menu.exec( pEvent->globalPos() );
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
	m_pRender = new QCheckBox( tr( "Render elements" ) );
	m_pRender->setObjectName( QStringLiteral( "hudRender" ) );
	m_pRender->setToolTip( tr( "Draw the elements as the game does, as far as the files say: backgrounds, labels in the scheme's fonts, images, and a sample of what the health, armour, ammunition, timer and money panels show. Off: boxes with their names" ) );
	m_pRender->setChecked( QSettings{}.value( QStringLiteral( "hudRender" ), true ).toBool() );
	connect( m_pRender, &QCheckBox::toggled, this, [ this ]( bool bOn ) {
		QSettings{}.setValue( QStringLiteral( "hudRender" ), bOn );
		m_pCanvas->update();
	} );
	pBar->addWidget( m_pRender );
	m_pSnap = new QCheckBox( tr( "Snap" ) );
	m_pSnap->setObjectName( QStringLiteral( "hudSnap" ) );
	m_pSnap->setToolTip( tr( "A dragged element catches on the screen's edges and middle and on the other elements' edges and middles, and the line it caught on is drawn. Hold Alt while dragging to place it freely" ) );
	m_pSnap->setChecked( QSettings{}.value( QStringLiteral( "hudSnap" ), true ).toBool() );
	connect( m_pSnap, &QCheckBox::toggled, this, []( bool bOn ) { QSettings{}.setValue( QStringLiteral( "hudSnap" ), bOn ); } );
	pBar->addWidget( m_pSnap );
	m_bGameMenu = xv::hud::IsGameMenu( m_Document );
	m_pInGame = new QCheckBox( tr( "In game" ) );
	m_pInGame->setObjectName( QStringLiteral( "hudInGame" ) );
	m_pInGame->setToolTip( tr( "The main menu as it is with a map loaded: the items marked OnlyInGame show too" ) );
	m_pInGame->setVisible( m_bGameMenu );
	connect( m_pInGame, &QCheckBox::toggled, this, [ this ] { Rebuild(); } );
	pBar->addWidget( m_pInGame );
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
	m_pList->setHeaderLabels( { tr( "Element" ), tr( "Control" ), QString{}, QString{} } );
	// The name takes what room there is; the eye and the pointer stay in sight at the right.
	m_pList->header()->setStretchLastSection( false );
	m_pList->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
	m_pList->header()->setSectionResizeMode( 1, QHeaderView::Interactive );
	m_pList->header()->setSectionResizeMode( 2, QHeaderView::Fixed );
	m_pList->header()->setSectionResizeMode( 3, QHeaderView::Fixed );
	m_pList->header()->setMinimumSectionSize( 22 );
	m_pList->setColumnWidth( 1, 80 );
	m_pList->setColumnWidth( 2, 24 );
	m_pList->setColumnWidth( 3, 36 ); // room for the list's scroll bar, which some styles lay over it
	m_pList->headerItem()->setIcon( 2, EyeIcon( true, palette().color( QPalette::Text ) ) );
	m_pList->headerItem()->setToolTip( 2, tr( "Whether the element is drawn on the screen. Click the eye to leave it out of the preview (the file isn't changed)" ) );
	m_pList->headerItem()->setIcon( 3, PickIcon( true, palette().color( QPalette::Text ) ) );
	m_pList->headerItem()->setToolTip( 3, tr( "Whether a click on the screen picks the element. Click the pointer to strike it out: clicks then go through the element to what is under it. It can still be picked here" ) );
	m_pList->setRootIsDecorated( false );
	connect( m_pList, &QTreeWidget::itemClicked, this, [ this ]( QTreeWidgetItem *pItem, int nColumn ) {
		if ( m_bFilling )
			return;
		const int nElement = pItem->data( 0, Qt::UserRole ).toInt();
		if ( nColumn == 2 )
			SetShown( nElement, !pItem->data( 2, Qt::UserRole ).toBool() );
		else if ( nColumn == 3 )
			SetPickable( nElement, !pItem->data( 3, Qt::UserRole ).toBool() );
	} );
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
	// The keys over the scheme: what the selected element says, and what its names stand for.
	auto *pRight = new QSplitter( Qt::Vertical );
	pRight->addWidget( m_pSheet );
	auto *pScheme = new QWidget;
	pScheme->setObjectName( QStringLiteral( "hudScheme" ) );
	auto *pSchemeLayout = new QVBoxLayout( pScheme );
	pSchemeLayout->setContentsMargins( 0, 0, 0, 0 );
	auto *pSchemeBar = new QHBoxLayout;
	m_pSchemeFilter = new QLineEdit;
	m_pSchemeFilter->setPlaceholderText( tr( "Filter the scheme" ) );
	m_pSchemeFilter->setClearButtonEnabled( true );
	connect( m_pSchemeFilter, &QLineEdit::textChanged, this, &CHudEditor::FillScheme );
	pSchemeBar->addWidget( m_pSchemeFilter, 1 );
	m_pUsedOnly = new QCheckBox( tr( "Used" ) );
	m_pUsedOnly->setObjectName( QStringLiteral( "hudSchemeUsed" ) );
	m_pUsedOnly->setToolTip( tr( "Only what the selected element uses of the scheme: the names in its keys, and what it is drawn with without saying so. Those are marked in the lists either way" ) );
	connect( m_pUsedOnly, &QCheckBox::toggled, this, &CHudEditor::FillScheme );
	pSchemeBar->addWidget( m_pUsedOnly );
	m_pEditScheme = new QPushButton( tr( "Edit Scheme" ) );
	m_pEditScheme->setObjectName( QStringLiteral( "hudEditScheme" ) );
	connect( m_pEditScheme, &QPushButton::clicked, this, [ this ] { emit EditSchemeRequested( m_szSchemeSource ); } );
	pSchemeBar->addWidget( m_pEditScheme );
	pSchemeLayout->addLayout( pSchemeBar );
	m_pSchemeTabs = new QTabWidget;
	m_pSchemeTabs->setObjectName( QStringLiteral( "hudSchemeTabs" ) );
	m_pSchemeTabs->setDocumentMode( true );
	const auto addTree = [ this ]( const QString &szTab, const QString &szObject, const QStringList &headers, bool bNested ) {
		auto *pTree = new QTreeWidget;
		pTree->setObjectName( szObject );
		pTree->setHeaderLabels( headers );
		pTree->setRootIsDecorated( bNested );
		pTree->setContextMenuPolicy( Qt::CustomContextMenu );
		pTree->setToolTip( tr( "Double-click a name to put it in the key selected above; right-click to copy it" ) );
		connect( pTree, &QTreeWidget::itemDoubleClicked, this, [ this ]( QTreeWidgetItem *pItem ) { ApplySchemeName( pItem ); } );
		connect( pTree, &QTreeWidget::customContextMenuRequested, this, [ this, pTree ]( const QPoint &at ) {
			QTreeWidgetItem *pItem = pTree->itemAt( at );
			if ( !pItem )
				return;
			while ( pItem->parent() )
				pItem = pItem->parent();
			QMenu menu( this );
			menu.addAction( tr( "Copy Name" ), this, [ pItem ] { QApplication::clipboard()->setText( pItem->text( 0 ) ); } );
			menu.addAction( tr( "Copy Value" ), this, [ pItem ] { QApplication::clipboard()->setText( pItem->text( 1 ) ); } );
			menu.exec( pTree->viewport()->mapToGlobal( at ) );
		} );
		m_pSchemeTabs->addTab( pTree, szTab );
		return pTree;
	};
	m_pSchemeColors = addTree( tr( "Colours" ), QStringLiteral( "hudSchemeColors" ), { tr( "Name" ), tr( "Value" ) }, false );
	m_pSchemeSettings = addTree( tr( "Settings" ), QStringLiteral( "hudSchemeSettings" ), { tr( "Name" ), tr( "Value" ) }, false );
	m_pSchemeFonts = addTree( tr( "Fonts" ), QStringLiteral( "hudSchemeFonts" ), { tr( "Name" ), tr( "On this screen" ), tr( "Sample" ) }, true );
	m_pSchemeBorders = addTree( tr( "Borders" ), QStringLiteral( "hudSchemeBorders" ), { tr( "Name" ), tr( "Value" ) }, true );
	// What the game fills into texts, to try values for.
	m_pVariables = new QTableWidget( 0, 2 );
	m_pVariables->setObjectName( QStringLiteral( "hudVariables" ) );
	m_pVariables->setHorizontalHeaderLabels( { tr( "Variable" ), tr( "Try with" ) } );
	m_pVariables->horizontalHeader()->setStretchLastSection( true );
	m_pVariables->verticalHeader()->hide();
	m_pVariables->setToolTip( tr( "The variables in this file's texts, which the game fills in while playing (%mapname%, a language string's %s1). A value typed here is drawn in its place; it isn't saved anywhere" ) );
	connect( m_pVariables, &QTableWidget::itemChanged, this, [ this ]( QTableWidgetItem *pItem ) {
		if ( !m_bFilling && pItem->column() == 1 )
			SetVariable( m_pVariables->item( pItem->row(), 0 )->data( Qt::UserRole ).toString(), pItem->text() );
	} );
	m_pSchemeTabs->addTab( m_pVariables, tr( "Variables" ) );
	pSchemeLayout->addWidget( m_pSchemeTabs, 1 );
	pRight->addWidget( pScheme );
	pRight->setStretchFactor( 0, 1 );
	pRight->setStretchFactor( 1, 1 );
	pSplitter->addWidget( pRight );
	pSplitter->setStretchFactor( 0, 0 );
	pSplitter->setStretchFactor( 1, 1 );
	pSplitter->setStretchFactor( 2, 0 );
	pSplitter->setSizes( { 250, 870, 300 } );
	pLayout->addWidget( pSplitter, 1 );

	for ( const QString &szElement : QSettings{}.value( GetPickKey() ).toStringList() )
		m_Unpickable.insert( szElement.toLower().toStdString() );
	for ( const QString &szElement : QSettings{}.value( QString( GetPickKey() ).replace( QStringLiteral( "hudUnpickable/" ), QStringLiteral( "hudUnshown/" ) ) ).toStringList() )
		m_Unshown.insert( szElement.toLower().toStdString() );
	Rebuild();
	FillScheme();
	FillVariables();
}

//-----------------------------------------------------------------------------
// Purpose: The scheme, for the colours and fonts elements name, and its own panel.
//-----------------------------------------------------------------------------
void CHudEditor::SetScheme( xv::hud::CScheme scheme, const QString &szSource )
{
	m_Scheme = std::move( scheme );
	m_szSchemeSource = szSource;
	Rebuild();
	FillScheme();
}

//-----------------------------------------------------------------------------
// Purpose: The game's files: the scheme's font files go to Qt's fonts (once a run each), the
//          HUD's icons are read, and the rest waits until something is drawn with it.
//-----------------------------------------------------------------------------
void CHudEditor::SetFiles( ReadFile_t pfnRead )
{
	m_pfnRead = std::move( pfnRead );
	m_Icons.clear();
	m_Tokens.reset();
	m_Images.clear();
	m_Animations.clear();
	if ( m_pfnRead )
	{
		if ( const auto bytes = m_pfnRead( "scripts/hudanimations.txt" ) )
			m_Animations = xv::hud::ReadAnimations( TextOf( *bytes ) );
		static std::set<std::string> s_Loaded;
		for ( const std::string &szFile : m_Scheme.GetFontFiles() )
		{
			const std::string szPath = CleanGamePath( szFile );
			if ( s_Loaded.contains( szPath ) )
				continue;
			if ( const auto bytes = m_pfnRead( szPath ) )
			{
				QFontDatabase::addApplicationFontFromData( QByteArray( reinterpret_cast<const char *>( bytes->data() ), static_cast<qsizetype>( bytes->size() ) ) );
				s_Loaded.insert( szPath );
			}
		}
		// The mod's icons over the base game's.
		for ( const char *pszFile : { "scripts/hud_textures.txt", "scripts/mod_textures.txt" } )
		{
			if ( const auto bytes = m_pfnRead( pszFile ) )
			{
				for ( auto &[ szName, icon ] : xv::hud::ReadIcons( xv::kv1::Parse( TextOf( *bytes ) ).m_Document ) )
					m_Icons[ szName ] = std::move( icon );
			}
		}
	}
	FillScheme();
	FillVariables();
	m_pCanvas->update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CHudEditor::IsShown( int nElement ) const
{
	if ( nElement < 0 || nElement >= static_cast<int>( m_Elements.size() ) )
		return false;
	return !m_Unshown.contains( QString::fromStdString( m_Elements[ nElement ].m_szName ).toLower().toStdString() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudEditor::SetShown( int nElement, bool bShown )
{
	if ( nElement < 0 || nElement >= static_cast<int>( m_Elements.size() ) || IsShown( nElement ) == bShown )
		return;
	const std::string szName = QString::fromStdString( m_Elements[ nElement ].m_szName ).toLower().toStdString();
	if ( bShown )
		m_Unshown.erase( szName );
	else
		m_Unshown.insert( szName );
	StorePickable();
	FillList();
	m_pCanvas->update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<xv::hud::Color_t> CHudEditor::AnimatedColor( std::string_view szElement, std::string_view szProperty ) const
{
	const auto lower = []( std::string_view sz ) { return QString::fromUtf8( sz.data(), static_cast<qsizetype>( sz.size() ) ).toLower().toStdString(); };
	const auto panel = m_Animations.find( lower( szElement ) );
	if ( panel == m_Animations.end() )
		return std::nullopt;
	const auto value = panel->second.find( lower( szProperty ) );
	return value != panel->second.end() ? m_Scheme.Resolve( value->second ) : std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudEditor::SetVariable( const QString &szName, const QString &szValue )
{
	const std::string szKey = szName.toLower().toStdString();
	if ( szValue.isEmpty() )
		m_Variables.erase( szKey );
	else
		m_Variables[ szKey ] = szValue.toStdString();
	FillVariables();
	m_pCanvas->update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CHudEditor::GetVariable( const QString &szName ) const
{
	const auto found = m_Variables.find( szName.toLower().toStdString() );
	return found != m_Variables.end() ? QString::fromStdString( found->second ) : QString{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CHudEditor::TextFor( const std::string &szText ) const
{
	return QString::fromStdString( xv::hud::ReplaceVariables( Localize( szText ).toStdString(), m_Variables ) );
}

//-----------------------------------------------------------------------------
// Purpose: The names an element's keys hold, and those it is drawn with unsaid; then, for
//          each that is a setting naming another, that one too, down to a colour.
//-----------------------------------------------------------------------------
std::set<std::string> CHudEditor::SchemeNamesUsedBy( int nElement ) const
{
	std::set<std::string> names;
	const xv::kv1::CNode *pRoot = xv::hud::LayoutRoot( m_Document );
	if ( nElement < 0 || nElement >= static_cast<int>( m_Elements.size() ) || !pRoot )
		return names;
	const auto lower = []( const std::string &sz ) { return QString::fromStdString( sz ).toLower().toStdString(); };
	const xv::hud::Element_t &element = m_Elements[ nElement ];
	for ( const xv::kv1::CNode &child : pRoot->Child( element.m_nIndex ).GetChildren() )
	{
		if ( child.IsPair() )
			names.insert( lower( child.GetValue() ) );
	}
	// Unsaid: the scheme's own colours where it names none, its sample's and label's fonts,
	// and what hudanimations.txt animates it to.
	if ( !element.m_szControl.empty() )
	{
		for ( const char *pszSuffix : { ".bgcolor", ".textcolor", ".fgcolor" } )
			names.insert( lower( element.m_szControl ) + pszSuffix );
		if ( element.m_bMenuItem )
			names.insert( { "mainmenu.textcolor", "mainmenu.menuitemheight", "mainmenu.inset", "menularge" } );
		if ( element.m_bDialog )
			names.insert( element.m_bMenu ? "bgcolor" : "frame.bgcolor" );
	}
	else
	{
		names.insert( "panel.fgcolor" );
	}
	if ( !element.m_FgColor )
		names.insert( "fgcolor" );
	if ( !element.m_BgColor && element.m_nPaintBackground >= 0 )
		names.insert( "bgcolor" );
	if ( !element.m_szLabel.empty() && element.m_szFont.empty() )
		names.insert( "default" );
	if ( const xv::hud::Sample_t *pSample = xv::hud::SampleFor( element.m_szName ) )
	{
		if ( element.m_Digit && element.m_szNumberFont.empty() )
			names.insert( "hudnumbers" );
		if ( element.m_Text && element.m_szTextFont.empty() && !pSample->m_szLabel.empty() )
			names.insert( "default" );
		for ( const std::string_view szIcon : { pSample->m_szIcon, pSample->m_szIcon2 } )
		{
			if ( const xv::hud::Icon_t *pIcon = IconFor( szIcon ); pIcon && !pIcon->m_szFont.empty() )
				names.insert( lower( pIcon->m_szFont ) );
		}
	}
	if ( const auto panel = m_Animations.find( lower( element.m_szName ) ); panel != m_Animations.end() )
	{
		for ( const auto &[ szProperty, szValue ] : panel->second )
			names.insert( lower( szValue ) );
	}
	// A setting that names a colour (or another setting) brings that along.
	std::map<std::string, std::string> values;
	for ( const auto *pList : { &m_Scheme.GetColors(), &m_Scheme.GetSettings() } )
	{
		for ( const xv::hud::SchemeValue_t &value : *pList )
			values[ lower( value.m_szName ) ] = lower( value.m_szValue );
	}
	for ( int nStep = 0; nStep < 8; nStep++ )
	{
		std::set<std::string> more;
		for ( const std::string &szName : names )
		{
			if ( const auto found = values.find( szName ); found != values.end() && !names.contains( found->second ) && values.contains( found->second ) )
				more.insert( found->second );
		}
		if ( more.empty() )
			break;
		names.insert( more.begin(), more.end() );
	}
	return names;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CHudEditor::IsSnapping() const
{
	return m_pSnap->isChecked();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudEditor::SetSnapping( bool bSnap )
{
	m_pSnap->setChecked( bSnap );
}

//-----------------------------------------------------------------------------
// Purpose: Each axis by itself: the lines to catch on are the screen's (and the file's own
//          panel's) two edges and middle, and those of every other element that is drawn; the
//          moved element's own two edges and middle are tried against them.
//-----------------------------------------------------------------------------
QPoint CHudEditor::SnapOffset( int nElement, QPoint offset, int nReach, std::vector<int> *pGuidesX, std::vector<int> *pGuidesY ) const
{
	if ( nElement < 0 || nElement >= static_cast<int>( m_Elements.size() ) )
		return offset;
	std::vector<int> linesX{ 0, m_Screen.m_nWide / 2, m_Screen.m_nWide };
	std::vector<int> linesY{ 0, m_Screen.m_nTall / 2, m_Screen.m_nTall };
	for ( int i = 0; i < static_cast<int>( m_Elements.size() ); i++ )
	{
		const xv::hud::Element_t &other = m_Elements[ i ];
		if ( i == nElement || !IsShown( i ) || ( !other.m_bVisible && !IsShowingHidden() ) || other.m_Rect.m_nWide <= 0 || other.m_Rect.m_nTall <= 0 )
			continue;
		linesX.insert( linesX.end(), { other.m_Rect.m_nX, other.m_Rect.m_nX + other.m_Rect.m_nWide / 2, other.m_Rect.m_nX + other.m_Rect.m_nWide } );
		linesY.insert( linesY.end(), { other.m_Rect.m_nY, other.m_Rect.m_nY + other.m_Rect.m_nTall / 2, other.m_Rect.m_nY + other.m_Rect.m_nTall } );
	}
	const xv::hud::Rect_t &rect = m_Elements[ nElement ].m_Rect;
	const auto snap = [ nReach ]( int nStart, int nLength, int nOffset, const std::vector<int> &lines, std::vector<int> *pGuides ) {
		int nBest = nReach + 1;
		int nPull = 0;
		for ( const int nOwn : { nStart + nOffset, nStart + nLength / 2 + nOffset, nStart + nLength + nOffset } )
		{
			for ( const int nLine : lines )
			{
				if ( std::abs( nLine - nOwn ) < nBest )
				{
					nBest = std::abs( nLine - nOwn );
					nPull = nLine - nOwn;
				}
			}
		}
		if ( nBest > nReach )
			return nOffset;
		nOffset += nPull;
		// Every line it now lies on is shown, not only the one that pulled.
		for ( const int nOwn : { nStart + nOffset, nStart + nLength / 2 + nOffset, nStart + nLength + nOffset } )
		{
			if ( pGuides && std::ranges::find( lines, nOwn ) != lines.end() && std::ranges::find( *pGuides, nOwn ) == pGuides->end() )
				pGuides->push_back( nOwn );
		}
		return nOffset;
	};
	return { snap( rect.m_nX, rect.m_nWide, offset.x(), linesX, pGuidesX ), snap( rect.m_nY, rect.m_nTall, offset.y(), linesY, pGuidesY ) };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CHudEditor::IsInGame() const
{
	return m_pInGame->isChecked();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudEditor::SetInGame( bool bInGame )
{
	m_pInGame->setChecked( bInGame );
}

//-----------------------------------------------------------------------------
// Purpose: The main menu's picture: materials/console/background01, the wide one on a wide
//          screen when the game has it.
//-----------------------------------------------------------------------------
QImage CHudEditor::GetBackdrop() const
{
	if ( !m_bGameMenu )
		return {};
	if ( m_Screen.m_nWide * 10 >= m_Screen.m_nTall * 16 )
	{
		if ( const QImage wide = ImageFor( "../console/background01_widescreen" ); !wide.isNull() )
			return wide;
	}
	return ImageFor( "../console/background01" );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CHudEditor::IsRendering() const
{
	return m_pRender->isChecked();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudEditor::SetRendering( bool bRender )
{
	m_pRender->setChecked( bRender );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CHudEditor::IsPickable( int nElement ) const
{
	if ( nElement < 0 || nElement >= static_cast<int>( m_Elements.size() ) )
		return false;
	return !m_Unpickable.contains( QString::fromStdString( m_Elements[ nElement ].m_szName ).toLower().toStdString() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudEditor::SetPickable( int nElement, bool bPickable )
{
	if ( nElement < 0 || nElement >= static_cast<int>( m_Elements.size() ) || IsPickable( nElement ) == bPickable )
		return;
	const std::string szName = QString::fromStdString( m_Elements[ nElement ].m_szName ).toLower().toStdString();
	if ( bPickable )
		m_Unpickable.erase( szName );
	else
		m_Unpickable.insert( szName );
	StorePickable();
	FillList();
	m_pCanvas->update();
}

//-----------------------------------------------------------------------------
// Purpose: Where this file's unpickable elements are kept: by its name alone, so a HUD's
//          copy of HudLayout.res and the game's go by the same list.
//-----------------------------------------------------------------------------
QString CHudEditor::GetPickKey() const
{
	const QString szFile = m_Disk.empty() ? m_szName.section( u'/', -1 ) : QString::fromStdU16String( m_Disk.filename().u16string() );
	return QStringLiteral( "hudUnpickable/" ) + szFile.toLower();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudEditor::StorePickable()
{
	QStringList names;
	for ( const std::string &szName : m_Unpickable )
		names.append( QString::fromStdString( szName ) );
	if ( names.isEmpty() )
		QSettings{}.remove( GetPickKey() );
	else
		QSettings{}.setValue( GetPickKey(), names );
	QStringList unshown;
	for ( const std::string &szName : m_Unshown )
		unshown.append( QString::fromStdString( szName ) );
	const QString szKey = QString( GetPickKey() ).replace( QStringLiteral( "hudUnpickable/" ), QStringLiteral( "hudUnshown/" ) );
	if ( unshown.isEmpty() )
		QSettings{}.remove( szKey );
	else
		QSettings{}.setValue( szKey, unshown );
}

//-----------------------------------------------------------------------------
// Purpose: The scheme's face for the font on this screen, sized as the game on this system
//          sizes it from the scheme's "tall" (Qt's size is the letters' square).
//-----------------------------------------------------------------------------
QFont CHudEditor::FontFor( std::string_view szName, double flZoom, bool *pAdditive ) const
{
	QFont font = this->font();
	const std::optional<xv::hud::Font_t> scheme = m_Scheme.ResolveFont( szName, m_Screen );
	const double flTall = std::max( 4.0, ( scheme && scheme->m_nPixels > 0 ? scheme->m_nPixels : 12.0 * m_Screen.GetScale() ) * flZoom );
	if ( scheme )
	{
		font.setFamily( QString::fromStdString( scheme->m_szFace ) );
		if ( scheme->m_nWeight >= 100 )
			font.setWeight( static_cast<QFont::Weight>( std::clamp( scheme->m_nWeight, 100, 900 ) ) );
		font.setItalic( scheme->m_bItalic );
		font.setStyleStrategy( scheme->m_bAntialias ? QFont::PreferAntialias : QFont::PreferDefault );
		if ( pAdditive )
			*pAdditive = scheme->m_bAdditive;
	}
#if defined( _WIN32 )
	// Windows' game asks for a line that tall.
	font.setPixelSize( std::max( 1, static_cast<int>( std::lround( flTall ) ) ) );
	const double flLine = QFontMetricsF( font ).height();
	if ( flLine > 0.0 )
		font.setPixelSize( std::max( 1, static_cast<int>( std::lround( flTall * flTall / flLine ) ) ) );
#else
	// Linux's game draws letters four fifths of "tall", whatever the face's own line: measured
	// off the game at 2560x1440 (Liberation Sans at 45 has 36 pixel letters, JetBrains Mono at
	// 14 has 11.2).
	font.setPixelSize( std::max( 1, static_cast<int>( std::lround( flTall * 0.8 ) ) ) );
#endif
	return font;
}

//-----------------------------------------------------------------------------
// Purpose: "image" names a material under materials/vgui (it may step out with ".."): its
//          $basetexture, or a texture of that name when there is no material.
//-----------------------------------------------------------------------------
QImage CHudEditor::ImageFor( const std::string &szImage ) const
{
	const std::string szName = CleanGamePath( "materials/vgui/" + szImage );
	if ( const auto found = m_Images.find( szName ); found != m_Images.end() )
		return found->second;
	QImage image;
	if ( m_pfnRead )
	{
		std::string szTexture = szName;
		if ( const auto material = m_pfnRead( szName + ".vmt" ) )
		{
			const xv::kv1::CDocument document = xv::kv1::Parse( TextOf( *material ) ).m_Document;
			for ( const xv::kv1::CNode &root : document.m_Roots )
			{
				if ( const auto szBase = root.IsBlock() ? xv::hud::GetKey( root, "$basetexture" ) : std::nullopt )
					szTexture = CleanGamePath( "materials/" + *szBase );
			}
		}
		if ( szTexture.ends_with( ".vtf" ) )
			szTexture.resize( szTexture.size() - 4 );
		if ( const auto texture = m_pfnRead( szTexture + ".vtf" ) )
		{
			if ( const auto decoded = DecodeImageFile( QStringLiteral( "x.vtf" ), *texture ) )
				image = ToQImage( *decoded );
		}
	}
	m_Images[ szName ] = image;
	return image;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const xv::hud::Icon_t *CHudEditor::IconFor( std::string_view szName ) const
{
	const auto found = m_Icons.find( QString::fromUtf8( szName.data(), static_cast<qsizetype>( szName.size() ) ).toLower().toStdString() );
	return found != m_Icons.end() ? &found->second : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: The game's English text for a "#Token": its own language file over the engine's.
//          They are read the first time a token is asked for.
//-----------------------------------------------------------------------------
QString CHudEditor::Localize( const std::string &szText ) const
{
	if ( !szText.starts_with( '#' ) )
		return QString::fromStdString( szText );
	if ( !m_Tokens )
	{
		m_Tokens.emplace();
		for ( const char *pszFile : { "resource/gameui_english.txt", "resource/valve_english.txt", "resource/hl2_english.txt", "resource/cstrike_english.txt", "resource/tf_english.txt", "resource/modevents_english.txt" } )
		{
			if ( const auto bytes = m_pfnRead ? m_pfnRead( pszFile ) : std::nullopt )
			{
				for ( auto &[ szToken, szValue ] : xv::hud::ReadTokens( xv::kv1::Parse( TextOf( *bytes ) ).m_Document ) )
					( *m_Tokens )[ szToken ] = std::move( szValue );
			}
		}
	}
	const auto found = m_Tokens->find( QString::fromStdString( szText.substr( 1 ) ).toLower().toStdString() );
	return QString::fromStdString( found != m_Tokens->end() ? found->second : szText );
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
	FillScheme();
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
	FillScheme();
	m_pCanvas->update();
}

//-----------------------------------------------------------------------------
// Purpose: The topmost (highest zpos, then latest in the file) element containing at, of
//          those that can be picked on the screen.
//-----------------------------------------------------------------------------
int CHudEditor::ElementAt( QPointF at ) const
{
	int nBest = -1;
	for ( int i = 0; i < static_cast<int>( m_Elements.size() ); i++ )
	{
		const auto &element = m_Elements[ i ];
		if ( ( !element.m_bVisible && !IsShowingHidden() ) || !IsPickable( i ) || !IsShown( i ) )
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
	// A menu's items are placed by the menu; a panel file's controls are in its dialog.
	if ( m_Elements[ nElement ].m_bMenuItem )
		return;
	xv::kv1::CNode &block = pRoot->Child( m_Elements[ nElement ].m_nIndex );
	const xv::hud::Rect_t *pParent = nElement > 0 && m_Elements.front().m_bDialog ? &m_Elements.front().m_Rect : nullptr;
	const auto moved = xv::hud::MoveTo( block, at.x(), at.y(), m_Screen, pParent );
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
	const xv::hud::CScheme *pScheme = m_Scheme.GetCount() ? &m_Scheme : nullptr;
	m_Elements = m_bGameMenu ? xv::hud::LayOutGameMenu( m_Document, m_Screen, pScheme, m_pInGame->isChecked() ) : xv::hud::LayOut( m_Document, m_Screen, pScheme );
	m_bCounterStrike = std::ranges::any_of( m_Elements, []( const xv::hud::Element_t &element ) { return xv::kv1::EqualsIgnoreCase( element.m_szName, "HudArmor" ); } );
	if ( m_nSelected >= static_cast<int>( m_Elements.size() ) )
		m_nSelected = -1;
	const auto nHidden = std::ranges::count_if( m_Elements, []( const auto &element ) { return !element.m_bVisible; } );
	m_pStatus->setText( tr( "%1 elements (%2 hidden), scale %3" ).arg( m_Elements.size() ).arg( nHidden ).arg( m_Screen.GetScale(), 0, 'f', 3 ) );
	FillList();
	FillSheet();
	if ( m_pVariables )
		FillVariables();
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
		const QColor ink = palette().color( QPalette::Text );
		pItem->setData( 2, Qt::UserRole, IsShown( i ) );
		pItem->setIcon( 2, EyeIcon( IsShown( i ), ink ) );
		pItem->setToolTip( 2, IsShown( i ) ? tr( "Drawn on the screen. Click to leave it out of the preview" ) : tr( "Left out of the preview. Click to draw it again" ) );
		pItem->setData( 3, Qt::UserRole, IsPickable( i ) );
		pItem->setIcon( 3, PickIcon( IsPickable( i ), ink ) );
		pItem->setToolTip( 3, IsPickable( i ) ? tr( "Picked by a click on the screen. Click to keep it from being picked there" ) : tr( "Not picked by clicks on the screen. Click to let it be picked again" ) );
		if ( !m_Elements[ i ].m_bVisible )
			pItem->setForeground( 0, palette().placeholderText() );
		if ( i == m_nSelected )
			m_pList->setCurrentItem( pItem );
	}
	m_bFilling = false;
}

//-----------------------------------------------------------------------------
// Purpose: The scheme's panel: its colours and settings with a swatch for each that is a
//          colour, its fonts (each with the face it draws with on this screen, in that
//          face, and its entries under it) and its borders with their keys.
//-----------------------------------------------------------------------------
void CHudEditor::FillScheme()
{
	const QString szFilter = m_pSchemeFilter->text().trimmed();
	const auto matches = [ & ]( const std::string &szName, const std::string &szValue ) {
		return szFilter.isEmpty() || QString::fromStdString( szName ).contains( szFilter, Qt::CaseInsensitive ) || QString::fromStdString( szValue ).contains( szFilter, Qt::CaseInsensitive );
	};
	// What the selected element uses is marked (bold, on the highlight's tint), counted on its
	// tab, scrolled to, and with Used ticked the only thing listed.
	const std::set<std::string> used = SchemeNamesUsedBy( m_nSelected );
	const bool bUsedOnly = m_pUsedOnly->isChecked() && m_nSelected >= 0;
	const auto isUsed = [ & ]( const std::string &szName ) { return used.contains( QString::fromStdString( szName ).toLower().toStdString() ); };
	QColor tint = palette().color( QPalette::Highlight );
	tint.setAlpha( 70 );
	const auto mark = [ & ]( QTreeWidgetItem *pItem ) {
		QFont bold = pItem->font( 0 );
		bold.setBold( true );
		pItem->setFont( 0, bold );
		for ( int nColumn = 0; nColumn < pItem->columnCount(); nColumn++ )
			pItem->setBackground( nColumn, tint );
		pItem->setData( 0, Qt::UserRole, true );
		pItem->setToolTip( 0, tr( "Used by %1" ).arg( QString::fromStdString( m_Elements[ m_nSelected ].m_szName ) ) );
	};
	const auto finish = [ & ]( QTreeWidget *pTree, const QString &szTitle ) {
		int nUsed = 0;
		QTreeWidgetItem *pFirst = nullptr;
		for ( int i = 0; i < pTree->topLevelItemCount(); i++ )
		{
			if ( pTree->topLevelItem( i )->data( 0, Qt::UserRole ).toBool() )
			{
				nUsed++;
				pFirst = pFirst ? pFirst : pTree->topLevelItem( i );
			}
		}
		m_pSchemeTabs->setTabText( m_pSchemeTabs->indexOf( pTree ), nUsed > 0 ? tr( "%1 (%2)" ).arg( szTitle ).arg( nUsed ) : szTitle );
		if ( pFirst )
			pTree->scrollToItem( pFirst, QAbstractItemView::PositionAtCenter );
	};
	const auto fillValues = [ & ]( QTreeWidget *pTree, const std::vector<xv::hud::SchemeValue_t> &values ) {
		pTree->clear();
		for ( const xv::hud::SchemeValue_t &value : values )
		{
			if ( !matches( value.m_szName, value.m_szValue ) || ( bUsedOnly && !isUsed( value.m_szName ) ) )
				continue;
			auto *pItem = new QTreeWidgetItem( pTree, { QString::fromStdString( value.m_szName ), QString::fromStdString( value.m_szValue ) } );
			if ( const auto color = m_Scheme.Resolve( value.m_szValue ) )
				pItem->setIcon( 0, Swatch( *color ) );
			if ( isUsed( value.m_szName ) )
				mark( pItem );
		}
		pTree->resizeColumnToContents( 0 );
	};
	fillValues( m_pSchemeColors, m_Scheme.GetColors() );
	finish( m_pSchemeColors, tr( "Colours" ) );
	fillValues( m_pSchemeSettings, m_Scheme.GetSettings() );
	finish( m_pSchemeSettings, tr( "Settings" ) );

	m_pSchemeFonts->clear();
	for ( const xv::hud::SchemeFont_t &font : m_Scheme.GetFonts() )
	{
		if ( !matches( font.m_szName, {} ) && std::ranges::none_of( font.m_Entries, [ & ]( const xv::hud::FontEntry_t &entry ) { return matches( entry.m_szFace, {} ); } ) )
			continue;
		if ( bUsedOnly && !isUsed( font.m_szName ) )
			continue;
		const std::optional<xv::hud::Font_t> now = m_Scheme.ResolveFont( font.m_szName, m_Screen );
		auto *pItem = new QTreeWidgetItem( m_pSchemeFonts, { QString::fromStdString( font.m_szName ), now ? tr( "%1, %2 px" ).arg( QString::fromStdString( now->m_szFace ) ).arg( now->m_nPixels ) : tr( "none for this screen" ) } );
		if ( now )
		{
			// Its name in its own face, at a size that fits the list.
			QFont sample = FontFor( font.m_szName, 1.0 );
			sample.setPixelSize( std::clamp( sample.pixelSize(), 10, 16 ) );
			// A sample beside it, not the name itself: an icon font's name would be icons.
			pItem->setText( 2, QStringLiteral( "AaBb 0123" ) );
			pItem->setFont( 2, sample );
		}
		if ( isUsed( font.m_szName ) )
			mark( pItem );
		for ( const xv::hud::FontEntry_t &entry : font.m_Entries )
		{
			QStringList what{ tr( "tall %1" ).arg( entry.m_nTall ) };
			if ( entry.m_nWeight > 0 )
				what << tr( "weight %1" ).arg( entry.m_nWeight );
			if ( entry.m_YRes )
				what << tr( "screens %1 to %2 high" ).arg( entry.m_YRes->first ).arg( entry.m_YRes->second );
			else
				what << tr( "any screen, scaled" );
			for ( const auto &[ bOn, szFlag ] : { std::pair{ entry.m_bAntialias, tr( "antialias" ) }, std::pair{ entry.m_bAdditive, tr( "additive" ) }, std::pair{ entry.m_bItalic, tr( "italic" ) }, std::pair{ entry.m_nBlur > 0, tr( "blur %1" ).arg( entry.m_nBlur ) }, std::pair{ entry.m_nScanlines > 0, tr( "scanlines %1" ).arg( entry.m_nScanlines ) } } )
			{
				if ( bOn )
					what << szFlag;
			}
			new QTreeWidgetItem( pItem, { QString::fromStdString( entry.m_szFace ), what.join( QStringLiteral( ", " ) ) } );
		}
	}
	m_pSchemeFonts->resizeColumnToContents( 0 );
	m_pSchemeFonts->setColumnWidth( 0, std::min( m_pSchemeFonts->columnWidth( 0 ), 170 ) );
	finish( m_pSchemeFonts, tr( "Fonts" ) );

	m_pSchemeBorders->clear();
	for ( const xv::hud::SchemeBorder_t &border : m_Scheme.GetBorders() )
	{
		if ( !matches( border.m_szName, {} ) || ( bUsedOnly && !isUsed( border.m_szName ) ) )
			continue;
		auto *pItem = new QTreeWidgetItem( m_pSchemeBorders, { QString::fromStdString( border.m_szName ), tr( "%n key(s)", nullptr, static_cast<int>( border.m_Keys.size() ) ) } );
		for ( const xv::hud::SchemeValue_t &key : border.m_Keys )
		{
			auto *pKey = new QTreeWidgetItem( pItem, { QString::fromStdString( key.m_szName ), QString::fromStdString( key.m_szValue ) } );
			if ( const auto color = m_Scheme.Resolve( key.m_szValue ); color && key.m_szName.ends_with( "color" ) )
				pKey->setIcon( 1, Swatch( *color ) );
		}
		if ( isUsed( border.m_szName ) )
			mark( pItem );
	}
	m_pSchemeBorders->resizeColumnToContents( 0 );
	finish( m_pSchemeBorders, tr( "Borders" ) );
	m_pUsedOnly->setEnabled( m_nSelected >= 0 );

	// The scheme's file opens in the scheme editor; the tip says which file that is.
	m_pEditScheme->setVisible( !m_szSchemeSource.isEmpty() );
	m_pEditScheme->setToolTip( tr( "Open %1 in the scheme editor; this panel follows its changes" ).arg( m_szSchemeSource ) );
}

//-----------------------------------------------------------------------------
// Purpose: The variables of every label's text (as written, and as its token reads in the
//          game's language), each with the value tried for it.
//-----------------------------------------------------------------------------
void CHudEditor::FillVariables()
{
	m_bFilling = true;
	std::vector<std::string> names;
	for ( const xv::hud::Element_t &element : m_Elements )
	{
		if ( element.m_szLabel.empty() )
			continue;
		for ( const std::string &szName : xv::hud::FindVariables( Localize( element.m_szLabel ).toStdString() ) )
		{
			if ( std::ranges::none_of( names, [ & ]( const std::string &szHave ) { return xv::kv1::EqualsIgnoreCase( szHave, szName ); } ) )
				names.push_back( szName );
		}
	}
	m_pVariables->setRowCount( 0 );
	for ( const std::string &szName : names )
	{
		const int nRow = m_pVariables->rowCount();
		m_pVariables->insertRow( nRow );
		const bool bArgument = szName.size() == 2 && szName[ 0 ] == 's';
		auto *pName = new QTableWidgetItem( bArgument ? QStringLiteral( "%" ) + QString::fromStdString( szName ) : QStringLiteral( "%" ) + QString::fromStdString( szName ) + QStringLiteral( "%" ) );
		pName->setData( Qt::UserRole, QString::fromStdString( szName ) );
		pName->setFlags( pName->flags() & ~Qt::ItemIsEditable );
		m_pVariables->setItem( nRow, 0, pName );
		m_pVariables->setItem( nRow, 1, new QTableWidgetItem( GetVariable( QString::fromStdString( szName ) ) ) );
	}
	m_pVariables->resizeColumnToContents( 0 );
	m_pSchemeTabs->setTabText( m_pSchemeTabs->indexOf( m_pVariables ), names.empty() ? tr( "Variables" ) : tr( "Variables (%1)" ).arg( names.size() ) );
	m_bFilling = false;
}

//-----------------------------------------------------------------------------
// Purpose: A scheme name double-clicked goes into the key selected in the sheet (a colour
//          into BgColor, a font into font...), as one edit.
//-----------------------------------------------------------------------------
void CHudEditor::ApplySchemeName( QTreeWidgetItem *pItem )
{
	if ( !pItem || m_nSelected < 0 )
		return;
	while ( pItem->parent() )
		pItem = pItem->parent();
	const int nRow = m_pSheet->currentRow();
	if ( nRow < 0 || !m_pSheet->item( nRow, 1 ) || !( m_pSheet->item( nRow, 1 )->flags() & Qt::ItemIsEditable ) )
		return;
	SetValue( m_nSelected, m_pSheet->item( nRow, 0 )->text(), pItem->text( 0 ) );
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
			// A value that is one of the scheme's names says so: its colour, or which font.
			if ( const auto color = m_Scheme.Resolve( child.GetValue() ) )
			{
				pValue->setIcon( Swatch( *color ) );
				pValue->setToolTip( tr( "%1 %2 %3 %4" ).arg( color->m_nR ).arg( color->m_nG ).arg( color->m_nB ).arg( color->m_nA ) );
			}
			else if ( const auto font = m_Scheme.ResolveFont( child.GetValue(), m_Screen ) )
			{
				pValue->setToolTip( tr( "The scheme's font %1: %2, %3 px on this screen" ).arg( QString::fromStdString( child.GetValue() ), QString::fromStdString( font->m_szFace ) ).arg( font->m_nPixels ) );
			}
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
	const xv::kv1::CDocument document = xv::kv1::Parse( szText ).m_Document;
	return xv::hud::IsLayout( document ) || xv::hud::IsGameMenu( document );
}
