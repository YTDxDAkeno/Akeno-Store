// Akeno Gaming Hub - store provider interface and the built-in providers.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "net/http.hpp"
#include "stores/game.hpp"

#include <memory>
#include <string>
#include <vector>

namespace akeno
{

struct ProviderContext
{
    net::Client *http = nullptr;
    std::string cache_dir; // provider-owned files live in <cache_dir>/<provider id>/
    std::string state_dir; // small persistent state (sequence numbers)
    std::string asset_dir; // read-only files shipped in the title (the bundled Akeno catalog)
    bool allow_adult = false;
};

class Provider
{
  public:
    virtual ~Provider() = default;
    virtual const char *id() const = 0;
    virtual const char *label() const = 0;
    // Short sentence about trust and what the store offers, shown in settings.
    virtual const char *about() const = 0;
    // Fetches the current listing. On failure returns false and sets `error`; a provider
    // may still return a verified cached listing (then `error` says it is offline data).
    virtual bool refresh(ProviderContext &context, std::vector<Game> &out, std::string &error) = 0;
    // Completes the install data for one entry (download URL and SHA-256).
    virtual bool resolve(ProviderContext &context, Game &game, std::string &error)
    {
        (void)context;
        if (!game.artifact_url.empty() && !game.sha256.empty())
        {
            game.resolved = true;
            return true;
        }
        error = "This entry has no verified download";
        return false;
    }
};

std::unique_ptr<Provider> make_prosperostore_provider();
std::unique_ptr<Provider> make_recompstore_provider();
std::unique_ptr<Provider> make_github_provider(std::vector<std::string> repositories);
std::unique_ptr<Provider> make_itch_provider();
std::unique_ptr<Provider> make_gamejolt_provider();
std::unique_ptr<Provider> make_gog_provider();
std::unique_ptr<Provider> make_epic_provider();
std::unique_ptr<Provider> make_akeno_provider();

// Parsers exposed for tests: they take the raw documents a provider downloads.
namespace parse
{
bool prosperostore_manifest(std::string_view manifest, std::string_view signature, std::string &error,
                            std::int64_t *sequence);
bool prosperostore_index(std::string_view index, std::vector<Game> &out, std::string &error);
bool prosperostore_app(std::string_view app, Game &game, std::string &error);
bool recompstore_catalog(std::string_view catalog, std::vector<Game> &out, std::string &error);
bool github_release(std::string_view release, const std::string &wanted_asset, Game &game, std::string &error);
bool itch_feed(std::string_view rss, bool allow_adult, std::vector<Game> &out, std::string &error);
bool gamejolt_discover(std::string_view json, bool allow_adult, std::vector<Game> &out, std::string &error);
bool gog_catalog(std::string_view json, std::vector<Game> &out, std::string &error);
bool akeno_catalog(std::string_view catalog, std::vector<Game> &out, std::string &error, std::int64_t *revision);
bool epic_promotions(std::string_view json, std::int64_t now, std::vector<Game> &out, std::string &error);
} // namespace parse

} // namespace akeno
