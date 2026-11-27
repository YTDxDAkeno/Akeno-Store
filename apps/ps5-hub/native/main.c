#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "catalog.h"
#include "font.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum { WIDTH = 960, HEIGHT = 540, VISIBLE_ROWS = 7 };
typedef struct {
    Catalog catalogs[2];
    size_t selected[2];
    int tab;
    int platform;
    char search[64];
    bool editing;
    size_t matches[CATALOG_LIMIT];
    size_t match_count;
    bool details;
    bool quit;
} View;

static const SDL_Color background = {17, 16, 29, 255};
static const SDL_Color panel = {28, 25, 43, 255};
static const SDL_Color border = {68, 56, 94, 255};
static const SDL_Color accent = {211, 177, 255, 255};
static const SDL_Color foreground = {244, 236, 255, 255};
static const SDL_Color muted = {183, 170, 209, 255};
static const SDL_Color selected = {65, 45, 98, 255};

static void color(SDL_Renderer *renderer, SDL_Color value)
{
    SDL_SetRenderDrawColor(renderer, value.r, value.g, value.b, value.a);
}

static void box(SDL_Renderer *renderer, SDL_Rect rectangle, SDL_Color fill, SDL_Color stroke)
{
    color(renderer, fill);
    SDL_RenderFillRect(renderer, &rectangle);
    color(renderer, stroke);
    SDL_RenderDrawRect(renderer, &rectangle);
}

