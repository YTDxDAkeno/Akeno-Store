// Akeno Gaming Hub - integrated runtimes and format detection.
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

const std::vector<RuntimeInfo> &akeno_runtimes()
{
    static const std::vector<RuntimeInfo> runtimes = {
        {"native", "Native PS5-Titel", "Ports und Recompilations aus dem signierten ProsperoStore und RecompStore",
         "ProsperoStore-Katalogvertrag (Ed25519), RecompStore", "Installieren, aktualisieren und direkt starten",
         RuntimeState::Integrated},
        {"easyrpg", "RPG Maker 2000/2003", "Spiele aus RPG Maker 2000 und 2003 (RPG_RT.ldb)",
         "EasyRPG Player 0.8.1.1 und liblcf 0.8.1 (GPL-3.0 / MIT), in Akeno eingebaut",
#ifdef AKENO_WITH_EASYRPG
         "In Akeno eingebaut: Spielmodus im Vollbild, DualSense, WAV- und MIDI-Ton. Noch nicht: MP3/OGG-Musik, "
         "Texte außer Windows-1252 (japanische Originale)",
         RuntimeState::Integrated
#else
         "In diesem Build nicht enthalten", RuntimeState::Planned
#endif
        },
        {"windows", "Windows x86/x64", "Windows-Spiele (PE32/PE64), Unity-Mono/IL2CPP- und GameMaker-Windows-Builds",
         "Wine im eigenen Titel nach dem Aufbau von Prospero Win (LGPL-2.1): ntdll.prx, Wine-Treiber für VideoOut, "
         "Pad und Audio, i386-Übersetzer, DXVK/Zink",
         "Geplant: braucht die Wine-PRX-Module (rund 170 MB) und ihre Erprobung auf der Konsole", RuntimeState::Planned},
        {"linux", "Linux x86-64 (ELF)", "Linux-Builds von Spielen, Godot- und Unity-Linux-Exporte",
         "ELF-Loader mit Linux-ABI nach PokeMMO Prospero (GPL-3.0) und PokeMMO-NX (MIT), OpenGL über ps5-opengl (Mesa)",
         "Geplant: der Loader deckt bisher die Bibliotheken eines einzelnen Spiels (PokeMMO) ab", RuntimeState::Planned},
        {"java", "Java / JVM / LWJGL", "Java-Desktop-Spiele (JAR), libGDX- und LWJGL-Spiele",
         "OpenJDK-25-Port für die PS5 (ps5-java25-homebrew, GPL-2.0 mit Classpath-Ausnahme), Interpreter-Modus",
         "Geplant: der Port läuft auf der Konsole, die Einbettung in fremde Titel ist dort noch nicht abgeschlossen",
         RuntimeState::Planned},
        {"html5", "HTML5 / JavaScript / WebAssembly", "Heruntergeladene Web-Spiele, RPG Maker MV/MZ, Web-Exporte",
         "WebKit-Engine ohne Browser-Oberfläche, wie in ProsperoWeb und Parallax Browser (LGPL/BSD, GPL-3.0)",
         "Geplant: die Engine muss für Akenos Spielmodus gebaut werden", RuntimeState::Planned},
        {"godot", "Godot", "Godot-Spiele (.pck)", "Godot-Engine (MIT) mit OpenGL ES über ps5-opengl",
         "Geplant", RuntimeState::Planned},
        {"renpy", "Ren'Py / Python", "Visual Novels mit Ren'Py, Python-Spiele",
         "CPython (PSF), pygame_sdl2 (zlib), Ren'Py (MIT); braucht OpenGL ES über ps5-opengl",
         "Geplant", RuntimeState::Planned},
        {"dotnet", ".NET / Mono", ".NET-Spiele (MonoGame, FNA)", "Mono (MIT); unter Windows-Builds über die Windows-Runtime",
         "Geplant", RuntimeState::Planned},
        {"retro", "Konsolen-ROMs und Disc-Images", "Eigene Dumps von NES bis PS2",
         "libretro-Kerne im Spielmodus (GPL); Recomp-Ports aus RecompStore bleiben native Titel",
         "Geplant", RuntimeState::Planned},
    };
    return runtimes;
}

