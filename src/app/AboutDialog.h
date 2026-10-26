//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Help → About: the version and exact build, licenses, and a button that copies the build
//          details for bug reports.
//
//===========================================================================//
#pragma once

#include <QDialog>

class CAboutDialog : public QDialog
{
	Q_OBJECT

public:
	explicit CAboutDialog( QWidget *pParent = nullptr );

	// "0.3.0", or "0.3.0 (v0.3.0-2-g1a2b3c4-dirty)" when built from anything but the release tag.
	[[nodiscard]] static QString VersionText();
	// The version plus Qt, OS, compiler and what the previews draw with, for pasting into a bug
	// report.
	[[nodiscard]] static QString BuildDetails();
	// A license shipped with the program: "LICENSE.txt" (ours) or "THIRD-PARTY-NOTICES.txt".
	[[nodiscard]] static QString LicenseText( const QString &szName );
	// Our license and every library's notice, one tab each.
	static void ShowLicenses( QWidget *pParent );

protected:
	bool event( QEvent *pEvent ) override;

private:
	void FitHeight();
};
