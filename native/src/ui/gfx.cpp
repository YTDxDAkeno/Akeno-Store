// Akeno Gaming Hub - 2D drawing on SDL2: text, rounded shapes, textures, gradients.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/gfx.hpp"

#include "core/fs.hpp"
#include "core/util.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace akeno::ui
{

Color mix(Color a, Color b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    const auto lerp = [&](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(std::lround(x + (y - x) * t));
    };
    return {lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), lerp(a.a, b.a)};
}

SDL_Rect Rect::sdl() const
{
    return {static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)), static_cast<int>(std::lround(w)),
            static_cast<int>(std::lround(h))};
}

Texture::~Texture()
{
    if (texture_)
        SDL_DestroyTexture(texture_);
}

Texture &Texture::operator=(Texture &&other) noexcept
{
    if (this != &other)
    {
        if (texture_)
            SDL_DestroyTexture(texture_);
        texture_ = other.texture_;
        w_ = other.w_;
        h_ = other.h_;
        opaque_center = other.opaque_center;
        corner = other.corner;
        other.texture_ = nullptr;
        other.w_ = other.h_ = 0;
    }
    return *this;
}

// ---------------------------------------------------------------------------------------
// Fonts: one packed atlas per (size, weight), Latin-1, Latin Extended-A and punctuation.

