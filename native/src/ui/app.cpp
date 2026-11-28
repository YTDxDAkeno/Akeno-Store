// Akeno Gaming Hub - the hub's screens and their input handling.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/app.hpp"

#include "core/fs.hpp"
#include "core/json.hpp"
#include "core/util.hpp"
#include "platform/platform.hpp"
#include "play/components.hpp"
#include "play/play.hpp"
#include "runtime/launch.hpp"

#include "qrcodegen.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <cmath>
#include <ctime>

namespace akeno::ui
{
namespace
{

namespace theme
{
const Color bg{7, 10, 26};
const Color panel{17, 21, 52, 255};
const Color panel_hi{30, 36, 84, 255};

const Color accent{139, 92, 246};
const Color accent2{88, 120, 255};
const Color glow{160, 110, 255};
const Color text{242, 244, 255};
const Color dim{172, 178, 222};
const Color muted{112, 120, 176};
const Color ok{52, 211, 153};
const Color warn{251, 191, 36};
const Color bad{248, 113, 113};
} // namespace theme

constexpr float kMargin = 72;
constexpr float kTop = 132;
constexpr float kHeroH = 470;
constexpr float kShelfTile = 232;
constexpr float kShelfGap = 28;
constexpr float kShelfH = 352;
constexpr int kStoreColumns = 5;
constexpr float kStoreTile = 250;
constexpr float kStoreGap = 47;
constexpr float kStoreRow = 336;
constexpr int kLibraryColumns = 7;
constexpr float kLibraryTile = 196;

enum ActionKind
{
    kInstall = 1,
    kStart,     // a native PS5 title
    kPlay,      // a game in Akeno's library, in Akeno's play mode
    kUninstall,
    kStorePage,
    kRate,
    kShortcut,
    kItchSetup,
};

std::string secrets_dir() { return fs::join(platform::data_dir(), "secrets"); }
std::string itch_key_path() { return fs::join(secrets_dir(), "itch-api-key"); }

float approach(float current, float target, float dt, float speed = 12)
{
    return current + (target - current) * (1 - std::exp(-speed * dt));
}

float ease_out_back(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    const float c1 = 1.70158f, c3 = c1 + 1;
    return 1 + c3 * std::pow(t - 1, 3) + c1 * std::pow(t - 1, 2);
}

const char *tab_label(int tab)
{
    static const char *labels[] = {"Entdecken", "Store", "Bibliothek", "Runtimes", "Downloads", "Diagnose", "Einstellungen"};
    return labels[tab];
}

Color provider_color(const std::string &provider)
{
    if (provider == "prosperostore")
        return {124, 92, 246};
    if (provider == "recompstore")
        return {236, 72, 153};
    if (provider == "itch")
        return {250, 92, 92};
    if (provider == "gamejolt")
        return {204, 255, 0};
    if (provider == "gog")
        return {170, 90, 220};
    if (provider == "epic")
        return {200, 200, 210};
    if (provider == "github")
        return {120, 140, 160};
    if (provider == "akeno")
        return {56, 189, 248};
    return theme::accent2;
}

Color compat_color(Compat compat)
{
    switch (compat)
    {
    case Compat::Verified:
        return theme::ok;
    case Compat::Limited:
        return {163, 230, 53};
    case Compat::Experimental:
    case Compat::Boots:
        return theme::warn;
    case Compat::Incompatible:
        return theme::bad;
    case Compat::Untested:
        break;
    }
    return theme::muted;
}

std::string runtime_name(const std::string &id)
{
    const RuntimeInfo *info = runtime_by_id(id);
    return info ? info->name : id;
}

} // namespace

// ---------------------------------------------------------------------------------------
// Settings

bool Settings::load(const std::string &path)
{
    const auto text = fs::read_file(path, 256 * 1024);
    if (!text)
        return false;
    const json::Document doc = json::Document::parse(*text);
    const json::Value root = doc.root();
    if (!root.is_object())
        return false;
    this->root = static_cast<int>(root["root"].integer(0));
    adult = root["adult"].boolean(false);
    reduce_motion = root["reduce_motion"].boolean(false);
    auto_update = root["auto_update"].boolean(false);
    disabled_providers.clear();
    root["disabled_providers"].each([&](json::Value v) { disabled_providers.push_back(v.str()); });
    github_repositories.clear();
    root["github_repositories"].each([&](json::Value v) { github_repositories.push_back(v.str()); });
    return true;
}

bool Settings::save(const std::string &path) const
{
    std::string text = "{\"schema\":1,\"root\":" + std::to_string(root) + ",\"adult\":" + (adult ? "true" : "false") +
                       ",\"reduce_motion\":" + (reduce_motion ? "true" : "false") +
                       ",\"auto_update\":" + (auto_update ? "true" : "false") + ",\"disabled_providers\":[";
    for (std::size_t i = 0; i < disabled_providers.size(); ++i)
        text += (i ? "," : "") + json::quote(disabled_providers[i]);
    text += "],\"github_repositories\":[";
    for (std::size_t i = 0; i < github_repositories.size(); ++i)
        text += (i ? "," : "") + json::quote(github_repositories[i]);
    text += "]}\n";
    return fs::write_atomic(path, text);
}

// ---------------------------------------------------------------------------------------

App::App(Gfx &gfx, std::string asset_dir, std::string returned_from)
    : gfx_(gfx), asset_dir_(std::move(asset_dir)), returned_(std::move(returned_from))
{
    settings_path_ = fs::join(platform::data_dir(), "settings.json");
    settings_.load(settings_path_);
    http_ = std::make_unique<net::Client>(net::make_platform_transport());

    ProviderContext context;
    context.http = http_.get();
    context.cache_dir = platform::cache_dir();
    context.state_dir = platform::data_dir();
    context.asset_dir = asset_dir_;
    context.allow_adult = settings_.adult;
    fs::make_dirs(context.cache_dir);
    std::vector<std::unique_ptr<Provider>> providers;
    providers.push_back(make_prosperostore_provider());
    providers.push_back(make_recompstore_provider());
    providers.push_back(make_akeno_provider());
    providers.push_back(make_itch_provider());
    providers.push_back(make_gamejolt_provider());
    providers.push_back(make_gog_provider());
    providers.push_back(make_epic_provider());
    if (!settings_.github_repositories.empty())
        providers.push_back(make_github_provider(settings_.github_repositories));
    if (const auto key = fs::read_file(itch_key_path(), 4096))
        itch_key_ = trim(*key);
    context.itch_key = itch_key_;
    catalog_ = std::make_unique<Catalog>(context, std::move(providers));
    for (const std::string &id : settings_.disabled_providers)
        catalog_->set_enabled(id, false);

    const std::string receipts = fs::join(platform::data_dir(), "receipts");
    library_ = std::make_unique<Library>(receipts);
    installer_ = std::make_unique<Installer>(*http_, *library_, receipts, [this](Game &game, std::string &error) {
        return catalog_->resolve(game, error);
    });
    images_ = std::make_unique<ImageCache>(gfx_, *http_, fs::join(platform::cache_dir(), "images"));
    compat_ = std::make_unique<CompatDb>(fs::join(platform::data_dir(), "compat.json"));
    roots_ = platform::install_roots();
    settings_.root = std::clamp(settings_.root, 0, static_cast<int>(roots_.size()) - 1);

    filters_ = {{"In Akeno spielbar", "playable"},
                {"Alles", "all"},
                {"RPG Maker", "rpgmaker"},
                {"Kostenlos", "free"},
                {"Online & Multiplayer", "online"},
                {"Singleplayer", "singleplayer"},
                {"Fanprojekte", "fan"},
                {"Indie", "indie"},
                {"PS5-Titel", "ps5"},
                {"Recomps & Ports", "recomp"},
                {"Emulatoren", "emulator"},
                {"HTML5 (Runtime fehlt)", "browser"},
                {"Windows (Runtime fehlt)", "windows"},
                {"Kostenlos für PC", "pc-free"},
                {"Visual Novels", "vn"},
                {"Getestet", "tested"},
                {"Demnächst", "upcoming"},
                {"Akeno-Katalog", "provider:akeno"},
                {"ProsperoStore", "provider:prosperostore"},
                {"RecompStore", "provider:recompstore"},
                {"itch.io", "provider:itch"},
                {"Game Jolt", "provider:gamejolt"},
                {"GOG", "provider:gog"},
                {"Epic Games", "provider:epic"}};
    catalog_->refresh();
    sync_data();
}

App::~App()
{
    if (resolve_thread_.joinable())
        resolve_thread_.join();
    if (probe_thread_.joinable())
        probe_thread_.join();
}

void App::merge_resolved()
{
    std::vector<Game> done;
    {
        std::lock_guard lock(resolve_mutex_);
        done.swap(resolved_);
    }
    for (const Game &r : done)
        for (Game &g : games_)
            if (g.id == r.id)
            {
                g.license = r.license.empty() ? g.license : r.license;
                g.release_notes = r.release_notes;
                g.artifact_url = r.artifact_url;
                g.sha256 = r.sha256;
                g.source_repo = r.source_repo;
                g.version = r.version.empty() ? g.version : r.version;
                g.resolved = r.resolved;
            }
}

int App::update_all()
{
    int queued = 0;
    for (const InstalledTitle &title : installed_)
    {
        const int index = game_index_by_title(title.title_id);
        if (index >= 0 && title.managed && update_available(games_[static_cast<std::size_t>(index)], title))
        {
            installer_->install(games_[static_cast<std::size_t>(index)], title.root);
            ++queued;
        }
    }
    return queued;
}

bool App::catalog_ready() const { return !catalog_->refreshing() && !games_.empty(); }

// ---------------------------------------------------------------------------------------
// Data

void App::sync_data()
{
    merge_resolved();
    if (settings_.auto_update && !auto_update_done_ && !catalog_->refreshing() && !games_.empty() &&
        library_generation_ != ~0ull)
    {
        auto_update_done_ = true;
        if (const int queued = update_all())
            toast("Automatische Updates", std::to_string(queued) + " Titel werden aktualisiert");
    }
    if (catalog_->generation() != catalog_generation_)
    {
        catalog_generation_ = catalog_->generation();
        const std::string keep = detail_ >= 0 && detail_ < static_cast<int>(games_.size()) ? games_[detail_].id : "";
        games_ = catalog_->games();
        providers_ = catalog_->status();
        detail_ = -1;
        if (!keep.empty())
            for (std::size_t i = 0; i < games_.size(); ++i)
                if (games_[i].id == keep)
                    detail_ = static_cast<int>(i);
        rebuild_shelves();
        rebuild_store();
    }
    if (installer_->generation() != library_generation_ || library_poll_ <= 0)
    {
        library_generation_ = installer_->generation();
        library_poll_ = 5;
        std::vector<std::string> paths;
        for (const auto &root : roots_)
            paths.push_back(root.path);
        installed_ = library_->scan(paths);
        profiles_ = list_profiles();
        local_ = scan_local_games(platform::import_dirs());
        rebuild_shelves();
    }
}

int App::game_index_by_id(const std::string &id) const
{
    if (id.empty())
        return -1;
    for (std::size_t i = 0; i < games_.size(); ++i)
        if (games_[i].id == id)
            return static_cast<int>(i);
    return -1;
}

int App::game_index_for(const InstalledTitle &title) const
{
    const int by_title = game_index_by_title(title.title_id);
    return by_title >= 0 ? by_title : game_index_by_id(title.source_game_id);
}

int App::game_index_by_title(const std::string &title_id) const
{
    if (title_id.empty())
        return -1;
    for (std::size_t i = 0; i < games_.size(); ++i)
        if (games_[i].title_id == title_id)
            return static_cast<int>(i);
    return -1;
}

const InstalledTitle *App::installed_for(const Game &game) const
{
    if (game.title_id.empty())
        return nullptr;
    for (const InstalledTitle &title : installed_)
        if (title.title_id == game.title_id)
            return &title;
    return nullptr;
}

const GameProfile *App::profile_for(const Game &game) const
{
    if (game.runtime != "akeno")
        return nullptr;
    for (const GameProfile &profile : profiles_)
        if (profile.id == game.profile || (!game.id.empty() && profile.source == game.id))
            return &profile;
    return nullptr;
}

Compat App::shown_compat(const Game &game, std::string *note) const
{
    if (const CompatReport *report = compat_->find(game.id))
        return report->status;
    // Only what Akeno can run itself counts; a game for a missing runtime is not compatible yet.
    if (game.access == Access::Runtime || (game.access == Access::External && game.runtime != "native"))
    {
        if (note && game.compat_note.empty())
            *note = "Akenos Runtime für " + runtime_label(game) + " ist noch nicht integriert.";
        else if (note)
            *note = game.compat_note;
        return game.compat == Compat::Untested ? Compat::Incompatible : game.compat;
    }
    if (note)
        *note = game.compat_note;
    return game.compat;
}

std::string App::runtime_label(const Game &game) const
{
    if (game.runtime == "native")
        return "Nativer PS5-Titel";
    if (game.runtime == "akeno")
    {
        if (const GameProfile *profile = profile_for(game))
            return "In Akeno: " + runtime_name(profile->runtime);
        return game.engine_hint == "rpgmaker2k" ? "In Akeno: RPG Maker 2000/2003" : "In Akeno";
    }
    if (game.runtime == "web")
        return "HTML5";
    if (game.runtime == "windows")
        return "Windows";
    if (game.runtime == "java")
        return "Java";
    if (game.runtime == "linux")
        return "Linux";
    return "PC";
}

bool App::update_available(const Game &game, const InstalledTitle &title) const
{
    bool comparable = false;
    return compare_content_version(game.content_version, title.content_version, &comparable) > 0 && comparable;
}

std::vector<int> App::filter_games(const std::string &key, const std::string &query) const
{
    std::vector<int> out;
    for (std::size_t i = 0; i < games_.size(); ++i)
    {
        const Game &g = games_[i];
        bool keep = false;
        if (key == "all")
            keep = true;
        else if (key == "playable")
            keep = g.access == Access::Install && g.kind == "game" && (g.runtime == "native" || g.runtime == "akeno");
        else if (key == "rpgmaker")
            keep = g.has_tag("rpgmaker") || g.engine_hint == "rpgmaker2k";
        else if (key == "ps5")
            keep = g.access == Access::Install;
        else if (key == "recomp")
            keep = g.has_tag("recomp") || g.has_tag("port");
        else if (key == "emulator")
            keep = g.has_tag("emulator");
        else if (key == "browser")
            keep = g.runtime == "web";
        else if (key == "pc-free")
            keep = g.has_tag("pc") && g.free;
        else if (key == "vn")
            keep = g.has_tag("visual-novel");
        else if (key == "upcoming")
            keep = g.access == Access::Upcoming;
        else if (key == "free")
            keep = g.free && g.access != Access::Upcoming && g.kind == "game";
        else if (key == "online")
            keep = g.has_tag("online") || g.has_tag("multiplayer") || g.has_tag("mmo");
        else if (key == "singleplayer")
            keep = g.has_tag("singleplayer");
        else if (key == "fan")
            keep = g.has_tag("fan");
        else if (key == "indie")
            keep = g.has_tag("indie");
        else if (key == "windows")
            keep = g.runtime == "windows";
        else if (key == "tested")
            keep = compat_->find(g.id) != nullptr || g.compat != Compat::Untested;
        else if (starts_with(key, "provider:"))
            keep = g.provider == key.substr(9);
        if (keep && !query.empty())
            keep = contains_folded(g.title, query) || contains_folded(g.developer, query) ||
                   contains_folded(g.title_id, query);
        if (keep)
            out.push_back(static_cast<int>(i));
    }
    return out;
}

void App::rebuild_store()
{
    store_items_ = filter_games(filters_[static_cast<std::size_t>(filter_)].key, query_);
    store_focus_ = std::clamp(store_focus_, 0, std::max(0, static_cast<int>(store_items_.size()) - 1));
}

void App::rebuild_shelves()
{
    const int keep_row = row_;
    std::vector<int> focus;
    for (const Shelf &shelf : shelves_)
        focus.push_back(shelf.focus);
    shelves_.clear();
    const auto take = [&](std::vector<int> items, std::size_t limit) {
        if (items.size() > limit)
            items.resize(limit);
        return items;
    };
    // Updates for what is installed, then the stores' shelves.
    std::vector<int> updates;
    for (std::size_t i = 0; i < games_.size(); ++i)
        if (const InstalledTitle *title = installed_for(games_[i]); title && update_available(games_[i], *title))
            updates.push_back(static_cast<int>(i));
    std::vector<int> newest;
    for (std::size_t i = 0; i < games_.size(); ++i)
        if (games_[i].provider == "prosperostore" && games_[i].access == Access::Install)
            newest.push_back(static_cast<int>(i));
    std::sort(newest.begin(), newest.end(), [&](int a, int b) { return games_[a].updated > games_[b].updated; });
    std::vector<int> games_only;
    for (int i : newest)
        if (games_[i].kind == "game")
            games_only.push_back(i);
    const auto add = [&](std::string title, std::string subtitle, std::vector<int> items) {
        if (!items.empty())
            shelves_.push_back({std::move(title), std::move(subtitle), std::move(items)});
    };
    add("Updates verfügbar", "Neuere Versionen deiner installierten Titel", updates);
    std::vector<int> mine;
    for (std::size_t i = 0; i < games_.size(); ++i)
        if (profile_for(games_[i]))
            mine.push_back(static_cast<int>(i));
    add("In deiner Akeno-Bibliothek", "Startet direkt in Akeno – ohne andere Apps", mine);
    add("RPG Maker 2000/2003 für Akeno", "Kostenlos von itch.io; Akeno prüft nach dem Download die Engine und spielt sie selbst",
        take(filter_games("rpgmaker", ""), 24));
    add("Online & Multiplayer", "Native PS5-Ports mit offiziellen oder Community-Servern",
        take(filter_games("online", ""), 24));
    add("Fanprojekte", "Von Fans gemachte Spiele und Clients", take(filter_games("fan", ""), 24));
    add("Neu & aktualisiert", "Native PS5-Titel aus dem signierten ProsperoStore-Katalog", take(newest, 24));
    add("Spiele für die PS5", "Ports, Homebrew und Fanprojekte zum direkten Installieren", take(games_only, 24));
    add("Recomps & Ports", "Mit eigenem, rechtmäßig erworbenem Original", take(filter_games("recomp", ""), 24));
    add("Emulatoren & Runtimes", "Gemeinsame Laufzeiten für ganze Plattformen", take(filter_games("emulator", ""), 24));
    add("HTML5-Spiele", "Warten auf Akenos HTML5-Runtime – noch nicht spielbar", take(filter_games("browser", ""), 24));
    add("Visual Novels", "Erzählspiele von itch.io", take(filter_games("vn", ""), 24));
    add("Kostenlos für PC", "GOG und Epic Games – warten auf Akenos Windows-Runtime", take(filter_games("pc-free", ""), 24));
    for (std::size_t i = 0; i < shelves_.size() && i < focus.size(); ++i)
        shelves_[i].focus = std::min(focus[i], static_cast<int>(shelves_[i].items.size()) - 1);
    row_ = std::clamp(keep_row, 0, static_cast<int>(shelves_.size()));

    // Featured: signed games with artwork first.
    featured_.clear();
    for (int i : games_only)
        if (featured_.size() < 6 && !games_[i].icon_url.empty())
            featured_.push_back(i);
    for (const Game &g : games_)
        if (featured_.size() < 8 && g.provider == "recompstore" && !g.hero_url.empty() && g.access == Access::Install)
            featured_.push_back(static_cast<int>(&g - games_.data()));
    hero_ = featured_.empty() ? 0 : hero_ % static_cast<int>(featured_.size());
}

std::string App::artwork(const Game &game) const
{
    if (const InstalledTitle *title = installed_for(game); title && !title->icon_path.empty() && game.icon_url.empty())
        return title->icon_path;
    return !game.icon_url.empty() ? game.icon_url : game.cover_url;
}

// ---------------------------------------------------------------------------------------
// Actions

std::vector<App::Action> App::actions_for(const Game &game) const
{
    std::vector<Action> actions;
    if (game.access == Access::Install && game.runtime == "akeno")
    {
        // A game Akeno plays itself with an integrated runtime.
        if (profile_for(game))
        {
            actions.push_back({"Spielen", kPlay});
            actions.push_back({"Hauptmenü-Kachel", kShortcut});
            actions.push_back({"Neu installieren", kInstall});
            actions.push_back({"Deinstallieren", kUninstall});
        }
        else if (game.provider == "itch" && itch_key_.empty())
            actions.push_back({"itch.io-Konto verbinden", kItchSetup});
        else
            actions.push_back({"Installieren", kInstall});
    }
    else if (game.access == Access::Install)
    {
        const InstalledTitle *installed = installed_for(game);
        if (installed)
        {
            actions.push_back({"Starten", kStart});
            actions.push_back({update_available(game, *installed) ? "Aktualisieren" : "Neu installieren", kInstall});
            actions.push_back({"Deinstallieren", kUninstall});
        }
        else
            actions.push_back({"Installieren", kInstall});
    }
    if (game.kind == "game" && (installed_for(game) || profile_for(game) || compat_->find(game.id)))
        actions.push_back({compat_->find(game.id) ? "Testbericht ändern" : "Kompatibilität bewerten", kRate});
    if (!game.page_url.empty())
        actions.push_back({"Projektseite (QR)", kStorePage});
    return actions;
}

void App::install(const Game &game)
{
    if (game.runtime == "akeno")
    {
        if (!fs::writable(platform::data_dir()))
        {
            toast("Kein Schreibzugriff", "Akenos Datenordner ist nicht beschreibbar (ShadowMountPlus 1.7+ nötig)", true);
            return;
        }
        installer_->install(game, library_root());
        toast("Zur Warteschlange hinzugefügt", game.title + " – Akeno erkennt die Engine nach dem Download");
        return;
    }
    if (roots_.empty() || !roots_[static_cast<std::size_t>(settings_.root)].writable)
    {
        toast("Kein Schreibzugriff",
              "Der Installationsort ist nicht beschreibbar. Auf der PS5: ShadowMountPlus 1.7+ einbinden.", true);
        return;
    }
    installer_->install(game, roots_[static_cast<std::size_t>(settings_.root)].path);
    toast("Zur Warteschlange hinzugefügt", game.title);
}

void App::launch(const std::string &title_id, const std::vector<std::string> &arguments, const std::string &name,
                 const Game *game, const std::string &runtime)
{
    std::string error;
    if (!request_launch(title_id, arguments, error))
    {
        toast("Start nicht möglich", error, true);
        return;
    }
    // A game started from Akeno is asked about the next time Akeno opens (compatibility report).
    if (game && game->kind == "game")
        compat_->set_pending({game->id, game->title, runtime, platform::unix_time()});
    toast("Wird gestartet", name + " – Akeno schließt sich");
    quit_ = true; // the payload starts the title once Akeno has closed
}

bool App::ensure_components(const std::string &runtime, bool quiet)
{
    const std::vector<std::string> missing = missing_components(runtime);
    for (const std::string &id : missing)
    {
        const ComponentSpec *spec = component_spec(id);
        const int index = spec ? game_index_by_id(spec->source) : -1;
        if (index < 0)
        {
            toast("Runtime nicht verfügbar",
                  std::string(spec ? spec->name : id.c_str()) + ": Quelle nicht im Katalog (Kataloge laden?)", true);
            continue;
        }
        installer_->install_component(id, games_[static_cast<std::size_t>(index)]);
        if (!quiet)
            toast("Runtime wird einmalig installiert", std::string(spec->name) + " – danach für alle Spiele bereit");
    }
    return missing.empty();
}

void App::play(const GameProfile &profile)
{
    if (!play::runtime_built(profile.runtime))
    {
        toast("Nicht spielbar", "Die Akeno-Runtime " + runtime_name(profile.runtime) + " fehlt in diesem Build", true);
        return;
    }
    if (!ensure_components(profile.runtime))
        return; // the runtime's files are being installed; Play works once they are
    // Akeno restarts its own title in play mode; the game runs inside Akeno, full screen.
    compat_->set_pending({profile.source.empty() ? "library:" + profile.id : profile.source, profile.title,
                          runtime_name(profile.runtime), platform::unix_time()});
    toast("Wird gestartet", profile.title + " – " + runtime_name(profile.runtime));
    play_request_ = profile.id;
    quit_ = true;
}

void App::profile_menu(const GameProfile &profile)
{
    const GameProfile copy = profile;
    std::vector<MenuItem> items;
    items.push_back({"Spielen", [this, copy] { play(copy); }});
    items.push_back({"Hauptmenü-Kachel anlegen", [this, copy] { add_shortcut(copy); }});
    items.push_back({"Kompatibilität bewerten", [this, copy] {
                         rate_menu(copy.source.empty() ? "library:" + copy.id : copy.source);
                     }});
    items.push_back({copy.owned_files ? "Deinstallieren" : "Aus der Bibliothek entfernen", [this, copy] {
                         installer_->uninstall_library(copy.id, copy.title);
                         toast("Wird entfernt", copy.title + " – Spielstände bleiben erhalten");
                     }});
    items.push_back({"Abbrechen", [] {}});
    const RuntimeInfo *runtime = runtime_by_id(copy.runtime);
    open_menu(copy.title,
              std::string(runtime ? runtime->name : copy.runtime.c_str()) + " · in Akeno\nDateien: " + copy.folder +
                  "\nSpielstände: " + copy.saves +
                  (copy.owned_files ? "\nVon Akeno heruntergeladen" : "\nEigene Dateien (werden nicht gelöscht)"),
              std::move(items));
}

void App::add_shortcut(const GameProfile &profile)
{
    // A home-screen tile that starts Akeno straight into this game (--play=<id>).
    ShortcutSpec spec;
    spec.name = profile.title;
    spec.target = kAkenoTitleId;
    spec.arguments = {"--play=" + profile.id};
    const std::string cover = images_->cached_file(profile.cover);
    if (!cover.empty())
        if (const auto data = fs::read_file(cover, 16 * 1024 * 1024))
            decode_image(*data, 1024, 1024, spec.icon);
    if (spec.icon.width == 0)
        if (const auto data = fs::read_file(platform::is_console() ? "/app0/sce_sys/icon0.png"
                                                                    : fs::join(asset_dir_, "../../ps5/sce_sys/icon0.png"),
                                            8 * 1024 * 1024))
            decode_image(*data, 512, 512, spec.icon);
    if (spec.icon.width == 0)
    {
        spec.icon.width = spec.icon.height = 2;
        spec.icon.rgba.assign(4, 0xFFF65C8Bu);
    }
    if (roots_.empty())
    {
        toast("Kachel nicht möglich", "Kein Installationsort für die Kachel", true);
        return;
    }
    std::string error;
    const std::string tile = create_shortcut(spec, roots_[static_cast<std::size_t>(settings_.root)].path,
                                             fs::join(platform::data_dir(), "receipts"), error);
    if (tile.empty())
        toast("Kachel fehlgeschlagen", error, true);
    else
        toast("Kachel angelegt", profile.title + " erscheint als " + tile + " im Hauptmenü und startet direkt");
    library_poll_ = 0;
}

void App::itch_menu()
{
    std::vector<MenuItem> items;
    const std::string file = fs::join(platform::data_dir(), "itch-api-key.txt");
    items.push_back({"Schlüssel aus Datei lesen", [this, file] {
                         const auto text = fs::read_file(file, 4096);
                         std::string error;
                         if (!text)
                             toast("Keine Datei", "Lege den Schlüssel als " + file + " ab (z. B. per FTP)", true);
                         else if (set_itch_key(trim(*text), error))
                         {
                             fs::remove_tree(file);
                             toast("itch.io verbunden", "Der Schlüssel liegt jetzt nur in " + itch_key_path());
                         }
                         else
                             toast("Schlüssel abgelehnt", error, true);
                     }});
    items.push_back({"Schlüssel eintippen", [this] {
                         key_entry_ = true;
                         query_.clear();
                         keyboard_ = true;
                     }});
    items.push_back({"Schlüssel erstellen (QR zu itch.io)", [this] {
                         open_qr("itch.io-API-Schlüssel", "https://itch.io/user/settings/api-keys");
                     }});
    if (!itch_key_.empty())
        items.push_back({"Verbindung trennen", [this] {
                             fs::remove_tree(itch_key_path());
                             itch_key_.clear();
                             catalog_->set_itch_key("");
                             toast("itch.io getrennt", "Der Schlüssel wurde von der Konsole gelöscht");
                         }});
    items.push_back({"Abbrechen", [] {}});
    open_menu("itch.io-Konto",
              "Akeno lädt itch.io-Spiele über die offizielle itch.io-API mit deinem eigenen API-Schlüssel. "
              "Er bleibt auf der Konsole (" + itch_key_path() + ") und geht nur an api.itch.io." +
                  std::string(itch_key_.empty() ? "\nStatus: nicht verbunden" : "\nStatus: verbunden"),
              std::move(items));
}

bool App::set_itch_key(const std::string &key, std::string &error)
{
    if (key.size() < 20 || key.size() > 128 || key.find_first_of(" \t\r\n") != std::string::npos)
    {
        error = "Das sieht nicht wie ein itch.io-API-Schlüssel aus";
        return false;
    }
    fs::make_dirs(secrets_dir(), 0700);
    if (!fs::write_atomic(itch_key_path(), key + "\n"))
    {
        error = itch_key_path() + " ist nicht beschreibbar";
        return false;
    }
    itch_key_ = key;
    catalog_->set_itch_key(key);
    return true;
}

void App::run_action(const Game &game, int kind)
{
    switch (kind)
    {
    case kInstall:
        install(game);
        break;
    case kStart:
        launch(game.title_id, {}, game.title, &game, "PS5-Titel");
        break;
    case kPlay:
        if (const GameProfile *profile = profile_for(game))
            play(*profile);
        break;
    case kShortcut:
        if (const GameProfile *profile = profile_for(game))
            add_shortcut(*profile);
        break;
    case kItchSetup:
        itch_menu();
        break;
    case kUninstall:
        if (const GameProfile *profile = profile_for(game))
        {
            const GameProfile copy = *profile;
            open_menu("Deinstallieren?", copy.title + " wird aus Akenos Bibliothek entfernt. Spielstände bleiben.",
                      {{"Deinstallieren", [this, copy] {
                            installer_->uninstall_library(copy.id, copy.title);
                            toast("Wird deinstalliert", copy.title);
                        }},
                       {"Abbrechen", [] {}}});
        }
        else if (const InstalledTitle *title = installed_for(game))
        {
            const InstalledTitle copy = *title;
            open_menu("Deinstallieren?", copy.name + " wird mit seinem Ordner entfernt. Spielstände in /download0 bleiben.",
                      {{"Deinstallieren", [this, copy] {
                            installer_->uninstall(copy);
                            toast("Wird deinstalliert", copy.name);
                        }},
                       {"Abbrechen", [] {}}});
        }
        break;
    case kStorePage:
        open_qr(game.title, game.page_url);
        break;
    case kRate:
        rate_menu(game.id);
        break;
    }
}

// ---------------------------------------------------------------------------------------
// Compatibility reports

std::string App::compat_line(const Game &game) const
{
    if (const CompatReport *report = compat_->find(game.id))
        return std::string("Dein Test: ") + compat_label(report->status);
    return compat_label(game.compat);
}

void App::rate_menu(const std::string &game_id, int focus)
{
    const int index = game_index_by_id(game_id);
    CompatReport report;
    if (const CompatReport *existing = compat_->find(game_id))
        report = *existing;
    else
    {
        report.game_id = game_id;
        if (index >= 0)
        {
            const Game &g = games_[static_cast<std::size_t>(index)];
            report.title = g.title;
            report.provider = g.provider;
        }
    }
    if (index >= 0)
    {
        const Game &g = games_[static_cast<std::size_t>(index)];
        const InstalledTitle *installed = installed_for(g);
        report.game_version = installed && !installed->content_version.empty() ? installed->content_version : g.version;
        if (g.runtime == "native")
            report.runtime = "PS5-Titel";
        else if (const GameProfile *profile = profile_for(g))
        {
            report.runtime = runtime_name(profile->runtime);
            report.runtime_version = "Akeno " AKENO_VERSION;
        }
    }
    report.firmware = platform::firmware_version();
    const std::string title = report.title.empty() ? game_id : report.title;
    std::vector<MenuItem> items;
    for (Compat status : {Compat::Verified, Compat::Limited, Compat::Boots, Compat::Experimental, Compat::Incompatible})
        items.push_back({std::string(report.status == status ? "● " : "○ ") + compat_label(status),
                         [this, report, status, game_id] {
                             CompatReport r = report;
                             r.status = status;
                             r.tested = platform::unix_time();
                             if (status == Compat::Verified || status == Compat::Limited || status == Compat::Boots)
                                 r.start = r.start == Aspect::Unknown ? Aspect::Works : r.start;
                             if (status == Compat::Incompatible && r.start == Aspect::Unknown)
                                 r.start = Aspect::Fails;
                             if (status == Compat::Verified && r.gameplay == Aspect::Unknown)
                                 r.gameplay = Aspect::Works;
                             compat_->put(r);
                             rebuild_store();
                             rate_menu(game_id, 6);
                         }});
    const auto aspect = [&](const char *label, Aspect CompatReport::*field) {
        items.push_back({std::string(label) + ": " + aspect_label(report.*field), [this, report, field, game_id,
                                                                                     at = static_cast<int>(items.size())] {
                             CompatReport r = report;
                             r.*field = next_aspect(r.*field);
                             r.tested = platform::unix_time();
                             compat_->put(r);
                             rate_menu(game_id, at);
                         }});
    };
    aspect("Start", &CompatReport::start);
    aspect("Gameplay", &CompatReport::gameplay);
    aspect("Grafik", &CompatReport::graphics);
    aspect("Ton", &CompatReport::audio);
    aspect("Controller", &CompatReport::controller);
    aspect("Netzwerk / Online", &CompatReport::network);
    if (compat_->find(game_id))
        items.push_back({"Testbericht löschen", [this, game_id, title] {
                             compat_->remove(game_id);
                             rebuild_store();
                             toast("Testbericht gelöscht", title);
                         }});
    items.push_back({"Fertig", [] {}});
    open_menu("Kompatibilität: " + title,
              "Was hast du auf dieser PS5 gesehen? Der Bericht bleibt lokal (/data/akeno/compat.json) und lässt sich "
              "unter Diagnose exportieren." +
                  (report.firmware.empty() ? std::string() : "\nFirmware " + report.firmware) +
                  (report.runtime.empty() ? std::string() : " · " + report.runtime + " " + report.runtime_version),
              std::move(items));
    menu_focus_ = std::clamp(focus, 0, static_cast<int>(menu_.size()) - 1);
}

void App::ask_pending_report()
{
    const auto pending = compat_->pending();
    if (!pending)
        return;
    compat_->clear_pending();
    // Only ask about a start from the last three days.
    if (platform::unix_time() - pending->started > 3 * 24 * 3600)
        return;
    const PendingLaunch p = *pending;
    open_menu("Wie lief " + p.title + "?",
              "Du hast es zuletzt aus Akeno gestartet" + (p.runtime.empty() ? std::string() : " (" + p.runtime + ")") +
                  ". Ein kurzer Bericht hilft dir und anderen, den Status ehrlich zu halten. Nichts wird gesendet.",
              {{"Jetzt bewerten", [this, id = p.game_id] { rate_menu(id); }}, {"Nicht jetzt", [] {}}});
}

void App::toast(const std::string &title, const std::string &text, bool error)
{
    toasts_.push_back({title, text, 0, error});
    if (toasts_.size() > 4)
        toasts_.pop_front();
}

void App::open_detail(int index)
{
    if (index < 0 || index >= static_cast<int>(games_.size()))
        return;
    detail_ = index;
    detail_anim_ = 0;
    detail_button_ = 0;
    detail_scroll_ = 0;
    const Game &game = games_[static_cast<std::size_t>(index)];
    // Signed-catalog entries: fetch the verified app record (license, notes, SHA-256) now.
    // Other stores resolve at install time (GitHub's API is rate limited).
    if (game.provider == "prosperostore" && game.access == Access::Install && !game.resolved && resolving_ != game.id)
    {
        if (resolve_thread_.joinable())
            resolve_thread_.join();
        resolving_ = game.id;
        resolve_thread_ = std::thread([this, copy = game]() mutable {
            std::string error;
            if (catalog_->resolve(copy, error))
            {
                std::lock_guard lock(resolve_mutex_);
                resolved_.push_back(std::move(copy));
            }
        });
    }
}

void App::open_qr(const std::string &title, const std::string &url)
{
    std::vector<std::uint8_t> qr(qrcodegen_BUFFER_LEN_MAX), temp(qrcodegen_BUFFER_LEN_MAX);
    if (!qrcodegen_encodeText(url.c_str(), temp.data(), qr.data(), qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                              qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true))
    {
        toast("QR-Code", "Die Adresse ist zu lang", true);
        return;
    }
    const int size = qrcodegen_getSize(qr.data());
    const int scale = std::max(1, 440 / (size + 8));
    Pixels pixels;
    pixels.width = pixels.height = (size + 8) * scale;
    pixels.rgba.assign(static_cast<std::size_t>(pixels.width) * pixels.height, 0xFFFFFFFFu);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            if (qrcodegen_getModule(qr.data(), x, y))
                for (int dy = 0; dy < scale; ++dy)
                    for (int dx = 0; dx < scale; ++dx)
                        pixels.rgba[static_cast<std::size_t>((y + 4) * scale + dy) * pixels.width + (x + 4) * scale + dx] =
                            0xFF1A0E0Bu;
    qr_texture_ = gfx_.upload(pixels);
    qr_title_ = title;
    qr_url_ = url;
}

void App::open_menu(const std::string &title, const std::string &text, std::vector<MenuItem> items)
{
    menu_title_ = title;
    menu_text_ = text;
    menu_ = std::move(items);
    menu_focus_ = 0;
}

void App::title_menu(const InstalledTitle &title)
{
    std::vector<MenuItem> items;
    const InstalledTitle copy = title;
    items.push_back({"Starten", [this, copy] {
                         const int index = game_index_for(copy);
                         launch(copy.title_id, {}, copy.name,
                                index >= 0 ? &games_[static_cast<std::size_t>(index)] : nullptr, "PS5-Titel");
                     }});
    const int index = game_index_for(title);
    if (index >= 0)
    {
        const Game game = games_[static_cast<std::size_t>(index)];
        if (update_available(game, title))
            items.push_back({"Aktualisieren auf " + game.content_version, [this, game] { install(game); }});
        items.push_back({"Details im Store", [this, index] { open_detail(index); }});
        if (game.kind == "game")
            items.push_back({"Kompatibilität bewerten", [this, id = game.id] { rate_menu(id); }});
    }
    if (title.shortcut || title.managed || index >= 0)
        items.push_back({"Deinstallieren", [this, copy] {
                             installer_->uninstall(copy);
                             toast("Wird deinstalliert", copy.name);
                         }});
    items.push_back({"Abbrechen", [] {}});
    const std::string text = title.title_id + " · Version " +
                             (title.content_version.empty() ? "?" : title.content_version) + "\n" + title.folder +
                             (title.managed ? "\nVon Akeno installiert" : "\nManuell installiert");
    open_menu(title.name, text, std::move(items));
}

void App::local_menu(const LocalGame &game)
{
    std::vector<MenuItem> items;
    const Detection &d = game.detection;
    std::string text = d.format + (d.engine_name.empty() || d.engine_name == d.format ? "" : " · " + d.engine_name) +
                       "\nErkannt an: " + d.evidence;
    if (d.playable)
    {
        // Own files are played where they are; Akeno only writes a start profile.
        GameProfile existing;
        for (const GameProfile &p : profiles_)
            if (p.folder == (d.root.empty() ? game.path : d.root))
                existing = p;
        const LocalGame copy = game;
        text += "\nLäuft in Akeno mit " + runtime_name(d.runtime) + ".";
        const auto make = [this, copy]() -> std::optional<GameProfile> {
            for (const GameProfile &p : profiles_)
                if (p.folder == (copy.detection.root.empty() ? copy.path : copy.detection.root))
                    return p;
            GameProfile profile;
            profile.id = make_library_id(copy.name);
            profile.title = copy.name;
            profile.engine = copy.detection.engine;
            profile.runtime = copy.detection.runtime;
            profile.folder = copy.detection.root.empty() ? copy.path : copy.detection.root;
            profile.saves = fs::join(library_dir(profile.id), "saves");
            profile.source = "import";
            profile.installed = platform::unix_time();
            if (!write_profile(profile))
            {
                toast("Profil nicht angelegt", library_root() + " ist nicht beschreibbar", true);
                return std::nullopt;
            }
            library_poll_ = 0;
            return profile;
        };
        items.push_back({"In Akeno spielen", [this, make] {
                             if (auto profile = make())
                                 play(*profile);
                         }});
        items.push_back({"Hauptmenü-Kachel anlegen", [this, make] {
                             if (auto profile = make())
                                 add_shortcut(*profile);
                         }});
    }
    else if (!d.platform.empty())
    {
        // An own dump: offer the native port or recompilation that takes this original.
        if (!d.game_title.empty() || !d.game_id.empty())
            text += "\nIm Header: " + d.game_title + (d.game_id.empty() ? "" : " (" + d.game_id + ")");
        std::vector<std::size_t> candidates;
        std::vector<std::string> texts;
        for (std::size_t i = 0; i < games_.size(); ++i)
        {
            const Game &g = games_[i];
            if (g.access != Access::Install || g.runtime != "native")
                continue;
            candidates.push_back(i);
            texts.push_back(lower(g.title + " " + g.needs + " " + g.description));
        }
        const auto ports = match_ports(d, texts);
        for (std::size_t k = 0; k < ports.size() && k < 3; ++k)
        {
            const int index = static_cast<int>(candidates[ports[k].index]);
            const Game &port = games_[static_cast<std::size_t>(index)];
            text += "\nPassender PS5-Port: " + port.title + (port.needs.empty() ? "" : " – benötigt: " + port.needs);
            items.push_back({(installed_for(port) ? "Port öffnen: " : "Port ansehen: ") + port.title,
                             [this, index] { open_detail(index); }});
        }
        if (ports.empty())
            text += "\nKein passender Port im Katalog. " + d.advice;
        else
            text += "\nDer Port liest dein Original selbst ein; wohin es gehört, steht in seiner Beschreibung.";
    }
    else
        text += "\n" + (d.advice.empty() ? std::string("Für dieses Format ist keine Akeno-Runtime vorgesehen.") : d.advice);
    items.push_back({"Schließen", [] {}});
    open_menu(game.name, text, std::move(items));
}

// ---------------------------------------------------------------------------------------
// Input

void App::press(Button button)
{
    focus_pop_ = 0;
    if (!menu_.empty())
        return press_menu(button);
    if (!qr_url_.empty())
    {
        if (button == Button::Circle || button == Button::Cross)
        {
            qr_url_.clear();
            qr_texture_ = Texture();
        }
        return;
    }
    if (keyboard_)
        return press_keyboard(button);
    if (detail_ >= 0)
        return press_detail(button);
    if (button == Button::L1 || button == Button::R1)
    {
        const int count = static_cast<int>(Tab::Count);
        tab_ = static_cast<Tab>((static_cast<int>(tab_) + (button == Button::R1 ? 1 : count - 1)) % count);
        fade_ = 0;
        return;
    }
    if (button == Button::Options)
    {
        tab_ = Tab::Settings;
        fade_ = 0;
        return;
    }
    if (in_tabs_)
        return press_tabs(button);
    switch (tab_)
    {
    case Tab::Discover:
        return press_discover(button);
    case Tab::Store:
        return press_store(button);
    case Tab::Library:
        return press_library(button);
    case Tab::Runtimes:
        return press_runtimes(button);
    case Tab::Downloads:
        return press_downloads(button);
    case Tab::Diagnose:
        return press_diagnose(button);
    case Tab::Settings:
        return press_settings(button);
    case Tab::Count:
        break;
    }
}

void App::press_tabs(Button button)
{
    const int count = static_cast<int>(Tab::Count);
    if (button == Button::Left || button == Button::Right)
    {
        tab_ = static_cast<Tab>((static_cast<int>(tab_) + (button == Button::Right ? 1 : count - 1)) % count);
        fade_ = 0;
    }
    else if (button == Button::Down || button == Button::Cross)
        in_tabs_ = false;
    else if (button == Button::Circle)
        quit_ = true;
}

void App::press_discover(Button button)
{
    const int rows = static_cast<int>(shelves_.size()) + 1;
    switch (button)
    {
    case Button::Up:
        if (row_ == 0)
            in_tabs_ = true;
        else
            --row_;
        break;
    case Button::Down:
        row_ = std::min(row_ + 1, rows - 1);
        break;
    case Button::Left:
    case Button::Right:
    {
        const int delta = button == Button::Right ? 1 : -1;
        if (row_ == 0)
        {
            if (hero_button_ + delta >= 0 && hero_button_ + delta <= 1)
                hero_button_ += delta;
            else if (!featured_.empty())
            {
                hero_previous_ = featured_[static_cast<std::size_t>(hero_)];
                hero_ = (hero_ + delta + static_cast<int>(featured_.size())) % static_cast<int>(featured_.size());
                hero_fade_ = 0;
                hero_timer_ = 0;
            }
        }
        else
        {
            Shelf &shelf = shelves_[static_cast<std::size_t>(row_ - 1)];
            shelf.focus = std::clamp(shelf.focus + delta, 0, static_cast<int>(shelf.items.size()) - 1);
        }
        break;
    }
    case Button::Cross:
        if (row_ == 0 && !featured_.empty())
        {
            const Game &game = games_[static_cast<std::size_t>(featured_[static_cast<std::size_t>(hero_)])];
            const auto actions = actions_for(game);
            if (hero_button_ == 0 && !actions.empty())
                run_action(game, actions[0].kind);
            else
                open_detail(featured_[static_cast<std::size_t>(hero_)]);
        }
        else if (row_ > 0)
        {
            const Shelf &shelf = shelves_[static_cast<std::size_t>(row_ - 1)];
            open_detail(shelf.items[static_cast<std::size_t>(shelf.focus)]);
        }
        break;
    case Button::Triangle:
        tab_ = Tab::Store;
        keyboard_ = true;
        break;
    case Button::Circle:
        if (row_ > 0)
            row_ = 0;
        else
            in_tabs_ = true;
        break;
    default:
        break;
    }
}

void App::press_store(Button button)
{
    const int count = static_cast<int>(store_items_.size());
    if (filter_focus_)
    {
        if (button == Button::Up)
        {
            if (filter_ == 0)
                in_tabs_ = true;
            else
                --filter_;
            rebuild_store();
            store_focus_ = 0;
        }
        else if (button == Button::Down && filter_ + 1 < static_cast<int>(filters_.size()))
        {
            ++filter_;
            rebuild_store();
            store_focus_ = 0;
        }
        else if ((button == Button::Right || button == Button::Cross) && count > 0)
            filter_focus_ = false;
        else if (button == Button::Triangle)
            keyboard_ = true;
        else if (button == Button::Circle)
            in_tabs_ = true;
        return;
    }
    switch (button)
    {
    case Button::Left:
        if (store_focus_ % kStoreColumns == 0)
            filter_focus_ = true;
        else
            --store_focus_;
        break;
    case Button::Right:
        if (store_focus_ + 1 < count && (store_focus_ + 1) % kStoreColumns != 0)
            ++store_focus_;
        break;
    case Button::Up:
        if (store_focus_ >= kStoreColumns)
            store_focus_ -= kStoreColumns;
        else
            in_tabs_ = true;
        break;
    case Button::Down:
        if (store_focus_ + kStoreColumns < count)
            store_focus_ += kStoreColumns;
        else if (store_focus_ / kStoreColumns < (count - 1) / kStoreColumns)
            store_focus_ = count - 1;
        break;
    case Button::Cross:
        if (count)
            open_detail(store_items_[static_cast<std::size_t>(store_focus_)]);
        break;
    case Button::Triangle:
        keyboard_ = true;
        break;
    case Button::Circle:
        if (!query_.empty())
        {
            query_.clear();
            rebuild_store();
        }
        else
            filter_focus_ = true;
        break;
    default:
        break;
    }
}

void App::press_library(Button button)
{
    const int games = static_cast<int>(profiles_.size());
    const int titles = games + static_cast<int>(installed_.size()); // grid: library games, then PS5 titles
    const int count = titles + static_cast<int>(local_.size());
    int &f = library_focus_;
    switch (button)
    {
    case Button::Left:
        if (f < titles && f % kLibraryColumns > 0)
            --f;
        break;
    case Button::Right:
        if (f < titles && f + 1 < titles && (f + 1) % kLibraryColumns != 0)
            ++f;
        break;
    case Button::Up:
        if (f >= titles)
            f = f == titles ? std::max(0, titles - 1) : f - 1;
        else if (f >= kLibraryColumns)
            f -= kLibraryColumns;
        else
            in_tabs_ = true;
        break;
    case Button::Down:
        if (f < titles)
            f = f + kLibraryColumns < titles ? f + kLibraryColumns : (count > titles ? titles : f);
        else if (f + 1 < count)
            ++f;
        break;
    case Button::Cross:
        if (f < games)
            play(profiles_[static_cast<std::size_t>(f)]);
        else if (f < titles)
            title_menu(installed_[static_cast<std::size_t>(f - games)]);
        else if (f < count)
            local_menu(local_[static_cast<std::size_t>(f - titles)]);
        break;
    case Button::Square:
        if (f < games)
            profile_menu(profiles_[static_cast<std::size_t>(f)]);
        else if (f < titles)
            title_menu(installed_[static_cast<std::size_t>(f - games)]);
        else if (f < count)
            local_menu(local_[static_cast<std::size_t>(f - titles)]);
        break;
    case Button::Circle:
        in_tabs_ = true;
        break;
    default:
        break;
    }
    f = std::clamp(f, 0, std::max(0, count - 1));
}

void App::press_runtimes(Button button)
{
    const int count = static_cast<int>(akeno_runtimes().size());
    if (button == Button::Up)
    {
        if (runtime_focus_ == 0)
            in_tabs_ = true;
        else
            --runtime_focus_;
    }
    else if (button == Button::Down)
        runtime_focus_ = std::min(runtime_focus_ + 1, count - 1);
    else if (button == Button::Circle)
        in_tabs_ = true;
    else if (button == Button::Cross)
    {
        const RuntimeInfo &info = akeno_runtimes()[static_cast<std::size_t>(runtime_focus_)];
        open_menu(info.name,
                  std::string(info.engines) + "\n\nGrundlage: " + info.basis + "\n\nStand: " + info.detail,
                  {{"Schließen", [] {}}});
    }
}

void App::press_downloads(Button button)
{
    const auto jobs = installer_->jobs();
    const int count = static_cast<int>(jobs.size());
    if (button == Button::Up)
    {
        if (download_focus_ == 0)
            in_tabs_ = true;
        else
            --download_focus_;
    }
    else if (button == Button::Down)
        download_focus_ = std::min(download_focus_ + 1, std::max(0, count - 1));
    else if (button == Button::Cross && download_focus_ < count && !jobs[static_cast<std::size_t>(download_focus_)].finished())
    {
        installer_->cancel(jobs[static_cast<std::size_t>(download_focus_)].id);
        toast("Abbruch angefordert", jobs[static_cast<std::size_t>(download_focus_)].title);
    }
    else if (button == Button::Square)
    {
        installer_->clear_finished();
        download_focus_ = 0;
    }
    else if (button == Button::Circle)
        in_tabs_ = true;
}

void App::press_settings(Button button)
{
    const int provider_rows = static_cast<int>(providers_.size());
    const int count = 7 + provider_rows;
    if (button == Button::Up)
    {
        if (settings_focus_ == 0)
            in_tabs_ = true;
        else
            --settings_focus_;
        return;
    }
    if (button == Button::Down)
    {
        settings_focus_ = std::min(settings_focus_ + 1, count - 1);
        return;
    }
    if (button == Button::Circle)
    {
        in_tabs_ = true;
        return;
    }
    if (button != Button::Cross)
        return;
    if (settings_focus_ == 0 && !roots_.empty())
        settings_.root = (settings_.root + 1) % static_cast<int>(roots_.size());
    else if (settings_focus_ == 1)
    {
        catalog_->refresh();
        toast("Kataloge", "Alle aktivierten Stores werden neu geladen");
    }
    else if (settings_focus_ == 2)
    {
        settings_.adult = !settings_.adult;
        toast("Jugendschutz", settings_.adult ? "Wird nach dem nächsten Start wirksam (Inhalte ab 18 sichtbar)"
                                              : "Inhalte für Erwachsene werden ausgeblendet");
    }
    else if (settings_focus_ == 3)
        settings_.reduce_motion = !settings_.reduce_motion;
    else if (settings_focus_ == 4)
        settings_.auto_update = !settings_.auto_update;
    else if (settings_focus_ == 5)
    {
        const int queued = update_all();
        toast("Updates", queued ? std::to_string(queued) + " Titel zur Warteschlange hinzugefügt"
                                : "Alle von Akeno installierten Titel sind aktuell");
    }
    else if (settings_focus_ == 6)
        itch_menu();
    else
    {
        ProviderStatus &p = providers_[static_cast<std::size_t>(settings_focus_ - 7)];
        p.enabled = !p.enabled;
        catalog_->set_enabled(p.id, p.enabled);
        auto &disabled = settings_.disabled_providers;
        disabled.erase(std::remove(disabled.begin(), disabled.end(), p.id), disabled.end());
        if (!p.enabled)
            disabled.push_back(p.id);
        else
            catalog_->refresh();
    }
    settings_.save(settings_path_);
}

void App::press_detail(Button button)
{
    const Game &game = games_[static_cast<std::size_t>(detail_)];
    const auto actions = actions_for(game);
    switch (button)
    {
    case Button::Left:
        detail_button_ = std::max(0, detail_button_ - 1);
        break;
    case Button::Right:
        detail_button_ = std::min(static_cast<int>(actions.size()) - 1, detail_button_ + 1);
        break;
    case Button::Down:
        detail_scroll_ = std::min(detail_scroll_ + 160, 2400.0f);
        break;
    case Button::Up:
        detail_scroll_ = std::max(0.0f, detail_scroll_ - 160);
        break;
    case Button::Cross:
        if (detail_button_ < static_cast<int>(actions.size()))
            run_action(game, actions[static_cast<std::size_t>(detail_button_)].kind);
        break;
    case Button::Circle:
        detail_ = -1;
        break;
    default:
        break;
    }
}

namespace
{
const char *kKeys[4] = {"1234567890", "QWERTZUIOPÜ", "ASDFGHJKLÖÄ", "YXCVBNM-'."};
std::vector<std::string> key_row(int row)
{
    std::vector<std::string> keys;
    const std::string text = kKeys[row];
    std::size_t i = 0;
    while (i < text.size())
    {
        const std::size_t start = i;
        next_codepoint(text, i);
        keys.push_back(text.substr(start, i - start));
    }
    return keys;
}
} // namespace

void App::press_keyboard(Button button)
{
    const int rows = 5;
    const auto row_size = [&](int row) { return row < 4 ? static_cast<int>(key_row(row).size()) : 3; };
    switch (button)
    {
    case Button::Up:
        key_row_ = std::max(0, key_row_ - 1);
        key_col_ = std::min(key_col_, row_size(key_row_) - 1);
        break;
    case Button::Down:
        key_row_ = std::min(rows - 1, key_row_ + 1);
        key_col_ = std::min(key_col_, row_size(key_row_) - 1);
        break;
    case Button::Left:
        key_col_ = (key_col_ + row_size(key_row_) - 1) % row_size(key_row_);
        break;
    case Button::Right:
        key_col_ = (key_col_ + 1) % row_size(key_row_);
        break;
    case Button::Cross:
        if (key_row_ < 4)
            query_ += key_row(key_row_)[static_cast<std::size_t>(key_col_)];
        else if (key_col_ == 0)
            query_ += ' ';
        else if (key_col_ == 1 && !query_.empty())
        {
            std::size_t cut = query_.size() - 1;
            while (cut > 0 && (static_cast<unsigned char>(query_[cut]) & 0xC0) == 0x80)
                --cut;
            query_.erase(cut);
        }
        else if (key_col_ == 2)
            keyboard_ = false;
        break;
    case Button::Square:
        if (!query_.empty())
        {
            std::size_t cut = query_.size() - 1;
            while (cut > 0 && (static_cast<unsigned char>(query_[cut]) & 0xC0) == 0x80)
                --cut;
            query_.erase(cut);
        }
        break;
    case Button::Triangle:
        query_ += ' ';
        break;
    case Button::Circle:
    case Button::R1:
    case Button::Options:
        keyboard_ = false;
        break;
    default:
        break;
    }
    if (key_entry_)
    {
        // Typing the itch.io key: lower case, no search; closing the keyboard saves it.
        for (char &c : query_)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (query_.size() > 128)
            query_.resize(128);
        if (!keyboard_)
        {
            key_entry_ = false;
            std::string error;
            const std::string key = trim(query_);
            query_.clear();
            if (!key.empty() && set_itch_key(key, error))
                toast("itch.io verbunden", "Schlüssel gespeichert");
            else if (!key.empty())
                toast("Schlüssel abgelehnt", error, true);
        }
        return;
    }
    if (query_.size() > 40)
        query_.resize(40);
    filter_ = 0;
    tab_ = Tab::Store;
    filter_focus_ = false;
    store_focus_ = 0;
    rebuild_store();
}

void App::press_menu(Button button)
{
    if (button == Button::Up)
        menu_focus_ = std::max(0, menu_focus_ - 1);
    else if (button == Button::Down)
        menu_focus_ = std::min(static_cast<int>(menu_.size()) - 1, menu_focus_ + 1);
    else if (button == Button::Circle)
        menu_.clear();
    else if (button == Button::Cross)
    {
        auto run = menu_[static_cast<std::size_t>(menu_focus_)].run;
        menu_.clear();
        if (run)
            run();
    }
}

void App::open_tab_for_test(int tab)
{
    tab_ = static_cast<Tab>(std::clamp(tab, 0, static_cast<int>(Tab::Count) - 1));
    detail_ = -1;
    fade_ = 1;
    if (tab_ == Tab::Discover && !shelves_.empty())
        row_ = 1;
    if (tab_ == Tab::Store)
        filter_focus_ = false;
}

void App::open_detail_for_test(const std::string &key)
{
    for (std::size_t i = 0; i < games_.size(); ++i)
        if (games_[i].title_id == key || games_[i].id == key)
        {
            open_detail(static_cast<int>(i));
            detail_anim_ = 1;
            return;
        }
}

// ---------------------------------------------------------------------------------------
// Update

void App::update(float dt)
{
    time_ += dt;
    library_poll_ -= dt;
    sync_data();
    images_->pump(platform::is_console() ? 2 : 6);
    images_->next_frame();
    fade_ = std::min(1.0f, fade_ + dt * 5);
    focus_pop_ = std::min(1.0f, focus_pop_ + dt * 5.5f);
    detail_anim_ = std::min(1.0f, detail_anim_ + dt * 4.5f);
    hero_fade_ = std::min(1.0f, hero_fade_ + dt * 2.2f);
    hero_timer_ += dt;
    if (hero_timer_ > 9 && row_ != 0 && !featured_.empty() && detail_ < 0)
    {
        hero_previous_ = featured_[static_cast<std::size_t>(hero_)];
        hero_ = (hero_ + 1) % static_cast<int>(featured_.size());
        hero_fade_ = 0;
        hero_timer_ = 0;
    }
    // Scroll targets.
    const float discover_target = row_ <= 1 ? 0 : kHeroH + 20 + (row_ - 1) * kShelfH - 0;
    discover_scroll_ = approach(discover_scroll_, discover_target, dt, 10);
    const float visible = (kWidth - 2 * kMargin) / (kShelfTile + kShelfGap);
    for (Shelf &shelf : shelves_)
    {
        const float target = std::max(0.0f, (shelf.focus - (visible - 2.2f)) * (kShelfTile + kShelfGap));
        shelf.scroll = approach(shelf.scroll, target, dt, 12);
    }
    const float store_target = std::max(0.0f, (store_focus_ / kStoreColumns - 1) * kStoreRow);
    store_scroll_ = approach(store_scroll_, store_target, dt, 12);
    const int titles = static_cast<int>(profiles_.size() + installed_.size());
    const float library_target =
        library_focus_ < titles ? std::max(0.0f, (library_focus_ / kLibraryColumns - 1) * 270.0f)
                                : std::max(0.0f, ((titles + kLibraryColumns - 1) / kLibraryColumns) * 270.0f - 200 +
                                                     (library_focus_ - titles) * 96.0f - 300);
    library_scroll_ = approach(library_scroll_, library_target, dt, 12);
    runtime_scroll_ = approach(runtime_scroll_, std::max(0.0f, runtime_focus_ * 104.0f - 520), dt, 12);
    settings_scroll_ = approach(settings_scroll_, std::max(0.0f, settings_focus_ * 92.0f - 540), dt, 12);
    diagnose_scroll_ = approach(diagnose_scroll_, diagnose_target_, dt, 12);
    if (!pending_asked_ && menu_.empty() && time_ > 1.5f)
    {
        pending_asked_ = true;
        const std::string error_file = fs::join(platform::data_dir(), "launch/play-error.txt");
        if (const auto text = fs::read_file(error_file, 4096); text && !returned_.empty())
        {
            const auto lines = split(*text, '\n');
            toast("Spiel konnte nicht starten", lines.size() > 1 ? lines[1] : *text, true);
            compat_->clear_pending();
        }
        else
            ask_pending_report();
        fs::remove_tree(error_file);
    }
    for (Toast &t : toasts_)
        t.age += dt;
    while (!toasts_.empty() && toasts_.front().age > 5)
        toasts_.pop_front();
    // Finished jobs become notifications.
    static std::map<int, JobState> announced;
    for (const Job &job : installer_->jobs())
        if (job.finished() && announced[job.id] != job.state)
        {
            announced[job.id] = job.state;
            if (job.state == JobState::Done && job.library && job.kind == JobKind::Install)
                if (const auto profile = read_profile(job.profile))
                    ensure_components(profile->runtime, true);
            if (job.state == JobState::Done)
                toast(job.kind == JobKind::Install ? "Installiert" : "Entfernt",
                      job.title + (job.kind != JobKind::Install ? ""
                                   : job.library               ? " – in Akenos Bibliothek, bereit zum Spielen"
                                                               : " – ShadowMountPlus registriert den Titel gleich"));
            else if (job.state == JobState::Failed)
                toast("Fehlgeschlagen: " + job.title, job.error, true);
        }
}

// ---------------------------------------------------------------------------------------
// Drawing helpers

float App::draw_glyph(Button button, float x, float y, float size)
{
    const float cx = x + size / 2, cy = y + size / 2, r = size / 2;
    Color color = theme::dim;
    gfx_.circle(cx, cy, r, {255, 255, 255, 30});
    const auto line = [&](float x1, float y1, float x2, float y2, float w, Color c) {
        const float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
        const float nx = -dy / len * w / 2, ny = dx / len * w / 2;
        const SDL_Color sc{c.r, c.g, c.b, c.a};
        const SDL_Vertex v[4] = {{{x1 + nx, y1 + ny}, sc, {0, 0}}, {{x2 + nx, y2 + ny}, sc, {0, 0}},
                                 {{x2 - nx, y2 - ny}, sc, {0, 0}}, {{x1 - nx, y1 - ny}, sc, {0, 0}}};
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(gfx_.renderer(), nullptr, v, 4, idx, 6);
    };
    const float k = r * 0.42f, w = std::max(2.0f, size * 0.09f);
    switch (button)
    {
    case Button::Cross:
        color = {124, 170, 255};
        line(cx - k, cy - k, cx + k, cy + k, w, color);
        line(cx - k, cy + k, cx + k, cy - k, w, color);
        break;
    case Button::Circle:
        color = {255, 110, 120};
        gfx_.circle(cx, cy, k + w / 2, color);
        gfx_.circle(cx, cy, k - w / 2, {40, 30, 60, 255});
        break;
    case Button::Square:
        color = {236, 130, 220};
        gfx_.rounded_outline({cx - k, cy - k, 2 * k, 2 * k}, 2, w, color);
        break;
    case Button::Triangle:
        color = {80, 220, 180};
        line(cx, cy - k, cx + k, cy + k * 0.75f, w, color);
        line(cx + k, cy + k * 0.75f, cx - k, cy + k * 0.75f, w, color);
        line(cx - k, cy + k * 0.75f, cx, cy - k, w, color);
        break;
    case Button::Up:
    case Button::Down:
        // The d-pad's up/down pair: two chevrons.
        color = theme::text;
        line(cx - k * 0.7f, cy - k * 0.25f, cx, cy - k, w, color);
        line(cx, cy - k, cx + k * 0.7f, cy - k * 0.25f, w, color);
        line(cx - k * 0.7f, cy + k * 0.25f, cx, cy + k, w, color);
        line(cx, cy + k, cx + k * 0.7f, cy + k * 0.25f, w, color);
        break;
    case Button::Options:
        for (int i = -1; i <= 1; ++i)
            gfx_.rounded({cx - k, cy + i * k * 0.6f - w / 2, 2 * k, w}, w / 2, theme::text);
        break;
    default:
    {
        const char *label = button == Button::L1 ? "L1" : button == Button::R1 ? "R1" : "";
        gfx_.text(label, cx, cy - 11, 18, theme::text, Weight::Bold, Align::Center);
        break;
    }
    }
    return size;
}

void App::draw_badge(const std::string &text, float x, float y, Color fill, Color ink)
{
    const float w = gfx_.measure(text, 17, Weight::Bold) + 22;
    gfx_.rounded({x, y, w, 30}, 15, fill);
    gfx_.text(text, x + 11, y + 5, 17, ink, Weight::Bold);
}

void App::draw_button(const std::string &label, const Rect &rect, bool focused, bool primary)
{
    if (focused)
        gfx_.glow(rect, rect.h / 2, theme::glow.with_alpha(0.35f), 16);
    if (primary || focused)
    {
        gfx_.rounded(rect, rect.h / 2, focused ? theme::accent : theme::accent.with_alpha(0.75f));
        const float a = focused ? 0.65f : 0.35f;
        gfx_.gradient_h({rect.x + rect.h / 2, rect.y, rect.w - rect.h, rect.h}, theme::accent.with_alpha(0),
                        theme::accent2.with_alpha(a));
        // The right cap gets the gradient's end layer over the same base, so the pill has no seam.
        const Color end = theme::accent2.with_alpha(a);
        // Clip to the cap's right half, inside any clip that is already set, and restore it after.
        SDL_Renderer *renderer = gfx_.renderer();
        SDL_Rect previous{};
        const bool clipped = SDL_RenderIsClipEnabled(renderer);
        SDL_RenderGetClipRect(renderer, &previous);
        SDL_Rect cap = Rect{rect.x + rect.w - rect.h / 2, rect.y, rect.h / 2, rect.h}.sdl();
        if (!clipped || SDL_IntersectRect(&previous, &cap, &cap))
        {
            SDL_RenderSetClipRect(renderer, &cap);
            gfx_.rounded({rect.x + rect.w - rect.h, rect.y, rect.h, rect.h}, rect.h / 2, end);
        }
        SDL_RenderSetClipRect(renderer, clipped ? &previous : nullptr);
    }
    else
        gfx_.rounded(rect, rect.h / 2, {255, 255, 255, 26});
    gfx_.text(label, rect.x + rect.w / 2, rect.y + rect.h / 2 - 14, 24, theme::text, Weight::Bold, Align::Center,
              rect.w - 24);
}

void App::draw_progress(const Rect &rect, float fraction, Color color)
{
    gfx_.rounded(rect, rect.h / 2, {255, 255, 255, 28});
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    if (fraction > 0)
    {
        const Rect fill{rect.x, rect.y, std::max(rect.h, rect.w * fraction), rect.h};
        gfx_.rounded(fill, rect.h / 2, color);
    }
}

void App::draw_tile(const Game &game, float x, float y, float size, bool focused, float pop)
{
    const float scale = focused ? 1 + 0.07f * ease_out_back(pop) : 1;
    const float s = size * scale;
    const Rect r{x + (size - s) / 2, y + (size - s) / 2, s, s};
    if (focused)
        gfx_.glow(r, 26, theme::glow.with_alpha(0.45f * std::min(1.0f, pop * 2)), 22);
    const std::string source = artwork(game);
    const Texture *art = images_->get(source, static_cast<int>(size), static_cast<int>(size), 24);
    if (art)
        gfx_.image(*art, r);
    else
        draw_missing_cover(game.title, r, 24, !source.empty() && !images_->failed(source));
    // Status badge.
    const InstalledTitle *installed = installed_for(game);
    std::string badge;
    Color fill{0, 0, 0, 170}, ink = theme::text;
    if (installed && update_available(game, *installed))
        badge = "Update", fill = theme::warn, ink = {40, 25, 0};
    else if (installed || profile_for(game))
        badge = "Installiert", fill = theme::ok, ink = {0, 40, 25};
    else if (game.access == Access::Install && game.runtime == "akeno")
        badge = game.engine_hint == "rpgmaker2k" ? "RPG Maker · Akeno" : "Akeno-Runtime", fill = theme::accent.with_alpha(0.95f);
    else if (game.access == Access::Runtime)
        badge = "HTML5 · fehlt";
    else if (game.access == Access::Upcoming)
        badge = "Demnächst";
    else if (game.access == Access::External)
        badge = game.has_tag("giveaway") ? "Gratis · PC" : "Nicht unterstützt";
    if (!badge.empty())
        draw_badge(badge, r.x + 12, r.y + 12, fill, ink);
    if (focused)
        gfx_.rounded_outline(r, 24, 3, {255, 255, 255, static_cast<std::uint8_t>(200 * std::min(1.0f, pop * 2))});
}

void App::draw_missing_cover(const std::string &title, const Rect &r, float radius, bool loading)
{
    // No generated letter art: while the store's picture loads the tile stays neutral; when the
    // store has none (or it cannot be read), the tile says so next to the game's name.
    gfx_.rounded(r, radius, theme::panel_hi);
    gfx_.gradient_v({r.x + radius, r.y + r.h * 0.45f, r.w - 2 * radius, r.h * 0.55f - radius}, {0, 0, 0, 0},
                    {0, 0, 0, 70});
    if (loading)
        return;
    const float pad = std::max(12.0f, r.w * 0.08f);
    const float top = r.w > 300 ? pad : pad + 34; // room for a status badge in the corner
    gfx_.paragraph(title, {r.x + pad, r.y + top, r.w - 2 * pad, r.h * 0.6f}, r.w > 300 ? 30 : 21,
                   theme::text.with_alpha(0.92f), 3);
    gfx_.text("Kein Cover vom Anbieter", r.x + pad, r.y + r.h - pad - 20, r.w > 300 ? 18 : 14, theme::muted,
              Weight::Bold, Align::Left, r.w - 2 * pad);
}

// ---------------------------------------------------------------------------------------
// Screens

void App::draw_top_bar()
{
    gfx_.top_shade();
    // Logo mark.
    const Rect mark{kMargin, 34, 58, 58};
    gfx_.glow(mark, 18, theme::accent.with_alpha(0.35f), 14);
    gfx_.rounded(mark, 18, theme::accent);
    gfx_.gradient_h({mark.x + 18, mark.y, mark.w - 36, mark.h}, theme::accent.with_alpha(0), theme::accent2.with_alpha(0.9f));
    gfx_.rounded({mark.x + mark.w - 18, mark.y, 18, mark.h}, 9, theme::accent2.with_alpha(0.9f));
    gfx_.text("A", mark.x + mark.w / 2, mark.y + 8, 38, theme::text, Weight::Bold, Align::Center);
    gfx_.text("AKENO", kMargin + 76, 36, 30, theme::text, Weight::Bold);
    gfx_.text("GAMING HUB", kMargin + 78, 72, 15, theme::dim, Weight::Bold);

    float x = 420;
    float active_x = 0, active_w = 0;
    for (int i = 0; i < static_cast<int>(Tab::Count); ++i)
    {
        const bool active = static_cast<int>(tab_) == i;
        const float w = gfx_.measure(tab_label(i), 24, Weight::Bold);
        if (active)
        {
            active_x = x;
            active_w = w;
            if (in_tabs_)
                gfx_.glow({x - 18, 40, w + 36, 48}, 24, theme::glow.with_alpha(0.25f), 10);
        }
        gfx_.text(tab_label(i), x, 51, 24, active ? theme::text : theme::muted, Weight::Bold);
        x += w + 46;
    }
    if (tab_line_w_ == 0)
    {
        tab_line_x_ = active_x;
        tab_line_w_ = active_w;
    }
    tab_line_x_ = approach(tab_line_x_, active_x, 1 / 60.0f, 14);
    tab_line_w_ = approach(tab_line_w_, active_w, 1 / 60.0f, 14);
    gfx_.rounded({tab_line_x_, 90, tab_line_w_, 5}, 2.5f, theme::accent);
    gfx_.gradient_h({tab_line_x_ + tab_line_w_ * 0.4f, 90, tab_line_w_ * 0.6f, 5}, theme::accent.with_alpha(0),
                    theme::accent2);
    draw_glyph(Button::L1, 360, 48, 34);
    draw_glyph(Button::R1, x - 20, 48, 34);

    // Right side: catalog state and clock.
    char clock[8];
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::snprintf(clock, sizeof clock, "%02d:%02d", local.tm_hour, local.tm_min);
    gfx_.text(clock, kWidth - kMargin, 34, 30, theme::text, Weight::Bold, Align::Right);
    std::string state;
    if (catalog_->refreshing())
        state = "Kataloge werden geladen …";
    else
    {
        int ready = 0, total = 0;
        for (const ProviderStatus &p : providers_)
            if (p.enabled)
            {
                ++total;
                ready += p.state == ProviderStatus::State::Ready || p.state == ProviderStatus::State::Offline;
            }
        state = std::to_string(ready) + "/" + std::to_string(total) + " Stores online · " +
                std::to_string(games_.size()) + " Titel";
    }
    gfx_.text(state, kWidth - kMargin, 76, 17, theme::dim, Weight::Regular, Align::Right);
    if (catalog_->refreshing())
    {
        const float a = time_ * 6;
        for (int i = 0; i < 8; ++i)
        {
            const float t = a + i * 0.785f;
            gfx_.circle(kWidth - kMargin - gfx_.measure(state, 17) - 22 + 8 * std::cos(t), 86 + 8 * std::sin(t),
                        2.2f, theme::accent2.with_alpha(i / 8.0f));
        }
    }
}

void App::draw_hints()
{
    std::vector<std::pair<Button, std::string>> hints;
    if (!menu_.empty())
        hints = {{Button::Cross, "Auswählen"}, {Button::Circle, "Zurück"}};
    else if (!qr_url_.empty())
        hints = {{Button::Circle, "Schließen"}};
    else if (keyboard_)
        hints = {{Button::Cross, "Eingeben"}, {Button::Square, "Löschen"}, {Button::Triangle, "Leerzeichen"},
                 {Button::Circle, "Fertig"}};
    else if (detail_ >= 0)
        hints = {{Button::Cross, "Ausführen"}, {Button::Circle, "Zurück"}};
    else
    {
        hints = {{Button::Cross, "Öffnen"}, {Button::Circle, "Zurück"}};
        if (tab_ == Tab::Store || tab_ == Tab::Discover)
            hints.push_back({Button::Triangle, "Suchen"});
        if (tab_ == Tab::Downloads)
            hints = {{Button::Cross, "Abbrechen"}, {Button::Square, "Erledigte entfernen"}, {Button::Circle, "Zurück"}};
        if (tab_ == Tab::Settings)
            hints = {{Button::Cross, "Ändern"}, {Button::Circle, "Zurück"}};
        if (tab_ == Tab::Diagnose)
            hints = {{Button::Cross, "Ausführen"}, {Button::Up, "Blättern"}, {Button::Circle, "Zurück"}};
        hints.push_back({Button::Options, "Einstellungen"});
    }
    gfx_.bottom_shade();
    float x = kWidth - kMargin;
    for (auto it = hints.rbegin(); it != hints.rend(); ++it)
    {
        const float w = gfx_.measure(it->second, 21);
        x -= w;
        gfx_.text(it->second, x, kHeight - 54, 21, theme::dim);
        x -= 40;
        draw_glyph(it->first, x, kHeight - 56, 30);
        x -= 34;
    }
    const std::string where = std::string(platform::name()) + " · " + http_->transport_name();
    gfx_.text(where, kMargin, kHeight - 54, 19, theme::muted);
}

void App::draw_discover()
{
    const float top = kTop - discover_scroll_;
    // Hero.
    const Rect hero{kMargin, top, kWidth - 2 * kMargin, kHeroH};
    if (hero.y + hero.h > 0)
    {
        gfx_.rounded(hero, 30, theme::panel);
        const auto draw_hero_art = [&](int index, float alpha) {
            if (index < 0 || index >= static_cast<int>(games_.size()) || alpha <= 0)
                return;
            const Game &g = games_[static_cast<std::size_t>(index)];
            if (!g.hero_url.empty())
                if (const Texture *t = images_->get(g.hero_url, static_cast<int>(hero.w), static_cast<int>(hero.h), 30, 2))
                {
                    gfx_.image(*t, hero, alpha);
                    return;
                }
            const Color base = provider_color(g.provider);
            gfx_.image(gfx_.panel_texture(static_cast<int>(hero.w), static_cast<int>(hero.h), 30,
                                          mix(theme::bg, base, 0.10f), mix(theme::panel_hi, base, 0.42f)),
                       hero, alpha);
            const float s = 400;
            const Rect art{hero.x + hero.w - s - 90, hero.y + (hero.h - s) / 2 + 6 * std::sin(time_ * 0.9f), s, s};
            if (const Texture *t = images_->get(artwork(g), 400, 400, 36))
            {
                gfx_.glow(art, 36, base.with_alpha(0.35f * alpha), 40);
                gfx_.image(*t, art, alpha);
            }
        };
        if (!featured_.empty())
        {
            if (hero_fade_ < 1)
                draw_hero_art(hero_previous_, 1 - hero_fade_);
            draw_hero_art(featured_[static_cast<std::size_t>(hero_)], hero_fade_);
            const Game &g = games_[static_cast<std::size_t>(featured_[static_cast<std::size_t>(hero_)])];
            const float tx = hero.x + 64;
            float by = hero.y + 56;
            draw_badge(g.provider_label, tx, by, provider_color(g.provider).with_alpha(0.9f), {10, 8, 24});
            if (g.signed_catalog)
                draw_badge("Signierter Katalog", tx + gfx_.measure(g.provider_label, 17, Weight::Bold) + 34, by,
                           {255, 255, 255, 40}, theme::text);
            gfx_.text(g.title, tx, hero.y + 104, 62, theme::text, Weight::Bold, Align::Left, 980);
            gfx_.text(g.developer + (g.category.empty() ? "" : "  ·  " + g.category), tx, hero.y + 184, 24, theme::dim,
                      Weight::Regular, Align::Left, 900);
            gfx_.paragraph(g.description, {tx, hero.y + 232, 860, 120}, 23, theme::text.with_alpha(0.85f), 3);
            const auto actions = actions_for(g);
            const std::string primary = actions.empty() ? "Details" : actions[0].label;
            draw_button(primary, {tx, hero.y + hero.h - 104, 300, 62}, row_ == 0 && !in_tabs_ && hero_button_ == 0, true);
            draw_button("Details", {tx + 320, hero.y + hero.h - 104, 200, 62},
                        row_ == 0 && !in_tabs_ && hero_button_ == 1, false);
            for (std::size_t i = 0; i < featured_.size(); ++i)
            {
                const bool on = static_cast<int>(i) == hero_;
                const float dx = hero.x + hero.w - 64 - (featured_.size() - i) * 28.0f;
                gfx_.rounded({dx, hero.y + hero.h - 44, on ? 24.0f : 10.0f, 10}, 5,
                             on ? theme::text : theme::text.with_alpha(0.35f));
            }
        }
        else
        {
            const float pulse = 0.5f + 0.5f * std::sin(time_ * 3);
            gfx_.rounded({hero.x + 64, hero.y + 110, 640, 60}, 14, {255, 255, 255, static_cast<std::uint8_t>(18 + 14 * pulse)});
            gfx_.rounded({hero.x + 64, hero.y + 200, 900, 26}, 13, {255, 255, 255, static_cast<std::uint8_t>(14 + 10 * pulse)});
            gfx_.text(catalog_->refreshing() ? "Kataloge werden geladen …" : "Keine Kataloge erreichbar",
                      hero.x + 64, hero.y + 260, 30, theme::dim, Weight::Bold);
            if (!catalog_->refreshing())
                gfx_.text("Prüfe die Netzwerkverbindung und das Datum der Konsole (HTTPS).", hero.x + 64,
                          hero.y + 306, 23, theme::muted);
        }
        if (row_ == 0 && !in_tabs_)
            gfx_.rounded_outline(hero, 30, 2, {255, 255, 255, 60});
    }
    // Shelves.
    float y = top + kHeroH + 40;
    for (std::size_t s = 0; s < shelves_.size(); ++s, y += kShelfH)
    {
        if (y > kHeight || y + kShelfH < 100)
            continue;
        Shelf &shelf = shelves_[s];
        const bool active_row = row_ == static_cast<int>(s) + 1 && !in_tabs_;
        gfx_.text(shelf.title, kMargin, y, 30, active_row ? theme::text : theme::dim, Weight::Bold);
        gfx_.text(shelf.subtitle, kMargin + gfx_.measure(shelf.title, 30, Weight::Bold) + 22, y + 9, 19, theme::muted);
        const Rect clip{0, y + 40, kWidth, kShelfTile + 100};
        gfx_.set_clip(&clip);
        for (std::size_t i = 0; i < shelf.items.size(); ++i)
        {
            const float x = kMargin + i * (kShelfTile + kShelfGap) - shelf.scroll;
            if (x > kWidth || x + kShelfTile < 0)
                continue;
            const Game &g = games_[static_cast<std::size_t>(shelf.items[i])];
            const bool focused = active_row && static_cast<int>(i) == shelf.focus;
            draw_tile(g, x, y + 56, kShelfTile, focused, focus_pop_);
            gfx_.text(g.title, x + 4, y + 56 + kShelfTile + 16, 21, focused ? theme::text : theme::dim, Weight::Bold,
                      Align::Left, kShelfTile - 8);
            gfx_.text(g.provider_label + (g.developer.empty() ? "" : " · " + g.developer), x + 4,
                      y + 56 + kShelfTile + 44, 17, theme::muted, Weight::Regular, Align::Left, kShelfTile - 8);
        }
        gfx_.set_clip(nullptr);
    }
}

void App::draw_store()
{
    // Filters (the list scrolls once the focused one would leave the screen).
    const float filter_scroll = std::max(0.0f, filter_ * 52.0f - 600.0f);
    const Rect filter_clip{0, kTop, 400, kHeight - kTop - 96};
    gfx_.set_clip(&filter_clip);
    float y = kTop + 8 - filter_scroll;
    for (std::size_t i = 0; i < filters_.size(); ++i, y += 52)
    {
        if (y + 48 < kTop || y > kHeight - 96)
            continue;
        const bool selected = static_cast<int>(i) == filter_;
        const bool focused = selected && filter_focus_ && !in_tabs_;
        const Rect r{kMargin, y, 300, 48};
        if (focused)
        {
            gfx_.glow(r, 24, theme::glow.with_alpha(0.3f), 10);
            gfx_.rounded(r, 24, theme::accent);
        }
        else if (selected)
            gfx_.rounded(r, 24, {255, 255, 255, 26});
        gfx_.text(filters_[i].label, r.x + 24, r.y + 11, 21, selected ? theme::text : theme::dim,
                  selected ? Weight::Bold : Weight::Regular);
    }
    gfx_.set_clip(nullptr);
    // Search field.
    const Rect search{410, kTop + 4, kWidth - 410 - kMargin, 60};
    gfx_.rounded(search, 30, keyboard_ ? theme::panel_hi : theme::panel);
    gfx_.circle(search.x + 36, search.y + 28, 11, theme::dim);
    gfx_.circle(search.x + 36, search.y + 28, 8, keyboard_ ? theme::panel_hi : theme::panel);
    gfx_.fill({search.x + 43, search.y + 36, 9, 3}, theme::dim);
    if (query_.empty())
        gfx_.text("Spiele, Apps und Entwickler suchen", search.x + 66, search.y + 15, 23, theme::muted);
    else
        gfx_.text(query_ + (keyboard_ && std::fmod(time_, 1.0f) < 0.5f ? "|" : ""), search.x + 66, search.y + 14, 25,
                  theme::text, Weight::Bold);
    gfx_.text(std::to_string(store_items_.size()) + " Treffer", search.x + search.w - 28, search.y + 17, 21, theme::dim,
              Weight::Regular, Align::Right);
    draw_glyph(Button::Triangle, search.x + search.w - 190, search.y + 14, 32);

    // Grid.
    const Rect clip{400, kTop + 80, kWidth - 400, kHeight - kTop - 160};
    gfx_.set_clip(&clip);
    const float grid_y = kTop + 100 - store_scroll_;
    for (std::size_t i = 0; i < store_items_.size(); ++i)
    {
        const int column = static_cast<int>(i) % kStoreColumns, row = static_cast<int>(i) / kStoreColumns;
        const float x = 410 + column * (kStoreTile + kStoreGap), yy = grid_y + row * kStoreRow;
        if (yy > kHeight || yy + kStoreRow < kTop)
            continue;
        const Game &g = games_[static_cast<std::size_t>(store_items_[i])];
        const bool focused = !filter_focus_ && !in_tabs_ && static_cast<int>(i) == store_focus_;
        draw_tile(g, x, yy, kStoreTile, focused, focus_pop_);
        gfx_.text(g.title, x + 4, yy + kStoreTile + 16, 22, focused ? theme::text : theme::dim, Weight::Bold,
                  Align::Left, kStoreTile - 8);
        gfx_.text(g.provider_label + " · " + (g.price.empty() ? "Kostenlos" : g.price), x + 4, yy + kStoreTile + 46, 17,
                  theme::muted, Weight::Regular, Align::Left, kStoreTile - 8);
    }
    if (store_items_.empty())
        gfx_.text(catalog_->refreshing() ? "Kataloge werden geladen …" : "Keine Treffer für diese Auswahl", 410,
                  kTop + 140, 28, theme::dim, Weight::Bold);
    gfx_.set_clip(nullptr);
}

void App::draw_library()
{
    float y = kTop + 6 - library_scroll_;
    gfx_.text("Bibliothek", kMargin, y, 32, theme::text, Weight::Bold);
    gfx_.text(std::to_string(profiles_.size()) + " Spiele in Akeno · " + std::to_string(installed_.size()) +
                  " PS5-Titel in den ShadowMountPlus-Ordnern",
              kMargin + 200, y + 10, 20, theme::muted);
    y += 64;
    const int games = static_cast<int>(profiles_.size());
    const int titles = games + static_cast<int>(installed_.size());
    if (titles == 0)
    {
        gfx_.rounded({kMargin, y, kWidth - 2 * kMargin, 120}, 22, theme::panel);
        gfx_.text("Noch nichts installiert. Im Store unter „In Akeno spielbar“ findest du Spiele, die direkt hier laufen.",
                  kMargin + 32, y + 46, 23, theme::dim);
        y += 150;
    }
    for (int i = 0; i < titles; ++i)
    {
        const bool is_game = i < games;
        const GameProfile *p = is_game ? &profiles_[static_cast<std::size_t>(i)] : nullptr;
        const InstalledTitle *t = is_game ? nullptr : &installed_[static_cast<std::size_t>(i - games)];
        const float x = kMargin + (i % kLibraryColumns) * (kLibraryTile + 52);
        const float ty = y + (i / kLibraryColumns) * 270.0f;
        if (ty > kHeight || ty + 270 < kTop)
            continue;
        const bool focused = !in_tabs_ && library_focus_ == i;
        const float scale = focused ? 1 + 0.07f * ease_out_back(focus_pop_) : 1;
        const float s = kLibraryTile * scale;
        const Rect r{x + (kLibraryTile - s) / 2, ty + (kLibraryTile - s) / 2, s, s};
        if (focused)
            gfx_.glow(r, 22, theme::glow.with_alpha(0.45f), 20);
        const int index = t ? game_index_for(*t) : game_index_by_id(p->source);
        std::string art = p ? p->cover : !t->icon_path.empty() ? t->icon_path : "";
        if (art.empty() && index >= 0)
            art = artwork(games_[static_cast<std::size_t>(index)]);
        const std::string name = p ? p->title : t->name;
        if (const Texture *tex = images_->get(art, static_cast<int>(kLibraryTile), static_cast<int>(kLibraryTile), 22))
            gfx_.image(*tex, r);
        else
            draw_missing_cover(name, r, 22);
        if (p)
            draw_badge("Akeno", r.x + 10, r.y + 10, theme::accent.with_alpha(0.95f), theme::text);
        else if (index >= 0 && update_available(games_[static_cast<std::size_t>(index)], *t))
            draw_badge("Update", r.x + 10, r.y + 10, theme::warn, {40, 25, 0});
        else if (t->shortcut)
            draw_badge("Verknüpfung", r.x + 10, r.y + 10, {0, 0, 0, 170}, theme::text);
        if (focused)
            gfx_.rounded_outline(r, 22, 3, {255, 255, 255, 200});
        gfx_.text(name, x + 2, ty + kLibraryTile + 14, 20, focused ? theme::text : theme::dim, Weight::Bold,
                  Align::Left, kLibraryTile);
        gfx_.text(p ? runtime_name(p->runtime) : t->title_id + " · " + (t->content_version.empty() ? "?" : t->content_version),
                  x + 2, ty + kLibraryTile + 40, 16, theme::muted, Weight::Regular, Align::Left, kLibraryTile);
    }
    y += ((titles + kLibraryColumns - 1) / kLibraryColumns) * 270.0f + 20;
    gfx_.text("Eigene Spieldateien", kMargin, y, 32, theme::text, Weight::Bold);
    const auto dirs = platform::import_dirs();
    gfx_.text("ROMs, Disc-Images und Spielordner aus " + (dirs.empty() ? std::string("/data/akeno/games") : dirs[0]),
              kMargin + 345, y + 10, 20, theme::muted);
    y += 60;
    if (local_.empty())
    {
        gfx_.rounded({kMargin, y, kWidth - 2 * kMargin, 120}, 22, theme::panel);
        gfx_.paragraph("Lege eigene, rechtmäßig erstellte Dumps (z. B. per FTP) in den Ordner oben. Akeno erkennt das "
                       "Format an der Dateisignatur und wählt die passende PS5-Runtime aus.",
                       {kMargin + 32, y + 28, kWidth - 2 * kMargin - 64, 80}, 22, theme::dim, 2);
    }
    for (std::size_t i = 0; i < local_.size(); ++i)
    {
        const LocalGame &g = local_[i];
        const Rect r{kMargin, y + i * 96.0f, kWidth - 2 * kMargin, 84};
        if (r.y > kHeight || r.y + r.h < kTop)
            continue;
        const bool focused = !in_tabs_ && library_focus_ == titles + static_cast<int>(i);
        if (focused)
            gfx_.glow(r, 20, theme::glow.with_alpha(0.3f), 12);
        gfx_.rounded(r, 20, focused ? theme::panel_hi : theme::panel);
        gfx_.text(g.name, r.x + 28, r.y + 14, 23, theme::text, Weight::Bold, Align::Left, 800);
        gfx_.text(g.detection.format + (g.detection.engine.empty() ? "" : " · " + g.detection.engine), r.x + 28,
                  r.y + 48, 18, theme::dim, Weight::Regular, Align::Left, 800);
        std::string status;
        Color color = theme::muted;
        if (g.detection.playable)
            status = "Spielbar in Akeno (" + runtime_name(g.detection.runtime) + ")", color = theme::ok;
        else if (!g.detection.runtime.empty())
            status = "Akeno-Runtime fehlt noch: " + runtime_name(g.detection.runtime), color = theme::warn;
        else
            status = "Nicht unterstützt", color = theme::bad;
        gfx_.text(status, r.x + r.w - 28, r.y + 30, 20, color, Weight::Bold, Align::Right);
    }
}

void App::draw_runtimes()
{
    float y = kTop + 6 - runtime_scroll_;
    gfx_.text("Akeno-Runtimes", kMargin, y, 32, theme::text, Weight::Bold);
    gfx_.paragraph("Spiele laufen in Akeno selbst: Akeno startet sich im Spielmodus neu und die passende Runtime "
                   "übernimmt Bild, Controller und Ton. Akeno öffnet dafür keine anderen Apps. Hier steht ehrlich, "
                   "was eingebaut ist und was noch fehlt.",
                   {kMargin, y + 50, 1600, 70}, 21, theme::dim, 2);
    y += 130;
    const auto &runtimes = akeno_runtimes();
    for (std::size_t i = 0; i < runtimes.size(); ++i)
    {
        const RuntimeInfo &info = runtimes[i];
        const Rect r{kMargin, y + i * 104.0f, kWidth - 2 * kMargin, 92};
        if (r.y > kHeight || r.y + r.h < kTop)
            continue;
        const bool focused = !in_tabs_ && runtime_focus_ == static_cast<int>(i);
        if (focused)
            gfx_.glow(r, 22, theme::glow.with_alpha(0.3f), 12);
        gfx_.rounded(r, 22, focused ? theme::panel_hi : theme::panel);
        gfx_.text(info.name, r.x + 32, r.y + 14, 24, theme::text, Weight::Bold);
        gfx_.text(std::string(info.engines) + "  ·  " + info.detail, r.x + 32, r.y + 50, 18, theme::dim,
                  Weight::Regular, Align::Left, 1380);
        const bool built = info.state == RuntimeState::Integrated;
        const std::string status = built ? "Eingebaut" : "Geplant";
        const Color color = built ? theme::ok : theme::muted;
        draw_badge(status, r.x + r.w - 40 - gfx_.measure(status, 17, Weight::Bold) - 22, r.y + 30,
                   color.with_alpha(0.9f), {8, 8, 20});
    }
}

void App::draw_downloads()
{
    const auto jobs = installer_->jobs();
    gfx_.text("Downloads & Installationen", kMargin, kTop + 6, 32, theme::text, Weight::Bold);
    const std::string where = roots_.empty() ? "" : roots_[static_cast<std::size_t>(settings_.root)].path;
    gfx_.text("Ziel: " + where, kMargin + 470, kTop + 16, 20, theme::muted);
    if (jobs.empty())
    {
        gfx_.rounded({kMargin, kTop + 80, kWidth - 2 * kMargin, 140}, 22, theme::panel);
        gfx_.paragraph("Keine Aufträge. Jeder Download wird beim Empfang per SHA-256 geprüft, entpackt und erst "
                       "zum Schluss mit einem einzigen Umbenennen aktiviert.",
                       {kMargin + 32, kTop + 120, kWidth - 2 * kMargin - 64, 80}, 22, theme::dim, 2);
        return;
    }
    float y = kTop + 80;
    for (std::size_t i = 0; i < jobs.size(); ++i, y += 132)
    {
        const Job &job = jobs[i];
        const Rect r{kMargin, y, kWidth - 2 * kMargin, 118};
        const bool focused = !in_tabs_ && download_focus_ == static_cast<int>(i);
        if (focused)
            gfx_.glow(r, 22, theme::glow.with_alpha(0.3f), 12);
        gfx_.rounded(r, 22, focused ? theme::panel_hi : theme::panel);
        gfx_.text(job.title, r.x + 32, r.y + 16, 25, theme::text, Weight::Bold, Align::Left, 900);
        const Color state_color = job.state == JobState::Done     ? theme::ok
                                  : job.state == JobState::Failed ? theme::bad
                                                                  : theme::accent2;
        gfx_.text(std::string(job_state_label(job.state)) + (job.title_id.empty() ? "" : "  ·  " + job.title_id),
                  r.x + r.w - 32, r.y + 18, 21, state_color, Weight::Bold, Align::Right);
        const float fraction = job.state == JobState::Done ? 1.0f
                               : job.total               ? static_cast<float>(job.done) / job.total
                                                         : 0.0f;
        if (!job.finished() || job.state == JobState::Done)
            draw_progress({r.x + 32, r.y + 62, r.w - 64, 12}, fraction,
                          job.state == JobState::Done ? theme::ok : theme::accent);
        std::string detail;
        if (job.state == JobState::Failed)
            detail = job.error;
        else if (job.state == JobState::Downloading)
        {
            detail = format_bytes(job.done) + " von " + (job.total ? format_bytes(job.total) : std::string("?"));
            if (job.bytes_per_second > 1)
            {
                detail += "  ·  " + format_bytes(static_cast<std::uint64_t>(job.bytes_per_second)) + "/s";
                if (job.total > job.done)
                    detail += "  ·  noch " + format_duration(static_cast<std::uint64_t>(
                                                  (job.total - job.done) / job.bytes_per_second));
            }
        }
        else if (job.state == JobState::Unpacking)
            detail = format_bytes(job.done) + " von " + format_bytes(job.total) + " entpackt";
        else if (!job.note.empty())
            detail = job.note;
        gfx_.text(detail, r.x + 32, r.y + 84, 18, job.state == JobState::Failed ? theme::bad : theme::dim,
                  Weight::Regular, Align::Left, r.w - 64);
    }
}

void App::draw_settings()
{
    float y = kTop + 6 - settings_scroll_;
    gfx_.text("Einstellungen", kMargin, y, 32, theme::text, Weight::Bold);
    y += 66;
    int row = 0;
    const auto entry = [&](const std::string &label, const std::string &value, const std::string &note, Color value_color) {
        const Rect r{kMargin, y, 1180, 80};
        const bool focused = !in_tabs_ && settings_focus_ == row;
        if (r.y < kHeight && r.y + r.h > kTop)
        {
            if (focused)
                gfx_.glow(r, 20, theme::glow.with_alpha(0.3f), 12);
            gfx_.rounded(r, 20, focused ? theme::panel_hi : theme::panel);
            gfx_.text(label, r.x + 28, r.y + (note.empty() ? 26 : 12), 23, theme::text, Weight::Bold, Align::Left, 640);
            if (!note.empty())
                gfx_.text(note, r.x + 28, r.y + 46, 16, theme::muted, Weight::Regular, Align::Left, 760);
            gfx_.text(value, r.x + r.w - 28, r.y + 26, 21, value_color, Weight::Bold, Align::Right, 380);
        }
        y += 92;
        ++row;
    };
    const auto &root = roots_.empty() ? platform::InstallRoot{} : roots_[static_cast<std::size_t>(settings_.root)];
    entry("Installationsort", root.label, root.writable ? "Beschreibbar" : "Nicht beschreibbar – ShadowMountPlus 1.7+ nötig",
          root.writable ? theme::ok : theme::bad);
    entry("Kataloge jetzt aktualisieren", catalog_->refreshing() ? "Lädt …" : "", "", theme::dim);
    entry("Inhalte für Erwachsene", settings_.adult ? "Sichtbar" : "Ausgeblendet",
          "Filtert itch.io- und Game-Jolt-Einträge mit Erwachsenen-Kennzeichnung", theme::dim);
    entry("Reduzierte Bewegung", settings_.reduce_motion ? "An" : "Aus", "Hintergrundanimation abschalten", theme::dim);
    entry("Automatische Updates", settings_.auto_update ? "An" : "Aus",
          "Aktualisiert von Akeno installierte Titel beim Start, sobald der Katalog geladen ist", theme::dim);
    int updates = 0;
    for (const InstalledTitle &t : installed_)
        if (const int i = game_index_by_title(t.title_id); i >= 0 && t.managed && update_available(games_[static_cast<std::size_t>(i)], t))
            ++updates;
    entry("Alle aktualisieren", updates ? std::to_string(updates) + " verfügbar" : "Alles aktuell",
          "Neuere Katalogversionen aller von Akeno installierten Titel", updates ? theme::warn : theme::ok);
    entry("itch.io-Konto", itch_key_.empty() ? "Nicht verbunden" : "Verbunden",
          "Eigener API-Schlüssel für Downloads über die offizielle itch.io-API", itch_key_.empty() ? theme::dim : theme::ok);
    for (const ProviderStatus &p : providers_)
    {
        std::string value = !p.enabled ? "Aus"
                            : p.state == ProviderStatus::State::Ready   ? std::to_string(p.count) + " Einträge"
                            : p.state == ProviderStatus::State::Offline ? "Offline (Cache)"
                            : p.state == ProviderStatus::State::Loading ? "Lädt …"
                            : p.state == ProviderStatus::State::Failed  ? "Fehler"
                                                                         : "Bereit";
        const Color color = !p.enabled ? theme::muted
                            : p.state == ProviderStatus::State::Failed ? theme::bad
                                                                        : theme::ok;
        entry(p.label, value, p.state == ProviderStatus::State::Failed ? p.message : p.about, color);
    }
    // About panel beside the list.
    const Rect about{kMargin + 1220, kTop + 72, kWidth - 2 * kMargin - 1220, 760};
    gfx_.rounded(about, 24, theme::panel);
    float ay = about.y + 28;
    gfx_.text("Über Akeno", about.x + 28, ay, 26, theme::text, Weight::Bold);
    ay += 48;
    const std::vector<std::pair<std::string, std::string>> facts = {
        {"Version", AKENO_VERSION " (Alpha)"},
        {"Plattform", platform::name()},
        {"Netzwerk", http_->transport_name()},
        {"Daten", platform::data_dir()},
        {"Title-ID", kAkenoTitleId},
        {"Lizenz", "GPL-3.0-or-later"},
    };
    for (const auto &[k, v] : facts)
    {
        gfx_.text(k, about.x + 28, ay, 17, theme::muted);
        gfx_.text(v, about.x + 28, ay + 22, 19, theme::text, Weight::Regular, Align::Left, about.w - 56);
        ay += 58;
    }
    gfx_.paragraph("Basiert auf ps5-native-app-boilerplate, der Runtime von OpenRCT2 für PS5, dem ProsperoStore-"
                   "Katalogvertrag, SDL2, Monocypher, yyjson, miniz, stb und Inter. Kein Sony-SDK, keine "
                   "proprietären Dateien.",
                   {about.x + 28, ay + 8, about.w - 56, 220}, 17, theme::dim, 8);
}

void App::draw_detail()
{
    const Game &g = games_[static_cast<std::size_t>(detail_)];
    const float t = ease_out_back(detail_anim_);
    gfx_.fill({0, 0, kWidth, kHeight}, {5, 7, 20, static_cast<std::uint8_t>(255 * std::min(1.0f, detail_anim_ * 2.0f))});
    const float offset = (1 - t) * 60 - detail_scroll_;
    const float alpha = std::min(1.0f, detail_anim_ * 1.6f);
    // Backdrop.
    const std::string backdrop = !g.hero_url.empty() ? g.hero_url : artwork(g);
    if (const Texture *bg = images_->get(backdrop, kWidth, 700, 0, 1))
    {
        gfx_.image(*bg, {0, offset - 60, kWidth, 700}, alpha);
        gfx_.fill({0, offset + 640, kWidth, kHeight}, {4, 5, 16, 255});
    }
    // Cover.
    const Rect cover{110, offset + 150, 400, 400};
    gfx_.glow(cover, 32, provider_color(g.provider).with_alpha(0.35f * alpha), 34);
    if (const Texture *art = images_->get(artwork(g), 400, 400, 32))
        gfx_.image(*art, cover, alpha);
    else
    {
        gfx_.rounded(cover, 32, theme::panel_hi);
        draw_missing_cover(g.title, cover, 32, !artwork(g).empty() && !images_->failed(artwork(g)));
    }
    // Header.
    const float x = 580;
    float y = offset + 150;
    float bx = x;
    const auto badge = [&](const std::string &text, Color fill, Color ink) {
        draw_badge(text, bx, y, fill, ink);
        bx += gfx_.measure(text, 17, Weight::Bold) + 34;
    };
    badge(g.provider_label, provider_color(g.provider).with_alpha(0.9f), {10, 8, 24});
    if (g.signed_catalog)
        badge("Signatur geprüft", theme::ok.with_alpha(0.9f), {0, 40, 25});
    if (g.sandbox == "leaves")
        badge("Verlässt die Sandbox", theme::warn.with_alpha(0.9f), {40, 25, 0});
    else if (g.sandbox == "stays")
        badge("Bleibt in der Sandbox", {255, 255, 255, 40}, theme::text);
    badge(g.price.empty() ? "Kostenlos" : g.price, {255, 255, 255, 40}, theme::text);
    y += 46;
    gfx_.text(g.title, x, y, 58, theme::text, Weight::Bold, Align::Left, kWidth - x - kMargin);
    y += 78;
    gfx_.text(g.developer, x, y, 26, theme::dim, Weight::Regular, Align::Left, 900);
    y += 52;
    // Facts.
    std::vector<std::pair<std::string, std::string>> facts;
    if (!g.version.empty())
        facts.push_back({"Version", g.version});
    if (g.size)
        facts.push_back({"Größe", format_bytes(g.size)});
    if (!g.title_id.empty())
        facts.push_back({"Title-ID", g.title_id});
    if (!g.category.empty())
        facts.push_back({"Kategorie", g.category});
    if (!g.license.empty())
        facts.push_back({"Lizenz", g.license});
    if (!g.genres.empty())
    {
        std::string genres;
        for (std::size_t i = 0; i < g.genres.size() && i < 3; ++i)
            genres += (i ? ", " : "") + g.genres[i];
        facts.push_back({"Genre", genres});
    }
    if (!g.age.empty())
        facts.push_back({"Alter", g.age});
    facts.push_back({"Runtime", runtime_label(g)});
    if (facts.size() > 6)
        facts.resize(6);
    float fx = x;
    for (const auto &[k, v] : facts)
    {
        gfx_.text(k, fx, y, 17, theme::muted);
        gfx_.text(v, fx, y + 24, 22, theme::text, Weight::Bold, Align::Left, 260);
        fx += std::max(170.0f, std::min(300.0f, gfx_.measure(v, 22, Weight::Bold) + 48));
    }
    y += 84;
    if (const InstalledTitle *installed = installed_for(g))
    {
        const bool update = update_available(g, *installed);
        gfx_.text(update ? "Installiert: " + installed->content_version + "  →  verfügbar: " + g.content_version
                         : "Installiert in " + installed->folder,
                  x, y, 20, update ? theme::warn : theme::ok, Weight::Bold);
        y += 36;
    }
    else if (const GameProfile *profile = profile_for(g))
    {
        gfx_.text("In Akenos Bibliothek · " + runtime_name(profile->runtime) + " · " + profile->folder, x, y, 20,
                  theme::ok, Weight::Bold, Align::Left, kWidth - x - kMargin);
        y += 36;
    }
    // Compatibility, online and controller facts (master prompt sections 11, 14 and 19).
    {
        const CompatReport *report = compat_->find(g.id);
        std::string shown_note;
        const Compat shown = shown_compat(g, &shown_note);
        const std::string label = std::string(report ? "Dein Test: " : "Kompatibilität: ") + compat_label(shown);
        draw_badge(label, x, y, compat_color(shown).with_alpha(0.9f), {10, 12, 24});
        float lx = x + gfx_.measure(label, 17, Weight::Bold) + 50;
        std::string note = report ? std::string("Start ") + aspect_label(report->start) + " · Gameplay " +
                                        aspect_label(report->gameplay) + " · Grafik " + aspect_label(report->graphics) +
                                        " · Ton " + aspect_label(report->audio) + " · Controller " +
                                        aspect_label(report->controller) + " · Online " + aspect_label(report->network)
                                  : shown_note;
        float note_h = 0;
        if (!note.empty())
            note_h = gfx_.paragraph(note, {lx, y + 1, kWidth - lx - kMargin, 60}, 18, theme::dim, 2);
        y += std::max(44.0f, note_h + 16);
        std::string online;
        if (!g.online.empty())
            online += "Online: " + g.online;
        if (!g.crossplay.empty())
            online += (online.empty() ? "" : "   ·   ") + std::string("Crossplay: ") + g.crossplay;
        if (!g.controller.empty())
            online += (online.empty() ? "" : "   ·   ") + std::string("Steuerung: ") + g.controller;
        if (!online.empty())
        {
            gfx_.text(online, x, y, 19, theme::text.with_alpha(0.85f), Weight::Regular, Align::Left, kWidth - x - kMargin);
            y += 36;
        }
    }
    if (!g.needs.empty())
    {
        const Rect warn{x, y, kWidth - x - kMargin, 64};
        gfx_.rounded(warn, 18, theme::warn.with_alpha(0.16f));
        gfx_.text("Benötigt: " + g.needs, warn.x + 24, warn.y + 18, 21, theme::warn, Weight::Bold, Align::Left,
                  warn.w - 48);
        y += 82;
    }
    // Actions.
    const auto actions = actions_for(g);
    float ax = x;
    for (std::size_t i = 0; i < actions.size(); ++i)
    {
        const float w = std::max(220.0f, gfx_.measure(actions[i].label, 24, Weight::Bold) + 70);
        draw_button(actions[i].label, {ax, y, w, 64}, static_cast<int>(i) == detail_button_, i == 0);
        ax += w + 20;
    }
    if (actions.empty())
        gfx_.text(g.access == Access::Upcoming ? "Angekündigt – noch kein Download verfügbar" : "Nur zur Information",
                  x, y + 18, 22, theme::muted, Weight::Bold);
    y += 104;
    // Text.
    const float tw = kWidth - x - kMargin;
    y += gfx_.paragraph(g.description, {x, y, tw, 800}, 23, theme::text.with_alpha(0.9f), 14) + 20;
    if (!g.release_notes.empty())
    {
        gfx_.text("Versionshinweise", x, y, 25, theme::text, Weight::Bold);
        y += 44;
        y += gfx_.paragraph(g.release_notes, {x, y, tw, 1200}, 20, theme::dim, 24) + 20;
    }
    if (!g.screenshots.empty())
    {
        float sx = x;
        for (const std::string &shot : g.screenshots)
        {
            if (const Texture *s = images_->get(shot, 400, 225, 18))
                gfx_.image(*s, {sx, y, 400, 225});
            else
                gfx_.rounded({sx, y, 400, 225}, 18, theme::panel);
            sx += 420;
            if (sx + 400 > kWidth)
                break;
        }
    }
    // Resolved install data appears after an install starts; show what is verified now.
    gfx_.paragraph(g.signed_catalog ? "Der Download wird gegen den SHA-256 des signierten Katalogs geprüft."
                   : g.access == Access::Install ? "Der Download wird gegen den von GitHub veröffentlichten SHA-256 geprüft."
                                                 : "",
                   {110, offset + 580, 400, 80}, 17, theme::muted, 3);
}

// ---------------------------------------------------------------------------------------
// Diagnose (master prompt section 4, "Akeno Diagnostics")

namespace
{
const char *kDiagnoseActions[] = {"Netzwerk testen", "Bericht speichern", "Kompatibilität exportieren",
                                  "Kompatibilität importieren"};
}

void App::start_network_test()
{
    if (probing_.exchange(true))
        return;
    if (probe_thread_.joinable())
        probe_thread_.join();
    {
        std::lock_guard lock(probe_mutex_);
        probes_ = {{"ProsperoStore", "https://prosperostore.net/api/v1/manifest.json"},
                   {"GitHub API", "https://api.github.com/rate_limit"},
                   {"GitHub (Akeno-Katalog)", "https://raw.githubusercontent.com/YTDxDAkeno/Akeno-Store/main/README.md"},
                   {"RecompStore", "https://www.recompstore.com/catalog.json"},
                   {"itch.io", "https://itch.io/games/free.xml"},
                   {"Game Jolt", "https://gamejolt.com/site-api/web/discover/games?section=hot"},
                   {"GOG", "https://catalog.gog.com/v1/catalog?limit=1"},
                   {"Epic Games", "https://store-site-backend-static.ak.epicgames.com/freeGamesPromotions?locale=de"}};
        for (Probe &probe : probes_)
            probe.result = "wartet …";
    }
    probe_thread_ = std::thread([this] {
        std::size_t count = 0;
        {
            std::lock_guard lock(probe_mutex_);
            count = probes_.size();
        }
        for (std::size_t i = 0; i < count; ++i)
        {
            std::string url;
            {
                std::lock_guard lock(probe_mutex_);
                url = probes_[i].url;
                probes_[i].result = "läuft …";
            }
            const std::uint64_t started = platform::monotonic_ms();
            const net::Response response = http_->get(url, 32 * 1024 * 1024);
            const int ms = static_cast<int>(platform::monotonic_ms() - started);
            std::lock_guard lock(probe_mutex_);
            Probe &probe = probes_[i];
            probe.ms = ms;
            probe.ok = response.error.empty() && response.status >= 200 && response.status < 400;
            probe.result = !response.error.empty() ? response.error
                                                   : "HTTP " + std::to_string(response.status) + " · " +
                                                         format_bytes(response.bytes) + " · " + std::to_string(ms) + " ms";
            platform::log("[diagnose] %s: %s", probe.name.c_str(), probe.result.c_str());
        }
        probing_ = false;
    });
}

std::vector<std::string> App::diagnose_lines() const
{
    std::vector<std::string> lines;
    const auto yes = [](bool value) { return std::string(value ? "ja" : "nein"); };
    lines.push_back("# System");
    const std::string firmware = platform::firmware_version();
    lines.push_back(std::string("Plattform: ") + platform::name() +
                    (firmware.empty() ? "" : " · Systemsoftware " + firmware) + " · Akeno " AKENO_VERSION);
    lines.push_back(std::string("HTTPS: ") + http_->transport_name() + " · " + net::Client::user_agent());
    lines.push_back("Datenordner: " + platform::data_dir() + " (beschreibbar: " + yes(fs::writable(platform::data_dir())) +
                    ")");
    if (platform::is_console())
        lines.push_back("/data in der App eingebunden (ShadowMountPlus 1.7+): " + yes(fs::writable("/data")));
    lines.push_back("# Speicher");
    for (const auto &root : roots_)
    {
        const auto space = fs::free_space(root.path);
        lines.push_back(root.label + ": " + (root.writable ? "beschreibbar" : "nicht beschreibbar") +
                        (space ? " · " + format_bytes(*space) + " frei" : std::string()));
    }
    lines.push_back("# Akeno-Runtimes");
    for (const RuntimeInfo &info : akeno_runtimes())
        lines.push_back(std::string(info.state == RuntimeState::Integrated ? "Eingebaut: " : "Geplant: ") + info.name +
                        " – " + info.basis);
    for (const ComponentSpec &spec : component_specs())
    {
        const auto installed = installed_component(spec.id);
        lines.push_back(std::string("Komponente ") + spec.name + ": " +
                        (installed ? "installiert, Version " + installed->version + " in " + installed->path
                                   : std::string("nicht installiert (wird bei Bedarf automatisch geladen)")));
    }
    lines.push_back("Bibliothek: " + std::to_string(profiles_.size()) + " Spiele in " + library_root());
    lines.push_back(std::string("itch.io-Konto: ") + (itch_key_.empty() ? "nicht verbunden" : "verbunden"));
    lines.push_back("# Letzter Start über den ELF-Loader");
    const auto last = fs::read_file(fs::join(platform::data_dir(), "launch/last.log"), 16 * 1024);
    if (last && !last->empty())
        for (const std::string &line : split(*last, '\n'))
        {
            if (!line.empty())
                lines.push_back(sanitize_text(line, 200));
        }
    else
        lines.push_back("Noch kein Start protokolliert (launch/last.log)");
    lines.push_back("# Stores");
    for (const ProviderStatus &p : providers_)
    {
        const char *state = !p.enabled                                     ? "deaktiviert"
                            : p.state == ProviderStatus::State::Ready     ? "bereit"
                            : p.state == ProviderStatus::State::Offline   ? "offline (Cache)"
                            : p.state == ProviderStatus::State::Loading   ? "lädt"
                            : p.state == ProviderStatus::State::Failed    ? "Fehler"
                                                                           : "inaktiv";
        lines.push_back(p.label + ": " + state + " · " + std::to_string(p.count) + " Einträge" +
                        (p.message.empty() ? std::string() : " · " + p.message));
    }
    lines.push_back("# Bilder");
    {
        const ImageCache::Stats images = images_->stats();
        lines.push_back(std::to_string(images.loaded) + " geladen, " + std::to_string(images.failed) +
                        " Fehlversuche (jedes Bild wird bis zu dreimal versucht)" +
                        (images.last_error.empty() ? std::string() : " · zuletzt: " + images.last_error));
    }
    lines.push_back("# Netzwerktest");
    {
        std::lock_guard lock(const_cast<std::mutex &>(probe_mutex_));
        if (probes_.empty())
            lines.push_back("Noch nicht ausgeführt – „Netzwerk testen“ wählen");
        for (const Probe &probe : probes_)
            lines.push_back(std::string(probe.ok ? "OK  " : probe.ms < 0 ? "…   " : "FEHLER  ") + probe.name + ": " +
                            probe.result);
    }
    lines.push_back("# Kompatibilitätsberichte");
    lines.push_back(std::to_string(compat_->all().size()) + " eigene Berichte in " +
                    fs::join(platform::data_dir(), "compat.json"));
    lines.push_back("# Protokoll (neueste zuletzt)");
    for (const std::string &line : platform::recent_log(16))
        lines.push_back(sanitize_text(line, 220));
    return lines;
}

std::string App::diagnose_report() const
{
    std::string text = "Akeno Gaming Hub – Diagnosebericht\n";
    const std::time_t now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    text += std::string("Erstellt: ") + stamp + "\n";
    for (const std::string &line : diagnose_lines())
        text += (starts_with(line, "# ") ? "\n== " + line.substr(2) + " ==" : line) + "\n";
    text += "\n== Vollständiges Protokoll ==\n";
    for (const std::string &line : platform::recent_log(300))
        text += line + (ends_with(line, "\n") ? "" : "\n");
    return text;
}

void App::press_diagnose(Button button)
{
    const int actions = static_cast<int>(std::size(kDiagnoseActions));
    switch (button)
    {
    case Button::Left:
        diagnose_focus_ = std::max(0, diagnose_focus_ - 1);
        break;
    case Button::Right:
        diagnose_focus_ = std::min(actions - 1, diagnose_focus_ + 1);
        break;
    case Button::Up:
        if (diagnose_target_ <= 0)
            in_tabs_ = true;
        diagnose_target_ = std::max(0.0f, diagnose_target_ - 240);
        break;
    case Button::Down:
        diagnose_target_ = std::min(diagnose_target_ + 240, 4000.0f);
        break;
    case Button::Circle:
        in_tabs_ = true;
        break;
    case Button::Cross:
    {
        const std::string data = platform::data_dir();
        if (diagnose_focus_ == 0)
        {
            start_network_test();
            toast("Netzwerktest", "Die Stores werden nacheinander abgefragt");
        }
        else if (diagnose_focus_ == 1)
        {
            const std::string path = fs::join(data, "diagnose.txt");
            if (fs::write_atomic(path, diagnose_report()))
                toast("Bericht gespeichert", path + " – per FTP abholen und weitergeben");
            else
                toast("Bericht nicht gespeichert", path + " ist nicht beschreibbar", true);
        }
        else if (diagnose_focus_ == 2)
        {
            const std::string path = fs::join(data, "compat-export.json");
            if (fs::write_atomic(path, compat_->export_json()))
                toast("Exportiert", std::to_string(compat_->all().size()) + " Berichte nach " + path);
            else
                toast("Export fehlgeschlagen", path, true);
        }
        else
        {
            const std::string path = fs::join(data, "compat-import.json");
            const auto text = fs::read_file(path, 8 * 1024 * 1024);
            std::string error;
            const int merged = text ? compat_->import_json(*text, error) : -1;
            if (!text)
                toast("Keine Importdatei", "Lege die Datei als " + path + " ab", true);
            else if (merged < 0)
                toast("Import fehlgeschlagen", error, true);
            else
            {
                toast("Importiert", std::to_string(merged) + " neue oder neuere Berichte übernommen");
                rebuild_store();
            }
        }
        break;
    }
    default:
        break;
    }
}

void App::draw_diagnose()
{
    // Actions.
    float bx = kMargin;
    for (int i = 0; i < static_cast<int>(std::size(kDiagnoseActions)); ++i)
    {
        const float w = gfx_.measure(kDiagnoseActions[i], 22, Weight::Bold) + 64;
        draw_button(kDiagnoseActions[i], {bx, kTop + 6, w, 58}, !in_tabs_ && diagnose_focus_ == i, i == 0);
        bx += w + 18;
    }
    if (probing_)
        gfx_.text("Netzwerktest läuft …", bx + 12, kTop + 22, 20, theme::accent2, Weight::Bold);
    // Lines.
    const Rect clip{0, kTop + 84, kWidth, kHeight - kTop - 84 - 96};
    gfx_.set_clip(&clip);
    float y = kTop + 100 - diagnose_scroll_;
    const Rect panel{kMargin, y, kWidth - 2 * kMargin, 0};
    for (const std::string &line : diagnose_lines())
    {
        if (starts_with(line, "# "))
        {
            y += 14;
            if (y > clip.y - 50 && y < kHeight)
                gfx_.text(line.substr(2), panel.x, y, 25, theme::text, Weight::Bold);
            y += 42;
            continue;
        }
        if (y > clip.y - 40 && y < kHeight)
        {
            const Color color = starts_with(line, "FEHLER") ? theme::bad : starts_with(line, "OK  ") ? theme::ok : theme::dim;
            gfx_.text(line, panel.x + 24, y, 19, color, Weight::Regular, Align::Left, panel.w - 24);
        }
        y += 30;
    }
    gfx_.set_clip(nullptr);
}

void App::draw_keyboard()
{
    const Rect panel{300, 560, kWidth - 600, 440};
    gfx_.fill({0, 0, kWidth, kHeight}, {0, 0, 0, 90});
    gfx_.glow(panel, 30, theme::glow.with_alpha(0.25f), 24);
    gfx_.rounded(panel, 30, theme::panel_hi);
    gfx_.text(query_.empty() ? "Suchbegriff eingeben" : query_, panel.x + 40, panel.y + 26, 30,
              query_.empty() ? theme::muted : theme::text, Weight::Bold);
    const float kw = 96, kh = 66, gap = 12;
    for (int row = 0; row < 5; ++row)
    {
        std::vector<std::string> keys = row < 4 ? key_row(row) : std::vector<std::string>{"Leerzeichen", "Löschen", "Fertig"};
        const float widths = row < 4 ? kw : 300;
        const float total = keys.size() * widths + (keys.size() - 1) * gap;
        float x = panel.x + (panel.w - total) / 2;
        const float y = panel.y + 96 + row * (kh + gap);
        for (std::size_t i = 0; i < keys.size(); ++i, x += widths + gap)
        {
            const bool focused = row == key_row_ && static_cast<int>(i) == key_col_;
            const Rect key{x, y, widths, kh};
            if (focused)
                gfx_.glow(key, 16, theme::glow.with_alpha(0.4f), 10);
            gfx_.rounded(key, 16, focused ? theme::accent : Color{255, 255, 255, 22});
            gfx_.text(keys[i], key.x + key.w / 2, key.y + 18, 26, theme::text, Weight::Bold, Align::Center);
        }
    }
}

void App::draw_qr()
{
    gfx_.fill({0, 0, kWidth, kHeight}, {0, 0, 0, 170});
    const Rect panel{kWidth / 2 - 380, 160, 760, 760};
    gfx_.rounded(panel, 32, theme::panel_hi);
    gfx_.text(qr_title_, panel.x + panel.w / 2, panel.y + 34, 30, theme::text, Weight::Bold, Align::Center, panel.w - 60);
    if (qr_texture_)
        gfx_.image(qr_texture_, {panel.x + (panel.w - 460) / 2, panel.y + 100, 460, 460});
    gfx_.text("Mit dem Handy scannen", panel.x + panel.w / 2, panel.y + 590, 24, theme::dim, Weight::Bold, Align::Center);
    gfx_.paragraph(qr_url_, {panel.x + 50, panel.y + 636, panel.w - 100, 90}, 19, theme::muted, 3);
}

void App::draw_menu()
{
    gfx_.fill({0, 0, kWidth, kHeight}, {0, 0, 0, 150});
    const float h = 170 + menu_.size() * 76.0f;
    const Rect panel{kWidth / 2 - 450, (kHeight - h) / 2, 900, h};
    gfx_.glow(panel, 30, theme::glow.with_alpha(0.2f), 24);
    gfx_.rounded(panel, 30, theme::panel_hi);
    gfx_.text(menu_title_, panel.x + 44, panel.y + 30, 32, theme::text, Weight::Bold, Align::Left, panel.w - 88);
    const float text_h = gfx_.paragraph(menu_text_, {panel.x + 44, panel.y + 82, panel.w - 88, 90}, 19, theme::dim, 4);
    (void)text_h;
    float y = panel.y + panel.h - menu_.size() * 76.0f - 24;
    for (std::size_t i = 0; i < menu_.size(); ++i, y += 76)
        draw_button(menu_[i].label, {panel.x + 44, y, panel.w - 88, 62}, static_cast<int>(i) == menu_focus_, false);
}

void App::draw_toasts()
{
    float y = kHeight - 140;
    for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it)
    {
        const float appear = std::min(1.0f, it->age * 5), leave = std::clamp(5 - it->age, 0.0f, 1.0f);
        const float a = std::min(appear, leave);
        const float w = 560, h = 92;
        const float x = kWidth - kMargin - w + (1 - ease_out_back(appear)) * 80;
        y -= h * a + 12;
        const Rect r{x, y, w, h};
        gfx_.glow(r, 22, (it->error ? theme::bad : theme::glow).with_alpha(0.25f * a), 14);
        gfx_.rounded(r, 22, theme::panel_hi.with_alpha(a));
        gfx_.rounded({r.x, r.y + 18, 6, r.h - 36}, 3, (it->error ? theme::bad : theme::ok).with_alpha(a));
        gfx_.text(it->title, r.x + 28, r.y + 14, 22, theme::text.with_alpha(a), Weight::Bold, Align::Left, w - 56);
        gfx_.text(it->text, r.x + 28, r.y + 50, 18, theme::dim.with_alpha(a), Weight::Regular, Align::Left, w - 56);
    }
}

void App::draw()
{
    gfx_.begin_frame();
    gfx_.background(settings_.reduce_motion || platform::is_console() ? -1 : time_);
    const float shift = (1 - fade_) * 24;
    (void)shift;
    switch (tab_)
    {
    case Tab::Discover:
        draw_discover();
        break;
    case Tab::Store:
        draw_store();
        break;
    case Tab::Library:
        draw_library();
        break;
    case Tab::Runtimes:
        draw_runtimes();
        break;
    case Tab::Downloads:
        draw_downloads();
        break;
    case Tab::Diagnose:
        draw_diagnose();
        break;
    case Tab::Settings:
        draw_settings();
        break;
    case Tab::Count:
        break;
    }
    if (fade_ < 1)
        gfx_.fill({0, kTop - 10, kWidth, kHeight}, {7, 10, 26, static_cast<std::uint8_t>(255 * (1 - fade_))});
    draw_top_bar();
    if (detail_ >= 0 && detail_ < static_cast<int>(games_.size()))
        draw_detail();
    if (keyboard_)
        draw_keyboard();
    if (!qr_url_.empty())
        draw_qr();
    if (!menu_.empty())
        draw_menu();
    draw_hints();
    draw_toasts();
}

} // namespace akeno::ui
