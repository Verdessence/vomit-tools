//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Dropdowns that list each choice next to what it means. A search box takes typing and
//          shows every choice as soon as it is clicked, narrowed by what is typed; a list box is a
//          fixed pick whose dropdown is a limited height that scrolls, grouped under headings.
//
//===========================================================================//
#pragma once

#include <vector>

#include <QString>
#include <QVariant>

class QComboBox;
class QWidget;

// A choice offered in a list, with what it means beside it. A heading groups the choices after it
// and can't be picked.
struct Choice_t
{
	QString m_szValue;
	QString m_szMeaning;
	QVariant m_Data = {}; // the item's data (Qt::UserRole); none for a search box
	bool m_bHeading = false;
};

// An editable box that works like a search box: a click drops down every choice next to what it
// means, and typing narrows them (anywhere in the name, whatever the case).
[[nodiscard]] QComboBox *MakeSearchBox( const std::vector<Choice_t> &choices, QWidget *pParent = nullptr );
// Drops down every choice of a search box now, with its text selected so typing replaces it.
void RevealChoices( QComboBox *pBox );
// Fills a fixed list whose dropdown shows each choice next to what it means, nVisible rows high
// and scrolling, whatever the style would do.
void FillListBox( QComboBox *pBox, const std::vector<Choice_t> &choices, int nVisible = 16 );
