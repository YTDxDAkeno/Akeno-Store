// Akeno Gaming Hub - Linux development host.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The host build runs the same catalog, installer and UI code. Installs go into a
// simulated ShadowMountPlus tree under the data folder; nothing is launched.
#include "platform/platform.hpp"

#include "core/fs.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>

namespace akeno::platform
{
namespace
{
std::string data_root()
{
    if (const char *forced = std::getenv("AKENO_DATA_DIR"); forced && *forced)
        return forced;
    if (const char *xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        return fs::join(xdg, "akeno-native");
    const char *home = std::getenv("HOME");
    return fs::join(home ? home : "/tmp", ".local/share/akeno-native");
}
std::mutex log_mutex;
} // namespace

void init() { fs::make_dirs(data_dir()); }
void shutdown() {}
const char *name() { return "Linux-Host"; }
bool is_console() { return false; }
std::string data_dir() { return data_root(); }
std::string cache_dir() { return fs::join(data_root(), "cache"); }

std::vector<InstallRoot> install_roots()
{
    const std::string root = fs::join(data_root(), "simulated-ps5/data/homebrew");
    fs::make_dirs(root);
    return {{root, "Simulierter Ordner /data/homebrew", fs::writable(root)}};
}

std::vector<std::string> import_dirs()
{
    const std::string dir = fs::join(data_root(), "simulated-ps5/data/akeno/games");
    fs::make_dirs(dir);
    return {dir};
}

void log(const char *format, ...)
{
    std::lock_guard lock(log_mutex);
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
}

std::uint64_t monotonic_ms()
{
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::uint64_t>(now.tv_sec) * 1000 + static_cast<std::uint64_t>(now.tv_nsec) / 1000000;
}

std::int64_t unix_time() { return static_cast<std::int64_t>(std::time(nullptr)); }

bool prefers_german()
{
    const char *lang = std::getenv("LANG");
    return !lang || std::string(lang).rfind("en", 0) != 0;
}

void first_frame_shown() {}

[[noreturn]] void quit() { std::exit(0); }

bool send_to_loader(const std::string &elf_path, std::string &error)
{
    (void)elf_path;
    error = "Auf dem Linux-Host gibt es keinen PS5-ELF-Loader (Start nur auf der Konsole)";
    return false;
}

} // namespace akeno::platform
