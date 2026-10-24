//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Help → About Premium, also shown the first time the app starts: a character asking
//          for a card, what Free and Premium each have, and fields that take anything. Nothing
//          typed is kept or sent. Its text is data/premium.txt; its picture is
//          assets/premium_character.gif.
//
//===========================================================================//
#pragma once

#include <QDialog>

class QLineEdit;

class CPremiumDialog : public QDialog
{
	Q_OBJECT

public:
	explicit CPremiumDialog( QWidget *pParent = nullptr );

	// Whether it has been shown before (the first start shows it).
	[[nodiscard]] static bool HasBeenShown();
	static void MarkShown();

private:
	QLineEdit *m_pCard = nullptr;
	QLineEdit *m_pExpiry = nullptr;
	QLineEdit *m_pCode = nullptr;
};
