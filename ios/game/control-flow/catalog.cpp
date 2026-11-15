// Authored bounded XED control-flow scanner; paths and module bounds are CLI
// inputs.
extern "C" {
#include <xed/xed-interface.h>
}
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
struct Inst {
  unsigned n;
  xed_category_enum_t cat;
  xed_iclass_enum_t kind;
  uint64_t branch;
};
int main(int argc, char **argv) {
  if (argc != 5)
    return 2;
  std::ifstream f(argv[1], std::ios::binary);
  std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
  std::ifstream roots(argv[2]);
  std::vector<uint64_t> q;
  uint64_t p;
  while (roots >> std::hex >> p)
    q.push_back(p);
  std::vector<uint64_t> origin = q;
  std::map<uint64_t, uint64_t> owner;
  uint64_t limit = std::stoull(argv[3], nullptr, 16);
  if (!f || limit > b.size())
    return 3;
  for (auto root : q)
    if (root >= limit)
      return 3;
  std::set<uint64_t> noreturn;
  {
    std::ifstream nf(argv[4]);
    uint64_t v;
    while (nf >> std::hex >> v)
      noreturn.insert(v);
  }
  xed_tables_init();
  xed_state_t s;
  xed_state_init2(&s, XED_MACHINE_MODE_LONG_64, XED_ADDRESS_WIDTH_64b);
  std::map<uint64_t, Inst> seen;
  std::map<uint64_t, std::vector<uint64_t>> prev;
  unsigned fail = 0;
  std::set<uint64_t> invalid;
  auto decode = [&](uint64_t pc, xed_decoded_inst_t &x) {
    xed_decoded_inst_zero_set_mode(&x, &s);
    return xed_decode(&x, b.data() + pc, std::min<uint64_t>(15, limit - pc)) ==
           XED_ERROR_NONE;
  };
  for (size_t z = 0; z < q.size(); ++z) {
    auto pc = q[z];
    while (pc < limit && !seen.count(pc) && !invalid.count(pc)) {
      xed_decoded_inst_t x;
      if (!decode(pc, x)) {
        invalid.insert(pc);
        ++fail;
        break;
      }
      auto n = xed_decoded_inst_get_length(&x);
      auto c = xed_decoded_inst_get_category(&x);
      auto k = xed_decoded_inst_get_iclass(&x);
      uint64_t t = 0;
      if (xed_decoded_inst_get_branch_displacement_width(&x))
        t = pc + n + xed_decoded_inst_get_branch_displacement(&x);
      seen[pc] = {n, c, k, t};
      owner[pc] = origin[z];
      prev[pc + n].push_back(pc);
      if (c == XED_CATEGORY_COND_BR || c == XED_CATEGORY_UNCOND_BR ||
          c == XED_CATEGORY_CALL) {
        if (t < limit && t) {
          q.push_back(t);
          origin.push_back(origin[z]);
        }
      }
      if (c == XED_CATEGORY_RET || c == XED_CATEGORY_UNCOND_BR ||
          k == XED_ICLASS_UD0 || k == XED_ICLASS_UD1 || k == XED_ICLASS_UD2 ||
          k == XED_ICLASS_INT3 || k == XED_ICLASS_HLT ||
          (c == XED_CATEGORY_CALL && noreturn.count(t)))
        break;
      pc += n;
    }
  }
  std::ofstream bounds("boundaries.tsv");
  for (auto [pc, x] : seen)
    bounds << std::hex << pc << '\t' << x.n << '\t'
           << xed_category_enum_t2str(x.cat) << '\t'
           << xed_iclass_enum_t2str(x.kind) << '\t' << x.branch << '\n';
  std::ofstream owners("owners.tsv");
  for (auto [pc, o] : owner)
    owners << std::hex << pc << '\t' << o << '\n';
  unsigned candidates = 0;
  for (auto [pc, i] : seen) {
    if (i.cat != XED_CATEGORY_UNCOND_BR || i.branch)
      continue;
    std::vector<uint64_t> w{pc};
    auto at = pc;
    for (int j = 0; j < 40; ++j) {
      if (prev[at].size() != 1)
        break;
      at = prev[at][0];
      w.push_back(at);
    }
    if (w.size() < 4)
      continue;
    // The computed target stays live through bounded independent cleanup.
    // Flags, the consumed index, and the table base may be dead after ADD.
    xed_decoded_inst_t jump;
    decode(pc, jump);
    auto target_reg = xed_decoded_inst_get_reg(&jump, XED_OPERAND_REG0);
    if (xed_reg_class(target_reg) != XED_REG_CLASS_GPR ||
        xed_get_register_width_bits64(target_reg) != 64)
      continue;
    bool found_add = false;
    for (size_t k = 1; k < w.size() && k <= 9; ++k) {
      xed_decoded_inst_t x;
      decode(w[k], x);
      auto kind = seen[w[k]].kind;
      auto dest = xed_decoded_inst_get_reg(&x, XED_OPERAND_REG0);
      if (kind == XED_ICLASS_ADD && dest == target_reg) {
        found_add = true;
        break;
      }
      if (kind != XED_ICLASS_MOV && kind != XED_ICLASS_MOVZX &&
          kind != XED_ICLASS_MOVSX && kind != XED_ICLASS_MOVSXD &&
          kind != XED_ICLASS_LEA && kind != XED_ICLASS_NOP &&
          kind != XED_ICLASS_XOR && kind != XED_ICLASS_AND &&
          kind != XED_ICLASS_OR && kind != XED_ICLASS_ADD &&
          kind != XED_ICLASS_SUB && kind != XED_ICLASS_INC &&
          kind != XED_ICLASS_DEC && kind != XED_ICLASS_CMP &&
          kind != XED_ICLASS_TEST)
        break;
      bool clobber = false;
      const auto *inst = xed_decoded_inst_inst(&x);
      for (unsigned j = 0; j < xed_inst_noperands(inst); ++j) {
        const auto *op = xed_inst_operand(inst, j);
        auto name = xed_operand_name(op);
        if (xed_operand_is_register(name) && xed_operand_written(op) &&
            xed_get_largest_enclosing_register(
                xed_decoded_inst_get_reg(&x, name)) == target_reg)
          clobber = true;
      }
      if (clobber)
        break;
    }
    if (!found_add)
      continue;
    std::reverse(w.begin(), w.end());
    ++candidates;
    std::cout << "CANDIDATE\t" << std::hex << pc << '\n';
    for (auto v : w) {
      xed_decoded_inst_t x;
      decode(v, x);
      char buf[512] = {};
      xed_format_context(XED_SYNTAX_INTEL, &x, buf, sizeof(buf), v, nullptr,
                         nullptr);
      std::cout << std::hex << v << '\t' << seen[v].n << '\t'
                << xed_iclass_enum_t2str(seen[v].kind) << '\t' << seen[v].branch
                << '\t' << buf << '\t';
      const xed_inst_t *xi = xed_decoded_inst_inst(&x);
      for (unsigned j = 0; j < xed_inst_noperands(xi); ++j) {
        const auto *o = xed_inst_operand(xi, j);
        auto name = xed_operand_name(o);
        if (xed_operand_is_register(name) && xed_operand_written(o)) {
          auto reg = xed_decoded_inst_get_reg(&x, name);
          std::cout << xed_reg_enum_t2str(reg) << ':' << std::dec
                    << xed_get_register_width_bits64(reg) << ',';
        }
      }
      std::cout << '\n';
    }
  }
  std::cerr << "decoded=" << seen.size() << " decode_failures=" << fail
            << " candidates=" << candidates
            << " roots_and_direct_edges=" << q.size() << '\n';
  return 0;
}
