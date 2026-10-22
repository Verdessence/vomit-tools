// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPoint>
#include <QWidget>

#include "sky/Sky.h"
#include "texture/Image.h"

// The sky as a player sees it from inside: drag to look around, wheel to zoom. Angles and field
// of view follow Source's, so "setang <pitch> <yaw> 0" in game shows the same view.
class SkyView : public QWidget {
	Q_OBJECT

public:
	explicit SkyView(QWidget* parent = nullptr);

	// The cube is not owned; call refresh() after it changes. nullptr shows nothing.
	void setCube(const xv::sky::Cube* cube);
	void refresh();

	[[nodiscard]] xv::sky::Angles angles() const { return angles_; }
	void setAngles(xv::sky::Angles angles);
	[[nodiscard]] float fov() const { return fov_; }
	void setFov(float fov);
	void setShowLabels(bool show);
	void setShowEdges(bool show);
	// "setang 0 90 0": the console command (sv_cheats 1) for the same view in game.
	[[nodiscard]] QString setangCommand() const;

signals:
	void viewChanged();

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;

private:
	void look(float yawDelta, float pitchDelta);

	const xv::sky::Cube* cube_ = nullptr;
	xv::sky::Angles angles_;
	float fov_ = 90.0f;
	bool labels_ = true;
	bool edges_ = false;
	QPoint lastMouse_;
	xv::texture::Image frame_;
};
