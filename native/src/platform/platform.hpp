// Akeno Gaming Hub - what differs between the Linux host and the PS5 title.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace akeno::platform
{

struct InstallRoot
{
    std::string path;  // e.g. /data/homebrew
    std::string label; // shown in settings
    bool writable = false;
};

void init();
void shutdown();

const char *name();       // "PS5" or "Linux"
bool is_console();
std::string data_dir();   // persistent settings, caches, receipts, logs
std::string cache_dir();  // artwork and catalog caches
// Folders ShadowMountPlus scans for title folders (host: a simulated tree).
std::vector<InstallRoot> install_roots();
// Where the user drops ROMs, ISOs and other own game files for import.
std::vector<std::string> import_dirs();

// System software version, e.g. "10.01" (host: "Linux-Host"); empty when unknown.
std::string firmware_version();
// The last log lines (newest last), for the diagnostics screen.
std::vector<std::string> recent_log(std::size_t lines);

void log(const char *format, ...) __attribute__((format(printf, 1, 2)));
std::uint64_t monotonic_ms();
std::int64_t unix_time();

// Console language (1 = English, 4 = German, ...); the host reads $LANG.
bool prefers_german();

// Hide the boot splash once the first frame is drawn (PS5).
void first_frame_shown();

// Ends the title the way the console expects (PS5: LoadExec "exit", never return).
[[noreturn]] void quit();

// Replaces this process with Akeno's own title and `arguments` (PS5: sceSystemServiceLoadExec
// of /app0/eboot.bin, as Prospero Win and MihawkOS restart into a game; host: execv).
// Returns only when the restart failed.
void restart(const std::vector<std::string> &arguments);

// Sends a payload ELF to the console's ELF loader (127.0.0.1:9021). Host: refused.
bool send_to_loader(const std::string &elf_path, std::string &error);

} // namespace akeno::platform
