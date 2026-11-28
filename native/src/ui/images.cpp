// Akeno Gaming Hub - artwork loading: network or file, disk cache, background decode.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/images.hpp"

#include "core/fs.hpp"
#include "core/util.hpp"
#include "platform/platform.hpp"

#include <algorithm>

namespace akeno::ui
{

ImageCache::ImageCache(Gfx &gfx, net::Client &http, std::string cache_dir, std::size_t max_textures)
    : gfx_(gfx), http_(http), cache_dir_(std::move(cache_dir)), max_textures_(max_textures)
{
    fs::make_dirs(cache_dir_);
    for (int i = 0; i < 3; ++i)
        workers_.emplace_back([this] { worker(); });
}

ImageCache::~ImageCache()
{
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        queue_.clear();
    }
    wake_.notify_all();
    for (auto &thread : workers_)
        thread.join();
}

const Texture *ImageCache::get(const std::string &source, int w, int h, int radius, int effect)
{
    if (source.empty())
        return nullptr;
    const std::string key = source + "|" + std::to_string(w) + "x" + std::to_string(h) + "e" + std::to_string(effect) +
                            "r" + std::to_string(radius);
    Slot &slot = slots_[key];
    slot.used = frame_;
    if (slot.texture)
        return &slot.texture;
    // A picture that failed (a network hiccup while the console starts, for example) is asked
    // for again after 20 seconds, at most three times.
    if (slot.failed && slot.attempts < 3 && platform::monotonic_ms() - slot.failed_at > 20000)
        slot.failed = false;
    if (!slot.failed && !slot.queued)
    {
        slot.queued = true;
        std::lock_guard lock(mutex_);
        // Newest requests first: what is on screen now matters most.
        queue_.push_front({key, source, w, h, radius, effect});
        if (queue_.size() > 400)
        {
            // slots_ belongs to the render thread (this one); a dropped request may be asked again.
            slots_[queue_.back().key].queued = false;
            queue_.pop_back();
        }
        wake_.notify_one();
    }
    return nullptr;
}

bool ImageCache::failed(const std::string &source) const
{
    const auto it = failed_sources_.find(source);
    return it != failed_sources_.end() && it->second >= 3;
}

std::string ImageCache::cached_file(const std::string &source) const
{
    if (!source.empty() && source[0] == '/')
        return fs::is_file(source) ? source : std::string();
    const std::string path = fs::join(cache_dir_, hex64(fnv1a(source)));
    return is_https_url(source) && fs::is_file(path) ? path : std::string();
}

ImageCache::Stats ImageCache::stats() const
{
    Stats stats;
    stats.loaded = loaded_count_;
    stats.failed = failed_count_;
    stats.last_error = last_error_;
    return stats;
}

std::size_t ImageCache::pending() const
{
    std::lock_guard lock(mutex_);
    return queue_.size();
}

void ImageCache::pump(int budget)
{
    std::deque<std::pair<std::string, Pixels>> ready;
    std::vector<std::pair<std::string, std::string>> failed;
    {
        std::lock_guard lock(mutex_);
        while (!ready_.empty() && budget-- > 0)
        {
            ready.push_back(std::move(ready_.front()));
            ready_.pop_front();
        }
        failed.swap(failed_);
    }
    for (auto &[key, pixels] : ready)
    {
        Slot &slot = slots_[key];
        slot.texture = gfx_.upload(pixels);
        slot.texture.opaque_center = true;
        slot.texture.corner = static_cast<int>(std::stoi(key.substr(key.rfind('r') + 1)));
        slot.queued = false;
        slot.failed = !slot.texture;
        if (slot.texture)
            ++loaded_count_;
    }
    for (const auto &[key, reason] : failed)
    {
        Slot &slot = slots_[key];
        slot.queued = false;
        slot.failed = true;
        slot.failed_at = platform::monotonic_ms();
        ++slot.attempts;
        ++failed_count_;
        const std::string source = key.substr(0, key.rfind('|'));
        failed_sources_[source] = std::max(failed_sources_[source], slot.attempts);
        last_error_ = host_of(source) + ": " + reason;
    }
    // Evict the longest-unused textures beyond the budget.
    std::size_t loaded = 0;
    for (const auto &[key, slot] : slots_)
        loaded += slot.texture ? 1 : 0;
    while (loaded > max_textures_)
    {
        auto oldest = slots_.end();
        for (auto it = slots_.begin(); it != slots_.end(); ++it)
            if (it->second.texture && it->second.used + 2 < frame_ &&
                (oldest == slots_.end() || it->second.used < oldest->second.used))
                oldest = it;
        if (oldest == slots_.end())
            break;
        slots_.erase(oldest);
        --loaded;
    }
}

