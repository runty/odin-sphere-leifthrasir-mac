#!/usr/bin/env python3
"""Conservative signed32 relative switch-table recognition from bounded XED CFG evidence."""

from pathlib import Path
import re, struct, json, collections, bisect, csv, hashlib, argparse
from common import load_atlas, load_elf, number

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--config", type=Path, required=True)
args = parser.parse_args()
config = json.loads(args.config.read_text())
base = config["canonical_base"]
image = Path(config["image"]).read_bytes()
limit = config["executable_limit"]
elf = load_elf(config["elf"])
noreturn = set(config["noreturn_offsets"])
atlas = load_atlas(config["atlas"], config["module"], base, limit)
starts = [int(r["canonical_start"], 16) - base for r in atlas]
nodes = {}
pred = collections.defaultdict(set)
for l in Path("boundaries.tsv").read_text().splitlines():
    p, n, cat, kind, t = l.split("\t")
    p, n, t = int(p, 16), int(n, 16), int(t, 16)
    nodes[p] = (n, cat, kind, t)
for p, (n, cat, kind, t) in nodes.items():
    if (
        cat not in ("RET", "UNCOND_BR")
        and kind not in ("UD0", "UD1", "UD2", "INT3", "HLT")
        and not (cat == "CALL" and t in noreturn)
    ):
        pred[p + n].add(p)
    if t and cat in ("COND_BR", "UNCOND_BR", "CALL"):
        pred[t].add(p)
keys = sorted(nodes)


def family(s):
    s = s.lower()
    if re.fullmatch(r"r(?:[89]|1[0-5])(?:d|w|b)?", s):
        return s.rstrip("dwb")
    for long, aliases in [
        ("rax", ["eax", "ax", "al", "ah"]),
        ("rbx", ["ebx", "bx", "bl", "bh"]),
        ("rcx", ["ecx", "cx", "cl", "ch"]),
        ("rdx", ["edx", "dx", "dl", "dh"]),
        ("rsi", ["esi", "si", "sil"]),
        ("rdi", ["edi", "di", "dil"]),
        ("rbp", ["ebp", "bp", "bpl"]),
        ("rsp", ["esp", "sp", "spl"]),
    ]:
        if s == long or s in aliases:
            return long
    return s


def reg32(s):
    return s.startswith("e") or bool(re.fullmatch(r"r\d+d", s))


def operand_width(s):
    if (
        s.startswith("byte ptr ")
        or s
        in ("al", "bl", "cl", "dl", "ah", "bh", "ch", "dh", "sil", "dil", "bpl", "spl")
        or re.fullmatch(r"r(?:[89]|1[0-5])b", s)
    ):
        return 8
    if (
        s.startswith("word ptr ")
        or s in ("ax", "bx", "cx", "dx", "si", "di", "bp", "sp")
        or re.fullmatch(r"r(?:[89]|1[0-5])w", s)
    ):
        return 16
    if reg32(s):
        return 32
    if re.fullmatch(r"r(?:ax|bx|cx|dx|si|di|bp|sp|[89]|1[0-5])", s):
        return 64
    return None


def overlap(p, n):
    i = bisect.bisect_left(keys, p)
    return (i > 0 and keys[i - 1] + nodes[keys[i - 1]][0] > p) or (
        i < len(keys) and keys[i] < p + n and keys[i] != p
    )


def writes(x, regs):
    return any(family(reg) in regs for reg, _ in x["writes"])


def fail(s):
    raise ValueError(s)


