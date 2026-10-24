//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MaterialEditor.h.
//
//===========================================================================//
#include "MaterialEditor.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Language.h"
#include "MaterialForm.h"
#include "MaterialView.h"
#include "Preferences.h"
#include "kv1/Kv1.h"
#include "material/Parameters.h"
#include "util/Numbers.h"

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

// Colours a material's text: parameters, shader and block names, values (numbers apart),
// conditions such as [$X360], braces and comments.
class CVmtHighlighter : public QSyntaxHighlighter
{
public:
	explicit CVmtHighlighter( QPlainTextEdit *pEdit )
		: QSyntaxHighlighter( pEdit->document() )
	{
		const bool bDark = pEdit->palette().color( QPalette::Base ).lightness() < 128;
		const auto format = [ bDark ]( const char *pszLight, const char *pszDark, bool bBold = false, bool bItalic = false ) {
			QTextCharFormat chars;
			chars.setForeground( QColor( QString::fromLatin1( bDark ? pszDark : pszLight ) ) );
			if ( bBold )
			{
				chars.setFontWeight( QFont::Bold );
			}
			chars.setFontItalic( bItalic );
			return chars;
		};
		m_Parameter = format( "#1f5fbf", "#7cb7ff" );
		m_Block = format( "#8a3fbf", "#d7a3ff", true );
		m_String = format( "#2d7d2d", "#a5d68a" );
		m_Number = format( "#b35900", "#f0b070" );
		m_Comment = format( "#7a7f88", "#8a8f98", false, true );
		m_Condition = format( "#12827a", "#6fd3c7" );
		m_Brace = format( "#444444", "#cccccc", true );
	}

protected:
	void highlightBlock( const QString &szLine ) override
	{
		// The words of the line: the first of two is a key, the second its value; a word alone
		// names a block.
		struct Word_t
		{
			qsizetype m_nStart;
			qsizetype m_nLength;
		};
		std::vector<Word_t> words;
		qsizetype i = 0;
		const qsizetype n = szLine.size();
		while ( i < n )
		{
			const QChar c = szLine[ i ];
			if ( c.isSpace() )
			{
				i++;
				continue;
			}
			if ( c == u'/' && i + 1 < n && szLine[ i + 1 ] == u'/' )
			{
				setFormat( static_cast<int>( i ), static_cast<int>( n - i ), m_Comment );
				break;
			}
			if ( c == u'{' || c == u'}' )
			{
				setFormat( static_cast<int>( i ), 1, m_Brace );
				i++;
				continue;
			}
			if ( c == u'[' )
			{
				const qsizetype nEnd = szLine.indexOf( u']', i );
				const qsizetype nLength = ( nEnd < 0 ? n : nEnd + 1 ) - i;
				// [1 1 1] after a key is a value; [$X360] is a condition.
				if ( szLine.mid( i, nLength ).contains( u'$' ) || szLine.mid( i, nLength ).contains( u'!' ) )
				{
					setFormat( static_cast<int>( i ), static_cast<int>( nLength ), m_Condition );
				}
				else
				{
					words.push_back( { i, nLength } );
				}
				i += nLength;
				continue;
			}
			const qsizetype nStart = i;
			if ( c == u'"' )
			{
				const qsizetype nEnd = szLine.indexOf( u'"', i + 1 );
				i = nEnd < 0 ? n : nEnd + 1;
			}
			else
			{
				while ( i < n && !szLine[ i ].isSpace() && szLine[ i ] != u'"' && szLine[ i ] != u'{' && szLine[ i ] != u'}' )
				{
					i++;
				}
			}
			words.push_back( { nStart, i - nStart } );
		}
		for ( std::size_t w = 0; w < words.size(); w++ )
		{
			const auto [ nStart, nLength ] = words[ w ];
			QString szWord = szLine.mid( nStart, nLength );
			szWord.remove( u'"' );
			if ( w == 0 )
			{
				const bool bParameter = szWord.startsWith( u'$' ) || szWord.startsWith( u'%' );
				setFormat( static_cast<int>( nStart ), static_cast<int>( nLength ), bParameter ? m_Parameter : words.size() == 1 ? m_Block
																																 : m_Parameter );
				continue;
			}
			QString szNumbers = szWord;
			szNumbers.remove( u'[' ).remove( u']' ).remove( u'{' ).remove( u'}' );
			bool bNumbers = !szNumbers.trimmed().isEmpty();
			for ( const QString &szPart : szNumbers.split( u' ', Qt::SkipEmptyParts ) )
			{
				bNumbers = bNumbers && xv::ParseNumber<double>( szPart.toStdString() ).has_value();
			}
			setFormat( static_cast<int>( nStart ), static_cast<int>( nLength ), bNumbers ? m_Number : m_String );
		}
	}

private:
	QTextCharFormat m_Parameter;
	QTextCharFormat m_Block;
	QTextCharFormat m_String;
	QTextCharFormat m_Number;
	QTextCharFormat m_Comment;
	QTextCharFormat m_Condition;
	QTextCharFormat m_Brace;
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
	new CVmtHighlighter( m_pText );
	m_pForm = new CMaterialForm;
	m_pForm->SetText( szText );
	m_pStack = new QStackedWidget;
	m_pStack->addWidget( m_pForm );
	m_pStack->addWidget( m_pText );