class Font
{
  public:
    bool build(SDL_Renderer *renderer, const std::string &data, int size)
    {
        size_ = size;
        if (!stbtt_InitFont(&info_, reinterpret_cast<const unsigned char *>(data.data()),
                            stbtt_GetFontOffsetForIndex(reinterpret_cast<const unsigned char *>(data.data()), 0)))
            return false;
        scale_ = stbtt_ScaleForPixelHeight(&info_, static_cast<float>(size));
        int ascent = 0, descent = 0, gap = 0;
        stbtt_GetFontVMetrics(&info_, &ascent, &descent, &gap);
        ascent_ = ascent * scale_;
        line_ = (ascent - descent + gap) * scale_;
        const int side = size <= 28 ? 512 : size <= 48 ? 1024 : 2048;
        std::vector<unsigned char> alpha(static_cast<std::size_t>(side) * side);
        stbtt_pack_context pack;
        if (!stbtt_PackBegin(&pack, alpha.data(), side, side, 0, 1, nullptr))
            return false;
        stbtt_PackSetOversampling(&pack, size <= 28 ? 2 : 1, 1);
        static const int ranges[][2] = {{0x20, 0x7E}, {0xA0, 0x17F}, {0x2010, 0x2027}, {0x2030, 0x203A},
                                        {0x20AC, 0x20AC}, {0x2122, 0x2122}, {0x2190, 0x2193}, {0x2212, 0x2212}};
        for (const auto &range : ranges)
        {
            const int count = range[1] - range[0] + 1;
            std::vector<stbtt_packedchar> chars(static_cast<std::size_t>(count));
            stbtt_pack_range r{};
            r.font_size = static_cast<float>(size);
            r.first_unicode_codepoint_in_range = range[0];
            r.num_chars = count;
            r.chardata_for_range = chars.data();
            stbtt_PackFontRanges(&pack, reinterpret_cast<const unsigned char *>(data.data()), 0, &r, 1);
            for (int i = 0; i < count; ++i)
                glyphs_[static_cast<std::uint32_t>(range[0] + i)] = chars[static_cast<std::size_t>(i)];
        }
        stbtt_PackEnd(&pack);
        std::vector<std::uint32_t> rgba(alpha.size());
        for (std::size_t i = 0; i < alpha.size(); ++i)
            rgba[i] = 0x00FFFFFFu | (static_cast<std::uint32_t>(alpha[i]) << 24);
        SDL_Texture *texture =
            SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, side, side);
        if (!texture)
            return false;
        SDL_UpdateTexture(texture, nullptr, rgba.data(), side * 4);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        atlas_ = Texture(texture, side, side);
        side_ = side;
        return true;
    }

    const stbtt_packedchar *glyph(std::uint32_t code) const
    {
        auto it = glyphs_.find(code);
        if (it == glyphs_.end())
        {
            // Common fallbacks before a visible replacement mark.
            if (code == 0x201E || code == 0x201C || code == 0x201D)
                it = glyphs_.find('"');
            else if (code == 0x2018 || code == 0x2019)
                it = glyphs_.find('\'');
            else
                it = glyphs_.find('?');
        }
        return it == glyphs_.end() ? nullptr : &it->second;
    }

    float kern(std::uint32_t a, std::uint32_t b) const
    {
        return a && b ? stbtt_GetCodepointKernAdvance(&info_, static_cast<int>(a), static_cast<int>(b)) * scale_ : 0;
    }

    float ascent() const { return ascent_; }
    float line() const { return line_; }

    // Rasterizes a whole line with subpixel positioning into an alpha bitmap.
    // `left` receives the bitmap's x offset from the pen origin (may be negative).
    std::vector<unsigned char> rasterize(std::string_view text, int &width, int &height, float &left) const
    {
        struct Placed
        {
            int cp;
            float pen;
        };
        std::vector<Placed> placed;
        float pen = 0;
        int previous = 0;
        std::size_t index = 0;
        while (index < text.size())
        {
            int cp = static_cast<int>(next_codepoint(text, index));
            if (cp == '\n')
                break;
            if (!stbtt_FindGlyphIndex(&info_, cp))
                cp = (cp == 0x201E || cp == 0x201C || cp == 0x201D) ? '"' : (cp == 0x2018 || cp == 0x2019) ? '\'' : '?';
            if (previous)
                pen += stbtt_GetCodepointKernAdvance(&info_, previous, cp) * scale_;
            placed.push_back({cp, pen});
            int advance = 0, bearing = 0;
            stbtt_GetCodepointHMetrics(&info_, cp, &advance, &bearing);
            pen += advance * scale_;
            previous = cp;
        }
        int x_min = 0, x_max = static_cast<int>(std::ceil(pen));
        for (const Placed &p : placed)
        {
            int x0, y0, x1, y1;
            const float shift = p.pen - std::floor(p.pen);
            stbtt_GetCodepointBitmapBoxSubpixel(&info_, p.cp, scale_, scale_, shift, 0, &x0, &y0, &x1, &y1);
            x_min = std::min(x_min, static_cast<int>(std::floor(p.pen)) + x0);
            x_max = std::max(x_max, static_cast<int>(std::floor(p.pen)) + x1);
        }
        const int pad = 1;
        width = std::max(1, x_max - x_min + 2 * pad);
        height = static_cast<int>(std::ceil(line_)) + 2 * pad;
        left = static_cast<float>(x_min - pad);
        std::vector<unsigned char> bitmap(static_cast<std::size_t>(width) * height, 0);
        std::vector<unsigned char> glyph;
        const int baseline = static_cast<int>(std::lround(ascent_)) + pad;
        for (const Placed &p : placed)
        {
            int x0, y0, x1, y1;
            const float shift = p.pen - std::floor(p.pen);
            stbtt_GetCodepointBitmapBoxSubpixel(&info_, p.cp, scale_, scale_, shift, 0, &x0, &y0, &x1, &y1);
            const int gw = x1 - x0, gh = y1 - y0;
            if (gw <= 0 || gh <= 0)
                continue;
            glyph.assign(static_cast<std::size_t>(gw) * gh, 0);
            stbtt_MakeCodepointBitmapSubpixel(&info_, glyph.data(), gw, gh, gw, scale_, scale_, shift, 0, p.cp);
            const int ox = static_cast<int>(std::floor(p.pen)) + x0 - x_min + pad, oy = baseline + y0;
            for (int y = 0; y < gh; ++y)
            {
                const int ty = oy + y;
                if (ty < 0 || ty >= height)
                    continue;
                for (int x = 0; x < gw; ++x)
                {
                    const int tx = ox + x;
                    if (tx < 0 || tx >= width)
                        continue;
                    unsigned char &d = bitmap[static_cast<std::size_t>(ty) * width + tx];
                    d = std::max(d, glyph[static_cast<std::size_t>(y) * gw + x]);
                }
            }
        }
        return bitmap;
    }
    const Texture &atlas() const { return atlas_; }
    int side() const { return side_; }

  private:
    stbtt_fontinfo info_{};
    float scale_ = 1, ascent_ = 0, line_ = 0;
    int size_ = 0, side_ = 0;
    std::map<std::uint32_t, stbtt_packedchar> glyphs_;
    Texture atlas_;
};

// ---------------------------------------------------------------------------------------

namespace
{
Texture make_texture(SDL_Renderer *renderer, int w, int h, const std::vector<std::uint32_t> &rgba,
                     SDL_BlendMode blend = SDL_BLENDMODE_BLEND)
{
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!texture)
        return {};
    SDL_UpdateTexture(texture, nullptr, rgba.data(), w * 4);
    SDL_SetTextureBlendMode(texture, blend);
    return Texture(texture, w, h);
}

std::uint32_t pack(float r, float g, float b, float a)
{
    const auto c = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 255.0f) + 0.5f); };
    return c(r) | c(g) << 8 | c(b) << 16 | c(a) << 24;
}
} // namespace