static void draw_text(SDL_Renderer *renderer, int x, int y, int width, int scale,
                      SDL_Color ink, const char *value)
{
    int letters = (width + scale) / (scale * 6);
    color(renderer, ink);
    for (const unsigned char *cursor = (const unsigned char *)value; *cursor && letters > 0; cursor++) {
        if ((*cursor & 0xc0) == 0x80) continue;
        const uint8_t *pixels = glyph(*cursor);
        for (int row = 0; row < 7; row++) {
            for (int column = 0; column < 5; column++) {
                if (pixels[row] & (1 << (4 - column))) {
                    SDL_Rect pixel = {x + column * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(renderer, &pixel);
                }
            }
        }
        x += 6 * scale;
        letters--;
    }
}

static void draw_detail(SDL_Renderer *renderer, const Game *game)
{
    box(renderer, (SDL_Rect){603, 133, 325, 335}, panel, border);
    draw_text(renderer, 626, 158, 270, 2, accent, "SPIELDETAILS");
    draw_text(renderer, 626, 201, 280, 2, foreground, game->title);
    const char *labels[] = {"QUELLE", "VERSION", "SPIEL-ID"};
    const char *values[] = {game->source, *game->version ? game->version : "Unbekannt", game->id};
    for (int index = 0; index < 3; index++) {
        int row = 250 + index * 62;
        draw_text(renderer, 626, row, 270, 1, muted, labels[index]);
        draw_text(renderer, 626, row + 18, 280, 2, foreground, values[index]);
    }
}

static bool contains(const char *value, const char *query)
{
    const size_t length = strlen(query);
    if (length == 0) return true;
    for (; *value; value++) {
        if (SDL_strncasecmp(value, query, length) == 0) return true;
    }
    return false;
}

static void filter(View *view)
{
    const Catalog *catalog = &view->catalogs[view->tab];
    view->match_count = 0;
    for (size_t index = 0; index < catalog->count; index++) {
        const Game *game = &catalog->games[index];
        if (view->platform == 1 && !contains(game->source, " / PS5")) continue;
        if (view->platform == 2 && !contains(game->source, " / PC")) continue;
        if (!contains(game->title, view->search) && !contains(game->source, view->search)) continue;
        view->matches[view->match_count++] = index;
    }
    if (view->selected[view->tab] >= view->match_count) view->selected[view->tab] = 0;
}

static void draw_view(SDL_Renderer *renderer, const View *view, bool connected)
{
    const Catalog *catalog = &view->catalogs[view->tab];
    const size_t current = view->selected[view->tab];
    const size_t start = current < VISIBLE_ROWS ? 0 : current - VISIBLE_ROWS + 1;
    bool has_details = view->details && view->match_count;
    int list_width = has_details ? 552 : 896;

    color(renderer, background);
    SDL_RenderClear(renderer);
    box(renderer, (SDL_Rect){32, 28, 36, 36}, selected, border);
    draw_text(renderer, 42, 36, 22, 3, accent, "A");
    draw_text(renderer, 83, 34, 500, 3, foreground, "AKENO GAMING HUB");
    draw_text(renderer, 83, 58, 500, 1, muted, "NATIVER KATALOG");
    draw_text(renderer, 786, 43, 142, 1, muted, connected ? "CONTROLLER AKTIV" : "TASTATUR AKTIV");

    const char *tabs[] = {"KATALOG", "BIBLIOTHEK"};
    for (int index = 0; index < 2; index++) {
        int left = 32 + index * 163;
        box(renderer, (SDL_Rect){left, 85, 153, 33}, view->tab == index ? selected : panel, border);
        draw_text(renderer, left + 11, 95, 128, 2, foreground, tabs[index]);
    }
    const char *platforms[] = {"ALLE PLATTFORMEN", "PS5", "PC"};
    box(renderer, (SDL_Rect){358, 85, 269, 33}, panel, view->editing ? accent : border);
    draw_text(renderer, 371, 97, 240, 1, foreground, *view->search ? view->search : "SUCHE (F1)");
    box(renderer, (SDL_Rect){638, 85, 151, 33}, panel, border);
    draw_text(renderer, 648, 97, 130, 1, foreground, platforms[view->platform]);
    char count[32];
    snprintf(count, sizeof count, "%zu EINTRAEGE", view->match_count);
    draw_text(renderer, 810, 101, 118, 1, muted, count);

    if (!view->match_count) {
        box(renderer, (SDL_Rect){32, 133, list_width, 70}, panel, border);
        draw_text(renderer, 51, 160, list_width - 30, 2, muted, "KEINE EINTRAEGE");
    }
    for (size_t index = start; index < view->match_count && index < start + VISIBLE_ROWS; index++) {
        const Game *game = &catalog->games[view->matches[index]];
        const int y = 133 + (int)(index - start) * 48;
        const bool active = index == current;
        box(renderer, (SDL_Rect){32, y, list_width, 43}, active ? selected : panel, active ? accent : border);
        draw_text(renderer, 49, y + 8, list_width - 36, 2, foreground, game->title);
        draw_text(renderer, 49, y + 28, list_width - 36, 1, muted, game->source);
    }
    if (has_details) draw_detail(renderer, &catalog->games[view->matches[current]]);
    box(renderer, (SDL_Rect){0, 486, WIDTH, 54}, panel, border);
    draw_text(renderer, 32, 505, 890, 1, foreground, connected
        ? "STEUERKREUZ NAVIGIEREN    L1/R1 ANSICHT    DREIECK/Y PLATTFORM    X/A DETAILS    KREIS/B ZURUECK"
        : "PFEILTASTEN NAVIGIEREN    TAB ANSICHT    F1 SUCHE    F2 PLATTFORM    ENTER DETAILS    ESC ZURUECK");
}

typedef enum { NONE, UP, DOWN, PREVIOUS, NEXT, SELECT, BACK, PLATFORM, SEARCH } Action;

static Action keyboard(SDL_Keycode key)
{
    switch (key) {
        case SDLK_UP: return UP;
        case SDLK_DOWN: return DOWN;
        case SDLK_LEFT: return PREVIOUS;
        case SDLK_RIGHT:
        case SDLK_TAB: return NEXT;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return SELECT;
        case SDLK_ESCAPE: return BACK;
        case SDLK_F1: return SEARCH;
        case SDLK_F2: return PLATFORM;
        default: return NONE;
    }
}

static Action gamepad(Uint8 button)
{
    switch (button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return PREVIOUS;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return NEXT;
        case SDL_CONTROLLER_BUTTON_A: return SELECT;
        case SDL_CONTROLLER_BUTTON_B: return BACK;
        case SDL_CONTROLLER_BUTTON_Y: return PLATFORM;
        default: return NONE;
    }
}

static void apply(View *view, Action action)
{
    size_t *selected_row = &view->selected[view->tab];
    size_t count = view->match_count;

    switch (action) {
        case UP:
            if (*selected_row > 0) (*selected_row)--;
            break;
        case DOWN:
            if (*selected_row + 1 < count) (*selected_row)++;
            break;
        case PREVIOUS:
        case NEXT:
            view->tab = 1 - view->tab;
            view->details = false;
            filter(view);
            break;
        case SELECT:
            if (count > 0) view->details = !view->details;
            break;
        case BACK:
            if (view->details) view->details = false;
            else if (*view->search || view->platform) {
                *view->search = '\0';
                view->platform = 0;
                filter(view);
            } else if (view->tab != 0) {
                view->tab = 0;
                filter(view);
            }
            else view->quit = true;
            break;
        case PLATFORM:
            view->platform = (view->platform + 1) % 3;
            view->selected[view->tab] = 0;
            view->details = false;
            filter(view);
            break;
        case SEARCH:
            view->editing = true;
            SDL_StartTextInput();
            break;
        case NONE:
            break;
    }
}

static SDL_GameController *find_controller(void)
{
    for (int index = 0; index < SDL_NumJoysticks(); index++) {
        if (SDL_IsGameController(index)) {
            SDL_GameController *controller = SDL_GameControllerOpen(index);
            if (controller) return controller;
        }
    }
    return NULL;
}

static void text_input(View *view, const SDL_Event *event)
{
    size_t length = strlen(view->search);
    if (event->type == SDL_TEXTINPUT) {
        size_t added = strlen(event->text.text);
        if (length + added >= sizeof view->search) return;
        memcpy(view->search + length, event->text.text, added + 1);
    } else if (event->key.keysym.sym == SDLK_BACKSPACE && length) {
        do { length--; } while (length && ((unsigned char)view->search[length] & 0xc0) == 0x80);
        view->search[length] = '\0';
    } else if (event->key.keysym.sym == SDLK_ESCAPE || event->key.keysym.sym == SDLK_RETURN) {
        view->editing = false;
        SDL_StopTextInput();
    } else {
        return;
    }
    view->selected[view->tab] = 0;
    view->details = false;
    filter(view);
}

static void handle(View *view, SDL_GameController **controller, const SDL_Event *event)
{
    switch (event->type) {
        case SDL_QUIT:
            view->quit = true;
            break;
        case SDL_TEXTINPUT:
            if (view->editing) text_input(view, event);
            break;
        case SDL_KEYDOWN:
            if (view->editing) {
                text_input(view, event);
                break;
            }
            if (!event->key.repeat || event->key.keysym.sym == SDLK_UP || event->key.keysym.sym == SDLK_DOWN) {
                apply(view, keyboard(event->key.keysym.sym));
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            if (view->editing) {
                if (gamepad(event->cbutton.button) == BACK) {
                    view->editing = false;
                    SDL_StopTextInput();
                }
            } else {
                apply(view, gamepad(event->cbutton.button));
            }
            break;
        case SDL_CONTROLLERDEVICEADDED:
            if (!*controller) *controller = find_controller();
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (*controller && event->cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(*controller))) {
                SDL_GameControllerClose(*controller);
                *controller = find_controller();
            }
            break;
    }
}

static int self_test(View *view, SDL_Renderer *renderer, SDL_GameController **controller)
{
    if (catalog_self_test() != 0) return 1;
    view->catalogs[0].count = 2;
    snprintf(view->catalogs[0].games[1].title, sizeof view->catalogs[0].games[1].title, "Signal Drift");

    filter(view);
    SDL_Event key = {.key = {.type = SDL_KEYDOWN, .keysym.sym = SDLK_DOWN}};
    handle(view, controller, &key);
    SDL_Event select = {.cbutton = {.type = SDL_CONTROLLERBUTTONDOWN, .button = SDL_CONTROLLER_BUTTON_A}};
    handle(view, controller, &select);
    if (view->selected[0] != 1 || !view->details) return 1;

    draw_view(renderer, view, true);
    SDL_Rect outline = {32, 181, 1, 1};
    Uint8 pixel[4];
    if (SDL_RenderReadPixels(renderer, &outline, SDL_PIXELFORMAT_RGBA32, pixel, sizeof pixel) != 0
        || pixel[0] != accent.r || pixel[1] != accent.g || pixel[2] != accent.b) {
        fputs("SDL2 self-test: selection highlight was not rendered.\n", stderr);
        return 1;
    }
    SDL_RenderPresent(renderer);
    key.key.keysym.sym = SDLK_TAB;
    handle(view, controller, &key);
    if (view->tab != 1 || view->details) return 1;
    key.key.keysym.sym = SDLK_ESCAPE;
    handle(view, controller, &key);
    if (view->tab != 0 || view->quit) return 1;
    handle(view, controller, &key);
    if (!view->quit) return 1;
    view->quit = false;
    SDL_strlcpy(view->catalogs[0].games[1].source, "Github / PS5", sizeof view->catalogs[0].games[1].source);
    apply(view, PLATFORM);
    if (view->match_count != 1 || view->matches[0] != 1) return 1;
    apply(view, SEARCH);
    SDL_Event input = {.text = {.type = SDL_TEXTINPUT, .text = "DRIFT"}};
    handle(view, controller, &input);
    if (view->match_count != 1) return 1;
    SDL_strlcpy(input.text.text, " Z", sizeof input.text.text);
    handle(view, controller, &input);
    if (view->match_count != 0) return 1;
    SDL_StopTextInput();
    puts("Native SDL2 self-test passed: parser, keyboard, controller mapping, rendering, search, platform filter.");
    return 0;
}

int main(int argc, char **argv)
{
    static View view;
    const bool testing = argc == 2 && strcmp(argv[1], "--self-test") == 0;
#ifdef AKENO_APP_DATA_DIR
    char *app_argv[] = {"akeno-hub", AKENO_APP_DATA_DIR "/catalog.tsv", AKENO_APP_DATA_DIR "/library.tsv"};
    if (argc < 2) {
        argv = app_argv;
        argc = 3;
    }
#endif
    if (!testing) {
        if (argc < 2 || argc > 3) {
            fputs("Aufruf: akeno-native KATALOG.tsv [BIBLIOTHEK.tsv]\n", stderr);
            return 2;
        }
        for (int index = 1; index < argc; index++) {
            char error[384];
            if (catalog_load(argv[index], &view.catalogs[index - 1], error, sizeof error) != 0) {
                fprintf(stderr, "%s\n", error);
                return 2;
            }
        }
    }

    filter(&view);
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL-Initialisierung fehlgeschlagen: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("Akeno Gaming Hub", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, testing ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED) : NULL;
    if (window && !renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer || SDL_RenderSetLogicalSize(renderer, WIDTH, HEIGHT) != 0) {
        fprintf(stderr, "SDL-Renderer konnte nicht gestartet werden: %s\n", SDL_GetError());
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GameController *controller = find_controller();
    int result = 0;
    if (testing) {
        result = self_test(&view, renderer, &controller);
    } else {
        draw_view(renderer, &view, controller != NULL);
        SDL_RenderPresent(renderer);
        while (!view.quit) {
            SDL_Event event;
            if (SDL_WaitEventTimeout(&event, 250)) {
                do {
                    handle(&view, &controller, &event);
                } while (!view.quit && SDL_PollEvent(&event));
                if (!view.quit) {
                    draw_view(renderer, &view, controller != NULL);
                    SDL_RenderPresent(renderer);
                }
            }
        }
    }
    if (controller) SDL_GameControllerClose(controller);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
