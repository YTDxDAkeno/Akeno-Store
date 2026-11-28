// Akeno Gaming Hub - normalized catalog entries from every store.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace akeno
{

// Compatibility of an entry on the PS5 (master prompt section 19). The catalog states what
// is known; a report the user makes after playing takes precedence on this console.
enum class Compat
{
    Untested,     // Noch nicht getestet
    Verified,     // Verifiziert spielbar
    Limited,      // Spielbar mit Einschränkungen
    Experimental, // Experimentell
    Boots,        // Startet, Gameplay unbestätigt
    Incompatible, // Nicht kompatibel
};
const char *compat_label(Compat compat);
Compat compat_from_key(std::string_view key); // untested, verified, limited, experimental, boots, incompatible
const char *compat_key(Compat compat);

// How Akeno can act on an entry. This is what the UI offers; nothing is faked.
enum class Access
{
    Install,  // a PS5 title folder ZIP with a known SHA-256 (or one the provider resolves)
    Runtime,  // a game whose files Akeno would run itself, but whose runtime is not integrated yet
    External, // discovery only: the store needs an account or a PC; Akeno shows the store page
    Upcoming, // announced, nothing to download yet
};

struct Game
{
    std::string id;       // unique across providers: "<provider>:<key>"
    std::string provider; // prosperostore, recompstore, github, itch, gamejolt, gog, epic
    std::string provider_label;
    std::string title;
    std::string developer;
    std::string description;
    std::string version;         // display only
    std::string content_version; // NN.NNN.NNN for update checks
    std::string category;        // provider shelf (games, emulators, console-ports ...)
    std::string kind;            // game, app, tool
    std::string title_id;        // PS5 title ID when known
    std::string icon_url;        // square artwork
    std::string cover_url;       // portrait or wide cover, when the store has one
    std::string hero_url;        // wide background
    std::vector<std::string> screenshots;
    std::string page_url;        // the store's own page
    std::string license;
    std::string needs;           // what the user must bring (own disc image, game data ...)
    std::string runtime;         // native (PS5 title), akeno (Akeno library game), or the platform of a game Akeno cannot run yet: web, windows, java, linux ...
    std::string launch_url;      // for web games
    std::vector<std::string> tags;
    std::string price;           // display text, "Kostenlos" when free
    bool free = true;
    bool signed_catalog = false; // came from a signature-verified catalog
    bool adult = false;
    std::string sandbox;         // stays / leaves / unclear (ProsperoStore release scan)
    std::string released;
    std::string updated;
    Access access = Access::External;

    // Install data, filled by the catalog or by Provider::resolve().
    std::string artifact_url;
    std::string sha256;
    std::string format; // zip
    std::uint64_t size = 0;
    std::string release_notes;
    std::string source_repo; // owner/name on GitHub
    std::string asset_name;  // GitHub release asset to resolve (glob; '|' separates alternatives)
    bool resolved = false;

    // Games for Akeno's own library (runtime "akeno", access Install): the original files
    // are downloaded, their engine detected and an integrated runtime plays them.
    std::string profile;     // library id
    std::string engine_hint; // what the store says (e.g. rpgmaker2k); the files decide
    std::string upload_id;   // itch.io upload to download

    // Curated facts (Akeno catalog); empty when unknown.
    std::vector<std::string> genres;
    std::string online;     // e.g. "Offizielle Server", "Nur lokal"
    std::string crossplay;  // e.g. "PC, Android, iOS"
    std::string controller; // e.g. "DualSense", "Maus und Tastatur nötig"
    std::string age;        // age rating text when published (USK/PEGI/ESRB)
    Compat compat = Compat::Untested;
    std::string compat_note; // why: what was tested, what blocks it

    bool has_tag(const std::string &tag) const
    {
        for (const auto &t : tags)
            if (t == tag)
                return true;
        return false;
    }
};

} // namespace akeno
