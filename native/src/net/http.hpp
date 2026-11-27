// Akeno Gaming Hub - HTTPS transport interface shared by the host (libcurl) and the PS5 (sceHttp).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace akeno::net
{

struct Request
{
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::uint64_t limit = 8 * 1024 * 1024; // response body limit
    std::string etag;                      // sent as If-None-Match when set
};

struct Response
{
    int status = 0;
    std::map<std::string, std::string> headers; // lower-case names
    std::string body;                           // filled when no sink was given
    std::uint64_t bytes = 0;
    std::string error; // empty on transport success (any HTTP status)
    bool ok() const { return error.empty() && status == 200; }
    std::string header(const std::string &name) const
    {
        const auto it = headers.find(name);
        return it == headers.end() ? std::string() : it->second;
    }
};

// Receives body chunks; return false to abort. `total` is Content-Length or 0.
using Sink = std::function<bool(std::string_view chunk, std::uint64_t total)>;

struct Cancel
{
    std::atomic<bool> requested{false};
};

class Transport
{
  public:
    virtual ~Transport() = default;
    // One request, no redirects followed. `sink` may be empty (body collected).
    virtual Response send(const Request &request, const Sink &sink, Cancel *cancel) = 0;
    virtual const char *name() const = 0;
};

// Shared policy: HTTPS only, at most five redirects, every hop re-validated, a User-Agent.
class Client
{
  public:
    explicit Client(std::unique_ptr<Transport> transport) : transport_(std::move(transport)) {}

    Response get(const std::string &url, std::uint64_t limit = 8 * 1024 * 1024, Cancel *cancel = nullptr,
                 const std::vector<std::pair<std::string, std::string>> &headers = {});
    Response download(const std::string &url, const Sink &sink, Cancel *cancel = nullptr,
                      std::uint64_t limit = 8ull * 1024 * 1024 * 1024);
    const char *transport_name() const { return transport_->name(); }

    static const char *user_agent();

  private:
    Response follow(Request request, const Sink &sink, Cancel *cancel);
    std::unique_ptr<Transport> transport_;
};

// Parses "Name: value" header blocks (used by both transports).
void parse_headers(std::string_view block, std::map<std::string, std::string> &out, int *status);

// Resolves a Location header against the request URL.
std::string resolve_location(const std::string &base, const std::string &location);

std::unique_ptr<Transport> make_platform_transport();

} // namespace akeno::net
