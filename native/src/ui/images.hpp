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
    const Texture *get(const std::string &source, int w, int h, int radius = 0);
    // Uploads decoded images; call once per frame on the render thread.
    void pump(int budget = 3);
    // Requests that were asked for this frame are kept; others become evictable.
    void next_frame() { ++frame_; }
    std::size_t pending() const;

  private:
    struct Request
    {
        std::string key, source;
        int w, h, radius;
    };
    struct Slot
    {
        Texture texture;
        bool failed = false;
        bool queued = false;
        std::uint64_t used = 0;
    };
    void worker();
    bool load(const Request &request, Pixels &out);

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
    std::vector<std::string> failed_;
    bool stopping_ = false;
    std::vector<std::thread> workers_;
};

} // namespace akeno::ui
