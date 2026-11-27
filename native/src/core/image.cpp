// Akeno Gaming Hub - image decoding, scaling and PNG encoding (no SDL).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/image.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>

namespace akeno::ui
{
namespace
{
std::uint32_t pack(float r, float g, float b, float a)
{
    const auto c = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 255.0f) + 0.5f); };
    return c(r) | c(g) << 8 | c(b) << 16 | c(a) << 24;
}
} // namespace


bool decode_image(std::string_view encoded, int max_w, int max_h, Pixels &out)
{
    int w = 0, h = 0, channels = 0;
    if (encoded.size() > 32 * 1024 * 1024)
        return false;
    if (!stbi_info_from_memory(reinterpret_cast<const stbi_uc *>(encoded.data()), static_cast<int>(encoded.size()),
                               &w, &h, &channels) ||
        w <= 0 || h <= 0 || static_cast<long long>(w) * h > 40'000'000)
        return false;
    stbi_uc *data = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(encoded.data()),
                                          static_cast<int>(encoded.size()), &w, &h, &channels, 4);
    if (!data)
        return false;
    const float scale = std::min({1.0f, max_w / static_cast<float>(w), max_h / static_cast<float>(h)});
    const int ow = std::max(1, static_cast<int>(w * scale)), oh = std::max(1, static_cast<int>(h * scale));
    out.width = ow;
    out.height = oh;
    out.rgba.assign(static_cast<std::size_t>(ow) * oh, 0);
    // Box filter: each output pixel averages the source pixels it covers.
    for (int y = 0; y < oh; ++y)
    {
        const int y0 = y * h / oh, y1 = std::max(y0 + 1, (y + 1) * h / oh);
        for (int x = 0; x < ow; ++x)
        {
            const int x0 = x * w / ow, x1 = std::max(x0 + 1, (x + 1) * w / ow);
            unsigned long long sum[4] = {0, 0, 0, 0};
            for (int sy = y0; sy < y1; ++sy)
                for (int sx = x0; sx < x1; ++sx)
                {
                    const stbi_uc *p = data + (static_cast<std::size_t>(sy) * w + sx) * 4;
                    const unsigned a = p[3];
                    sum[0] += p[0] * a;
                    sum[1] += p[1] * a;
                    sum[2] += p[2] * a;
                    sum[3] += a;
                }
            const unsigned long long count = static_cast<unsigned long long>(x1 - x0) * (y1 - y0);
            const unsigned long long a = sum[3];
            std::uint32_t pixel = 0;
            if (a > 0)
                pixel = static_cast<std::uint32_t>(sum[0] / a) | static_cast<std::uint32_t>(sum[1] / a) << 8 |
                        static_cast<std::uint32_t>(sum[2] / a) << 16 | static_cast<std::uint32_t>(a / count) << 24;
            out.rgba[static_cast<std::size_t>(y) * ow + x] = pixel;
        }
    }
    stbi_image_free(data);
    return true;
}

Pixels cover_crop(const Pixels &source, int w, int h, int radius)
{
    Pixels out;
    out.width = w;
    out.height = h;
    out.rgba.assign(static_cast<std::size_t>(w) * h, 0);
    if (source.width <= 0 || source.height <= 0)
        return out;
    const float scale = std::max(w / static_cast<float>(source.width), h / static_cast<float>(source.height));
    const float ox = (source.width * scale - w) / 2, oy = (source.height * scale - h) / 2;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            // Bilinear sample from the source.
            const float sx = (x + ox + 0.5f) / scale - 0.5f, sy = (y + oy + 0.5f) / scale - 0.5f;
            const int ix = std::clamp(static_cast<int>(std::floor(sx)), 0, source.width - 1);
            const int iy = std::clamp(static_cast<int>(std::floor(sy)), 0, source.height - 1);
            const int jx = std::min(ix + 1, source.width - 1), jy = std::min(iy + 1, source.height - 1);
            const float fx = std::clamp(sx - ix, 0.0f, 1.0f), fy = std::clamp(sy - iy, 0.0f, 1.0f);
            const auto at = [&](int px, int py) { return source.rgba[static_cast<std::size_t>(py) * source.width + px]; };
            const std::uint32_t p00 = at(ix, iy), p10 = at(jx, iy), p01 = at(ix, jy), p11 = at(jx, jy);
            float c[4];
            for (int k = 0; k < 4; ++k)
            {
                const auto ch = [&](std::uint32_t p) { return static_cast<float>((p >> (8 * k)) & 0xFF); };
                c[k] = (ch(p00) * (1 - fx) + ch(p10) * fx) * (1 - fy) + (ch(p01) * (1 - fx) + ch(p11) * fx) * fy;
            }
            // Rounded corners: coverage of the pixel centre against the corner circle.
            float coverage = 1;
            const float cx = x < radius ? radius : x >= w - radius ? w - radius : x + 0.5f;
            const float cy = y < radius ? radius : y >= h - radius ? h - radius : y + 0.5f;
            if (radius > 0 && (x < radius || x >= w - radius) && (y < radius || y >= h - radius))
            {
                const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
                coverage = std::clamp(radius - std::sqrt(dx * dx + dy * dy) + 0.5f, 0.0f, 1.0f);
            }
            out.rgba[static_cast<std::size_t>(y) * w + x] = pack(c[0], c[1], c[2], c[3] * coverage);
        }
    return out;
}

std::string encode_png(const Pixels &pixels)
{
    std::string out;
    stbi_write_png_to_func(
        [](void *context, void *data, int size) {
            static_cast<std::string *>(context)->append(static_cast<const char *>(data), static_cast<std::size_t>(size));
        },
        &out, pixels.width, pixels.height, 4, pixels.rgba.data(), pixels.width * 4);
    return out;
}

} // namespace akeno::ui
