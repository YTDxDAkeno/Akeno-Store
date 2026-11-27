// Akeno Gaming Hub - validated ZIP inspection and extraction (miniz).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/zip.hpp"

#include "core/fs.hpp"
#include "core/util.hpp"
#include "miniz.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace akeno::zip
{
namespace
{

struct Reader
{
    mz_zip_archive archive{};
    bool open = false;
    ~Reader()
    {
        if (open)
            mz_zip_reader_end(&archive);
    }
};

bool open_reader(Reader &reader, const std::string &path, std::string &error)
{
    std::memset(&reader.archive, 0, sizeof reader.archive);
    if (!mz_zip_reader_init_file(&reader.archive, path.c_str(), 0))
    {
        error = "The archive is not a readable ZIP file";
        return false;
    }
    reader.open = true;
    return true;
}

bool is_link_or_special(const mz_zip_archive_file_stat &stat)
{
    // Unix attributes live in the high 16 bits for archives made on Unix-like hosts.
    const unsigned host = stat.m_version_made_by >> 8;
    if (host != 3 && host != 19)
        return false;
    const unsigned mode = (stat.m_external_attr >> 16) & 0170000u;
    return mode != 0 && mode != 0100000u && mode != 0040000u;
}

struct Item
{
    unsigned index;
    std::string name;
    bool directory;
    std::uint64_t size;
};

bool collect(Reader &reader, std::vector<Item> &items, std::string &error, const Limits &limits)
{
    const mz_uint count = mz_zip_reader_get_num_files(&reader.archive);
    if (count == 0 || count > limits.max_entries)
    {
        error = count == 0 ? "The archive is empty" : "The archive has too many entries";
        return false;
    }
    std::set<std::string> seen;
    std::uint64_t total = 0;
    for (mz_uint i = 0; i < count; ++i)
    {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&reader.archive, i, &stat))
        {
            error = "An archive entry cannot be read";
            return false;
        }
        std::string name = stat.m_filename;
        const bool directory = mz_zip_reader_is_file_a_directory(&reader.archive, i);
        if (directory && !name.empty() && name.back() == '/')
            name.pop_back();
        if (!safe_entry_name(name))
        {
            error = "The archive contains an unsafe path: " + sanitize_text(name, 120);
            return false;
        }
        if (stat.m_bit_flag & 1)
        {
            error = "Encrypted archives are not supported";
            return false;
        }
        if (is_link_or_special(stat))
        {
            error = "The archive contains links or special files";
            return false;
        }
        if (!directory && stat.m_method != 0 && stat.m_method != MZ_DEFLATED)
        {
            error = "The archive uses an unsupported compression method";
            return false;
        }
        if (stat.m_uncomp_size > limits.max_file_bytes)
        {
            error = "An archive entry exceeds the file size limit";
            return false;
        }
        total += stat.m_uncomp_size;
        if (total > limits.max_total_bytes)
        {
            error = "The archive exceeds the unpacked size limit";
            return false;
        }
        const std::string folded = lower(name);
        if (!seen.insert(folded).second && !directory)
        {
            error = "The archive contains duplicate paths";
            return false;
        }
        items.push_back({i, name, directory, stat.m_uncomp_size});
    }
    return true;
}

std::size_t depth_of(const std::string &prefix)
{
    std::size_t depth = 0;
    for (char c : prefix)
        depth += c == '/';
    return depth;
}

} // namespace

bool safe_entry_name(const std::string &name)
{
    if (name.empty() || name.size() > 1024 || name.front() == '/')
        return false;
    for (char c : name)
    {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || c == '\\' || c == ':')
            return false;
    }
    for (const std::string &part : split(name, '/'))
        if (part.empty() || part == "." || part == "..")
            return false;
    return true;
}

