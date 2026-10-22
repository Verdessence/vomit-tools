// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStyledItemDelegate>

#include "sound/SoundSchema.h"

class SoundDocument;
class SoundIndex;

// What a value means, for the grey column next to it: "140 dB · attenuation 0.22", "Tunnel Medium", ...
[[nodiscard]] QString describeValue(xv::sound::ValueKind kind, const QString& value);

// Editors for the key/value grid of one block: keys come from the block's schema, values get a
// dropdown of every constant that fits (CHAN_*, SNDLVL_*, DSP presets...) or a sound-file picker.
class SoundValueDelegate : public QStyledItemDelegate {
	Q_OBJECT

public:
	enum Column { KeyColumn, ValueColumn, MeaningColumn };

	SoundValueDelegate(const xv::sound::BlockDef* block, const SoundIndex& index, const SoundDocument& document, QObject* parent = nullptr);

	QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
	void setEditorData(QWidget* editor, const QModelIndex& index) const override;
	void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;

	[[nodiscard]] static xv::sound::ValueKind kindOf(const xv::sound::BlockDef* block, const QString& key);

private:
	const xv::sound::BlockDef* block_;
	const SoundIndex& index_;
	const SoundDocument& document_;
};
