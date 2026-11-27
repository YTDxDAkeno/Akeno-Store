// Akeno Gaming Hub - universal runtimes: format detection and routing to PS5 runtime titles.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Akeno does not reimplement emulators, Wine or a browser. Each runtime is an existing
// PS5 title from the signed ProsperoStore catalog; Akeno detects what a file is, picks the
// runtime titles that handle it, installs them like any other title and starts them
// through the console's ELF loader.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace akeno
{

enum class LaunchArgs
{
    None,        // the runtime opens its own library/file browser
    MihawkPath,  // MihawkOS: profile=<id> or path=<Windows path> (src/pw_wine_launch.h)
    Url,         // Parallax Browser: a launch argument with "://" is its start page (ps5browser.c)
};

struct RuntimeInfo
{
    const char *key;      // runtime family: web, windows, retroarch, n64, ...
    const char *name;     // display name of the runtime title
    const char *title_id; // its PS5 title ID in the ProsperoStore catalog
    const char *summary;  // what it runs
    LaunchArgs args = LaunchArgs::None;
};

const std::vector<RuntimeInfo> &runtime_registry();
// Runtime titles for a runtime family, best first.
std::vector<const RuntimeInfo *> runtimes_for(const std::string &family);
const RuntimeInfo *runtime_by_title(const std::string &title_id);

struct Detection
{
    std::string family;   // runtime family to route to; empty when nothing matched
    std::string format;   // e.g. "Nintendo 64 ROM", "Windows PE (x86-64)"
    std::string engine;   // e.g. "Ren'Py", "Godot 4", "RPG Maker MV"
    std::string evidence; // what was read to decide
    std::string advice;   // when no PS5 runtime exists: what would work instead
};

// Reads only file headers (never executes anything). `path` may be a file or a folder.
Detection detect_game(const std::string &path);

// User files below the import folders (ROMs, ISOs, game folders), each with its detection.
struct LocalGame
{
    std::string path;
    std::string name;
    Detection detection;
    std::uint64_t size = 0;
};
std::vector<LocalGame> scan_local_games(const std::vector<std::string> &folders);

} // namespace akeno
