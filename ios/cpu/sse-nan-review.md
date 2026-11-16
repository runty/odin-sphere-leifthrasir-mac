# Isolated SSE/AVX NaN result review

This candidate reuses OpenCrown `a3e07c2fe69fcf2558a39748e3c2e693aebc6e4d`
with Odin's actual checked-memory runtime and longjmp recovery. The read-only
25-patch backend supplies build commands/libraries; a frozen private width
candidate supplies baseline semantics. The active game backend is unchanged.

On an x86-64 binary under Rosetta and native arm64 on this Mac, 96 forms each
exercise 2,048 operand vectors, including all 16×16 source-class pairs, four
rounding modes and two alignments. Baseline native and ASan/UBSan runs each
retain 14,656 NaN result-word mismatches; fixed runs each pass all 2,163,456
checks, 384 guarded boundaries and 14,155,776 exact result bytes. An independent
integer policy verifies 200,704 NaN inputs, 8,960 invalid non-NaN inputs and
14,848 min/max controls against the original native oracle.

This qualifies result bits, accesses, observed lanes and arithmetic flags. Guest
MXCSR status/traps, DAZ/FTZ, x87, horizontal operations, conversions, every alias
arrangement, actual game generation and performance remain unqualified. The
oracle is Rosetta on Apple silicon, not physical AMD/Intel hardware. No device,
game, shared backend or save is modified. Evidence: `sse-nan-review-odin/` and
`crown-nan-compatibility-audit/` under the private Odin build root.
