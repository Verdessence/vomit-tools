//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Image files the skybox editor and the preview read: VTF, plus what stb reads (PNG,
//          JPEG, TGA, BMP, PSD, GIF, HDR, PNM).
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include <QImage>
#include <QString>

#include "texture/Image.h"

[[nodiscard]] bool IsImageFile( const QString &szName );
// name decides VTF or not; the content decides the rest.
[[nodiscard]] std::optional<xv::texture::CImage> DecodeImageFile( const QString &szName, std::span<const std::byte> data, QString *pError = nullptr );
[[nodiscard]] std::optional<xv::texture::CImage> ReadImageFile( const QString &szPath, QString *pError = nullptr );
// A tooltip-sized description of an image file: size, and for VTFs version, format, mipmaps, flags.
[[nodiscard]] QString DescribeImageFile( const QString &szName, std::span<const std::byte> data );

[[nodiscard]] QImage ToQImage( const xv::texture::CImage &image );
[[nodiscard]] xv::texture::CImage FromQImage( const QImage &image );
