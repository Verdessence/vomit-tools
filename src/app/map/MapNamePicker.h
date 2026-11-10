//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A name picked from a long list (sounds, particle systems): words
//          typed narrow it, a double-click or OK picks. For Object Properties' Browse.
//
//===========================================================================//
#pragma once

#include <optional>

#include <QDialog>
#include <QStringList>

class QLineEdit;
class QListView;
class QSortFilterProxyModel;
class QStringListModel;

class CMapNamePicker : public QDialog
{
	Q_OBJECT

public:
	CMapNamePicker( const QString &szTitle, QStringList names, const QString &szCurrent, QWidget *pParent = nullptr );

	// Runs the picker; the name picked, if one was.
	static std::optional<QString> Pick( QWidget *pParent, const QString &szTitle, QStringList names, const QString &szCurrent );
	[[nodiscard]] QString GetPicked() const;
	[[nodiscard]] QLineEdit *GetFilter() const { return m_pFilter; }
	[[nodiscard]] QListView *GetView() const { return m_pView; }

private:
	QLineEdit *m_pFilter = nullptr;
	QListView *m_pView = nullptr;
	QStringListModel *m_pModel = nullptr;
	QSortFilterProxyModel *m_pProxy = nullptr;
};
