// Akeno Gaming Hub - merged catalog of all enabled providers.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "stores/catalog.hpp"

#include "platform/platform.hpp"

#include <algorithm>
#include <map>

namespace akeno
{

Catalog::Catalog(ProviderContext context, std::vector<std::unique_ptr<Provider>> providers)
    : context_(std::move(context)), providers_(std::move(providers))
{
    lists_.resize(providers_.size());
    for (const auto &provider : providers_)
    {
        ProviderStatus status;
        status.id = provider->id();
        status.label = provider->label();
        status.about = provider->about();
        status_.push_back(status);
    }
}

Catalog::~Catalog()
{
    if (worker_.joinable())
        worker_.join();
}

void Catalog::refresh()
{
    bool expected = false;
    if (!refreshing_.compare_exchange_strong(expected, true))
        return;
    if (worker_.joinable())
        worker_.join();
    worker_ = std::thread([this] { run(); });
}

void Catalog::run()
{
    // Providers run in parallel; each publishes its list as soon as it is ready.
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < providers_.size(); ++i)
    {
        {
            std::lock_guard lock(mutex_);
            if (!status_[i].enabled)
                continue;
            status_[i].state = ProviderStatus::State::Loading;
            status_[i].message.clear();
        }
        threads.emplace_back([this, i] {
            std::vector<Game> list;
            std::string error;
            const std::uint64_t started = platform::monotonic_ms();
            const bool ok = providers_[i]->refresh(context_, list, error);
            platform::log("[catalog] %s: %s, %zu entries in %llu ms%s%s", providers_[i]->id(), ok ? "ok" : "failed",
                          list.size(), static_cast<unsigned long long>(platform::monotonic_ms() - started),
                          error.empty() ? "" : " – ", error.c_str());
            std::lock_guard lock(mutex_);
            ProviderStatus &status = status_[i];
            status.count = list.size();
            status.message = error;
            status.state = !ok ? ProviderStatus::State::Failed
                         : error.empty() ? ProviderStatus::State::Ready
                                         : ProviderStatus::State::Offline;
            if (ok)
                lists_[i] = std::move(list);
            merged_ = merge(lists_);
            generation_.fetch_add(1);
        });
    }
    for (auto &thread : threads)
        thread.join();
    refreshing_ = false;
    generation_.fetch_add(1);
}

std::vector<Game> Catalog::games() const
{
    std::lock_guard lock(mutex_);
    return merged_;
}

std::vector<ProviderStatus> Catalog::status() const
{
    std::lock_guard lock(mutex_);
    return status_;
}

void Catalog::set_enabled(const std::string &provider, bool enabled)
{
    std::lock_guard lock(mutex_);
    for (std::size_t i = 0; i < status_.size(); ++i)
        if (status_[i].id == provider)
        {
            status_[i].enabled = enabled;
            if (!enabled)
            {
                lists_[i].clear();
                status_[i].state = ProviderStatus::State::Idle;
                status_[i].count = 0;
            }
        }
    merged_ = merge(lists_);
    generation_.fetch_add(1);
}

void Catalog::set_itch_key(const std::string &key)
{
    std::lock_guard lock(mutex_);
    context_.itch_key = key;
}

bool Catalog::resolve(Game &game, std::string &error)
{
    ProviderContext context;
    {
        std::lock_guard lock(mutex_);
        context = context_;
    }
    for (const auto &provider : providers_)
        if (game.provider == provider->id())
            return provider->resolve(context, game, error);
    error = "Unknown store";
    return false;
}

std::vector<Game> Catalog::merge(const std::vector<std::vector<Game>> &lists)
{
    std::vector<Game> merged;
    std::map<std::string, std::size_t> by_title;
    std::map<std::string, std::size_t> by_id;
    for (const auto &list : lists)
        for (const Game &game : list)
        {
            if (by_id.count(game.id))
                continue;
            if (!game.title_id.empty())
            {
                const auto it = by_title.find(game.title_id);
                if (it != by_title.end())
                {
                    Game &kept = merged[it->second];
                    // The first list is the signed catalog: it keeps its install data.
                    if (kept.hero_url.empty())
                        kept.hero_url = game.hero_url;
                    if (kept.needs.empty())
                        kept.needs = game.needs;
                    if (kept.screenshots.empty())
                        kept.screenshots = game.screenshots;
                    if (kept.description.size() < game.description.size() / 2)
                        kept.description = game.description;
                    for (const std::string &tag : game.tags)
                        if (!kept.has_tag(tag))
                            kept.tags.push_back(tag);
                    // Curated facts (Akeno catalog) complete a store listing.
                    if (kept.genres.empty())
                        kept.genres = game.genres;
                    if (kept.online.empty())
                        kept.online = game.online;
                    if (kept.crossplay.empty())
                        kept.crossplay = game.crossplay;
                    if (kept.controller.empty())
                        kept.controller = game.controller;
                    if (kept.age.empty())
                        kept.age = game.age;
                    if (kept.compat == Compat::Untested && kept.compat_note.empty())
                    {
                        kept.compat = game.compat;
                        kept.compat_note = game.compat_note;
                    }
                    // e.g. an image-only ProsperoStore listing that RecompStore offers as a ZIP
                    if (kept.access != Access::Install && game.access == Access::Install)
                        kept = game;
                    by_id[game.id] = it->second;
                    continue;
                }
                by_title[game.title_id] = merged.size();
            }
            by_id[game.id] = merged.size();
            merged.push_back(game);
        }
    return merged;
}

} // namespace akeno
