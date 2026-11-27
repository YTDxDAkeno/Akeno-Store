// Akeno Gaming Hub - installed PS5 titles in the folders ShadowMountPlus scans.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

namespace akeno
{

struct InstalledTitle
{
    std::string title_id;
    std::string name;
    std::string content_version;
    std::string folder;    // full path of the title folder
    std::string root;      // the scanned root it lives in
    std::string icon_path; // sce_sys/icon0.png when present
    bool game = true;      // applicationCategoryType 0
    bool managed = false;  // installed by Akeno (receipt present)
    std::string source_game_id; // catalog id recorded in the receipt
    bool shortcut = false;      // an Akeno home-screen tile
    // A Windows game in MihawkOS's library (title_id empty): started as MihawkOS profile=<id>.
    bool windows = false;
    std::string profile;        // /data/ps5proton/games/<profile>
    std::string program;        // its Windows program path (C:\...)
};

// The MihawkOS layout Akeno installs Windows games into (docs/SELF_INSTALL.md of MihawkOS):
// <root>/games/<profile>/game.json and the game itself in C:\Games\<folder>, where C: is
// <root>/prefixes/mihawk/drive_c.
std::string mihawk_drive_c(const std::string &mihawk_root);
bool valid_mihawk_profile(const std::string &profile);

struct ParamInfo
{
    std::string title_id, name, content_version;
    bool game = true;
};

// Reads sce_sys/param.json; false when it is not a PS5 title description.
bool parse_param_json(const std::string &text, ParamInfo &out);

class Library
{
  public:
    explicit Library(std::string receipts_dir) : receipts_(std::move(receipts_dir)) {}
    // Scans every root one level deep (and two for nested folders) for title folders.
    std::vector<InstalledTitle> scan(const std::vector<std::string> &roots) const;
    // Lists MihawkOS's library entries; those Akeno installed carry an "akeno" block.
    static std::vector<InstalledTitle> scan_mihawk(const std::string &mihawk_root);

    void write_receipt(const std::string &title_id, const std::string &game_id, const std::string &version,
                       const std::string &sha256, const std::string &folder, bool shortcut = false) const;
    void remove_receipt(const std::string &title_id) const;

  private:
    std::string receipts_;
};

// True when the console reports the title as running (a folder <TITLEID>_<n> in /mnt/sandbox).
bool title_running(const std::string &title_id);

} // namespace akeno