Gfx::Gfx(SDL_Renderer *renderer) : renderer_(renderer)
{
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_RendererInfo info{};
    software_ = SDL_GetRendererInfo(renderer_, &info) == 0 && (info.flags & SDL_RENDERER_SOFTWARE);
    // Quarter disc for rounded corners: alpha 1 inside the radius, antialiased edge.
    constexpr int n = 64;
    std::vector<std::uint32_t> corner(n * n), glow(n * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
        {
            // The disc centre is the texture's bottom-right corner (n, n).
            const float dx = n - (x + 0.5f), dy = n - (y + 0.5f);
            const float d = std::sqrt(dx * dx + dy * dy);
            const float a = std::clamp(n - d + 0.5f, 0.0f, 1.0f);
            corner[static_cast<std::size_t>(y * n + x)] = pack(255, 255, 255, a * 255);
            const float t = std::clamp(d / n, 0.0f, 1.0f);
            const float g = (1 - t) * (1 - t) * (1 - t);
            glow[static_cast<std::size_t>(y * n + x)] = pack(255, 255, 255, g * 255);
        }
    corner_ = make_texture(renderer_, n, n, corner);
    glow_corner_ = make_texture(renderer_, n, n, glow);

    // Backdrop: deep navy with violet and blue light, dithered against banding.
    constexpr int bw = 960, bh = 540;
    std::vector<std::uint32_t> back(static_cast<std::size_t>(bw * bh));
    std::uint32_t seed = 0x12345678u;
    for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x)
        {
            const float u = x / static_cast<float>(bw), v = y / static_cast<float>(bh);
            float r = 7 + 6 * v, g = 9 + 5 * v, b = 26 + 14 * v;
            const auto light = [&](float cx, float cy, float radius, float lr, float lg, float lb, float k) {
                const float dx = (u - cx) * 1.78f, dy = v - cy;
                const float f = std::exp(-(dx * dx + dy * dy) / (radius * radius)) * k;
                r += lr * f;
                g += lg * f;
                b += lb * f;
            };
            light(0.82f, 0.05f, 0.55f, 92, 38, 170, 0.85f);  // violet, top right
            light(0.10f, 0.95f, 0.60f, 24, 52, 160, 0.75f);  // blue, bottom left
            light(0.45f, 0.40f, 0.45f, 40, 24, 90, 0.35f);   // soft centre
            const float vignette = 1 - 0.35f * ((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) * 2.2f;
            seed = seed * 1664525u + 1013904223u;
            const float noise = ((seed >> 24) / 255.0f - 0.5f) * 2.0f;
            back[static_cast<std::size_t>(y * bw + x)] =
                pack(r * vignette + noise, g * vignette + noise, b * vignette + noise, 255);
        }
    // Upscaled once on the CPU to the full 1920x1080, so each frame is a plain unscaled copy.
    std::vector<std::uint32_t> full(static_cast<std::size_t>(kWidth) * kHeight);
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x)
        {
            const float sx = std::max(0.0f, (x + 0.5f) * bw / kWidth - 0.5f), sy = std::max(0.0f, (y + 0.5f) * bh / kHeight - 0.5f);
            const int x0 = std::min(static_cast<int>(sx), bw - 1), y0 = std::min(static_cast<int>(sy), bh - 1);
            const int x1 = std::min(x0 + 1, bw - 1), y1 = std::min(y0 + 1, bh - 1);
            const float fx = sx - x0, fy = sy - y0;
            float c[3];
            for (int k = 0; k < 3; ++k)
            {
                const auto ch = [&](int px, int py) { return static_cast<float>((back[static_cast<std::size_t>(py * bw + px)] >> (8 * k)) & 0xFF); };
                c[k] = (ch(x0, y0) * (1 - fx) + ch(x1, y0) * fx) * (1 - fy) + (ch(x0, y1) * (1 - fx) + ch(x1, y1) * fx) * fy;
            }
            seed = seed * 1664525u + 1013904223u;
            const float noise = ((seed >> 24) / 255.0f - 0.5f) * 1.5f;
            full[static_cast<std::size_t>(y) * kWidth + x] = pack(c[0] + noise, c[1] + noise, c[2] + noise, 255);
        }
    backdrop_ = make_texture(renderer_, kWidth, kHeight, full, SDL_BLENDMODE_NONE);
    // Shading behind the top bar and the hints: vertical alpha ramps of the backdrop's navy.
    const auto shade = [&](int h, bool down) {
        std::vector<std::uint32_t> px(static_cast<std::size_t>(kWidth) * h);
        for (int y = 0; y < h; ++y)
        {
            const float t = down ? 1 - y / static_cast<float>(h - 1) : y / static_cast<float>(h - 1);
            const float a = down ? 235 * t * t : (t < 0.45f ? 235 * (t / 0.45f) * (t / 0.45f) : 235);
            const std::uint32_t value = pack(6, 8, 22, a);
            std::fill(px.begin() + static_cast<std::ptrdiff_t>(y) * kWidth, px.begin() + static_cast<std::ptrdiff_t>(y + 1) * kWidth, value);
        }
        return make_texture(renderer_, kWidth, h, px);
    };
    top_shade_ = shade(130, true);
    bottom_shade_ = shade(150, false);
    constexpr int on = 128;
    std::vector<std::uint32_t> orb(on * on);
    for (int y = 0; y < on; ++y)
        for (int x = 0; x < on; ++x)
        {
            const float dx = (x + 0.5f) / on * 2 - 1, dy = (y + 0.5f) / on * 2 - 1;
            const float d = std::sqrt(dx * dx + dy * dy);
            const float a = d >= 1 ? 0 : (1 - d) * (1 - d);
            orb[static_cast<std::size_t>(y * on + x)] = pack(255, 255, 255, a * 255);
        }
    orb_ = make_texture(renderer_, on, on, orb, SDL_BLENDMODE_ADD);
}