accepted = []
rejected = []
for group in Path("candidates.tsv").read_text().split("CANDIDATE\t")[1:]:
    ls = group.splitlines()
    dispatch = int(ls[0], 16)
    w = []
    for l in ls[1:]:
        pc, n, k, t, asm, wr = l.split("\t")
        w.append(
            dict(
                pc=int(pc, 16),
                n=int(n, 16),
                kind=k,
                target=int(t, 16),
                asm=asm,
                writes=[v.split(":") for v in wr.split(",") if v],
            )
        )
    try:
        if len(w) < 6:
            fail("short predecessor window")
        j = w[-1]
        mj = re.fullmatch(r"jmp (r\w+)", j["asm"])
        if not mj:
            fail("unrecognized register JMP")
        dst = mj.group(1)
        addi = None
        breg = None
        cleanup_kinds = {
            "MOV",
            "MOVZX",
            "MOVSX",
            "MOVSXD",
            "LEA",
            "NOP",
            "XOR",
            "AND",
            "OR",
            "ADD",
            "SUB",
            "INC",
            "DEC",
            "CMP",
            "TEST",
        }
        for k in range(len(w) - 2, max(-1, len(w) - 11), -1):
            z = w[k]
            md = re.fullmatch(r"add " + dst + r", (r\w+)", z["asm"])
            if md:
                addi = k
                breg = md.group(1)
                break
            if z["kind"] not in cleanup_kinds or writes(z, {dst}):
                fail("post-ADD cleanup clobbers target or changes control flow")
        if addi is None or dst == breg:
            fail("unrecognized ADD/JMP register identity")
        mi = None
        idx = None
        for k in range(addi - 1, max(-1, addi - 8), -1):
            mm = re.fullmatch(
                r"movsxd " + dst + r", dword ptr \[" + breg + r"\+(r\w+)\*4\]",
                w[k]["asm"],
            )
            if mm:
                mi = k
                idx = mm.group(1)
                break
            if writes(w[k], {dst, breg}) or w[k]["kind"].startswith(
                ("J", "CALL", "RET")
            ):
                break
        if mi is None or idx == breg:
            fail("unrecognized or clobbered signed32 indexed load")
        ai = None
        disp = None
        base_source = breg
        base_copies = []
        for k in range(mi - 1, max(-1, mi - 9), -1):
            z = w[k]
            ma = re.fullmatch(
                r"lea " + base_source + r", ptr \[rip([+-]0x[0-9a-f]+)\]", z["asm"]
            )
            if ma:
                if base_source == idx:
                    fail("base materialization clobbers indexed value")
                ai = k
                disp = int(ma.group(1), 16)
                break
            cp = re.fullmatch(r"mov " + base_source + r", (\w+)", z["asm"])
            if cp and operand_width(cp.group(1)) == 64:
                if base_source == idx or family(cp.group(1)) == idx:
                    fail("base copy overlaps live indexed value")
                base_copies.append(z["pc"])
                base_source = cp.group(1)
                continue
            # Before the indexed load the old target value is not live. Track
            # only the current base source and index through full64 copies.
            if writes(z, {idx, base_source}) or z["kind"].startswith(
                ("J", "CALL", "RET")
            ):
                break
        if ai is None:
            fail("unrecognized or clobbered RIP table base")
        gi = None
        for k in range(ai - 1, max(-1, ai - 8), -1):
            if w[k]["kind"] == "JNBE":
                gi = k
                break
            if writes(w[k], {idx, breg}) or w[k]["kind"].startswith(
                ("J", "CALL", "RET")
            ):
                break
        if gi is None or gi < 1:
            fail("no nearby unsigned JA guard")
        guard = w[gi]
        # Scheduling may separate CMP and JA. Only a bounded straight-line
        # sequence of flag-preserving operations is supported. Its writes must
        # preserve the index, except one proved32-bit copy of the unchanged
        # compared32-bit value. The future table base is not live until LEA.
        ci = None
        scheduled_index_copies = []
        scheduled_kinds = {"MOV", "MOVZX", "MOVSX", "MOVSXD", "LEA", "NOP"}
        for k in range(gi - 1, max(-1, gi - 10), -1):
            z = w[k]
            if z["kind"] == "CMP":
                ci = k
                break
            if z["kind"] not in scheduled_kinds:
                fail("unsupported instruction between CMP and unsigned JA")
            if writes(z, {"rflags"}):
                fail("scheduled instruction clobbers flags")
            if writes(z, {idx}):
                mv = re.fullmatch(r"mov (\w+), (\w+)", z["asm"])
                if (
                    z["kind"] == "MOV"
                    and mv
                    and operand_width(mv.group(1)) == operand_width(mv.group(2)) == 32
                    and family(mv.group(1)) == idx
                    and family(mv.group(2)) != idx
                ):
                    scheduled_index_copies.append((k, mv.group(1), mv.group(2)))
                else:
                    fail("scheduled instruction clobbers indexed value")
        if ci is None:
            fail("no bounded flag-preserving CMP/JA chain")
        cmp = w[ci]
        cm = re.fullmatch(r"cmp (\w+), (0x[0-9a-f]+)", cmp["asm"])
        if not cm:
            fail("guard compare is not register/immediate")
        creg, N = cm.group(1), int(cm.group(2), 16)
        late_index_copy = None
        if scheduled_index_copies:
            if len(scheduled_index_copies) != 1:
                fail("multiple scheduled index definitions are outside late-copy proof")
            late_index, late_destination, late_source = scheduled_index_copies[0]
            if operand_width(creg) != 32 or late_source != creg:
                fail("late index copy does not match the32-bit compared value")
            if any(
                writes(w[q], {family(creg)})
                for q in range(ci + 1, gi)
                if q != late_index
            ):
                fail("scheduled late-copy source changed after CMP")
            late_index_copy = dict(
                pc=w[late_index]["pc"],
                kind="Late32-bit index copy after CMP",
                destination=late_destination,
                source=late_source,
                comparison_pc=cmp["pc"],
            )
        wide_guard = creg == idx and bool(
            re.fullmatch(r"r(?:ax|bx|cx|dx|si|di|sp|bp|[89]|1[0-5])", creg)
        )
        narrow_width = (
            operand_width(creg) if creg not in ("ah", "bh", "ch", "dh") else None
        )
        narrow_guard = narrow_width in (8, 16)
        if not (reg32(creg) or wide_guard or narrow_guard) or N > 4095:
            fail("guard is not a supported bounded width")
        protected_pcs = {z["pc"] for z in w[ci + 1 :]}
        definitions = []
        for k in range(ci + 1, len(w)):
            if pred[w[k]["pc"]] != {w[k - 1]["pc"]}:
                fail("alternate/absent predecessor within protected dispatch path")
        if guard["target"] in {z["pc"] for z in w[gi + 1 :]}:
            fail("guard default enters dispatch")
        copy = late_index_copy["pc"] if late_index_copy else None
        if late_index_copy:
            definitions.append(late_index_copy)
        elif family(creg) != idx:
            for k in range(ci - 1, -1, -1):
                z = w[k]
                pattern = (r"movzx (\w+), " if narrow_guard else r"mov (\w+), ") + creg
                mv = re.fullmatch(pattern, z["asm"])
                if (
                    mv
                    and operand_width(mv.group(1))
                    in ((32, 64) if narrow_guard else (32,))
                    and family(mv.group(1)) == idx
                ):
                    # The value-equivalence copy must execute on every path to CMP.
                    for q in range(k + 1, ci + 1):
                        if pred[w[q]["pc"]] != {w[q - 1]["pc"]}:
                            fail(
                                "copy does not dominate CMP on a unique predecessor chain"
                            )
                        protected_pcs.add(w[q]["pc"])
                    copy = z["pc"]
                    break
                if writes(z, {idx, family(creg)}) or z["kind"].startswith(
                    ("CALL", "J", "RET")
                ):
                    break
            if copy is None:
                fail("guard/index equivalence not proven by unchanged32-bit copy")
        wm = {x["pc"]: x for x in w}
        memo = {}

        def zero_before(pc, active=frozenset()):
            if pc in memo:
                return memo[pc]
            if pc in active or not pred[pc]:
                return False
            protected_pcs.add(pc)
            for pp in pred[pc]:
                if pp not in wm:
                    return False
                z = wm[pp]
                if z["kind"].startswith("CALL"):
                    return False
                wr = [
                    (reg, int(width))
                    for reg, width in z["writes"]
                    if family(reg) == idx
                ]
                if wr:
                    if all(width == 32 for reg, width in wr):
                        definitions.append(dict(pc=pp, kind="32-bit index definition"))
                        continue
                    if z["kind"] in ("MOVSX", "MOVSXD") and z["asm"].startswith(
                        ("movsx " + idx + ",", "movsxd " + idx + ",")
                    ):
                        definitions.append(
                            dict(pc=pp, kind="Sign-extended index definition")
                        )
                        continue
                    return False
                if not zero_before(pp, active | {pc}):
                    return False
            memo[pc] = True
            return True

        narrow_memo = {}

        def narrow_zero_before(reg, pc, active=frozenset()):
            key = (reg, pc)
            if key in narrow_memo:
                return narrow_memo[key]
            if key in active or not pred[pc]:
                return False
            protected_pcs.add(pc)
            for pp in pred[pc]:
                if pp not in wm:
                    return False
                z = wm[pp]
                if z["kind"].startswith("CALL"):
                    return False
                wr = [(r, int(width)) for r, width in z["writes"] if family(r) == reg]
                if wr:
                    mv = re.fullmatch(r"movzx (\w+), (.+)", z["asm"])
                    if (
                        mv
                        and family(mv.group(1)) == reg
                        and operand_width(mv.group(1)) in (32, 64)
                        and operand_width(mv.group(2)) in (8, 16)
                        and operand_width(mv.group(2)) <= narrow_width
                    ):
                        definitions.append(
                            dict(
                                pc=pp,
                                kind="MOVZX",
                                destination=mv.group(1),
                                source=mv.group(2),
                                zero_above=operand_width(mv.group(2)),
                            )
                        )
                        continue
                    cp = re.fullmatch(r"mov (\w+), (\w+)", z["asm"])
                    if (
                        cp
                        and family(cp.group(1)) == reg
                        and operand_width(cp.group(1)) in (32, 64)
                        and operand_width(cp.group(2)) == operand_width(cp.group(1))
                    ):
                        if not narrow_zero_before(
                            family(cp.group(2)), pp, active | {key}
                        ):
                            return False
                        definitions.append(
                            dict(
                                pc=pp,
                                kind="MOV-full-register",
                                destination=cp.group(1),
                                source=cp.group(2),
                            )
                        )
                        continue
                    return False
                if not narrow_zero_before(reg, pp, active | {key}):
                    return False
            narrow_memo[key] = True
            return True

        assumptions = []
        if narrow_guard:
            structural_protected_pcs = set(protected_pcs) | {cmp["pc"]}
            if not narrow_zero_before(idx, cmp["pc"]):
                if config.get("conditional_narrow", False) and family(creg) == idx:
                    # This is never enabled by the strict catalog driver. The
                    # separate conditional pass may precompile these byte-exact
                    # destinations, without claiming the unproved index bound.
                    assumptions = [
                        {
                            "kind": "zero-upper-index",
                            "full_index_register": idx,
                            "guard_register": creg,
                            "guard_width": narrow_width,
                            "at_pc": cmp["pc"],
                            "at_pc_canonical": cmp["pc"] + base,
                            "expression": f"{idx} >> {narrow_width} == 0",
                            "proof_status": "unproven",
                            "required_at": "guard comparison and unchanged indexed load",
                        }
                    ]
                    protected_pcs = structural_protected_pcs
                    definitions = []
                else:
                    fail("narrow guard lacks a dominating full-index MOVZX/copy proof")
        elif not wide_guard and not late_index_copy:
            structural_protected_pcs = set(protected_pcs) | {cmp["pc"]}
            if not zero_before(cmp["pc"]):
                if config.get("conditional_narrow", False) and family(creg) == idx:
                    assumptions = [{
                        "kind": "zero-upper-index",
                        "full_index_register": idx,
                        "guard_register": creg,
                        "guard_width": 32,
                        "at_pc": cmp["pc"],
                        "at_pc_canonical": cmp["pc"] + base,
                        "expression": f"{idx} >> 32 == 0",
                        "proof_status": "unproven",
                        "required_at": "guard comparison and unchanged indexed load",
                    }]
                    protected_pcs = structural_protected_pcs
                    definitions = []
                else:
                    fail("index upper-zero/sign extension not dominated within bounded CFG")
        a = w[ai]
        table = a["pc"] + a["n"] + disp
        count = N + 1
        if not 0 <= table <= len(image) - count * 4:
            fail("table byte range invalid")
        raw = image[table : table + count * 4]
        elf.file_match(table, raw, readonly=True)
        offsets = struct.unpack("<" + "i" * count, raw)
        targets = sorted({table + v for v in offsets})
        oi = bisect.bisect_right(starts, dispatch) - 1
        if oi < 0:
            fail("no unwind owner")
        owner = starts[oi]
        end = int(atlas[oi]["canonical_end"], 16) - base
        if not owner <= dispatch < end or not all(
            owner <= t < end for t in targets + [guard["target"]]
        ):
            fail("case/default outside owning unwind range")
        if any(overlap(x["pc"], x["n"]) for x in w[ci:]):
            fail("overlapping dispatch instructions")
        if any(t in nodes and overlap(t, nodes[t][0]) for t in targets):
            fail("target conflicts with established boundary")
        accepted.append(
            dict(
                owner=owner,
                end=end,
                fde=atlas[oi]["fde_va"],
                dispatch=dispatch,
                table=table,
                count=count,
                targets=targets,
                default=guard["target"],
                guard=cmp["pc"],
                index=idx,
                index_copy=copy,
                **(
                    dict(
                        base_copies=base_copies,
                        target_add=w[addi]["pc"],
                        post_add_cleanup=[z["pc"] for z in w[addi + 1 : -1]],
                    )
                    if base_copies or addi + 1 < len(w) - 1
                    else {}
                ),
                guard_width=narrow_width if narrow_guard else 64 if wide_guard else 32,
                protected_pcs=sorted(protected_pcs),
                index_definitions=definitions,
                proof_window=w,
                offsets=offsets,
                table_sha256=hashlib.sha256(raw).hexdigest(),
            )
        )
        if late_index_copy:
            accepted[-1]["late_index_copy"] = late_index_copy
        if assumptions:
            accepted[-1]["conditional"] = True
            accepted[-1]["assumptions"] = assumptions
            accepted[-1]["strict_rejection"] = (
                "32-bit guard lacks a dominating full-index extension proof"
                if assumptions[0]["guard_width"] == 32 else
                "narrow guard lacks a dominating full-index MOVZX/copy proof"
            )
    except ValueError as e:
        rejected.append(dict(dispatch=dispatch, reason=str(e), window=w))
