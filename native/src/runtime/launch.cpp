// Akeno Gaming Hub - starting titles and home-screen shortcuts through the ELF loader.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime/launch.hpp"

#include "core/fs.hpp"
#include "core/json.hpp"
#include "core/util.hpp"
#include "install/library.hpp"
#include "platform/platform.hpp"

#include <cstdlib>
#include <cstring>

namespace akeno
{
namespace
{
// Files packaged in Akeno's own title folder.
std::string asset(const std::string &relative)
{
    if (platform::is_console())
        return "/app0/assets/" + relative;
    const char *forced = std::getenv("AKENO_ASSETS");
    return std::string(forced && *forced ? forced : AKENO_ASSET_DIR) + "/" + relative;
}

bool write_arguments(const std::string &path, const std::string &target, const std::vector<std::string> &arguments,
                     std::string &error)
{
    std::string text = "AKENO-LAUNCH 1\n" + target + "\n";
    for (const std::string &argument : arguments)
    {
        if (argument.size() > 500 || argument.find('\n') != std::string::npos)
        {
            error = "Ein Startargument ist ungültig";
            return false;
        }
        text += argument + "\n";
    }
    if (!fs::make_dirs(fs::parent(path)) || !fs::write_atomic(path, text))
    {
        error = "Die Startanfrage kann nicht gespeichert werden (" + path + ")";
        return false;
    }
    return true;
}
} // namespace

bool patch_launch_slot(std::string &payload, const std::string &requester)
{
    const std::size_t at = payload.find(kLaunchSlotMarker);
    if (at == std::string::npos || !is_title_id(requester))
        return false;
    std::memset(payload.data() + at, 0, std::strlen(kLaunchSlotMarker));
    std::memcpy(payload.data() + at, requester.data(), requester.size());
    return true;
}

bool request_launch(const std::string &target, const std::vector<std::string> &arguments, std::string &error)
{
    if (!is_title_id(target))
    {
        error = "Ungültige Title-ID";
        return false;
    }
    if (!platform::is_console())
    {
        error = "Titel starten geht nur auf der PS5 (Host-Build)";
        return false;
    }
    if (!fs::is_dir("/data"))
    {
        error = "Kein Zugriff auf /data: ShadowMountPlus 1.7+ wird für Starts aus Akeno benötigt";
        return false;
    }
    const std::string request = "/data/akeno/launch/" + std::string(kAkenoTitleId) + ".txt";
    if (!write_arguments(request, target, arguments, error))
        return false;
    auto payload = fs::read_file(asset("payload/akeno-launch.elf"), 8 * 1024 * 1024);
    if (!payload || !patch_launch_slot(*payload, kAkenoTitleId))
    {
        error = "Die Start-Payload fehlt oder ist beschädigt";
        return false;
    }
    const std::string staged = "/data/akeno/launch/akeno-launch.elf";
    if (!fs::write_atomic(staged, *payload))
    {
        error = "Die Start-Payload kann nicht vorbereitet werden";
        return false;
    }
    return platform::send_to_loader(staged, error);
}

std::string shortcut_param_json(const std::string &title_id, const std::string &name)
{
    // The shape of the boilerplate's hardware-tested sce_sys/param.json (fw 6.02 and 12.70).
    const std::string content = "UP9000-" + title_id + "_00-AKENOSHORTCUT" + title_id.substr(6, 3);
    return "{\n"
           "  \"ageLevel\": {\"default\": 0},\n"
           "  \"applicationCategoryType\": 0,\n"
           "  \"applicationDrmType\": \"free\",\n"
           "  \"attribute\": 0,\n"
           "  \"attribute2\": 0,\n"
           "  \"attribute3\": 0,\n"
           "  \"conceptId\": \"" + title_id.substr(4) + "\",\n"
           "  \"contentBadgeType\": 1,\n"
           "  \"contentId\": " + json::quote(content) + ",\n"
           "  \"contentVersion\": \"01.000.000\",\n"
           "  \"downloadDataSize\": 0,\n"
           "  \"gameIntent\": {\"permittedIntents\": [{\"intentType\": \"launchActivity\"}]},\n"
           "  \"localizedParameters\": {\"defaultLanguage\": \"en-US\", \"en-US\": {\"titleName\": " +
           json::quote(sanitize_text(name, 60)) + "}},\n"
           "  \"masterVersion\": \"01.00\",\n"
           "  \"requiredSystemSoftwareVersion\": \"0x0000000000000000\",\n"
           "  \"sdkVersion\": \"0x0000000000000000\",\n"
           "  \"titleId\": \"" + title_id + "\",\n"
           "  \"versionFileUri\": \"\"\n"
           "}\n";
}

std::string create_shortcut(const ShortcutSpec &spec, const std::string &root, const std::string &receipts_dir,
                            std::string &error)
{
    if (!is_title_id(spec.target))
    {
        error = "Ungültiges Ziel für die Verknüpfung";
        return {};
    }
    std::string tile;
    for (int number = kShortcutFirst; number <= kShortcutLast; ++number)
    {
        const std::string candidate = "PPSA" + std::to_string(number);
        if (!fs::exists(fs::join(root, candidate)))
        {
            tile = candidate;
            break;
        }
    }
    if (tile.empty())
    {
        error = "Alle 100 Verknüpfungsplätze sind belegt";
        return {};
    }
    const auto eboot = fs::read_file(asset("tile/eboot.bin"), 16 * 1024 * 1024);
    const auto runtime = fs::read_file(asset("tile/libc.prx"), 16 * 1024 * 1024);
    const auto payload = fs::read_file(asset("payload/akeno-launch.elf"), 8 * 1024 * 1024);
    if (!eboot || !runtime || !payload)
    {
        error = "Die Kachel-Vorlage fehlt im Akeno-Paket";
        return {};
    }
    const std::string staging = fs::join(fs::parent(root), "akeno-work/tile-" + tile);
    fs::remove_tree(staging);
    ui::Pixels icon = ui::cover_crop(spec.icon, 512, 512, 0);
    const bool ok = fs::make_dirs(fs::join(staging, "sce_sys")) && fs::make_dirs(fs::join(staging, "sce_module")) &&
                    fs::write_atomic(fs::join(staging, "eboot.bin"), *eboot) &&
                    fs::write_atomic(fs::join(staging, "sce_module/libc.prx"), *runtime) &&
                    fs::write_atomic(fs::join(staging, "akeno-launch.elf"), *payload) &&
                    fs::write_atomic(fs::join(staging, "sce_sys/param.json"), shortcut_param_json(tile, spec.name)) &&
                    fs::write_atomic(fs::join(staging, "sce_sys/icon0.png"), ui::encode_png(icon));
    std::string forward_error;
    if (!ok || !write_arguments(fs::join(staging, "forward.txt"), spec.target, spec.arguments, forward_error))
    {
        fs::remove_tree(staging);
        error = forward_error.empty() ? "Die Verknüpfung kann nicht geschrieben werden" : forward_error;
        return {};
    }
    const std::string destination = fs::join(root, tile);
    if (!fs::rename(staging, destination))
    {
        fs::remove_tree(staging);
        error = "Die Verknüpfung kann nicht aktiviert werden";
        return {};
    }
    fs::chmod_tree(destination, 0777);
    Library(receipts_dir).write_receipt(tile, "shortcut:" + spec.target, "01.000.000", "", destination, true);
    return tile;
}

} // namespace akeno
