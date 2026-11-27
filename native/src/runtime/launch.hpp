// Akeno Gaming Hub - starting titles and home-screen shortcuts through the ELF loader.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A PS5 title cannot start another title itself (sceLncUtilLaunchApp is a null import
// for a title, sceSystemServiceLaunchApp answers 0x8094000f), so Akeno follows the
// forwarder design of PS5_RetroArch and MihawkOS (GPL-3.0-or-later): a small payload is
// sent to the console's ELF loader on 127.0.0.1:9021 with the requesting title's ID
// written into a marked slot of its bytes. The payload waits until that title has closed
// and starts the target with the arguments from /data/akeno/launch/<requester>.txt (or,
// for a shortcut tile, /data/homebrew/<tile>/forward.txt).
#pragma once

#include "core/image.hpp"

#include <string>
#include <vector>

namespace akeno
{

constexpr const char *kAkenoTitleId = "PPSA90410";
constexpr const char *kLaunchSlotMarker = "AKENO-LAUNCH-REQUESTER-SLOT-0001";
constexpr int kShortcutFirst = 90500, kShortcutLast = 90599;

// Writes the request and sends the payload. On success the caller must close Akeno
// (platform::quit) so the payload can start the target.
bool request_launch(const std::string &target_title_id, const std::vector<std::string> &arguments,
                    std::string &error);

// Patches `payload` so its slot names `requester`. False when the marker is missing.
bool patch_launch_slot(std::string &payload, const std::string &requester);

struct ShortcutSpec
{
    std::string name;          // shown on the home screen
    std::string target;        // title to start
    std::vector<std::string> arguments;
    ui::Pixels icon;           // any size; scaled to 512x512
};

// Creates a home-screen tile title in `root` (ShadowMountPlus registers it). Returns the
// tile's title ID or an empty string with `error` set.
std::string create_shortcut(const ShortcutSpec &spec, const std::string &root, const std::string &receipts_dir,
                            std::string &error);

// Builds the tile's sce_sys/param.json.
std::string shortcut_param_json(const std::string &title_id, const std::string &name);

} // namespace akeno
