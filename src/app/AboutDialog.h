// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

// Help → About: the version and exact build, licenses, and a button that copies the build
// details for bug reports.
class AboutDialog : public QDialog {
	Q_OBJECT

public:
	explicit AboutDialog(QWidget* parent = nullptr);

	// "0.3.0", or "0.3.0 (v0.3.0-2-g1a2b3c4-dirty)" when built from anything but the release tag.
	[[nodiscard]] static QString versionText();
	// The version plus Qt, OS and compiler, for pasting into a bug report.
	[[nodiscard]] static QString buildDetails();
};
