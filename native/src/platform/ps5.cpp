// Akeno Gaming Hub - PS5 native title platform layer.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Paths follow the ShadowMountPlus layout ProsperoStore documents: titles live in
// /data/homebrew (and the same folder on extended and USB drives). ShadowMountPlus 1.7+
// mounts /data into a title's sandbox; without it Akeno keeps its data in /download0
// and offers browsing only.
#include "platform/platform.hpp"

#include "core/fs.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <fcntl.h>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

extern "C"
{
    int sceKernelDebugOutText(int channel, const char *text);
    int sceKernelUsleep(unsigned int microseconds);
    int sceSystemServiceHideSplashScreen(void);
    int sceSystemServiceLoadExec(const char *path, const char **arguments);
    int sceSystemServiceParamGetInt(int id, int *value);
    int sceKernelGetProsperoSystemSwVersion(void *version);
}

namespace akeno::platform
{
namespace
{
std::mutex log_mutex;
std::deque<std::string> log_lines;
std::string data_root;
FILE *log_file = nullptr;
} // namespace

void init()
{
    data_root = fs::writable("/data") ? "/data/akeno" : "/download0/akeno";
    fs::make_dirs(data_root);
    const std::string log_path = fs::join(data_root, "akeno.log");
    fs::rename(log_path, fs::join(data_root, "akeno.prev.log"));
    log_file = std::fopen(log_path.c_str(), "w");
    if (log_file)
        std::setvbuf(log_file, nullptr, _IOLBF, 0);
    log("[akeno] data folder %s", data_root.c_str());
}

void shutdown()
{
    if (log_file)
        std::fclose(log_file);
    log_file = nullptr;
}

const char *name() { return "PS5"; }
bool is_console() { return true; }
std::string data_dir() { return data_root; }
std::string cache_dir() { return fs::join(data_root, "cache"); }

std::vector<InstallRoot> install_roots()
{
    std::vector<InstallRoot> roots;
    // Only folders that already exist: Akeno does not create folders on the user's drives.
    // /data/homebrew is the default ShadowMountPlus folder and is created when /data is reachable.
    const auto add = [&](const std::string &path, const std::string &label) {
        if (!fs::is_dir(path))
            return;
        roots.push_back({path, label, fs::writable(path)});
    };
    if (fs::is_dir("/data") && !fs::is_dir("/data/homebrew"))
        fs::make_dirs("/data/homebrew");
    add("/data/homebrew", "Interner Speicher (/data/homebrew)");
    add("/mnt/ext0/homebrew", "Erweiterter Speicher (/mnt/ext0/homebrew)");
    add("/mnt/ext1/homebrew", "M.2-SSD (/mnt/ext1/homebrew)");
    for (int i = 0; i < 8; ++i)
        add("/mnt/usb" + std::to_string(i) + "/homebrew", "USB-Laufwerk " + std::to_string(i));
    if (roots.empty())
        roots.push_back({"/data/homebrew", "Interner Speicher (/data/homebrew)", false});
    return roots;
}

std::vector<std::string> import_dirs()
{
    std::vector<std::string> dirs;
    if (fs::is_dir("/data"))
    {
        fs::make_dirs("/data/akeno/games");
        dirs.push_back("/data/akeno/games");
    }
    for (int i = 0; i < 8; ++i)
    {
        const std::string usb = "/mnt/usb" + std::to_string(i) + "/akeno/games";
        if (fs::is_dir(usb))
            dirs.push_back(usb);
    }
    return dirs;
}

std::string mihawk_root() { return "/data/ps5proton"; }

std::string firmware_version()
{
    struct
    {
        std::uint64_t size;
        char text[0x1C];
        std::uint32_t version;
    } version{sizeof version, {}, 0};
    if (sceKernelGetProsperoSystemSwVersion(&version) != 0)
        return {};
    version.text[sizeof version.text - 1] = '\0';
    std::string text = version.text;
    // "10.01.00.00..." style strings are trimmed to "10.01".
    if (text.size() >= 5 && text[2] == '.')
        text = text.substr(0, 5);
    else if (text.empty() && version.version)
    {
        char built[16];
        std::snprintf(built, sizeof built, "%x.%02x", version.version >> 24, (version.version >> 16) & 0xff);
        text = built;
    }
    return text;
}

std::vector<std::string> recent_log(std::size_t lines)
{
    std::lock_guard lock(log_mutex);
    const std::size_t first = log_lines.size() > lines ? log_lines.size() - lines : 0;
    return {log_lines.begin() + static_cast<std::ptrdiff_t>(first), log_lines.end()};
}

void log(const char *format, ...)
{
    char line[512];
    va_list args;
    va_start(args, format);
    const int length = std::vsnprintf(line, sizeof line - 2, format, args);
    va_end(args);
    if (length < 0)
        return;
    std::size_t used = std::strlen(line);
    line[used] = '\n';
    line[used + 1] = '\0';
    std::lock_guard lock(log_mutex);
    sceKernelDebugOutText(0, line);
    if (log_file)
        std::fputs(line, log_file);
    log_lines.emplace_back(line, used);
    if (log_lines.size() > 300)
        log_lines.pop_front();
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
    int language = 1;
    // SCE_SYSTEM_SERVICE_PARAM_ID_LANG = 1; German is 4.
    if (sceSystemServiceParamGetInt(1, &language) != 0)
        return true;
    return language == 4 || language != 1;
}

void first_frame_shown() { sceSystemServiceHideSplashScreen(); }

[[noreturn]] void quit()
{
    log("[akeno] quit");
    shutdown();
    // exit() gets a native title killed with SIGSYS; the console's own exit request ends it.
    sceSystemServiceLoadExec("exit", nullptr);
    for (;;)
        sceKernelUsleep(100000);
}

bool send_to_loader(const std::string &elf_path, std::string &error)
{
    const auto payload = fs::read_file(elf_path, 16 * 1024 * 1024);
    if (!payload || payload->size() < 64)
    {
        error = "Die Start-Payload fehlt im Akeno-Paket";
        return false;
    }
    const int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        error = "Kein Socket für den ELF-Loader";
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(9021);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(sock, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0)
    {
        ::close(sock);
        error = "Kein ELF-Loader auf Port 9021 erreichbar (etaHEN/kstuff mit ELF-Loader starten)";
        return false;
    }
    std::size_t sent = 0;
    while (sent < payload->size())
    {
        const ssize_t n = ::send(sock, payload->data() + sent, payload->size() - sent, 0);
        if (n <= 0)
        {
            if (n < 0 && errno == EINTR)
                continue;
            ::close(sock);
            error = "Die Payload konnte nicht vollständig gesendet werden";
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    ::close(sock);
    log("[akeno] sent %zu bytes to the ELF loader", payload->size());
    return true;
}

} // namespace akeno::platform
