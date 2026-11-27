// Akeno Gaming Hub - POSIX file helpers that behave the same on Linux and in a PS5 title.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace akeno::fs
{

std::string join(std::string_view a, std::string_view b);
std::string parent(std::string_view path);
std::string leaf(std::string_view path);

bool exists(const std::string &path);
bool is_dir(const std::string &path);
bool is_file(const std::string &path);
std::optional<std::uint64_t> file_size(const std::string &path);
bool make_dirs(const std::string &path, unsigned mode = 0777);

std::optional<std::string> read_file(const std::string &path, std::size_t limit = 64 * 1024 * 1024);
// Writes to `<path>.tmp-<n>`, flushes, then renames over `path`.
bool write_atomic(const std::string &path, std::string_view data);

struct Entry
{
    std::string name;
    bool directory = false;
};
// Lists a directory without "." and ".."; empty when it cannot be read.
std::vector<Entry> list(const std::string &path);

// Removes a file or a whole tree without following links. Returns true when nothing remains.
bool remove_tree(const std::string &path);
bool rename(const std::string &from, const std::string &to);
// chmod on every file and folder in the tree (PS5 titles must be 0777 to launch).
void chmod_tree(const std::string &path, unsigned mode);
// Bytes available to the caller on the volume holding `path`.
std::optional<std::uint64_t> free_space(const std::string &path);
// True when a file can be created and removed inside `directory`.
bool writable(const std::string &directory);

} // namespace akeno::fs
