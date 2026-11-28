// Akeno Gaming Hub - the curated Akeno catalog: free, online and fan games from the
// master list (section 9), each routed to what really runs it on a PS5.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The catalog ships inside the title (assets/catalog/akeno.json) and may be replaced by a
// newer revision from the project's repository. It holds facts, never downloads: native
// ports point at their PS5 title ID (the signed ProsperoStore listing supplies the ZIP and
// its SHA-256); "library" entries name original game files that Akeno downloads, detects
// and plays with an integrated runtime. Everything else is listed with the reason it does
// not run yet, so nothing in the catalog pretends to be playable.
#include "core/fs.hpp"
#include "core/json.hpp"
#include "core/util.hpp"
#include "stores/provider.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace akeno
{

namespace
{

bool valid_slug(const std::string &text, std::size_t limit)
{
    if (text.empty() || text.size() > limit || text == "." || text == "..")
        return false;
    for (char c : text)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.'))
            return false;
    return true;
}

// A folder name under C:\Games: letters, digits, space, dot, dash, underscore.
bool valid_folder(const std::string &text)
{
    if (text.empty() || text.size() > 64 || text.front() == ' ' || text.back() == ' ' || text.back() == '.' ||
        text == "." || text == "..")
        return false;
    for (char c : text)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == ' '))
            return false;
    return true;
}

void strings(json::Value list, std::vector<std::string> &out, std::size_t max, std::size_t length)
{
    list.each([&](json::Value item) {
        if (item.is_string() && out.size() < max)
        {
            const std::string text = sanitize_text(item.str(), length);
            if (!text.empty())
                out.push_back(text);
        }
    });
}

} // namespace

const char *compat_label(Compat compat)
{
    switch (compat)
    {
    case Compat::Verified:
        return "Verifiziert spielbar";
    case Compat::Limited:
        return "Spielbar mit Einschränkungen";
    case Compat::Experimental:
        return "Experimentell";
    case Compat::Boots:
        return "Startet, Gameplay unbestätigt";
    case Compat::Incompatible:
        return "Nicht kompatibel";
    case Compat::Untested:
        break;
    }
    return "Noch nicht getestet";
}

const char *compat_key(Compat compat)
{
    switch (compat)
    {
    case Compat::Verified:
        return "verified";
    case Compat::Limited:
        return "limited";
    case Compat::Experimental:
        return "experimental";
    case Compat::Boots:
        return "boots";
    case Compat::Incompatible:
        return "incompatible";
    case Compat::Untested:
        break;
    }
    return "untested";
}

Compat compat_from_key(std::string_view key)
{
    if (key == "verified")
        return Compat::Verified;
    if (key == "limited")
        return Compat::Limited;
    if (key == "experimental")
        return Compat::Experimental;
    if (key == "boots")
        return Compat::Boots;
    if (key == "incompatible")
        return Compat::Incompatible;
    return Compat::Untested;
}

