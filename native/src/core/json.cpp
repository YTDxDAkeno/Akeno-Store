// Akeno Gaming Hub - bounded JSON access on top of yyjson.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/json.hpp"

#include "yyjson.h"

#include <cstdio>

namespace akeno::json
{

bool Value::is_object() const { return value_ && yyjson_is_obj(value_); }
bool Value::is_array() const { return value_ && yyjson_is_arr(value_); }
bool Value::is_string() const { return value_ && yyjson_is_str(value_); }
bool Value::is_number() const { return value_ && yyjson_is_num(value_); }
bool Value::is_bool() const { return value_ && yyjson_is_bool(value_); }
bool Value::is_null() const { return !value_ || yyjson_is_null(value_); }

Value Value::operator[](const char *key) const
{
    if (!is_object())
        return Value();
    return Value(yyjson_obj_get(value_, key));
}

Value Value::at(std::size_t index) const
{
    if (!is_array())
        return Value();
    return Value(yyjson_arr_get(value_, index));
}

std::size_t Value::size() const
{
    if (is_array())
        return yyjson_arr_size(value_);
    if (is_object())
        return yyjson_obj_size(value_);
    return 0;
}

std::string Value::str(std::string_view fallback) const
{
    if (!is_string())
        return std::string(fallback);
    return std::string(yyjson_get_str(value_), yyjson_get_len(value_));
}

std::int64_t Value::integer(std::int64_t fallback) const
{
    if (!value_)
        return fallback;
    if (yyjson_is_sint(value_))
        return yyjson_get_sint(value_);
    if (yyjson_is_uint(value_))
    {
        const std::uint64_t v = yyjson_get_uint(value_);
        return v > static_cast<std::uint64_t>(INT64_MAX) ? fallback : static_cast<std::int64_t>(v);
    }
    if (yyjson_is_real(value_))
        return static_cast<std::int64_t>(yyjson_get_real(value_));
    return fallback;
}

double Value::number(double fallback) const
{
    if (!value_ || !yyjson_is_num(value_))
        return fallback;
    return yyjson_get_num(value_);
}

bool Value::boolean(bool fallback) const
{
    if (!value_ || !yyjson_is_bool(value_))
        return fallback;
    return yyjson_get_bool(value_);
}

void Document::Free::operator()(yyjson_doc *doc) const { yyjson_doc_free(doc); }

Document Document::parse(std::string_view text, std::size_t limit)
{
    Document document;
    if (text.size() > limit)
    {
        document.error_ = "JSON exceeds its size limit";
        return document;
    }
    yyjson_read_err error{};
    yyjson_doc *doc = yyjson_read_opts(const_cast<char *>(text.data()), text.size(), YYJSON_READ_NOFLAG,
                                       nullptr, &error);
    if (!doc)
    {
        char message[128];
        std::snprintf(message, sizeof message, "JSON error at %zu: %s", error.pos,
                      error.msg ? error.msg : "unknown");
        document.error_ = message;
        return document;
    }
    document.doc_.reset(doc);
    return document;
}

Value Document::root() const { return doc_ ? Value(yyjson_doc_get_root(doc_.get())) : Value(); }

void for_each_member(const Value &object, void (*function)(void *, std::string_view, Value), void *context)
{
    if (!object.is_object())
        return;
    yyjson_obj_iter iter = yyjson_obj_iter_with(object.raw());
    yyjson_val *key = nullptr;
    while ((key = yyjson_obj_iter_next(&iter)))
    {
        yyjson_val *value = yyjson_obj_iter_get_val(key);
        function(context, std::string_view(yyjson_get_str(key), yyjson_get_len(key)), Value(value));
    }
}

std::string quote(std::string_view text)
{
    std::string out = "\"";
    for (unsigned char c : text)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20)
            {
                char escaped[8];
                std::snprintf(escaped, sizeof escaped, "\\u%04x", c);
                out += escaped;
            }
            else
                out += static_cast<char>(c);
        }
    }
    out += '"';
    return out;
}

} // namespace akeno::json
