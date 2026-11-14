#pragma once
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
// Offline contract used by the Mac runtime: no PSN account or network transfer.
// Title credentials are neither read nor retained. Handles represent local
// setup bookkeeping; transfer/identity queries always report signed out.
class OfflineServices {
    uint32_t next_=1;
public:
    static constexpr uint32_t signed_out=0x80550006u, invalid=0x80550003u;
    struct Callback { uint64_t function,argument; };
    bool title_configured=false;
    unsigned restriction_updates=0;
    std::unordered_set<uint32_t> contexts;
    std::unordered_map<uint32_t,uint32_t> requests;
    std::unordered_map<uint32_t,Callback> np_callbacks,net_callbacks;
    uint32_t handle() { return next_<=4096 ? next_++ : 0; }
    uint32_t configureTitle() { title_configured=true; return 0; }
    uint32_t restriction() { ++restriction_updates; return 0; }
    uint32_t context() {
        auto id=handle(); if (id) contexts.insert(id); return id;
    }
    uint32_t request(uint32_t context) {
        if (!contexts.count(context)) return invalid;
        auto id=handle(); if (id) requests[id]=context; return id;
    }
    void removeContext(uint32_t context) {
        contexts.erase(context);
        for (auto it=requests.begin();it!=requests.end();)
            if (it->second==context) it=requests.erase(it); else ++it;
    }
    uint32_t callback(bool network,uint64_t function,uint64_t argument) {
        auto id=handle(); if (id) (network ? net_callbacks : np_callbacks)[id]={function,argument};
        return id;
    }
};
