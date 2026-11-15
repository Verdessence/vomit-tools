//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Help → About: the version and exact build, licenses, and a button that copies the build
//          details for bug reports.
//
//===========================================================================//
#pragma once

#include <QDialog>
#include <QList>

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
	// One library of THIRD-PARTY-NOTICES.txt: what its heading says and its license's text.
	struct Notice_t
	{
		QString m_szName;
		QString m_szSource;
		QString m_szLicense; // the SPDX id, as the heading gives it
		QString m_szNote;
		QString m_szText;
	};
	// The notices file taken apart, in its order; pIntroduction gets what stands before the
	// first library.
	[[nodiscard]] static QList<Notice_t> ThirdPartyNotices( QString *pIntroduction = nullptr );
	// The licenses window: our license in one tab; in the other the libraries listed by name
	// and license (a filter over them), each with its own notice beside the list.
	[[nodiscard]] static QDialog *NewLicensesDialog( QWidget *pParent );
	static void ShowLicenses( QWidget *pParent );

protected:
	bool event( QEvent *pEvent ) override;

private:
	void FitHeight();
};
