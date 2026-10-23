//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The simple or regular interface, app-wide and remembered. Simple shows plain names and
//          tucks advanced settings away; regular shows the files' own keys and every setting. Both
//          edit the files the same way.
//
//===========================================================================//
#pragma once

#include <QObject>

class CInterfaceMode : public QObject
{
	Q_OBJECT

public:
	[[nodiscard]] static CInterfaceMode &Instance();
	[[nodiscard]] bool IsSimple() const { return m_bSimple; }
	void SetSimple( bool bSimple );

signals:
	void Changed( bool bSimple );

private:
	CInterfaceMode();

	bool m_bSimple = false;
};