const RuntimeInfo *runtime_by_id(const std::string &id)
{
    for (const RuntimeInfo &info : akeno_runtimes())
        if (id == info.id)
            return &info;
    return nullptr;
}

bool runtime_integrated(const std::string &id)
{
    const RuntimeInfo *info = runtime_by_id(id);
    return info && info->state == RuntimeState::Integrated;
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

// engine id, runtime id, format, evidence, readable engine name
Detection match(const char *engine, const char *runtime, const char *format, const char *evidence,
                const char *engine_name = "")
{
    Detection d;
    d.engine = engine;
    d.runtime = runtime;
    d.format = format;
    d.evidence = evidence;
    d.engine_name = *engine_name ? engine_name : format;
    d.playable = runtime_integrated(runtime);
    if (!d.playable)
    {
        const RuntimeInfo *info = runtime_by_id(runtime);
        d.advice = info ? std::string("Akeno-Runtime \"") + info->name + "\": " + info->detail
                        : std::string("Für dieses Format ist keine Akeno-Runtime vorgesehen.");
    }
    return d;
}

Detection rom(const char *system, const char *evidence) { return match("rom", "retro", system, evidence); }

// The folder (this one or up to three levels below) that holds RPG_RT.ldb.
std::string rpgmaker_root(const std::string &path, int depth = 0)
{
    if (fs::is_file(fs::join(path, "RPG_RT.ldb")) || fs::is_file(fs::join(path, "rpg_rt.ldb")))
        return path;
    if (depth >= 3)
        return {};
    for (const fs::Entry &entry : fs::list(path))
        if (entry.directory && entry.name[0] != '.' && entry.name != "__MACOSX")
            if (std::string found = rpgmaker_root(fs::join(path, entry.name), depth + 1); !found.empty())
                return found;
    return {};
}

Detection detect_file(const std::string &path)
{
    const std::string name = lower(fs::leaf(path));
    const std::string ext = name.find('.') == std::string::npos ? "" : name.substr(name.rfind('.'));
    const std::string head = read_at(path, 0, 4096);
    const auto u8 = [&](std::size_t at) { return at < head.size() ? static_cast<unsigned char>(head[at]) : 0u; };

    // Cartridge ROMs: header magic or checksums, not only the extension.
    if (head.size() >= 4 && head.compare(0, 4, "NES\x1a") == 0)
        return rom("NES ROM (iNES)", "iNES-Kopf");
    if ((ext == ".z64" || ext == ".n64" || ext == ".v64") && head.size() >= 4 &&
        (head.compare(0, 4, "\x80\x37\x12\x40") == 0 || head.compare(0, 4, "\x37\x80\x40\x12") == 0 ||
         head.compare(0, 4, "\x40\x12\x37\x80") == 0))
        return rom("Nintendo 64 ROM", "N64-Byte-Order-Signatur");
    if ((ext == ".gb" || ext == ".gbc") && head.size() >= 0x150)
    {
        unsigned sum = 0;
        for (std::size_t i = 0x134; i < 0x14D; ++i)
            sum += u8(i);
        if (((0u - sum - 25u) & 0xFF) == u8(0x14D))
            return rom(u8(0x143) & 0x80 ? "Game Boy Color ROM" : "Game Boy ROM", "Header-Prüfsumme");
    }
    if (ext == ".gba" && head.size() >= 0xC0 && u8(0xB2) == 0x96)
        return rom("Game Boy Advance ROM", "GBA-Header");
    if ((ext == ".sfc" || ext == ".smc"))
        return rom("Super Nintendo ROM", "Dateityp .sfc/.smc");
    if (ext == ".md" || ext == ".gen" || ext == ".smd")
        return rom("Mega Drive ROM", "Dateityp");
    if (ext == ".nds" && head.size() >= 0x160 && u8(0x15C) == 0x56 && u8(0x15D) == 0xCF)
        return rom("Nintendo DS ROM", "NDS-Logo-Prüfsumme");
    if (ext == ".pce")
        return rom("PC Engine HuCard", "Dateityp");
    if (ext == ".a26" || ext == ".a78" || ext == ".lnx")
        return rom("Atari ROM", "Dateityp");
    if (ext == ".cso" || (ext == ".iso" && read_at(path, 0x8000 + 8, 8) == "PSP GAME"))
        return rom("PSP-Image", "PSP-Volume-Kennung");
    if (ext == ".iso" || ext == ".gcm" || ext == ".rvz" || ext == ".wbfs")
    {
        if (head.size() >= 0x20 && le32(head, 0x1C) == 0x3D9F33C2u)
            return rom("GameCube-Disc-Image", "GameCube-Disc-Header");
        if (head.size() >= 0x1C && le32(head, 0x18) == 0xA39E1C5Du)
            return rom("Wii-Disc-Image", "Wii-Disc-Header");
        if (ext == ".rvz" || ext == ".wbfs")
            return rom("GameCube/Wii-Image", "Dateityp");
        if (read_at(path, 0xFD90000 + 0x10000, 20) == "MICROSOFT*XBOX*MEDIA" ||
            read_at(path, 0x2080000 + 0x10000, 20) == "MICROSOFT*XBOX*MEDIA")
            return rom("Xbox-360-Disc-Image (XGD2/XGD3)", "XDVDFS-Volume im Spielbereich");
        if (read_at(path, 32 * 2048, 20) == "MICROSOFT*XBOX*MEDIA")
            return rom("Xbox-Disc-Image (XDVDFS)", "XDVDFS-Volume");
        const std::string pvd = read_at(path, 16 * 2048, 2048);
        if (pvd.size() > 40 && pvd.compare(1, 5, "CD001") == 0)
        {
            const std::string system = pvd.substr(8, 32);
            if (system.find("PLAYSTATION") != std::string::npos)
            {
                // PS2 discs carry SYSTEM.CNF with BOOT2; PS1 with BOOT. Read the root names only.
                const std::string area = read_at(path, 0, 2 * 1024 * 1024);
                if (area.find("BOOT2") != std::string::npos)
                    return rom("PlayStation-2-Disc-Image", "ISO 9660 + SYSTEM.CNF BOOT2");
                return rom("PlayStation-Disc-Image", "ISO 9660 System PLAYSTATION");
            }
            return rom("ISO-9660-Image (unbekanntes System)", "ISO 9660");
        }
    }
    if (ext == ".cue" || ext == ".chd" || ext == ".pbp")
        return rom("CD-Image (cue/chd/pbp)", "Dateityp");
    if (ext == ".gdi" || ext == ".cdi")
        return rom("Dreamcast-Image", "Dateityp");
    if (ext == ".xci" && head.size() >= 0x104 && head.compare(0x100, 4, "HEAD") == 0)
        return rom("Switch-Cartridge-Image", "XCI-Header");
    if (ext == ".nsp" && head.compare(0, 4, "PFS0") == 0)
        return rom("Switch-Paket", "PFS0-Header");
    if (ext == ".xex")
        return rom("Xbox-360-Titel", "Dateityp");
    if (ext == ".swf" && head.size() >= 3 &&
        (head.compare(0, 3, "FWS") == 0 || head.compare(0, 3, "CWS") == 0 || head.compare(0, 3, "ZWS") == 0))
        return match("flash", "", "Flash-Film (SWF)", "SWF-Signatur", "Adobe Flash");
    if (ext == ".p8" || ends_with(name, ".p8.png"))
        return match("pico8", "", "PICO-8-Cartridge", "Dateityp", "PICO-8");
    if (ext == ".d64" || ext == ".t64" || ext == ".prg" || ext == ".crt")
        return rom("Commodore-64-Image", "Dateityp");
    if (ext == ".jad" || (ext == ".jar" && head.compare(0, 2, "PK") == 0 &&
                          read_at(path, 0, 256 * 1024).find("MIDlet-") != std::string::npos))
        return match("j2me", "java", "Java-ME-Spiel (MIDlet)", "MIDlet-Manifest", "Java ME");
    if (ext == ".jar" && head.compare(0, 2, "PK") == 0)
        return match("java", "java", "Java-Desktop-JAR", "ZIP mit Java-Klassen", "Java");
    if (head.size() >= 64 && head.compare(0, 2, "MZ") == 0)
    {
        const std::uint32_t pe = le32(head, 0x3C);
        const std::string coff = read_at(path, pe, 6);
        if (coff.size() == 6 && coff.compare(0, 4, std::string("PE\0\0", 4)) == 0)
        {
            const unsigned machine = static_cast<unsigned char>(coff[4]) | static_cast<unsigned char>(coff[5]) << 8;
            return match(machine == 0x8664 ? "windows-x64" : "windows-x86", "windows",
                         machine == 0x8664 ? "Windows-Programm (x86-64)" : "Windows-Programm (x86)", "PE-Signatur",
                         "Windows");
        }
    }
    if (head.size() >= 20 && head.compare(0, 4, "\x7f" "ELF") == 0)
        return match("linux-elf", "linux", "Linux-Programm (ELF)", "ELF-Kopf", "Linux");
    if (head.size() >= 4 && head.compare(0, 4, "GDPC") == 0)
        return match("godot", "godot", "Godot-Paket (PCK)", "GDPC-Kopf", "Godot");
    if (ext == ".rpa" || ext == ".rpyc")
        return match("renpy", "renpy", "Ren'Py-Archiv", "Dateityp", "Ren'Py");
    if ((ext == ".html" || ext == ".htm") && contains_folded(head, "<html"))
        return match("html5", "html5", "HTML5-Spiel", "HTML-Dokument", "HTML5");
    return {};
}

} // namespace

