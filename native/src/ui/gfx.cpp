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
    backdrop_ = make_texture(renderer_, bw, bh, back, SDL_BLENDMODE_NONE);
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
    if (time < 0)
        return; // reduced motion
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
    SDL_RenderFillRectF(renderer_, &r);
}

void Gfx::corner_pass(const Rect &rect, float radius, Color color, SDL_Texture *mask, int mask_size)
{
    (void)mask_size;
    SDL_SetTextureColorMod(mask, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(mask, color.a);
    const float r = radius;
    const SDL_FRect tl{rect.x, rect.y, r, r}, tr{rect.x + rect.w - r, rect.y, r, r},
        bl{rect.x, rect.y + rect.h - r, r, r}, br{rect.x + rect.w - r, rect.y + rect.h - r, r, r};
    SDL_RenderCopyExF(renderer_, mask, nullptr, &tl, 0, nullptr, SDL_FLIP_NONE);
    SDL_RenderCopyExF(renderer_, mask, nullptr, &tr, 0, nullptr, SDL_FLIP_HORIZONTAL);
    SDL_RenderCopyExF(renderer_, mask, nullptr, &bl, 0, nullptr, SDL_FLIP_VERTICAL);
    SDL_RenderCopyExF(renderer_, mask, nullptr, &br, 0, nullptr,
                      static_cast<SDL_RendererFlip>(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL));
}

void Gfx::rounded(const Rect &rect, float radius, Color color)
{
    radius = std::min({radius, rect.w / 2, rect.h / 2});
    if (radius < 1)
    {
        fill(rect, color);
        return;
    }
    corner_pass(rect, radius, color, corner_.get(), 64);
    fill({rect.x + radius, rect.y, rect.w - 2 * radius, rect.h}, color);
    fill({rect.x, rect.y + radius, radius, rect.h - 2 * radius}, color);
    fill({rect.x + rect.w - radius, rect.y + radius, radius, rect.h - 2 * radius}, color);
}

void Gfx::rounded_outline(const Rect &rect, float radius, float thickness, Color color)
{
    // Four straight edges plus corner arcs approximated by short segments.
    radius = std::min({radius, rect.w / 2, rect.h / 2});
    fill({rect.x + radius, rect.y, rect.w - 2 * radius, thickness}, color);
    fill({rect.x + radius, rect.y + rect.h - thickness, rect.w - 2 * radius, thickness}, color);
    fill({rect.x, rect.y + radius, thickness, rect.h - 2 * radius}, color);
    fill({rect.x + rect.w - thickness, rect.y + radius, thickness, rect.h - 2 * radius}, color);
    // Corners: an antialiased quarter ring, baked once per (radius, thickness).
    const int r = std::max(1, static_cast<int>(std::lround(radius)));
    const int t = std::max(1, static_cast<int>(std::lround(thickness)));
    Texture &ring = rings_[{r, t}];
    if (!ring)
    {
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(r) * r);
        for (int y = 0; y < r; ++y)
            for (int x = 0; x < r; ++x)
            {
                const float dx = r - (x + 0.5f), dy = r - (y + 0.5f);
                const float d = std::sqrt(dx * dx + dy * dy);
                const float outer = std::clamp(r - d + 0.5f, 0.0f, 1.0f);
                const float inner = std::clamp(d - (r - t) + 0.5f, 0.0f, 1.0f);
                pixels[static_cast<std::size_t>(y * r + x)] = pack(255, 255, 255, outer * inner * 255);
            }
        ring = make_texture(renderer_, r, r, pixels);
    }
    corner_pass(rect, radius, color, ring.get(), r);
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
    return slot;
}

