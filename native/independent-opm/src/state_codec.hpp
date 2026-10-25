// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once
#include <vector>
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <type_traits>
#include <stdexcept>
namespace independent_opm { namespace state_detail {
struct Invalid {};
inline std::uint64_t hash(const std::uint8_t* p,std::size_t n) {
    std::uint64_t h=14695981039346656037ull;
    for(std::size_t i=0;i<n;++i) { h^=p[i];h*=1099511628211ull; } return h;
}
struct Archive {
    bool reading=false;std::vector<std::uint8_t> bytes;
    const std::uint8_t* input=nullptr;std::size_t size=0,pos=0;
    Archive()=default;
    Archive(const void* p,std::size_t n,unsigned kind,unsigned expected_version=1):reading(true),input(static_cast<const std::uint8_t*>(p)),size(n) {
        if(!p||n<24||n>4194304) throw Invalid{};
        std::uint64_t magic=0,version=0,checksum=0;
        (*this)(magic,version,checksum);
        if(magic!=0x3154534d504f0000ull+kind||version!=expected_version||checksum!=hash(input+24,n-24)) throw Invalid{};
    }
    template<class T> void one(T& v) {
        if constexpr(std::is_same_v<T,bool>) {
            std::uint8_t b=v?1:0;one(b);if(b>1)throw Invalid{};v=b!=0;
        } else if constexpr(std::is_same_v<T,double>) {
            static_assert(sizeof(double)==8 && std::numeric_limits<double>::is_iec559,"IEEE binary64 required");
            std::uint64_t u=0;std::memcpy(&u,&v,8);one(u);std::memcpy(&v,&u,8);
            if(!std::isfinite(v))throw Invalid{};
        } else if constexpr(std::is_enum_v<T>) {
            std::uint32_t u=static_cast<std::uint32_t>(v);one(u);v=static_cast<T>(u);
        } else {
            static_assert(std::is_integral_v<T>);static_assert(sizeof(unsigned)==4);
            using U=std::make_unsigned_t<T>;U u=static_cast<U>(v);
            if(reading) {
                if(sizeof(T)>size-pos)throw Invalid{};
                u=0;
                for(unsigned i=0;i<sizeof(T);++i)u|=U(input[pos++])<<(8*i);
                std::memcpy(&v,&u,sizeof(T));
            } else for(unsigned i=0;i<sizeof(T);++i)bytes.push_back(static_cast<std::uint8_t>(u>>(8*i)));
        }
    }
    template<class T,std::size_t N> void one(std::array<T,N>& a) {for(auto& v:a)one(v);}
    template<class... T> void operator()(T&... v) {(one(v),...);}
    void blob(std::vector<std::uint8_t>& b) {
        std::uint32_t n=static_cast<std::uint32_t>(b.size());one(n);
        if(reading) {if(n>size-pos)throw Invalid{};b.assign(input+pos,input+pos+n);pos+=n;}
        else bytes.insert(bytes.end(),b.begin(),b.end());
    }
    void end() {if(pos!=size)throw Invalid{};}
    std::vector<std::uint8_t> finish(unsigned kind,unsigned format_version=1) {
        Archive header;std::uint64_t magic=0x3154534d504f0000ull+kind,version=format_version,checksum=hash(bytes.data(),bytes.size());
        header(magic,version,checksum);header.bytes.insert(header.bytes.end(),bytes.begin(),bytes.end());return header.bytes;
    }
};
} }
