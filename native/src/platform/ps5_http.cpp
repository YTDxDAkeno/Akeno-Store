// Akeno Gaming Hub - sceHttp transport for the PS5 title.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A sandboxed native title cannot rely on BSD sockets for TLS, so Akeno uses the
// console's own HTTPS stack, as the boilerplate update check and ProsperoStore do
// (GPL-3.0-or-later, BlackBearReloaded): one pool/SSL/HTTP context per request,
// certificate checks enabled, automatic redirects off (the shared Client follows them).
#include "net/http.hpp"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <vector>

extern "C"
{
    int sceNetPoolCreate(const char *, int, int);
    int sceNetPoolDestroy(int);
    int sceSslInit(std::size_t);
    int sceSslTerm(int);
    int sceHttpInit(int, int, std::size_t);
    int sceHttpTerm(int);
    int sceHttpCreateTemplate(int, const char *, int, int);
    int sceHttpDeleteTemplate(int);
    int sceHttpSetAutoRedirect(int, int);
    int sceHttpSetResolveTimeOut(int, std::uint32_t);
    int sceHttpSetConnectTimeOut(int, std::uint32_t);
    int sceHttpSetSendTimeOut(int, std::uint32_t);
    int sceHttpSetRecvTimeOut(int, std::uint32_t);
    int sceHttpsEnableOption(int, std::uint32_t);
    int sceHttpCreateConnectionWithURL(int, const char *, int);
    int sceHttpDeleteConnection(int);
    int sceHttpCreateRequestWithURL(int, int, const char *, std::uint64_t);
    int sceHttpDeleteRequest(int);
    int sceHttpAddRequestHeader(int, const char *, const char *, std::uint32_t);
    int sceHttpSendRequest(int, const void *, std::size_t);
    int sceHttpGetStatusCode(int, int *);
    int sceHttpGetAllResponseHeaders(int, char **, std::size_t *);
    int sceHttpReadData(int, void *, std::size_t);
    int sceHttpAbortRequest(int);
}

namespace akeno::net
{
namespace
{

class SceHttpTransport final : public Transport
{
  public:
    const char *name() const override { return "sceHttp"; }

    Response send(const Request &request, const Sink &sink, Cancel *cancel) override
    {
        struct Handles
        {
            int pool = -1, ssl = -1, http = -1, tmpl = -1, connection = -1, request = -1;
            ~Handles()
            {
                if (request >= 0)
                    sceHttpDeleteRequest(request);
                if (connection >= 0)
                    sceHttpDeleteConnection(connection);
                if (tmpl >= 0)
                    sceHttpDeleteTemplate(tmpl);
                if (http >= 0)
                    sceHttpTerm(http);
                if (ssl >= 0)
                    sceSslTerm(ssl);
                if (pool >= 0)
                    sceNetPoolDestroy(pool);
            }
        } h;
        Response out;
        int rc = h.pool = sceNetPoolCreate("AkenoHub", 1024 * 1024, 0);
        if (rc >= 0)
            rc = h.ssl = sceSslInit(2 * 1024 * 1024);
        if (rc >= 0)
            rc = h.http = sceHttpInit(h.pool, h.ssl, 4 * 1024 * 1024);
        if (rc >= 0)
            rc = h.tmpl = sceHttpCreateTemplate(h.http, Client::user_agent(), 2, 0);
        if (rc >= 0)
            rc = sceHttpSetAutoRedirect(h.tmpl, 0);
        if (rc >= 0)
            rc = sceHttpSetResolveTimeOut(h.tmpl, 10000000);
        if (rc >= 0)
            rc = sceHttpSetConnectTimeOut(h.tmpl, 15000000);
        if (rc >= 0)
            rc = sceHttpSetSendTimeOut(h.tmpl, 15000000);
        if (rc >= 0)
            rc = sceHttpSetRecvTimeOut(h.tmpl, 30000000);
        if (rc >= 0) // verify the peer, its host name and its validity dates
            rc = sceHttpsEnableOption(h.tmpl, 0x01 | 0x04 | 0x08 | 0x10 | 0x20 | 0x80);
        if (rc >= 0)
            rc = h.connection = sceHttpCreateConnectionWithURL(h.tmpl, request.url.c_str(), 0);
        if (rc >= 0)
            rc = h.request = sceHttpCreateRequestWithURL(h.connection, 0, request.url.c_str(), 0);
        for (const auto &[name, value] : request.headers)
            if (rc >= 0 && name != "User-Agent")
                rc = sceHttpAddRequestHeader(h.request, name.c_str(), value.c_str(), 0);
        if (rc >= 0 && !request.etag.empty())
            rc = sceHttpAddRequestHeader(h.request, "If-None-Match", request.etag.c_str(), 0);
        if (rc >= 0)
            rc = sceHttpAddRequestHeader(h.request, "Accept-Encoding", "identity", 0);
        if (rc >= 0 && cancel && cancel->requested.load())
            rc = -1;
        if (rc >= 0)
            rc = sceHttpSendRequest(h.request, nullptr, 0);
        if (rc >= 0)
            rc = sceHttpGetStatusCode(h.request, &out.status);
        char *headers = nullptr;
        std::size_t size = 0;
        if (rc >= 0)
            rc = sceHttpGetAllResponseHeaders(h.request, &headers, &size);
        if (rc >= 0 && headers)
        {
            int ignored = 0;
            parse_headers(std::string_view(headers, size), out.headers, &ignored);
        }
        const std::string length = out.header("content-length");
        const std::uint64_t total = length.empty() ? 0 : std::strtoull(length.c_str(), nullptr, 10);
        std::vector<char> buffer(128 * 1024);
        while (rc >= 0 && out.status == 200)
        {
            if (cancel && cancel->requested.load())
            {
                sceHttpAbortRequest(h.request);
                out.error = "Cancelled";
                return out;
            }
            const int got = sceHttpReadData(h.request, buffer.data(), buffer.size());
            if (got == 0)
                break;
            if (got < 0)
            {
                rc = got;
                break;
            }
            if (out.bytes + static_cast<std::uint64_t>(got) > request.limit)
            {
                out.error = "The response exceeds its size limit";
                return out;
            }
            out.bytes += static_cast<std::uint64_t>(got);
            const std::string_view chunk(buffer.data(), static_cast<std::size_t>(got));
            if (sink)
            {
                if (!sink(chunk, total))
                {
                    out.error = "The download could not be saved";
                    return out;
                }
            }
            else
                out.body.append(chunk);
        }
        if (rc < 0 && out.error.empty())
        {
            char message[96];
            std::snprintf(message, sizeof message, "The console's HTTPS request failed (0x%08x)",
                          static_cast<unsigned>(rc));
            out.error = message;
        }
        return out;
    }
};

} // namespace

std::unique_ptr<Transport> make_platform_transport() { return std::make_unique<SceHttpTransport>(); }

} // namespace akeno::net