Gfx::~Gfx() = default;

bool Gfx::load_fonts(const std::string &regular_ttf, const std::string &bold_ttf)
{
    auto regular = fs::read_file(regular_ttf, 8 * 1024 * 1024);
    auto bold = fs::read_file(bold_ttf, 8 * 1024 * 1024);
    if (!regular || !bold)
        return false;
    font_data_[0] = std::move(*regular);
    font_data_[1] = std::move(*bold);
    return true;
}

Font &Gfx::font(int size, Weight weight)
{
    const int w = weight == Weight::Bold ? 1 : 0;
    auto &slot = fonts_[{size, w}];
    if (!slot)
    {
        slot = std::make_unique<Font>();
        slot->build(renderer_, font_data_[w], size);
    }
    return *slot;
}

Texture Gfx::upload(const Pixels &pixels)
{
    if (pixels.width <= 0 || pixels.height <= 0)
        return {};
    return make_texture(renderer_, pixels.width, pixels.height, pixels.rgba);
}

void Gfx::clear(Color color)
{
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    SDL_RenderClear(renderer_);
}

void Gfx::background(float time)
{
    SDL_RenderCopy(renderer_, backdrop_.get(), nullptr, nullptr);
    if (time < 0 || software_)
        return; // reduced motion, or the CPU renderer (large scaled blends cost too much there)
    // Two slow light orbs drifting over the backdrop.
    const auto orb = [&](float cx, float cy, float size, Color color) {
        SDL_SetTextureColorMod(orb_.get(), color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(orb_.get(), color.a);
        const SDL_Rect dst{static_cast<int>(cx - size / 2), static_cast<int>(cy - size / 2), static_cast<int>(size),
                           static_cast<int>(size)};
        SDL_RenderCopy(renderer_, orb_.get(), nullptr, &dst);
    };
    orb(1500 + 160 * std::sin(time * 0.11f), 180 + 70 * std::cos(time * 0.13f), 760, {120, 70, 230, 70});
    orb(330 + 140 * std::cos(time * 0.09f), 900 + 60 * std::sin(time * 0.12f), 680, {40, 90, 230, 60});
}

void Gfx::fill(const Rect &rect, Color color)
{
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    const SDL_FRect r{rect.x, rect.y, rect.w, rect.h};
    if (color.a == 255)
    {
        // SDL's software renderer blends per pixel even at full opacity; NONE is a plain fill.
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        SDL_RenderFillRectF(renderer_, &r);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        return;
    }
    SDL_RenderFillRectF(renderer_, &r);
}

namespace
{
// Whole-pixel edges so neighbouring pieces neither overlap nor leave gaps (seams show in the
// software renderer when two translucent pieces share a row).
struct Box
{
    int x0, y0, x1, y1, r;
};
Box snap(const Rect &rect, float radius)
{
    Box b{static_cast<int>(std::lround(rect.x)), static_cast<int>(std::lround(rect.y)),
          static_cast<int>(std::lround(rect.x + rect.w)), static_cast<int>(std::lround(rect.y + rect.h)), 0};
    b.r = std::clamp(static_cast<int>(std::lround(radius)), 0, std::min(b.x1 - b.x0, b.y1 - b.y0) / 2);
    return b;
}
} // namespace

const Texture *Gfx::corners(int kind, int radius, int thickness)
{
    radius = std::clamp(radius, 1, 512);
    auto &set = corner_sets_[{kind, radius, thickness}];
    if (!set[0])
    {
        const int n = radius;
        std::vector<std::uint32_t> base(static_cast<std::size_t>(n) * n);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
            {
                // Top-left piece: the circle centre is the piece's bottom-right corner.
                const float dx = n - (x + 0.5f), dy = n - (y + 0.5f);
                const float d = std::sqrt(dx * dx + dy * dy);
                float a = 0;
                if (kind == 0)
                    a = std::clamp(n - d + 0.5f, 0.0f, 1.0f);
                else if (kind == 1)
                    a = std::clamp(n - d + 0.5f, 0.0f, 1.0f) * std::clamp(d - (n - thickness) + 0.5f, 0.0f, 1.0f);
                else
                {
                    const float t = std::clamp(d / n, 0.0f, 1.0f);
                    a = (1 - t) * (1 - t) * (1 - t);
                }
                base[static_cast<std::size_t>(y) * n + x] = 0x00FFFFFFu | (static_cast<std::uint32_t>(a * 255 + 0.5f) << 24);
            }
        for (int o = 0; o < 4; ++o)
        {
            std::vector<std::uint32_t> px(base.size());
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x)
                {
                    const int sx = (o & 1) ? n - 1 - x : x, sy = (o & 2) ? n - 1 - y : y;
                    px[static_cast<std::size_t>(y) * n + x] = base[static_cast<std::size_t>(sy) * n + sx];
                }
            set[static_cast<std::size_t>(o)] = make_texture(renderer_, n, n, px);
        }
    }
    return set.data();
}

