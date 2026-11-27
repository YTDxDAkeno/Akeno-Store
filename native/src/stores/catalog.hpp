// Akeno Gaming Hub - merged catalog of all enabled providers.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "stores/provider.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace akeno
{

struct ProviderStatus
{
    std::string id, label, about;
    enum class State
    {
        Idle,
        Loading,
        Ready,
        Offline,
        Failed
    } state = State::Idle;
    std::string message;
    std::size_t count = 0;
    bool enabled = true;
};

class Catalog
{
  public:
    Catalog(ProviderContext context, std::vector<std::unique_ptr<Provider>> providers);
    ~Catalog();

    // Starts a background refresh of every enabled provider (no-op while one runs).
    void refresh();
    bool refreshing() const { return refreshing_.load(); }
    std::uint64_t generation() const { return generation_.load(); }

    std::vector<Game> games() const;
    std::vector<ProviderStatus> status() const;
    void set_enabled(const std::string &provider, bool enabled);

    // Completes download data through the entry's provider (thread-safe).
    bool resolve(Game &game, std::string &error);

    // Merge rule, exposed for tests: entries with the same PS5 title ID collapse, the
    // signed catalog's entry wins and others contribute artwork and notes.
    static std::vector<Game> merge(const std::vector<std::vector<Game>> &lists);

  private:
    void run();

    ProviderContext context_;
    std::vector<std::unique_ptr<Provider>> providers_;
    mutable std::mutex mutex_;
    std::vector<std::vector<Game>> lists_;
    std::vector<Game> merged_;
    std::vector<ProviderStatus> status_;
    std::atomic<bool> refreshing_{false};
    std::atomic<std::uint64_t> generation_{0};
    std::thread worker_;
};

} // namespace akeno