bool ImageCache::load(const Request &request, Pixels &out, std::string &reason)
{
    std::string encoded;
    if (!request.source.empty() && request.source[0] == '/')
    {
        auto data = fs::read_file(request.source, 16 * 1024 * 1024);
        if (!data)
        {
            reason = "Datei fehlt";
            return false;
        }
        encoded = std::move(*data);
    }
    else
    {
        if (!is_https_url(request.source))
        {
            reason = "keine HTTPS-Adresse";
            return false;
        }
        const std::string cached = fs::join(cache_dir_, hex64(fnv1a(request.source)));
        if (auto data = fs::read_file(cached, 16 * 1024 * 1024))
            encoded = std::move(*data);
        else
        {
            const net::Response response = http_.get(request.source, 16 * 1024 * 1024, nullptr,
                                                     {{"Accept", "image/png,image/jpeg,image/gif,*/*;q=0.5"}});
            if (!response.ok())
            {
                reason = response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error;
                return false;
            }
            encoded = response.body;
            fs::write_atomic(cached, encoded);
        }
    }
    Pixels decoded;
    if (!decode_image(encoded, request.w * 2, request.h * 2, decoded))
    {
        // stb_image reads PNG, JPEG, GIF and BMP; WebP and AVIF are not decoded.
        const bool webp = encoded.size() > 12 && encoded.compare(8, 4, "WEBP") == 0;
        reason = webp ? "WebP-Bild (nicht unterstützt)" : "Bildformat nicht lesbar";
        fs::remove_tree(fs::join(cache_dir_, hex64(fnv1a(request.source))));
        return false;
    }
    static const std::uint32_t tile = 0xFF54241Eu; // theme panel_hi (30, 36, 84)
    out = cover_crop(decoded, request.w, request.h, request.radius, &tile);
    const auto blend = [](std::uint32_t &pixel, float r, float g, float b, float k) {
        float c[3] = {static_cast<float>(pixel & 0xFF), static_cast<float>((pixel >> 8) & 0xFF),
                      static_cast<float>((pixel >> 16) & 0xFF)};
        const float target[3] = {r, g, b};
        for (int i = 0; i < 3; ++i)
            c[i] = c[i] * (1 - k) + target[i] * k;
        pixel = (pixel & 0xFF000000u) | static_cast<std::uint32_t>(c[0]) | static_cast<std::uint32_t>(c[1]) << 8 |
                static_cast<std::uint32_t>(c[2]) << 16;
    };
    if (request.effect == 1)
    {
        // Page backdrop: 30 % of the picture over the navy page colour, fading out at the bottom.
        for (int y = 0; y < out.height; ++y)
        {
            const float fade = std::clamp((y - out.height * 0.45f) / (out.height * 0.55f), 0.0f, 1.0f);
            for (int x = 0; x < out.width; ++x)
                blend(out.rgba[static_cast<std::size_t>(y) * out.width + x], 5, 7, 20, 0.7f + 0.3f * fade);
        }
    }
    else if (request.effect == 2)
    {
        // Hero art: darken the left 70 % so the title stays readable.
        for (int y = 0; y < out.height; ++y)
            for (int x = 0; x < out.width; ++x)
            {
                const float t = std::clamp(x / (out.width * 0.7f), 0.0f, 1.0f);
                blend(out.rgba[static_cast<std::size_t>(y) * out.width + x], 6, 8, 24, 0.92f * (1 - t) * (1 - t));
            }
    }
    return true;
}

void ImageCache::worker()
{
    for (;;)
    {
        Request request;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
            if (stopping_)
                return;
            request = std::move(queue_.front());
            queue_.pop_front();
        }
        Pixels pixels;
        std::string reason;
        const bool ok = load(request, pixels, reason);
        std::lock_guard lock(mutex_);
        if (ok)
            ready_.emplace_back(request.key, std::move(pixels));
        else
            failed_.emplace_back(request.key, reason);
    }
}

} // namespace akeno::ui
