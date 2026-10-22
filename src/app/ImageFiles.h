// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include <QImage>
#include <QString>

#include "texture/Image.h"

// Image files the skybox editor and the preview read: VTF, plus what stb reads (PNG, JPEG, TGA,
// BMP, PSD, GIF, HDR, PNM).
[[nodiscard]] bool isImageFile(const QString& name);
// name decides VTF or not; the content decides the rest.
[[nodiscard]] std::optional<xv::texture::Image> decodeImageFile(const QString& name, std::span<const std::byte> data, QString* error = nullptr);
[[nodiscard]] std::optional<xv::texture::Image> readImageFile(const QString& path, QString* error = nullptr);
// A tooltip-sized description of an image file: size, and for VTFs version, format, mipmaps, flags.
[[nodiscard]] QString describeImageFile(const QString& name, std::span<const std::byte> data);

[[nodiscard]] QImage toQImage(const xv::texture::Image& image);
[[nodiscard]] xv::texture::Image fromQImage(const QImage& image);
