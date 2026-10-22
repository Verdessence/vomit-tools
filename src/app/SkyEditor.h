// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <optional>
#include <vector>

#include <QWidget>

#include "sky/Sky.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class SkyDocument;
class SkyView;

// The skybox creator: six face images, laid out as they join, a view from inside, and checks for
// what would go wrong in game (missing or mismatched faces, faces turned the wrong way, sizes the
// game can't use).
class SkyEditor : public QWidget {
	Q_OBJECT

public:
	struct Issue {
		enum class Severity { Info, Warning, Error };
		Severity severity = Severity::Info;
		QString message;
		std::optional<xv::sky::OrientationFix> fix; // applied by double-clicking the issue
	};

	// Takes ownership of document.
	explicit SkyEditor(SkyDocument* document, QWidget* parent = nullptr);

	[[nodiscard]] SkyDocument* document() const { return document_; }
	[[nodiscard]] SkyView* view() const { return view_; }

	// Puts each file on the face its name ends with (…rt.png, …UP.tga), as one undo step. A single
	// file dropped into an empty sky brings the other faces beside it along. Returns the files that
	// couldn't be placed or read, with the reason.
	QStringList loadFiles(const QStringList& paths);
	bool loadFace(xv::sky::Face face, const QString& path);
	// Fills every face with a labelled pattern for checking orientation in game.
	void useTestPattern(int size = 512);
	void applyFix(const xv::sky::OrientationFix& fix);

	[[nodiscard]] const std::vector<Issue>& issues() const { return issues_; }
	// No errors: every face present, a valid name, sizes the game can use.
	[[nodiscard]] bool canExport() const;
	// After an export: where the files went and how to see the sky in game.
	void showExported(const QString& folder);

signals:
	void exportRequested();   // to the game's write folder
	void exportToRequested(); // to a folder the user picks

protected:
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dropEvent(QDropEvent* event) override;

private:
	void browseFace(xv::sky::Face face);
	void browseFiles();
	void facesChanged();
	void relint();
	void updateSizeEstimate();

	SkyDocument* document_ = nullptr;
	SkyView* view_ = nullptr;
	std::array<QWidget*, 6> slots_{};
	QLineEdit* name_ = nullptr;
	QComboBox* format_ = nullptr;
	QComboBox* size_ = nullptr;
	QCheckBox* mipmaps_ = nullptr;
	QLabel* estimate_ = nullptr;
	QLabel* exported_ = nullptr;
	QListWidget* issueList_ = nullptr;
	std::vector<Issue> issues_;
};