namespace parse
{
bool akeno_catalog(std::string_view text, std::vector<Game> &out, std::string &error, std::int64_t *revision)
{
    const json::Document doc = json::Document::parse(text, 4 * 1024 * 1024);
    const json::Value root = doc.root();
    if (!root.is_object() || root["schema"].integer() != 1 || !root["games"].is_array())
    {
        error = doc.ok() ? "akeno.json has an unexpected shape" : doc.error();
        return false;
    }
    if (revision)
        *revision = root["revision"].integer();
    std::set<std::string> seen;
    root["games"].each([&](json::Value entry) {
        const std::string key = entry.str("id");
        const std::string route = entry.str("route");
        if (!entry.is_object() || !valid_slug(key, 48) || !seen.insert(key).second)
            return;
        Game game;
        game.provider = "akeno";
        game.provider_label = "Akeno-Katalog";
        game.id = "akeno:" + key;
        game.title = sanitize_text(entry.str("title"), 160);
        if (game.title.empty())
            return;
        game.developer = sanitize_text(entry.str("developer"), 160);
        game.description = sanitize_text(entry.str("description"), 4000);
        game.license = sanitize_text(entry.str("license"), 120);
        game.needs = sanitize_text(entry.str("needs"), 400);
        game.online = sanitize_text(entry.str("online"), 200);
        game.crossplay = sanitize_text(entry.str("crossplay"), 200);
        game.controller = sanitize_text(entry.str("controller"), 200);
        game.age = sanitize_text(entry.str("age"), 60);
        game.version = sanitize_text(entry.str("version"), 60);
        game.category = sanitize_text(entry.str("category", "Spiele"), 40);
        game.kind = "game";
        game.price = sanitize_text(entry.str("price", "Kostenlos"), 40);
        game.free = entry["free"].boolean(true);
        game.adult = entry["adult"].boolean(false);
        if (const std::string page = entry.str("page"); is_https_url(page))
            game.page_url = page;
        if (const std::string icon = entry.str("icon"); is_https_url(icon))
            game.icon_url = game.cover_url = icon;
        if (const std::string hero = entry.str("hero"); is_https_url(hero))
            game.hero_url = hero;
        entry["screenshots"].each([&](json::Value shot) {
            if (is_https_url(shot.str()) && game.screenshots.size() < 8)
                game.screenshots.push_back(shot.str());
        });
        strings(entry["genres"], game.genres, 6, 40);
        std::vector<std::string> tags;
        strings(entry["tags"], tags, 16, 30);
        for (std::string &tag : tags)
            game.tags.push_back(lower(tag));
        game.tags.push_back("curated");
        if (game.free)
            game.tags.push_back("free");
        const json::Value compat = entry["compat"];
        game.compat = compat_from_key(compat.str("status"));
        game.compat_note = sanitize_text(compat.str("note"), 600);

        if (route == "ps5")
        {
            // A native port: the signed ProsperoStore listing with this title ID supplies the
            // install data; this entry adds the curated facts (Catalog::merge).
            const std::string title_id = entry.str("title_id");
            if (!is_title_id(title_id))
                return;
            game.title_id = title_id;
            game.runtime = "native";
            game.access = Access::External;
            if (game.page_url.empty())
                game.page_url = "https://prosperostore.net/app/" + title_id + "/";
            game.tags.push_back("ps5-native");
        }
        else if (route == "web")
        {
            // An online browser game: it needs Akeno's HTML5 runtime, which is not integrated yet.
            const std::string url = entry.str("url");
            if (!is_https_url(url))
                return;
            game.launch_url = url;
            game.runtime = "web";
            game.access = Access::Runtime;
            if (game.page_url.empty())
                game.page_url = url;
            game.tags.push_back("browser");
        }
        else if (route == "library")
        {
            // Original game files Akeno downloads, detects and plays with an integrated runtime.
            game.artifact_url = entry.str("download");
            game.sha256 = lower(entry.str("sha256"));
            game.size = static_cast<std::uint64_t>(std::max<std::int64_t>(0, entry["size"].integer()));
            game.profile = "akeno-" + key;
            game.engine_hint = entry.str("engine");
            if (!is_https_url(game.artifact_url) || (game.sha256.size() != 64 && game.size == 0))
                return;
            game.runtime = "akeno";
            game.access = Access::Install;
            game.resolved = true;
        }
        else if (route == "external")
        {
            game.runtime = sanitize_text(entry.str("platform", "pc"), 20);
            game.access = Access::External;
            if (game.runtime == "windows" || game.runtime == "pc")
                game.tags.push_back("pc");
        }
        else
            return;
        out.push_back(std::move(game));
    });
    return true;
}
} // namespace parse

namespace
{

constexpr const char *kRemoteCatalog =
    "https://raw.githubusercontent.com/YTDxDAkeno/Akeno-Store/main/native/assets/catalog/akeno.json";

class AkenoCatalog final : public Provider
{
  public:
    const char *id() const override { return "akeno"; }
    const char *label() const override { return "Akeno-Katalog"; }
    const char *about() const override
    {
        return "Kuratierte kostenlose, Online- und Fanspiele mit ehrlichem Kompatibilitätsstatus. Native Ports "
               "kommen aus dem signierten ProsperoStore; alle anderen zeigen, welche Akeno-Runtime sie brauchen "
               "und wie weit sie ist.";
    }

    bool refresh(ProviderContext &context, std::vector<Game> &out, std::string &error) override
    {
        // The copy inside the title, then a newer revision from the repository if there is one.
        std::vector<Game> bundled;
        std::int64_t bundled_revision = -1;
        std::string bundled_error;
        if (const auto text = fs::read_file(fs::join(context.asset_dir, "catalog/akeno.json"), 4 * 1024 * 1024))
            if (!parse::akeno_catalog(*text, bundled, bundled_error, &bundled_revision))
                bundled_revision = -1;
        std::vector<Game> remote;
        std::int64_t remote_revision = -1;
        if (context.http)
        {
            const net::Response response = context.http->get(kRemoteCatalog, 4 * 1024 * 1024);
            std::string remote_error;
            if (!response.ok() || !parse::akeno_catalog(response.body, remote, remote_error, &remote_revision))
                remote_revision = -1;
        }
        if (remote_revision <= bundled_revision && bundled_revision < 0)
        {
            error = bundled_error.empty() ? "The catalog inside the title is missing" : bundled_error;
            return false;
        }
        out = remote_revision > bundled_revision ? std::move(remote) : std::move(bundled);
        if (!context.allow_adult)
            out.erase(std::remove_if(out.begin(), out.end(), [](const Game &g) { return g.adult; }), out.end());
        return true;
    }

};

} // namespace

std::unique_ptr<Provider> make_akeno_provider() { return std::make_unique<AkenoCatalog>(); }

} // namespace akeno
