// Akeno Gaming Hub - Akeno's own integrated runtimes and game format detection.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A game runs inside Akeno: the hub restarts its own title in play mode (src/play) and an
// integrated runtime takes the screen, the controller and the audio output. Akeno does not
// hand games to other applications. Each runtime below says honestly whether it is part of
// this build or still planned, and which open-source project it builds on.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace akeno
{

enum class RuntimeState
{
    Integrated, // compiled into this Akeno build
    Planned,    // designed, not built yet
};

struct RuntimeInfo
{
    const char *id;      // easyrpg, native, windows, linux, java, html5, godot, unity, renpy, python, dotnet, gamemaker, retro
    const char *name;    // shown in the runtime manager
    const char *engines; // what it runs
    const char *basis;   // the project and licence it builds on
    const char *detail;  // current state in plain words
    RuntimeState state;
    const char *components = nullptr; // shared runtime components it needs first ("wine"), comma separated
};

const std::vector<RuntimeInfo> &akeno_runtimes();
const RuntimeInfo *runtime_by_id(const std::string &id);
bool runtime_integrated(const std::string &id);

struct Detection
{
    std::string engine;      // machine id: rpgmaker2k, rpgmakermv, renpy, godot, unity, html5, windows-x64, linux-elf, java, rom-nes ...
    std::string engine_name; // e.g. "RPG Maker 2000/2003"
    std::string format;      // e.g. "Windows-Programm (x86-64)"
    std::string evidence;    // what was read to decide
    std::string runtime;     // the Akeno runtime that runs it, empty when none is designed
    std::string root;        // for folders: where the game itself starts (e.g. the folder with RPG_RT.ldb)
    bool playable = false;   // that runtime is integrated in this build
    std::string advice;      // when not playable: why, and what is missing
    // Own dumps: what the header says, so Akeno can find the native port or recompilation
    // that takes this original.
    std::string platform;   // n64, gamecube, wii, ps2, psx, psp, gba, nds, xbox, xbox360, switch, nes, snes ...
    std::string game_id;    // N64/GBA/NDS game code, GameCube/Wii ID, PS1/PS2/PSP serial (SLUS-21590)
    std::string game_title; // the title stored in the header, when there is one
};

// Recomp/port entries in the catalog that name this original (platform + title or serial),
// best first. `texts` are the candidates' name, needs and description, lower case.
struct PortCandidate
{
    std::size_t index;
    int score;
};
std::vector<PortCandidate> match_ports(const Detection &dump, const std::vector<std::string> &texts);

// Reads only file headers and folder layouts (never executes anything).
Detection detect_game(const std::string &path);

// User files below the import folders (own games, ROMs, ISOs), each with its detection.
struct LocalGame
{
    std::string path;
    std::string name;
    Detection detection;
    std::uint64_t size = 0;
};
std::vector<LocalGame> scan_local_games(const std::vector<std::string> &folders);

} // namespace akeno