	// Above it: list or text, and how the preview follows the list.
	m_pVisualButton = new QToolButton;
	m_pVisualButton->setObjectName( QStringLiteral( "materialVisualButton" ) );
	m_pVisualButton->setText( tr( "Visual" ) );
	m_pVisualButton->setToolTip( tr( "Each parameter with a box that suits it; right-click to add or remove" ) );
	m_pTextButton = new QToolButton;
	m_pTextButton->setObjectName( QStringLiteral( "materialTextButton" ) );
	m_pTextButton->setText( tr( "Raw" ) );
	m_pTextButton->setToolTip( tr( "The file's text, coloured" ) );
	auto *pModes = new QButtonGroup( this );
	for ( QToolButton *pButton : { m_pVisualButton, m_pTextButton } )
	{
		pButton->setCheckable( true );
		pButton->setAutoRaise( true );
		pModes->addButton( pButton );
	}
	m_pAdd = new QPushButton( tr( "Add Parameter…" ) );
	m_pAdd->setObjectName( QStringLiteral( "materialAddParameter" ) );
	m_pLive = new QCheckBox( tr( "Live preview" ) );
	m_pLive->setObjectName( QStringLiteral( "materialLive" ) );
	m_pLive->setToolTip( tr( "Redraw the preview at each edit in the list; off, it waits for Refresh" ) );
	m_pRefresh = new QPushButton( tr( "Refresh" ) );
	m_pRefresh->setObjectName( QStringLiteral( "materialRefresh" ) );
	m_pRefresh->setToolTip( tr( "Draw the preview from the material as it is now" ) );
	auto *pHeader = new QHBoxLayout;
	pHeader->setContentsMargins( 0, 0, 0, 0 );
	pHeader->addWidget( m_pVisualButton );
	pHeader->addWidget( m_pTextButton );
	pHeader->addSpacing( 8 );
	pHeader->addWidget( m_pAdd );
	auto *pDetail = new QPushButton( tr( "Open detail type" ) );
	pDetail->setObjectName( QStringLiteral( "openDetailType" ) );
	const auto updateDetail = [ this, pDetail ] {
		const auto material = xv::material::ParseMaterial( GetText().toStdString(), m_pfnRead );
		pDetail->setVisible( material && material->Has( "%detailtype" ) );
	};
	connect( m_pText, &QPlainTextEdit::textChanged, pDetail, updateDetail );
	connect( pDetail, &QPushButton::clicked, this, [ this ] {
		if ( const auto material = xv::material::ParseMaterial( GetText().toStdString(), m_pfnRead ) )
		{
			emit OpenDetailTypeRequested( QString::fromStdString( material->Value( "%detailtype" ) ) );
		}
	} );
	connect( m_pForm, &CMaterialForm::OpenDetailTypeRequested, this, &CMaterialEditor::OpenDetailTypeRequested );
	updateDetail();
	pHeader->addWidget( pDetail );
	pHeader->addStretch();
	pHeader->addWidget( m_pLive );
	pHeader->addWidget( m_pRefresh );

	auto *pEditorSide = new QWidget;
	auto *pEditorLayout = new QVBoxLayout( pEditorSide );
	pEditorLayout->setContentsMargins( 0, 0, 0, 0 );
	pEditorLayout->addLayout( pHeader );
	pEditorLayout->addWidget( m_pStack, 1 );

