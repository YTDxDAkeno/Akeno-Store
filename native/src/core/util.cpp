// Akeno Gaming Hub - small string, version and formatting helpers.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/util.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace akeno
{

std::string lower(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return out;
}

std::string trim(std::string_view text)
{
    std::size_t start = 0, end = text.size();
    while (start < end && static_cast<unsigned char>(text[start]) <= ' ')
        ++start;
    while (end > start && static_cast<unsigned char>(text[end - 1]) <= ' ')
        --end;
    return std::string(text.substr(start, end - start));
}

bool starts_with(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool contains_folded(std::string_view haystack, std::string_view needle)
{
    if (needle.empty())
        return true;
    if (needle.size() > haystack.size())
        return false;
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i)
    {
        std::size_t j = 0;
        for (; j < needle.size(); ++j)
        {
            char a = haystack[i + j], b = needle[j];
            if (a >= 'A' && a <= 'Z')
                a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z')
                b = static_cast<char>(b - 'A' + 'a');
            if (a != b)
                break;
        }
        if (j == needle.size())
            return true;
    }
    return false;
}

std::vector<std::string> split(std::string_view text, char separator)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t end = text.find(separator, start);
        if (end == std::string_view::npos)
        {
            parts.emplace_back(text.substr(start));
            break;
        }
        parts.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

static void append_utf8(std::string &out, std::uint32_t code)
{
    if (code < 0x80)
        out += static_cast<char>(code);
    else if (code < 0x800)
    {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    else if (code < 0x10000)
    {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    else if (code < 0x110000)
    {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

std::string decode_entities(std::string_view text)
{
    static const struct
    {
        const char *name;
        std::uint32_t code;
    } named[] = {{"amp", '&'},      {"lt", '<'},       {"gt", '>'},       {"quot", '"'},
                 {"apos", '\''},    {"nbsp", 0xA0},    {"auml", 0xE4},    {"ouml", 0xF6},
                 {"uuml", 0xFC},    {"Auml", 0xC4},    {"Ouml", 0xD6},    {"Uuml", 0xDC},
                 {"szlig", 0xDF},   {"eacute", 0xE9},  {"hellip", 0x2026}, {"mdash", 0x2014},
                 {"ndash", 0x2013}, {"rsquo", 0x2019}, {"lsquo", 0x2018}, {"ldquo", 0x201C},
                 {"rdquo", 0x201D}, {"copy", 0xA9},    {"reg", 0xAE},     {"trade", 0x2122}};
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != '&')
        {
            out += text[i];
            continue;
        }
        const std::size_t end = text.find(';', i + 1);
        if (end == std::string_view::npos || end - i > 10)
        {
            out += '&';
            continue;
        }
        std::string_view entity = text.substr(i + 1, end - i - 1);
        std::uint32_t code = 0;
        bool ok = false;
        if (!entity.empty() && entity[0] == '#')
        {
            const bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
            for (std::size_t k = hex ? 2 : 1; k < entity.size(); ++k)
            {
                const char c = entity[k];
                int digit = -1;
                if (c >= '0' && c <= '9')
                    digit = c - '0';
                else if (hex && c >= 'a' && c <= 'f')
                    digit = c - 'a' + 10;
                else if (hex && c >= 'A' && c <= 'F')
                    digit = c - 'A' + 10;
                if (digit < 0)
                {
                    code = 0;
                    break;
                }
                code = code * (hex ? 16 : 10) + static_cast<std::uint32_t>(digit);
                if (code > 0x10FFFF)
                    break;
            }
            ok = code > 0 && code <= 0x10FFFF && !(code >= 0xD800 && code <= 0xDFFF);
        }
        else
        {
            for (const auto &item : named)
                if (entity == item.name)
                {
                    code = item.code;
                    ok = true;
                    break;
                }
        }
        if (!ok)
        {
            out += '&';
            continue;
        }
        append_utf8(out, code);
        i = end;
    }
    return out;
}

std::string strip_html(std::string_view html)
{
    std::string text;
    text.reserve(html.size());
    bool in_tag = false;
    for (std::size_t i = 0; i < html.size(); ++i)
    {
        const char c = html[i];
        if (in_tag)
        {
            if (c == '>')
            {
                in_tag = false;
                text += ' ';
            }
            continue;
        }
        if (c == '<')
        {
            in_tag = true;
            continue;
        }
        text += c;
    }
    std::string decoded = decode_entities(text);
    std::string folded;
    folded.reserve(decoded.size());
    bool space = false;
    for (char c : decoded)
    {
        const bool blank = c == ' ' || c == '\n' || c == '\r' || c == '\t';
        if (blank)
        {
            space = !folded.empty();
            continue;
        }
        if (space)
            folded += ' ';
        space = false;
        folded += c;
    }
    return folded;
}

std::uint32_t next_codepoint(std::string_view text, std::size_t &index)
{
    const auto byte = [&](std::size_t at) { return static_cast<unsigned char>(text[at]); };
    const unsigned char first = byte(index);
    if (first < 0x80)
    {
        ++index;
        return first;
    }
    int length = 0;
    std::uint32_t code = 0;
    if ((first & 0xE0) == 0xC0)
    {
        length = 2;
        code = first & 0x1F;
    }
    else if ((first & 0xF0) == 0xE0)
    {
        length = 3;
        code = first & 0x0F;
    }
    else if ((first & 0xF8) == 0xF0)
    {
        length = 4;
        code = first & 0x07;
    }
    else
    {
        ++index;
        return 0xFFFD;
    }
    if (index + static_cast<std::size_t>(length) > text.size())
    {
        index = text.size();
        return 0xFFFD;
    }
    for (int k = 1; k < length; ++k)
    {
        const unsigned char next = byte(index + static_cast<std::size_t>(k));
        if ((next & 0xC0) != 0x80)
        {
            index += static_cast<std::size_t>(k);
            return 0xFFFD;
        }
        code = (code << 6) | (next & 0x3F);
    }
    index += static_cast<std::size_t>(length);
    const std::uint32_t minimum[] = {0, 0, 0x80, 0x800, 0x10000};
    if (code < minimum[length] || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
        return 0xFFFD;
    return code;
}

std::string sanitize_text(std::string_view text, std::size_t limit)
{
    std::string out;
    out.reserve(std::min(text.size(), limit));
    std::size_t index = 0;
    while (index < text.size())
    {
        std::uint32_t code = next_codepoint(text, index);
        if (code == '\t' || code == '\r')
            code = ' ';
        if ((code < 0x20 && code != '\n') || code == 0x7F)
            continue;
        std::string encoded;
        append_utf8(encoded, code);
        if (out.size() + encoded.size() > limit)
            break;
        out += encoded;
    }
    return out;
}

std::string url_encode(std::string_view text)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += static_cast<char>(c);
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string host_of(std::string_view url)
{
    const std::size_t scheme = url.find("://");
    if (scheme == std::string_view::npos)
        return {};
    std::size_t start = scheme + 3;
    std::size_t end = url.find_first_of("/?#", start);
    std::string_view authority = url.substr(start, end == std::string_view::npos ? url.size() - start
                                                                                  : end - start);
    if (authority.find('@') != std::string_view::npos)
        return {};
    const std::size_t colon = authority.find(':');
    if (colon != std::string_view::npos)
        authority = authority.substr(0, colon);
    return lower(authority);
}

bool is_https_url(std::string_view url)
{
    if (!starts_with(url, "https://") || url.size() > 4096)
        return false;
    for (char c : url)
        if (static_cast<unsigned char>(c) <= ' ' || c == '\\' || static_cast<unsigned char>(c) >= 0x7F)
            return false;
    const std::string host = host_of(url);
    return !host.empty() && host.find('.') != std::string::npos;
}

bool is_title_id(std::string_view text)
{
    if (text.size() != 9)
        return false;
    for (std::size_t i = 0; i < 9; ++i)
    {
        const char c = text[i];
        if (i < 4 ? !(c >= 'A' && c <= 'Z') : !(c >= '0' && c <= '9'))
            return false;
    }
    return true;
}

bool parse_content_version(std::string_view text, unsigned out[3])
{
    static const char shape[] = "dd.ddd.ddd";
    if (text.size() != sizeof(shape) - 1)
        return false;
    unsigned values[3] = {0, 0, 0};
    unsigned field = 0;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (shape[i] == '.')
        {
            if (c != '.')
                return false;
            ++field;
        }
        else
        {
            if (c < '0' || c > '9')
                return false;
            values[field] = values[field] * 10 + static_cast<unsigned>(c - '0');
        }
    }
    if (out)
        for (int i = 0; i < 3; ++i)
            out[i] = values[i];
    return true;
}

int compare_content_version(std::string_view left, std::string_view right, bool *comparable)
{
    unsigned a[3], b[3];
    const bool ok = parse_content_version(left, a) && parse_content_version(right, b);
    if (comparable)
        *comparable = ok;
    if (!ok)
        return 0;
    for (int i = 0; i < 3; ++i)
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
}

std::string format_bytes(std::uint64_t bytes)
{
    char text[32];
    if (bytes >= 1024ull * 1024 * 1024)
        std::snprintf(text, sizeof text, "%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024ull * 1024)
        std::snprintf(text, sizeof text, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024));
    else if (bytes >= 1024)
        std::snprintf(text, sizeof text, "%.0f KB", static_cast<double>(bytes) / 1024.0);
    else
        std::snprintf(text, sizeof text, "%llu B", static_cast<unsigned long long>(bytes));
    return text;
}

std::string format_duration(std::uint64_t seconds)
{
    char text[32];
    if (seconds >= 3600)
        std::snprintf(text, sizeof text, "%llu h %02llu min", static_cast<unsigned long long>(seconds / 3600),
                      static_cast<unsigned long long>((seconds / 60) % 60));
    else if (seconds >= 60)
        std::snprintf(text, sizeof text, "%llu min %02llu s", static_cast<unsigned long long>(seconds / 60),
                      static_cast<unsigned long long>(seconds % 60));
    else
        std::snprintf(text, sizeof text, "%llu s", static_cast<unsigned long long>(seconds));
    return text;
}

std::uint64_t fnv1a(std::string_view text)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : text)
    {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hex64(std::uint64_t value)
{
    char text[17];
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(value));
    return text;
}

} // namespace akeno
