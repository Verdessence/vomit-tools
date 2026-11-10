//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The text editors' box: line numbers, colour schemes, problems, suggestions and notes.
//
//===========================================================================//
#include "CodeEdit.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QCompleter>
#include <QCoreApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStringListModel>
#include <QTextBlock>
#include <QToolButton>
#include <QToolTip>

#include "TabGroups.h"

namespace
{

// Gruvbox (github.com/morhetz/gruvbox, MIT): its palette, which is all that's taken.
struct Gruvbox_t
{
	const char *m_pszBackground;
	const char *m_pszCurrentLine;
	const char *m_pszSelection;
	const char *m_pszText;
	const char *m_pszSelectedText;
	const char *m_pszGutterText;
	const char *m_pszGray;
	const char *m_pszRed;
	const char *m_pszGreen;
	const char *m_pszYellow;
	const char *m_pszBlue;
	const char *m_pszPurple;
	const char *m_pszOrange;
	const char *m_pszFaint;
};

constexpr Gruvbox_t GRUVBOX_DARK = { "#282828", "#32302f", "#504945", "#ebdbb2", "#fbf1c7", "#7c6f64", "#928374", "#fb4934", "#b8bb26", "#fabd2f", "#83a598", "#d3869b", "#fe8019", "#a89984" };
constexpr Gruvbox_t GRUVBOX_DARK_HARD = { "#1d2021", "#282828", "#504945", "#ebdbb2", "#fbf1c7", "#7c6f64", "#928374", "#fb4934", "#b8bb26", "#fabd2f", "#83a598", "#d3869b", "#fe8019", "#a89984" };
constexpr Gruvbox_t GRUVBOX_LIGHT = { "#fbf1c7", "#f2e5bc", "#d5c4a1", "#3c3836", "#282828", "#a89984", "#928374", "#9d0006", "#79740e", "#b57614", "#076678", "#8f3f71", "#af3a03", "#7c6f64" };

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QColor Mix( const QColor &from, const QColor &to, double flAmount )
{
	return QColor::fromRgbF( static_cast<float>( from.redF() + ( to.redF() - from.redF() ) * flAmount ),
		static_cast<float>( from.greenF() + ( to.greenF() - from.greenF() ) * flAmount ),
		static_cast<float>( from.blueF() + ( to.blueF() - from.blueF() ) * flAmount ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CodeColors_t FromGruvbox( const Gruvbox_t &scheme, bool bDark )
{
	const auto c = []( const char *psz ) { return QColor( QString::fromLatin1( psz ) ); };
	CodeColors_t colors;
	colors.m_bDark = bDark;
	colors.m_bOwnPalette = true;
	colors.m_Background = c( scheme.m_pszBackground );
	colors.m_Text = c( scheme.m_pszText );
	colors.m_Selection = c( scheme.m_pszSelection );
	colors.m_SelectedText = c( scheme.m_pszSelectedText );
	colors.m_CurrentLine = c( scheme.m_pszCurrentLine );
	colors.m_Gutter = c( scheme.m_pszBackground );
	colors.m_GutterText = c( scheme.m_pszGutterText );
	colors.m_GutterCurrent = c( scheme.m_pszYellow );
	colors.m_Keyword = c( scheme.m_pszRed );
	colors.m_Key = c( scheme.m_pszBlue );
	colors.m_String = c( scheme.m_pszGreen );
	colors.m_Number = c( scheme.m_pszPurple );
	colors.m_Comment = c( scheme.m_pszGray );
	colors.m_Special = c( scheme.m_pszOrange );
	colors.m_Brace = c( scheme.m_pszFaint );
	colors.m_Error = c( scheme.m_pszRed );
	return colors;
}

//-----------------------------------------------------------------------------
// Purpose: Whether a typed character may be part of a word worth suggesting for.
//-----------------------------------------------------------------------------
bool IsCompletionChar( QChar c )
{
	return c.isLetterOrNumber() || c == u'_' || c == u'"' || c == u':' || c == u'$' || c == u'!' || c == u',' || c == u'.' || c == u'/';
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The desktop's colours with words coloured to suit them, or a scheme of its own.
//-----------------------------------------------------------------------------
CodeColors_t CodeColorsFor( CodeScheme_t nScheme, const QPalette &palette )
{
	switch ( nScheme )
	{
		case CodeScheme_t::CODE_SCHEME_GRUVBOX_DARK: return FromGruvbox( GRUVBOX_DARK, true );
		case CodeScheme_t::CODE_SCHEME_GRUVBOX_DARK_HARD: return FromGruvbox( GRUVBOX_DARK_HARD, true );
		case CodeScheme_t::CODE_SCHEME_GRUVBOX_LIGHT: return FromGruvbox( GRUVBOX_LIGHT, false );
		default: break;
	}
	CodeColors_t colors;
	colors.m_Background = palette.color( QPalette::Base );
	colors.m_Text = palette.color( QPalette::Text );
	colors.m_bDark = colors.m_Background.lightness() < 128;
	colors.m_Selection = palette.color( QPalette::Highlight );
	colors.m_SelectedText = palette.color( QPalette::HighlightedText );
	colors.m_CurrentLine = Mix( colors.m_Background, colors.m_Text, 0.06 );
	colors.m_Gutter = palette.color( QPalette::AlternateBase );
	colors.m_GutterText = palette.color( QPalette::PlaceholderText );
	colors.m_GutterCurrent = colors.m_Text;
	const bool bDark = colors.m_bDark;
	const auto c = [ bDark ]( const char *pszLight, const char *pszDark ) { return QColor( QString::fromLatin1( bDark ? pszDark : pszLight ) ); };
	colors.m_Keyword = c( "#8a3fbf", "#d7a3ff" );
	colors.m_Key = c( "#1f5fbf", "#7cb7ff" );
	colors.m_String = c( "#2d7d2d", "#a5d68a" );
	colors.m_Number = c( "#b35900", "#f0b070" );
	colors.m_Comment = c( "#7a7f88", "#8a8f98" );
	colors.m_Special = c( "#12827a", "#6fd3c7" );
	colors.m_Brace = c( "#444444", "#cccccc" );
	colors.m_Error = c( "#d01c1c", "#ff6b6b" );
	return colors;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CodeSchemeName( CodeScheme_t nScheme )
{
	switch ( nScheme )
	{
		case CodeScheme_t::CODE_SCHEME_GRUVBOX_DARK: return QStringLiteral( "Gruvbox Dark" );
		case CodeScheme_t::CODE_SCHEME_GRUVBOX_DARK_HARD: return QStringLiteral( "Gruvbox Dark (hard)" );
		case CodeScheme_t::CODE_SCHEME_GRUVBOX_LIGHT: return QStringLiteral( "Gruvbox Light" );
		default: return QCoreApplication::translate( "CodeEdit", "Desktop colours" );
	}
}

// The line numbers, painted by the box (it knows where each line is).
class CCodeEdit::CGutter : public QWidget
{
public:
	explicit CGutter( CCodeEdit *pEdit )
		: QWidget( pEdit ),
		  m_pEdit( pEdit )
	{}

protected:
	void paintEvent( QPaintEvent *pEvent ) override { m_pEdit->PaintGutter( pEvent ); }

	bool event( QEvent *pEvent ) override
	{
		if ( pEvent->type() == QEvent::ToolTip )
		{
			auto *pHelp = static_cast<QHelpEvent *>( pEvent );
			const int nLine = m_pEdit->cursorForPosition( QPoint( 0, pHelp->pos().y() ) ).blockNumber() + 1;
			const QString szText = m_pEdit->DiagnosticsAt( nLine );
			szText.isEmpty() ? QToolTip::hideText() : QToolTip::showText( pHelp->globalPos(), szText, this );
			return true;
		}
		return QWidget::event( pEvent );
	}

private:
	CCodeEdit *m_pEdit;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CCodeEdit::CCodeEdit( QWidget *pParent )
	: QPlainTextEdit( pParent ),
	  m_pGutter( new CGutter( this ) )
{
	setLineWrapMode( QPlainTextEdit::NoWrap );
	connect( this, &QPlainTextEdit::blockCountChanged, this, [ this ] { setViewportMargins( GutterWidth(), 0, 0, 0 ); } );
	connect( this, &QPlainTextEdit::updateRequest, this, [ this ]( const QRect &area, int nScrolled ) {
		nScrolled ? m_pGutter->scroll( 0, nScrolled ) : m_pGutter->update( 0, area.y(), m_pGutter->width(), area.height() );
	} );
	// The marks are cursors into the document, so they're made again after an edit is done, not
	// while the document is still changing (clearing it walks its cursors).
	connect( this, &QPlainTextEdit::cursorPositionChanged, this, &CCodeEdit::ScheduleMarks );
	connect( document(), &QTextDocument::contentsChange, this, [ this ]( int, int nRemoved, int nAdded ) {
		if ( nRemoved || nAdded )
		{
			ScheduleMarks();
		}
	} );

	m_pWords = new QStringListModel( this );
	m_pCompleter = new QCompleter( m_pWords, this );
	m_pCompleter->setWidget( this );
	m_pCompleter->setCompletionMode( QCompleter::PopupCompletion );
	m_pCompleter->setCaseSensitivity( Qt::CaseInsensitive );
	m_pCompleter->setFilterMode( Qt::MatchContains );
	m_pCompleter->setModelSorting( QCompleter::UnsortedModel );
	m_pCompleter->setMaxVisibleItems( 12 );
	connect( m_pCompleter, qOverload<const QString &>( &QCompleter::activated ), this, &CCodeEdit::InsertCompletion );

	connect( &CPreferences::Instance(), &CPreferences::CodeEditorChanged, this, &CCodeEdit::ApplyPreferences );
	ApplyPreferences();
}

//-----------------------------------------------------------------------------
// Purpose: The scheme and font from Preferences, onto the box and its words.
//-----------------------------------------------------------------------------
void CCodeEdit::ApplyPreferences()
{
	m_bApplying = true;
	const CPreferences &preferences = CPreferences::Instance();
	m_szEditor = CTabGroups::EditorNameOf( this );
	setFont( preferences.GetCodeFont( m_szEditor ) );
	setTabStopDistance( 4 * fontMetrics().horizontalAdvance( u' ' ) );
	m_pCompleter->popup()->setFont( font() );
	// The desktop's palette first, so its own colours are what a scheme of the desktop's reads.
	setPalette( QPalette() );
	viewport()->setPalette( QPalette() );
	m_Colors = CodeColorsFor( preferences.GetCodeScheme( m_szEditor ), palette() );
	if ( m_Colors.m_bOwnPalette )
	{
		QPalette colours = palette();
		for ( QPalette::ColorGroup nGroup : { QPalette::Active, QPalette::Inactive } )
		{
			colours.setColor( nGroup, QPalette::Base, m_Colors.m_Background );
			colours.setColor( nGroup, QPalette::Window, m_Colors.m_Background );
			colours.setColor( nGroup, QPalette::Text, m_Colors.m_Text );
			colours.setColor( nGroup, QPalette::Highlight, m_Colors.m_Selection );
			colours.setColor( nGroup, QPalette::HighlightedText, m_Colors.m_SelectedText );
		}
		colours.setColor( QPalette::Disabled, QPalette::Base, m_Colors.m_Background );
		setPalette( colours );
		viewport()->setPalette( colours );
		viewport()->setAutoFillBackground( true );
		m_pCompleter->popup()->setPalette( colours );
	}
	else
	{
		m_pCompleter->popup()->setPalette( QPalette() );
	}
	setViewportMargins( GutterWidth(), 0, 0, 0 );
	const QRect inside = contentsRect();
	m_pGutter->setGeometry( inside.left(), inside.top(), GutterWidth(), inside.height() );
	m_bApplying = false;
	UpdateMarks();
	m_pGutter->update();
	emit ColorsChanged();
}

//-----------------------------------------------------------------------------
// Purpose: The desktop's colours changing changes a scheme taken from them.
//-----------------------------------------------------------------------------
void CCodeEdit::changeEvent( QEvent *pEvent )
{
	QPlainTextEdit::changeEvent( pEvent );
	if ( pEvent->type() == QEvent::PaletteChange && !m_bApplying && !m_Colors.m_bOwnPalette )
	{
		const CodeColors_t colors = CodeColorsFor( CPreferences::Instance().GetCodeScheme( m_szEditor ), palette() );
		if ( colors.m_Background != m_Colors.m_Background || colors.m_Text != m_Colors.m_Text )
		{
			m_Colors = colors;
			UpdateMarks();
			m_pGutter->update();
			emit ColorsChanged();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CCodeEdit::SetDiagnostics( std::vector<CodeDiagnostic_t> diagnostics )
{
	m_Diagnostics = std::move( diagnostics );
	ScheduleMarks();
}

//-----------------------------------------------------------------------------
// Purpose: Marks the lines again once the event loop is back, once however many edits ask.
//-----------------------------------------------------------------------------
void CCodeEdit::ScheduleMarks()
{
	if ( m_bMarksPending )
	{
		return;
	}
	m_bMarksPending = true;
	QMetaObject::invokeMethod( this, [ this ] {
		UpdateMarks();
		m_pGutter->update(); }, Qt::QueuedConnection );
}

//-----------------------------------------------------------------------------
// Purpose: The current line's band and the problems' underlines.
//-----------------------------------------------------------------------------
void CCodeEdit::UpdateMarks()
{
	m_bMarksPending = false;
	QList<QTextEdit::ExtraSelection> marks;
	QTextEdit::ExtraSelection current;
	current.format.setBackground( m_Colors.m_CurrentLine );
	current.format.setProperty( QTextFormat::FullWidthSelection, true );
	current.cursor = textCursor();
	current.cursor.clearSelection();
	marks.push_back( current );
	if ( IsFindShown() )
	{
		UpdateFindMatches();
		UpdateFindCount();
		QColor match = m_Colors.m_Selection;
		match.setAlpha( 110 );
		// Past a few thousand they slow typing down and say nothing more.
		for ( std::size_t i = 0; i < std::min<std::size_t>( m_FindMatches.size(), 5000 ); i++ )
		{
			QTextEdit::ExtraSelection mark;
			mark.format.setBackground( match );
			mark.cursor = QTextCursor( document() );
			mark.cursor.setPosition( m_FindMatches[ i ] );
			mark.cursor.setPosition( m_FindMatches[ i ] + m_nFindLength, QTextCursor::KeepAnchor );
			marks.push_back( mark );
		}
	}
	for ( const CodeDiagnostic_t &diagnostic : m_Diagnostics )
	{
		const QTextBlock block = document()->findBlockByNumber( diagnostic.m_nLine - 1 );
		if ( !block.isValid() )
		{
			continue;
		}
		const QString szLine = block.text();
		const qsizetype nFirst = std::distance( szLine.begin(), std::ranges::find_if( szLine, []( QChar c ) { return !c.isSpace(); } ) );
		if ( nFirst >= szLine.size() )
		{
			continue;
		}
		QTextEdit::ExtraSelection mark;
		mark.format.setUnderlineStyle( QTextCharFormat::WaveUnderline );
		mark.format.setUnderlineColor( m_Colors.m_Error );
		mark.cursor = QTextCursor( block );
		mark.cursor.setPosition( block.position() + static_cast<int>( nFirst ) );
		mark.cursor.setPosition( block.position() + static_cast<int>( szLine.trimmed().size() + nFirst ), QTextCursor::KeepAnchor );
		marks.push_back( mark );
	}
	setExtraSelections( marks );
}

//-----------------------------------------------------------------------------
// Purpose: The problems on a line, one to a line; empty for none.
//-----------------------------------------------------------------------------
QString CCodeEdit::DiagnosticsAt( int nLine ) const
{
	QStringList messages;
	for ( const CodeDiagnostic_t &diagnostic : m_Diagnostics )
	{
		if ( diagnostic.m_nLine == nLine )
		{
			messages.push_back( diagnostic.m_szMessage );
		}
	}
	return messages.join( u'\n' );
}

//-----------------------------------------------------------------------------
// Purpose: Room for the largest line number, and a mark for problems.
//-----------------------------------------------------------------------------
int CCodeEdit::GutterWidth() const
{
	return 16 + fontMetrics().horizontalAdvance( QString::number( std::max( 1, blockCount() ) ) ) + fontMetrics().horizontalAdvance( u'9' );
}

//-----------------------------------------------------------------------------
// Purpose: The visible lines' numbers, the cursor's brighter, problems' with a dot.
//-----------------------------------------------------------------------------
void CCodeEdit::PaintGutter( QPaintEvent *pEvent )
{
	QPainter painter( m_pGutter );
	painter.fillRect( pEvent->rect(), m_Colors.m_Gutter );
	const int nCurrent = textCursor().blockNumber();
	QFont bold = font();
	bold.setBold( true );
	QTextBlock block = firstVisibleBlock();
	int nTop = qRound( blockBoundingGeometry( block ).translated( contentOffset() ).top() );
	const int nDot = std::max( 4, fontMetrics().height() / 3 );
	while ( block.isValid() && nTop <= pEvent->rect().bottom() )
	{
		const int nHeight = qRound( blockBoundingRect( block ).height() );
		if ( block.isVisible() && nTop + nHeight >= pEvent->rect().top() )
		{
			const int nLine = block.blockNumber() + 1;
			const bool bProblem = std::ranges::any_of( m_Diagnostics, [ nLine ]( const CodeDiagnostic_t &d ) { return d.m_nLine == nLine; } );
			if ( bProblem )
			{
				painter.setRenderHint( QPainter::Antialiasing );
				painter.setPen( Qt::NoPen );
				painter.setBrush( m_Colors.m_Error );
				painter.drawEllipse( QRect( 3, nTop + ( fontMetrics().height() - nDot ) / 2, nDot, nDot ) );
			}
			const bool bCurrent = block.blockNumber() == nCurrent;
			painter.setFont( bCurrent ? bold : font() );
			painter.setPen( bProblem ? m_Colors.m_Error : bCurrent ? m_Colors.m_GutterCurrent
																   : m_Colors.m_GutterText );
			painter.drawText( 0, nTop, m_pGutter->width() - 5, nHeight, Qt::AlignRight | Qt::AlignTop, QString::number( nLine ) );
		}
		nTop += nHeight;
		block = block.next();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CCodeEdit::resizeEvent( QResizeEvent *pEvent )
{
	QPlainTextEdit::resizeEvent( pEvent );
	const QRect inside = contentsRect();
	m_pGutter->setGeometry( inside.left(), inside.top(), GutterWidth(), inside.height() );
	PlaceFindBar();
}

//-----------------------------------------------------------------------------
// Purpose: Notes on what's under the pointer: the line's problems, then the source's note.
//          Ctrl+F and F3 are taken here first.
//-----------------------------------------------------------------------------
bool CCodeEdit::event( QEvent *pEvent )
{
	// Made before it is put in an editor: that editor's own settings apply once it shows there.
	if ( pEvent->type() == QEvent::Show && CTabGroups::EditorNameOf( this ) != m_szEditor )
	{
		ApplyPreferences();
	}
	// Ctrl+F and F3 are the box's own while it has the focus, before a tab's or window's.
	if ( pEvent->type() == QEvent::ShortcutOverride )
	{
		auto *pKey = static_cast<QKeyEvent *>( pEvent );
		if ( pKey->matches( QKeySequence::Find ) || pKey->matches( QKeySequence::FindNext ) || pKey->matches( QKeySequence::FindPrevious ) || ( pKey->key() == Qt::Key_Escape && IsFindShown() ) )
		{
			pEvent->accept();
			return true;
		}
	}
	if ( pEvent->type() == QEvent::ToolTip )
	{
		auto *pHelp = static_cast<QHelpEvent *>( pEvent );
		const QPoint at = viewport()->mapFrom( this, pHelp->pos() );
		QStringList notes;
		if ( viewport()->rect().contains( at ) )
		{
			const QTextCursor cursor = cursorForPosition( at );
			// Past the end of a line, the cursor lands on its end: no note for empty space.
			const bool bOnText = cursorRect( cursor ).left() + fontMetrics().horizontalAdvance( u'M' ) >= at.x();
			if ( bOnText )
			{
				if ( const QString szProblems = DiagnosticsAt( cursor.blockNumber() + 1 ); !szProblems.isEmpty() )
				{
					notes.push_back( szProblems );
				}
				if ( m_pfnHover )
				{
					if ( const QString szNote = m_pfnHover( cursor ); !szNote.isEmpty() )
					{
						notes.push_back( szNote );
					}
				}
			}
		}
		notes.isEmpty() ? QToolTip::hideText() : QToolTip::showText( pHelp->globalPos(), notes.join( QStringLiteral( "\n\n" ) ), this );
		return true;
	}
	return QPlainTextEdit::event( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose: The popup's keys, Ctrl+Space, keeping the indent on new lines (and writing a block's braces), and suggestions as
//          words are typed.
//-----------------------------------------------------------------------------
void CCodeEdit::keyPressEvent( QKeyEvent *pEvent )
{
	QAbstractItemView *pPopup = m_pCompleter->popup();
	if ( pPopup->isVisible() )
	{
		switch ( pEvent->key() )
		{
			case Qt::Key_Enter:
			case Qt::Key_Return:
			case Qt::Key_Escape:
			case Qt::Key_Tab:
			case Qt::Key_Backtab:
				pEvent->ignore(); // the completer takes these
				return;
			default: break;
		}
	}
	if ( pEvent->key() == Qt::Key_Space && ( pEvent->modifiers() & Qt::ControlModifier ) )
	{
		Complete( true );
		return;
	}
	if ( pEvent->matches( QKeySequence::Find ) )
	{
		ShowFind();
		return;
	}
	if ( pEvent->matches( QKeySequence::FindNext ) || pEvent->matches( QKeySequence::FindPrevious ) )
	{
		if ( !IsFindShown() )
		{
			ShowFind();
		}
		FindNext( pEvent->matches( QKeySequence::FindPrevious ) );
		return;
	}
	if ( pEvent->key() == Qt::Key_Escape && IsFindShown() )
	{
		HideFind();
		return;
	}
	if ( isReadOnly() )
	{
		QPlainTextEdit::keyPressEvent( pEvent );
		return;
	}
	if ( ( pEvent->key() == Qt::Key_Return || pEvent->key() == Qt::Key_Enter ) && !( pEvent->modifiers() & ~Qt::KeypadModifier ) )
	{
		QTextCursor cursor = textCursor();
		const QString szLine = cursor.block().text();
		const QString szBefore = szLine.left( cursor.positionInBlock() );
		QString szIndent;
		for ( QChar c : szLine )
		{
			if ( c != u' ' && c != u'\t' )
			{
				break;
			}
			szIndent += c;
		}
		szIndent = szIndent.left( std::min<qsizetype>( szIndent.size(), szBefore.size() ) );
		const bool bAtEnd = szLine.mid( cursor.positionInBlock() ).trimmed().isEmpty();
		if ( bAtEnd && IsBlockWordLine( cursor ) )
		{
			OpenBlock( cursor, szIndent, true );
		}
		else if ( bAtEnd && szBefore.trimmed().endsWith( u'{' ) && IsBlockUnclosed() )
		{
			OpenBlock( cursor, szIndent, false );
		}
		else
		{
			if ( szBefore.trimmed().endsWith( u'{' ) )
			{
				szIndent += u'\t';
			}
			cursor.insertText( u'\n' + szIndent );
		}
		setTextCursor( cursor );
		ensureCursorVisible();
		pPopup->hide();
		return;
	}
	if ( pEvent->text() == QStringLiteral( "}" ) )
	{
		// A closing brace alone on its line goes back to its block's indent.
		QTextCursor cursor = textCursor();
		const QString szBefore = cursor.block().text().left( cursor.positionInBlock() );
		if ( !szBefore.isEmpty() && szBefore.trimmed().isEmpty() && szBefore.endsWith( u'\t' ) && !cursor.hasSelection() )
		{
			cursor.deletePreviousChar();
			setTextCursor( cursor );
		}
	}

	QPlainTextEdit::keyPressEvent( pEvent );

	const QString szTyped = pEvent->text();
	const bool bWordChar = szTyped.size() == 1 && IsCompletionChar( szTyped[ 0 ] ) && !( pEvent->modifiers() & Qt::ControlModifier );
	if ( bWordChar || ( pPopup->isVisible() && pEvent->key() == Qt::Key_Backspace ) )
	{
		Complete( false );
	}
	else if ( pPopup->isVisible() && !szTyped.isEmpty() )
	{
		pPopup->hide();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CCodeEdit::Complete( bool bAsked )
{
	QAbstractItemView *pPopup = m_pCompleter->popup();
	const QTextCursor cursor = textCursor();
	if ( !m_pfnCompletion || isReadOnly() || cursor.hasSelection() || ( !bAsked && !CPreferences::Instance().IsCodeCompletion( m_szEditor ) ) )
	{
		pPopup->hide();
		return;
	}
	m_Completion = m_pfnCompletion( cursor );
	if ( m_Completion.m_Words.isEmpty() || m_Completion.m_nStart < 0 || m_Completion.m_nStart > cursor.position() )
	{
		pPopup->hide();
		return;
	}
	QTextCursor typed( document() );
	typed.setPosition( m_Completion.m_nStart );
	typed.setPosition( cursor.position(), QTextCursor::KeepAnchor );
	const QString szPrefix = typed.selectedText();
	// Nothing typed yet: only when asked, or just inside a quote that was opened.
	const QChar chBefore = cursor.position() > 0 ? document()->characterAt( cursor.position() - 1 ) : QChar();
	if ( !bAsked && szPrefix.isEmpty() && chBefore != u'"' )
	{
		pPopup->hide();
		return;
	}
	m_pWords->setStringList( m_Completion.m_Words );
	m_pCompleter->setCompletionPrefix( szPrefix );
	const int nCount = m_pCompleter->completionCount();
	if ( nCount == 0 || ( !bAsked && nCount == 1 && m_pCompleter->currentCompletion().compare( szPrefix, Qt::CaseInsensitive ) == 0 ) )
	{
		pPopup->hide();
		return;
	}
	pPopup->setCurrentIndex( m_pCompleter->completionModel()->index( 0, 0 ) );
	QRect at = cursorRect();
	at.translate( viewport()->pos() );
	at.setWidth( std::min( 560, pPopup->sizeHintForColumn( 0 ) + pPopup->verticalScrollBar()->sizeHint().width() + 8 ) );
	m_pCompleter->complete( at );
}

//-----------------------------------------------------------------------------
// Purpose: The picked word in place of what was typed of it (and the rest of the word the cursor
//          was in), then its suffix.
//-----------------------------------------------------------------------------
void CCodeEdit::InsertCompletion( const QString &szWord )
{
	QTextCursor cursor = textCursor();
	if ( m_Completion.m_nStart < 0 || m_Completion.m_nStart > cursor.position() )
	{
		return;
	}
	int nEnd = cursor.position();
	const int nBlockEnd = cursor.block().position() + cursor.block().length() - 1;
	while ( nEnd < nBlockEnd )
	{
		const QChar c = document()->characterAt( nEnd );
		if ( c.isSpace() || c == u'"' || c == u'{' || c == u'}' || ( !m_Completion.m_szSuffix.isEmpty() && c == m_Completion.m_szSuffix[ 0 ] ) )
		{
			break;
		}
		nEnd++;
	}
	cursor.beginEditBlock();
	cursor.setPosition( m_Completion.m_nStart );
	cursor.setPosition( nEnd, QTextCursor::KeepAnchor );
	cursor.insertText( szWord );
	const QString &szSuffix = m_Completion.m_szSuffix;
	if ( !szSuffix.isEmpty() )
	{
		QTextCursor after( document() );
		after.setPosition( cursor.position() );
		after.setPosition( std::min( cursor.position() + static_cast<int>( szSuffix.size() ), document()->characterCount() - 1 ), QTextCursor::KeepAnchor );
		if ( after.selectedText() == szSuffix )
		{
			cursor.setPosition( after.position() );
		}
		else
		{
			cursor.insertText( szSuffix );
		}
	}
	if ( IsBlockWordLine( cursor ) && cursor.block().text().mid( cursor.positionInBlock() ).trimmed().isEmpty() )
	{
		const QString szLine = cursor.block().text();
		qsizetype nIndent = 0;
		while ( nIndent < szLine.size() && ( szLine[ nIndent ] == u' ' || szLine[ nIndent ] == u'\t' ) )
		{
			nIndent++;
		}
		OpenBlock( cursor, szLine.left( nIndent ), true );
	}
	cursor.endEditBlock();
	setTextCursor( cursor );
	m_Completion = {};
}

//-----------------------------------------------------------------------------
// Purpose: Whether the cursor's line, up to the cursor, is a block word alone, with no block
//          after it yet.
//-----------------------------------------------------------------------------
bool CCodeEdit::IsBlockWordLine( const QTextCursor &cursor ) const
{
	const QString szBefore = cursor.block().text().left( cursor.positionInBlock() ).trimmed();
	if ( szBefore.isEmpty() || !m_BlockWords.contains( szBefore, Qt::CaseInsensitive ) )
	{
		return false;
	}
	// Already followed by its brace (on the next line that isn't blank): nothing to add.
	for ( QTextBlock block = cursor.block().next(); block.isValid(); block = block.next() )
	{
		const QString szNext = block.text().trimmed();
		if ( !szNext.isEmpty() )
		{
			return !szNext.startsWith( u'{' );
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Whether the document has more opening braces than closing ones, outside quotes and
//          comments: a brace just typed whose block isn't closed yet.
//-----------------------------------------------------------------------------
bool CCodeEdit::IsBlockUnclosed() const
{
	int nDepth = 0;
	for ( QTextBlock block = document()->begin(); block.isValid(); block = block.next() )
	{
		const QString szLine = block.text();
		bool bQuoted = false;
		for ( qsizetype i = 0; i < szLine.size(); i++ )
		{
			const QChar c = szLine[ i ];
			if ( c == u'"' )
			{
				bQuoted = !bQuoted;
			}
			else if ( bQuoted )
			{
				continue;
			}
			else if ( c == u';' || c == u'#' || ( c == u'/' && i + 1 < szLine.size() && szLine[ i + 1 ] == u'/' ) )
			{
				break;
			}
			else if ( c == u'{' )
			{
				nDepth++;
			}
			else if ( c == u'}' )
			{
				nDepth--;
			}
		}
	}
	return nDepth > 0;
}

//-----------------------------------------------------------------------------
// Purpose: Writes a block's braces after the cursor (the opening one too, when bOpenBrace) at the
//          line's indent, and leaves the cursor on the indented line between them.
//-----------------------------------------------------------------------------
void CCodeEdit::OpenBlock( QTextCursor &cursor, const QString &szIndent, bool bOpenBrace )
{
	cursor.beginEditBlock();
	// Spaces typed after the word or brace would only trail.
	while ( cursor.positionInBlock() > 0 && document()->characterAt( cursor.position() - 1 ).isSpace() && cursor.block().text().left( cursor.positionInBlock() ).trimmed().size() > 0 )
	{
		cursor.deletePreviousChar();
	}
	cursor.movePosition( QTextCursor::EndOfBlock, QTextCursor::KeepAnchor );
	cursor.removeSelectedText();
	if ( bOpenBrace )
	{
		cursor.insertText( u'\n' + szIndent + u'{' );
	}
	cursor.insertText( u'\n' + szIndent + u'\t' );
	const int nInside = cursor.position();
	cursor.insertText( u'\n' + szIndent + u'}' );
	cursor.setPosition( nInside );
	cursor.endEditBlock();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CCodeHighlighter::CCodeHighlighter( CCodeEdit *pEdit )
	: QSyntaxHighlighter( pEdit->document() ),
	  m_pEdit( pEdit )
{
	// Colouring again isn't an edit: the box's textChanged would mark its file modified.
	connect( pEdit, &CCodeEdit::ColorsChanged, this, [ this ] {
		const QSignalBlocker block( m_pEdit );
		rehighlight();
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QTextCharFormat CCodeHighlighter::Format( const QColor &colour, bool bBold, bool bItalic ) const
{
	QTextCharFormat chars;
	chars.setForeground( colour );
	if ( bBold )
	{
		chars.setFontWeight( QFont::Bold );
	}
	chars.setFontItalic( bItalic );
	return chars;
}

//-----------------------------------------------------------------------------
// Purpose: The bar: what to look for, how many there are, previous, next, match case, close.
//-----------------------------------------------------------------------------
void CCodeEdit::MakeFindBar()
{
	auto *pBar = new QFrame( this );
	pBar->setObjectName( QStringLiteral( "codeFindBar" ) );
	pBar->setFrameShape( QFrame::StyledPanel );
	pBar->setAutoFillBackground( true );
	// The bar keeps the desktop's colours, whatever the box's scheme.
	pBar->setPalette( QPalette() );
	m_pFindBar = pBar;
	auto *pLayout = new QHBoxLayout( pBar );
	pLayout->setContentsMargins( 4, 4, 4, 4 );
	pLayout->setSpacing( 2 );
	m_pFindText = new QLineEdit;
	m_pFindText->setObjectName( QStringLiteral( "codeFindText" ) );
	m_pFindText->setPlaceholderText( tr( "Find" ) );
	m_pFindText->setClearButtonEnabled( true );
	m_pFindText->setMinimumWidth( 180 );
	m_pFindText->installEventFilter( this );
	m_pFindCount = new QLabel;
	m_pFindCount->setObjectName( QStringLiteral( "codeFindCount" ) );
	m_pFindCount->setMinimumWidth( m_pFindCount->fontMetrics().horizontalAdvance( QStringLiteral( "0000 of 0000" ) ) );
	auto button = [ & ]( const QString &szText, const QString &szTip, const QString &szName ) {
		auto *pButton = new QToolButton;
		pButton->setText( szText );
		pButton->setToolTip( szTip );
		pButton->setObjectName( szName );
		pButton->setAutoRaise( true );
		pButton->setFocusPolicy( Qt::NoFocus );
		return pButton;
	};
	QToolButton *pPrevious = button( QStringLiteral( "↑" ), tr( "Previous match (Shift+Enter, Shift+F3)" ), QStringLiteral( "codeFindPrevious" ) );
	QToolButton *pNext = button( QStringLiteral( "↓" ), tr( "Next match (Enter, F3)" ), QStringLiteral( "codeFindNext" ) );
	m_pFindCase = button( QStringLiteral( "Aa" ), tr( "Match case" ), QStringLiteral( "codeFindCase" ) );
	m_pFindCase->setCheckable( true );
	QToolButton *pClose = button( QStringLiteral( "✕" ), tr( "Close (Esc)" ), QStringLiteral( "codeFindClose" ) );
	pLayout->addWidget( m_pFindText, 1 );
	pLayout->addWidget( m_pFindCount );
	pLayout->addWidget( pPrevious );
	pLayout->addWidget( pNext );
	pLayout->addWidget( m_pFindCase );
	pLayout->addWidget( pClose );
	connect( pPrevious, &QToolButton::clicked, this, [ this ] { FindNext( true ); } );
	connect( pNext, &QToolButton::clicked, this, [ this ] { FindNext( false ); } );
	connect( pClose, &QToolButton::clicked, this, &CCodeEdit::HideFind );
	// Typing looks from where the selection starts, so the match grows in place as it's typed.
	auto search = [ this ] {
		UpdateFindMatches();
		const int nFrom = textCursor().selectionStart();
		const auto it = std::ranges::lower_bound( m_FindMatches, nFrom );
		SelectMatch( it != m_FindMatches.end() ? static_cast<int>( it - m_FindMatches.begin() ) : 0 );
		ScheduleMarks();
	};
	connect( m_pFindText, &QLineEdit::textEdited, this, search );
	connect( m_pFindCase, &QToolButton::toggled, this, search );
	pBar->hide();
}

//-----------------------------------------------------------------------------
// Purpose: Over the top right of the text, clear of the scroll bar.
//-----------------------------------------------------------------------------
void CCodeEdit::PlaceFindBar()
{
	if ( !m_pFindBar )
	{
		return;
	}
	const QRect area = viewport()->geometry();
	const QSize size = m_pFindBar->sizeHint();
	const int nWidth = std::min( size.width() + 80, std::max( area.width() - 8, 0 ) );
	m_pFindBar->setGeometry( area.right() - nWidth - 4, area.top() + 4, nWidth, size.height() );
	m_pFindBar->raise();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CCodeEdit::ShowFind()
{
	if ( !m_pFindBar )
	{
		MakeFindBar();
	}
	// A selection on one line is what to look for; a longer one is probably not.
	const QString szSelected = textCursor().selectedText();
	if ( !szSelected.isEmpty() && !szSelected.contains( QChar::ParagraphSeparator ) )
	{
		m_pFindText->setText( szSelected );
	}
	m_pFindBar->show();
	PlaceFindBar();
	m_pFindText->setFocus();
	m_pFindText->selectAll();
	ScheduleMarks();
}

//-----------------------------------------------------------------------------
// Purpose: Back to the text, the last match still selected.
//-----------------------------------------------------------------------------
void CCodeEdit::HideFind()
{
	if ( !IsFindShown() )
	{
		return;
	}
	m_pFindBar->hide();
	m_FindMatches.clear();
	setFocus();
	ScheduleMarks();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CCodeEdit::IsFindShown() const
{
	return m_pFindBar && !m_pFindBar->isHidden();
}

//-----------------------------------------------------------------------------
// Purpose: The document's text holds one character for each position (a line's end too), so
//          where a match is in it is where it is in the document.
//-----------------------------------------------------------------------------
void CCodeEdit::UpdateFindMatches()
{
	m_FindMatches.clear();
	const QString szFind = m_pFindText ? m_pFindText->text() : QString{};
	m_nFindLength = static_cast<int>( szFind.size() );
	if ( szFind.isEmpty() )
	{
		return;
	}
	const Qt::CaseSensitivity nCase = m_pFindCase->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
	const QString szText = document()->toPlainText();
	for ( qsizetype n = szText.indexOf( szFind, 0, nCase ); n >= 0; n = szText.indexOf( szFind, n + szFind.size(), nCase ) )
	{
		m_FindMatches.push_back( static_cast<int>( n ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: "3 of 12" when the selection is a match, else how many there are.
//-----------------------------------------------------------------------------
void CCodeEdit::UpdateFindCount()
{
	if ( !m_pFindCount )
	{
		return;
	}
	const QTextCursor cursor = textCursor();
	QString szCount;
	if ( m_pFindText->text().isEmpty() )
	{
		szCount.clear();
	}
	else if ( m_FindMatches.empty() )
	{
		szCount = tr( "No matches" );
	}
	else if ( const auto it = std::ranges::lower_bound( m_FindMatches, cursor.selectionStart() );
		it != m_FindMatches.end() && *it == cursor.selectionStart() && cursor.selectionEnd() - cursor.selectionStart() == m_nFindLength )
	{
		szCount = tr( "%1 of %2" ).arg( it - m_FindMatches.begin() + 1 ).arg( m_FindMatches.size() );
	}
	else
	{
		szCount = tr( "%n match(es)", nullptr, static_cast<int>( m_FindMatches.size() ) );
	}
	m_pFindCount->setText( szCount );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CCodeEdit::SelectMatch( int nMatch )
{
	if ( nMatch < 0 || nMatch >= static_cast<int>( m_FindMatches.size() ) )
	{
		UpdateFindCount();
		return;
	}
	QTextCursor cursor = textCursor();
	cursor.setPosition( m_FindMatches[ nMatch ] );
	cursor.setPosition( m_FindMatches[ nMatch ] + m_nFindLength, QTextCursor::KeepAnchor );
	setTextCursor( cursor );
	centerCursor();
	UpdateFindCount();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CCodeEdit::FindNext( bool bBackward )
{
	UpdateFindMatches();
	if ( m_FindMatches.empty() )
	{
		UpdateFindCount();
		return false;
	}
	const QTextCursor cursor = textCursor();
	int nMatch;
	if ( bBackward )
	{
		const auto it = std::ranges::lower_bound( m_FindMatches, cursor.selectionStart() );
		nMatch = it == m_FindMatches.begin() ? static_cast<int>( m_FindMatches.size() ) - 1 : static_cast<int>( it - m_FindMatches.begin() ) - 1;
	}
	else
	{
		// From the selection's start, past a match that is selected now.
		const int nFrom = cursor.hasSelection() ? cursor.selectionStart() + 1 : cursor.position();
		const auto it = std::ranges::lower_bound( m_FindMatches, nFrom );
		nMatch = it == m_FindMatches.end() ? 0 : static_cast<int>( it - m_FindMatches.begin() );
	}
	SelectMatch( nMatch );
	ScheduleMarks();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: The find bar's keys: Enter for the next match, Shift+Enter the one before, Esc
//          back to the text.
//-----------------------------------------------------------------------------
bool CCodeEdit::eventFilter( QObject *pObject, QEvent *pEvent )
{
	if ( pObject == m_pFindText && ( pEvent->type() == QEvent::KeyPress || pEvent->type() == QEvent::ShortcutOverride ) )
	{
		auto *pKey = static_cast<QKeyEvent *>( pEvent );
		const bool bReturn = pKey->key() == Qt::Key_Return || pKey->key() == Qt::Key_Enter;
		const bool bOurs = bReturn || pKey->key() == Qt::Key_Escape || pKey->matches( QKeySequence::FindNext ) || pKey->matches( QKeySequence::FindPrevious ) || pKey->matches( QKeySequence::Find );
		if ( !bOurs )
		{
			return QPlainTextEdit::eventFilter( pObject, pEvent );
		}
		// Taken here before a window's shortcut (Esc closing a dialog, say) can have them.
		pEvent->accept();
		if ( pEvent->type() == QEvent::ShortcutOverride )
		{
			return true;
		}
		if ( pKey->key() == Qt::Key_Escape )
		{
			HideFind();
		}
		else if ( pKey->matches( QKeySequence::Find ) )
		{
			m_pFindText->selectAll();
		}
		else
		{
			FindNext( ( bReturn && ( pKey->modifiers() & Qt::ShiftModifier ) ) || pKey->matches( QKeySequence::FindPrevious ) );
		}
		return true;
	}
	return QPlainTextEdit::eventFilter( pObject, pEvent );
}
