//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See DemoViewer.h.
//
//===========================================================================//
#include "DemoViewer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace
{

constexpr int TIMELINE_TALL = 72;
constexpr int PATH_MARGIN = 24;
// Two camera places further apart than this between packets are a respawn or a teleport,
// not a move: no line joins them.
constexpr float PATH_JUMP = 512.0f;
// The tick length played at when the header gives none (a demo cut off while recording):
// the usual 66.67 ticks a second.
constexpr double FALLBACK_TICK_INTERVAL = 0.015;
constexpr int COMMAND_COLUMN_TIME = 0;
constexpr int COMMAND_COLUMN_TICK = 1;
constexpr int COMMAND_COLUMN_TEXT = 2;

//-----------------------------------------------------------------------------
// Purpose: A value label whose text can be selected and copied. bWrap for text of any
//          length (a server's name, a warning), which then takes the lines it needs.
//-----------------------------------------------------------------------------
QLabel *ValueLabel( const QString &szText, bool bWrap = false )
{
	auto *pLabel = new QLabel( szText );
	pLabel->setTextInteractionFlags( Qt::TextSelectableByMouse );
	pLabel->setWordWrap( bWrap );
	return pLabel;
}

//-----------------------------------------------------------------------------
// Purpose: A read-only table with whole rows picked and the last column taking the room left.
//-----------------------------------------------------------------------------
QTableWidget *ListTable( const QStringList &headers )
{
	auto *pTable = new QTableWidget( 0, static_cast<int>( headers.size() ) );
	pTable->setHorizontalHeaderLabels( headers );
	pTable->setEditTriggers( QAbstractItemView::NoEditTriggers );
	pTable->setSelectionBehavior( QAbstractItemView::SelectRows );
	pTable->setSelectionMode( QAbstractItemView::SingleSelection );
	pTable->verticalHeader()->hide();
	pTable->horizontalHeader()->setStretchLastSection( true );
	return pTable;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CDemoTimeline::CDemoTimeline( CDemoViewer &viewer, QWidget *pParent )
	: QWidget( pParent ),
	  m_Viewer( viewer )
{
	setObjectName( QStringLiteral( "demoTimeline" ) );
	setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
	setCursor( Qt::PointingHandCursor );
	setToolTip( tr( "How much the server sent over the demo's length. Click or drag to move through it; "
					"the marks along the top are console commands" ) );
	connect( &viewer, &CDemoViewer::TickChanged, this, qOverload<>( &QWidget::update ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QSize CDemoTimeline::sizeHint() const
{
	return { 400, TIMELINE_TALL };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CDemoTimeline::TickAt( int x ) const
{
	const int nLast = m_Viewer.GetDemo().GetLastTick();
	if ( width() <= 1 || nLast <= 0 )
	{
		return 0;
	}
	return static_cast<int>( std::clamp<qint64>( static_cast<qint64>( x ) * nLast / ( width() - 1 ), 0, nLast ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CDemoTimeline::XAt( int nTick ) const
{
	const int nLast = m_Viewer.GetDemo().GetLastTick();
	if ( nLast <= 0 )
	{
		return 0;
	}
	return static_cast<int>( static_cast<qint64>( nTick ) * ( width() - 1 ) / nLast );
}

//-----------------------------------------------------------------------------
// Purpose: The bars are counted once for each width, not at every repaint.
//-----------------------------------------------------------------------------
void CDemoTimeline::resizeEvent( QResizeEvent *pEvent )
{
	QWidget::resizeEvent( pEvent );
	m_Bars = m_Viewer.GetDemo().PacketBytesOverTime( width() );
}

//-----------------------------------------------------------------------------
// Purpose: Bars up from the bottom, as tall as their share of the busiest one; what has
//          played is in the highlight colour.
//-----------------------------------------------------------------------------
void CDemoTimeline::paintEvent( QPaintEvent * )
{
	QPainter painter( this );
	painter.fillRect( rect(), palette().color( QPalette::Base ) );
	const int nPlayhead = XAt( m_Viewer.GetTick() );
	const std::uint64_t nMost = m_Bars.empty() ? 0 : *std::max_element( m_Bars.begin(), m_Bars.end() );
	if ( nMost > 0 )
	{
		QColor played = palette().color( QPalette::Highlight );
		QColor ahead = palette().color( QPalette::Text );
		ahead.setAlphaF( 0.35f );
		const int nRoom = height() - 8;
		for ( int x = 0; x < static_cast<int>( m_Bars.size() ); x++ )
		{
			const int nTall = static_cast<int>( m_Bars[ static_cast<std::size_t>( x ) ] * static_cast<std::uint64_t>( nRoom ) / nMost );
			if ( nTall > 0 )
			{
				painter.fillRect( x, height() - nTall, 1, nTall, x <= nPlayhead ? played : ahead );
			}
		}
	}
	else
	{
		painter.setPen( palette().color( QPalette::PlaceholderText ) );
		painter.drawText( rect(), Qt::AlignCenter, tr( "No packets were read" ) );
	}
	painter.setPen( palette().color( QPalette::Link ) );
	for ( const xv::demo::Command_t &command : m_Viewer.GetDemo().GetCommands() )
	{
		const int x = XAt( command.m_nTick );
		painter.drawLine( x, 0, x, 5 );
	}
	painter.setPen( QPen( palette().color( QPalette::Text ), 1 ) );
	painter.drawLine( nPlayhead, 0, nPlayhead, height() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoTimeline::mousePressEvent( QMouseEvent *pEvent )
{
	if ( pEvent->button() == Qt::LeftButton )
	{
		m_Viewer.SetTick( TickAt( pEvent->position().toPoint().x() ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoTimeline::mouseMoveEvent( QMouseEvent *pEvent )
{
	if ( pEvent->buttons() & Qt::LeftButton )
	{
		m_Viewer.SetTick( TickAt( pEvent->position().toPoint().x() ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The path's bounds are taken once; packets from before the recorder was in the
//          map (the camera at the map's origin exactly) don't count.
//-----------------------------------------------------------------------------
CDemoPathView::CDemoPathView( CDemoViewer &viewer, QWidget *pParent )
	: QWidget( pParent ),
	  m_Viewer( viewer )
{
	setObjectName( QStringLiteral( "demoPath" ) );
	setMinimumSize( 200, 160 );
	bool bFirst = true;
	for ( const xv::demo::View_t &view : viewer.GetDemo().GetViews() )
	{
		const xv::CVec3 &vecAt = view.m_vecOrigin;
		if ( vecAt.m_flX == 0.0f && vecAt.m_flY == 0.0f && vecAt.m_flZ == 0.0f )
		{
			continue;
		}
		if ( bFirst )
		{
			m_vecMins = m_vecMaxs = vecAt;
			bFirst = false;
		}
		m_vecMins = { std::min( m_vecMins.m_flX, vecAt.m_flX ), std::min( m_vecMins.m_flY, vecAt.m_flY ), std::min( m_vecMins.m_flZ, vecAt.m_flZ ) };
		m_vecMaxs = { std::max( m_vecMaxs.m_flX, vecAt.m_flX ), std::max( m_vecMaxs.m_flY, vecAt.m_flY ), std::max( m_vecMaxs.m_flZ, vecAt.m_flZ ) };
	}
	connect( &viewer, &CDemoViewer::TickChanged, this, qOverload<>( &QWidget::update ) );
}

//-----------------------------------------------------------------------------
// Purpose: The bounds fitted inside the margin, the same scale both ways, centred.
//-----------------------------------------------------------------------------
QPointF CDemoPathView::ToWidget( const xv::CVec3 &vecAt ) const
{
	const double flWide = std::max( 1.0, static_cast<double>( m_vecMaxs.m_flX - m_vecMins.m_flX ) );
	const double flTall = std::max( 1.0, static_cast<double>( m_vecMaxs.m_flY - m_vecMins.m_flY ) );
	const double flScale = std::min( ( width() - 2 * PATH_MARGIN ) / flWide, ( height() - 2 * PATH_MARGIN ) / flTall );
	const double flMidX = ( m_vecMins.m_flX + m_vecMaxs.m_flX ) * 0.5;
	const double flMidY = ( m_vecMins.m_flY + m_vecMaxs.m_flY ) * 0.5;
	return { width() * 0.5 + ( vecAt.m_flX - flMidX ) * flScale, height() * 0.5 - ( vecAt.m_flY - flMidY ) * flScale };
}

//-----------------------------------------------------------------------------
// Purpose: A long demo has a packet's camera for every tick: only those that land a pixel
//          or more from the one before are kept, so a repaint while playing stays cheap.
//-----------------------------------------------------------------------------
void CDemoPathView::BuildStretches()
{
	m_Stretches.clear();
	const xv::CVec3 *pLast = nullptr;
	for ( const xv::demo::View_t &view : m_Viewer.GetDemo().GetViews() )
	{
		const xv::CVec3 &vecAt = view.m_vecOrigin;
		if ( vecAt.m_flX == 0.0f && vecAt.m_flY == 0.0f && vecAt.m_flZ == 0.0f )
		{
			continue;
		}
		const bool bJump = pLast && ( std::abs( vecAt.m_flX - pLast->m_flX ) > PATH_JUMP || std::abs( vecAt.m_flY - pLast->m_flY ) > PATH_JUMP || std::abs( vecAt.m_flZ - pLast->m_flZ ) > PATH_JUMP );
		pLast = &vecAt;
		const QPointF at = ToWidget( vecAt );
		if ( m_Stretches.empty() || bJump )
		{
			m_Stretches.emplace_back();
		}
		Stretch_t &stretch = m_Stretches.back();
		if ( !stretch.m_Points.isEmpty() && ( stretch.m_Points.back() - at ).manhattanLength() < 1.0 )
		{
			continue;
		}
		stretch.m_Points.push_back( at );
		stretch.m_Ticks.push_back( view.m_nTick );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoPathView::resizeEvent( QResizeEvent *pEvent )
{
	QWidget::resizeEvent( pEvent );
	BuildStretches();
}

//-----------------------------------------------------------------------------
// Purpose: The whole path faint, the part played so far over it, then the camera.
//-----------------------------------------------------------------------------
void CDemoPathView::paintEvent( QPaintEvent * )
{
	QPainter painter( this );
	painter.fillRect( rect(), palette().color( QPalette::Base ) );
	const xv::demo::CDemo &demo = m_Viewer.GetDemo();
	if ( !demo.HasCamera() )
	{
		painter.setPen( palette().color( QPalette::PlaceholderText ) );
		painter.drawText( rect().adjusted( 16, 16, -16, -16 ), Qt::AlignCenter | Qt::TextWordWrap,
			demo.IsSourceTv() ? tr( "A SourceTV demo has no camera of its own: the server recorded it, not a player.\n"
									"Its players are inside the packets, which this first look doesn't read." )
							  : tr( "This demo's packets don't say where the camera was." ) );
		return;
	}
	painter.setRenderHint( QPainter::Antialiasing );
	QColor faint = palette().color( QPalette::Text );
	faint.setAlphaF( 0.25f );
	const QColor strong = palette().color( QPalette::Highlight );
	const int nTick = m_Viewer.GetTick();
	for ( const Stretch_t &stretch : m_Stretches )
	{
		painter.setPen( QPen( faint, 1 ) );
		painter.drawPolyline( stretch.m_Points );
		const auto nPlayed = std::upper_bound( stretch.m_Ticks.begin(), stretch.m_Ticks.end(), nTick ) - stretch.m_Ticks.begin();
		if ( nPlayed > 1 )
		{
			painter.setPen( QPen( strong, 2 ) );
			painter.drawPolyline( stretch.m_Points.constData(), static_cast<int>( nPlayed ) );
		}
	}
	if ( const xv::demo::View_t *pView = demo.ViewAt( nTick ) )
	{
		const QPointF at = ToWidget( pView->m_vecOrigin );
		const double flYaw = pView->m_vecAngles.m_flY * std::numbers::pi / 180.0;
		// The map's y goes up the widget.
		const QPointF ahead( std::cos( flYaw ) * 18.0, -std::sin( flYaw ) * 18.0 );
		painter.setPen( QPen( palette().color( QPalette::Text ), 2 ) );
		painter.drawLine( at, at + ahead );
		painter.setBrush( strong );
		painter.drawEllipse( at, 5.0, 5.0 );
		painter.setPen( palette().color( QPalette::Text ) );
		painter.drawText( QPointF( 8, height() - 8 ), tr( "%1 %2 %3" ).arg( pView->m_vecOrigin.m_flX, 0, 'f', 0 ).arg( pView->m_vecOrigin.m_flY, 0, 'f', 0 ).arg( pView->m_vecOrigin.m_flZ, 0, 'f', 0 ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The summary beside the pages, the timeline and its controls under both.
//-----------------------------------------------------------------------------
CDemoViewer::CDemoViewer( const QString &szName, xv::demo::CDemo demo, const std::filesystem::path &disk, bool bFromGame, QWidget *pParent )
	: QWidget( pParent ),
	  m_Demo( std::move( demo ) ),
	  m_szName( szName ),
	  m_Disk( disk ),
	  m_bFromGame( bFromGame )
{
	setFocusPolicy( Qt::StrongFocus );

	m_pPath = new CDemoPathView( *this );
	m_pPages = new QTabWidget;
	m_pPages->setObjectName( QStringLiteral( "demoPages" ) );
	m_pPages->addTab( m_pPath, tr( "Camera Path" ) );
	m_pPages->addTab( BuildCommands(), tr( "Console Commands (%1)" ).arg( m_Demo.GetCommands().size() ) );
	m_pPages->addTab( BuildFrameKinds(), tr( "Frames" ) );
	// A demo with no camera opens on what it does have.
	if ( !m_Demo.HasCamera() )
	{
		m_pPages->setCurrentIndex( m_Demo.GetCommands().empty() ? 2 : 1 );
	}

	auto *pSplitter = new QSplitter( Qt::Horizontal );
	pSplitter->addWidget( BuildSummary() );
	pSplitter->addWidget( m_pPages );
	pSplitter->setStretchFactor( 0, 0 );
	pSplitter->setStretchFactor( 1, 1 );

	m_pTimeline = new CDemoTimeline( *this );
	m_pPlay = new QPushButton( tr( "Play" ) );
	m_pPlay->setObjectName( QStringLiteral( "demoPlay" ) );
	m_pPlay->setCheckable( true );
	m_pPlay->setToolTip( tr( "Run the playhead at the demo's own speed (Space)" ) );
	connect( m_pPlay, &QPushButton::toggled, this, &CDemoViewer::SetPlaying );
	m_pSpeed = new QComboBox;
	m_pSpeed->setObjectName( QStringLiteral( "demoSpeed" ) );
	for ( const double flSpeed : { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 } )
	{
		m_pSpeed->addItem( tr( "%1×" ).arg( flSpeed ), flSpeed );
	}
	m_pSpeed->setCurrentIndex( m_pSpeed->findData( 1.0 ) );
	m_pSpeed->setToolTip( tr( "How fast Play runs" ) );
	// The clock starts over at the playhead, so a change of speed applies from here on.
	connect( m_pSpeed, &QComboBox::currentIndexChanged, this, [ this ] {
		m_flPlayTick = m_nTick;
		m_PlayClock.restart();
	} );
	m_pTime = new QLabel;
	m_pTime->setObjectName( QStringLiteral( "demoTime" ) );

	auto *pControls = new QHBoxLayout;
	pControls->addWidget( m_pPlay );
	pControls->addWidget( m_pSpeed );
	pControls->addWidget( m_pTime, 1 );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->addWidget( pSplitter, 1 );
	pLayout->addWidget( m_pTimeline );
	pLayout->addLayout( pControls );

	m_pPlayTimer = new QTimer( this );
	m_pPlayTimer->setInterval( 16 );
	connect( m_pPlayTimer, &QTimer::timeout, this, &CDemoViewer::Advance );
	UpdateTimeLabel();
}

//-----------------------------------------------------------------------------
// Purpose: What the header says, and what reading the frames found wrong.
//-----------------------------------------------------------------------------
QWidget *CDemoViewer::BuildSummary()
{
	const xv::demo::Header_t &header = m_Demo.GetHeader();
	auto *pPanel = new QWidget;
	pPanel->setObjectName( QStringLiteral( "demoSummary" ) );
	pPanel->setMinimumWidth( 320 );
	auto *pForm = new QFormLayout( pPanel );
	// Fields kept at their size hints (some styles' way) give a wrapped label a narrow column
	// and one line's height.
	pForm->setFieldGrowthPolicy( QFormLayout::AllNonFixedFieldsGrow );
	pForm->addRow( tr( "Server" ), ValueLabel( QString::fromUtf8( header.m_szServer ), true ) );
	pForm->addRow( tr( "Recorded by" ), ValueLabel( m_Demo.IsSourceTv() ? tr( "SourceTV (the server)" ) : QString::fromUtf8( header.m_szClient ) ) );
	pForm->addRow( tr( "Map" ), ValueLabel( QString::fromUtf8( header.m_szMap ) ) );
	pForm->addRow( tr( "Game" ), ValueLabel( QString::fromUtf8( header.m_szGameDir ) ) );
	pForm->addRow( tr( "Length" ), ValueLabel( FormatTime( m_Demo.GetLastTick() ) ) );
	const double flInterval = m_Demo.GetTickInterval();
	pForm->addRow( tr( "Ticks" ), ValueLabel( flInterval > 0.0 ? tr( "%L1, %2 a second" ).arg( m_Demo.GetLastTick() ).arg( 1.0 / flInterval, 0, 'f', 1 ) : tr( "%L1" ).arg( m_Demo.GetLastTick() ) ) );
	pForm->addRow( tr( "Frames" ), ValueLabel( tr( "%L1" ).arg( header.m_nFrames > 0 ? header.m_nFrames : static_cast<int>( m_Demo.GetFrames().size() ) ) ) );
	pForm->addRow( tr( "File size" ), ValueLabel( QLocale{}.formattedDataSize( static_cast<qint64>( m_Demo.GetFileSize() ) ) ) );
	pForm->addRow( tr( "Protocol" ), ValueLabel( tr( "demo %1, network %2" ).arg( header.m_nDemoProtocol ).arg( header.m_nNetworkProtocol ) ) );
	if ( !m_Demo.GetWarning().empty() )
	{
		QLabel *pWarning = ValueLabel( QString::fromStdString( m_Demo.GetWarning() ), true );
		pWarning->setObjectName( QStringLiteral( "demoWarning" ) );
		pForm->addRow( pWarning );
	}
	return pPanel;
}

//-----------------------------------------------------------------------------
// Purpose: The commands the recorder's console ran, each at its time; a double click puts
//          the playhead there.
//-----------------------------------------------------------------------------
QWidget *CDemoViewer::BuildCommands()
{
	m_pCommands = ListTable( { tr( "Time" ), tr( "Tick" ), tr( "Command" ) } );
	m_pCommands->setObjectName( QStringLiteral( "demoCommands" ) );
	const std::vector<xv::demo::Command_t> &commands = m_Demo.GetCommands();
	m_pCommands->setRowCount( static_cast<int>( commands.size() ) );
	for ( int i = 0; i < static_cast<int>( commands.size() ); i++ )
	{
		const xv::demo::Command_t &command = commands[ static_cast<std::size_t>( i ) ];
		m_pCommands->setItem( i, COMMAND_COLUMN_TIME, new QTableWidgetItem( FormatTime( command.m_nTick ) ) );
		m_pCommands->setItem( i, COMMAND_COLUMN_TICK, new QTableWidgetItem( QString::number( command.m_nTick ) ) );
		m_pCommands->setItem( i, COMMAND_COLUMN_TEXT, new QTableWidgetItem( QString::fromUtf8( command.m_szText ) ) );
	}
	m_pCommands->resizeColumnToContents( COMMAND_COLUMN_TIME );
	m_pCommands->resizeColumnToContents( COMMAND_COLUMN_TICK );
	connect( m_pCommands, &QTableWidget::cellDoubleClicked, this, [ this ]( int nRow ) {
		SetTick( m_Demo.GetCommands()[ static_cast<std::size_t>( nRow ) ].m_nTick );
	} );
	return m_pCommands;
}

//-----------------------------------------------------------------------------
// Purpose: How many frames of each kind the file has, and how much of the file they are.
//-----------------------------------------------------------------------------
QWidget *CDemoViewer::BuildFrameKinds()
{
	QTableWidget *pTable = ListTable( { tr( "Kind" ), tr( "Frames" ), tr( "Size" ), tr( "Of the file" ) } );
	pTable->setObjectName( QStringLiteral( "demoFrameKinds" ) );
	for ( int nKind = 1; nKind <= xv::demo::FRAME_KIND_COUNT; nKind++ )
	{
		const auto nFrameKind = static_cast<xv::demo::FrameKind_t>( nKind );
		const int nCount = m_Demo.CountOf( nFrameKind );
		if ( nCount == 0 )
		{
			continue;
		}
		const std::uint64_t nBytes = m_Demo.BytesOf( nFrameKind );
		const int nRow = pTable->rowCount();
		pTable->insertRow( nRow );
		const std::string_view szKind = xv::demo::FrameKindName( nFrameKind );
		pTable->setItem( nRow, 0, new QTableWidgetItem( QString::fromUtf8( szKind.data(), static_cast<qsizetype>( szKind.size() ) ) ) );
		pTable->setItem( nRow, 1, new QTableWidgetItem( tr( "%L1" ).arg( nCount ) ) );
		pTable->setItem( nRow, 2, new QTableWidgetItem( QLocale{}.formattedDataSize( static_cast<qint64>( nBytes ) ) ) );
		pTable->setItem( nRow, 3, new QTableWidgetItem( tr( "%1%" ).arg( m_Demo.GetFileSize() ? 100.0 * static_cast<double>( nBytes ) / static_cast<double>( m_Demo.GetFileSize() ) : 0.0, 0, 'f', 1 ) ) );
	}
	pTable->resizeColumnsToContents();
	return pTable;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoViewer::SetTick( int nTick )
{
	nTick = std::clamp( nTick, 0, m_Demo.GetLastTick() );
	if ( nTick == m_nTick )
	{
		return;
	}
	m_nTick = nTick;
	// Moved by hand while playing: it plays on from there.
	if ( IsPlaying() && std::abs( m_flPlayTick - nTick ) >= 1.0 )
	{
		m_flPlayTick = nTick;
		m_PlayClock.restart();
	}
	UpdateTimeLabel();
	emit TickChanged( m_nTick );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CDemoViewer::IsPlaying() const
{
	return m_pPlayTimer->isActive();
}

//-----------------------------------------------------------------------------
// Purpose: Play at the end starts over from the beginning.
//-----------------------------------------------------------------------------
void CDemoViewer::SetPlaying( bool bPlaying )
{
	if ( bPlaying == IsPlaying() )
	{
		return;
	}
	if ( bPlaying )
	{
		if ( m_nTick >= m_Demo.GetLastTick() )
		{
			SetTick( 0 );
		}
		m_flPlayTick = m_nTick;
		m_PlayClock.start();
		m_pPlayTimer->start();
	}
	else
	{
		m_pPlayTimer->stop();
	}
	m_pPlay->setChecked( bPlaying );
	m_pPlay->setText( bPlaying ? tr( "Pause" ) : tr( "Play" ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double CDemoViewer::GetSpeed() const
{
	return m_pSpeed->currentData().toDouble();
}

//-----------------------------------------------------------------------------
// Purpose: One of the box's speeds; any other is left as it was.
//-----------------------------------------------------------------------------
void CDemoViewer::SetSpeed( double flSpeed )
{
	if ( const int nIndex = m_pSpeed->findData( flSpeed ); nIndex >= 0 )
	{
		m_pSpeed->setCurrentIndex( nIndex );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The playhead moved by the time gone since the clock last read, not by a tick a
//          timer shot: a slow repaint doesn't slow the demo.
//-----------------------------------------------------------------------------
void CDemoViewer::Advance()
{
	const double flInterval = m_Demo.GetTickInterval() > 0.0 ? m_Demo.GetTickInterval() : FALLBACK_TICK_INTERVAL;
	m_flPlayTick += static_cast<double>( m_PlayClock.restart() ) / 1000.0 / flInterval * GetSpeed();
	const int nLast = m_Demo.GetLastTick();
	if ( m_flPlayTick >= nLast )
	{
		m_flPlayTick = nLast;
		SetTick( nLast );
		SetPlaying( false );
		return;
	}
	SetTick( static_cast<int>( m_flPlayTick ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CDemoViewer::FormatTime( int nTick ) const
{
	if ( m_Demo.GetTickInterval() <= 0.0 )
	{
		return tr( "tick %L1" ).arg( nTick );
	}
	const double flSeconds = m_Demo.SecondsAt( nTick );
	const int nMinutes = static_cast<int>( flSeconds / 60.0 );
	return QStringLiteral( "%1:%2" ).arg( nMinutes ).arg( flSeconds - nMinutes * 60.0, 4, 'f', 1, QLatin1Char( '0' ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoViewer::UpdateTimeLabel()
{
	m_pTime->setText( tr( "%1 / %2   tick %L3" ).arg( FormatTime( m_nTick ), FormatTime( m_Demo.GetLastTick() ) ).arg( m_nTick ) );
}

//-----------------------------------------------------------------------------
// Purpose: Space plays and pauses; the arrows step a tick, a second with Shift; Home and
//          End go to the ends.
//-----------------------------------------------------------------------------
void CDemoViewer::keyPressEvent( QKeyEvent *pEvent )
{
	const double flInterval = m_Demo.GetTickInterval() > 0.0 ? m_Demo.GetTickInterval() : FALLBACK_TICK_INTERVAL;
	const int nStep = pEvent->modifiers() & Qt::ShiftModifier ? std::max( 1, static_cast<int>( std::lround( 1.0 / flInterval ) ) ) : 1;
	switch ( pEvent->key() )
	{
		case Qt::Key_Space:
			SetPlaying( !IsPlaying() );
			break;
		case Qt::Key_Left:
			SetTick( m_nTick - nStep );
			break;
		case Qt::Key_Right:
			SetTick( m_nTick + nStep );
			break;
		case Qt::Key_Home:
			SetTick( 0 );
			break;
		case Qt::Key_End:
			SetTick( m_Demo.GetLastTick() );
			break;
		default:
			QWidget::keyPressEvent( pEvent );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CDemoViewer::GetTabTitle() const
{
	return m_Disk.empty() ? m_szName.section( u'/', -1 ) : QString::fromStdU16String( m_Disk.filename().u16string() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CDemoViewer::GetSessionEntry() const
{
	if ( m_bFromGame )
	{
		return QStringLiteral( "demo\tgame\t%1" ).arg( m_szName );
	}
	if ( m_Disk.empty() )
	{
		return {};
	}
	return QStringLiteral( "demo\tdisk\t%1" ).arg( QString::fromStdU16String( m_Disk.u16string() ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoViewer::StopWork()
{
	SetPlaying( false );
}
