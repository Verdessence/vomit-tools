//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What a value means, for the grey column next to it: "140 dB · attenuation 0.22",
//          "Tunnel Medium", ...
//
//===========================================================================//
#pragma once

#include <QString>
#include <QStyledItemDelegate>

#include "sound/SoundSchema.h"

class CSoundDocument;
class CSoundIndex;

[[nodiscard]] QString DescribeValue( xv::sound::ValueKind_t nKind, const QString &szValue );

// Editors for the key/value grid of one block: keys come from the block's schema, values get a
// dropdown of every constant that fits (CHAN_*, SNDLVL_*, DSP presets...) or a sound-file picker.
class CSoundValueDelegate : public QStyledItemDelegate
{
	Q_OBJECT

public:
	enum Column_t
	{
		COLUMN_KEY_COLUMN,
		COLUMN_VALUE_COLUMN,
		COLUMN_MEANING_COLUMN
	};

	CSoundValueDelegate( const xv::sound::BlockDef_t *pBlock, const CSoundIndex &index, const CSoundDocument &document, QObject *pParent = nullptr );

	QWidget *createEditor( QWidget *pParent, const QStyleOptionViewItem &option, const QModelIndex &index ) const override;
	void setEditorData( QWidget *pEditor, const QModelIndex &index ) const override;
	void setModelData( QWidget *pEditor, QAbstractItemModel *pModel, const QModelIndex &index ) const override;

	[[nodiscard]] static xv::sound::ValueKind_t KindOf( const xv::sound::BlockDef_t *pBlock, const QString &szKey );

private:
	const xv::sound::BlockDef_t *m_pBlock;
	const CSoundIndex &m_Index;
	const CSoundDocument &m_Document;
};