	// Below it: what is wrong or worth knowing, by line.
	m_pMessages = new QTreeWidget;
	m_pMessages->setObjectName( QStringLiteral( "materialMessages" ) );
	m_pMessages->setColumnCount( 2 );
	m_pMessages->setHeaderLabels( { tr( "Message" ), tr( "Line" ) } );
	m_pMessages->setRootIsDecorated( false );
	m_pMessages->setUniformRowHeights( true );
	m_pMessages->header()->setStretchLastSection( false );
	m_pMessages->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
	m_pMessages->header()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
	m_pMessageCount = new QLabel;
	m_pMessageCount->setObjectName( QStringLiteral( "materialMessageCount" ) );
	m_pMessagesPanel = new QWidget;
	auto *pMessagesLayout = new QVBoxLayout( m_pMessagesPanel );
	pMessagesLayout->setContentsMargins( 0, 0, 0, 0 );
	pMessagesLayout->setSpacing( 2 );
	pMessagesLayout->addWidget( m_pMessageCount );
	pMessagesLayout->addWidget( m_pMessages, 1 );

	m_pPreviewHost = new QWidget;
	auto *pPreviewLayout = new QVBoxLayout( m_pPreviewHost );
	pPreviewLayout->setContentsMargins( 0, 0, 0, 0 );

	auto *pSplitter = new QSplitter;
	pSplitter->addWidget( m_pPreviewHost );
	pSplitter->addWidget( pEditorSide );
	pSplitter->setStretchFactor( 0, 3 );
	pSplitter->setStretchFactor( 1, 2 );
	pSplitter->setChildrenCollapsible( false );
	pEditorSide->setMinimumWidth( 320 );
	pSplitter->setSizes( { 640, 460 } );
	auto *pOuter = new QSplitter( Qt::Vertical );
	pOuter->addWidget( pSplitter );
	pOuter->addWidget( m_pMessagesPanel );
	pOuter->setStretchFactor( 0, 1 );
	pOuter->setSizes( { 600, 130 } );
	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->addWidget( pOuter );

	m_pPause = new QTimer( this );
	m_pPause->setSingleShot( true );
	m_pPause->setInterval( 400 );
	connect( m_pPause, &QTimer::timeout, this, [ this ] {
		// The text always redraws after a pause; the list only when live.
		if ( !IsVisual() || CPreferences::Instance().IsMaterialLive() )
		{
			UpdatePreview();
		}
		else
		{
			CheckMaterial();
		}
	} );
	connect( m_pText, &QPlainTextEdit::textChanged, this, [ this ] {
		CheckText();
		if ( IsVisual() )
		{
			m_pForm->SetText( GetText() );
		}
		m_pPause->start();
	} );
	connect( m_pText->document(), &QTextDocument::modificationChanged, this, [ this ] { emit ModifiedChanged( IsModified() ); } );
	connect( m_pForm, &CMaterialForm::TextEdited, this, [ this ]( const QString &szNew ) {
		m_bFormEdit = true;
		SetText( szNew );
		m_bFormEdit = false;
	} );
	connect( m_pForm, &CMaterialForm::PreviewTogglesChanged, this, &CMaterialEditor::UpdatePreview );
	connect( m_pForm, &CMaterialForm::UndoRequested, m_pText, &QPlainTextEdit::undo );
	connect( m_pForm, &CMaterialForm::RedoRequested, m_pText, &QPlainTextEdit::redo );
	connect( m_pAdd, &QPushButton::clicked, m_pForm, &CMaterialForm::ChooseParameterToAdd );
	connect( m_pRefresh, &QPushButton::clicked, this, &CMaterialEditor::UpdatePreview );
	connect( m_pVisualButton, &QToolButton::clicked, this, [] { CPreferences::Instance().SetMaterialVisual( true ); } );
	connect( m_pTextButton, &QToolButton::clicked, this, [] { CPreferences::Instance().SetMaterialVisual( false ); } );
	connect( m_pLive, &QCheckBox::toggled, this, []( bool bOn ) { CPreferences::Instance().SetMaterialLive( bOn ); } );
	connect( &CPreferences::Instance(), &CPreferences::MaterialEditingChanged, this, &CMaterialEditor::ShowMode );
	connect( m_pMessages, &QTreeWidget::itemActivated, this, [ this ]( QTreeWidgetItem *pItem ) { GoToLine( pItem->data( 0, Qt::UserRole ).toInt() ); } );
	m_pForm->SetTextureIcons( [ this ]( const QString &szKey, QString *pDescription ) { return m_pPreview ? m_pPreview->TextureIcon( szKey.toStdString(), 32, pDescription ) : QIcon{}; } );
	ShowMode();
	CheckText();
	UpdatePreview();
}