std::optional<TitleFolder> find_title_folder(const std::string &archive, std::string &error, const Limits &limits)
{
    Reader reader;
    if (!open_reader(reader, archive, error))
        return std::nullopt;
    std::vector<Item> items;
    if (!collect(reader, items, error, limits))
        return std::nullopt;

    std::vector<std::string> candidates;
    for (const Item &item : items)
    {
        if (item.directory || !ends_with(item.name, "sce_sys/param.json"))
            continue;
        const std::string prefix = item.name.substr(0, item.name.size() - std::strlen("sce_sys/param.json"));
        if (depth_of(prefix) > 3)
            continue;
        for (const Item &other : items)
            if (!other.directory && other.name == prefix + "eboot.bin")
            {
                candidates.push_back(prefix);
                break;
            }
    }
    if (candidates.empty())
    {
        error = "The archive holds no PS5 title folder (eboot.bin with sce_sys/param.json)";
        return std::nullopt;
    }
    if (candidates.size() > 1)
    {
        error = "The archive holds more than one title folder";
        return std::nullopt;
    }
    TitleFolder folder;
    folder.prefix = candidates.front();
    for (const Item &item : items)
    {
        if (!starts_with(item.name, folder.prefix) || item.directory)
            continue;
        folder.bytes += item.size;
        ++folder.files;
        if (item.name == folder.prefix + "sce_sys/param.json")
        {
            if (item.size > 256 * 1024)
            {
                error = "sce_sys/param.json is too large";
                return std::nullopt;
            }
            std::size_t size = 0;
            void *data = mz_zip_reader_extract_to_heap(&reader.archive, item.index, &size, 0);
            if (!data)
            {
                error = "sce_sys/param.json cannot be unpacked";
                return std::nullopt;
            }
            folder.param_json.assign(static_cast<const char *>(data), size);
            mz_free(data);
        }
    }
    return folder;
}

namespace
{
struct Sink
{
    int file = -1;
    std::uint64_t written = 0;
    std::uint64_t *done = nullptr;
    std::uint64_t total = 0;
    const Progress *progress = nullptr;
    bool cancelled = false;
};

size_t write_chunk(void *opaque, mz_uint64 offset, const void *data, size_t size)
{
    auto *sink = static_cast<Sink *>(opaque);
    if (offset != sink->written)
        return 0;
    std::size_t put = 0;
    while (put < size)
    {
        const ssize_t result = ::write(sink->file, static_cast<const char *>(data) + put, size - put);
        if (result < 0)
        {
            if (errno == EINTR)
                continue;
            return 0;
        }
        put += static_cast<std::size_t>(result);
    }
    sink->written += size;
    *sink->done += size;
    if (sink->progress && *sink->progress && !(*sink->progress)(*sink->done, sink->total))
    {
        sink->cancelled = true;
        return 0;
    }
    return size;
}
} // namespace

bool extract(const std::string &archive, const std::string &prefix, const std::string &destination,
             const Progress &progress, std::string &error, const Limits &limits)
{
    Reader reader;
    if (!open_reader(reader, archive, error))
        return false;
    std::vector<Item> items;
    if (!collect(reader, items, error, limits))
        return false;
    std::uint64_t total = 0;
    for (const Item &item : items)
        if (starts_with(item.name, prefix))
            total += item.size;
    if (auto space = fs::free_space(fs::parent(destination)); space && *space < total + 16 * 1024 * 1024)
    {
        error = "Not enough free space to unpack (" + format_bytes(total) + " needed)";
        return false;
    }
    if (!fs::make_dirs(destination))
    {
        error = "The destination folder cannot be created";
        return false;
    }
    std::uint64_t done = 0;
    for (const Item &item : items)
    {
        if (!starts_with(item.name, prefix) || item.name.size() == prefix.size())
            continue;
        const std::string relative = item.name.substr(prefix.size());
        const std::string target = fs::join(destination, relative);
        if (item.directory)
        {
            if (!fs::make_dirs(target))
            {
                error = "A folder cannot be created: " + sanitize_text(relative, 120);
                return false;
            }
            continue;
        }
        if (!fs::make_dirs(fs::parent(target)))
        {
            error = "A folder cannot be created for " + sanitize_text(relative, 120);
            return false;
        }
        Sink sink;
        sink.file = ::open(target.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0777);
        if (sink.file < 0)
        {
            error = "A file cannot be created: " + sanitize_text(relative, 120);
            return false;
        }
        sink.done = &done;
        sink.total = total;
        sink.progress = &progress;
        const bool ok = mz_zip_reader_extract_to_callback(&reader.archive, item.index, write_chunk, &sink, 0);
        ::close(sink.file);
        if (!ok || sink.written != item.size)
        {
            error = sink.cancelled ? "Cancelled" : "A file could not be unpacked: " + sanitize_text(relative, 120);
            return false;
        }
    }
    return true;
}

} // namespace akeno::zip