void Gfx::draw_corners(const Texture *set, int x0, int y0, int x1, int y1, int r, Color color)
{
    const SDL_Rect pieces[4] = {{x0, y0, r, r}, {x1 - r, y0, r, r}, {x0, y1 - r, r, r}, {x1 - r, y1 - r, r, r}};
    for (int o = 0; o < 4; ++o)
    {
        SDL_Texture *t = set[o].get();
        SDL_SetTextureColorMod(t, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(t, color.a);
        SDL_RenderCopy(renderer_, t, nullptr, &pieces[o]);
    }
}


void Gfx::corner_pass(const Rect &rect, float radius, Color color, SDL_Texture *mask, int mask_size)
{
    (void)mask;
    (void)mask_size;
    const Box b = snap(rect, radius);
    if (b.r > 0)
        draw_corners(corners(0, b.r), b.x0, b.y0, b.x1, b.y1, b.r, color);
}

void Gfx::rounded(const Rect &rect, float radius, Color color)
{
    const Box b = snap(rect, radius);
    if (b.r < 1)
    {
        fill({static_cast<float>(b.x0), static_cast<float>(b.y0), static_cast<float>(b.x1 - b.x0),
              static_cast<float>(b.y1 - b.y0)}, color);
        return;
    }
    corner_pass(rect, radius, color, corner_.get(), 64);
    const auto f = [](int x, int y, int w, int h) {
        return Rect{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)};
    };
    fill(f(b.x0 + b.r, b.y0, b.x1 - b.x0 - 2 * b.r, b.y1 - b.y0), color);
    fill(f(b.x0, b.y0 + b.r, b.r, b.y1 - b.y0 - 2 * b.r), color);
    fill(f(b.x1 - b.r, b.y0 + b.r, b.r, b.y1 - b.y0 - 2 * b.r), color);
}

void Gfx::rounded_outline(const Rect &rect, float radius, float thickness, Color color)
{
    // Four straight edges on whole pixels plus baked corner rings.
    const Box b = snap(rect, radius);
    radius = static_cast<float>(b.r);
    const float t0 = std::max(1.0f, std::round(thickness));
    const float x0 = b.x0, y0 = b.y0, w = b.x1 - b.x0, h = b.y1 - b.y0;
    fill({x0 + radius, y0, w - 2 * radius, t0}, color);
    fill({x0 + radius, y0 + h - t0, w - 2 * radius, t0}, color);
    fill({x0, y0 + radius, t0, h - 2 * radius}, color);
    fill({x0 + w - t0, y0 + radius, t0, h - 2 * radius}, color);
    // Corners: antialiased quarter rings baked at their exact size.
    if (b.r > 0)
        draw_corners(corners(1, b.r, static_cast<int>(t0)), b.x0, b.y0, b.x1, b.y1, b.r, color);
}

