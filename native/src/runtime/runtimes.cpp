// Akeno Gaming Hub - universal runtimes: format detection and routing to PS5 runtime titles.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime/runtimes.hpp"

#include "core/fs.hpp"
#include "core/util.hpp"

#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace akeno
{

const std::vector<RuntimeInfo> &runtime_registry()
{
    // Title IDs as listed in the signed ProsperoStore catalog (2026-11-27).
    static const std::vector<RuntimeInfo> registry = {
        {"web", "ProsperoWeb", "PRWB00001", "HTML5- und WebGL-Spiele, Browser-Exporte von Godot, Unity, Ren'Py und RPG Maker MV/MZ"},
        {"windows", "MihawkOS", "PPSA99170", "Windows-Spiele über Wine/GE-Proton und RADV Vulkan", LaunchArgs::MihawkPath},
        {"windows", "PC5", "PPSA11018", "Eigene PC-Spiele über Wine, DXVK und VKD3D-Proton"},
        {"windows", "Prospero Win", "PPSA99995", "Windows-Spiele mit eigener Wine-Kopie, x86-Übersetzer für 32 Bit"},
        {"retro", "RetroArch", "PPSA99169", "NES, SNES, Game Boy, GBA, Mega Drive, Arcade und weitere Kerne"},
        {"retro", "Genesis Plus GX PS5", "PPSA99921", "Mega Drive, Sega CD, Master System, Game Gear"},
        {"retro", "Snes9x PS5", "PPSA99922", "Super Nintendo"},
        {"retro", "fceuxs5", "PPSA51855", "NES"},
        {"arcade", "FBNeo PS5", "PPSA99924", "Arcade (FinalBurn Neo)"},
        {"pce", "Beetle PCE PS5", "PPSA99925", "PC Engine / TurboGrafx-16"},
        {"atari", "Stella PS5", "PPSA99927", "Atari 2600, 5200, 7800, Lynx"},
        {"n64", "Mupen64Plus PS5", "PPSA99064", "Nintendo 64"},
        {"n64", "PSGopher", "PPSA99280", "Nintendo 64 (Gopher64)"},
        {"psx", "PSXS5", "PPSA97510", "PlayStation 1"},
        {"psx", "SwanStationPS5", "PPSA98510", "PlayStation 1"},
        {"ps2", "PS5SX2", "PPSA99203", "PlayStation 2 (PCSX2, Vulkan)"},
        {"ps2", "Crux2", "PPSA99249", "PlayStation 2 (PCSX2)"},
        {"psp", "PSP5", "PPSA99131", "PlayStation Portable (PPSSPP)"},
        {"gamecube", "Porpoise", "PPSA99764", "GameCube und Wii (Dolphin)"},
        {"dreamcast", "PSFlyCast", "PPSA99247", "Dreamcast, NAOMI, Atomiswave"},
        {"saturn", "YmirPS5", "PPSA98567", "Sega Saturn"},
        {"xbox", "XPSemu", "PPSA97358", "Original-Xbox (xemu)"},
        {"xbox360", "PS5X360", "PPSA50011", "Xbox 360 (Xenia Canary)"},
        {"switch", "ProsperoEden", "PPSA99016", "Nintendo Switch (Eden)"},
        {"nds", "PS5-ECAM", "PPSA99360", "Nintendo DS, 3DS, Wii U, Switch"},
        {"flash", "Ruffle Flash", "PPSA68091", "Adobe Flash (SWF)"},
        {"j2me", "Capy Mobile", "PPSA92280", "Java-Handyspiele (J2ME)"},
        {"pico8", "PICO-8 for PS5", "PPSA99808", "Eigene PICO-8-Kopie (Linux-Build)"},
        {"c64", "PS64", "PPSA06464", "Commodore 64 (VICE)"},
    };
    return registry;
}

std::vector<const RuntimeInfo *> runtimes_for(const std::string &family)
{
    std::vector<const RuntimeInfo *> out;
    for (const RuntimeInfo &info : runtime_registry())
        if (family == info.key)
            out.push_back(&info);
    // RetroArch also covers the dedicated families as a fallback.
    if (family == "arcade" || family == "pce" || family == "atari" || family == "n64" || family == "psx")
        for (const RuntimeInfo &info : runtime_registry())
            if (std::strcmp(info.title_id, "PPSA99169") == 0)
                out.push_back(&info);
    return out;
}

const RuntimeInfo *runtime_by_title(const std::string &title_id)
{
    for (const RuntimeInfo &info : runtime_registry())
        if (title_id == info.title_id)
            return &info;
    return nullptr;
}

namespace
{

std::string read_at(const std::string &path, std::uint64_t offset, std::size_t count)
{
    std::string data;
    const int file = ::open(path.c_str(), O_RDONLY);
    if (file < 0)
        return data;
    data.resize(count);
    const ssize_t got = ::pread(file, data.data(), count, static_cast<off_t>(offset));
    ::close(file);
    data.resize(got > 0 ? static_cast<std::size_t>(got) : 0);
    return data;
}

std::uint32_t le32(const std::string &d, std::size_t at)
{
    if (at + 4 > d.size())
        return 0;
    return static_cast<std::uint32_t>(static_cast<unsigned char>(d[at])) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(d[at + 1])) << 8 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(d[at + 2])) << 16 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(d[at + 3])) << 24;
}

