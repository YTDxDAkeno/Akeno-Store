// Akeno Gaming Hub - libcurl transport for the Linux development host.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/http.hpp"

#include <curl/curl.h>

#include <mutex>

namespace akeno::net
{
namespace
{

struct Transfer
{
    const Request *request;
    const Sink *sink;
    Cancel *cancel;
    Response *response;
    std::string header_block;
    bool headers_parsed = false;
    std::uint64_t total = 0;
    bool overflow = false;
    bool sink_refused = false;
};

void parse_once(Transfer &transfer)
{
    if (transfer.headers_parsed)
        return;
    transfer.headers_parsed = true;
    parse_headers(transfer.header_block, transfer.response->headers, &transfer.response->status);
    const std::string length = transfer.response->header("content-length");
    transfer.total = length.empty() ? 0 : std::strtoull(length.c_str(), nullptr, 10);
}

size_t on_header(char *data, size_t size, size_t count, void *opaque)
{
    auto &transfer = *static_cast<Transfer *>(opaque);
    const std::string_view line(data, size * count);
    if (line.substr(0, 5) == "HTTP/")
    {
        transfer.header_block.clear();
        transfer.headers_parsed = false;
    }
    transfer.header_block.append(line);
    return size * count;
}

size_t on_body(char *data, size_t size, size_t count, void *opaque)
{
    auto &transfer = *static_cast<Transfer *>(opaque);
    parse_once(transfer);
    const std::size_t bytes = size * count;
    if (transfer.cancel && transfer.cancel->requested.load())
        return 0;
    if (transfer.response->bytes + bytes > transfer.request->limit)
    {
        transfer.overflow = true;
        return 0;
    }
    transfer.response->bytes += bytes;
    if (transfer.response->status != 200)
        return bytes; // redirect and error bodies are discarded
    if (transfer.sink && *transfer.sink)
    {
        if (!(*transfer.sink)(std::string_view(data, bytes), transfer.total))
        {
            transfer.sink_refused = true;
            return 0;
        }
    }
    else
        transfer.response->body.append(data, bytes);
    return bytes;
}

int on_progress(void *opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    auto &transfer = *static_cast<Transfer *>(opaque);
    return transfer.cancel && transfer.cancel->requested.load() ? 1 : 0;
}

class CurlTransport final : public Transport
{
  public:
    CurlTransport()
    {
        static std::once_flag once;
        std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    }

    const char *name() const override { return "libcurl"; }

    Response send(const Request &request, const Sink &sink, Cancel *cancel) override
    {
        Response response;
        CURL *curl = curl_easy_init();
        if (!curl)
        {
            response.error = "The network library could not start";
            return response;
        }
        Transfer transfer{&request, &sink, cancel, &response};
        curl_slist *headers = nullptr;
        for (const auto &[name, value] : request.headers)
            headers = curl_slist_append(headers, (name + ": " + value).c_str());
        if (!request.etag.empty())
            headers = curl_slist_append(headers, ("If-None-Match: " + request.etag).c_str());
        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 64L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "identity");
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, on_header);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &transfer);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_body);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, on_progress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);
        const CURLcode code = curl_easy_perform(curl);
        parse_once(transfer);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        response.status = static_cast<int>(status);
        if (transfer.overflow)
            response.error = "The response exceeds its size limit";
        else if (cancel && cancel->requested.load())
            response.error = "Cancelled";
        else if (transfer.sink_refused)
            response.error = "The download could not be saved";
        else if (code != CURLE_OK)
            response.error = curl_easy_strerror(code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return response;
    }
};

} // namespace

std::unique_ptr<Transport> make_platform_transport() { return std::make_unique<CurlTransport>(); }

} // namespace akeno::net
