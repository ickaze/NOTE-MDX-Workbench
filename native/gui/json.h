// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace workbench
{
// JSON boundary for persisted settings and the existing compiler/analysis ABI.
// Numbers retain their spelling: revisions and sample offsets must not pass through double.
struct Json
{
    enum Kind
    {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object
    } kind = Null;
    std::string scalar;
    std::vector<Json> items;
    std::map<std::string, Json> fields;
    Json() = default;
    Json(bool v) : kind(Boolean)
    {
        if (v)
        {
            scalar = "true";
        }
        else
        {
            scalar = "false";
        }
    }
    Json(int v) : kind(Number), scalar(std::to_string(v))
    {
    }
    Json(std::uint64_t v) : kind(Number), scalar(std::to_string(v))
    {
    }
    Json(std::string v) : kind(String), scalar(std::move(v))
    {
    }
    Json(const char *v) : Json(std::string(v))
    {
    }
    static Json object()
    {
        Json v;
        v.kind = Object;
        return v;
    }
    static Json array()
    {
        Json v;
        v.kind = Array;
        return v;
    }
    const Json &operator[](const std::string &key) const
    {
        static const Json empty;
        auto it = fields.find(key);
        if (it == fields.end())
        {
            return empty;
        }
        return it->second;
    }
    Json &operator[](const std::string &key)
    {
        kind = Object;
        return fields[key];
    }
    const Json &operator[](std::size_t i) const
    {
        static const Json empty;
        if (i < items.size())
        {
            return items[i];
        }
        return empty;
    }
    const Json &operator[](std::size_t i)
    {
        return static_cast<const Json &>(*this)[i];
    }
    bool has(const std::string &key) const
    {
        return fields.count(key) != 0;
    }
    bool boolean(bool fallback = false) const
    {
        if (kind == Boolean)
        {
            return scalar == "true";
        }
        return fallback;
    }
    std::string text(std::string fallback = {}) const
    {
        if (kind == String)
        {
            return scalar;
        }
        return fallback;
    }
    std::uint64_t u64(std::uint64_t fallback = 0) const
    {
        if (kind != Number || scalar.empty() || scalar[0] == '-')
        {
            return fallback;
        }
        try
        {
            std::size_t end;
            auto n = std::stoull(scalar, &end);
            if (end == scalar.size())
            {
                return n;
            }
            return fallback;
        }
        catch (...)
        {
            return fallback;
        }
    }
    int integer(int fallback = 0) const
    {
        if (kind != Number)
        {
            return fallback;
        }
        try
        {
            std::size_t end;
            auto n = std::stoll(scalar, &end);
            if (end == scalar.size() && n >= INT32_MIN && n <= INT32_MAX)
            {
                return int(n);
            }
            return fallback;
        }
        catch (...)
        {
            return fallback;
        }
    }
    double real(double fallback = 0) const
    {
        if (kind == Number)
        {
            return std::strtod(scalar.c_str(), nullptr);
        }
        return fallback;
    }
    static std::string quote(const std::string &s)
    {
        const char *h = "0123456789abcdef";
        std::string out = "\"";
        for (unsigned char c : s)
        {
            if (c == '"' || c == '\\')
            {
                out += '\\';
                out += char(c);
            }
            else if (c < 32)
            {
                out += "\\u00";
                out += h[c >> 4];
                out += h[c & 15];
            }
            else
            {
                out += char(c);
            }
        }
        return out + '"';
    }
    std::string dump() const
    {
        if (kind == Null)
        {
            return "null";
        }
        if (kind == String)
        {
            return quote(scalar);
        }
        if (kind == Number || kind == Boolean)
        {
            return scalar;
        }
        std::string out = "{";
        if (kind == Array)
        {
            out = "[";
        }
        bool first = true;
        if (kind == Array)
        {
            for (const auto &v : items)
            {
                if (!first)
                {
                    out += ',';
                }
                first = false;
                out += v.dump();
            }
        }
        else
        {
            for (const auto &kv : fields)
            {
                if (!first)
                {
                    out += ',';
                }
                first = false;
                out += quote(kv.first) + ':' + kv.second.dump();
            }
        }
        if (kind == Array)
        {
            return out + ']';
        }
        return out + '}';
    }
    static Json parse(const std::string &input);
};
class JsonReader
{
    const std::string &s;
    std::size_t p = 0;
    unsigned depth = 0;
    [[noreturn]] void fail() const
    {
        throw std::runtime_error("Invalid JSON at byte " + std::to_string(p));
    }
    char peek() const
    {
        if (p < s.size())
        {
            return s[p];
        }
        return '\0';
    }
    void space()
    {
        while (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n')
        {
            ++p;
        }
    }
    void take(char c)
    {
        if (peek() != c)
        {
            fail();
        }
        ++p;
    }
    unsigned hex4()
    {
        unsigned n = 0;
        for (int i = 0; i < 4; ++i)
        {
            char c = peek();
            ++p;
            unsigned v = 16;
            if (c >= '0' && c <= '9')
            {
                v = c - '0';
            }
            else if (c >= 'a' && c <= 'f')
            {
                v = c - 'a' + 10;
            }
            else if (c >= 'A' && c <= 'F')
            {
                v = c - 'A' + 10;
            }
            if (v == 16)
            {
                fail();
            }
            n = n * 16 + v;
        }
        return n;
    }
    static void utf8(std::string &out, unsigned n)
    {
        if (n < 128)
        {
            out += char(n);
        }
        else if (n < 2048)
        {
            out += char(0xc0 | (n >> 6));
            out += char(0x80 | (n & 63));
        }
        else if (n < 65536)
        {
            out += char(0xe0 | (n >> 12));
            out += char(0x80 | ((n >> 6) & 63));
            out += char(0x80 | (n & 63));
        }
        else
        {
            out += char(0xf0 | (n >> 18));
            out += char(0x80 | ((n >> 12) & 63));
            out += char(0x80 | ((n >> 6) & 63));
            out += char(0x80 | (n & 63));
        }
    }
    std::string string()
    {
        take('"');
        std::string out;
        while (peek() != '"')
        {
            unsigned char c = peek();
            if (!c || c < 32)
            {
                fail();
            }
            ++p;
            if (c != '\\')
            {
                out += char(c);
                continue;
            }
            char e = peek();
            ++p;
            switch (e)
            {
            case '"':
            case '\\':
            case '/':
                out += e;
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u':
            {
                unsigned n = hex4();
                if (n >= 0xd800 && n <= 0xdbff)
                {
                    take('\\');
                    take('u');
                    unsigned low = hex4();
                    if (low < 0xdc00 || low > 0xdfff)
                    {
                        fail();
                    }
                    n = 65536 + ((n - 0xd800) << 10) + low - 0xdc00;
                }
                else if (n >= 0xdc00 && n <= 0xdfff)
                {
                    fail();
                }
                utf8(out, n);
                break;
            }
            default:
                fail();
            }
        }
        ++p;
        return out;
    }
    Json value()
    {
        space();
        if (++depth > 128)
        {
            fail();
        }
        Json v;
        char c = peek();
        if (c == '"')
        {
            v = Json(string());
        }
        else if (c == '[' || c == '{')
        {
            ++p;
            bool object = c == '{';
            v = Json::array();
            if (object)
            {
                v = Json::object();
            }
            space();
            char close = ']';
            if (object)
            {
                close = '}';
            }
            if (peek() != close)
            {
                for (;;)
                {
                    space();
                    std::string key;
                    if (object)
                    {
                        key = string();
                        space();
                        take(':');
                    }
                    Json next = value();
                    if (object)
                    {
                        v.fields[key] = std::move(next);
                    }
                    else
                    {
                        v.items.push_back(std::move(next));
                    }
                    space();
                    if (peek() != ',')
                    {
                        break;
                    }
                    ++p;
                }
            }
            take(close);
        }
        else if (c == 't' || c == 'f' || c == 'n')
        {
            std::string word = "null";
            if (c == 't')
            {
                word = "true";
            }
            else if (c == 'f')
            {
                word = "false";
            }
            if (s.compare(p, word.size(), word) != 0)
            {
                fail();
            }
            p += word.size();
            if (c != 'n')
            {
                v = Json(c == 't');
            }
        }
        else
        {
            auto begin = p;
            if (peek() == '-')
            {
                ++p;
            }
            if (peek() == '0')
            {
                ++p;
            }
            else
            {
                if (peek() < '1' || peek() > '9')
                {
                    fail();
                }
                while (peek() >= '0' && peek() <= '9')
                {
                    ++p;
                }
            }
            if (peek() == '.')
            {
                ++p;
                if (peek() < '0' || peek() > '9')
                {
                    fail();
                }
                while (peek() >= '0' && peek() <= '9')
                {
                    ++p;
                }
            }
            if (peek() == 'e' || peek() == 'E')
            {
                ++p;
                if (peek() == '+' || peek() == '-')
                {
                    ++p;
                }
                if (peek() < '0' || peek() > '9')
                {
                    fail();
                }
                while (peek() >= '0' && peek() <= '9')
                {
                    ++p;
                }
            }
            v.kind = Json::Number;
            v.scalar = s.substr(begin, p - begin);
        }
        --depth;
        return v;
    }

  public:
    explicit JsonReader(const std::string &input) : s(input)
    {
    }
    Json read()
    {
        Json v = value();
        space();
        if (p != s.size())
        {
            fail();
        }
        return v;
    }
};
inline Json Json::parse(const std::string &input)
{
    return JsonReader(input).read();
}
} // namespace workbench
