// Akeno Gaming Hub - validated ZIP inspection and extraction (miniz).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace akeno::zip
{

struct Limits
{
    std::size_t max_entries = 20000;
    std::uint64_t max_total_bytes = 8ull * 1024 * 1024 * 1024; // 8 GiB unpacked
    std::uint64_t max_file_bytes = 4ull * 1024 * 1024 * 1024;
};

// A PS5 title folder inside an archive: the directory that holds eboot.bin and
// sce_sys/param.json, at the top or up to three folders deep.
struct TitleFolder
{
    std::string prefix;     // "" or "PPSA12345/" etc., always ending in '/' unless empty
    std::string param_json; // the raw sce_sys/param.json
    std::uint64_t bytes = 0;
    std::size_t files = 0;
};

// Checks every entry name and attribute and locates exactly one title folder.
std::optional<TitleFolder> find_title_folder(const std::string &archive, std::string &error,
                                             const Limits &limits = {});

using Progress = std::function<bool(std::uint64_t done, std::uint64_t total)>;

// Extracts the entries under `prefix` into `destination` (created). Names are validated
// again; links, encrypted entries and paths that leave the destination are refused.
bool extract(const std::string &archive, const std::string &prefix, const std::string &destination,
             const Progress &progress, std::string &error, const Limits &limits = {});

// Entry-name policy shared by inspection and extraction.
bool safe_entry_name(const std::string &name);

} // namespace akeno::zip
