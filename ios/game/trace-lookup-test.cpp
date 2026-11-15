// Exact-PC and gap semantics against the actual private cache's address list.
#include "trace_lookup.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <vector>
#include <limits>
#include <cstdlib>
struct Entry { uint64_t pc,tag; };
static unsigned checks=0;
static void require(bool ok){++checks;if(!ok){std::fprintf(stderr,"Trace lookup check %u failed\n",checks);std::exit(2);}}
static const Entry *linear(const std::vector<Entry> &entries,uint64_t pc){
    for(const auto &entry:entries)if(entry.pc==pc)return &entry;
    return nullptr;
}
int main(int argc,char **argv){
    if(argc!=2)return 3;
    std::ifstream input(argv[1]);std::vector<Entry> entries;uint64_t pc;
    while(input>>std::hex>>pc)entries.push_back({pc,pc^0x12345678abcdef00ULL});
    require(input.eof() && !entries.empty());
    require(GuestTraces::valid(entries.data(),entries.data()+entries.size()));
    const Entry fixture[]={{0,0},{0xff,1},{0x100,2},{0x1000,3},{UINT64_MAX,4}};
    require(GuestTraces::valid(std::begin(fixture),std::end(fixture)));
    require(GuestTraces::find(std::begin(fixture),std::end(fixture),UINT64_MAX)==&fixture[4]);
    require(!GuestTraces::find(std::begin(fixture),std::end(fixture),0x101));
    require(!GuestTraces::find(fixture,fixture,0));
    const Entry unsorted[]={{0x1000,1},{0xff,2}},duplicate[]={{9,1},{9,2}};
    require(!GuestTraces::valid(std::begin(unsorted),std::end(unsorted)));
    require(!GuestTraces::valid(std::begin(duplicate),std::end(duplicate)));
    std::vector<uint64_t> queries{0,UINT64_MAX};
    for(const auto &entry:entries){
        queries.push_back(entry.pc);
        if(entry.pc)queries.push_back(entry.pc-1);
        if(entry.pc!=UINT64_MAX)queries.push_back(entry.pc+1);
    }
    for(auto query:queries){
        const auto actual=GuestTraces::find(entries.data(),entries.data()+entries.size(),query);
        const auto expected=linear(entries,query);
        require(actual==expected);
        if(actual)require(actual->pc==query && actual->tag==(query^0x12345678abcdef00ULL));
    }
    volatile uint64_t sink=0;
    constexpr unsigned rounds=200000;
    auto benchmark=[&](bool indexed){
        auto begin=std::chrono::steady_clock::now();
        for(unsigned n=0;n<rounds;++n){
            const auto key=queries[(uint64_t(n)*7919)%queries.size()];
            const auto result=indexed ? GuestTraces::find(entries.data(),entries.data()+entries.size(),key) : linear(entries,key);
            sink=sink^(result ? result->tag : key);
        }
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    };
    const double old_ms=benchmark(false),new_ms=benchmark(true);
    std::printf("{\"checks\":%u,\"entries\":%zu,\"queries\":%zu,\"benchmark_lookups\":%u,\"linear_ms\":%.3f,\"indexed_ms\":%.3f,\"game_executed\":false}\n",checks,entries.size(),queries.size(),rounds,old_ms,new_ms);
}
