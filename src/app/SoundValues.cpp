// SPDX-License-Identifier: GPL-3.0-or-later
#include "SoundValues.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include <QComboBox>
#include <QCompleter>
#include <QHeaderView>
#include <QLineEdit>
#include <QStandardItemModel>
#include <QTreeView>

#include "SoundDocument.h"
#include "SoundIndex.h"
#include "sound/SoundConstants.h"

using namespace xv::sound;

namespace {

QString qs(std::string_view text) {
	return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString formatNumber(double value) {
	return QString::number(value, 'g', 4);
}

std::optional<double> toNumber(const QString& text) {
	bool ok = false;
	const double value = text.trimmed().toDouble(&ok);
	return ok ? std::optional{value} : std::nullopt;
}

QStringList rangeParts(const QString& text) {
	QStringList parts = text.split(u',');
	for (QString& part : parts) {
		part = part.trimmed();
	}
	return parts;
}

struct Choice {
	QString value;
	QString meaning;
};

std::vector<Choice> constantChoices(std::span<const Constant> table) {
	std::vector<Choice> choices;
	for (const Constant& constant : table) {
		choices.push_back({qs(constant.name), qs(constant.note)});
	}
	return choices;
}

std::vector<Choice> choicesFor(ValueKind kind) {
	switch (kind) {
		case ValueKind::Channel:
			return constantChoices(channels());
		case ValueKind::Volume:
			return constantChoices(volumes());
		case ValueKind::Pitch:
			return constantChoices(pitches());
		case ValueKind::Attenuation:
			return constantChoices(attenuations());
		case ValueKind::SoundLevel: {
			std::vector<Choice> choices;
			for (const Constant& level : soundLevels()) {
				const QString note = level.note.empty() ? QString{} : QStringLiteral(" · %1").arg(qs(level.note));
				choices.push_back({qs(level.name), QStringLiteral("%1 dB%2").arg(level.value).arg(note)});
			}
			return choices;
		}
		case ValueKind::DspPreset: {
			std::vector<Choice> choices;
			for (const Constant& preset : dspPresets()) {
				choices.push_back({QString::number(static_cast<int>(preset.value)), qs(preset.name)});
			}
			return choices;
		}
		case ValueKind::Position:
		case ValueKind::RandomPosition: {
			std::vector<Choice> choices;
			for (int i = 0; i < 8; i++) {
				choices.push_back({QString::number(i), QObject::tr("env_soundscape's position%1 target").arg(i)});
			}
			if (kind == ValueKind::RandomPosition) {
				choices.push_back({QStringLiteral("random"), QObject::tr("Random spots around the listener")});
			}
			return choices;
		}
		case ValueKind::Bool:
			return {{QStringLiteral("0"), QObject::tr("No")}, {QStringLiteral("1"), QObject::tr("Yes")}};
		default:
			return {};
	}
}

// An editable dropdown whose popup lists each choice next to what it means.
QComboBox* makeChoiceCombo(const std::vector<Choice>& choices, QWidget* parent) {
	auto* combo = new QComboBox(parent);
	combo->setEditable(true);
	combo->setInsertPolicy(QComboBox::NoInsert);

	auto* model = new QStandardItemModel(0, 2, combo);
	for (const Choice& choice : choices) {
		auto* meaning = new QStandardItem(choice.meaning);
		meaning->setEnabled(false);
		model->appendRow({new QStandardItem(choice.value), meaning});
	}
	auto* view = new QTreeView;
	view->setHeaderHidden(true);
	view->setRootIsDecorated(false);
	view->setUniformRowHeights(true);
	view->setAllColumnsShowFocus(true);
	combo->setModel(model);
	combo->setView(view);
	combo->setModelColumn(0);
	view->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	view->setMinimumWidth(460);

	combo->completer()->setCompletionMode(QCompleter::PopupCompletion);
	combo->completer()->setFilterMode(Qt::MatchContains);
	combo->completer()->setCaseSensitivity(Qt::CaseInsensitive);
	return combo;
}

QString betweenText(const QString& low, const QString& high) {
	return QObject::tr("random from %1 to %2").arg(low, high);
}

} // namespace

QString describeValue(ValueKind kind, const QString& value) {
	const QString text = value.trimmed();
	const QStringList range = rangeParts(text);
	const bool isRange = range.size() == 2;
	const std::string utf8 = text.toStdString();

	switch (kind) {
		case ValueKind::Channel: {
			if (const Constant* channel = findConstant(channels(), utf8)) {
				return qs(channel->note);
			}
			if (const auto number = toNumber(text)) {
				for (const Constant& channel : channels()) {
					if (channel.value == *number) {
						return qs(channel.name);
					}
				}
			}
			return {};
		}
		case ValueKind::Volume:
		case ValueKind::Pitch: {
			const auto table = kind == ValueKind::Volume ? volumes() : pitches();
			const auto resolve = [table](const QString& part) {
				const Constant* constant = findConstant(table, part.toStdString());
				return constant ? formatNumber(constant->value) : part;
			};
			if (isRange) {
				return betweenText(resolve(range[0]), resolve(range[1]));
			}
			if (const Constant* constant = findConstant(table, utf8)) {
				return formatNumber(constant->value);
			}
			return {};
		}
		case ValueKind::NumberRange:
			return isRange ? betweenText(range[0], range[1]) : QString{};
		case ValueKind::SoundLevel: {
			const auto decibels = [](const QString& part) {
				const auto level = parseSoundLevel(part.toStdString());
				return level ? QObject::tr("%1 dB").arg(*level) : part;
			};
			if (isRange) {
				return betweenText(decibels(range[0]), decibels(range[1]));
			}
			if (const auto level = parseSoundLevel(utf8)) {
				if (*level == 0) {
					return QObject::tr("no falloff, heard everywhere");
				}
				return QObject::tr("%1 dB · attenuation %2").arg(*level).arg(formatNumber(soundLevelToAttenuation(*level)));
			}
			return {};
		}
		case ValueKind::Attenuation: {
			const Constant* constant = findConstant(attenuations(), utf8);
			const auto attenuation = constant ? std::optional{constant->value} : toNumber(text);
			if (!attenuation) {
				return {};
			}
			if (*attenuation == 0) {
				return QObject::tr("no falloff, heard everywhere");
			}
			return QObject::tr("about %1 dB").arg(std::round(attenuationToSoundLevel(*attenuation)));
		}
		case ValueKind::DspPreset: {
			const auto number = toNumber(text);
			if (!number) {
				return {};
			}
			for (const Constant& preset : dspPresets()) {
				if (preset.value == *number) {
					return qs(preset.name);
				}
			}
			return QObject::tr("not a stock preset");
		}
		case ValueKind::Position:
		case ValueKind::RandomPosition:
			if (text.compare(u"random", Qt::CaseInsensitive) == 0) {
				return QObject::tr("random spots around the listener");
			}
			return toNumber(text) ? QObject::tr("position%1 target").arg(text) : QString{};
		case ValueKind::Wave: {
			QStringList meanings;
			for (qsizetype i = 0; i < 2 && i < text.size(); i++) {
				const auto flags = pathFlags();
				const auto it = std::ranges::find(flags, text[i].toLatin1(), &PathFlag::character);
				if (it == flags.end()) {
					break;
				}
				meanings << qs(it->meaning);
			}
			return meanings.join(QStringLiteral(" · "));
		}
		case ValueKind::Bool:
			return text == u"1" ? QObject::tr("yes") : text == u"0" ? QObject::tr("no") : QString{};
		default:
			return {};
	}
}

SoundValueDelegate::SoundValueDelegate(const BlockDef* block, const SoundIndex& index, const SoundDocument& document, QObject* parent)
		: QStyledItemDelegate(parent)
		, block_(block)
		, index_(index)
		, document_(document) {}

ValueKind SoundValueDelegate::kindOf(const BlockDef* block, const QString& key) {
	if (!block) {
		return ValueKind::Text;
	}
	const KeyDef* def = findKey(*block, key.toStdString());
	return def ? def->kind : ValueKind::Text;
}

QWidget* SoundValueDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& index) const {
	if (index.column() == KeyColumn) {
		std::vector<Choice> keys;
		if (block_) {
			for (const KeyDef& key : block_->keys) {
				keys.push_back({qs(key.key), qs(key.help)});
			}
		}
		return makeChoiceCombo(keys, parent);
	}
	if (index.column() != ValueColumn) {
		return nullptr;
	}

	const ValueKind kind = kindOf(block_, index.siblingAtColumn(KeyColumn).data().toString());
	if (kind == ValueKind::Wave) {
		auto* edit = new QLineEdit(parent);
		auto* completer = new QCompleter(index_.soundFiles(), edit);
		completer->setCaseSensitivity(Qt::CaseInsensitive);
		completer->setFilterMode(Qt::MatchContains);
		completer->setMaxVisibleItems(16);
		edit->setCompleter(completer);
		edit->setPlaceholderText(tr("Type to search sound files"));
		return edit;
	}
	if (kind == ValueKind::SoundscapeName) {
		std::vector<Choice> names;
		for (const QString& name : index_.soundscapeNames(&document_)) {
			names.push_back({name, {}});
		}
		return makeChoiceCombo(names, parent);
	}
	if (const auto choices = choicesFor(kind); !choices.empty()) {
		return makeChoiceCombo(choices, parent);
	}
	return new QLineEdit(parent);
}

void SoundValueDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const {
	const QString text = index.data(Qt::EditRole).toString();
	if (auto* combo = qobject_cast<QComboBox*>(editor)) {
		combo->setCurrentIndex(-1);
		combo->setEditText(text);
	} else if (auto* edit = qobject_cast<QLineEdit*>(editor)) {
		edit->setText(text);
	}
}

void SoundValueDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const {
	if (auto* combo = qobject_cast<QComboBox*>(editor)) {
		model->setData(index, combo->currentText(), Qt::EditRole);
	} else if (auto* edit = qobject_cast<QLineEdit*>(editor)) {
		model->setData(index, edit->text(), Qt::EditRole);
	}
}
