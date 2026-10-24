//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MaterialEditor.h.
//
//===========================================================================//
#include "MaterialEditor.h"

#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTextBlock>
#include <QTimer>
#include <QVBoxLayout>

#include "MaterialView.h"
#include "kv1/Kv1.h"

namespace
{

// A plain text box with line numbers down its left side.
class CNumberedEdit : public QPlainTextEdit
{
public:
	CNumberedEdit()
		: m_pNumbers( new CNumbers( this ) )
	{
		connect( this, &QPlainTextEdit::blockCountChanged, this, [ this ] { setViewportMargins( NumberWidth(), 0, 0, 0 ); } );
		connect( this, &QPlainTextEdit::updateRequest, this, [ this ]( const QRect &area, int nScrolled ) {
			nScrolled ? m_pNumbers->scroll( 0, nScrolled ) : m_pNumbers->update( 0, area.y(), m_pNumbers->width(), area.height() );
		} );
		setViewportMargins( NumberWidth(), 0, 0, 0 );
	}

protected:
	void resizeEvent( QResizeEvent *pEvent ) override
	{
		QPlainTextEdit::resizeEvent( pEvent );
		const QRect inside = contentsRect();
		m_pNumbers->setGeometry( inside.left(), inside.top(), NumberWidth(), inside.height() );
	}

private:
	class CNumbers : public QWidget
	{
	public:
		explicit CNumbers( CNumberedEdit *pEdit )
			: QWidget( pEdit ),
			  m_pEdit( pEdit )
		{}

	protected:
		void paintEvent( QPaintEvent *pEvent ) override
		{
			QPainter painter( this );
			painter.fillRect( pEvent->rect(), palette().color( QPalette::AlternateBase ) );
			painter.setPen( palette().color( QPalette::PlaceholderText ) );
			QTextBlock block = m_pEdit->firstVisibleBlock();
			int nTop = qRound( m_pEdit->blockBoundingGeometry( block ).translated( m_pEdit->contentOffset() ).top() );
			while ( block.isValid() && nTop <= pEvent->rect().bottom() )
			{
				const int nHeight = qRound( m_pEdit->blockBoundingRect( block ).height() );
				if ( block.isVisible() )
				{
					painter.drawText( 0, nTop, width() - 4, nHeight, Qt::AlignRight | Qt::AlignTop, QString::number( block.blockNumber() + 1 ) );
				}
				nTop += nHeight;
				block = block.next();
			}
		}

	private:
		CNumberedEdit *m_pEdit;
	};

	[[nodiscard]] int NumberWidth() const { return 12 + fontMetrics().horizontalAdvance( QString::number( std::max( 1, blockCount() ) ) ) + fontMetrics().horizontalAdvance( u'9' ); }

