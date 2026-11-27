// Akeno Gaming Hub - HTTPS redirect and header policy.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/http.hpp"

#include "core/util.hpp"

#include <cstdlib>

namespace akeno::net
{

const char *Client::user_agent() { return "AkenoGamingHub/00.002.000 (PS5 homebrew)"; }

void parse_headers(std::string_view block, std::map<std::string, std::string> &out, int *status)
{
    for (const std::string &raw : split(block, '\n'))
    {
        std::string line = raw;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (starts_with(line, "HTTP/"))
        {
            out.clear(); // a new response block (after a 100 Continue, for example)
            const std::size_t space = line.find(' ');
            if (status && space != std::string::npos)
                *status = std::atoi(line.c_str() + space + 1);
            continue;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos || colon == 0)
            continue;
        out[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
    }
}

std::string resolve_location(const std::string &base, const std::string &location)
{
    if (starts_with(location, "https://") || starts_with(location, "http://"))
        return location;
    const std::size_t scheme = base.find("://");
    if (scheme == std::string::npos)
        return {};
    const std::size_t path = base.find('/', scheme + 3);
    const std::string origin = path == std::string::npos ? base : base.substr(0, path);
    if (starts_with(location, "//"))
        return base.substr(0, scheme + 1) + location;
    if (starts_with(location, "/"))
        return origin + location;
    const std::size_t last = base.rfind('/');
    if (last == std::string::npos || last < scheme + 3)
        return origin + "/" + location;
    return base.substr(0, last + 1) + location;
}

Response Client::follow(Request request, const Sink &sink, Cancel *cancel)
{
    Response response;
    for (int hop = 0; hop < 6; ++hop)
    {
        if (!is_https_url(request.url))
        {
            response = Response();
            response.error = "Only HTTPS addresses are allowed";
            return response;
        }
        bool has_agent = false;
        for (const auto &[name, value] : request.headers)
            has_agent |= lower(name) == "user-agent";
        if (!has_agent)
            request.headers.emplace_back("User-Agent", user_agent());
        // Transports hand only HTTP 200 bodies to the sink, so a redirect body never reaches it.
        response = transport_->send(request, sink, cancel);
        if (!response.error.empty())
            return response;
        if (response.status == 301 || response.status == 302 || response.status == 303 ||
            response.status == 307 || response.status == 308)
        {
            const std::string next = resolve_location(request.url, response.header("location"));
            if (next.empty())
            {
                response.error = "A redirect has no destination";
                return response;
            }
            request.url = next;
            request.etag.clear();
            continue;
        }
        return response;
    }
    response.error = "Too many redirects";
    return response;
}

Response Client::get(const std::string &url, std::uint64_t limit, Cancel *cancel,
                     const std::vector<std::pair<std::string, std::string>> &headers)
{
    Request request;
    request.url = url;
    request.limit = limit;
    request.headers = headers;
    return follow(request, Sink(), cancel);
}

Response Client::download(const std::string &url, const Sink &sink, Cancel *cancel, std::uint64_t limit)
{
    Request request;
    request.url = url;
    request.limit = limit;
    request.headers.emplace_back("Accept", "application/octet-stream");
    return follow(request, sink, cancel);
}

} // namespace akeno::net
