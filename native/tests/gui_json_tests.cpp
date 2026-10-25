// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "../gui/json.h"
#include <iostream>
using workbench::Json;
int main()
{
    unsigned checks = 0;
    auto require = [&](bool good)
    {
        ++checks;
        if (!good)
        {
            throw std::runtime_error("JSON boundary regression " + std::to_string(checks));
        }
    };
    try
    {
        auto doc = Json::parse("{\"revision\":18446744073709551615,\"pos\":9007199254740993,\"negative\":-"
                               "2147483648,\"escaped\":\"\\ud83d\\ude00\\n\\\\\\\"\",\"enabled\":true,"
                               "\"unknown\":{\"nested\":[false,null,3.25e2]}}");
        require(doc["revision"].u64() == UINT64_MAX);
        require(doc["pos"].u64() == 9007199254740993ULL);
        require(doc["negative"].integer() == INT32_MIN);
        require(doc["escaped"].text() == "\xf0\x9f\x98\x80\n\\\"");
        require(doc["enabled"].boolean());
        require(doc["unknown"]["nested"][std::size_t(2)].real() == 325);
        auto again = Json::parse(doc.dump());
        require(again["revision"].u64() == UINT64_MAX);
        require(again["unknown"].dump() == doc["unknown"].dump());
        doc["volume"] = 137;
        require(Json::parse(doc.dump())["volume"].integer() == 137);
        require(doc["pos"].u64() == 9007199254740993ULL);
        for (const char *bad :
             {"", "[", "{", "{\"a\":}", "[1,]", "{\"a\":1,}", "01", "-", "1.", "1e+", "true false",
              "\"\\ud800\"", "\"\\udc00\"", "\"\\ud800\\u1234\"", "\"\\q\"", "\"\n\"", "nullx"})
        {
            bool rejected = false;
            try
            {
                Json::parse(bad);
            }
            catch (const std::exception &)
            {
                rejected = true;
            }
            require(rejected);
        }
        for (auto number : {"18446744073709551616", "-1", "1e3", "1.5"})
        {
            require(Json::parse(number).u64(77) == 77);
        }
        require(Json::parse("2147483648").integer(88) == 88);
        require(Json::parse("-2147483649").integer(88) == 88);
        for (auto text :
             {std::string("日本語"), std::string("a\0b", 3), std::string("\b\f\n\r\t"), std::string("\\\"")})
        {
            require(Json::parse(Json(text).dump()).text() == text);
        }
        std::string nested(129, '[');
        nested += std::string(129, ']');
        bool rejected = false;
        try
        {
            Json::parse(nested);
        }
        catch (...)
        {
            rejected = true;
        }
        require(rejected);
        std::cout << checks << " JSON/settings boundary checks passed\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
