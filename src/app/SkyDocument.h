// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <optional>

#include <QObject>
#include <QString>

#include "sky/Sky.h"
#include "texture/Image.h"
#include "texture/Vtf.h"

class QUndoStack;

// A sky being made: an image for each face (turned or mirrored as needed), the sky's name and
// how it's written. Face changes go through the undo stack.
class SkyDocument : public QObject {
	Q_OBJECT

public:
	struct Slot {
		std::shared_ptr<const xv::texture::Image> image; // as loaded; null for an empty face
		QString source;                                  // the file or game path it came from
		xv::texture::Orientation orientation;            // applied to image
		xv::sky::UvTransform transform;                  // from a stock material ($basetexturetransform)
	};

	explicit SkyDocument(QObject* parent = nullptr);
	~SkyDocument() override;

	[[nodiscard]] QString name() const { return name_; }
	void setName(const QString& name);

	[[nodiscard]] const Slot& slot(xv::sky::Face face) const { return slots_[xv::sky::indexOf(face)]; }
	// Undoable. Faces left as nullopt are unchanged; all the changes are one undo step.
	void setSlots(const std::array<std::optional<Slot>, 6>& changes, const QString& text);
	void setSlot(xv::sky::Face face, Slot slot, const QString& text);
	void clearFace(xv::sky::Face face);
	// Turns and/or mirrors a face further (undoable).
	void orient(xv::sky::Face face, xv::texture::Orientation change, const QString& text);

	// The faces as they will be written, for previews, checks and export.
	[[nodiscard]] const xv::sky::Cube& cube() const;
	[[nodiscard]] bool complete() const;

	// How it is written: VTF format, face size (0 keeps the images' own) and mipmaps.
	[[nodiscard]] xv::texture::VtfFormat format() const { return format_; }
	[[nodiscard]] int size() const { return size_; }
	[[nodiscard]] bool mipmaps() const { return mipmaps_; }
	void setFormat(xv::texture::VtfFormat format);
	void setSize(int size);
	void setMipmaps(bool mipmaps);
	[[nodiscard]] xv::sky::ExportOptions exportOptions() const;

	// Where Export writes when the sky came from a folder on disk: the folder holding its
	// materials/skybox (empty: the game's write folder). The last folder exported to, so replacing
	// files there again needs no confirmation.
	[[nodiscard]] const std::filesystem::path& exportFolder() const { return exportFolder_; }
	void setExportFolder(std::filesystem::path folder) { exportFolder_ = std::move(folder); }
	[[nodiscard]] const std::filesystem::path& lastExportFolder() const { return lastExportFolder_; }
	void setLastExportFolder(std::filesystem::path folder) { lastExportFolder_ = std::move(folder); }
	// What the sky was opened from (a folder and base name, or a game sky), to find it among open tabs.
	[[nodiscard]] QString origin() const { return origin_; }
	void setOrigin(const QString& origin) { origin_ = origin; }

	[[nodiscard]] QUndoStack* undoStack() const { return undoStack_; }
	// Changed since the last export (or since it was opened).
	[[nodiscard]] bool isModified() const;
	void markExported();

signals:
	void facesChanged();
	void nameChanged();
	void optionsChanged(); // format, size or mipmaps
	void modifiedChanged(bool modified);

private:
	friend class SkySlotsCommand;
	void applySlots(const std::array<Slot, 6>& all);
	void settingChanged();
	void updateModified();

	QString name_;
	std::array<Slot, 6> slots_;
	mutable std::optional<xv::sky::Cube> cube_;
	xv::texture::VtfFormat format_ = xv::texture::VtfFormat::BGR888;
	int size_ = 0;
	bool mipmaps_ = false;
	std::filesystem::path exportFolder_;
	std::filesystem::path lastExportFolder_;
	QString origin_;
	QUndoStack* undoStack_ = nullptr;
	bool settingsChanged_ = false; // name or options; they aren't undoable
	bool modified_ = false;        // as last signalled
};
