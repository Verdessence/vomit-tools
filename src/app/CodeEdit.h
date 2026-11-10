//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The text editors' box (Stripper configs, materials): line numbers down its left side,
//          the colour scheme and font from Preferences > Text Editor, problems underlined where
//          they are, words suggested as they're typed, and notes on what's under the pointer.
//
//===========================================================================//
#pragma once

#include <functional>
#include <vector>

#include <QColor>
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>

#include "Preferences.h"

class QCompleter;
class QLabel;
class QLineEdit;
class QStringListModel;
class QToolButton;

// What a scheme gives each part of the box and each kind of word.
struct CodeColors_t
{
	bool m_bDark = false;
	bool m_bOwnPalette = false; // false: the desktop's colours stay on the box itself
	QColor m_Background;
	QColor m_Text;
	QColor m_Selection;
	QColor m_SelectedText;
	QColor m_CurrentLine;
	QColor m_Gutter;
	QColor m_GutterText;
	QColor m_GutterCurrent;
	QColor m_Keyword;   // section words, block names
	QColor m_Key;       // keys, parameters
	QColor m_String;    // values
	QColor m_Number;
	QColor m_Comment;
	QColor m_Special;   // conditions, regular expressions
	QColor m_Brace;
	QColor m_Error;     // problems' underline and their mark by the line number
};

[[nodiscard]] CodeColors_t CodeColorsFor( CodeScheme_t nScheme, const QPalette &palette );
[[nodiscard]] QString CodeSchemeName( CodeScheme_t nScheme );

struct CodeDiagnostic_t
{
	int m_nLine = 1; // from 1
	QString m_szMessage;
};

// The words that may go at the cursor: they replace the text from m_nStart (a position in the
// document) to the cursor, and m_szSuffix follows unless the text already goes on with it.
struct CodeCompletion_t
{
	QStringList m_Words;
	int m_nStart = -1;
	QString m_szSuffix;
};

class CCodeEdit : public QPlainTextEdit
{
	Q_OBJECT

public:
	explicit CCodeEdit( QWidget *pParent = nullptr );

	[[nodiscard]] const CodeColors_t &GetColors() const { return m_Colors; }

	// Lines with problems, underlined, marked by their number and told when hovered.
	void SetDiagnostics( std::vector<CodeDiagnostic_t> diagnostics );
	[[nodiscard]] const std::vector<CodeDiagnostic_t> &GetDiagnostics() const { return m_Diagnostics; }

	// What may be typed at a cursor. Asked after each word character, a quote or a colon, and on
	// Ctrl+Space.
	void SetCompletionSource( std::function<CodeCompletion_t( const QTextCursor &cursor )> pfnSource ) { m_pfnCompletion = std::move( pfnSource ); }
	// A note on what's at a cursor (a section word, a key...); empty for none.
	void SetHoverSource( std::function<QString( const QTextCursor &cursor )> pfnSource ) { m_pfnHover = std::move( pfnSource ); }
	// Shows the suggestions for the cursor, if any. bAsked: Ctrl+Space, which also shows them with
	// nothing typed yet.
	void Complete( bool bAsked );
	// Words that open a block on the next line (Stripper's "modify:"...): Return after one alone on
	// its line, or picking one from the suggestions, writes its braces with the cursor inside.
	void SetBlockWords( QStringList words ) { m_BlockWords = std::move( words ); }
	[[nodiscard]] QCompleter *GetCompleter() const { return m_pCompleter; }

	// Ctrl+F: the find bar over the box's top right corner, holding the selection if there is one.
	void ShowFind();
	void HideFind();
	[[nodiscard]] bool IsFindShown() const;
	[[nodiscard]] QLineEdit *GetFindText() const { return m_pFindText; }
	// Selects the next match of the find bar's text after the selection (bBackward: the one
	// before it), going round past the end; false when there is none.
	bool FindNext( bool bBackward = false );
	// Every match of the find bar's text, as start positions in the document; empty while the
	// bar is hidden.
	[[nodiscard]] const std::vector<int> &GetFindMatches() const { return m_FindMatches; }

signals:
	void ColorsChanged();

protected:
	void resizeEvent( QResizeEvent *pEvent ) override;
	void keyPressEvent( QKeyEvent *pEvent ) override;
	bool event( QEvent *pEvent ) override;
	void changeEvent( QEvent *pEvent ) override;
	bool eventFilter( QObject *pObject, QEvent *pEvent ) override;

private:
	class CGutter;

	void ApplyPreferences();
	void ScheduleMarks();
	void UpdateMarks();
	void InsertCompletion( const QString &szWord );
	[[nodiscard]] bool IsBlockWordLine( const QTextCursor &cursor ) const;
	[[nodiscard]] bool IsBlockUnclosed() const;
	void OpenBlock( QTextCursor &cursor, const QString &szIndent, bool bOpenBrace );
	[[nodiscard]] int GutterWidth() const;
	[[nodiscard]] QString DiagnosticsAt( int nLine ) const;
	void PaintGutter( QPaintEvent *pEvent );
	void MakeFindBar();
	void PlaceFindBar();
	void UpdateFindMatches();
	void UpdateFindCount();
	void SelectMatch( int nMatch );

	CGutter *m_pGutter = nullptr;
	CodeColors_t m_Colors;
	bool m_bApplying = false;
	bool m_bMarksPending = false;
	QString m_szEditor; // the editor it sits in, for that editor's own settings (vomit-tools.nut)
	std::vector<CodeDiagnostic_t> m_Diagnostics;
	std::function<CodeCompletion_t( const QTextCursor & )> m_pfnCompletion;
	std::function<QString( const QTextCursor & )> m_pfnHover;
	QCompleter *m_pCompleter = nullptr;
	QStringListModel *m_pWords = nullptr;
	CodeCompletion_t m_Completion; // what the popup shows now
	QStringList m_BlockWords;
	QWidget *m_pFindBar = nullptr; // made at the first Ctrl+F
	QLineEdit *m_pFindText = nullptr;
	QToolButton *m_pFindCase = nullptr;
	QLabel *m_pFindCount = nullptr;
	std::vector<int> m_FindMatches;
	int m_nFindLength = 0;
};

// A highlighter whose colours come from its box's scheme, redrawn when the scheme changes.
class CCodeHighlighter : public QSyntaxHighlighter
{
public:
	explicit CCodeHighlighter( CCodeEdit *pEdit );

protected:
	[[nodiscard]] const CodeColors_t &Colors() const { return m_pEdit->GetColors(); }
	[[nodiscard]] QTextCharFormat Format( const QColor &colour, bool bBold = false, bool bItalic = false ) const;

private:
	CCodeEdit *m_pEdit;
};