//-----------------------------------------------------------------------------
// Purpose: The list or the text, as Preferences says, and what goes with each.
//-----------------------------------------------------------------------------
void CMaterialEditor::ShowMode()
{
	const bool bVisual = IsVisual();
	const bool bLive = CPreferences::Instance().IsMaterialLive();
	if ( bVisual )
	{
		m_pForm->SetText( GetText() );
	}
	m_pStack->setCurrentWidget( bVisual ? static_cast<QWidget *>( m_pForm ) : m_pText );
	m_pVisualButton->setChecked( bVisual );
	m_pTextButton->setChecked( !bVisual );
	m_pAdd->setVisible( bVisual );
	m_pLive->setVisible( bVisual );
	{
		const QSignalBlocker blocker{ m_pLive };
		m_pLive->setChecked( bLive );
	}
	m_pRefresh->setVisible( bVisual && !bLive );
	// Unticked parameters count in the list only.
	if ( m_pPreview && m_pForm->GetPreviewText() != GetText() )
	{
		UpdatePreview();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMaterialEditor::IsVisual() const
{
	return CPreferences::Instance().IsMaterialVisual();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CMaterialEditor::GetText() const
{
	return m_pText->toPlainText();
}

//-----------------------------------------------------------------------------
// Purpose: Replaces the text as an edit; only what changed is replaced, so the undo steps stay
//          small and the cursor stays put.
//-----------------------------------------------------------------------------
void CMaterialEditor::SetText( const QString &szText )
{
	const QString szOld = GetText();
	if ( szOld == szText )
	{
		return;
	}
	qsizetype nStart = 0;
	while ( nStart < szOld.size() && nStart < szText.size() && szOld[ nStart ] == szText[ nStart ] )
	{
		nStart++;
	}
	qsizetype nOldEnd = szOld.size();
	qsizetype nNewEnd = szText.size();
	while ( nOldEnd > nStart && nNewEnd > nStart && szOld[ nOldEnd - 1 ] == szText[ nNewEnd - 1 ] )
	{
		nOldEnd--;
		nNewEnd--;
	}
	QTextCursor cursor{ m_pText->document() };
	cursor.setPosition( static_cast<int>( nStart ) );
	cursor.setPosition( static_cast<int>( nOldEnd ), QTextCursor::KeepAnchor );
	cursor.insertText( szText.mid( nStart, nNewEnd - nStart ) );
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
// Purpose:
//-----------------------------------------------------------------------------
void CMaterialEditor::SetSurfaceProps( std::vector<Choice_t> surfaceProps )
{
	m_SurfaceProps = std::move( surfaceProps );
	m_pForm->SetSurfaceProps( m_SurfaceProps );
	CheckMaterial();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterialEditor::SetTextureNames( std::function<QStringList()> pfnNames )
{
	m_pForm->SetTextureNames( std::move( pfnNames ) );
}

//-----------------------------------------------------------------------------
// Purpose: Marks the lines the parser stumbles on (missing quotes, braces...).
//-----------------------------------------------------------------------------
void CMaterialEditor::CheckText()
{
	const auto parsed = xv::kv1::Parse( GetText().toStdString() );
	QList<QTextEdit::ExtraSelection> marks;
	m_ProblemLines.clear();
	QColor tint = palette().color( QPalette::Base );
	tint = QColor::fromRgbF( static_cast<float>( tint.redF() * 0.7 + 0.3 * 0.86 ), static_cast<float>( tint.greenF() * 0.7 + 0.3 * 0.2 ), static_cast<float>( tint.blueF() * 0.7 + 0.3 * 0.2 ) );
	for ( const xv::kv1::Diagnostic_t &diagnostic : parsed.m_Diagnostics )
	{
		const int nLine = static_cast<int>( diagnostic.m_nLine );
		m_ProblemLines << nLine;
		QTextEdit::ExtraSelection mark;
		mark.cursor = QTextCursor( m_pText->document()->findBlockByNumber( std::max( 0, nLine - 1 ) ) );
		mark.format.setBackground( tint );
		mark.format.setProperty( QTextFormat::FullWidthSelection, true );
		marks << mark;
	}
	m_pText->setExtraSelections( marks );
}

//-----------------------------------------------------------------------------
// Purpose: Messages: everything worth saying about the material, by line.
//-----------------------------------------------------------------------------
void CMaterialEditor::CheckMaterial()
{
	using Severity_t = xv::material::MaterialIssue_t::Severity_t;
	xv::material::MaterialLintContext_t context;
	context.m_pfnRead = m_pfnRead;
	for ( const Choice_t &surface : m_SurfaceProps )
	{
		context.m_SurfaceProps.push_back( surface.m_szValue.toLower().toStdString() );
	}
	const auto issues = xv::material::LintMaterial( GetText().toStdString(), MaterialParameters(), context );
	m_pMessages->clear();
	int nCounts[ 3 ] = {};
	for ( const xv::material::MaterialIssue_t &issue : issues )
	{
		const QStyle::StandardPixmap nIcon = issue.m_nSeverity == Severity_t::SEVERITY_ERROR ? QStyle::SP_MessageBoxCritical
			: issue.m_nSeverity == Severity_t::SEVERITY_WARNING                              ? QStyle::SP_MessageBoxWarning
																							 : QStyle::SP_MessageBoxInformation;
		auto *pItem = new QTreeWidgetItem( m_pMessages, { QString::fromStdString( issue.m_szMessage ), issue.m_nLine ? QString::number( issue.m_nLine ) : QString{} } );
		pItem->setIcon( 0, style()->standardIcon( nIcon ) );
		pItem->setData( 0, Qt::UserRole, static_cast<int>( issue.m_nLine ) );
		pItem->setData( 0, Qt::UserRole + 1, static_cast<int>( issue.m_nSeverity ) );
		pItem->setToolTip( 0, pItem->text( 0 ) );
		nCounts[ static_cast<int>( issue.m_nSeverity ) ]++;
	}
	QStringList counts;
	if ( nCounts[ 2 ] )
	{
		counts << tr( "%n error(s)", nullptr, nCounts[ 2 ] );
	}
	if ( nCounts[ 1 ] )
	{
		counts << tr( "%n warning(s)", nullptr, nCounts[ 1 ] );
	}
	if ( nCounts[ 0 ] )
	{
		counts << tr( "%n note(s)", nullptr, nCounts[ 0 ] );
	}
	m_pMessageCount->setText( tr( "<b>Messages</b> · %1" ).arg( counts.isEmpty() ? tr( "nothing to report" ) : counts.join( QStringLiteral( ", " ) ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: Shows a line: picked in the text, or its row in the list.
//-----------------------------------------------------------------------------
void CMaterialEditor::GoToLine( int nLine )
{
	if ( nLine <= 0 )
	{
		return;
	}
	if ( IsVisual() && !m_pForm->SelectLine( nLine ) )
	{
		// Not a row (a mistake in the text): the text shows it.
		CPreferences::Instance().SetMaterialVisual( false );
	}
	if ( !IsVisual() )
	{
		QTextCursor cursor( m_pText->document()->findBlockByNumber( nLine - 1 ) );
		m_pText->setTextCursor( cursor );
		m_pText->centerCursor();
		m_pText->setFocus();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Draws the preview from the text now, and checks it again.
//-----------------------------------------------------------------------------
void CMaterialEditor::UpdatePreview()
{
	m_pPause->stop();
	// The list's unticked parameters are left out of the preview, not the file.
	const QByteArray utf8 = ( IsVisual() ? m_pForm->GetPreviewText() : GetText() ).toUtf8();
	const std::span bytes{ reinterpret_cast<const std::byte *>( utf8.constData() ), static_cast<std::size_t>( utf8.size() ) };
	auto *pPreview = new CMaterialView( m_szGamePath, {}, bytes, m_pfnRead );
	pPreview->SetPreviewOnly( true );
	if ( m_pPreview )
	{
		m_pPreview->hide();
		m_pPreview->deleteLater();
	}
	m_pPreview = pPreview;
	m_pPreviewHost->layout()->addWidget( m_pPreview );
	// The texture rows' pictures come from the preview's textures.
	m_pForm->SetTextureIcons( [ this ]( const QString &szKey, QString *pDescription ) { return m_pPreview ? m_pPreview->TextureIcon( szKey.toStdString(), 32, pDescription ) : QIcon{}; } );
	CheckMaterial();
	emit PreviewUpdated();
}
