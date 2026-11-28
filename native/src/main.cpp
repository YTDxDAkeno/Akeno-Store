// Akeno Gaming Hub - entry point: SDL2 window, input mapping, frame loop.
// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "core/fs.hpp"
#include "platform/platform.hpp"
#include "play/play.hpp"
#include "ui/app.hpp"
#include "ui/gfx.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using namespace akeno;

namespace
{

std::string asset_dir()
{
#ifdef AKENO_HOST
    if (const char *forced = std::getenv("AKENO_ASSETS"); forced && *forced)
        return forced;
    return AKENO_ASSET_DIR;
#else
    return "/app0/assets";
#endif
}

struct Repeat
{
    bool held = false;
    ui::Button button = ui::Button::Up;
    std::uint64_t next = 0;
};

bool direction(ui::Button b)
{
    return b == ui::Button::Up || b == ui::Button::Down || b == ui::Button::Left || b == ui::Button::Right;
}

bool map_key(SDL_Keycode key, Uint16 mod, ui::Button &out)
{
    switch (key)
    {
    case SDLK_UP: out = ui::Button::Up; return true;
    case SDLK_DOWN: out = ui::Button::Down; return true;
    case SDLK_LEFT: out = ui::Button::Left; return true;
    case SDLK_RIGHT: out = ui::Button::Right; return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: out = ui::Button::Cross; return true;
    case SDLK_ESCAPE:
    case SDLK_BACKSPACE: out = ui::Button::Circle; return true;
    case SDLK_SPACE:
    case SDLK_DELETE: out = ui::Button::Square; return true;
    case SDLK_SLASH:
    case SDLK_F3: out = ui::Button::Triangle; return true;
    case SDLK_TAB: out = (mod & KMOD_SHIFT) ? ui::Button::L1 : ui::Button::R1; return true;
    case SDLK_PAGEUP:
    case SDLK_q: out = ui::Button::L1; return true;
    case SDLK_PAGEDOWN:
    case SDLK_e: out = ui::Button::R1; return true;
    case SDLK_F10:
    case SDLK_MENU: out = ui::Button::Options; return true;
    default: return false;
    }
}

bool map_pad(Uint8 button, ui::Button &out)
{
    switch (button)
    {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: out = ui::Button::Up; return true;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: out = ui::Button::Down; return true;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: out = ui::Button::Left; return true;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: out = ui::Button::Right; return true;
    case SDL_CONTROLLER_BUTTON_A: out = ui::Button::Cross; return true;
    case SDL_CONTROLLER_BUTTON_B: out = ui::Button::Circle; return true;
    case SDL_CONTROLLER_BUTTON_X: out = ui::Button::Square; return true;
    case SDL_CONTROLLER_BUTTON_Y: out = ui::Button::Triangle; return true;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: out = ui::Button::L1; return true;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: out = ui::Button::R1; return true;
    case SDL_CONTROLLER_BUTTON_START: out = ui::Button::Options; return true;
    default: return false;
    }
}

SDL_GameController *open_controller()
{
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i))
            if (SDL_GameController *pad = SDL_GameControllerOpen(i))
                return pad;
    return nullptr;
}

bool save_screenshot(SDL_Renderer *renderer, const std::string &path)
{
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    ui::Pixels pixels;
    pixels.width = w;
    pixels.height = h;
    pixels.rgba.resize(static_cast<std::size_t>(w) * h);
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ABGR8888, pixels.rgba.data(), w * 4) != 0)
        return false;
    for (auto &p : pixels.rgba)
        p |= 0xFF000000u;
    return fs::write_atomic(path, ui::encode_png(pixels));
}

} // namespace

