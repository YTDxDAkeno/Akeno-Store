// Akeno Gaming Hub - Akeno's game library.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "play/library.hpp"

#include "core/fs.hpp"
#include "core/json.hpp"
#include "core/util.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cctype>

namespace akeno
{

std::string library_root() { return fs::join(platform::data_dir(), "library"); }

std::string library_dir(const std::string &id) { return fs::join(library_root(), id); }

bool valid_library_id(const std::string &id)
{
    if (id.empty() || id.size() > 64 || id.front() == '-')
        return false;
    for (char c : id)
        if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '-'))
            return false;
    return true;
}

std::string make_library_id(const std::string &text)
{
    std::string id;
    for (char c : lower(text))
    {
        if (std::isalnum(static_cast<unsigned char>(c)))
            id += c;
        else if (!id.empty() && id.back() != '-')
            id += '-';
        if (id.size() >= 48)
            break;
    }
    while (!id.empty() && id.back() == '-')
        id.pop_back();
    if (id.empty())
        id = "spiel";
    std::string candidate = id;
    for (int n = 2; fs::exists(library_dir(candidate)) && n < 1000; ++n)
        candidate = id + "-" + std::to_string(n);
    return candidate;
}

std::string profile_json(const GameProfile &p)
{
    return "{\n  \"schema\": 1,\n  \"id\": " + json::quote(p.id) + ",\n  \"title\": " + json::quote(p.title) +
           ",\n  \"developer\": " + json::quote(p.developer) + ",\n  \"engine\": " + json::quote(p.engine) +
           ",\n  \"runtime\": " + json::quote(p.runtime) + ",\n  \"folder\": " + json::quote(p.folder) +
           ",\n  \"saves\": " + json::quote(p.saves) + ",\n  \"source\": " + json::quote(p.source) +
           ",\n  \"version\": " + json::quote(p.version) + ",\n  \"cover\": " + json::quote(p.cover) +
           ",\n  \"sha256\": " + json::quote(p.sha256) + ",\n  \"owned_files\": " + (p.owned_files ? "true" : "false") +
           ",\n  \"installed\": " + std::to_string(p.installed) + ",\n  \"last_played\": " +
           std::to_string(p.last_played) + "\n}\n";
}

bool parse_profile(const std::string &text, GameProfile &out)
{
    const json::Document doc = json::Document::parse(text, 256 * 1024);
    const json::Value root = doc.root();
    if (!root.is_object() || root["schema"].integer() != 1)
        return false;
    GameProfile p;
    p.id = root.str("id");
    p.title = sanitize_text(root.str("title"), 160);
    p.developer = sanitize_text(root.str("developer"), 160);
    p.engine = root.str("engine");
    p.runtime = root.str("runtime");
    p.folder = root.str("folder");
    p.saves = root.str("saves");
    p.source = root.str("source");
    p.version = sanitize_text(root.str("version"), 60);
    p.cover = root.str("cover");
    p.sha256 = root.str("sha256");
    p.owned_files = root["owned_files"].boolean(false);
    p.installed = root["installed"].integer();
    p.last_played = root["last_played"].integer();
    // Paths must be absolute and must not climb out of where they point.
    const auto safe = [](const std::string &path) {
        return !path.empty() && path.front() == '/' && path.find("/../") == std::string::npos &&
               !ends_with(path, "/..");
    };
    if (!valid_library_id(p.id) || p.title.empty() || p.runtime.empty() || !safe(p.folder) || !safe(p.saves))
        return false;
    out = std::move(p);
    return true;
}

bool write_profile(const GameProfile &profile)
{
    if (!valid_library_id(profile.id))
        return false;
    const std::string dir = library_dir(profile.id);
    return fs::make_dirs(dir) && fs::write_atomic(fs::join(dir, "profile.json"), profile_json(profile));
}

std::optional<GameProfile> read_profile(const std::string &id)
{
    if (!valid_library_id(id))
        return std::nullopt;
    const auto text = fs::read_file(fs::join(library_dir(id), "profile.json"), 256 * 1024);
    GameProfile profile;
    if (!text || !parse_profile(*text, profile) || profile.id != id)
        return std::nullopt;
    return profile;
}

std::vector<GameProfile> list_profiles()
{
    std::vector<GameProfile> out;
    for (const fs::Entry &entry : fs::list(library_root()))
        if (entry.directory)
            if (auto profile = read_profile(entry.name))
                out.push_back(std::move(*profile));
    std::sort(out.begin(), out.end(), [](const GameProfile &a, const GameProfile &b) {
        return a.last_played != b.last_played ? a.last_played > b.last_played : lower(a.title) < lower(b.title);
    });
    return out;
}

bool remove_profile(const std::string &id, bool remove_saves, std::string &error)
{
    const auto profile = read_profile(id);
    if (!profile)
    {
        error = "Unbekanntes Spiel";
        return false;
    }
    const std::string dir = library_dir(id);
    // Downloaded files live inside the library folder; imported ones are never touched.
    if (profile->owned_files)
        fs::remove_tree(fs::join(dir, "game"));
    if (remove_saves)
        fs::remove_tree(fs::join(dir, "saves"));
    fs::remove_tree(fs::join(dir, "profile.json"));
    fs::remove_tree(fs::join(dir, "cover.png"));
    if (fs::list(dir).empty())
        fs::remove_tree(dir);
    return true;
}

} // namespace akeno
