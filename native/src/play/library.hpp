// Akeno Gaming Hub - games installed into Akeno's own library and their start profiles.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// <data>/library/<id>/profile.json describes how Akeno's play mode starts a game:
// which integrated runtime, where its files are, where saves go. Games Akeno downloaded
// live in <data>/library/<id>/game; games the user copied to an import folder stay where
// they are and only get a profile.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace akeno
{

struct GameProfile
{
    std::string id;          // [a-z0-9-], unique in the library
    std::string title;
    std::string developer;
    std::string engine;      // detection engine id (rpgmaker2k, ...)
    std::string runtime;     // integrated runtime id (easyrpg, ...)
    std::string folder;      // the game's files (absolute)
    std::string saves;       // where the runtime writes saves (absolute)
    std::string source;      // catalog id ("itch:...", "akeno:...") or "import"
    std::string version;
    std::string cover;       // local artwork path or HTTPS address from the store
    std::string sha256;      // of the downloaded archive, when Akeno downloaded it
    bool owned_files = false; // Akeno downloaded the files (uninstall removes them)
    std::int64_t installed = 0;
    std::int64_t last_played = 0;
};

std::string library_root();                 // <data>/library
std::string library_dir(const std::string &id);
bool valid_library_id(const std::string &id);
// A library id from free text ("Towelket 3" -> "towelket-3"), with a suffix when taken.
std::string make_library_id(const std::string &text);

bool write_profile(const GameProfile &profile);
std::optional<GameProfile> read_profile(const std::string &id);
std::vector<GameProfile> list_profiles();
// Removes the profile and, for downloaded games, the files. Saves are kept unless asked.
bool remove_profile(const std::string &id, bool remove_saves, std::string &error);

std::string profile_json(const GameProfile &profile);
bool parse_profile(const std::string &text, GameProfile &out);

} // namespace akeno
