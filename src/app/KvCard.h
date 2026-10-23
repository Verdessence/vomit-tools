//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: One block drawn as a card: a tab with its name, a key/value grid under it, and its sub-
//          blocks as nested cards. Edits go straight to the document as undoable commands.
//
//===========================================================================//
#pragma once

#include <vector>

#include <QWidget>

#include "kv1/Kv1.h"
#include "sound/SoundLint.h"

class QLabel;
class QMenu;
class QTableWidget;
class CSoundDocument;
class CSoundIndex;
class CSoundPlayer;

// One block drawn as a card: a tab with its name, a key/value grid under it, and its
// sub-blocks as nested cards. Edits go straight to the document as undoable commands.
class CKvCard : public QWidget
{
	Q_OBJECT

public:
	// player may be null: no play buttons then.
	CKvCard( CSoundDocument &document, const CSoundIndex &index, CSoundPlayer *pPlayer, xv::sound::NodePath_t path, QWidget *pParent = nullptr );

	[[nodiscard]] const xv::sound::NodePath_t &GetPath() const { return m_Path; }

	// Tints the rows and tabs that issues point at, here and in nested cards.
	void ShowIssues( const std::vector<xv::sound::Issue_t> &issues );
	// Opens the value editor of the pair at path, if it is in this card or a nested one.
	bool BeginEdit( const xv::sound::NodePath_t &path );
	// The card for the block at path, or nullptr.
	[[nodiscard]] CKvCard *CardFor( const xv::sound::NodePath_t &path );
	// Plays this block with its settings; a soundscape entry plays as a whole soundscape.
	void Play();
	[[nodiscard]] bool GetPlayable() const;

protected:
	bool eventFilter( QObject *pWatched, QEvent *pEvent ) override;

signals:
	// A key or block was added; the editor reopens it for editing once the cards are rebuilt.
	void Added( const xv::sound::NodePath_t &path );

private:
	void BuildTable( const xv::kv1::CNode &node );
	void FillAddMenu( QMenu *pMenu );
	void AddKey( const QString &szKey );
	void AddBlock( const QString &szKey );
	void RemoveRow( int nRow );
	void Rename();
	[[nodiscard]] std::size_t GetPairInsertIndex() const;

	CSoundDocument &m_Document;
	const CSoundIndex &m_Index;
	CSoundPlayer *m_pPlayer;
	xv::sound::NodePath_t m_Path;
	QLabel *m_pTab = nullptr;
	QTableWidget *m_pTable = nullptr;
	std::vector<std::size_t> m_RowChild; // table row -> child index in the block
	std::vector<CKvCard *> m_Children;
	bool m_bFilling = false;
};

// Starting content for new blocks and entries, so they are valid as soon as they are added.
[[nodiscard]] xv::kv1::CNode BlockTemplate( const CSoundDocument &document, std::string_view szKey );
[[nodiscard]] xv::kv1::CNode EntryTemplate( const CSoundDocument &document, std::string_view szName );
[[nodiscard]] QString DefaultValue( const CSoundDocument &document, std::string_view szKey );
