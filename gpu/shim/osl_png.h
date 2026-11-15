// SPDX-License-Identifier: GPL-2.0-or-later
// PNG pictures for the runtime UI (save list icons), decoded by libpng to RGBA8.

#pragma once

#include <cstddef>
#include <vector>

namespace OslPng {

struct Image {
    int width = 0, height = 0;
    std::vector<unsigned char> rgba; // width * height * 4, rows top to bottom
};

/// False (and `out` empty) when the data is not a PNG libpng can decode.
bool DecodeRgba(const unsigned char* data, size_t size, Image& out);
/// Reads the file and decodes it; false when it is missing or bad.
bool LoadRgba(const char* path, Image& out);
/// Writes tightly packed top-to-bottom 8-bit RGBA/BGRA pixels. Dimensions and
/// byte count are checked before opening the destination; no HDR conversion.
bool WriteRgba(const char* path, unsigned width, unsigned height,
               const unsigned char* pixels, size_t size, bool bgra = false);

} // namespace OslPng
