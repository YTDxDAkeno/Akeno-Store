// Akeno Gaming Hub - image decoding, scaling and PNG encoding (no SDL).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace akeno::ui
{

struct Pixels
{
    int width = 0, height = 0;
    std::vector<std::uint32_t> rgba; // R,G,B,A bytes in memory order
};

// Decodes PNG/JPEG/GIF/BMP from memory and downsizes to fit (box filter, keeps aspect).
bool decode_image(std::string_view encoded, int max_w, int max_h, Pixels &out);
// Crops/scales to exactly w x h (cover) with rounded, antialiased corners baked in.
Pixels cover_crop(const Pixels &source, int w, int h, int radius);
// Encodes RGBA as PNG (home-screen tile icons, screenshots).
std::string encode_png(const Pixels &pixels);

} // namespace akeno::ui
