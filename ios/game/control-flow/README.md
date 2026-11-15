# Conservative internal switch atlas

Adapted from OpenCrown `68cb2a7b9bfc042593fdd26d7a045697cc29e65a`,
`ios/game/control-flow`. Odin adds a separate conditional candidate tier for
32-bit comparisons whose full 64-bit index lacks a local extension proof.
The strict scan remains unchanged. These tools contain no game bytes or
addresses; private analysis outputs belong on the external build disk.

The conditional pass may emit `full_index >> 32 == 0` as an explicitly
**unproven** assumption. All original instruction, predecessor, clobber,
table-byte and target-boundary checks still apply. Such candidates may be
compiled, but they do not prove the bound, reachability, execution or complete
owning control flow. The synthetic high-bit counterexample passes a low-word
comparison while indexing outside the small table. Strict output bytes match
the original tool on the expanded fixture.


These are authored generic host tools and synthetic fixtures used for private startup analysis. There are no game bytes, addresses, tables, or module maps in these source files. They read an explicit caller-supplied ELF, prepared flat image, and a previously validated unwind atlas. They never lift code, execute the input, edit a trace cache, install software, or contact devices.

## CLI

```sh
python3 catalog.py \
  --image /path/to/module.image \
  --elf /path/to/module.elf \
  --atlas /path/to/unwind-atlas/entries.json \
  --module MODULE_NAME \
  --base CANONICAL_BASE \
  --xed-root /path/to/remill-install \
  --trace-directory /path/to/traces \
  --passes 3 \
  --output /Volumes/office/Build/OdinSphere/switch-analysis
```

`--base` uses normal Python integer syntax, such as `0x400000`. The atlas is either a JSON list compatible with the existing unwind parser's `entries.json` (integer canonical addresses), or a TSV with `module`, `canonical_start`, and `canonical_end` (hex text). The producer is responsible for validating those unwind anchors. One module is selected per invocation; invoke independently for each module/base/image.

`--xed-root` supplies the existing matching host XED installation (`include/xed/xed-interface.h`, `lib/libxed.a`). `--cxx` can select the host C++ compiler. No dependency is downloaded. The scanner/validator executables build under `output/tools`. `--noreturn CANONICAL_ADDRESS` is repeatable and must only name independently verified nonreturning callees; its default is empty, with no hidden routine address. `--trace-directory` is optional, read-only, and only controls the missing-target snapshot. Output must be a separate analysis directory on a mounted external volume, never an artifact directory or these sources.

`--passes 1..8` bounds analysis expansion. Every pass starts with all selected unwind entries plus provisional case/default roots from earlier passes. Provisional roots support discovery of nested dispatches; they never become published targets merely because they were decoded. Only the final pass's fully validated owners are published, without silently retaining earlier rejected owners.

## Acceptance

The scanner decodes from instruction anchors and follows direct control-flow edges. The recognizer accepts only the explicit RIP-relative-base/signed32-offset/ADD/indirect-JMP sequence with an unsigned JA guard and a small fixed bound (at most 4096 slots). Scheduling independent instructions between LEA and MOVSXD is permitted only if they preserve the participating registers and control flow. Guard/index copies require unchanged register equivalence and a dominating copy on every protected predecessor path. A 32-bit guard also needs an independently dominating zero/sign-extension proof for the full index; a 64-bit comparison of the exact full index already bounds the whole indexed value.

An 8/16-bit guard additionally requires dominating full-register MOVZX/copy
definitions that bound the entire indexed value to the compared width. Partial
destination writes, stale extensions, high-byte guards and source clobbers reject.
Zero/copy proof dependency PCs are protected against both direct and recognized
table-edge bypasses.

Every accepted table must match an immutable, uniquely file-backed span of the original ELF byte-for-byte. All case/default targets must stay within the owning nonoverlapping unwind range. Alternate predecessors into the protected guard/dispatch path are rejected, including accepted table edges. The validator then walks the owning function from its unwind entry with all recognized tables: every declared dispatch and case must be reached, with no decode failures, unresolved indirect jumps, overlapping instruction intervals, or external branches. Direct calls retain the return continuation; explicitly configured nonreturning calls terminate the path.

