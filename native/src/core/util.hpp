// Akeno Gaming Hub - small string, version and formatting helpers.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The title's content version (sce_sys/param.json contentVersion).
#define AKENO_VERSION "01.003.000"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace akeno
{

std::string lower(std::string_view text);
std::string trim(std::string_view text);
bool starts_with(std::string_view text, std::string_view prefix);
bool ends_with(std::string_view text, std::string_view suffix);
bool contains_folded(std::string_view haystack, std::string_view needle);
std::vector<std::string> split(std::string_view text, char separator);

// Plain text from feed HTML: tags removed, entities decoded, whitespace folded.
std::string strip_html(std::string_view html);
std::string decode_entities(std::string_view text);

// Keeps valid UTF-8 and replaces control characters; never longer than `limit` bytes.
std::string sanitize_text(std::string_view text, std::size_t limit);

std::string url_encode(std::string_view text);
std::string host_of(std::string_view url);
bool is_https_url(std::string_view url);

// PS5 title identifiers: four capital letters and five digits (PPSA12345).
bool is_title_id(std::string_view text);

// PlayStation content versions NN.NNN.NNN. Returns false when either side is not comparable.
bool parse_content_version(std::string_view text, unsigned out[3]);
// -1, 0 or 1; `comparable` reports whether both inputs had the expected shape.
int compare_content_version(std::string_view left, std::string_view right, bool *comparable);

std::string format_bytes(std::uint64_t bytes);
std::string format_duration(std::uint64_t seconds);

// FNV-1a, used for cache file names (not for integrity).
std::uint64_t fnv1a(std::string_view text);
std::string hex64(std::uint64_t value);

// Decodes one UTF-8 code point at `index`, advancing it. Invalid bytes yield U+FFFD.
std::uint32_t next_codepoint(std::string_view text, std::size_t &index);

} // namespace akeno