const Texture &Gfx::panel_texture(int w, int h, int radius, Color left, Color right)
{
    const std::string key = std::to_string(w) + "x" + std::to_string(h) + "r" + std::to_string(radius) + ":" +
                            std::to_string(left.r << 16 | left.g << 8 | left.b) + "-" +
                            std::to_string(right.r << 16 | right.g << 8 | right.b);
    Texture &slot = panels_[key];
    if (slot)
        return slot;
    if (panels_.size() > 24)
    {
        panels_.clear();
        return panel_texture(w, h, radius, left, right);
    }
    Pixels pixels;
    pixels.width = w;
    pixels.height = h;
    pixels.rgba.resize(static_cast<std::size_t>(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float u = x / static_cast<float>(w), v = y / static_cast<float>(h);
            // Diagonal blend with a soft light in the upper right.
            const float k = std::clamp(u * 0.8f + (1 - v) * 0.2f, 0.0f, 1.0f);
            const float lx = (u - 0.85f) * 1.6f, ly = v - 0.2f;
            const float light = std::exp(-(lx * lx + ly * ly) * 3.5f) * 0.35f;
            const float r = left.r + (right.r - left.r) * k + 255 * light * 0.25f;
            const float g = left.g + (right.g - left.g) * k + 255 * light * 0.12f;
            const float b = left.b + (right.b - left.b) * k + 255 * light * 0.45f;
            pixels.rgba[static_cast<std::size_t>(y) * w + x] = pack(r, g, b, 255);
        }
    slot = upload(cover_crop(pixels, w, h, radius));
    slot.opaque_center = true;
    slot.corner = radius;
    return slot;
}

void Gfx::glow(const Rect &rect, float radius, Color color, float spread)
{
    const Rect outer{rect.x - spread, rect.y - spread, rect.w + 2 * spread, rect.h + 2 * spread};
    const Box b = snap(outer, radius + spread);
    if (b.r <= 0)
        return;
    const Texture *set = corners(2, b.r);
    draw_corners(set, b.x0, b.y0, b.x1, b.y1, b.r, color);
    // Edges: one column (or row) of the falloff stretched along each side.
    const int r = b.r, w = b.x1 - b.x0, h = b.y1 - b.y0;
    const SDL_Rect column{r - 1, 0, 1, r}, row{0, r - 1, r, 1};
    const SDL_Rect column_bottom{r - 1, 0, 1, r}, row_right{0, r - 1, r, 1};
    const SDL_Rect top{b.x0 + r, b.y0, w - 2 * r, r}, bottom{b.x0 + r, b.y1 - r, w - 2 * r, r};
    const SDL_Rect left{b.x0, b.y0 + r, r, h - 2 * r}, right{b.x1 - r, b.y0 + r, r, h - 2 * r};
    SDL_RenderCopy(renderer_, set[0].get(), &column, &top);
    SDL_RenderCopy(renderer_, set[2].get(), &column_bottom, &bottom);
    SDL_RenderCopy(renderer_, set[0].get(), &row, &left);
    SDL_RenderCopy(renderer_, set[1].get(), &row_right, &right);
    // The element drawn on top covers the centre; only small glows (text highlights) fill it.
    if (rect.w * rect.h < 40000)
        fill({static_cast<float>(b.x0 + r), static_cast<float>(b.y0 + r), static_cast<float>(w - 2 * r),
              static_cast<float>(h - 2 * r)}, color);
}

void Gfx::top_shade()
{
    const SDL_Rect dst{0, 0, kWidth, top_shade_.height()};
    SDL_RenderCopy(renderer_, top_shade_.get(), nullptr, &dst);
}

void Gfx::bottom_shade()
{
    const SDL_Rect dst{0, kHeight - bottom_shade_.height(), kWidth, bottom_shade_.height()};
    SDL_RenderCopy(renderer_, bottom_shade_.get(), nullptr, &dst);
}

void Gfx::begin_frame()
{
    ++frame_;
    if (lines_.size() > 900)
        for (auto it = lines_.begin(); it != lines_.end();)
            it = it->second.used + 90 < frame_ ? lines_.erase(it) : std::next(it);
    if (layouts_.size() > 400)
        layouts_.clear();
    if (fitted_.size() > 1500)
        fitted_.clear();
}

void Gfx::gradient_h(const Rect &rect, Color left, Color right)
{
    const SDL_Vertex v[4] = {
        {{rect.x, rect.y}, {left.r, left.g, left.b, left.a}, {0, 0}},
        {{rect.x + rect.w, rect.y}, {right.r, right.g, right.b, right.a}, {0, 0}},
        {{rect.x + rect.w, rect.y + rect.h}, {right.r, right.g, right.b, right.a}, {0, 0}},
        {{rect.x, rect.y + rect.h}, {left.r, left.g, left.b, left.a}, {0, 0}},
    };
    const int indices[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer_, nullptr, v, 4, indices, 6);
}

void Gfx::gradient_v(const Rect &rect, Color top, Color bottom)
{
    const SDL_Vertex v[4] = {
        {{rect.x, rect.y}, {top.r, top.g, top.b, top.a}, {0, 0}},
        {{rect.x + rect.w, rect.y}, {top.r, top.g, top.b, top.a}, {0, 0}},
        {{rect.x + rect.w, rect.y + rect.h}, {bottom.r, bottom.g, bottom.b, bottom.a}, {0, 0}},
        {{rect.x, rect.y + rect.h}, {bottom.r, bottom.g, bottom.b, bottom.a}, {0, 0}},
    };
    const int indices[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer_, nullptr, v, 4, indices, 6);
}

void Gfx::image(const Texture &texture, const Rect &rect, float alpha)
{
    if (!texture)
        return;
    const auto a = static_cast<std::uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255);
    SDL_SetTextureColorMod(texture.get(), 255, 255, 255);
    SDL_SetTextureAlphaMod(texture.get(), a);
    const SDL_FRect dst{rect.x, rect.y, rect.w, rect.h};
    if (!texture.opaque_center || a != 255 || texture.height() <= 2 * texture.corner)
    {
        SDL_RenderCopyF(renderer_, texture.get(), nullptr, &dst);
        return;
    }
    // Opaque middle band without blending; only the rounded top and bottom rows blend.
    // Bands are placed on whole pixels so they meet exactly when the tile is scaled.
    const int w = texture.width(), h = texture.height(), c = texture.corner;
    const int x0 = static_cast<int>(std::lround(rect.x)), y0 = static_cast<int>(std::lround(rect.y));
    const int x1 = static_cast<int>(std::lround(rect.x + rect.w)), y1 = static_cast<int>(std::lround(rect.y + rect.h));
    const int band = static_cast<int>(std::lround(c * (y1 - y0) / static_cast<float>(h)));
    if (c > 0)
    {
        const SDL_Rect top{0, 0, w, c}, bottom{0, h - c, w, c};
        const SDL_Rect dtop{x0, y0, x1 - x0, band}, dbottom{x0, y1 - band, x1 - x0, band};
        SDL_RenderCopy(renderer_, texture.get(), &top, &dtop);
        SDL_RenderCopy(renderer_, texture.get(), &bottom, &dbottom);
    }
    const SDL_Rect middle{0, c, w, h - 2 * c};
    const SDL_Rect dmiddle{x0, y0 + band, x1 - x0, y1 - y0 - 2 * band};
    SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_NONE);
    SDL_RenderCopy(renderer_, texture.get(), &middle, &dmiddle);
    SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);
}

