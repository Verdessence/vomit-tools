//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Main-thread work between dispatcher sleeps, including nested event loops.
//
//===========================================================================//
#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QString>

class CMapEventClock : public QObject
{
public:
	CMapEventClock();
	[[nodiscard]] double GetBusyTime() const;
	[[nodiscard]] int GetNestedLoopCount() const { return m_nNestedLoops; }
	[[nodiscard]] int GetMaximumLoopLevel() const { return m_nMaximumLoopLevel; }
	[[nodiscard]] QString Describe();
	static CMapEventClock &Get();

protected:
	bool eventFilter( QObject *pWatched, QEvent *pEvent ) override;

private:
	void Awake();
	void Block();
	QElapsedTimer m_Wall, m_Busy;
	double m_flBusyMs = 0, m_flWindowStart = 0, m_flLastBusy = 0, m_flLastWall = 0;
	int m_nNestedLoops = 0, m_nLastLoopLevel = 0, m_nMaximumLoopLevel = 0;
};