The current ELF helper supports little-endian x86-64 ELF program headers including SCE RELRO mappings and requires one executable file mapping starting at image offset0. It validates file/memory/load bounds, atlas ranges, and prepared-image identity. Non-load SCE metadata segments may legitimately have file bytes with memsz0; they do not become runtime mappings.

This is a deliberately conservative static catalog, not proof of whole-game reachability. Preloaded loop table bases, ABI-dependent callee-saved proofs, pointer tables, ambiguous narrow guards, unresolved functions, and otherwise valid external tail branches remain excluded. Those shapes require a distinct reviewed extension or separate evidence. There is no silent success fallback.

## Outputs and integration

- `all-targets.txt`: sorted unique canonical case/default addresses accepted in the final pass.
- `missing-targets.txt`: those addresses absent from the optional trace-directory snapshot; without a trace directory, all accepted entries are missing.
- `incomplete-owner-targets.txt` / `incomplete-owner-tables.json`: separate speculative candidates, with unresolved indirect PCs and external direct edges recorded in canonical addresses. Every published table, default, case and proof dependency must be reached/decoded inside the owner, without decoding failures or overlaps. Unknown exits may still bypass those dependencies through unseen paths; these are not complete-owner proofs.
- `prefetch-targets.txt` / `missing-prefetch-targets.txt`: compilation union of accepted targets and the explicitly incomplete candidates.
- `entries.json`: each target's owning bounds, source dispatch/table, table slots, and default role.
- `accepted-tables.json`: guard/index proof windows, bounded offsets, source/table hashes, and original ELF offsets.
- `rejected-owners.json`: incomplete or conflicting CFGs, retained for review rather than prefetch.
- `provenance.json`: input/source/output SHA256 values, pass counts, configured nonreturning addresses, and explicit limitations. Per-pass raw boundaries, candidates, rejections, CFG results, and provisional roots remain available.

Invoke this independently for each prepared module after producing the validated unwind atlas. Odin currently selects targets from these private analyses explicitly; `advance.py` does not run this scanner automatically. Compiling a candidate changes neither exact-PC dispatch nor runtime checkpoint acceptance. A new unsupported lift/codegen operation still stops explicitly with its named log; completed peers remain cached. Runtime coverage remains an independent check. Keep the catalog output and original decoder inputs for reproducibility; no input bytes need to enter the public repository. The tools use the already provisioned XED dependency and copy no vendor source.

## Owned tests

```sh
python3 test.py \
  --output /Volumes/office/Build/OdinSphere/switch-fixtures \
  --xed-root /path/to/remill-install \
  --llvm-bin /path/to/llvm/bin
```

The test assembles authored x86-64 routines, requires the object to contain no unresolved relocations, builds its own small ELF carrier and atlas, then runs the actual CLI. It never executes the generated x86 code.

The 109-function matrix accepts 52 owners / 53 tables, covering ordinary,
copied-index, interposed-MOV, full 64-bit and zero-extended narrow guards, and
nested tables. Fifty-seven owners reject, including stale/partial extensions,
source/index/base clobbers, direct and table-edge proof bypasses, ambiguous upper
bits, unresolved CFG, overlapping case entry, excessive bounds and external tails.
Four incomplete owners separately exercise speculative candidate labeling and
exact unresolved/external site records without entering the accepted set. Additional owned failures cover prepared-image mismatch, overlapping atlas owners, and malformed ELF bounds. Nested discovery requires more than one pass. `results.json` records 118 checks with zero failures and artifact hashes.


## Separate conditional compile-only tier

`conditional.py --strict-output EXISTING_STRICT_SCAN --xed-root EXISTING_HOST_XED_PREFIX --output NEW_SEPARATE_EXTERNAL_DIRECTORY` performs an explicitly isolated optional pass. Its output folder cannot overlap the strict scan. The strict scan is hashed before and after and is never changed. No conditional roots or edges feed back into strict discovery, accepted tables, incomplete-owner outputs, or completion counts.

