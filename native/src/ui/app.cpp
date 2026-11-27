// Akeno Gaming Hub - the hub's screens and their input handling.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/app.hpp"

#include "core/fs.hpp"
#include "core/json.hpp"
#include "core/util.hpp"
#include "platform/platform.hpp"
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
    kStart,
    kUninstall,
    kPlayBrowser,
    kStorePage,
    kInstallRuntime,
    kOpenRuntime,
};

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
    static const char *labels[] = {"Entdecken", "Store", "Bibliothek", "Runtimes", "Downloads", "Einstellungen"};
    return labels[tab];
}

std::string initials(const std::string &title)
{
    std::string out;
    for (const std::string &word : split(title, ' '))
        if (!word.empty() && out.size() < 2 && std::isalnum(static_cast<unsigned char>(word[0])))
            out += static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
    return out.empty() ? "?" : out;
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
    return theme::accent2;
}

std::string family_label(const std::string &family)
{
    static const std::map<std::string, std::string> labels = {
        {"web", "Web / HTML5"},      {"windows", "Windows"},       {"retro", "Retro-Konsolen"},
        {"arcade", "Arcade"},        {"pce", "PC Engine"},         {"atari", "Atari"},
        {"n64", "Nintendo 64"},      {"psx", "PlayStation 1"},     {"ps2", "PlayStation 2"},
        {"psp", "PSP"},              {"gamecube", "GameCube/Wii"}, {"dreamcast", "Dreamcast"},
        {"saturn", "Sega Saturn"},   {"xbox", "Xbox"},             {"xbox360", "Xbox 360"},
        {"switch", "Switch"},        {"nds", "DS/3DS/Wii U"},      {"flash", "Flash"},
        {"j2me", "Java ME"},         {"pico8", "PICO-8"},          {"c64", "Commodore 64"}};
    const auto it = labels.find(family);
    return it == labels.end() ? family : it->second;
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

App::App(Gfx &gfx, std::string asset_dir) : gfx_(gfx), asset_dir_(std::move(asset_dir))
{
    settings_path_ = fs::join(platform::data_dir(), "settings.json");
    settings_.load(settings_path_);
    http_ = std::make_unique<net::Client>(net::make_platform_transport());

    ProviderContext context;
    context.http = http_.get();
    context.cache_dir = platform::cache_dir();
    context.state_dir = platform::data_dir();
    context.allow_adult = settings_.adult;
    fs::make_dirs(context.cache_dir);
    std::vector<std::unique_ptr<Provider>> providers;
    providers.push_back(make_prosperostore_provider());
    providers.push_back(make_recompstore_provider());
    providers.push_back(make_itch_provider());
    providers.push_back(make_gamejolt_provider());
    providers.push_back(make_gog_provider());
    providers.push_back(make_epic_provider());
    if (!settings_.github_repositories.empty())
        providers.push_back(make_github_provider(settings_.github_repositories));
    catalog_ = std::make_unique<Catalog>(context, std::move(providers));
    for (const std::string &id : settings_.disabled_providers)
        catalog_->set_enabled(id, false);

    const std::string receipts = fs::join(platform::data_dir(), "receipts");
    library_ = std::make_unique<Library>(receipts);
    installer_ = std::make_unique<Installer>(*http_, *library_, receipts, [this](Game &game, std::string &error) {
        return catalog_->resolve(game, error);
    });
    images_ = std::make_unique<ImageCache>(gfx_, *http_, fs::join(platform::cache_dir(), "images"));
    roots_ = platform::install_roots();
    settings_.root = std::clamp(settings_.root, 0, static_cast<int>(roots_.size()) - 1);

    filters_ = {{"Alle", "all"},
                {"PS5-Titel", "ps5"},
                {"Recomps & Ports", "recomp"},
                {"Emulatoren", "emulator"},
                {"Im Browser spielbar", "browser"},
                {"Kostenlos für PC", "pc-free"},
                {"Visual Novels", "vn"},
                {"Demnächst", "upcoming"},
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
        local_ = scan_local_games(platform::import_dirs());
        rebuild_shelves();
    }
}

int App::game_index_by_title(const std::string &title_id) const
{
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

bool App::update_available(const Game &game, const InstalledTitle &title) const
{
    bool comparable = false;
    return compare_content_version(game.content_version, title.content_version, &comparable) > 0 && comparable;
}

bool App::runtime_installed(const std::string &family, const RuntimeInfo **which) const
{
    for (const RuntimeInfo *info : runtimes_for(family))
        for (const InstalledTitle &title : installed_)
            if (title.title_id == info->title_id)
            {
                if (which)
                    *which = info;
                return true;
            }
    return false;
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
        else if (key == "ps5")
            keep = g.access == Access::Install;
        else if (key == "recomp")
            keep = g.has_tag("recomp") || g.has_tag("port");
        else if (key == "emulator")
            keep = g.has_tag("emulator");
        else if (key == "browser")
            keep = g.access == Access::Runtime;
        else if (key == "pc-free")
            keep = g.has_tag("pc") && g.free;
        else if (key == "vn")
            keep = g.has_tag("visual-novel");
        else if (key == "upcoming")
            keep = g.access == Access::Upcoming;
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
    add("Neu & aktualisiert", "Native PS5-Titel aus dem signierten ProsperoStore-Katalog", take(newest, 24));
    add("Spiele für die PS5", "Ports, Homebrew und Fanprojekte zum direkten Installieren", take(games_only, 24));
    add("Recomps & Ports", "Mit eigenem, rechtmäßig erworbenem Original", take(filter_games("recomp", ""), 24));
    add("Emulatoren & Runtimes", "Gemeinsame Laufzeiten für ganze Plattformen", take(filter_games("emulator", ""), 24));
    add("Im Browser spielbar", "itch.io und Game Jolt – startet in ProsperoWeb", take(filter_games("browser", ""), 24));
    add("Visual Novels", "Erzählspiele von itch.io", take(filter_games("vn", ""), 24));
    add("Kostenlos für PC", "GOG und Epic Games – über die Windows-Runtimes", take(filter_games("pc-free", ""), 24));
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
    const InstalledTitle *installed = installed_for(game);
    switch (game.access)
    {
    case Access::Install:
        if (installed)
        {
            actions.push_back({"Starten", kStart});
            if (update_available(game, *installed))
                actions.push_back({"Aktualisieren", kInstall});
            else
                actions.push_back({"Neu installieren", kInstall});
            actions.push_back({"Deinstallieren", kUninstall});
        }
        else
            actions.push_back({"Installieren", kInstall});
        break;
    case Access::Runtime:
    {
        const RuntimeInfo *web = nullptr;
        if (runtime_installed(game.runtime, &web))
            actions.push_back({"In ProsperoWeb spielen", kPlayBrowser});
        else
            actions.push_back({"Web-Runtime installieren", kInstallRuntime});
        break;
    }
    case Access::External:
    case Access::Upcoming:
        break;
    }
    if (!game.page_url.empty())
        actions.push_back({"Store-Seite (QR)", kStorePage});
    if (game.access == Access::External && game.runtime == "windows")
    {
        if (runtime_installed("windows", nullptr))
            actions.push_back({"Windows-Runtime öffnen", kOpenRuntime});
        else
            actions.push_back({"Windows-Runtime installieren", kInstallRuntime});
    }
    return actions;
}

void App::install(const Game &game)
{
    if (roots_.empty() || !roots_[static_cast<std::size_t>(settings_.root)].writable)
    {
        toast("Kein Schreibzugriff",
              "Der Installationsort ist nicht beschreibbar. Auf der PS5: ShadowMountPlus 1.7+ einbinden.", true);
        return;
    }
    installer_->install(game, roots_[static_cast<std::size_t>(settings_.root)].path);
    toast("Zur Warteschlange hinzugefügt", game.title);
}

void App::launch(const std::string &title_id, const std::vector<std::string> &arguments, const std::string &name)
{
    std::string error;
    if (!request_launch(title_id, arguments, error))
    {
        toast("Start nicht möglich", error, true);
        return;
    }
    toast("Wird gestartet", name + " – Akeno schließt sich");
    quit_ = true; // the payload starts the title once Akeno has closed
}

void App::run_action(const Game &game, int kind)
{
    switch (kind)
    {
    case kInstall:
        install(game);
        break;
    case kStart:
        launch(game.title_id, {}, game.title);
        break;
    case kUninstall:
        if (const InstalledTitle *title = installed_for(game))
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
    case kPlayBrowser:
    {
        const RuntimeInfo *web = nullptr;
        runtime_installed("web", &web);
        if (!web)
            break;
        const std::string url = game.launch_url, name = game.title, id = web->title_id;
        open_menu("Im Browser spielen",
                  "ProsperoWeb übernimmt keine Startadresse. Öffne dort " + url +
                      " – oder scanne den QR-Code und sende die Adresse an dich.",
                  {{"ProsperoWeb starten", [this, id, name] { launch(id, {}, "ProsperoWeb (" + name + ")"); }},
                   {"QR-Code anzeigen", [this, name, url] { open_qr(name, url); }},
                   {"Abbrechen", [] {}}});
        break;
    }
    case kStorePage:
        open_qr(game.title, game.page_url);
        break;
    case kInstallRuntime:
    {
        const std::string family = game.access == Access::Runtime ? game.runtime : "windows";
        for (const RuntimeInfo *info : runtimes_for(family))
        {
            const int index = game_index_by_title(info->title_id);
            if (index >= 0)
            {
                install(games_[static_cast<std::size_t>(index)]);
                return;
            }
        }
        toast("Runtime nicht im Katalog", family_label(family), true);
        break;
    }
    case kOpenRuntime:
    {
        const RuntimeInfo *info = nullptr;
        if (runtime_installed("windows", &info))
            launch(info->title_id, {}, info->name);
        break;
    }
    }
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
    items.push_back({"Starten", [this, copy] { launch(copy.title_id, {}, copy.name); }});
    const int index = game_index_by_title(title.title_id);
    if (index >= 0)
    {
        const Game game = games_[static_cast<std::size_t>(index)];
        if (update_available(game, title))
            items.push_back({"Aktualisieren auf " + game.content_version, [this, game] { install(game); }});
        items.push_back({"Details im Store", [this, index] { open_detail(index); }});
    }
    if (title.shortcut || title.managed || index >= 0)
        items.push_back({"Deinstallieren", [this, copy] {
                             installer_->uninstall(copy);
                             toast("Wird deinstalliert", copy.name);
                         }});
    items.push_back({"Abbrechen", [] {}});
    std::string text = title.title_id + " · Version " + (title.content_version.empty() ? "?" : title.content_version) +
                       "\n" + title.folder + (title.managed ? "\nVon Akeno installiert" : "\nManuell installiert");
    open_menu(title.name, text, std::move(items));
}

void App::local_menu(const LocalGame &game)
{
    std::vector<MenuItem> items;
    const Detection &d = game.detection;
    std::string text = d.format + (d.engine.empty() ? "" : " · " + d.engine) + "\nErkannt an: " + d.evidence;
    const RuntimeInfo *runtime = nullptr;
    if (!d.family.empty() && runtime_installed(d.family, &runtime))
    {
        std::vector<std::string> args;
        if (runtime->args == LaunchArgs::MihawkPath)
        {
            std::string windows = "Z:" + game.path;
            std::replace(windows.begin(), windows.end(), '/', '\\');
            args.push_back("path=" + windows);
        }
        const std::string id = runtime->title_id, name = runtime->name;
        text += "\nRuntime: " + name + (args.empty() ? " (öffnet ihre eigene Bibliothek)" : " (Start mit path=…)");
        items.push_back({"Mit " + name + " starten", [this, id, args, name] { launch(id, args, name); }});
        const std::string label = game.name;
        items.push_back({"Verknüpfung im Hauptmenü", [this, id, args, label] {
                             ShortcutSpec spec;
                             spec.name = label;
                             spec.target = id;
                             spec.arguments = args;
                             spec.icon.width = spec.icon.height = 2;
                             spec.icon.rgba.assign(4, 0xFFF65C8Bu);
                             std::string error;
                             const std::string tile = create_shortcut(
                                 spec, roots_[static_cast<std::size_t>(settings_.root)].path,
                                 fs::join(platform::data_dir(), "receipts"), error);
                             if (tile.empty())
                                 toast("Verknüpfung fehlgeschlagen", error, true);
                             else
                                 toast("Verknüpfung erstellt", label + " erscheint als " + tile + " im Hauptmenü");
                             library_poll_ = 0;
                         }});
    }
    else if (!d.family.empty())
    {
        for (const RuntimeInfo *info : runtimes_for(d.family))
        {
            const int index = game_index_by_title(info->title_id);
            if (index >= 0)
            {
                const Game runtime_game = games_[static_cast<std::size_t>(index)];
                items.push_back({std::string(info->name) + " installieren", [this, runtime_game] { install(runtime_game); }});
                break;
            }
        }
        text += "\nBenötigt die Runtime für " + family_label(d.family);
    }
    else if (!d.advice.empty())
        text += "\n" + d.advice;
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
    const int titles = static_cast<int>(installed_.size());
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
    case Button::Square:
        if (f < titles)
            title_menu(installed_[static_cast<std::size_t>(f)]);
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
    const int count = static_cast<int>(runtime_registry().size());
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
        const RuntimeInfo &info = runtime_registry()[static_cast<std::size_t>(runtime_focus_)];
        for (const InstalledTitle &title : installed_)
            if (title.title_id == info.title_id)
                return title_menu(title);
        const int index = game_index_by_title(info.title_id);
        if (index >= 0)
            open_detail(index);
        else
            toast("Nicht im Katalog", std::string(info.name) + " ist derzeit nicht gelistet", true);
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
    const int count = 6 + provider_rows;
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
    else
    {
        ProviderStatus &p = providers_[static_cast<std::size_t>(settings_focus_ - 6)];
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
    const int titles = static_cast<int>(installed_.size());
    const float library_target =
        library_focus_ < titles ? std::max(0.0f, (library_focus_ / kLibraryColumns - 1) * 270.0f)
                                : std::max(0.0f, ((titles + kLibraryColumns - 1) / kLibraryColumns) * 270.0f - 200 +
                                                     (library_focus_ - titles) * 96.0f - 300);
    library_scroll_ = approach(library_scroll_, library_target, dt, 12);
    runtime_scroll_ = approach(runtime_scroll_, std::max(0.0f, runtime_focus_ * 104.0f - 520), dt, 12);
    settings_scroll_ = approach(settings_scroll_, std::max(0.0f, settings_focus_ * 92.0f - 540), dt, 12);
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
            if (job.state == JobState::Done)
                toast(job.kind == JobKind::Install ? "Installiert" : "Entfernt",
                      job.title + (job.kind == JobKind::Install ? " – ShadowMountPlus registriert den Titel gleich" : ""));
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
        gfx_.gradient_h({rect.x + rect.h / 2, rect.y, rect.w - rect.h, rect.h},
                        theme::accent.with_alpha(0), theme::accent2.with_alpha(focused ? 0.65f : 0.35f));
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
    const Texture *art = images_->get(artwork(game), static_cast<int>(size), static_cast<int>(size), 24);
    if (art)
        gfx_.image(*art, r);
    else
    {
        const Color base = provider_color(game.provider);
        gfx_.rounded(r, 24, mix(theme::panel_hi, base, 0.35f));
        gfx_.gradient_v({r.x + 24, r.y + r.h * 0.5f, r.w - 48, r.h * 0.5f - 24}, {0, 0, 0, 0}, {0, 0, 0, 60});
        gfx_.text(initials(game.title), r.x + r.w / 2, r.y + r.h / 2 - 34, 52, theme::text.with_alpha(0.9f),
                  Weight::Bold, Align::Center);
    }
    // Status badge.
    const InstalledTitle *installed = installed_for(game);
    std::string badge;
    Color fill{0, 0, 0, 170}, ink = theme::text;
    if (installed && update_available(game, *installed))
        badge = "Update", fill = theme::warn, ink = {40, 25, 0};
    else if (installed)
        badge = "Installiert", fill = theme::ok, ink = {0, 40, 25};
    else if (game.access == Access::Runtime)
        badge = "Browser";
    else if (game.access == Access::Upcoming)
        badge = "Demnächst";
    else if (game.access == Access::External)
        badge = game.has_tag("giveaway") ? "Gratis" : "PC";
    if (!badge.empty())
        draw_badge(badge, r.x + 12, r.y + 12, fill, ink);
    if (focused)
        gfx_.rounded_outline(r, 24, 3, {255, 255, 255, static_cast<std::uint8_t>(200 * std::min(1.0f, pop * 2))});
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
        const float w = gfx_.measure(tab_label(i), 25, Weight::Bold);
        if (active)
        {
            active_x = x;
            active_w = w;
            if (in_tabs_)
                gfx_.glow({x - 18, 40, w + 36, 48}, 24, theme::glow.with_alpha(0.25f), 10);
        }
        gfx_.text(tab_label(i), x, 50, 25, active ? theme::text : theme::muted, Weight::Bold);
        x += w + 54;
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
    gfx_.text(clock, kWidth - kMargin, 44, 30, theme::text, Weight::Bold, Align::Right);
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
    gfx_.text(state, kWidth - kMargin - 112, 52, 19, theme::dim, Weight::Regular, Align::Right);
    if (catalog_->refreshing())
    {
        const float a = time_ * 6;
        for (int i = 0; i < 8; ++i)
        {
            const float t = a + i * 0.785f;
            gfx_.circle(kWidth - kMargin - 112 - gfx_.measure(state, 19) - 26 + 9 * std::cos(t), 64 + 9 * std::sin(t),
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
    // Filters.
    float y = kTop + 8;
    for (std::size_t i = 0; i < filters_.size(); ++i, y += 56)
    {
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
    gfx_.text("Installierte Titel", kMargin, y, 32, theme::text, Weight::Bold);
    const std::string where = roots_.empty() ? "" : roots_[static_cast<std::size_t>(settings_.root)].path;
    gfx_.text(std::to_string(installed_.size()) + " Titel in den ShadowMountPlus-Ordnern", kMargin + 330, y + 10, 20,
              theme::muted);
    y += 64;
    const int titles = static_cast<int>(installed_.size());
    if (titles == 0)
    {
        gfx_.rounded({kMargin, y, kWidth - 2 * kMargin, 120}, 22, theme::panel);
        gfx_.text("Noch keine Titel gefunden. Installiere etwas im Store – Akeno legt Titel in " + where + " ab.",
                  kMargin + 32, y + 46, 23, theme::dim);
        y += 150;
    }
    for (int i = 0; i < titles; ++i)
    {
        const InstalledTitle &t = installed_[static_cast<std::size_t>(i)];
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
        const int index = game_index_by_title(t.title_id);
        const std::string art = !t.icon_path.empty() ? t.icon_path
                                : index >= 0        ? artwork(games_[static_cast<std::size_t>(index)])
                                                    : "";
        if (const Texture *tex = images_->get(art, static_cast<int>(kLibraryTile), static_cast<int>(kLibraryTile), 22))
            gfx_.image(*tex, r);
        else
        {
            gfx_.rounded(r, 22, theme::panel_hi);
            gfx_.text(initials(t.name), r.x + r.w / 2, r.y + r.h / 2 - 28, 44, theme::text, Weight::Bold, Align::Center);
        }
        if (index >= 0 && update_available(games_[static_cast<std::size_t>(index)], t))
            draw_badge("Update", r.x + 10, r.y + 10, theme::warn, {40, 25, 0});
        else if (t.shortcut)
            draw_badge("Verknüpfung", r.x + 10, r.y + 10, {0, 0, 0, 170}, theme::text);
        if (focused)
            gfx_.rounded_outline(r, 22, 3, {255, 255, 255, 200});
        gfx_.text(t.name, x + 2, ty + kLibraryTile + 14, 20, focused ? theme::text : theme::dim, Weight::Bold,
                  Align::Left, kLibraryTile);
        gfx_.text(t.title_id + " · " + (t.content_version.empty() ? "?" : t.content_version), x + 2,
                  ty + kLibraryTile + 40, 16, theme::muted, Weight::Regular, Align::Left, kLibraryTile);
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
        const RuntimeInfo *rt = nullptr;
        std::string status;
        Color color = theme::muted;
        if (!g.detection.family.empty() && runtime_installed(g.detection.family, &rt))
            status = std::string("Startbereit mit ") + rt->name, color = theme::ok;
        else if (!g.detection.family.empty())
            status = "Runtime fehlt: " + family_label(g.detection.family), color = theme::warn;
        else
            status = "Keine PS5-Runtime", color = theme::bad;
        gfx_.text(status, r.x + r.w - 28, r.y + 30, 20, color, Weight::Bold, Align::Right);
    }
}

void App::draw_runtimes()
{
    float y = kTop + 6 - runtime_scroll_;
    gfx_.text("Universelle Runtimes", kMargin, y, 32, theme::text, Weight::Bold);
    gfx_.paragraph("Akeno portiert nicht jedes Spiel einzeln. Jede Runtime ist ein vorhandener PS5-Titel aus dem "
                   "signierten Katalog; Akeno erkennt Formate, installiert die passende Runtime und startet sie.",
                   {kMargin, y + 50, 1500, 70}, 21, theme::dim, 2);
    y += 130;
    const auto &registry = runtime_registry();
    for (std::size_t i = 0; i < registry.size(); ++i)
    {
        const RuntimeInfo &info = registry[i];
        const Rect r{kMargin, y + i * 104.0f, kWidth - 2 * kMargin, 92};
        if (r.y > kHeight || r.y + r.h < kTop)
            continue;
        const bool focused = !in_tabs_ && runtime_focus_ == static_cast<int>(i);
        if (focused)
            gfx_.glow(r, 22, theme::glow.with_alpha(0.3f), 12);
        gfx_.rounded(r, 22, focused ? theme::panel_hi : theme::panel);
        const int index = game_index_by_title(info.title_id);
        if (index >= 0)
            if (const Texture *t = images_->get(artwork(games_[static_cast<std::size_t>(index)]), 64, 64, 14))
                gfx_.image(*t, {r.x + 16, r.y + 14, 64, 64});
        gfx_.text(info.name, r.x + 100, r.y + 14, 24, theme::text, Weight::Bold);
        gfx_.text(family_label(info.key) + "  ·  " + info.summary, r.x + 100, r.y + 50, 18, theme::dim, Weight::Regular,
                  Align::Left, 1150);
        bool installed = false;
        for (const InstalledTitle &t : installed_)
            installed |= t.title_id == info.title_id;
        const std::string status = installed ? "Installiert" : index >= 0 ? "Im Katalog" : "Nicht gelistet";
        const Color color = installed ? theme::ok : index >= 0 ? theme::accent2 : theme::muted;
        draw_badge(status, r.x + r.w - 40 - gfx_.measure(status, 17, Weight::Bold) - 22, r.y + 30, color.with_alpha(0.9f),
                   {8, 8, 20});
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
        {"Version", "00.002.000 (Alpha)"},
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
        gfx_.text(initials(g.title), cover.x + 200, cover.y + 150, 96, theme::text, Weight::Bold, Align::Center);
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
    facts.push_back({"Runtime", g.runtime == "native" ? "Nativer PS5-Titel"
                                : g.runtime == "web"  ? "Web (ProsperoWeb)"
                                : g.runtime == "windows" ? "Windows (MihawkOS/PC5)"
                                                         : "PC"});
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