Detection detect_game(const std::string &path)
{
    if (!fs::is_dir(path))
        return detect_file(path);
    // Engine layouts in a folder (also one to three levels down, as in most download ZIPs).
    if (const std::string root = rpgmaker_root(path); !root.empty())
    {
        Detection d = match("rpgmaker2k", "easyrpg", "RPG-Maker-2000/2003-Spiel", "RPG_RT.ldb", "RPG Maker 2000/2003");
        d.root = root;
        return d;
    }
    for (const char *base : {"", "www/"})
    {
        if (fs::is_file(fs::join(path, std::string(base) + "js/rpg_core.js")))
            return match("rpgmakermv", "html5", "RPG-Maker-MV-Spiel", "js/rpg_core.js", "RPG Maker MV");
        if (fs::is_file(fs::join(path, std::string(base) + "js/rmmz_core.js")))
            return match("rpgmakermz", "html5", "RPG-Maker-MZ-Spiel", "js/rmmz_core.js", "RPG Maker MZ");
    }
    if (fs::is_dir(fs::join(path, "renpy")) || fs::is_dir(fs::join(path, "game")))
        for (const fs::Entry &entry : fs::list(fs::join(path, "game")))
            if (ends_with(entry.name, ".rpa") || ends_with(entry.name, ".rpy") || ends_with(entry.name, ".rpyc"))
                return match("renpy", "renpy", "Ren'Py-Spiel", "game/*.rpa", "Ren'Py");
    if (fs::is_file(fs::join(path, "index.html")))
    {
        const std::string head = read_at(fs::join(path, "index.html"), 0, 8192);
        Detection d = match("html5", "html5", "HTML5-Spiel", "index.html", "HTML5");
        if (contains_folded(head, "godot"))
            d.engine_name = "Godot (Web-Export)";
        else if (contains_folded(head, "unityloader") || contains_folded(head, "createunityinstance"))
            d.engine_name = "Unity (WebGL)";
        else if (contains_folded(head, "renpy"))
            d.engine_name = "Ren'Py (Web)";
        return d;
    }
    for (const fs::Entry &entry : fs::list(path))
        if (!entry.directory && ends_with(lower(entry.name), ".pck"))
            if (Detection d = detect_file(fs::join(path, entry.name)); d.engine == "godot")
                return d;
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
            d.engine_name = "Unity (Windows)";
        else if (fs::is_file(fs::join(path, "data.win")))
            d.engine_name = "GameMaker (Windows)";
        return d;
    }
    for (const fs::Entry &entry : fs::list(path))
        if (!entry.directory)
        {
            Detection d = detect_file(fs::join(path, entry.name));
            if (!d.engine.empty())
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
            if (!game.detection.engine.empty())
                games.push_back(std::move(game));
        }
    return games;
}

} // namespace akeno
