// Akeno Gaming Hub - prosperostore.net signed catalog (API v1, schema 3).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Contract: https://github.com/darkxex/ps5-homebrew-catalog/blob/main/docs/api.md
// manifest.json is signed with Ed25519 (manifest.sig, 64 raw bytes); either published
// key may sign. Every API file Akeno uses must match the SHA-256 listed in the manifest,
// and the manifest's sequence may never go backwards.
#include "core/fs.hpp"
#include "core/hash.hpp"
#include "core/json.hpp"
#include "core/util.hpp"
#include "platform/platform.hpp"
#include "stores/provider.hpp"

#include <cstdlib>
#include <map>
#include <mutex>

namespace akeno
{
namespace
{

constexpr const char *kApi = "https://prosperostore.net/api/v1/";
// keys/catalog-signing-1.pub.pem (da351006acb6e3c3) and catalog-signing-2.pub.pem (eef399ea3007720a)
constexpr const char *kKeys[] = {
    "87391bf1698ecef101bf5e29dc8585ee5947d571e19470de7411c5d3b137b5cf",
    "509bcfab7edfb4e5ed23639488517c6ef2657c13b6b7f2bf699c8989d9b0dd7b",
};

std::string label_for_category(const std::string &category)
{
    static const std::map<std::string, std::string> names = {
        {"console-ports", "Konsolen-Ports"}, {"pc-ports", "PC-Ports"},   {"emulators", "Emulatoren"},
        {"utilities", "Werkzeuge"},          {"video", "Video"},         {"homebrew-games", "Homebrew-Spiele"},
        {"ai", "KI"},                        {"streaming", "Streaming"}, {"music", "Musik"},
        {"tools", "Werkzeuge"},              {"showcases", "Showcases"}};
    const auto it = names.find(category);
    return it == names.end() ? category : it->second;
}

void apply_common(json::Value app, Game &game)
{
    game.provider = "prosperostore";
    game.provider_label = "ProsperoStore";
    game.title_id = app.str("titleid");
    const std::string id = app.str("id", game.title_id);
    game.id = "prosperostore:" + id;
    game.title = sanitize_text(app.str("name"), 160);
    game.developer = sanitize_text(app.str("author", app.str("owner")), 120);
    game.description = sanitize_text(app.str("description"), 4000);
    game.kind = app.str("kind");
    game.category = label_for_category(app.str("category"));
    game.version = sanitize_text(app.str("version"), 60);
    game.content_version = app.str("content_version");
    game.format = app.str("format");
    game.size = static_cast<std::uint64_t>(std::max<std::int64_t>(0, app["size"].integer(0)));
    game.icon_url = app.str("icon_small", app.str("icon"));
    game.cover_url = app.str("icon", game.icon_url);
    // index.json carries no page address; the API documents /app/<id>/ on the website.
    game.page_url = app.str("page", "https://prosperostore.net/app/" + id + "/");
    game.released = app.str("released");
    game.updated = app.str("updated");
    game.signed_catalog = true;
    game.free = true;
    game.price = "Kostenlos";
    game.runtime = "native";
    game.sandbox = app["safety"].is_object() ? app["safety"].str("sandbox") : app.str("sandbox");
    const std::string status = app.str("status");
    if (status == "coming_soon")
        game.access = Access::Upcoming;
    else if (game.format == "zip")
        game.access = Access::Install;
    else
        game.access = Access::External; // image formats are listed but not installed
    game.tags.clear();
    game.tags.push_back("ps5-native");
    game.tags.push_back("free");
    if (game.kind == "game")
        game.tags.push_back("game");
    const std::string category = app.str("category");
    if (category == "console-ports" || category == "pc-ports")
        game.tags.push_back("port");
    if (category == "emulators")
        game.tags.push_back("emulator");
    if (category == "homebrew-games")
        game.tags.push_back("homebrew");
}

} // namespace

namespace parse
{

bool prosperostore_manifest(std::string_view manifest, std::string_view signature, std::string &error,
                            std::int64_t *sequence)
{
    if (signature.size() != 64)
    {
        error = "manifest.sig has the wrong size";
        return false;
    }
    bool verified = false;
    for (const char *hex : kKeys)
    {
        std::uint8_t key[32];
        from_hex(hex, key, sizeof key);
        if (ed25519_verify(key, reinterpret_cast<const std::uint8_t *>(signature.data()), manifest))
        {
            verified = true;
            break;
        }
    }
    if (!verified)
    {
        error = "The catalog signature does not verify";
        return false;
    }
    const json::Document doc = json::Document::parse(manifest, 4 * 1024 * 1024);
    const json::Value root = doc.root();
    if (!root.is_object() || root["schema"].integer() != 3 || !root["files"].is_object())
    {
        error = "The catalog manifest has an unexpected shape";
        return false;
    }
    if (sequence)
        *sequence = root["sequence"].integer(-1);
    return true;
}

bool prosperostore_index(std::string_view index, std::vector<Game> &out, std::string &error)
{
    const json::Document doc = json::Document::parse(index, 16 * 1024 * 1024);
    const json::Value root = doc.root();
    if (!root.is_object() || root["schema"].integer() != 3 || !root["apps"].is_array())
    {
        error = doc.ok() ? "index.json has an unexpected shape" : doc.error();
        return false;
    }
    const auto add = [&](json::Value app) {
        if (!app.is_object())
            return;
        const std::string title_id = app.str("titleid");
        const std::string status = app.str("status");
        if (title_id.size() != 9 || (status != "available" && status != "coming_soon"))
            return;
        Game game;
        apply_common(app, game);
        if (game.title.empty())
            return;
        out.push_back(std::move(game));
    };
    root["apps"].each(add);
    root["shared"].each(add);
    return true;
}

bool prosperostore_app(std::string_view app_json, Game &game, std::string &error)
{
    const json::Document doc = json::Document::parse(app_json, 2 * 1024 * 1024);
    const json::Value app = doc.root();
    if (!app.is_object() || app["schema"].integer() != 3 || app.str("titleid") != game.title_id)
    {
        error = "The app record does not match the catalog entry";
        return false;
    }
    if (app.str("status") != "available")
    {
        error = "This app has no release to install yet";
        return false;
    }
    apply_common(app, game);
    game.license = sanitize_text(app.str("license"), 120);
    game.source_repo = app.str("source_repo");
    game.release_notes = sanitize_text(app.str("release_notes"), 4000);
    game.artifact_url = app.str("artifact_url");
    game.sha256 = lower(app.str("sha256"));
    if (game.format != "zip")
    {
        error = "Only ZIP releases can be installed (this one is " + game.format + ")";
        return false;
    }
    if (!is_https_url(game.artifact_url) || game.sha256.size() != 64)
    {
        error = "The app record has no valid download";
        return false;
    }
    return true;
}

} // namespace parse

namespace
{

class ProsperoStore final : public Provider
{
  public:
    const char *id() const override { return "prosperostore"; }
    const char *label() const override { return "ProsperoStore"; }
    const char *about() const override
    {
        return "Signierter Community-Katalog nativer PS5-Homebrew (prosperostore.net). "
               "Ed25519-Signatur, Sequenzschutz und SHA-256 jeder Datei werden geprüft.";
    }

