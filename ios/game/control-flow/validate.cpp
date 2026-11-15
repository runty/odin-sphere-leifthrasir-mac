// Authored per-owner CFG and instruction-overlap validator.
extern "C" {
#include <xed/xed-interface.h>
}
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <vector>
struct R {
  uint64_t end;
  std::map<uint64_t, std::vector<uint64_t>> tables;
};
int main(int argc, char **argv) {
  if (argc != 6)
    return 2;
  std::ifstream f(argv[1], std::ios::binary);
  std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
  if (!f)
    return 3;
  uint64_t limit = std::stoull(argv[4], nullptr, 16);
  if (limit > b.size())
    return 3;
  std::set<uint64_t> noreturn;
  {
    std::ifstream nf(argv[5]);
    uint64_t v;
    while (nf >> std::hex >> v)
      noreturn.insert(v);
  }
  std::map<uint64_t, R> regions;
  std::ifstream rf(argv[2]);
  uint64_t lo, hi;
  while (rf >> std::hex >> lo >> hi) {
    if (lo >= hi || hi > limit)
      return 3;
    regions[lo].end = hi;
  }
  std::ifstream tf(argv[3]);
  uint64_t owner, pc, table, count;
  while (tf >> std::hex >> owner >> pc >> table >> count) {
    if (!regions.count(owner) || count > 4096 || table > b.size() ||
        count > (b.size() - table) / 4)
      return 3;
    for (unsigned i = 0; i < count; ++i) {
      int32_t v;
      memcpy(&v, b.data() + table + 4 * i, 4);
      auto target = uint64_t(int64_t(table) + v);
      if (target < owner || target >= regions[owner].end)
        return 3;
      regions[owner].tables[pc].push_back(target);
    }
  }
  xed_tables_init();
  xed_state_t s;
  xed_state_init2(&s, XED_MACHINE_MODE_LONG_64, XED_ADDRESS_WIDTH_64b);
  bool bad = false;
  for (auto &[start, r] : regions) {
    std::vector<uint64_t> q{start};
    std::map<uint64_t, unsigned> seen;
    std::set<uint64_t> rets, calls, unresolvedSites;
    std::map<uint64_t, uint64_t> externalTargets;
    unsigned failures = 0, indirect = 0, external = 0;
    for (size_t i = 0; i < q.size(); ++i) {
      auto at = q[i];
      while (!seen.count(at)) {
        if (at < start || at >= r.end) {
          ++failures;
          break;
        }
        xed_decoded_inst_t x;
        xed_decoded_inst_zero_set_mode(&x, &s);
        if (xed_decode(&x, b.data() + at, std::min<uint64_t>(15, r.end - at)) !=
            XED_ERROR_NONE) {
          ++failures;
          break;
        }
        auto n = xed_decoded_inst_get_length(&x);
        seen[at] = n;
        auto cat = xed_decoded_inst_get_category(&x);
        auto kind = xed_decoded_inst_get_iclass(&x);
        auto disp = xed_decoded_inst_get_branch_displacement(&x);
        if (cat == XED_CATEGORY_RET) {
          rets.insert(at);
          break;
        }
        if (kind == XED_ICLASS_UD0 || kind == XED_ICLASS_UD1 ||
            kind == XED_ICLASS_UD2 || kind == XED_ICLASS_INT3 ||
            kind == XED_ICLASS_HLT)
          break;
        if (cat == XED_CATEGORY_CALL) {
          calls.insert(at);
          if (xed_decoded_inst_get_branch_displacement_width(&x) &&
              noreturn.count(at + n + disp))
            break;
        }
        if (cat == XED_CATEGORY_COND_BR || cat == XED_CATEGORY_UNCOND_BR) {
          if (xed_decoded_inst_get_branch_displacement_width(&x)) {
            uint64_t dest = at + n + disp;
            if (dest < start || dest >= r.end) {
              if (dest >= limit)
                ++failures;
              else {
                ++external;
                externalTargets[at] = dest;
              }
            } else
              q.push_back(dest);
          } else if (r.tables.count(at)) {
            for (auto t : r.tables[at])
              q.push_back(t);
          } else {
            ++indirect;
            unresolvedSites.insert(at);
          }
          if (cat == XED_CATEGORY_UNCOND_BR)
            break;
        }
        at += n;
      }
    }
    unsigned overlaps = 0;
    uint64_t end = 0;
    for (auto [at, n] : seen) {
      if (at < end)
        ++overlaps;
      end = at + n;
    }
    for (auto &[jump, targets] : r.tables) {
      if (!seen.count(jump))
        ++failures;
      for (auto t : targets)
        if (!seen.count(t))
          ++failures;
    }
    std::cout << std::hex << start << '\t' << r.end << std::dec << '\t'
              << seen.size() << '\t' << failures << '\t' << indirect << '\t'
              << overlaps << '\t' << calls.size() << '\t' << external << '\t'
              << r.tables.size() << '\n';
    for (auto [at, n] : seen)
      std::cerr << "INSN\t" << std::hex << start << '\t' << at << '\t' << n
                << '\n';
    for (auto at : unresolvedSites)
      std::cerr << "UNRESOLVED\t" << std::hex << start << '\t' << at << '\n';
    for (auto [at, dest] : externalTargets)
      std::cerr << "EXTERNAL\t" << std::hex << start << '\t' << at << '\t'
                << dest << '\n';
    bad |= failures || indirect || overlaps || external;
  }
  return bad ? 1 : 0;
}
