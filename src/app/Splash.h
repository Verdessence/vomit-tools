//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The startup logo, with the current step and its progress below it.
//
//===========================================================================//
#pragma once

#include <QSplashScreen>

class CSplash : public QSplashScreen
{
public:
	// nSteps: how many SetStep calls there will be.
	explicit CSplash( int nSteps );

	// Shows the next step's name, and draws it at once (the event loop isn't running yet).
	void SetStep( const QString &szWhat );
	void Complete();
	[[nodiscard]] QString GetText() const { return m_szText; }

protected:
	void drawContents( QPainter *pPainter ) override;

private:
	int m_nSteps;
	int m_nStep = 0;
	bool m_bComplete = false;
	QString m_szText;
};