Detection match(const char *family, const char *format, const char *evidence, const char *engine = "")
{
    Detection d;
    d.family = family;
    d.format = format;
    d.evidence = evidence;
    d.engine = engine;
    return d;
}

Detection no_runtime(const char *format, const char *engine, const char *advice)
{
    Detection d;
    d.format = format;
    d.engine = engine;
    d.advice = advice;
    d.evidence = "Dateisignatur";
    return d;
}

Detection detect_file(const std::string &path)
{
    const std::string name = lower(fs::leaf(path));
    const std::string ext = name.find('.') == std::string::npos ? "" : name.substr(name.rfind('.'));
    const std::string head = read_at(path, 0, 4096);
    const auto u8 = [&](std::size_t at) { return at < head.size() ? static_cast<unsigned char>(head[at]) : 0u; };

    // Cartridge ROMs: header magic or checksums, not only the extension.
    if (head.size() >= 4 && head.compare(0, 4, "NES\x1a") == 0)
        return match("retro", "NES ROM (iNES)", "iNES-Kopf");
    if ((ext == ".z64" || ext == ".n64" || ext == ".v64") && head.size() >= 4 &&
        (head.compare(0, 4, "\x80\x37\x12\x40") == 0 || head.compare(0, 4, "\x37\x80\x40\x12") == 0 ||
         head.compare(0, 4, "\x40\x12\x37\x80") == 0))
        return match("n64", "Nintendo 64 ROM", "N64-Byte-Order-Signatur");
    if ((ext == ".gb" || ext == ".gbc") && head.size() >= 0x150)
    {
        unsigned sum = 0;
        for (std::size_t i = 0x134; i < 0x14D; ++i)
            sum += u8(i);
        if (((0u - sum - 25u) & 0xFF) == u8(0x14D))
            return match("retro", u8(0x143) & 0x80 ? "Game Boy Color ROM" : "Game Boy ROM", "Header-Prüfsumme");
    }
    if (ext == ".gba" && head.size() >= 0xC0 && u8(0xB2) == 0x96)
        return match("retro", "Game Boy Advance ROM", "GBA-Header");
    if ((ext == ".sfc" || ext == ".smc"))
        return match("retro", "Super Nintendo ROM", "Dateityp .sfc/.smc");
    if (ext == ".md" || ext == ".gen" || ext == ".smd")
        return match("retro", "Mega Drive ROM", "Dateityp");
    if (ext == ".nds" && head.size() >= 0x160 && u8(0x15C) == 0x56 && u8(0x15D) == 0xCF)
        return match("nds", "Nintendo DS ROM", "NDS-Logo-Prüfsumme");
    if (ext == ".pce")
        return match("pce", "PC Engine HuCard", "Dateityp");
    if (ext == ".a26" || ext == ".a78" || ext == ".lnx")
        return match("atari", "Atari ROM", "Dateityp");
    if (ext == ".cso" || (ext == ".iso" && read_at(path, 0x8000 + 8, 8) == "PSP GAME"))
        return match("psp", "PSP-Image", "PSP-Volume-Kennung");
    if (ext == ".iso" || ext == ".gcm" || ext == ".rvz" || ext == ".wbfs")
    {
        if (head.size() >= 0x20 && le32(head, 0x1C) == 0x3D9F33C2u)
            return match("gamecube", "GameCube-Disc-Image", "GameCube-Disc-Header");
        if (head.size() >= 0x1C && le32(head, 0x18) == 0xA39E1C5Du)
            return match("gamecube", "Wii-Disc-Image", "Wii-Disc-Header");
        if (ext == ".rvz" || ext == ".wbfs")
            return match("gamecube", "GameCube/Wii-Image", "Dateityp");
        if (read_at(path, 0xFD90000 + 0x10000, 20) == "MICROSOFT*XBOX*MEDIA" ||
            read_at(path, 0x2080000 + 0x10000, 20) == "MICROSOFT*XBOX*MEDIA")
            return match("xbox360", "Xbox-360-Disc-Image (XGD2/XGD3)", "XDVDFS-Volume im Spielbereich");
        if (read_at(path, 32 * 2048, 20) == "MICROSOFT*XBOX*MEDIA")
            return match("xbox", "Xbox-Disc-Image (XDVDFS)", "XDVDFS-Volume");
        const std::string pvd = read_at(path, 16 * 2048, 2048);
        if (pvd.size() > 40 && pvd.compare(1, 5, "CD001") == 0)
        {
            const std::string system = pvd.substr(8, 32);
            if (system.find("PLAYSTATION") != std::string::npos)
            {
                // PS2 discs carry SYSTEM.CNF with BOOT2; PS1 with BOOT. Read the root names only.
                const std::string area = read_at(path, 0, 2 * 1024 * 1024);
                if (area.find("BOOT2") != std::string::npos)
                    return match("ps2", "PlayStation-2-Disc-Image", "ISO 9660 + SYSTEM.CNF BOOT2");
                return match("psx", "PlayStation-Disc-Image", "ISO 9660 System PLAYSTATION");
            }
            return no_runtime("ISO-9660-Image", "", "Unbekanntes Disc-System. Recomp-Titel aus RecompStore nennen das benötigte Original.");
        }
    }
    if (ext == ".cue" || ext == ".chd" || ext == ".pbp")
        return match("psx", "CD-Image (cue/chd/pbp)", "Dateityp");
    if (ext == ".gdi" || ext == ".cdi")
        return match("dreamcast", "Dreamcast-Image", "Dateityp");
    if (ext == ".xci" && head.size() >= 0x104 && head.compare(0x100, 4, "HEAD") == 0)
        return match("switch", "Switch-Cartridge-Image", "XCI-Header");
    if (ext == ".nsp" && head.compare(0, 4, "PFS0") == 0)
        return match("switch", "Switch-Paket", "PFS0-Header");
    if (ext == ".xex")
        return match("xbox360", "Xbox-360-Titel", "Dateityp");
    if (ext == ".swf" && head.size() >= 3 &&
        (head.compare(0, 3, "FWS") == 0 || head.compare(0, 3, "CWS") == 0 || head.compare(0, 3, "ZWS") == 0))
        return match("flash", "Flash-Film (SWF)", "SWF-Signatur");
    if (ext == ".p8" || ends_with(name, ".p8.png"))
        return match("pico8", "PICO-8-Cartridge", "Dateityp");
    if (ext == ".d64" || ext == ".t64" || ext == ".prg" || ext == ".crt")
        return match("c64", "Commodore-64-Image", "Dateityp");
    if (ext == ".jad" || (ext == ".jar" && head.compare(0, 2, "PK") == 0 &&
                          read_at(path, 0, 256 * 1024).find("MIDlet-") != std::string::npos))
        return match("j2me", "Java-ME-Spiel (MIDlet)", "MIDlet-Manifest");
    if (ext == ".jar" && head.compare(0, 2, "PK") == 0)
        return no_runtime("Java-Desktop-JAR", "Java",
                          "Für allgemeine Java-Desktop-Spiele gibt es noch keinen PS5-Starter. ProsperoCraft "
                          "bringt eine Java-25-Runtime nur für Minecraft mit.");
    if (head.size() >= 64 && head.compare(0, 2, "MZ") == 0)
    {
        const std::uint32_t pe = le32(head, 0x3C);
        const std::string coff = read_at(path, pe, 6);
        if (coff.size() == 6 && coff.compare(0, 4, std::string("PE\0\0", 4)) == 0)
        {
            const unsigned machine = static_cast<unsigned char>(coff[4]) | static_cast<unsigned char>(coff[5]) << 8;
            return match("windows", machine == 0x8664 ? "Windows-Programm (x86-64)" : "Windows-Programm (x86)",
                         "PE-Signatur");
        }
    }
    if (head.size() >= 20 && head.compare(0, 4, "\x7f" "ELF") == 0)
        return no_runtime("Linux-ELF", "Linux",
                          "Für allgemeine Linux-Programme gibt es keine PS5-Runtime. Einzelne Ports (PokeMMO, "
                          "PICO-8) bringen eigene Shims mit.");
    if (head.size() >= 4 && head.compare(0, 4, "GDPC") == 0)
        return no_runtime("Godot-Paket (PCK)", "Godot",
                          "Godot läuft auf der PS5 über einen Web-Export (ProsperoWeb) oder einen Windows-Export (MihawkOS).");
    if (ext == ".rpa" || ext == ".rpyc")
        return no_runtime("Ren'Py-Archiv", "Ren'Py",
                          "Ren'Py-Spiele laufen über ihren Web-Export (ProsperoWeb) oder den Windows-Build (MihawkOS).");
    if ((ext == ".html" || ext == ".htm") && contains_folded(head, "<html"))
        return match("web", "HTML5-Spiel", "HTML-Dokument");
    return {};
}

} // namespace

