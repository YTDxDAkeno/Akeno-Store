// Akeno Gaming Hub - POSIX file helpers that behave the same on Linux and in a PS5 title.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/fs.hpp"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/statvfs.h>
#endif

namespace akeno::fs
{

std::string join(std::string_view a, std::string_view b)
{
    if (a.empty())
        return std::string(b);
    std::string out(a);
    if (out.back() != '/')
        out += '/';
    while (!b.empty() && b.front() == '/')
        b.remove_prefix(1);
    out += b;
    return out;
}

std::string parent(std::string_view path)
{
    while (path.size() > 1 && path.back() == '/')
        path.remove_suffix(1);
    const std::size_t slash = path.rfind('/');
    if (slash == std::string_view::npos)
        return ".";
    if (slash == 0)
        return "/";
    return std::string(path.substr(0, slash));
}

std::string leaf(std::string_view path)
{
    while (path.size() > 1 && path.back() == '/')
        path.remove_suffix(1);
    const std::size_t slash = path.rfind('/');
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

bool exists(const std::string &path)
{
    struct stat info{};
    return ::stat(path.c_str(), &info) == 0;
}

bool is_dir(const std::string &path)
{
    struct stat info{};
    return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool is_file(const std::string &path)
{
    struct stat info{};
    return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

std::optional<std::uint64_t> file_size(const std::string &path)
{
    struct stat info{};
    if (::stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
        return std::nullopt;
    return static_cast<std::uint64_t>(info.st_size);
}

bool make_dirs(const std::string &path, unsigned mode)
{
    if (path.empty())
        return false;
    if (is_dir(path))
        return true;
    const std::string up = parent(path);
    if (up != path && up != "." && up != "/" && !is_dir(up) && !make_dirs(up, mode))
        return false;
    if (::mkdir(path.c_str(), static_cast<mode_t>(mode)) == 0 || errno == EEXIST)
    {
        ::chmod(path.c_str(), static_cast<mode_t>(mode));
        return is_dir(path);
    }
    return false;
}

std::optional<std::string> read_file(const std::string &path, std::size_t limit)
{
    const int file = ::open(path.c_str(), O_RDONLY);
    if (file < 0)
        return std::nullopt;
    std::string data;
    char buffer[64 * 1024];
    for (;;)
    {
        const ssize_t got = ::read(file, buffer, sizeof buffer);
        if (got < 0)
        {
            if (errno == EINTR)
                continue;
            ::close(file);
            return std::nullopt;
        }
        if (got == 0)
            break;
        if (data.size() + static_cast<std::size_t>(got) > limit)
        {
            ::close(file);
            return std::nullopt;
        }
        data.append(buffer, static_cast<std::size_t>(got));
    }
    ::close(file);
    return data;
}

bool write_atomic(const std::string &path, std::string_view data)
{
    static std::atomic<unsigned> counter{0};
    char suffix[48];
    std::snprintf(suffix, sizeof suffix, ".tmp-%d-%u", static_cast<int>(::getpid()), counter.fetch_add(1));
    const std::string temporary = path + suffix;
    const int file = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (file < 0)
        return false;
    std::size_t written = 0;
    while (written < data.size())
    {
        const ssize_t put = ::write(file, data.data() + written, data.size() - written);
        if (put < 0)
        {
            if (errno == EINTR)
                continue;
            ::close(file);
            ::unlink(temporary.c_str());
            return false;
        }
        written += static_cast<std::size_t>(put);
    }
    ::fsync(file);
    ::close(file);
    if (::rename(temporary.c_str(), path.c_str()) != 0)
    {
        ::unlink(temporary.c_str());
        return false;
    }
    return true;
}

std::vector<Entry> list(const std::string &path)
{
    std::vector<Entry> entries;
    DIR *directory = ::opendir(path.c_str());
    if (!directory)
        return entries;
    while (dirent *item = ::readdir(directory))
    {
        const std::string name = item->d_name;
        if (name.empty() || name == "." || name == "..")
            continue;
        Entry entry;
        entry.name = name;
        struct stat info{};
        if (::lstat(join(path, name).c_str(), &info) == 0)
            entry.directory = S_ISDIR(info.st_mode);
        else
            entry.directory = item->d_type == DT_DIR;
        entries.push_back(std::move(entry));
    }
    ::closedir(directory);
    return entries;
}

bool remove_tree(const std::string &path)
{
    struct stat info{};
    if (::lstat(path.c_str(), &info) != 0)
        return errno == ENOENT;
    if (S_ISDIR(info.st_mode))
    {
        for (const Entry &entry : list(path))
            remove_tree(join(path, entry.name));
        return ::rmdir(path.c_str()) == 0 || errno == ENOENT;
    }
    return ::unlink(path.c_str()) == 0 || errno == ENOENT;
}

bool rename(const std::string &from, const std::string &to) { return ::rename(from.c_str(), to.c_str()) == 0; }

void chmod_tree(const std::string &path, unsigned mode)
{
    struct stat info{};
    if (::lstat(path.c_str(), &info) != 0 || S_ISLNK(info.st_mode))
        return;
    ::chmod(path.c_str(), static_cast<mode_t>(mode));
    if (S_ISDIR(info.st_mode))
        for (const Entry &entry : list(path))
            chmod_tree(join(path, entry.name), mode);
}

std::optional<std::uint64_t> free_space(const std::string &path)
{
#if defined(__linux__)
    struct statvfs info{};
    if (::statvfs(path.c_str(), &info) != 0)
        return std::nullopt;
    return static_cast<std::uint64_t>(info.f_bavail) * info.f_frsize;
#else
    struct statfs info{};
    if (::statfs(path.c_str(), &info) != 0)
        return std::nullopt;
    return static_cast<std::uint64_t>(info.f_bavail) * info.f_bsize;
#endif
}

bool writable(const std::string &directory)
{
    const std::string probe = join(directory, ".akeno-write-probe");
    const int file = ::open(probe.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (file < 0)
        return false;
    ::close(file);
    return ::unlink(probe.c_str()) == 0;
}

} // namespace akeno::fs
