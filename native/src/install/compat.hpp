// Akeno Gaming Hub - the local compatibility database (master prompt section 19).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reports are what the player saw on this console, entered by hand after playing. Akeno
// never guesses a result: a game it started is only marked as "waiting for a report", and
// the next time Akeno opens it asks how the game ran. Reports stay on the console
// (/data/akeno/compat.json) and can be exported to and imported from a JSON file; nothing
// is sent anywhere.
#pragma once

#include "stores/game.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace akeno
{

// One aspect of a test: unknown, works, works with problems, does not work.
enum class Aspect
{
    Unknown,
    Works,
    Issues,
    Fails,
};
const char *aspect_label(Aspect aspect);
Aspect next_aspect(Aspect aspect);

struct CompatReport
{
    std::string game_id; // catalog id ("prosperostore:PPSA12345", "akeno:luanti", ...)
    std::string title;
    std::string provider;
    std::string game_version;
    std::string runtime;         // "PS5-Titel", "MihawkOS", "Parallax Browser", ...
    std::string runtime_version;
    std::string firmware;
    Compat status = Compat::Untested;
    Aspect install = Aspect::Unknown, start = Aspect::Unknown, gameplay = Aspect::Unknown,
           graphics = Aspect::Unknown, audio = Aspect::Unknown, controller = Aspect::Unknown,
           network = Aspect::Unknown;
    std::string issues;   // known problems, free text
    std::string settings; // recommended settings, free text
    std::int64_t tested = 0;
};

struct PendingLaunch
{
    std::string game_id, title, runtime;
    std::int64_t started = 0;
};

class CompatDb
{
  public:
    explicit CompatDb(std::string path) : path_(std::move(path)) { load(); }

    const CompatReport *find(const std::string &game_id) const;
    void put(const CompatReport &report); // replaces the report for the same game
    void remove(const std::string &game_id);
    const std::vector<CompatReport> &all() const { return reports_; }

    // A game Akeno is about to start; asked about the next time Akeno opens.
    void set_pending(const PendingLaunch &launch);
    std::optional<PendingLaunch> pending() const { return pending_; }
    void clear_pending();

    // Export writes every report; import merges reports, keeping the newer test per game.
    std::string export_json() const;
    int import_json(const std::string &text, std::string &error);

    bool save() const;

  private:
    bool load();
    std::string path_;
    std::vector<CompatReport> reports_;
    std::optional<PendingLaunch> pending_;
};

} // namespace akeno
