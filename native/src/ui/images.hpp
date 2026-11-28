// Akeno Gaming Hub - artwork loading: network or file, disk cache, background decode.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "net/http.hpp"
#include "ui/gfx.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace akeno::ui
{

class ImageCache
{
  public:
    ImageCache(Gfx &gfx, net::Client &http, std::string cache_dir, std::size_t max_textures = 260);
    ~ImageCache();

    // The texture for `source` (an HTTPS URL or an absolute file path) cropped to w x h
    // with rounded corners, or nullptr while it loads / when it cannot be loaded.
    // `effect`: 0 plain, 1 dimmed page backdrop fading into the background, 2 hero art with
    // a dark left side for text. Effects are baked in once, so frames copy opaque pixels.
    const Texture *get(const std::string &source, int w, int h, int radius = 0, int effect = 0);
    // Uploads decoded images; call once per frame on the render thread.
    void pump(int budget = 3);
    // Requests that were asked for this frame are kept; others become evictable.
    void next_frame() { ++frame_; }
    std::size_t pending() const;
    // True once every attempt to load `source` failed (the store has no readable picture).
    bool failed(const std::string &source) const;
    // The downloaded file of an HTTPS image, or "" when it was never loaded.
    std::string cached_file(const std::string &source) const;
    struct Stats
    {
        std::size_t loaded = 0, failed = 0;
        std::string last_error; // "<host>: <reason>"
    };
    Stats stats() const;

  private:
    struct Request
    {
        std::string key, source;
        int w, h, radius, effect;
    };
    struct Slot
    {
        Texture texture;
        bool failed = false;
        bool queued = false;
        std::uint64_t used = 0;
        std::uint64_t failed_at = 0; // ms; a failed picture is tried again later, three times in all
        int attempts = 0;
    };
    void worker();
    bool load(const Request &request, Pixels &out, std::string &reason);

    Gfx &gfx_;
    net::Client &http_;
    std::string cache_dir_;
    std::size_t max_textures_;
    std::uint64_t frame_ = 0;
    std::map<std::string, Slot> slots_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Request> queue_;
    std::deque<std::pair<std::string, Pixels>> ready_;
    std::vector<std::pair<std::string, std::string>> failed_; // key, reason
    std::map<std::string, int> failed_sources_;               // source -> failed attempts (render thread)
    std::size_t loaded_count_ = 0, failed_count_ = 0;
    std::string last_error_;
    bool stopping_ = false;
    std::vector<std::thread> workers_;
};

} // namespace akeno::ui
