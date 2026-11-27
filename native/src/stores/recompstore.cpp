// Akeno Gaming Hub - recompstore.com catalog (recompiled games and community ports).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The catalog at https://www.recompstore.com/catalog.json is not signed and most entries
// carry no SHA-256. Each entry names a GitHub repository and asset; Akeno installs only
// when GitHub publishes the asset's SHA-256 digest (and it matches the catalog when the
// catalog has one). Games that need the player's own disc image say so in `needs`.
#include "core/json.hpp"
#include "core/util.hpp"
#include "stores/github.hpp"
#include "stores/provider.hpp"

#include <set>

namespace akeno
{

namespace parse
{
bool recompstore_catalog(std::string_view catalog, std::vector<Game> &out, std::string &error)
{
    const json::Document doc = json::Document::parse(catalog, 16 * 1024 * 1024);
    const json::Value root = doc.root();
    if (!root.is_object() || (!root["v2Apps"].is_array() && !root["apps"].is_array()))
    {
        error = doc.ok() ? "catalog.json has an unexpected shape" : doc.error();
        return false;
    }
    std::set<std::string> seen;
    const auto add = [&](json::Value app, bool upcoming) {
        const std::string title_id = app.str("titleId", app.str("id"));
        if (!app.is_object() || title_id.size() < 7 || title_id.size() > 9 || !seen.insert(title_id).second)
            return;
        Game game;
        game.provider = "recompstore";
        game.provider_label = "RecompStore";
        game.id = "recompstore:" + title_id;
        game.title_id = is_title_id(title_id) ? title_id : std::string();
        game.title = sanitize_text(app.str("name"), 160);
        if (game.title.empty())
            return;
        game.developer = sanitize_text(app.str("author", app.str("dev")), 120);
        game.description = sanitize_text(app.str("description", app.str("blurb")), 4000);
        game.category = app.str("category") == "games" ? "Recomps & Ports" : sanitize_text(app.str("category"), 40);
        game.kind = app.str("category") == "games" ? "game" : "app";
        game.version = sanitize_text(app.str("version"), 60);
        game.icon_url = app.str("icon");
        game.hero_url = app.str("hero");
        game.cover_url = game.icon_url;
        game.needs = sanitize_text(app.str("needs"), 400);
        game.sha256 = lower(app.str("sha256"));
        game.page_url = "https://www.recompstore.com/game/" + title_id;
        app["screens"].each([&](json::Value shot) {
            const std::string url = shot.is_string() ? shot.str() : shot.str("url");
            if (is_https_url(url) && game.screenshots.size() < 8)
                game.screenshots.push_back(url);
        });
        const json::Value source = app["source"];
        if (source.str("type") == "github")
        {
            game.source_repo = source.str("repo");
            game.asset_name = source.str("asset");
        }
        game.runtime = "native";
        game.price = "Kostenlos";
        game.tags = {"ps5-native", "free"};
        if (game.kind == "game")
            game.tags.push_back("game");
        if (!game.needs.empty())
            game.tags.push_back("bring-your-own");
        if (app.str("category") == "games")
            game.tags.push_back("recomp");
        if (app.str("category") == "emulators")
            game.tags.push_back("emulator");
        app["tags"].each([&](json::Value tag) {
            if (tag.is_string() && game.tags.size() < 16)
                game.tags.push_back(lower(sanitize_text(tag.str(), 30)));
        });
        game.access = upcoming ? Access::Upcoming
                               : (!game.source_repo.empty() && !game.asset_name.empty() ? Access::Install
                                                                                         : Access::External);
        out.push_back(std::move(game));
    };
    root["v2Apps"].each([&](json::Value app) { add(app, false); });
    root["apps"].each([&](json::Value app) { add(app, false); });
    root["comingSoonV2"].each([&](json::Value app) { add(app, true); });
    root["comingSoon"].each([&](json::Value app) { add(app, true); });
    return true;
}
} // namespace parse

namespace
{

class RecompStore final : public Provider
{
  public:
    const char *id() const override { return "recompstore"; }
    const char *label() const override { return "RecompStore"; }
    const char *about() const override
    {
        return "Recompilations und Community-Ports (recompstore.com). Der Katalog ist nicht signiert: "
               "Akeno installiert nur GitHub-Assets mit veröffentlichtem SHA-256. Spiele mit eigenem "
               "Datenträger-Image nennen das benötigte Original.";
    }

    bool refresh(ProviderContext &context, std::vector<Game> &out, std::string &error) override
    {
        const net::Response response = context.http->get("https://www.recompstore.com/catalog.json", 16 * 1024 * 1024);
        if (!response.ok())
        {
            error = response.error.empty() ? "recompstore.com answered HTTP " + std::to_string(response.status)
                                           : response.error;
            return false;
        }
        return parse::recompstore_catalog(response.body, out, error);
    }

    bool resolve(ProviderContext &context, Game &game, std::string &error) override
    {
        if (game.source_repo.empty())
        {
            error = "The catalog names no GitHub source for this entry";
            return false;
        }
        return resolve_github_latest(context, game.source_repo, game.asset_name, game, error);
    }
};

} // namespace

std::unique_ptr<Provider> make_recompstore_provider() { return std::make_unique<RecompStore>(); }

} // namespace akeno
