// Akeno Gaming Hub - discovery providers: itch.io, Game Jolt, GOG and Epic Games.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// These stores publish public listings but no console packages. Akeno shows what they
// offer and marks each entry honestly:
//  - browser games (itch.io HTML, Game Jolt HTML) run in the Web runtime (ProsperoWeb);
//  - downloads for PC need the store's own account flow; Akeno links the store page.
// Nothing here downloads or launches an executable.
#include "core/json.hpp"
#include "core/util.hpp"
#include "stores/provider.hpp"

#include <ctime>
#include <set>

namespace akeno
{
namespace
{

// Text of the first <tag>...</tag> inside `block`, CDATA unwrapped, entities decoded.
std::string xml_text(std::string_view block, std::string_view tag)
{
    const std::string open = "<" + std::string(tag) + ">", close = "</" + std::string(tag) + ">";
    const std::size_t start = block.find(open);
    if (start == std::string_view::npos)
        return {};
    const std::size_t end = block.find(close, start + open.size());
    if (end == std::string_view::npos)
        return {};
    std::string_view text = block.substr(start + open.size(), end - start - open.size());
    if (starts_with(text, "<![CDATA[") && ends_with(text, "]]>"))
        return std::string(text.substr(9, text.size() - 12));
    return decode_entities(text);
}

bool looks_restricted(std::string_view text)
{
    static const char *words[] = {"nsfw", "18+", "adult", "hentai", "erotic", "porn", "explicit"};
    for (const char *word : words)
        if (contains_folded(text, word))
            return true;
    return false;
}

void classify_title(Game &game, const std::string &raw_title)
{
    if (contains_folded(raw_title, "[Visual Novel]"))
        game.tags.push_back("visual-novel");
    if (contains_folded(raw_title, "[Role Playing]"))
        game.tags.push_back("rpg");
    if (contains_folded(raw_title, "fan game") || contains_folded(raw_title, "fangame"))
        game.tags.push_back("fan-project");
}

} // namespace

namespace parse
{

bool itch_feed(std::string_view rss, bool allow_adult, std::vector<Game> &out, std::string &error)
{
    if (rss.find("<rss") == std::string_view::npos || rss.find("<!DOCTYPE") != std::string_view::npos ||
        rss.find("<!ENTITY") != std::string_view::npos)
    {
        error = "The itch.io feed has an unexpected shape";
        return false;
    }
    std::size_t at = 0;
    std::set<std::string> seen;
    while ((at = rss.find("<item>", at)) != std::string_view::npos && out.size() < 400)
    {
        const std::size_t end = rss.find("</item>", at);
        if (end == std::string_view::npos)
            break;
        const std::string_view item = rss.substr(at, end - at);
        at = end;
        const std::string link = trim(xml_text(item, "link"));
        if (!is_https_url(link) || !ends_with(host_of(link), ".itch.io") || !seen.insert(link).second)
            continue;
        const std::string raw_title = xml_text(item, "title");
        std::string title = trim(xml_text(item, "plainTitle"));
        if (title.empty())
            title = raw_title;
        const std::string description = strip_html(xml_text(item, "description"));
        if (!allow_adult && (looks_restricted(raw_title) || looks_restricted(description)))
            continue;
        Game game;
        game.provider = "itch";
        game.provider_label = "itch.io";
        game.id = "itch:" + lower(host_of(link)) + "/" + link.substr(link.find(".itch.io/") + 9);
        game.title = sanitize_text(title, 160);
        game.developer = host_of(link).substr(0, host_of(link).size() - 8);
        game.description = sanitize_text(description, 2000);
        game.icon_url = trim(xml_text(item, "imageurl"));
        game.cover_url = game.icon_url;
        game.page_url = link;
        game.price = "Kostenlos";
        game.free = true;
        game.released = xml_text(item, "pubDate");
        game.tags = {"free", "indie"};
        classify_title(game, raw_title);
        const std::string platforms = xml_text(item, "platforms");
        if (platforms.find("<html>yes</html>") != std::string::npos)
        {
            game.runtime = "web";
            game.launch_url = link;
            game.access = Access::Runtime;
            game.tags.push_back("browser");
        }
        else
        {
            game.runtime = platforms.find("<windows>yes</windows>") != std::string::npos ? "windows" : "pc";
            game.access = Access::External;
        }
        out.push_back(std::move(game));
    }
    return true;
}

bool gamejolt_discover(std::string_view text, bool allow_adult, std::vector<Game> &out, std::string &error)
{
    const json::Document doc = json::Document::parse(text, 16 * 1024 * 1024);
    const json::Value games = doc.root()["payload"]["games"];
    if (!games.is_array())
    {
        error = doc.ok() ? "Game Jolt returned no game list" : doc.error();
        return false;
    }
    games.each([&](json::Value g) {
        const std::int64_t id = g["id"].integer(0);
        const std::string slug = g.str("slug");
        if (id <= 0 || slug.empty())
            return;
        if (g["has_adult_content"].boolean(false) && !allow_adult)
            return;
        Game game;
        game.provider = "gamejolt";
        game.provider_label = "Game Jolt";
        game.id = "gamejolt:" + std::to_string(id);
        game.title = sanitize_text(g.str("title"), 160);
        game.developer = sanitize_text(g["developer"].str("display_name", g["developer"].str("username")), 120);
        // The thumbnail is WebP; the original upload is PNG or JPEG, which Akeno decodes.
        const json::Value thumb = g["thumbnail_media_item"];
        const std::string filetype = thumb.str("filetype");
        if (filetype == "image/png" || filetype == "image/jpeg" || filetype == "image/gif")
            game.cover_url = thumb.str("img_url");
        game.icon_url = game.cover_url;
        game.page_url = "https://gamejolt.com/games/" + slug + "/" + std::to_string(id);
        const bool free = g["sellable"].str("type", "free") == "free";
        game.free = free;
        game.price = free ? "Kostenlos" : "Kostenpflichtig";
        game.tags = {"indie"};
        if (free)
            game.tags.push_back("free");
        if (g["compatibility"]["type_html"].boolean(false))
        {
            game.runtime = "web";
            game.launch_url = game.page_url;
            game.access = Access::Runtime;
            game.tags.push_back("browser");
        }
        else
        {
            game.runtime = g["compatibility"]["os_windows"].boolean(false) ? "windows" : "pc";
            game.access = Access::External;
        }
        if (!game.title.empty())
            out.push_back(std::move(game));
    });
    return true;
}

bool gog_catalog(std::string_view text, std::vector<Game> &out, std::string &error)
{
    const json::Document doc = json::Document::parse(text, 16 * 1024 * 1024);
    const json::Value products = doc.root()["products"];
    if (!products.is_array())
    {
        error = doc.ok() ? "GOG returned no products" : doc.error();
        return false;
    }
    products.each([&](json::Value p) {
        const std::string id = p.str("id");
        if (id.empty())
            return;
        Game game;
        game.provider = "gog";
        game.provider_label = "GOG";
        game.id = "gog:" + id;
        game.title = sanitize_text(p.str("title"), 160);
        p["developers"].each([&](json::Value d) {
            if (game.developer.empty())
                game.developer = sanitize_text(d.str(), 120);
        });
        game.cover_url = p.str("coverVertical");
        game.hero_url = p.str("coverHorizontal");
        game.icon_url = game.cover_url;
        game.page_url = p.str("storeLink");
        game.released = p.str("releaseDate");
        p["screenshots"].each([&](json::Value s) {
            std::string url = s.str();
            const std::size_t token = url.find("{formatter}");
            if (token != std::string::npos)
                url.replace(token, 11, "product_card_v2_mobile_slider_639");
            if (is_https_url(url) && game.screenshots.size() < 6)
                game.screenshots.push_back(url);
        });
        const std::string amount = p["price"]["finalMoney"].str("amount");
        game.free = amount == "0.00";
        game.price = game.free ? "Kostenlos" : sanitize_text(p["price"].str("final"), 30);
        game.tags = {"pc"};
        if (game.free)
            game.tags.push_back("free");
        bool windows = false;
        p["operatingSystems"].each([&](json::Value os) { windows |= os.str() == "windows"; });
        p["features"].each([&](json::Value f) {
            const std::string slug = f.str("slug");
            if (slug == "single")
                game.tags.push_back("singleplayer");
            if (slug == "multi" || slug == "online_multiplayer")
                game.tags.push_back("online");
            if (slug == "controller_support")
                game.tags.push_back("controller");
        });
        game.runtime = windows ? "windows" : "pc";
        game.description = "GOG-Titel für " + std::string(windows ? "Windows" : "PC") +
                           ". Download und Kauf laufen über dein GOG-Konto; Windows-Spiele "
                           "lassen sich danach mit einer Windows-Runtime (MihawkOS, PC5) starten.";
        game.access = Access::External;
        if (!game.title.empty())
            out.push_back(std::move(game));
    });
    return true;
}

bool epic_promotions(std::string_view text, std::int64_t now, std::vector<Game> &out, std::string &error)
{
    const json::Document doc = json::Document::parse(text, 16 * 1024 * 1024);
    const json::Value elements = doc.root()["data"]["Catalog"]["searchStore"]["elements"];
    if (!elements.is_array())
    {
        error = doc.ok() ? "Epic returned no promotions" : doc.error();
        return false;
    }
    const auto parse_time = [](const std::string &iso) -> std::int64_t {
        int y = 0, m = 0, d = 0, hh = 0, mm = 0, ss = 0;
        if (iso.size() < 19 || std::sscanf(iso.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &m, &d, &hh, &mm, &ss) != 6)
            return 0;
        // Days from the civil date (H. Hinnant's algorithm): no timegm in the console's libc.
        y -= m <= 2;
        const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
        const std::int64_t yoe = y - era * 400;
        const std::int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        const std::int64_t days = era * 146097 + doe - 719468;
        return days * 86400 + hh * 3600 + mm * 60 + ss;
    };
    elements.each([&](json::Value e) {
        bool free_now = false;
        e["promotions"]["promotionalOffers"].each([&](json::Value group) {
            group["promotionalOffers"].each([&](json::Value offer) {
                const std::int64_t start = parse_time(offer.str("startDate"));
                const std::int64_t end = parse_time(offer.str("endDate"));
                // discountPercentage is the share of the price that remains: 0 means free.
                if (offer["discountSetting"]["discountPercentage"].integer(-1) == 0 && start <= now &&
                    (end == 0 || now < end))
                    free_now = true;
            });
        });
        if (!free_now || e["price"]["totalPrice"]["discountPrice"].integer(-1) != 0)
            return;
        Game game;
        game.provider = "epic";
        game.provider_label = "Epic Games";
        game.id = "epic:" + e.str("id");
        game.title = sanitize_text(e.str("title"), 160);
        game.developer = sanitize_text(e["seller"].str("name"), 120);
        game.description = sanitize_text(e.str("description"), 2000);
        e["keyImages"].each([&](json::Value image) {
            const std::string type = image.str("type"), url = image.str("url");
            if (type == "OfferImageTall" && game.cover_url.empty())
                game.cover_url = url + "?h=480&resize=1&w=360";
            if (type == "OfferImageWide" && game.hero_url.empty())
                game.hero_url = url + "?h=720&resize=1&w=1280";
        });
        game.icon_url = game.cover_url;
        std::string slug = e.str("productSlug");
        e["catalogNs"]["mappings"].each([&](json::Value m) {
            if (slug.empty() && m.str("pageType") == "productHome")
                slug = m.str("pageSlug");
        });
        if (!slug.empty())
            game.page_url = "https://store.epicgames.com/p/" + slug;
        game.free = true;
        game.price = "Gratis (befristet)";
        game.tags = {"free", "pc", "giveaway"};
        game.runtime = "windows";
        if (game.description.empty() || game.description == game.title)
            game.description = "Aktuell kostenlos im Epic Games Store. Das Einlösen erfolgt mit deinem "
                               "Epic-Konto; das Spiel gehört danach dauerhaft dir.";
        game.access = Access::External;
        if (!game.title.empty())
            out.push_back(std::move(game));
    });
    return true;
}

} // namespace parse

namespace
{

template <typename Parse> bool fetch_and_parse(ProviderContext &context, const char *url, Parse &&parse_body,
                                               std::string &error)
{
    const net::Response response = context.http->get(url, 16 * 1024 * 1024);
    if (!response.ok())
    {
        error = response.error.empty() ? std::string(host_of(url)) + " answered HTTP " + std::to_string(response.status)
                                       : response.error;
        return false;
    }
    return parse_body(response.body);
}

class Itch final : public Provider
{
  public:
    const char *id() const override { return "itch"; }
    const char *label() const override { return "itch.io"; }
    const char *about() const override
    {
        return "Kostenlose Indie-Spiele aus den öffentlichen itch.io-Feeds. Browserspiele starten in "
               "der Web-Runtime; Downloads für PC laufen über die itch.io-Seite.";
    }
    bool refresh(ProviderContext &context, std::vector<Game> &out, std::string &error) override
    {
        bool any = false;
        for (const char *url : {"https://itch.io/games/free/platform-web.xml", "https://itch.io/games/free.xml"})
        {
            std::vector<Game> batch;
            if (fetch_and_parse(context, url, [&](const std::string &body) {
                    return parse::itch_feed(body, context.allow_adult, batch, error);
                }, error))
            {
                any = true;
                for (Game &game : batch)
                {
                    bool duplicate = false;
                    for (const Game &existing : out)
                        duplicate |= existing.id == game.id;
                    if (!duplicate)
                        out.push_back(std::move(game));
                }
            }
        }
        if (any)
            error.clear();
        return any;
    }
};

class GameJolt final : public Provider
{
  public:
    const char *id() const override { return "gamejolt"; }
    const char *label() const override { return "Game Jolt"; }
    const char *about() const override
    {
        return "Beliebte Indie-Spiele von Game Jolt. Browserspiele starten in der Web-Runtime; "
               "Inhalte für Erwachsene sind standardmäßig ausgeblendet.";
    }
    bool refresh(ProviderContext &context, std::vector<Game> &out, std::string &error) override
    {
        return fetch_and_parse(context, "https://gamejolt.com/site-api/web/discover/games?section=hot",
                               [&](const std::string &body) {
                                   return parse::gamejolt_discover(body, context.allow_adult, out, error);
                               }, error);
    }
};

class Gog final : public Provider
{
  public:
    const char *id() const override { return "gog"; }
    const char *label() const override { return "GOG"; }
    const char *about() const override
    {
        return "Kostenlose DRM-freie Spiele aus dem GOG-Katalog. Download über dein GOG-Konto; "
               "Windows-Spiele laufen danach in einer Windows-Runtime.";
    }
    bool refresh(ProviderContext &context, std::vector<Game> &out, std::string &error) override
    {
        return fetch_and_parse(context,
                               "https://catalog.gog.com/v1/catalog?limit=48&price=between:0,0&order=desc:trending"
                               "&productType=in:game&locale=de-DE",
                               [&](const std::string &body) { return parse::gog_catalog(body, out, error); }, error);
    }
};

class Epic final : public Provider
{
  public:
    const char *id() const override { return "epic"; }
    const char *label() const override { return "Epic Games"; }
    const char *about() const override
    {
        return "Die aktuellen Gratisspiele des Epic Games Store. Einlösen mit deinem Epic-Konto.";
    }
    bool refresh(ProviderContext &context, std::vector<Game> &out, std::string &error) override
    {
        return fetch_and_parse(
            context,
            "https://store-site-backend-static.ak.epicgames.com/freeGamesPromotions?locale=de&country=DE&allowCountries=DE",
            [&](const std::string &body) {
                return parse::epic_promotions(body, static_cast<std::int64_t>(std::time(nullptr)), out, error);
            },
            error);
    }
};

} // namespace

std::unique_ptr<Provider> make_itch_provider() { return std::make_unique<Itch>(); }
std::unique_ptr<Provider> make_gamejolt_provider() { return std::make_unique<GameJolt>(); }
std::unique_ptr<Provider> make_gog_provider() { return std::make_unique<Gog>(); }
std::unique_ptr<Provider> make_epic_provider() { return std::make_unique<Epic>(); }

} // namespace akeno
