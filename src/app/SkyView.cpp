// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkyView.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

using xv::sky::Face;

namespace {

constexpr float MIN_FOV = 20.0f;
constexpr float MAX_FOV = 130.0f;

float wrapYaw(float yaw) {
	yaw = std::fmod(yaw, 360.0f);
	return yaw < 0.0f ? yaw + 360.0f : yaw;
}

void outlinedText(QPainter& painter, QPointF centre, const QString& text, const QFont& font, const QColor& colour) {
	QPainterPath path;
	const QFontMetricsF metrics{font};
	path.addText(centre.x() - metrics.horizontalAdvance(text) / 2, centre.y() + metrics.capHeight() / 2, font, text);
	painter.setPen(QPen(QColor(0, 0, 0, 170), 3));
	painter.setBrush(Qt::NoBrush);
	painter.drawPath(path);
	painter.fillPath(path, colour);
}

} // namespace

SkyView::SkyView(QWidget* parent)
		: QWidget(parent) {
	setMinimumSize(240, 180);
	setFocusPolicy(Qt::StrongFocus);
	setCursor(Qt::OpenHandCursor);
	setToolTip(tr("Drag to look around, scroll to zoom, double-click to look east (yaw 0) again"));
}

void SkyView::setCube(const xv::sky::Cube* cube) {
	cube_ = cube;
	update();
}

void SkyView::refresh() {
	update();
}

void SkyView::setAngles(xv::sky::Angles angles) {
	angles_ = {std::clamp(angles.pitch, -89.0f, 89.0f), wrapYaw(angles.yaw)};
	update();
	emit viewChanged();
}

void SkyView::setFov(float fov) {
	fov_ = std::clamp(fov, MIN_FOV, MAX_FOV);
	update();
	emit viewChanged();
}

void SkyView::setShowLabels(bool show) {
	labels_ = show;
	update();
}

void SkyView::setShowEdges(bool show) {
	edges_ = show;
	update();
}

QString SkyView::setangCommand() const {
	return QStringLiteral("setang %1 %2 0").arg(std::lround(angles_.pitch)).arg(std::lround(angles_.yaw) % 360);
}

void SkyView::look(float yawDelta, float pitchDelta) {
	setAngles({angles_.pitch + pitchDelta, angles_.yaw + yawDelta});
}

void SkyView::paintEvent(QPaintEvent*) {
	QPainter painter(this);
	painter.fillRect(rect(), QColor(24, 24, 28));
	if (!cube_ || std::ranges::all_of(cube_->faces, &xv::texture::Image::empty)) {
		painter.setPen(palette().color(QPalette::PlaceholderText));
		painter.drawText(rect(), Qt::AlignCenter, tr("Add face images to see the sky here."));
		return;
	}
	const xv::sky::View view{angles_, fov_, width(), height()};
	xv::sky::render(*cube_, view, frame_, edges_);
	const QImage image(frame_.pixels.data(), frame_.width, frame_.height, frame_.width * 4, QImage::Format_RGBA8888);
	painter.drawImage(0, 0, image);

	painter.setRenderHint(QPainter::Antialiasing);
	if (labels_) {
		QFont font = this->font();
		font.setBold(true);
		font.setPixelSize(std::clamp(height() / 14, 12, 40));
		for (Face face : xv::sky::FACES) {
			if (const auto point = view.toScreen(xv::sky::basis(face).normal)) {
				outlinedText(painter, {(*point)[0], (*point)[1]}, QString::fromUtf8(xv::sky::suffix(face)).toUpper(), font, QColor(255, 255, 255, 220));
			}
		}
	}
	QFont small = font();
	small.setPixelSize(12);
	painter.setFont(small);
	const QString status = tr("%1 · fov %2").arg(setangCommand()).arg(std::lround(fov_));
	const QRect box = painter.fontMetrics().boundingRect(status).adjusted(-6, -3, 6, 3);
	const QRect placed = box.translated(8 - box.left(), height() - 8 - box.bottom());
	painter.fillRect(placed, QColor(0, 0, 0, 150));
	painter.setPen(Qt::white);
	painter.drawText(placed, Qt::AlignCenter, status);
}

void SkyView::mousePressEvent(QMouseEvent* event) {
	lastMouse_ = event->position().toPoint();
	if (event->button() == Qt::LeftButton) {
		setCursor(Qt::ClosedHandCursor);
	}
}

void SkyView::mouseMoveEvent(QMouseEvent* event) {
	if (!(event->buttons() & Qt::LeftButton)) {
		setCursor(Qt::OpenHandCursor);
		return;
	}
	const QPoint position = event->position().toPoint();
	const QPoint delta = position - lastMouse_;
	lastMouse_ = position;
	// Grab the sky: it follows the mouse. Degrees per pixel from the vertical field of view.
	const float degrees = 2.0f * std::atan(std::tan(fov_ * 0.5f * 3.14159265f / 180.0f) * 0.75f) * 180.0f / 3.14159265f / static_cast<float>(std::max(height(), 1));
	look(static_cast<float>(delta.x()) * degrees, -static_cast<float>(delta.y()) * degrees);
}

void SkyView::mouseDoubleClickEvent(QMouseEvent*) {
	setFov(90.0f);
	setAngles({});
}

void SkyView::wheelEvent(QWheelEvent* event) {
	const float steps = static_cast<float>(event->angleDelta().y()) / 120.0f;
	setFov(fov_ * std::pow(0.9f, steps));
}

void SkyView::keyPressEvent(QKeyEvent* event) {
	switch (event->key()) {
		case Qt::Key_Left: look(5.0f, 0.0f); break;
		case Qt::Key_Right: look(-5.0f, 0.0f); break;
		case Qt::Key_Up: look(0.0f, -5.0f); break;
		case Qt::Key_Down: look(0.0f, 5.0f); break;
		case Qt::Key_Home: setAngles({}); break;
		default: QWidget::keyPressEvent(event);
	}
}

void SkyView::contextMenuEvent(QContextMenuEvent* event) {
	QMenu menu(this);
	menu.addAction(tr("Copy \"%1\"").arg(setangCommand()), this, [this] { QApplication::clipboard()->setText(setangCommand()); });
	menu.addSeparator();
	const std::array<std::pair<QString, xv::sky::Angles>, 6> looks{{
		{tr("Look at rt (east, yaw 0)"), {0, 0}},
		{tr("Look at bk (north, yaw 90)"), {0, 90}},
		{tr("Look at lf (west, yaw 180)"), {0, 180}},
		{tr("Look at ft (south, yaw 270)"), {0, 270}},
		{tr("Look up"), {-89, 0}},
		{tr("Look down"), {89, 0}},
	}};
	for (const auto& [label, angles] : looks) {
		menu.addAction(label, this, [this, angles] { setAngles(angles); });
	}
	menu.exec(event->globalPos());
}