void Gfx::image_cover(const Texture &texture, const Rect &rect, float alpha)
{
    if (!texture)
        return;
    const float sx = rect.w / texture.width(), sy = rect.h / texture.height();
    const float scale = std::max(sx, sy);
    const float cw = rect.w / scale, ch = rect.h / scale;
    const SDL_Rect src{static_cast<int>((texture.width() - cw) / 2), static_cast<int>((texture.height() - ch) / 2),
                       static_cast<int>(cw), static_cast<int>(ch)};
    SDL_SetTextureColorMod(texture.get(), 255, 255, 255);
    SDL_SetTextureAlphaMod(texture.get(), static_cast<std::uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255));
    const SDL_FRect dst{rect.x, rect.y, rect.w, rect.h};
    SDL_RenderCopyF(renderer_, texture.get(), &src, &dst);
}

void Gfx::circle(float cx, float cy, float radius, Color color)
{
    rounded({cx - radius, cy - radius, 2 * radius, 2 * radius}, radius, color);
}

Gfx::Line &Gfx::line(std::string_view text, int size, Weight weight)
{
    std::string key = std::to_string(size) + (weight == Weight::Bold ? "b|" : "r|");
    key.append(text);
    Line &entry = lines_[key];
    entry.used = frame_;
    if (!entry.texture && !text.empty())
    {
        Font &f = font(size, weight);
        int w = 0, h = 0;
        float left = 0;
        const std::vector<unsigned char> alpha = f.rasterize(text, w, h, left);
        std::vector<std::uint32_t> rgba(alpha.size());
        for (std::size_t i = 0; i < alpha.size(); ++i)
            rgba[i] = 0x00FFFFFFu | (static_cast<std::uint32_t>(alpha[i]) << 24);
        entry.texture = make_texture(renderer_, w, h, rgba);
        entry.left = left;
        entry.width = measure(text, size, weight);
    }
    return entry;
}

