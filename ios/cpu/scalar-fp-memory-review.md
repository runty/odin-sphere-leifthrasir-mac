# Isolated scalar floating-point memory-width review

Reuses OpenCrown `697047062d02dbf27bc6324689ae4d7e76be18c8`'s exact
`remill-scalar-fp-memory.patch` and test matrix. The fixture is adapted to
Odin's actual checked runtime, mapped regions and longjmp recovery. Shared
backend sources, bitcode and lifter remain unchanged. This branch starts at
Odin's qualified twenty-five-patch instruction candidate; neither that backend
nor this additional patch is adopted by the active twenty-two-patch game.

```sh
python3 ios/cpu/test-scalar-fp-memory.py \
  --backends /Volumes/office/Build/OdinSphere/cpu-backends-instruction-candidate \
  --output /Volumes/office/Build/OdinSphere/scalar-fp-width-review-25
```

Use a new output directory for the initial run; `--reuse-backend` verifies and
reuses the exact private overlay. The review compiles explicitly bound private
baseline/fixed lifters, including all nine scalar memory selectors. It does
not alter or retag existing game IR. A future game candidate needs a separately
audited backend and a fresh, actually re-lifted corpus.

The actual native x86 oracle executes 24 forms with 256 vectors each, across
rounding modes, aligned/unaligned operands, finite and nonfinite values and
source/destination aliases. Twenty real native protected-page operand-end
probes succeed; twenty one-byte-overrun probes fault in controlled children.

Both baseline and fixed Odin runs evaluate 67,904 conditions per mode (native
arm64 and ASan/UBSan), including 160 boundary/recovery probes. All 18 oversized
boundary negative controls disappear in the fixed version. Typed read counts
match the intended widths, and all 442,368 captured result/flag/inactive-lane
bytes are identical between baseline and fixed in both modes. Shared runtime
is `3217a61abd66995cf9196041d69dabf900b6904988a29a99c9ac6b37727d9e83`.

**The strict native oracle fails with 160 existing NaN-selection differences
in every baseline/fixed configuration.** Full failure output and nonzero exit
are retained. `width-delta-qualification.json` separately establishes the
width/bounds improvement and unchanged result bytes; it is not an overall
CPU-fidelity pass. The fixture does not compare floating-point exception flags
or unmasked exception delivery. These remain separate architectural work.

Private evidence is in `scalar-fp-width-review-25/`. No game, performance,
audio quality, SDK execution or physical-device result is claimed. The active
native/iPad game and all saves are unchanged by this fixture.
