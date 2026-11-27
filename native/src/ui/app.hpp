// Akeno Gaming Hub - the hub's screens and their input handling.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "install/installer.hpp"
#include "install/library.hpp"
#include "platform/platform.hpp"
#include "runtime/runtimes.hpp"
#include "stores/catalog.hpp"
#include "ui/gfx.hpp"
#include "ui/images.hpp"

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace akeno::ui
{

enum class Button
{
    Up,
    Down,
    Left,
    Right,
    Cross,    // confirm
    Circle,   // back
    Square,   // secondary action
    Triangle, // search
    L1,
    R1,
    L2,
    R2,
    Options,
};

struct Settings
{
    int root = 0;
    bool adult = false;
    bool reduce_motion = false;
    std::vector<std::string> disabled_providers;
    std::vector<std::string> github_repositories;
    bool load(const std::string &path);
    bool save(const std::string &path) const;
};

class App
{
  public:
    App(Gfx &gfx, std::string asset_dir);
    ~App();

    void press(Button button);
    void update(float dt);
    void draw();
    bool quit_requested() const { return quit_; }
    bool controller_connected = false;

    // Drives a scripted walk through every screen (host screenshots and smoke tests).
    void open_tab_for_test(int tab);
    void open_detail_for_test(const std::string &title_id_or_id);
    bool catalog_ready() const;

  private:
    enum class Tab
    {
        Discover,
        Store,
        Library,
        Runtimes,
        Downloads,
        Settings,
        Count
    };
    struct Shelf
    {
        std::string title, subtitle;
        std::vector<int> items; // indices into games_
        float scroll = 0;       // animated x offset
        int focus = 0;
    };
    struct Toast
    {
        std::string title, text;
        float age = 0;
        bool error = false;
    };
    struct StoreFilter
    {
        std::string label;
        std::string key; // all, ps5, recomp, emulator, browser, pc-free, vn, provider:<id>
    };
    struct Action
    {
        std::string label;
        int kind; // see app.cpp
    };

    // data
    void sync_data();
    void rebuild_shelves();
    void rebuild_store();
    std::vector<int> filter_games(const std::string &key, const std::string &query) const;
    const InstalledTitle *installed_for(const Game &game) const;
    bool update_available(const Game &game, const InstalledTitle &title) const;
    bool runtime_installed(const std::string &family, const RuntimeInfo **which) const;
    int game_index_by_title(const std::string &title_id) const;

    // actions
    std::vector<Action> actions_for(const Game &game) const;
    void run_action(const Game &game, int kind);
    void launch(const std::string &title_id, const std::vector<std::string> &arguments, const std::string &name);
    void install(const Game &game);
    void toast(const std::string &title, const std::string &text, bool error = false);
    void open_detail(int index);
    void open_qr(const std::string &title, const std::string &url);

    // input per screen
    void press_tabs(Button button);
    void press_discover(Button button);
    void press_store(Button button);
    void press_library(Button button);
    void press_runtimes(Button button);
    void press_downloads(Button button);
    void press_settings(Button button);
    void press_detail(Button button);
    void press_keyboard(Button button);

    // drawing
    void draw_top_bar();
    void draw_hints();
    void draw_discover();
    void draw_store();
    void draw_library();
    void draw_runtimes();
    void draw_downloads();
    void draw_settings();
    void draw_detail();
    void draw_keyboard();
    void draw_qr();
    void draw_toasts();
    void draw_tile(const Game &game, float x, float y, float size, bool focused, float focus_amount);
    void draw_badge(const std::string &text, float x, float y, Color fill, Color ink);
    float draw_glyph(Button button, float x, float y, float size);
    void draw_button(const std::string &label, const Rect &rect, bool focused, bool primary);
    void draw_progress(const Rect &rect, float fraction, Color color);
    std::string artwork(const Game &game) const;

    Gfx &gfx_;
    std::string asset_dir_;
    std::string settings_path_;
    Settings settings_;
    std::unique_ptr<net::Client> http_;
    std::unique_ptr<Catalog> catalog_;
    std::unique_ptr<Library> library_;
    std::unique_ptr<Installer> installer_;
    std::unique_ptr<ImageCache> images_;
    std::vector<platform::InstallRoot> roots_;

    std::vector<Game> games_;
    std::vector<InstalledTitle> installed_;
    std::vector<LocalGame> local_;
    std::vector<ProviderStatus> providers_;
    std::uint64_t catalog_generation_ = ~0ull, library_generation_ = ~0ull;
    float library_poll_ = 0;

    Tab tab_ = Tab::Discover;
    float tab_line_x_ = 0, tab_line_w_ = 0;
    float time_ = 0;
    float fade_ = 1;
    bool quit_ = false;
    bool in_tabs_ = false; // focus on the top bar

    // discover
    std::vector<int> featured_;
    int hero_ = 0;
    float hero_timer_ = 0, hero_fade_ = 1;
    int hero_previous_ = -1;
    std::vector<Shelf> shelves_;
    int row_ = 0; // 0 = hero, 1.. = shelves
    float discover_scroll_ = 0;
    int hero_button_ = 0;

    // store
    std::vector<StoreFilter> filters_;
    int filter_ = 0;
    bool filter_focus_ = true;
    std::vector<int> store_items_;
    int store_focus_ = 0;
    float store_scroll_ = 0;
    std::string query_;

    // library, runtimes, downloads, settings
    int library_focus_ = 0;
    float library_scroll_ = 0;
    int runtime_focus_ = 0;
    float runtime_scroll_ = 0;
    int download_focus_ = 0;
    int settings_focus_ = 0;
    float settings_scroll_ = 0;

    // overlays
    int detail_ = -1;
    float detail_anim_ = 0;
    int detail_button_ = 0;
    float detail_scroll_ = 0;
    bool keyboard_ = false;
    int key_row_ = 0, key_col_ = 0;
    std::string qr_title_, qr_url_;
    Texture qr_texture_;
    struct MenuItem
    {
        std::string label;
        std::function<void()> run;
    };
    std::string menu_title_, menu_text_;
    std::vector<MenuItem> menu_;
    int menu_focus_ = 0;
    void open_menu(const std::string &title, const std::string &text, std::vector<MenuItem> items);
    void draw_menu();
    void press_menu(Button button);
    void title_menu(const InstalledTitle &title);
    void local_menu(const LocalGame &game);
    std::deque<Toast> toasts_;
    float focus_pop_ = 0; // grows to 1 after focus changes
};

} // namespace akeno::ui