	CNumbers *m_pNumbers;
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose: pfnRead finds the textures the material names; it may be empty.
//-----------------------------------------------------------------------------
CMaterialEditor::CMaterialEditor( QString szGamePath, const QString &szText, xv::material::Reader_t pfnRead, QWidget *pParent )
	: QWidget( pParent ),
	  m_szGamePath( std::move( szGamePath ) ),
	  m_pfnRead( std::move( pfnRead ) )
{
	m_pText = new CNumberedEdit;
	m_pText->setObjectName( QStringLiteral( "materialText" ) );
	m_pText->setFont( QFontDatabase::systemFont( QFontDatabase::FixedFont ) );
	m_pText->setTabStopDistance( m_pText->fontMetrics().horizontalAdvance( u' ' ) * 4 );
	m_pText->setLineWrapMode( QPlainTextEdit::NoWrap );
	m_pText->setPlainText( szText );
	m_pText->document()->setModified( false );
	m_pProblems = new QLabel;
	m_pProblems->setObjectName( QStringLiteral( "materialProblems" ) );
	m_pProblems->setWordWrap( true );

	auto *pLeft = new QWidget;
	auto *pLeftLayout = new QVBoxLayout( pLeft );
	pLeftLayout->setContentsMargins( 0, 0, 0, 0 );
	pLeftLayout->addWidget( m_pText, 1 );
	pLeftLayout->addWidget( m_pProblems );

	m_pPreviewHost = new QWidget;
	auto *pPreviewLayout = new QVBoxLayout( m_pPreviewHost );
	pPreviewLayout->setContentsMargins( 0, 0, 0, 0 );

	auto *pSplitter = new QSplitter;
	pSplitter->addWidget( pLeft );
	pSplitter->addWidget( m_pPreviewHost );
	pSplitter->setStretchFactor( 0, 2 );
	pSplitter->setStretchFactor( 1, 3 );
	pSplitter->setChildrenCollapsible( false );
	m_pText->setMinimumWidth( 320 );
	pSplitter->setSizes( { 420, 680 } );
	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->addWidget( pSplitter );

	m_pPause = new QTimer( this );
	m_pPause->setSingleShot( true );
	m_pPause->setInterval( 400 );
	connect( m_pPause, &QTimer::timeout, this, &CMaterialEditor::UpdatePreview );
	connect( m_pText, &QPlainTextEdit::textChanged, this, [ this ] {
		CheckText();
		m_pPause->start();
	} );
	connect( m_pText->document(), &QTextDocument::modificationChanged, this, [ this ] { emit ModifiedChanged( IsModified() ); } );
	CheckText();
	UpdatePreview();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CMaterialEditor::GetText() const
{
	return m_pText->toPlainText();
}

//-----------------------------------------------------------------------------
// Purpose: Replaces the text as an edit.
//-----------------------------------------------------------------------------
void CMaterialEditor::SetText( const QString &szText )
{
	// As an edit: it can be undone and marks the material changed.
	QTextCursor cursor{ m_pText->document() };
	cursor.select( QTextCursor::Document );
	cursor.insertText( szText );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CMaterialEditor::GetFileName() const
{
	return QFileInfo{ m_szGamePath }.fileName();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMaterialEditor::IsModified() const
{
	return m_bNeverSaved || m_pText->document()->isModified();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterialEditor::MarkSaved()
{
	m_bNeverSaved = false;
	m_pText->document()->setModified( false );
	emit ModifiedChanged( false );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterialEditor::MarkUnsaved()
{
	m_bNeverSaved = true;
	emit ModifiedChanged( true );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterialEditor::SetReader( xv::material::Reader_t pfnRead )
{
	m_pfnRead = std::move( pfnRead );
	UpdatePreview();
}

//-----------------------------------------------------------------------------
// Purpose: Marks the lines the parser stumbles on (missing quotes, braces...) and lists why.
//-----------------------------------------------------------------------------
void CMaterialEditor::CheckText()
{
	const auto parsed = xv::kv1::Parse( GetText().toStdString() );
	QList<QTextEdit::ExtraSelection> marks;
	QStringList problems;
	m_ProblemLines.clear();
	QColor tint = palette().color( QPalette::Base );
	tint = QColor::fromRgbF( static_cast<float>( tint.redF() * 0.7 + 0.3 * 0.86 ), static_cast<float>( tint.greenF() * 0.7 + 0.3 * 0.2 ), static_cast<float>( tint.blueF() * 0.7 + 0.3 * 0.2 ) );
	for ( const xv::kv1::Diagnostic_t &diagnostic : parsed.m_Diagnostics )
	{
		const int nLine = static_cast<int>( diagnostic.m_nLine );
		m_ProblemLines << nLine;
		problems << tr( "Line %1: %2" ).arg( nLine ).arg( QString::fromStdString( diagnostic.m_szMessage ) );
		QTextEdit::ExtraSelection mark;
		mark.cursor = QTextCursor( m_pText->document()->findBlockByNumber( std::max( 0, nLine - 1 ) ) );
		mark.format.setBackground( tint );
		mark.format.setProperty( QTextFormat::FullWidthSelection, true );
		marks << mark;
	}
	m_pText->setExtraSelections( marks );
	m_pProblems->setText( problems.join( u'\n' ) );
	m_pProblems->setVisible( !problems.isEmpty() );
}

//-----------------------------------------------------------------------------
// Purpose: Draws the preview from the text now.
//-----------------------------------------------------------------------------
void CMaterialEditor::UpdatePreview()
{
	const QByteArray utf8 = GetText().toUtf8();
	const std::span bytes{ reinterpret_cast<const std::byte *>( utf8.constData() ), static_cast<std::size_t>( utf8.size() ) };
	auto *pPreview = new CMaterialView( m_szGamePath, {}, bytes, m_pfnRead );
	if ( m_pPreview )
	{
		m_pPreview->hide();
		m_pPreview->deleteLater();
	}
	m_pPreview = pPreview;
	m_pPreviewHost->layout()->addWidget( m_pPreview );
}