# Every table edge must respect each accepted guard-to-dispatch protected interval.
changed = True
while changed:
    changed = False
    edges = collections.defaultdict(set)
    for s in accepted:
        for t in s["targets"]:
            edges[t].add(s["dispatch"])
    keep = []
    for s in accepted:
        w = s["proof_window"]
        protected = s["protected_pcs"]
        if any(edges[pc] for pc in protected):
            rejected.append(
                dict(
                    dispatch=s["dispatch"],
                    reason="accepted table creates alternate protected-path entry",
                )
            )
            changed = True
        else:
            keep.append(s)
    accepted = keep
Path("preliminary.json").write_text(json.dumps(accepted, indent=2) + "\n")
Path("rejected.json").write_text(json.dumps(rejected, indent=2) + "\n")
Path("regions.tsv").write_text(
    "".join(
        f"{lo:x}\t{hi:x}\n"
        for lo, hi in sorted({(s["owner"], s["end"]) for s in accepted})
    )
)
Path("tables.tsv").write_text(
    "".join(
        f"{s['owner']:x}\t{s['dispatch']:x}\t{s['table']:x}\t{s['count']:x}\n"
        for s in accepted
    )
)
print(
    "candidates",
    len(accepted) + len(rejected),
    "preliminary",
    len(accepted),
    "owners",
    len({s["owner"] for s in accepted}),
    "unique_cases",
    len({t for s in accepted for t in s["targets"]}),
)
print("rejections", dict(collections.Counter(s["reason"] for s in rejected)))
