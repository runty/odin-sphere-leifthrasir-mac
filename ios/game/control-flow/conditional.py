#!/usr/bin/env python3
"""Separate compile-only candidates with explicitly unproved narrow-index assumptions.

The strict scan is immutable input. This pass never publishes strict accepted
owners/tables and never feeds conditional roots or edges back to that scan.
"""

from pathlib import Path
import argparse, datetime, hashlib, json, re, subprocess, sys
from catalog import run
from common import load_elf, load_atlas, number, sha


def snapshot(folder):
    return {
        str(f.relative_to(folder)): sha(f) for f in folder.rglob("*") if f.is_file()
    }


def conditional_result(folder, config):
    pool = json.loads((folder / "preliminary.json").read_text())
    rows = {}
    for line in (folder / "validation.tsv").read_text().splitlines():
        row = line.split("\t")
        if len(row) != 9:
            raise ValueError("Malformed conditional CFG validation output")
        rows[int(row[0], 16)] = row
    decoded, unresolved, external = {}, {}, {}
    for line in (folder / "validation-boundaries.tsv").read_text().splitlines():
        fields = line.split("\t")
        kind, owner, pc = fields[0], int(fields[1], 16), int(fields[2], 16)
        if kind == "INSN" and len(fields) == 4:
            decoded.setdefault(owner, set()).add(pc)
        elif kind == "UNRESOLVED" and len(fields) == 3:
            unresolved.setdefault(owner, set()).add(pc)
        elif kind == "EXTERNAL" and len(fields) == 4:
            external.setdefault(owner, {})[pc] = int(fields[3], 16)
        else:
            raise ValueError("Malformed conditional detailed CFG evidence")
    candidates, rejected = [], []
    base = config["canonical_base"]
    image = Path(config["image"]).read_bytes()
    elf = load_elf(config["elf"])
    for table in pool:
        if not table.get("assumptions"):
            continue  # Never publish otherwise-strict tables discovered via conditional closure.
        owner = table["owner"]
        row = rows.get(owner)
        required = set(
            table["targets"] + [table["default"], table["guard"], table["dispatch"]]
        )
        required.update(table["protected_pcs"])
        if (
            row is None
            or int(row[3])
            or int(row[5])
            or not required <= decoded.get(owner, set())
        ):
            rejected.append(
                {
                    "dispatch": table["dispatch"] + base,
                    "reason": "decode/overlap/reachability failure",
                    "owner_row": row,
                }
            )
            continue
        table["elf_table_file_offset"], table["elf_segment_flags"] = elf.file_match(
            table["table"],
            image[table["table"] : table["table"] + 4 * table["count"]],
            readonly=True,
        )
        table["owner_cfg"] = {
            "complete": False,
            "analysis_uses_unproven_assumptions": True,
            "decode_or_reachability_failures": int(row[3]),
            "instruction_overlaps": int(row[5]),
            "unresolved_indirect_branches": int(row[4]),
            "external_direct_branches": int(row[7]),
            "opaque_call_sites": int(row[6]),
            "unresolved_indirect_pcs": [
                v + base for v in sorted(unresolved.get(owner, set()))
            ],
            "external_direct_edges": [
                {"pc": pc + base, "target": target + base}
                for pc, target in sorted(external.get(owner, {}).items())
            ],
            "claim": "Conditional compile-only candidate; full-index upper bits and unseen incoming paths are not proven",
        }
        candidates.append(table)
    entries = {}
    for table in candidates:
        for target in sorted(set(table["targets"] + [table["default"]])):
            entries.setdefault(f"{target + base:x}", []).append(
                {
                    "owner": table["owner"] + base,
                    "end": table["end"] + base,
                    "dispatch": table["dispatch"] + base,
                    "table": table["table"] + base,
                    "is_default": target == table["default"],
                    "assumptions": table["assumptions"],
                    "claim": "Compile-only under explicit unproven upper-index assumption",
                }
            )
    targets = sorted(int(v, 16) for v in entries)
    (folder / "conditional-tables.json").write_text(
        json.dumps(candidates, indent=2) + "\n"
    )
    (folder / "conditional-entries.json").write_text(
        json.dumps(entries, indent=2) + "\n"
    )
    (folder / "conditional-rejections.json").write_text(
        json.dumps(rejected, indent=2) + "\n"
    )
    (folder / "conditional-targets.txt").write_text(
        "".join(f"{v:x}\n" for v in targets)
    )
    result = {
        "conditional_tables": len(candidates),
        "conditional_owners": len({s["owner"] for s in candidates}),
        "conditional_slots": sum(s["count"] for s in candidates),
        "conditional_targets": len(targets),
        "unconditionally_complete_owners": 0,
        "rejected_conditional_tables": len(rejected),
    }
    (folder / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    return result, pool


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ["strict-output", "output", "xed-root"]:
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--passes", type=int, default=3)
    parser.add_argument("--cxx", default="clang++")
    args = parser.parse_args()
    strict, out = args.strict_output.resolve(), args.output.resolve()
    source = Path(__file__).resolve().parent
    if not 1 <= args.passes <= 8:
        parser.error("--passes must be1..8")
    if (
        len(out.parts) < 3
        or out.parts[1] != "Volumes"
        or not Path("/Volumes", out.parts[2]).is_mount()
    ):
        parser.error("Conditional output must be on a mounted external disk")
    if (
        out == source
        or out == strict
        or out.is_relative_to(strict)
        or strict.is_relative_to(out)
    ):
        parser.error("Conditional output must be separate from the strict scan")
    strict_before = snapshot(strict)
    provenance = json.loads((strict / "provenance.json").read_text())
    noreturn_provenance = provenance.get("noreturn_provenance")
    if (
        noreturn_provenance
        and sha(Path(noreturn_provenance["path"])) != noreturn_provenance["sha256"]
    ):
        raise ValueError("Noreturn provenance no longer matches the strict scan")
    iteration = provenance["passes"][-1]["iteration"]
    strict_pass = strict / f"pass-{iteration}"
    config = json.loads((strict_pass / "config.json").read_text())
    if config.get("conditional_narrow"):
        raise ValueError("Input is not an unconditional strict scan")
    elf = load_elf(config["elf"])
    load_atlas(config["atlas"], config["module"], config["canonical_base"], elf.limit)
    paths = {name: Path(config[name]) for name in ["image", "elf", "atlas"]}
    hashes = {name: sha(path) for name, path in paths.items()}
    if hashes != provenance["inputs"]:
        raise ValueError("Strict scan input hashes no longer match")
    input_parents = {path.parent.resolve() for path in paths.values()}
    if out in input_parents:
        parser.error("Output must not be an artifact directory")
    image = paths["image"].read_bytes()
    for va, end, offset, flags in elf.loads:
        if end > len(image) or image[va:end] != elf.data[offset : offset + end - va]:
            raise ValueError("Prepared image differs from ELF load bytes")
    roots = {int(v, 16) for v in (strict_pass / "roots.txt").read_text().splitlines()}
    config["conditional_narrow"] = True
    out.mkdir(parents=True, exist_ok=True)
    tools = out / "tools"
    tools.mkdir(exist_ok=True)
    prefix = args.xed_root.resolve()
    lib = prefix / "lib/libxed.a"
    decoder_hash = sha(lib)
    if decoder_hash != provenance["decoder"]["xed_library_sha256"]:
        raise ValueError("Conditional decoder differs from strict XED identity")
    source_hashes = {
        f.name: sha(f) for f in source.iterdir() if f.suffix in [".py", ".cpp"]
    }
    for name in ["catalog", "validate"]:
        run(
            [
                args.cxx,
                "-std=c++17",
                "-O2",
                "-I" + str(prefix / "include"),
                source / (name + ".cpp"),
                lib,
                "-o",
                tools / name,
            ],
            out / (name + "-build.log"),
        )
    passes = []
    for number_pass in range(args.passes):
        folder = out / f"conditional-pass-{number_pass}"
        folder.mkdir(exist_ok=True)
        (folder / "roots.txt").write_text("".join(f"{v:x}\n" for v in sorted(roots)))
        (folder / "noreturn.txt").write_text(
            "".join(f"{v:x}\n" for v in config["noreturn_offsets"])
        )
        (folder / "config.json").write_text(json.dumps(config, indent=2) + "\n")
        run(
            [
                tools / "catalog",
                paths["image"],
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
                paths["image"],
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
        summary, pool = conditional_result(folder, config)
        summary["iteration"] = number_pass
        passes.append(summary)
        expanded = set(roots)
        for table in pool:
            expanded.update(table["targets"])
            expanded.add(table["default"])
        if roots == expanded:
            break
        roots = expanded
    if (
        strict_before != snapshot(strict)
        or hashes != {name: sha(path) for name, path in paths.items()}
        or decoder_hash != sha(lib)
        or source_hashes
        != {f.name: sha(f) for f in source.iterdir() if f.suffix in [".py", ".cpp"]}
    ):
        raise RuntimeError(
            "Strict scan/input/decoder/source changed; conditional outputs not published"
        )
    if (
        noreturn_provenance
        and sha(Path(noreturn_provenance["path"])) != noreturn_provenance["sha256"]
    ):
        raise RuntimeError(
            "Noreturn provenance changed; conditional outputs not published"
        )
    names = [
        "conditional-targets.txt",
        "conditional-tables.json",
        "conditional-entries.json",
        "conditional-rejections.json",
    ]
    for name in names:
        (out / name).write_bytes((folder / name).read_bytes())
    targets = {
        int(v, 16) for v in (out / "conditional-targets.txt").read_text().splitlines()
    }
    original = {
        int(v, 16) for v in (strict / "prefetch-targets.txt").read_text().splitlines()
    }
    compiled = set()
    if config.get("trace_directory"):
        compiled = {
            int(f.stem, 16)
            for f in Path(config["trace_directory"]).glob("*.ll")
            if re.fullmatch("[0-9a-fA-F]+", f.stem)
        }
    union = original | targets
    for name, values in [
        ("missing-conditional-targets.txt", targets - compiled),
        ("prefetch-with-conditional-targets.txt", union),
        ("missing-prefetch-with-conditional-targets.txt", union - compiled),
    ]:
        (out / name).write_text("".join(f"{v:x}\n" for v in sorted(values)))
        names.append(name)
    manifest = {
        "schema": 1,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "claim": "Compile-only conditional candidates; never unconditional accepted tables or complete owners",
        "module": config["module"],
        "canonical_base": config["canonical_base"],
        "compiler": args.cxx,
        "compiler_version": subprocess.check_output([args.cxx, "--version"], text=True),
        "conditional_targets": len(targets),
        "missing_conditional_targets": len(targets - compiled),
        "compiled_snapshot_count": len(compiled),
        "strict_output": str(strict),
        "strict_snapshot_sha256": hashlib.sha256(
            json.dumps(strict_before, sort_keys=True).encode()
        ).hexdigest(),
        "strict_outputs_unchanged": True,
        "strict_output_hashes": {
            name: sha(strict / name) for name in provenance["outputs"]
        },
        "inputs": hashes,
        "noreturn_provenance": noreturn_provenance,
        "decoder_sha256": decoder_hash,
        "tool_source": source_hashes,
        "passes": passes,
        "outputs": {name: sha(out / name) for name in names},
        "limits": [
            "Only same-family low8/16-bit comparisons with explicit full_index>>guard_width==0 assumption",
            "No unproved differing-register value-equivalence copy is waived",
            "Conditional edges/roots never enter the strict scan; otherwise-strict tables reached via conditional closure are not published here",
            "Unknown incoming paths and unresolved/external exits remain explicit; no runtime branch matching, clamps, callbacks, or input values are changed",
        ],
    }
    (out / "provenance.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(passes[-1], sort_keys=True))


if __name__ == "__main__":
    main()
