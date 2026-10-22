// SPDX-License-Identifier: GPL-3.0-or-later
#include "ImageFiles.h"

#include <cstring>

#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include "texture/Vtf.h"

namespace {

bool isVtf(const QString& name) {
	return name.endsWith(u".vtf", Qt::CaseInsensitive);
}

} // namespace

bool isImageFile(const QString& name) {
	const QString suffix = QFileInfo{name}.suffix().toLower();
	return suffix == u"vtf" || xv::texture::isImageExtension(suffix.toStdString());
}

std::optional<xv::texture::Image> decodeImageFile(const QString& name, std::span<const std::byte> data, QString* error) {
	std::string problem;
	std::optional<xv::texture::Image> image;
	if (isVtf(name)) {
		if (const auto vtf = xv::texture::readVtf(data, &problem)) {
			image = vtf->decode(0, 0, 0, &problem);
		}
	} else {
		image = xv::texture::loadImage(data, &problem);
	}
	if (!image && error) {
		*error = QString::fromStdString(problem);
	}
	return image;
}

std::optional<xv::texture::Image> readImageFile(const QString& path, QString* error) {
	QFile file{path};
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = file.errorString();
		}
		return std::nullopt;
	}
	const QByteArray data = file.readAll();
	return decodeImageFile(path, {reinterpret_cast<const std::byte*>(data.constData()), static_cast<std::size_t>(data.size())}, error);
}

QString describeImageFile(const QString& name, std::span<const std::byte> data) {
	if (isVtf(name)) {
		std::string error;
		const auto vtf = xv::texture::readVtf(data, &error);
		if (!vtf) {
			return QString::fromStdString(error);
		}
		const auto& info = vtf->info;
		QStringList flags;
		for (const std::string& flag : xv::texture::flagNames(info.flags)) {
			flags << QString::fromStdString(flag);
		}
		QString text = QStringLiteral("%1×%2 · %3 · VTF %4.%5 · %6").arg(info.width).arg(info.height)
			.arg(QString::fromUtf8(xv::texture::formatName(info.format))).arg(info.versionMajor).arg(info.versionMinor)
			.arg(info.mipCount == 1 ? QStringLiteral("no mipmaps") : QStringLiteral("%1 mipmaps").arg(info.mipCount));
		if (info.frames > 1) {
			text += QStringLiteral(" · %1 frames").arg(info.frames);
		}
		if (info.faces > 1) {
			text += QStringLiteral(" · %1 faces").arg(info.faces);
		}
		if (!flags.isEmpty()) {
			text += QStringLiteral(" · ") + flags.join(u' ');
		}
		return text;
	}
	const auto image = xv::texture::loadImage(data);
	return image ? QStringLiteral("%1×%2").arg(image->width).arg(image->height) : QStringLiteral("not an image this app can read");
}

QImage toQImage(const xv::texture::Image& image) {
	QImage out(image.width, image.height, QImage::Format_RGBA8888);
	for (int y = 0; y < image.height; y++) {
		std::memcpy(out.scanLine(y), image.pixel(0, y), static_cast<std::size_t>(image.width) * 4);
	}
	return out;
}

xv::texture::Image fromQImage(const QImage& image) {
	const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
	xv::texture::Image out{rgba.width(), rgba.height()};
	for (int y = 0; y < rgba.height(); y++) {
		std::memcpy(out.pixel(0, y), rgba.constScanLine(y), static_cast<std::size_t>(rgba.width()) * 4);
	}
	return out;
}