    bool refresh(ProviderContext &context, std::vector<Game> &out, std::string &error) override
    {
        const std::string cache = fs::join(context.cache_dir, "prosperostore");
        fs::make_dirs(cache);
        std::string manifest, signature, index;
        bool online = fetch(context, "manifest.json", manifest, 4 * 1024 * 1024, error) &&
                      fetch(context, "manifest.sig", signature, 1024, error) &&
                      fetch(context, "index.json", index, 16 * 1024 * 1024, error);
        if (online && accept(context, manifest, signature, index, error))
        {
            fs::write_atomic(fs::join(cache, "manifest.json"), manifest);
            fs::write_atomic(fs::join(cache, "manifest.sig"), signature);
            fs::write_atomic(fs::join(cache, "index.json"), index);
            return parse::prosperostore_index(index, out, error);
        }
        // Offline: the cached copy is verified again exactly like a fresh one.
        const std::string online_error = error;
        auto cm = fs::read_file(fs::join(cache, "manifest.json"));
        auto cs = fs::read_file(fs::join(cache, "manifest.sig"));
        auto ci = fs::read_file(fs::join(cache, "index.json"));
        std::string cached_error;
        if (cm && cs && ci && accept(context, *cm, *cs, *ci, cached_error) &&
            parse::prosperostore_index(*ci, out, cached_error))
        {
            error = "Offline – gespeicherter, geprüfter Katalog (" + online_error + ")";
            return true;
        }
        error = online_error.empty() ? cached_error : online_error;
        return false;
    }

    bool resolve(ProviderContext &context, Game &game, std::string &error) override
    {
        const std::string key = game.id.substr(game.id.find(':') + 1);
        const std::string path = "apps/" + key + ".json";
        std::string expected;
        {
            std::lock_guard lock(mutex_);
            const auto it = files_.find(path);
            if (it != files_.end())
                expected = it->second;
        }
        if (expected.empty())
        {
            error = "The app is not listed in the verified manifest";
            return false;
        }
        std::string body;
        if (!fetch(context, path, body, 2 * 1024 * 1024, error))
            return false;
        if (!digest_equal(sha256_hex(body), expected))
        {
            error = "The app record does not match the signed manifest (try again after a refresh)";
            return false;
        }
        if (!parse::prosperostore_app(body, game, error))
            return false;
        game.resolved = true;
        return true;
    }

  private:
    bool fetch(ProviderContext &context, const std::string &path, std::string &body, std::uint64_t limit,
               std::string &error)
    {
        const net::Response response = context.http->get(std::string(kApi) + path, limit);
        if (!response.ok())
        {
            error = response.error.empty() ? "prosperostore.net answered HTTP " + std::to_string(response.status)
                                           : response.error;
            return false;
        }
        body = response.body;
        return true;
    }

    bool accept(ProviderContext &context, const std::string &manifest, const std::string &signature,
                const std::string &index, std::string &error)
    {
        std::int64_t sequence = -1;
        if (!parse::prosperostore_manifest(manifest, signature, error, &sequence))
            return false;
        const std::string state = fs::join(context.state_dir, "prosperostore.sequence");
        const auto stored = fs::read_file(state, 64);
        const std::int64_t highest = stored ? std::atoll(stored->c_str()) : -1;
        if (sequence < highest)
        {
            error = "The catalog is older than one already accepted (rollback refused)";
            return false;
        }
        const json::Document doc = json::Document::parse(manifest, 4 * 1024 * 1024);
        std::map<std::string, std::string> files;
        doc.root()["files"].each_member([&](std::string_view name, json::Value value) {
            files[std::string(name)] = lower(value.str());
        });
        const auto listed = files.find("index.json");
        if (listed == files.end() || !digest_equal(sha256_hex(index), listed->second))
        {
            error = "index.json does not match the signed manifest";
            return false;
        }
        if (sequence > highest)
            fs::write_atomic(state, std::to_string(sequence));
        std::lock_guard lock(mutex_);
        files_ = std::move(files);
        return true;
    }

    std::mutex mutex_;
    std::map<std::string, std::string> files_;
};

} // namespace

std::unique_ptr<Provider> make_prosperostore_provider() { return std::make_unique<ProsperoStore>(); }

} // namespace akeno