void Gfx::glow(const Rect &rect, float radius, Color color, float spread)
{
    const Rect outer{rect.x - spread, rect.y - spread, rect.w + 2 * spread, rect.h + 2 * spread};
    const float r = std::min({radius + spread, outer.w / 2, outer.h / 2});
    SDL_Texture *mask = glow_corner_.get();
    corner_pass(outer, r, color, mask, 64);
    SDL_SetTextureColorMod(mask, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(mask, color.a);
    // Edges: one column (or row) of the falloff stretched along each side.
    const SDL_Rect column{63, 0, 1, 64}, row{0, 63, 64, 1};
    const SDL_FRect top{outer.x + r, outer.y, outer.w - 2 * r, r}, bottom{outer.x + r, outer.y + outer.h - r, outer.w - 2 * r, r};
    const SDL_FRect left{outer.x, outer.y + r, r, outer.h - 2 * r}, right{outer.x + outer.w - r, outer.y + r, r, outer.h - 2 * r};
    SDL_RenderCopyExF(renderer_, mask, &column, &top, 0, nullptr, SDL_FLIP_NONE);
    SDL_RenderCopyExF(renderer_, mask, &column, &bottom, 0, nullptr, SDL_FLIP_VERTICAL);
    SDL_RenderCopyExF(renderer_, mask, &row, &left, 0, nullptr, SDL_FLIP_NONE);
    SDL_RenderCopyExF(renderer_, mask, &row, &right, 0, nullptr, SDL_FLIP_HORIZONTAL);
    fill({outer.x + r, outer.y + r, outer.w - 2 * r, outer.h - 2 * r}, color);
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
    SDL_SetTextureColorMod(texture.get(), 255, 255, 255);
    SDL_SetTextureAlphaMod(texture.get(), static_cast<std::uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255));
    const SDL_FRect dst{rect.x, rect.y, rect.w, rect.h};
    SDL_RenderCopyF(renderer_, texture.get(), nullptr, &dst);
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

float Gfx::text(std::string_view text, float x, float y, int size, Color color, Weight weight, Align align,
                float max_width)
{
    std::string fitted;
    if (max_width > 0 && measure(text, size, weight) > max_width)
    {
        fitted = ellipsize(text, size, max_width, weight);
        text = fitted;
    }
    Font &f = font(size, weight);
    const float width = measure(text, size, weight);
    if (align == Align::Center)
        x -= width / 2;
    else if (align == Align::Right)
        x -= width;
    SDL_Texture *atlas = f.atlas().get();
    if (!atlas)
        return width;
    SDL_SetTextureColorMod(atlas, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(atlas, color.a);
    float pen = x;
    const float baseline = y + f.ascent();
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
        pen += f.kern(previous, code);
        previous = code;
        stbtt_aligned_quad q;
        float px = pen, py = baseline;
        stbtt_GetPackedQuad(g, f.side(), f.side(), 0, &px, &py, &q, 0);
        const SDL_Rect src{static_cast<int>(q.s0 * f.side()), static_cast<int>(q.t0 * f.side()),
                           static_cast<int>((q.s1 - q.s0) * f.side() + 0.5f),
                           static_cast<int>((q.t1 - q.t0) * f.side() + 0.5f)};
        const SDL_FRect dst{q.x0, q.y0, q.x1 - q.x0, q.y1 - q.y0};
        if (src.w > 0 && src.h > 0)
            SDL_RenderCopyF(renderer_, atlas, &src, &dst);
        pen = px;
    }
    return width;
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
    const float advance = size * line_height;
    float y = box.y;
    int lines = 0;
    for (const std::string &raw : split(text, '\n'))
    {
        std::string line;
        const auto words = split(raw, ' ');
        for (std::size_t i = 0; i < words.size(); ++i)
        {
            const std::string candidate = line.empty() ? words[i] : line + " " + words[i];
            if (measure(candidate, size, weight) <= box.w || line.empty())
            {
                line = candidate;
                continue;
            }
            if (lines + 1 == max_lines)
            {
                std::string rest = line;
                for (std::size_t j = i; j < words.size(); ++j)
                    rest += " " + words[j];
                this->text(ellipsize(rest, size, box.w, weight), box.x, y, size, color, weight);
                return y + advance - box.y;
            }
            this->text(line, box.x, y, size, color, weight);
            y += advance;
            ++lines;
            line = words[i];
        }
        if (lines + 1 >= max_lines && max_lines > 0)
        {
            this->text(ellipsize(line, size, box.w, weight), box.x, y, size, color, weight);
            return y + advance - box.y;
        }
        this->text(line, box.x, y, size, color, weight);
        y += advance;
        ++lines;
        if (y > box.y + box.h)
            break;
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