float Gfx::text(std::string_view text, float x, float y, int size, Color color, Weight weight, Align align,
                float max_width)
{
    std::string fitted;
    if (max_width > 0)
    {
        std::string key = std::to_string(size) + (weight == Weight::Bold ? "b" : "r") + std::to_string(static_cast<int>(max_width)) + "|";
        key.append(text);
        auto it = fitted_.find(key);
        if (it == fitted_.end())
            it = fitted_.emplace(key, measure(text, size, weight) > max_width ? ellipsize(text, size, max_width, weight)
                                                                               : std::string(text)).first;
        fitted = it->second;
        text = fitted;
    }
    const std::size_t newline = text.find('\n');
    if (newline != std::string_view::npos)
        text = text.substr(0, newline);
    if (text.empty())
        return 0;
    Line &l = line(text, size, weight);
    if (align == Align::Center)
        x -= l.width / 2;
    else if (align == Align::Right)
        x -= l.width;
    if (l.texture)
    {
        SDL_SetTextureColorMod(l.texture.get(), color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(l.texture.get(), color.a);
        // Whole pixels keep the cached bitmap sharp (no resampling in the software renderer).
        const SDL_Rect dst{static_cast<int>(std::lround(x + l.left)), static_cast<int>(std::lround(y - 1)),
                           l.texture.width(), l.texture.height()};
        SDL_RenderCopy(renderer_, l.texture.get(), nullptr, &dst);
    }
    return l.width;
}

float Gfx::measure(std::string_view text, int size, Weight weight)
{
    Font &f = font(size, weight);
    float width = 0;
    std::uint32_t previous = 0;
    std::size_t index = 0;
    while (index < text.size())
    {
        const std::uint32_t code = next_codepoint(text, index);
        if (code == '\n')
            break;
        const stbtt_packedchar *g = f.glyph(code);
        if (!g)
            continue;
        width += f.kern(previous, code) + g->xadvance;
        previous = code;
    }
    return width;
}

std::string Gfx::ellipsize(std::string_view text, int size, float max_width, Weight weight)
{
    if (measure(text, size, weight) <= max_width)
        return std::string(text);
    const float dots = measure("…", size, weight);
    std::string out;
    std::size_t index = 0;
    while (index < text.size())
    {
        const std::size_t start = index;
        next_codepoint(text, index);
        const std::string candidate = out + std::string(text.substr(start, index - start));
        if (measure(candidate, size, weight) + dots > max_width)
            break;
        out = candidate;
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out + "…";
}

float Gfx::paragraph(std::string_view text, const Rect &box, int size, Color color, int max_lines, Weight weight,
                     float line_height)
{
    std::string key = std::to_string(size) + (weight == Weight::Bold ? "b" : "r") + std::to_string(static_cast<int>(box.w)) +
                      "/" + std::to_string(max_lines) + "/" + std::to_string(static_cast<int>(box.h)) + "|";
    key.append(text);
    auto it = layouts_.find(key);
    if (it == layouts_.end())
    {
        std::vector<std::string> lines;
        const float advance = size * line_height;
        const int capacity = std::max(1, std::min(max_lines > 0 ? max_lines : 1000, static_cast<int>(box.h / advance) + 1));
        bool cut = false;
        for (const std::string &raw : split(text, '\n'))
        {
            std::string current;
            for (const std::string &word : split(raw, ' '))
            {
                const std::string candidate = current.empty() ? word : current + " " + word;
                if (current.empty() || measure(candidate, size, weight) <= box.w)
                {
                    current = candidate;
                    continue;
                }
                lines.push_back(current);
                current = word;
                if (static_cast<int>(lines.size()) >= capacity)
                {
                    cut = true;
                    break;
                }
            }
            if (cut)
                break;
            lines.push_back(current);
            if (static_cast<int>(lines.size()) >= capacity)
            {
                cut = lines.size() < split(text, '\n').size() || cut;
                break;
            }
        }
        if (static_cast<int>(lines.size()) > capacity)
            lines.resize(static_cast<std::size_t>(capacity));
        if (cut && !lines.empty())
            lines.back() = ellipsize(lines.back() + " …", size, box.w, weight);
        for (std::string &l : lines)
            if (measure(l, size, weight) > box.w)
                l = ellipsize(l, size, box.w, weight);
        it = layouts_.emplace(key, std::move(lines)).first;
    }
    const float advance = size * line_height;
    float y = box.y;
    for (const std::string &l : it->second)
    {
        if (!l.empty())
            this->text(l, box.x, y, size, color, weight);
        y += advance;
    }
    return y - box.y;
}

void Gfx::set_clip(const Rect *rect)
{
    if (!rect)
    {
        SDL_RenderSetClipRect(renderer_, nullptr);
        return;
    }
    const SDL_Rect r = rect->sdl();
    SDL_RenderSetClipRect(renderer_, &r);
}

} // namespace akeno::ui
