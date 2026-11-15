#!/usr/bin/env python3
"""Create a conservative internal-switch atlas from an owned ELF/image and validated unwind entry list.

This only reads the supplied artifacts. It never lifts code, runs the game, or edits its cache.
"""

from pathlib import Path
import argparse, hashlib, json, subprocess, sys, datetime, re
from common import load_atlas, load_elf, number, sha


def run(command, log, cwd=None, stderr=None, allowed=(0,)):
    with Path(log).open("w") as out:
        if stderr:
            with Path(stderr).open("w") as err:
                result = subprocess.run(
                    list(map(str, command)), cwd=cwd, stdout=out, stderr=err
                )
        else:
            result = subprocess.run(
                list(map(str, command)), cwd=cwd, stdout=out, stderr=subprocess.STDOUT
            )
    if result.returncode not in allowed:
        raise RuntimeError(f"Command failed ({result.returncode}); see {log}")


def finalize(folder, config):
    pool = json.loads((folder / "preliminary.json").read_text())
    valid, rejected, partial = {}, {}, {}
    for line in (folder / "validation.tsv").read_text().splitlines():
        v = line.split("\t")
        if len(v) != 9:
            raise ValueError("Malformed CFG validation output")
        owner = int(v[0], 16)
        if all(int(v[i]) == 0 for i in [3, 4, 5, 7]):
            valid[owner] = v
        else:
            rejected[owner] = v
            # These remain incomplete owners. A known, locally guarded table
            # can still supply speculative compilation candidates when every
            # declared case is decoded without errors or instruction overlap.
            if int(v[3]) == 0 and int(v[5]) == 0:
                partial[owner] = v
    decoded, unresolved, external = {}, {}, {}
    for line in (folder / "validation-boundaries.tsv").read_text().splitlines():
        row = line.split("\t")
        kind, owner, pc = row[0], int(row[1], 16), int(row[2], 16)
        if kind == "INSN" and len(row) == 4:
            decoded.setdefault(owner, set()).add(pc)
        elif kind == "UNRESOLVED" and len(row) == 3:
            unresolved.setdefault(owner, set()).add(pc)
        elif kind == "EXTERNAL" and len(row) == 4:
            external.setdefault(owner, {})[pc] = int(row[3], 16)
        else:
            raise ValueError("Malformed detailed CFG evidence")

    def reached(table):
        required = set(
            table["targets"] + [table["default"], table["guard"], table["dispatch"]]
        )
        required.update(table["protected_pcs"])
        required.update(d["pc"] for d in table.get("index_definitions", []))
        if table.get("index_copy") is not None:
            required.add(table["index_copy"])
        return required <= decoded.get(table["owner"], set())

    accepted = [s for s in pool if s["owner"] in valid and reached(s)]
    candidates = [s for s in pool if s["owner"] in partial and reached(s)]
    entries = {}
    elf = load_elf(config["elf"])
    image = Path(config["image"]).read_bytes()
    base = config["canonical_base"]
    for table in accepted + candidates:
        start = table["table"]
        table["elf_table_file_offset"], table["elf_segment_flags"] = elf.file_match(
            start, image[start : start + 4 * table["count"]], readonly=True
        )
        if table["owner"] in partial:
            row = partial[table["owner"]]
            table["owner_cfg"] = {
                "complete": False,
                "decode_or_reachability_failures": int(row[3]),
                "unresolved_indirect_branches": int(row[4]),
                "instruction_overlaps": int(row[5]),
                "external_direct_branches": int(row[7]),
                "opaque_call_sites": int(row[6]),
                "unresolved_indirect_pcs": [
                    v + base for v in sorted(unresolved.get(table["owner"], set()))
                ],
                "external_direct_edges": [
                    {"pc": pc + base, "target": target + base}
                    for pc, target in sorted(external.get(table["owner"], {}).items())
                ],
                "claim": "Local-pattern prefetch candidate; unknown edges may bypass its proof dependencies",
            }
            continue
        for target in sorted(set(table["targets"] + [table["default"]])):
            entries.setdefault(target, []).append(
                {
                    "owner": table["owner"] + base,
                    "end": table["end"] + base,
                    "dispatch": table["dispatch"] + base,
                    "table": table["table"] + base,
                    "is_default": target == table["default"],
                    "slots": [
                        i
                        for i, v in enumerate(table["offsets"])
                        if table["table"] + v == target
                    ],
                }
            )
    values = sorted(entries)
    candidate_values = sorted(
        {v for table in candidates for v in table["targets"] + [table["default"]]}
    )
    prefetch = sorted(set(values) | set(candidate_values))
    compiled = []
    if config.get("trace_directory"):
        compiled = sorted(
            int(f.stem, 16)
            for f in Path(config["trace_directory"]).glob("*.ll")
            if re.fullmatch("[0-9a-fA-F]+", f.stem)
        )
    cache = set(compiled)
    missing = [v for v in values if v + base not in cache]
    missing_prefetch = [v for v in prefetch if v + base not in cache]
    for name, rows in [
        ("all-targets.txt", values),
        ("missing-targets.txt", missing),
        ("incomplete-owner-targets.txt", candidate_values),
        ("prefetch-targets.txt", prefetch),
        ("missing-prefetch-targets.txt", missing_prefetch),
    ]:
        (folder / name).write_text("".join(f"{v + base:x}\n" for v in rows))
    (folder / "entries.json").write_text(
        json.dumps(
            {f"{v + base:x}": refs for v, refs in sorted(entries.items())}, indent=2
        )
        + "\n"
    )
    (folder / "accepted-tables.json").write_text(json.dumps(accepted, indent=2) + "\n")
    (folder / "incomplete-owner-tables.json").write_text(
        json.dumps(candidates, indent=2) + "\n"
    )
    (folder / "rejected-owners.json").write_text(
        json.dumps({f"{v + base:x}": r for v, r in rejected.items()}, indent=2) + "\n"
    )
    summary = {
        "preliminary_tables": len(pool),
        "validated_owners": len(valid),
        "validated_tables": len(accepted),
        "validated_slots": sum(s["count"] for s in accepted),
        "validated_cfg_instructions": sum(int(v[2]) for v in valid.values()),
        "unique_cases_plus_defaults": len(values),
        "missing": len(missing),
        "incomplete_owner_tables": len(candidates),
        "incomplete_owner_candidate_targets": len(candidate_values),
        "prefetch_targets": len(prefetch),
        "missing_prefetch_targets": len(missing_prefetch),
        "compiled_snapshot_count": len(cache),
        "compiled_snapshot_sha256": hashlib.sha256(
            "".join(f"{v:x}\n" for v in compiled).encode()
        ).hexdigest(),
    }
    (folder / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    return summary, pool


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ["image", "elf", "atlas", "output", "xed-root"]:
        ap.add_argument("--" + name, type=Path, required=True)
    ap.add_argument("--module", required=True)
    ap.add_argument("--base", type=lambda x: int(x, 0), required=True)
    ap.add_argument("--passes", type=int, default=3)
    ap.add_argument(
        "--noreturn",
        action="append",
        type=lambda x: int(x, 0),
        default=[],
        help="Canonical address of independently verified nonreturning routine; repeatable",
    )
    ap.add_argument("--noreturn-provenance", type=Path)
    ap.add_argument("--trace-directory", type=Path)
    ap.add_argument("--cxx", default="clang++")
    args = ap.parse_args()
    if not 1 <= args.passes <= 8:
        ap.error("--passes must be1..8")
    image = args.image.resolve()
    elf_path = args.elf.resolve()
    atlas_path = args.atlas.resolve()
    out = args.output.resolve()
    source = Path(__file__).resolve().parent
    if (
        len(out.parts) < 3
        or out.parts[1] != "Volumes"
        or not Path("/Volumes", out.parts[2]).is_mount()
    ):
        ap.error("Analysis output must be on a mounted external disk")
    if out in [image.parent, elf_path.parent, atlas_path.parent] or out == source:
        ap.error("Output must be a separate analysis directory")
    if not 0 <= args.base < 2**64:
        ap.error("Canonical base must fit uint64")
    elf = load_elf(elf_path)
    data = image.read_bytes()
    for va, end, off, flags in elf.loads:
        if end > len(data) or data[va:end] != elf.data[off : off + end - va]:
            raise ValueError("Prepared image differs from ELF load bytes")
    input_paths = {"image": image, "elf": elf_path, "atlas": atlas_path}
    input_hashes = {name: sha(path) for name, path in input_paths.items()}
    noreturn_provenance = None
    if args.noreturn_provenance:
        path = args.noreturn_provenance.resolve()
        document = json.loads(path.read_text())
        matching = [r for r in document["targets"] if r["module"] == args.module]
        allowed = {int(r["canonical"], 0) for r in matching}
        if not set(args.noreturn) <= allowed or any(
            r["elf_sha256"] != input_hashes["elf"]
            or r["image_sha256"] != input_hashes["image"]
            for r in matching
        ):
            raise ValueError(
                "Noreturn declarations do not match their artifact-derived provenance"
            )
        noreturn_provenance = {"path": str(path), "sha256": sha(path)}
    source_hashes = {
        f.name: sha(f) for f in source.iterdir() if f.suffix in [".py", ".cpp"]
    }
    owners = load_atlas(atlas_path, args.module, args.base, elf.limit)
    roots = {number(r["canonical_start"]) - args.base for r in owners}
    noreturn = [pc - args.base for pc in args.noreturn]
    if any(not 0 <= pc < elf.limit for pc in noreturn):
        ap.error("Nonreturning target outside executable mapping")
    out.mkdir(parents=True, exist_ok=True)
    tools = out / "tools"
    tools.mkdir(exist_ok=True)
    inc = args.xed_root.resolve() / "include"
    lib = args.xed_root.resolve() / "lib/libxed.a"
    decoder_hash = sha(lib)
    for name in ["catalog", "validate"]:
        run(
            [
                args.cxx,
                "-std=c++17",
                "-O2",
                "-I" + str(inc),
                source / (name + ".cpp"),
                lib,
                "-o",
                tools / name,
            ],
            out / (name + "-build.log"),
        )
    config = {
        "image": str(image),
        "elf": str(elf_path),
        "atlas": str(atlas_path),
        "module": args.module,
        "canonical_base": args.base,
        "executable_limit": elf.limit,
        "noreturn_offsets": noreturn,
        "trace_directory": str(args.trace_directory.resolve())
        if args.trace_directory
        else None,
    }
    passes = []
    last = None
    for iteration in range(args.passes):
        folder = out / f"pass-{iteration}"
        folder.mkdir(exist_ok=True)
        (folder / "roots.txt").write_text("".join(f"{pc:x}\n" for pc in sorted(roots)))
        (folder / "noreturn.txt").write_text("".join(f"{pc:x}\n" for pc in noreturn))
        (folder / "config.json").write_text(json.dumps(config, indent=2) + "\n")
        run(
            [
                tools / "catalog",
                image,
                folder / "roots.txt",
                f"{elf.limit:x}",
                folder / "noreturn.txt",
            ],
            folder / "candidates.tsv",
            cwd=folder,
            stderr=folder / "catalog-summary.txt",
        )
        run(
            [sys.executable, source / "filter.py", "--config", folder / "config.json"],
            folder / "filter-summary.txt",
            cwd=folder,
        )
        run(
            [
                tools / "validate",
                image,
                folder / "regions.tsv",
                folder / "tables.tsv",
                f"{elf.limit:x}",
                folder / "noreturn.txt",
            ],
            folder / "validation.tsv",
            cwd=folder,
            stderr=folder / "validation-boundaries.tsv",
            allowed=(0, 1),
        )
        summary, pool = finalize(folder, config)
        summary["iteration"] = iteration
        summary["analysis_roots"] = len(roots)
        summary["unwind_roots"] = len(owners)
        passes.append(summary)
        last = folder
        expanded = set(roots)
        for table in pool:
            expanded.update(table["targets"])
            expanded.add(table["default"])
        if expanded == roots:
            break
        roots = expanded
    if (
        sha(lib) != decoder_hash
        or input_hashes != {name: sha(path) for name, path in input_paths.items()}
        or source_hashes
        != {f.name: sha(f) for f in source.iterdir() if f.suffix in [".py", ".cpp"]}
    ):
        raise RuntimeError(
            "Analysis input or source changed during the run; outputs not published"
        )
    if (
        noreturn_provenance
        and sha(Path(noreturn_provenance["path"])) != noreturn_provenance["sha256"]
    ):
        raise RuntimeError(
            "Noreturn provenance changed during analysis; outputs not published"
        )
    # Only final-pass accepted entries are published. Provisional roots never imply accepted targets.
    for name in [
        "all-targets.txt",
        "missing-targets.txt",
        "entries.json",
        "accepted-tables.json",
        "rejected-owners.json",
        "incomplete-owner-targets.txt",
        "incomplete-owner-tables.json",
        "prefetch-targets.txt",
        "missing-prefetch-targets.txt",
    ]:
        (out / name).write_bytes((last / name).read_bytes())
    manifest = {
        "schema": 1,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "claim": "Bounded immutable signed32 relative switch cases with complete per-owner CFG validation; static reachability beyond those owners unknown",
        "candidate_claim": "Incomplete-owner targets are speculative prefetch only; unknown edges may bypass local guard/definition proofs. No runtime acceptance or target restriction is changed.",
        "module": args.module,
        "canonical_base": args.base,
        "passes": passes,
        "noreturn_canonical": args.noreturn,
        "noreturn_provenance": noreturn_provenance,
        "inputs": input_hashes,
        "decoder": {
            "xed_library_sha256": decoder_hash,
            "compiler": args.cxx,
            "compiler_version": subprocess.check_output(
                [args.cxx, "--version"], text=True
            ),
        },
        "tool_source": source_hashes,
        "outputs": {
            name: sha(out / name)
            for name in [
                "all-targets.txt",
                "missing-targets.txt",
                "entries.json",
                "accepted-tables.json",
                "rejected-owners.json",
                "incomplete-owner-targets.txt",
                "incomplete-owner-tables.json",
                "prefetch-targets.txt",
                "missing-prefetch-targets.txt",
            ]
        },
        "limits": [
            "Only one executable file mapping beginning at offset0",
            "Guard must be unsigned JA with adjacent32-bit/same-index64-bit CMP or provenzero-extended8/16-bit CMP",
            "Only explicit signed32 table offsets/RIP base/local32-bit copy equivalence",
            "Preloaded loop bases, pointer tables, ambiguous8/16-bit guards and unresolved owners are excluded",
            "No external branch from an accepted owner; direct calls follow standard return continuation",
            "Analysis roots may include provisional cases, but published entries come only from full final CFG validation",
        ],
    }
    (out / "provenance.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(passes[-1], sort_keys=True))


if __name__ == "__main__":
    main()