Only a low 8/16/32-bit guard of the indexed register's own family can receive the explicit unproven assumption `full_index >> guard_width == 0`; differing-register equivalence is never waived. Postguard preservation, guard control-flow scheduling, immutable table bytes, bounded executable owner ranges, instruction boundaries, and decoded case/default reachability remain mandatory. A missing strict MOVZX/upper-zero proof stays a strict rejection. This pass changes no game input, instruction, clamp, callback, or runtime dispatch rule.

Outputs are `conditional-tables.json`, `conditional-entries.json`, `conditional-targets.txt`, and `missing-conditional-targets.txt`. Tables record the assumption's register, width, relative/canonical comparison PC, and `proof_status=unproven`. Every `owner_cfg.complete` is false, with unresolved/external PCs retained. Even if conditional closure discovers an otherwise-strict table, that later table is not published as strict or as an assumption-free conditional row. `prefetch-with-conditional-targets.txt` explicitly unions these candidates with the unchanged strict scan's existing prefetch file; it is a compile-only artifact. `missing-prefetch-with-conditional-targets.txt` is the corresponding cache snapshot.

Run `conditional-test.py --output NEW_EXTERNAL_DIRECTORY --xed-root EXISTING_HOST_XED_PREFIX --llvm-bin EXISTING_LLVM_BIN`. Its fifteen owned functions include strict and conditional tables, invalid copies/clobbers/high-byte guards, unresolved/external exits, and a conditionally reachable otherwise-strict secondary table. The strict folder must remain byte-for-byte unchanged, assumptions must be present on every exported row, and the secondary table must not enter strict or conditional table outputs. Optional `--baseline-source PRE_EXTENSION_SOURCE_DIRECTORY` additionally compares strict table/target outputs with the prior implementation.

The required assumption is not established by observing a case target. For example, a full index0x100 has low byte0 and passes a low-byte comparison against2 while indexing beyond a three-slot table. These entries are static speculative prefetch only and must never be counted as proved bounds or complete owning CFGs. Compilation failures remain explicit, and successful peers retain their per-target provenance.

## Scheduled comparisons and derived termination imports

The guard recognizer allows up to eight flag-preserving MOV/MOVZX/MOVSX/MOVSXD,
LEA or NOP instructions between CMP and unsigned JA. Every intervening path must
be sequential and leave the indexed value unchanged. Flag/index clobbers, an
alternate entry, or an accepted table edge into that dependency chain reject.
The complete regression matrix currently passes 118 checks across 109 functions.

`--noreturn-provenance MAP.json` binds independently derived declarations to
matching module/ELF/image records and captures the map hash in provenance. The
bootstrap's [OpenCrown import derivation](https://github.com/runty/OpenCrown-PC/blob/68cb2a7b9bfc042593fdd26d7a045697cc29e65a/ios/game/noreturn/README.md) validates both stack-failure
thunks from original relocation/GOT/PLT bytes, without game-address literals.
Only the declared call fallthrough terminates; this is not `nounwind` and does
not change runtime code or dispatch.

## Table-base copies and target lifetime

Bounded full 64-bit MOV chains may carry the RIP-relative table base to the
register consumed by ADD. Partial copies reject. The index must remain live
until the indexed MOVSXD, the base until ADD, and the computed target through
JMP. Up to eight independent whitelisted instructions may follow ADD; dead
base/index cleanup is allowed, while target-alias writes, calls, branches and
alternate entries reject. Guard flags retain their stricter lifetime through
JA. The owned matrix includes clobber, partial-copy, instruction-count and
direct/table-edge bypass counterexamples.

A separately tested extension permits exactly one full32-bit MOV from the exact
compared32-bit register to the index between CMP and JA. The source must remain
unchanged throughout the bounded schedule; the copy zero-extends the full index.
Partial/64-bit copies, multiple definitions, mismatched source widths/registers,
flag clobbers and alternate predecessors reject.
`late-index-test.py` takes the same output/XED/LLVM options as `test.py`; its
23 owned functions pass 49 checks. The existing 118-check matrix and 42-check
conditional isolation/baseline comparison also pass with this extension.
