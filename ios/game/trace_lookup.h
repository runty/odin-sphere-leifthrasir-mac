#pragma once
#include <algorithm>
#include <cstdint>

// Same exact-PC lower-bound lookup as OpenCrown's mobile dispatcher. The
// immutable generated table must be strictly increasing; validate once at boot.
namespace GuestTraces {
template<class Entry> bool valid(const Entry *first,const Entry *last) {
    return std::adjacent_find(first,last,[](const Entry &a,const Entry &b){return a.pc>=b.pc;})==last;
}
template<class Entry> const Entry *find(const Entry *first,const Entry *last,uint64_t pc) {
    const auto entry=std::lower_bound(first,last,pc,[](const Entry &a,uint64_t key){return a.pc<key;});
    return entry!=last && entry->pc==pc ? entry : nullptr;
}
}