Detection detect_game(const std::string &path)
{
    if (!fs::is_dir(path))
        return detect_file(path);
    // Engine layouts in a folder.
    if (fs::is_file(fs::join(path, "www/js/rpg_core.js")) || fs::is_file(fs::join(path, "js/rpg_core.js")))
    {
        Detection d = match("web", "RPG-Maker-MV-Projekt", "js/rpg_core.js", "RPG Maker MV");
        return d;
    }
    if (fs::is_file(fs::join(path, "js/rmmz_core.js")) || fs::is_file(fs::join(path, "www/js/rmmz_core.js")))
        return match("web", "RPG-Maker-MZ-Projekt", "js/rmmz_core.js", "RPG Maker MZ");
    if (fs::is_file(fs::join(path, "RPG_RT.ldb")))
        return no_runtime("RPG-Maker-2000/2003-Projekt", "RPG Maker 2000/2003",
                          "EasyRPG Player ist für die PS5 noch nicht im Katalog; der Windows-Build läuft über MihawkOS.");
    if (fs::is_dir(fs::join(path, "renpy")) || fs::is_dir(fs::join(path, "game")))
        for (const fs::Entry &entry : fs::list(fs::join(path, "game")))
            if (ends_with(entry.name, ".rpa") || ends_with(entry.name, ".rpy") || ends_with(entry.name, ".rpyc"))
                return no_runtime("Ren'Py-Spiel", "Ren'Py",
                                  "Visual Novels mit Ren'Py laufen über den Web-Export (ProsperoWeb) oder den "
                                  "Windows-Build (MihawkOS).");
    if (fs::is_file(fs::join(path, "index.html")))
    {
        const std::string head = read_at(fs::join(path, "index.html"), 0, 8192);
        Detection d = match("web", "HTML5-Spiel", "index.html");
        if (contains_folded(head, "godot"))
            d.engine = "Godot (Web-Export)";
        else if (contains_folded(head, "unityloader") || contains_folded(head, "createunityinstance"))
            d.engine = "Unity (WebGL)";
        else if (contains_folded(head, "renpy"))
            d.engine = "Ren'Py (Web)";
        return d;
    }
    // A Windows game folder: the largest .exe decides.
    std::string best;
    std::uint64_t best_size = 0;
    for (const fs::Entry &entry : fs::list(path))
        if (!entry.directory && ends_with(lower(entry.name), ".exe"))
        {
            const auto size = fs::file_size(fs::join(path, entry.name)).value_or(0);
            if (size > best_size)
            {
                best = fs::join(path, entry.name);
                best_size = size;
            }
        }
    if (!best.empty())
    {
        Detection d = detect_file(best);
        if (fs::is_dir(fs::join(path, fs::leaf(best).substr(0, fs::leaf(best).size() - 4) + "_Data")))
            d.engine = "Unity (Windows)";
        return d;
    }
    for (const fs::Entry &entry : fs::list(path))
        if (!entry.directory)
        {
            Detection d = detect_file(fs::join(path, entry.name));
            if (!d.family.empty())
                return d;
        }
    return {};
}

std::vector<LocalGame> scan_local_games(const std::vector<std::string> &folders)
{
    std::vector<LocalGame> games;
    for (const std::string &folder : folders)
        for (const fs::Entry &entry : fs::list(folder))
        {
            if (entry.name.empty() || entry.name[0] == '.' || games.size() >= 500)
                continue;
            LocalGame game;
            game.path = fs::join(folder, entry.name);
            game.name = entry.name;
            game.detection = detect_game(game.path);
            if (!entry.directory)
                game.size = fs::file_size(game.path).value_or(0);
            if (!game.detection.format.empty())
                games.push_back(std::move(game));
        }
    return games;
}

} // namespace akeno
