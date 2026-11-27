// Akeno Gaming Hub - bounded JSON access on top of yyjson.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

struct yyjson_doc;
struct yyjson_val;

namespace akeno::json
{

// A non-owning view of one JSON value. A default (null) Value answers every query
// with "missing", so chained lookups on untrusted documents never dereference null.
class Value
{
  public:
    Value() = default;
    explicit Value(yyjson_val *value) : value_(value) {}

    bool valid() const { return value_ != nullptr; }
    bool is_object() const;
    bool is_array() const;
    bool is_string() const;
    bool is_number() const;
    bool is_bool() const;
    bool is_null() const;

    Value operator[](const char *key) const;
    Value at(std::size_t index) const;
    std::size_t size() const;

    // Missing or wrongly typed values return the fallback.
    std::string str(std::string_view fallback = {}) const;
    std::string str(const char *key, std::string_view fallback = {}) const { return (*this)[key].str(fallback); }
    std::int64_t integer(std::int64_t fallback = 0) const;
    double number(double fallback = 0) const;
    bool boolean(bool fallback = false) const;

    template <typename F> void each(F &&function) const
    {
        const std::size_t count = size();
        for (std::size_t i = 0; i < count; ++i)
            function(at(i));
    }

    template <typename F> void each_member(F &&function) const;

    yyjson_val *raw() const { return value_; }

  private:
    yyjson_val *value_ = nullptr;
};

class Document
{
  public:
    Document() = default;
    // Parses a copy of `text`; nesting and size are bounded by the caller's limit.
    static Document parse(std::string_view text, std::size_t limit = 32 * 1024 * 1024);

    bool ok() const { return doc_ != nullptr; }
    Value root() const;
    const std::string &error() const { return error_; }

  private:
    struct Free
    {
        void operator()(yyjson_doc *doc) const;
    };
    std::unique_ptr<yyjson_doc, Free> doc_;
    std::string error_;
};

// Calls function(key, value) for each member of an object.
void for_each_member(const Value &object, void (*function)(void *, std::string_view, Value), void *context);

template <typename F> void Value::each_member(F &&function) const
{
    for_each_member(
        *this,
        [](void *context, std::string_view key, Value value) { (*static_cast<F *>(context))(key, value); },
        &function);
}

// JSON string escaping for files Akeno writes itself (receipts, settings).
std::string quote(std::string_view text);

} // namespace akeno::json
