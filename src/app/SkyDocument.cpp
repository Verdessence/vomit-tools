// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkyDocument.h"

#include <utility>

#include <QUndoCommand>
#include <QUndoStack>

using xv::sky::Face;

// Swaps all six slots between two states: undo restores every face exactly.
class SkySlotsCommand : public QUndoCommand {
public:
	SkySlotsCommand(SkyDocument* document, std::array<SkyDocument::Slot, 6> before, std::array<SkyDocument::Slot, 6> after, const QString& text)
			: QUndoCommand(text)
			, document_(document)
			, before_(std::move(before))
			, after_(std::move(after)) {}

	void redo() override { document_->applySlots(after_); }
	void undo() override { document_->applySlots(before_); }

private:
	SkyDocument* document_;
	std::array<SkyDocument::Slot, 6> before_;
	std::array<SkyDocument::Slot, 6> after_;
};

SkyDocument::SkyDocument(QObject* parent)
		: QObject(parent)
		, undoStack_(new QUndoStack(this)) {
	connect(undoStack_, &QUndoStack::cleanChanged, this, &SkyDocument::updateModified);
}

SkyDocument::~SkyDocument() = default;

void SkyDocument::setName(const QString& name) {
	if (name == name_) {
		return;
	}
	name_ = name;
	settingChanged();
	emit nameChanged();
}

void SkyDocument::setFormat(xv::texture::VtfFormat format) {
	if (format != format_) {
		format_ = format;
		settingChanged();
		emit optionsChanged();
	}
}

void SkyDocument::setSize(int size) {
	if (size != size_) {
		size_ = size;
		settingChanged();
		emit optionsChanged();
	}
}

void SkyDocument::setMipmaps(bool mipmaps) {
	if (mipmaps != mipmaps_) {
		mipmaps_ = mipmaps;
		settingChanged();
		emit optionsChanged();
	}
}

void SkyDocument::setSlots(const std::array<std::optional<Slot>, 6>& changes, const QString& text) {
	std::array<Slot, 6> after = slots_;
	for (std::size_t i = 0; i < after.size(); i++) {
		if (changes[i]) {
			after[i] = *changes[i];
		}
	}
	undoStack_->push(new SkySlotsCommand(this, slots_, std::move(after), text));
}

void SkyDocument::setSlot(Face face, Slot slot, const QString& text) {
	std::array<std::optional<Slot>, 6> changes;
	changes[xv::sky::indexOf(face)] = std::move(slot);
	setSlots(changes, text);
}

void SkyDocument::clearFace(Face face) {
	if (slot(face).image) {
		setSlot(face, {}, tr("Clear %1").arg(QString::fromUtf8(xv::sky::suffix(face))));
	}
}

void SkyDocument::orient(Face face, xv::texture::Orientation change, const QString& text) {
	Slot changed = slot(face);
	if (!changed.image) {
		return;
	}
	changed.orientation = xv::texture::combined(changed.orientation, change);
	setSlot(face, std::move(changed), text);
}

void SkyDocument::applySlots(const std::array<Slot, 6>& all) {
	slots_ = all;
	cube_.reset();
	emit facesChanged();
}

const xv::sky::Cube& SkyDocument::cube() const {
	if (!cube_) {
		xv::sky::Cube cube;
		for (Face face : xv::sky::FACES) {
			const Slot& s = slot(face);
			if (s.image) {
				cube.face(face) = xv::texture::oriented(*s.image, s.orientation.quarterTurns, s.orientation.mirror);
				cube.transforms[xv::sky::indexOf(face)] = s.transform;
			}
		}
		cube_ = std::move(cube);
	}
	return *cube_;
}

bool SkyDocument::complete() const {
	return std::ranges::all_of(slots_, [](const Slot& s) { return s.image != nullptr; });
}

xv::sky::ExportOptions SkyDocument::exportOptions() const {
	return {.name = name_.toStdString(), .format = format_, .size = size_, .mipmaps = mipmaps_};
}

bool SkyDocument::isModified() const {
	return settingsChanged_ || !undoStack_->isClean();
}

void SkyDocument::markExported() {
	settingsChanged_ = false;
	undoStack_->setClean();
	updateModified();
}

void SkyDocument::settingChanged() {
	settingsChanged_ = true;
	updateModified();
}

void SkyDocument::updateModified() {
	if (isModified() != modified_) {
		modified_ = isModified();
		emit modifiedChanged(modified_);
	}
}
