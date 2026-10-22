// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileViews.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <QFileInfo>
#include <QFontMetricsF>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QListWidget>
#include <QLocale>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBlock>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "SoundPlayer.h"
#include "audio/Audio.h"
#include "dmx/Dmx.h"
#include "kv1/Kv1.h"
#include "sound/Wav.h"

namespace {

QString qs(std::string_view text) {
	return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QPlainTextEdit* makeTextView(const QString& text) {
	auto* view = new QPlainTextEdit;
	view->setReadOnly(true);
	view->setLineWrapMode(QPlainTextEdit::NoWrap);
	view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	view->setTabStopDistance(QFontMetricsF{view->font()}.horizontalAdvance(u' ') * 4);
	view->setPlainText(text);
	view->setAcceptDrops(false); // dropped files go to the main window, which opens them
	return view;
}

// The decoded sound's envelope, with the loop point marked.
class WaveformView : public QWidget {
public:
	WaveformView(std::shared_ptr<const xv::audio::Clip> clip, QWidget* parent = nullptr)
			: QWidget(parent) {
		setMinimumHeight(120);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		if (!clip) {
			return;
		}
		// Peaks per bucket; painting picks buckets per pixel column.
		constexpr std::size_t BUCKETS = 4000;
		const std::uint64_t frames = clip->frames();
		peaks_.assign(std::min<std::uint64_t>(BUCKETS, frames), {0.0f, 0.0f});
		for (std::uint64_t frame = 0; frame < frames; frame++) {
			auto& [low, high] = peaks_[static_cast<std::size_t>(frame * peaks_.size() / frames)];
			for (std::uint32_t c = 0; c < clip->channels; c++) {
				const float sample = clip->samples[frame * clip->channels + c];
				low = std::min(low, sample);
				high = std::max(high, sample);
			}
		}
		if (clip->loopStart) {
			loop_ = static_cast<double>(*clip->loopStart) / static_cast<double>(frames);
		}
	}

protected:
	void paintEvent(QPaintEvent*) override {
		QPainter painter{this};
		painter.fillRect(rect(), palette().base());
		const int middle = height() / 2;
		painter.setPen(palette().color(QPalette::Mid));
		painter.drawLine(0, middle, width(), middle);
		if (peaks_.empty()) {
			return;
		}
		painter.setPen(palette().color(QPalette::Highlight));
		for (int x = 0; x < width(); x++) {
			const std::size_t first = static_cast<std::size_t>(x) * peaks_.size() / static_cast<std::size_t>(width());
			const std::size_t last = std::max(first + 1, static_cast<std::size_t>(x + 1) * peaks_.size() / static_cast<std::size_t>(width()));
			float low = 0;
			float high = 0;
			for (std::size_t i = first; i < last && i < peaks_.size(); i++) {
				low = std::min(low, peaks_[i].first);
				high = std::max(high, peaks_[i].second);
			}
			painter.drawLine(x, middle - static_cast<int>(high * middle), x, middle - static_cast<int>(low * middle));
		}
		if (loop_) {
			const int x = static_cast<int>(*loop_ * width());
			painter.setPen(QPen{QColor{230, 140, 20}, 2});
			painter.drawLine(x, 0, x, height());
			painter.drawText(x + 4, 14, QObject::tr("loop"));
		}
	}

private:
	std::vector<std::pair<float, float>> peaks_;
	std::optional<double> loop_;
};

QString formatName(std::uint16_t format) {
	switch (format) {
		case 1:    return QObject::tr("PCM");
		case 2:    return QObject::tr("Microsoft ADPCM");
		case 3:    return QObject::tr("IEEE float");
		case 0x11: return QObject::tr("IMA ADPCM");
		case 0x55: return QObject::tr("MP3 in WAV");
		default:   return QObject::tr("format %1").arg(format);
	}
}

QString roundTripText(bool identical) {
	return identical ? QObject::tr("round-trip: byte-identical") : QObject::tr("<b>round-trip: CHANGED</b>");
}

} // namespace

KeyValuesView::KeyValuesView(const QString& path, std::span<const std::byte> bytes, QWidget* parent)
		: QWidget(parent) {
	const std::string text{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
	const auto result = xv::kv1::parse(text);
	const bool identical = xv::kv1::write(result.document) == text;
	const auto errors = std::ranges::count_if(result.diagnostics, [](const auto& d) {
		return d.severity == xv::kv1::Diagnostic::Severity::Error;
	});
	const auto warnings = static_cast<qsizetype>(result.diagnostics.size()) - errors;

	auto* summary = new QLabel(tr("<b>%1</b> · %2 top-level entries · %3 errors, %4 warnings · %5")
		.arg(path.toHtmlEscaped())
		.arg(result.document.roots.size())
		.arg(errors)
		.arg(warnings)
		.arg(roundTripText(identical)));

	auto* editor = makeTextView(qs(text));
	auto* diagnostics = new QListWidget;
	for (const auto& d : result.diagnostics) {
		const QString severity = d.severity == xv::kv1::Diagnostic::Severity::Error ? tr("error") : tr("warning");
		auto* item = new QListWidgetItem(QStringLiteral("%1:%2  %3: %4").arg(d.line).arg(d.column).arg(severity, qs(d.message)), diagnostics);
		item->setData(Qt::UserRole, static_cast<int>(d.line));
	}
	connect(diagnostics, &QListWidget::itemActivated, editor, [editor](QListWidgetItem* item) {
		const QTextBlock block = editor->document()->findBlockByLineNumber(item->data(Qt::UserRole).toInt() - 1);
		editor->setTextCursor(QTextCursor{block});
		editor->centerCursor();
		editor->setFocus();
	});
	diagnostics->setVisible(!result.diagnostics.empty());

	auto* splitter = new QSplitter(Qt::Vertical);
	splitter->addWidget(editor);
	splitter->addWidget(diagnostics);
	splitter->setStretchFactor(0, 4);
	splitter->setStretchFactor(1, 1);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(summary);
	layout->addWidget(splitter);
}

DmxView::DmxView(const QString& path, std::span<const std::byte> bytes, QWidget* parent)
		: QWidget(parent) {
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	const auto result = xv::dmx::read(bytes);
	if (!result.document) {
		layout->addWidget(new QLabel(tr("<b>%1</b> · could not read: %2").arg(path.toHtmlEscaped(), qs(result.error).toHtmlEscaped())));
		layout->addStretch();
		return;
	}
	const xv::dmx::Document& doc = *result.document;
	const auto written = xv::dmx::writeBinary(doc);
	const bool identical = written && std::ranges::equal(*written, bytes);

	layout->addWidget(new QLabel(tr("<b>%1</b> · binary %2, format %3 %4 · %5 elements · %6")
		.arg(path.toHtmlEscaped())
		.arg(doc.encodingVersion)
		.arg(qs(doc.format))
		.arg(doc.formatVersion)
		.arg(doc.elements.size())
		.arg(roundTripText(identical))));

	auto* tree = new QTreeWidget;
	tree->setHeaderLabels({tr("Name"), tr("Type"), tr("Value")});
	tree->setUniformRowHeights(true);

	// Each element is expanded once, at its shallowest reference (found breadth-first), so particle
	// systems sit in the root's list rather than under whichever system uses them as a child.
	// Every other reference just points at it.
	struct Home {
		std::int32_t element = -1;
		const xv::dmx::Attribute* attribute = nullptr;
		std::size_t position = 0;
	};
	std::vector<std::optional<Home>> homes(doc.elements.size());
	std::deque<std::int32_t> queue;
	if (!doc.elements.empty()) {
		homes[0] = Home{};
		queue.push_back(0);
	}
	while (!queue.empty()) {
		const std::int32_t index = queue.front();
		queue.pop_front();
		for (const xv::dmx::Attribute& attribute : doc.elements[static_cast<std::size_t>(index)].attributes) {
			const auto claim = [&](const xv::dmx::ElementRef& ref, std::size_t position) {
				if (ref.index >= 0 && !homes[static_cast<std::size_t>(ref.index)]) {
					homes[static_cast<std::size_t>(ref.index)] = Home{index, &attribute, position};
					queue.push_back(ref.index);
				}
			};
			if (const auto* ref = std::get_if<xv::dmx::ElementRef>(&attribute.value)) {
				claim(*ref, 0);
			} else if (const auto* refs = std::get_if<std::vector<xv::dmx::ElementRef>>(&attribute.value)) {
				for (std::size_t i = 0; i < refs->size(); i++) {
					claim((*refs)[i], i);
				}
			}
		}
	}
	const auto isHome = [&homes](std::int32_t index, std::int32_t owner, const xv::dmx::Attribute& attribute, std::size_t position) {
		const auto& home = homes[static_cast<std::size_t>(index)];
		return home && home->element == owner && home->attribute == &attribute && home->position == position;
	};

	std::function<void(QTreeWidgetItem*, std::int32_t, const QString&, bool)> addElement;
	addElement = [&](QTreeWidgetItem* item, std::int32_t index, const QString& label, bool expand) {
		const xv::dmx::Element& element = doc.elements[static_cast<std::size_t>(index)];
		item->setText(0, label.isEmpty() ? qs(element.name) : QStringLiteral("%1: %2").arg(label, qs(element.name)));
		item->setText(1, qs(element.type));
		if (!expand) {
			item->setText(2, tr("→ element #%1").arg(index));
			return;
		}
		for (const xv::dmx::Attribute& attribute : element.attributes) {
			const auto* ref = std::get_if<xv::dmx::ElementRef>(&attribute.value);
			if (ref && ref->index >= 0) {
				addElement(new QTreeWidgetItem(item), ref->index, qs(attribute.name), isHome(ref->index, index, attribute, 0));
				continue;
			}
			auto* row = new QTreeWidgetItem(item, {qs(attribute.name), qs(xv::dmx::typeName(attribute.type))});
			const auto* refs = std::get_if<std::vector<xv::dmx::ElementRef>>(&attribute.value);
			if (!refs) {
				row->setText(2, qs(xv::dmx::valueText(attribute.value)));
				continue;
			}
			row->setText(2, tr("%n element(s)", nullptr, static_cast<int>(refs->size())));
			for (std::size_t i = 0; i < refs->size(); i++) {
				const QString position = QStringLiteral("[%1]").arg(i);
				const xv::dmx::ElementRef& child = (*refs)[i];
				if (child.index >= 0) {
					addElement(new QTreeWidgetItem(row), child.index, position, isHome(child.index, index, attribute, i));
				} else {
					new QTreeWidgetItem(row, {position, QStringLiteral("element"), qs(xv::dmx::valueText(xv::dmx::Value{child}))});
				}
			}
		}
	};
	if (!doc.elements.empty()) {
		addElement(new QTreeWidgetItem(tree), 0, {}, true);
	}
	for (std::size_t i = 0; i < doc.elements.size(); i++) {
		if (!homes[i]) {
			addElement(new QTreeWidgetItem(tree), static_cast<std::int32_t>(i), tr("unreferenced"), true);
		}
	}
	tree->expandToDepth(1);
	tree->resizeColumnToContents(0);
	tree->resizeColumnToContents(1);

	auto* tabs = new QTabWidget;
	tabs->addTab(tree, tr("Elements"));
	tabs->addTab(makeTextView(qs(xv::dmx::writeKeyValues2(doc))), tr("keyvalues2"));
	layout->addWidget(tabs);
}

AudioView::AudioView(const QString& path, const QString& source, std::span<const std::byte> bytes, SoundPlayer* player, QWidget* parent)
		: QWidget(parent) {
	std::string error;
	// Huge files (music) are decoded only when played, not just for the picture.
	const auto clip = bytes.size() <= 64u * 1024 * 1024 ? xv::audio::decode(bytes, &error) : nullptr;
	const auto info = xv::sound::readWavInfo(bytes);

	QStringList details;
	if (info) {
		details << QStringLiteral("%1, %2-bit, %3 Hz, %4").arg(formatName(info->format)).arg(info->bitsPerSample).arg(info->sampleRate)
			.arg(info->channels == 1 ? tr("mono") : tr("%n channel(s)", nullptr, info->channels));
	} else if (clip) {
		details << QStringLiteral("MP3, %1 Hz, %2").arg(clip->sampleRate).arg(clip->channels == 1 ? tr("mono") : tr("stereo"));
	}
	if (clip) {
		details << tr("%1 s").arg(clip->seconds(), 0, 'f', 2);
		details << (clip->loopStart ? tr("loops from %1 s").arg(static_cast<double>(*clip->loopStart) / clip->sampleRate, 0, 'f', 2)
		                           : tr("no loop point: plays once even in playlooping"));
	} else if (!error.empty()) {
		details << tr("can't decode: %1").arg(QString::fromStdString(error));
	}

	auto* layout = new QVBoxLayout(this);
	auto* title = new QLabel(tr("<b>%1</b>%2").arg(path.toHtmlEscaped(), source.isEmpty() ? QString{} : tr(" · read from %1").arg(source.toHtmlEscaped())));
	title->setWordWrap(true);
	layout->addWidget(title);
	layout->addWidget(new QLabel(details.join(QStringLiteral(" · "))));

	if (player) {
		const auto data = std::make_shared<std::vector<std::byte>>(bytes.begin(), bytes.end());
		auto* buttons = new QHBoxLayout;
		auto* play = new QPushButton(tr("▶ Play"));
		auto* loop = new QPushButton(tr("⟳ Play Looped"));
		auto* stop = new QPushButton(tr("■ Stop"));
		loop->setToolTip(tr("Loop the way playlooping does: from the loop point (or the start) to the end"));
		connect(play, &QPushButton::clicked, this, [player, path, data] { player->playData(path, *data, false); });
		connect(loop, &QPushButton::clicked, this, [player, path, data] { player->playData(path, *data, true); });
		connect(stop, &QPushButton::clicked, player, &SoundPlayer::stop);
		buttons->addWidget(play);
		buttons->addWidget(loop);
		buttons->addWidget(stop);
		buttons->addStretch();
		layout->addLayout(buttons);
	}
	layout->addWidget(new WaveformView(clip), 1);
}

FileInfoView::FileInfoView(const QString& path, const QString& source, std::size_t size, QWidget* parent)
		: QWidget(parent) {
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(new QLabel(tr("<b>%1</b><br>Read from %2<br>%3")
		.arg(path.toHtmlEscaped(), source.toHtmlEscaped(), QLocale{}.formattedDataSize(static_cast<qint64>(size)))));
	layout->addWidget(new QLabel(tr("No viewer for this file type yet.")));
	layout->addStretch();
}

QWidget* createFileView(const QString& path, const QString& source, std::span<const std::byte> bytes, SoundPlayer* player, QWidget* parent) {
	const QString suffix = QFileInfo{path}.suffix().toLower();
	if (suffix == u"wav" || suffix == u"mp3") {
		return new AudioView(path, source, bytes, player, parent);
	}
	if (suffix == u"pcf" || suffix == u"dmx") {
		return new DmxView(path, bytes, parent);
	}
	if (suffix == u"txt" || suffix == u"vmt" || suffix == u"vdf" || suffix == u"res") {
		return new KeyValuesView(path, bytes, parent);
	}
	return new FileInfoView(path, source, bytes.size(), parent);
}