int main(int argc, char **argv)
{
    platform::init();
    // Play mode: the hub restarted this title to run one game with an integrated runtime.
    const play::Request request = play::parse_arguments(argc, argv);
    if (!request.game.empty())
    {
        std::string error;
        const int code = play::run(request.game, error);
        if (code < 0)
        {
            platform::log("[play] %s", error.c_str());
            fs::make_dirs(fs::join(platform::data_dir(), "launch"));
            fs::write_atomic(fs::join(platform::data_dir(), "launch/play-error.txt"), request.game + "\n" + error + "\n");
        }
        // Back to the hub when it started the game (or the game could not start), else to the home screen.
        if (request.from_hub || code < 0)
            platform::restart({"--return=" + request.game});
        platform::quit();
    }
    std::string screenshots;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--screenshots") == 0)
            screenshots = argv[i + 1];

    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0)
    {
        platform::log("[akeno] SDL_Init failed: %s", SDL_GetError());
        platform::quit();
    }
    const bool console = platform::is_console();
    if (console)
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software"); // the PS5 SDL has only the software renderer
    const int window_w = console ? ui::kWidth : (screenshots.empty() ? 1600 : ui::kWidth);
    const int window_h = console ? ui::kHeight : (screenshots.empty() ? 900 : ui::kHeight);
    // On the console: the window setup OpenRCT2 for PS5 uses on hardware (full-screen desktop at 0,0).
    SDL_Window *window = console ? SDL_CreateWindow("Akeno Gaming Hub", 0, 0, window_w, window_h,
                                                    SDL_WINDOW_FULLSCREEN_DESKTOP)
                                 : SDL_CreateWindow("Akeno Gaming Hub", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                                    window_w, window_h, SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        platform::log("[akeno] window failed: %s", SDL_GetError());
        platform::quit();
    }
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer)
    {
        platform::log("[akeno] renderer failed: %s", SDL_GetError());
        platform::quit();
    }
    // The patched PS5 SDL draws a software pointer once the mouse position is set; going full
    // screen puts it at 0,0. Akeno is controller-driven, so the pointer stays hidden.
    SDL_ShowCursor(SDL_DISABLE);
    SDL_RendererInfo info{};
    SDL_GetRendererInfo(renderer, &info);
    int output_w = 0, output_h = 0;
    SDL_GetRendererOutputSize(renderer, &output_w, &output_h);
    platform::log("[akeno] renderer %s, output %dx%d", info.name, output_w, output_h);
    SDL_RenderSetLogicalSize(renderer, ui::kWidth, ui::kHeight);

    auto gfx = std::make_unique<ui::Gfx>(renderer);
    const std::string assets = asset_dir();
    if (!gfx->load_fonts(fs::join(assets, "fonts/Inter-Regular.ttf"), fs::join(assets, "fonts/Inter-SemiBold.ttf")))
    {
        platform::log("[akeno] fonts missing in %s", assets.c_str());
        platform::quit();
    }
    auto app = std::make_unique<ui::App>(*gfx, assets, request.returned);
    SDL_GameController *pad = open_controller();
    app->controller_connected = pad != nullptr;

    Repeat repeat;
    std::uint64_t last = SDL_GetTicks64();
    bool first_frame = true, running = true;
    int shot = 0;
    std::uint64_t shot_started = last;
    float frame_avg = 0;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ui::Button button;
            switch (event.type)
            {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_KEYDOWN:
                if (!event.key.repeat && map_key(event.key.keysym.sym, event.key.keysym.mod, button))
                {
                    app->press(button);
                    repeat = {direction(button), button, SDL_GetTicks64() + 380};
                }
                break;
            case SDL_KEYUP:
                repeat.held = false;
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                if (map_pad(event.cbutton.button, button))
                {
                    app->press(button);
                    repeat = {direction(button), button, SDL_GetTicks64() + 380};
                }
                break;
            case SDL_CONTROLLERBUTTONUP:
                repeat.held = false;
                break;
            case SDL_CONTROLLERAXISMOTION:
                if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
                {
                    const bool x = event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX;
                    const int v = event.caxis.value;
                    if (std::abs(v) > 20000)
                    {
                        const ui::Button b = x ? (v < 0 ? ui::Button::Left : ui::Button::Right)
                                               : (v < 0 ? ui::Button::Up : ui::Button::Down);
                        if (!repeat.held || repeat.button != b)
                        {
                            app->press(b);
                            repeat = {true, b, SDL_GetTicks64() + 380};
                        }
                    }
                    else if (std::abs(v) < 12000 && repeat.held &&
                             (x ? (repeat.button == ui::Button::Left || repeat.button == ui::Button::Right)
                                : (repeat.button == ui::Button::Up || repeat.button == ui::Button::Down)))
                        repeat.held = false;
                }
                else if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT && event.caxis.value > 24000)
                    app->press(ui::Button::R2);
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (!pad)
                    pad = open_controller();
                app->controller_connected = pad != nullptr;
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (pad && event.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)))
                {
                    SDL_GameControllerClose(pad);
                    pad = open_controller();
                }
                app->controller_connected = pad != nullptr;
                break;
            default:
                break;
            }
        }
        const std::uint64_t now = SDL_GetTicks64();
        if (repeat.held && now >= repeat.next)
        {
            app->press(repeat.button);
            repeat.next = now + 85;
        }
        const float dt = std::min(0.1f, (now - last) / 1000.0f);
        last = now;
        frame_avg = frame_avg * 0.95f + dt * 0.05f;
        app->update(dt);
        app->draw();
        if (!screenshots.empty())
        {
            // Scripted tour: wait for the catalogs, then capture every screen once its art settles.
            const bool ready = app->catalog_ready() || now - shot_started > 25000;
            if (ready && now - shot_started > 6000)
            {
                static const char *names[] = {"01-entdecken", "02-entdecken-regale", "03-store", "04-bibliothek",
                                              "05-runtimes", "06-downloads", "07-diagnose", "08-einstellungen",
                                              "09-detail"};
                constexpr int kShots = static_cast<int>(sizeof names / sizeof names[0]);
                save_screenshot(renderer, fs::join(screenshots, std::string(names[shot]) + ".png"));
                platform::log("[akeno] screenshot %s (%.1f ms/frame)", names[shot], frame_avg * 1000);
                ++shot;
                shot_started = now - 3500;
                if (shot >= kShots)
                    running = false;
                else if (shot == 1)
                    app->press(ui::Button::Down);
                else if (shot == kShots - 1)
                    app->open_detail_for_test(std::getenv("AKENO_DETAIL") ? std::getenv("AKENO_DETAIL") : "PPSA99874");
                else
                    app->open_tab_for_test(shot - 1);
            }
        }
        SDL_RenderPresent(renderer);
        if (first_frame)
        {
            platform::first_frame_shown();
            first_frame = false;
        }
        if (app->quit_requested())
        {
            // Show the last frame (the "starting" notice) briefly before closing.
            SDL_Delay(600);
            running = false;
        }
    }
    const std::string play_next = app->play_request();
    app.reset();
    gfx.reset();
    if (pad)
        SDL_GameControllerClose(pad);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (!play_next.empty())
        platform::restart(play::play_arguments(play_next, true)); // returns only when that failed
    platform::quit();
}
