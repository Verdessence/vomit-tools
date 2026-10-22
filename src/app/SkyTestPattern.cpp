// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkyTestPattern.h"

#include <QFont>
#include <QImage>
#include <QPainter>

#include "ImageFiles.h"

using xv::sky::Edge;
using xv::sky::Face;

namespace {

QColor background(Face face) {
	switch (face) {
		case Face::Rt: return {196, 92, 84};
		case Face::Bk: return {92, 160, 96};
		case Face::Lf: return {84, 116, 196};
		case Face::Ft: return {200, 172, 72};
		case Face::Up: return {150, 196, 220};
		case Face::Dn: return {120, 96, 80};
	}
	return Qt::gray;
}

QString heading(Face face) {
	switch (face) {
		case Face::Rt: return QStringLiteral("+X · yaw 0 · east");
		case Face::Bk: return QStringLiteral("+Y · yaw 90 · north");
		case Face::Lf: return QStringLiteral("−X · yaw 180 · west");
		case Face::Ft: return QStringLiteral("−Y · yaw 270 · south");
		case Face::Up: return QStringLiteral("+Z · up");
		case Face::Dn: return QStringLiteral("−Z · down");
	}
	return {};
}

QString name(Face face) {
	return QString::fromUtf8(xv::sky::suffix(face)).toUpper();
}

} // namespace

xv::texture::Image skyTestPattern(Face face, int size) {
	QImage image(size, size, QImage::Format_RGBA8888);
	image.fill(background(face));
	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::TextAntialiasing);
	const double s = size;

	// A grid, so stretching and seams are easy to see.
	painter.setPen(QPen(QColor(255, 255, 255, 70), std::max(1.0, s / 512)));
	for (int i = 1; i < 8; i++) {
		painter.drawLine(QPointF(s * i / 8, 0), QPointF(s * i / 8, s));
		painter.drawLine(QPointF(0, s * i / 8), QPointF(s, s * i / 8));
	}
	painter.setPen(QPen(Qt::white, std::max(2.0, s / 128)));
	painter.drawRect(QRectF(0, 0, s, s));

	QFont font;
	font.setBold(true);
	font.setPixelSize(static_cast<int>(s / 4));
	painter.setFont(font);
	painter.setPen(Qt::white);
	painter.drawText(QRectF(0, s * 0.22, s, s * 0.35), Qt::AlignCenter, name(face));
	font.setPixelSize(static_cast<int>(s / 18));
	painter.setFont(font);
	painter.drawText(QRectF(0, s * 0.55, s, s * 0.1), Qt::AlignCenter, heading(face));

	// Each edge names the face that should be on the other side of it, with an arrow pointing there.
	font.setPixelSize(static_cast<int>(s / 14));
	painter.setFont(font);
	const double band = s * 0.12;
	painter.drawText(QRectF(0, s * 0.02, s, band), Qt::AlignCenter, QStringLiteral("▲ %1").arg(name(xv::sky::neighbor(face, Edge::Top))));
	painter.drawText(QRectF(0, s * 0.98 - band, s, band), Qt::AlignCenter, QStringLiteral("▼ %1").arg(name(xv::sky::neighbor(face, Edge::Bottom))));
	painter.save();
	painter.translate(s * 0.02 + band / 2, s / 2);
	painter.rotate(-90);
	painter.drawText(QRectF(-s / 2, -band / 2, s, band), Qt::AlignCenter, QStringLiteral("▲ %1").arg(name(xv::sky::neighbor(face, Edge::Left))));
	painter.restore();
	painter.save();
	painter.translate(s * 0.98 - band / 2, s / 2);
	painter.rotate(90);
	painter.drawText(QRectF(-s / 2, -band / 2, s, band), Qt::AlignCenter, QStringLiteral("▲ %1").arg(name(xv::sky::neighbor(face, Edge::Right))));
	painter.restore();

	// The top-left corner is marked, so a turned or mirrored face shows at a glance.
	painter.setBrush(Qt::white);
	painter.setPen(Qt::NoPen);
	painter.drawPolygon(QPolygonF{{QPointF(0, 0), QPointF(s * 0.1, 0), QPointF(0, s * 0.1)}});
	painter.end();
	return fromQImage(image);
}
