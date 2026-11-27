// Akeno Gaming Hub - artwork loading: network or file, disk cache, background decode.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/images.hpp"

#include "core/fs.hpp"
#include "core/util.hpp"

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

const Texture *ImageCache::get(const std::string &source, int w, int h, int radius)
{
    if (source.empty())
        return nullptr;
    const std::string key = source + "|" + std::to_string(w) + "x" + std::to_string(h) + "r" + std::to_string(radius);
    Slot &slot = slots_[key];
    slot.used = frame_;
    if (slot.texture)
        return &slot.texture;
    if (!slot.failed && !slot.queued)
    {
        slot.queued = true;
        std::lock_guard lock(mutex_);
        // Newest requests first: what is on screen now matters most.
        queue_.push_front({key, source, w, h, radius});
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

std::size_t ImageCache::pending() const
{
    std::lock_guard lock(mutex_);
    return queue_.size();
}

void ImageCache::pump(int budget)
{
    std::deque<std::pair<std::string, Pixels>> ready;
    std::vector<std::string> failed;
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
        slot.queued = false;
        slot.failed = !slot.texture;
    }
    for (const auto &key : failed)
    {
        Slot &slot = slots_[key];
        slot.queued = false;
        slot.failed = true;
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

bool ImageCache::load(const Request &request, Pixels &out)
{
    std::string encoded;
    if (!request.source.empty() && request.source[0] == '/')
    {
        auto data = fs::read_file(request.source, 16 * 1024 * 1024);
        if (!data)
            return false;
        encoded = std::move(*data);
    }
    else
    {
        if (!is_https_url(request.source))
            return false;
        const std::string cached = fs::join(cache_dir_, hex64(fnv1a(request.source)));
        if (auto data = fs::read_file(cached, 16 * 1024 * 1024))
            encoded = std::move(*data);
        else
        {
            const net::Response response = http_.get(request.source, 16 * 1024 * 1024, nullptr,
                                                     {{"Accept", "image/png,image/jpeg,image/gif,*/*;q=0.5"}});
            if (!response.ok())
                return false;
            encoded = response.body;
            fs::write_atomic(cached, encoded);
        }
    }
    Pixels decoded;
    if (!decode_image(encoded, request.w * 2, request.h * 2, decoded))
        return false;
    out = cover_crop(decoded, request.w, request.h, request.radius);
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
        const bool ok = load(request, pixels);
        std::lock_guard lock(mutex_);
        if (ok)
            ready_.emplace_back(request.key, std::move(pixels));
        else
            failed_.push_back(request.key);
    }
}

} // namespace akeno::ui
