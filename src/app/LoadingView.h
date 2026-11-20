//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Skeleton placeholders and wait cursors for views that are loading.
//
//===========================================================================//
#pragma once

#include <QElapsedTimer>
#include <QIcon>
#include <QPointer>
#include <QWidget>

class QTimer;

enum class LoadingLayout_t
{
	LOADING_LAYOUT_ROWS,
	LOADING_LAYOUT_GRID,
	LOADING_LAYOUT_PREVIEW,
	LOADING_LAYOUT_PANES,
};

// A child overlay that follows its host's size without affecting its layout.
class CLoadingView final : public QWidget
{
public:
	explicit CLoadingView( QWidget *pHost, LoadingLayout_t nLayout );

	void Begin( const QString &szStatus );
	void SetProgress( const QString &szStatus, int nPercent = -1 );
	void Fail( const QString &szStatus );
	void End();

protected:
	bool eventFilter( QObject *pObject, QEvent *pEvent ) override;
	void paintEvent( QPaintEvent *pEvent ) override;
	void showEvent( QShowEvent *pEvent ) override;
	void hideEvent( QHideEvent *pEvent ) override;

private:
	LoadingLayout_t m_nLayout;
	QString m_szStatus;
	QTimer *m_pTimer = nullptr;
	QElapsedTimer m_Clock;
	int m_nPercent = -1;
	bool m_bBusy = false;
};

[[nodiscard]] QIcon LoadingThumbnail();

// Nested, synchronous operations restore the cursor they found on entry.
class CWaitCursor final
{
public:
	explicit CWaitCursor( CLoadingView *pLoading = nullptr, const QString &szStatus = {} );
	~CWaitCursor();
	CWaitCursor( const CWaitCursor & ) = delete;
	CWaitCursor &operator=( const CWaitCursor & ) = delete;
	void Finish();

private:
	QPointer<CLoadingView> m_pLoading;
	bool m_bWaiting = true;
};
