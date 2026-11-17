/* SPDX-License-Identifier: GPL-3.0-or-later */
/* (satrecomp's port/jpeg_write.h, by the same author.)
   A baseline JPEG file from 8-bit RGB pixels: no colour subsampling (pictures of a few hundred dots keep their
   edges), the standard tables of the JPEG standard's annex K scaled for a quality of 1-100.
*/
#pragma once

#include <cstdint>
#include <vector>

namespace sc2 {

// rgb: width * height pixels, three bytes each (red, green, blue), row after row
std::vector<uint8_t> EncodeJpeg(const uint8_t *rgb, int width, int height, int quality = 95);

} // namespace sc2
