// Akeno Gaming Hub - 2D drawing on SDL2: text, rounded shapes, textures, gradients.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Everything is drawn in a fixed 1920x1080 logical space. Expensive work (glyph atlases,
// corner masks, the background) happens once; frames are texture copies, so the PS5's
// software SDL renderer keeps up.
#pragma once

#include <SDL.h>

#include "core/image.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace akeno::ui
{

constexpr int kWidth = 1920;
constexpr int kHeight = 1080;

struct Color
{
    std::uint8_t r, g, b, a = 255;
    Color with_alpha(float alpha) const
    {
        return {r, g, b, static_cast<std::uint8_t>(a * (alpha < 0 ? 0 : alpha > 1 ? 1 : alpha))};
    }
};

Color mix(Color a, Color b, float t);

struct Rect
{
    float x = 0, y = 0, w = 0, h = 0;
    SDL_Rect sdl() const;
    Rect inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

class Texture
{
  public:
    Texture() = default;
    Texture(SDL_Texture *texture, int w, int h) : texture_(texture), w_(w), h_(h) {}
    ~Texture();
    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;
    Texture(Texture &&other) noexcept { *this = std::move(other); }
    Texture &operator=(Texture &&other) noexcept;

    SDL_Texture *get() const { return texture_; }
    int width() const { return w_; }
    int height() const { return h_; }
    explicit operator bool() const { return texture_ != nullptr; }

  private:
    SDL_Texture *texture_ = nullptr;
    int w_ = 0, h_ = 0;
};

enum class Weight
{
    Regular,
    Bold
};

enum class Align
{
    Left,
    Center,
    Right
};

class Font;

class Gfx
{
  public:
    explicit Gfx(SDL_Renderer *renderer);
    ~Gfx();
    bool load_fonts(const std::string &regular_ttf, const std::string &bold_ttf);

    SDL_Renderer *renderer() const { return renderer_; }
    Texture upload(const Pixels &pixels);

    void clear(Color color);
    void background(float time); // the animated hub backdrop
    void fill(const Rect &rect, Color color);
    void rounded(const Rect &rect, float radius, Color color);
    void rounded_outline(const Rect &rect, float radius, float thickness, Color color);
    void glow(const Rect &rect, float radius, Color color, float spread); // soft outer glow
    void gradient_h(const Rect &rect, Color left, Color right);
    void gradient_v(const Rect &rect, Color top, Color bottom);
    void image(const Texture &texture, const Rect &rect, float alpha = 1.0f);
    // Fills `rect` with the texture scaled to cover it, cropping the overflow.
    void image_cover(const Texture &texture, const Rect &rect, float alpha = 1.0f);
    void circle(float cx, float cy, float radius, Color color);

    // Text in logical pixels; size picks the nearest baked font size.
    float text(std::string_view text, float x, float y, int size, Color color, Weight weight = Weight::Regular,
               Align align = Align::Left, float max_width = 0);
    float measure(std::string_view text, int size, Weight weight = Weight::Regular);
    // Word-wraps into at most `max_lines` lines (the last ends with an ellipsis when cut).
    float paragraph(std::string_view text, const Rect &box, int size, Color color, int max_lines,
                    Weight weight = Weight::Regular, float line_height = 1.35f);
    std::string ellipsize(std::string_view text, int size, float max_width, Weight weight = Weight::Regular);

    void set_clip(const Rect *rect);
    // A rounded panel with a diagonal gradient, baked once and cached.
    const Texture &panel_texture(int w, int h, int radius, Color left, Color right);

  private:
    Font &font(int size, Weight weight);
    void corner_pass(const Rect &rect, float radius, Color color, SDL_Texture *mask, int mask_size);

    SDL_Renderer *renderer_;
    std::string font_data_[2];
    std::map<std::pair<int, int>, std::unique_ptr<Font>> fonts_;
    Texture corner_;      // antialiased quarter disc, 64 px
    Texture glow_corner_; // soft quarter falloff, 64 px
    Texture backdrop_;
    Texture orb_;
    std::map<std::pair<int, int>, Texture> rings_;
    std::map<std::string, Texture> panels_;
};

} // namespace akeno::ui
