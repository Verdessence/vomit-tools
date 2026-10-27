//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Pins the pointer where it is while the mouse turns a camera (Hammer's mouselook):
//          the cursor is hidden, the movement comes as deltas, and when let go the cursor is back
//          where it was. On Wayland this is the compositor's pointer constraints (with its
//          relative pointer for the deltas), which Qt doesn't offer; a desktop without them
//          (X11, Windows, an older compositor) gets IsAvailable() false, and the view warps the
//          cursor back itself instead.
//
//===========================================================================//
#pragma once

#include <functional>
#include <memory>

#include <QObject>
#include <QPoint>
#include <QPointF>

class QWidget;

class CPointerLock : public QObject
{
	Q_OBJECT

public:
	explicit CPointerLock( QObject *pParent = nullptr );
	~CPointerLock() override;

	// Whether this desktop pins pointers. Known once the compositor has listed what it does,
	// shortly after the program starts.
	[[nodiscard]] static bool IsAvailable();
	// Pins the pointer over the widget's window; pfnMoved gets each movement, in the widget's
	// logical pixels. False when it can't (then nothing is pinned).
	bool Lock( QWidget *pWidget, std::function<void( QPointF )> pfnMoved );
	// Lets the pointer go, with the cursor put at `at` in the widget's coordinates.
	void Unlock( QPoint at );
	// Whether Lock() succeeded and Unlock() hasn't been called.
	[[nodiscard]] bool IsActive() const;

private:
	struct Impl_t;
	std::unique_ptr<Impl_t> m_pImpl;
};
