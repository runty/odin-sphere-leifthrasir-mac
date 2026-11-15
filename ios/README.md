# Odin Sphere on iPhone and iPad

This is the first CPU experiment, not a playable mobile game. Rosetta is not
used on mobile. Three routines from the player's matching CUSA05083 US base
1.00 files are lifted ahead of time with Remill and compiled into signed arm64
code. No runtime executable-code generation or debugger is needed by the app.
Generated code, game images, logs and provisioning remain in the external
private build tree; only the builder, adapters and fixture descriptions belong
in Git. Never distribute the game-derived fixture library with the source.

## Build

The reference backend is Remill `66c488c9911fd9b368e4d0c9647d74239f8bba8b`
with LLVM 23.1.5 and the twenty ordered patches listed in
`audit-backend.py`. Those compatibility/semantics
patches include nineteen from the OpenCrown port plus Odin’s qualified
CMPXCHG8B register-write correction, with reproducible source inputs. Existing matching OpenCrown backend
caches may be read without modifying them. To build an isolated backend:

```sh
bash ios/build-backend.sh "$WORK/cpu-backends" /opt/homebrew/opt/llvm
python3 ios/cpu/build.py --work "$WORK/ios-cpu/fixtures" \
    --app0 "$APP0" --image "$IMAGE_DIR" --remill "$WORK/cpu-backends"
bash ios/build-probe.sh "$WORK/ios-probe" "$DEVELOPMENT_TEAM" "$WORK/ios-cpu/fixtures"
```

The builder verifies the three source-module hashes, module layout, strlen
export and selected routine hashes. It executes the original routines from a
private image in an x86-64 Mac oracle, then compares their translated execution
on arm64 Mac and builds static iPhoneOS and Simulator libraries. Guest addresses
are explicit virtual addresses; they do not depend on the host's malloc range.

Coverage is a read-only eboot getter with both branches, libc strlen across 16
alignments and 12 lengths, and a Fios2 setter with six values. Checks compare
returns, relevant arithmetic flags, the setter's memory write, unchanged input
memory, preserved registers and the return stack/PC. Two workers also test
invalid/overflowing pointers, mapping overlap and unresolved trace rejection.
This gives 414 checks. It does not establish whole-game instruction coverage,
module TLS, imports/callbacks/fibers, acceptable full-game CPU speed or graphics.

## Device app

The signed `com.wadscorp.odinsphere.probe` app displays and writes
`Documents/diagnostics.json` on startup. It reports the actual CPU results,
corpus hash, device/OS/page size, Metal capability query and persistent launch
count. The share button is optional; devicectl can retrieve the same file.
The UI and report explicitly say `game_booted=false`.

```sh
xcrun devicectl device install app --device "$DEVICE" "$APP"
xcrun devicectl device process launch --device "$DEVICE" com.wadscorp.odinsphere.probe
xcrun devicectl device copy from --device "$DEVICE" --domain-type appDataContainer \
    --domain-identifier com.wadscorp.odinsphere.probe --source Documents/diagnostics.json \
    --destination "$PRIVATE_DIAGNOSTICS"
```

The reference devices are iPhone 17 Pro Max (`00008150-000E74E11EF0401C`) and
iPad mini A20 Pro (`00008160-000450890E800036`). Respect a locked-device launch
failure: retain the signed app and continue host/runtime work without requesting
an unlock during the player's unattended run. Simulator results are separate
from physical-device results.

## First results — 2026-11-13

The arm64 Mac passes all 414 checks, and the iPhoneOS/Simulator archives and
signed Release app build successfully. Initial iPhone hardware execution,
launched without LLDB, also passes 414 checks on iPhone18,2 / iOS 27.2.1
24B156 / A19 Pro / 16 KiB pages. Its observed corpus is
`14c2882bdef30a22965c8cadd5cf62cf27855b63ef909b59befd24fd003d7606`.
The first app build identified the worktree as dirty. A subsequent signed build
at `6875944` installs on both devices. A background launch on the iPhone, without
LLDB, passes the same 414 checks and reports that exact clean revision; the
persistent launch count advances from 1 to 2 across replacement. Foreground
launch was denied after the phone locked, so the successful background run is
recorded separately from Home Screen/share-sheet acceptance.

The iPad install succeeds, but launch is rejected with the explicit `Locked`
error, so no iPad routine-execution result is claimed yet. The user's hands-on
30-minute Mac gameplay session remains deferred. Next: establish a committed
device baseline, then extend the checked runtime to the actual linked-module
initializers and guest startup before selecting the final mobile CPU backend.


## Linked-module startup experiment

The probe can now include [checked module/eboot startup](game/README.md) as an
optional fourth build argument. Both actual module initializers return zero on
arm64 Mac and iPhone, followed by the real eboot entry. A 244-trace development
corpus reaches the same next missing block after 397 dispatches on both hosts,
without a memory fault. GPU-visible host identity addresses fit below 1 TiB on
the reference phone; Metal 4 family support is reported. This is evidence for
the runtime approach, not whole-game instruction or renderer coverage.

The comparison library and startup library share one checked memory/dispatch
core. Their lifted symbol namespaces are separate. The build checks matching
runtime hashes; startup reports identify the exact corpus. Generated code and
private images stay outside the source tree. Continue in an isolated backend
cache so another project's rebuild cannot change semantics under discovery.


The isolated backend's 371-trace corpus subsequently reaches 513 dispatches
with both initializers complete and no fault. Signed clean `d5a25db` installs on
both devices, but both deny even background launch as `Locked`; the older JSON
retrieved from the iPhone is explicitly marked stale. This does not replace the
earlier actual hardware result. Its startup corpus is
`9b905bfe1f6b3d756c955e18e129c22a732dec689f9261ae7f463e0779e1bbb4`.


The [graphics adapter experiment](graphics/README.md) now builds the full arm64
iPhoneOS driver and a signed framework probe. Its native arm64 Mac submit/fence/
readback check passes; no physical-device graphics result is claimed. Simulator
can test CPU startup with `OSL_PROBE_SDK=iphonesimulator` and a startup library
built with `ios/game/build-ios.py --sdk iphonesimulator`. Its guest addresses
are explicitly virtual because ordinary Simulator malloc can exceed the native
GPU frontend's range. Metal 4 encoder APIs are absent from the Simulator SDK,
so the actual graphics driver does not have a Simulator result.


The isolated backend now includes immediate-form `VPERMILPS` selectors. The
four register/memory and XMM/YMM forms reuse bitwise dword shuffles, preserve
flags and clear upper YMM bits for VEX XMM destinations. Run
`python3 ios/cpu/test-vpermilps.py --root "$WORK/ios-vpermilps-tests" --remill "$WORK/cpu-backends"`
for all 256 immediates: 8,192 lane values match bounded native x86 execution,
including NaN/signed-zero bit patterns and upper-lane canaries. Unsupported
synchronous instruction fallbacks still stop at the exact guest PC; they never
silently succeed. Changing semantics requires a new startup trace directory;
`advance.py --seed-from` re-lifts the old executed PC list without copying IR.

### Packed reciprocal instructions

The next executed backend gap, `VRCPPS` at `0x106aa6df`, now has a qualified
semantics patch, `remill-rcpps.patch`. It covers legacy XMM register/memory and
AVX XMM/YMM register/memory forms. Integer arithmetic produces a deterministic
reciprocal approximation, avoiding host floating-point modes and exceptions;
denormals, NaNs, signed zero/infinity, tiny-result flushing and upper lanes are
handled explicitly. The contract follows the [Intel instruction reference](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-2b-manual.pdf).
It does **not** reproduce a specific PS4/Intel approximation table bit for bit.

Use a NEW isolated backend/cache; existing semantics and game IR must stay intact:

```sh
bash ios/build-backend.sh "$WORK/cpu-backends-rcpps"
python3 ios/cpu/test-rcpps.py --root "$WORK/ios-rcpps-tests" \
  --remill "$WORK/cpu-backends-rcpps"
```

The reference qualification passes all six forms, 8,192 input bit patterns and
32 MXCSR control/status combinations, checking 1,572,864 output lanes separately
in native x86 under Rosetta and arm64 AOT. Tests enforce the numerical error
bound (including its tiny-result boundary), special values, unchanged flags and
correct upper lanes. This is ISA-contract qualification, not exact cross-CPU
reciprocal bits, a full-game boot, or a mobile frame. A new game corpus must
re-lift every seeded PC using the changed semantics before further discovery.

### Immediate packed-single blends

The executed `VBLENDPS` gap at `0x106aa713` is implemented by
`remill-blendps.patch`: six legacy/AVX, XMM/YMM, register/memory forms select
32-bit lanes without floating-point arithmetic. `ios/cpu/test-blendps.py`
checks every immediate, six destructive operand-alias cases (including the
executed destination/second-source overlap), NaN/signed-zero bits, preserved
sources/flags/MXCSR and legacy/VEX upper lanes. All 24,576 output lanes match native x86 under Rosetta exactly. Reciprocal, VPERMILPS and the 414
real-routine checks also pass with the new backend.

Use a NEW backend and game corpus after this semantics change. The builder's
`audit-backend.py` compares the complete ordered patch set through a temporary
Git index rather than trying to reverse each patch in isolation; stacked
end-of-file patches depend on one another. It rejects unexpected tracked edits
and untracked files without changing the working tree. Positive/negative audit
checks pass against a separate checkout. Backend semantics SHA:
`0ba9d2c0d84b78f7bf119a1e4ab1ea2720673541176044d52766d671f64e4efd`.
Unit qualification does not establish a new game startup or mobile frame.

### Lane-local packed-single shuffles

The next executed gap, `VSHUFPS` at `0x106aac36`, is implemented by
`remill-shufps.patch`. Its immediate selects lanes independently in each
128-bit half of a YMM register; the existing 128-bit legacy implementation is
retained. Six legacy/VEX register/memory forms plus six destructive operand
alias cases pass all 256 immediates, matching 24,576 output lanes exactly with
native x86 under Rosetta. Flags, MXCSR, NaN bits, source preservation and
legacy/VEX upper-lane rules are checked. Blend, reciprocal and VPERMILPS
regressions pass with the same isolated backend. Complete-patch audit passes
and rejects both unexpected tracked edits and untracked sources.

New backend semantics SHA:
`8bcacf9eed0816c35af34788513b6b4ecb11ddd565b6364a4d894cf1bd710331`.
Use a NEW backend/corpus and re-lift PC seeds; do not reuse old IR across this
change. Unit checks do not establish a game frame or mobile execution.

### Immediate packed-double permutation

The next executed gap, `VPERMILPD` at `0x106aad24`, has four immediate
register/memory XMM/YMM forms in `remill-vpermilpd.patch`. Each output selects
one of the two raw 64-bit values in its own 128-bit lane, using a separate
immediate bit. The helper does no FP arithmetic and VEX128 clears upper YMM
bits. [Intel instruction reference](https://cdrdv2-public.intel.com/671110/325383-sdm-vol-2abcd.pdf).

`ios/cpu/test-vpermilpd.py` checks all 256 control bytes, four encodings plus
two destructive register-source aliases, 6,144 exact 64-bit output lanes,
quiet/signaling NaN bits, signed zeros, flags/MXCSR/source preservation and
upper lanes. Native x86 under Rosetta and arm64 AOT match exactly. Shuffle
regression (24,576 lanes), all 414 real-routine checks and both mobile fixture
archives pass. Whole-patch audit rejects unexpected tracked/untracked sources.
This covers the executed immediate family; register-controlled VPERMILPD forms
remain explicit backend gaps. New semantics SHA:
`34c958493fd1e1ae4a4043d033efb08e6e33d6ad034d21386711a3ad903c9c0a`.
Re-lift all PC seeds in a NEW corpus/backend; never reuse old IR across this
change. These unit tests do not establish mobile game execution or frames.

### Packed reciprocal square roots

`remill-rsqrtps.patch` implements the executed VRSQRTPS gap at `0x106aa80c`,
including all six legacy/VEX XMM/YMM register/memory forms. Normal estimates
use integer binary search and midpoint comparisons; two 64-bit limbs avoid
host FP rounding/exceptions and also compile for Remill's 32-bit semantics.
Denormals become signed zero; negative normals/infinity produce indefinite,
positive infinity produces zero, and NaNs preserve sign/payload and quiet.
[Intel instruction reference](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-2b-manual.pdf).

`ios/cpu/test-rsqrtps.py` checks 8,192 input patterns, every exponent/sign,
32 MXCSR control/status combinations and 1,572,864 output lanes separately on
native x86 under Rosetta and arm64 AOT. Both meet ISA accuracy/special/upper-lane
and flag contracts. This is not bit-exact equivalence to any processor's
approximation table. Reciprocal regression and real-routine CPU 414 pass; both
mobile fixture archives build. New semantics SHA:
`6220000320437ddeaad5b1df03638988ce3c83ecee1d5dc742568a9eda766bb6`.
Whole ordered patch-set audit passes and rejects extra tracked/untracked source.

NEW full-window corpus `d32f7665` re-lifts all 1,039 seed PCs and advances through
VRSQRTPS to explicit VBLENDPD `0x106aa939`. Native Mac GPU qualification passes
60 bridge checks, four joined guest workers, three buffers, 3,164 dispatches /
32,086 aggregate checkpoints and no fault/frame. Headless fixed/identity/corrupt
input checks pass. This is host CPU/renderer evidence, not new mobile execution.

### Packed-double blend and shuffle

`remill-double-select.patch` implements the executed VBLENDPD stop at
`0x106aa939`: six legacy/VEX XMM/YMM register/memory blend forms copy raw
64-bit lanes, preserve FP state and honor legacy versus VEX upper-lane rules.
It also completes the immediate SHUFPD family using its existing lane-local
helper (legacy memory plus four VEX forms). Shuffle coverage is a related
family extension; it was not the observed executed stop.

`ios/cpu/test-double-select.py --operation blend|shuffle` independently checks
six encodings plus six destructive aliases and all 256 immediates. Native
Rosetta x86 and arm64 AOT match 12,288 exact 64-bit output lanes per operation,
including NaN payloads, signed zeros, upper lanes, input preservation and
unchanged flags/MXCSR. Reciprocal-square-root regression passes 1,572,864 lanes
against ISA contracts; all 414 real-routine checks and both mobile SDK fixture
archives pass. The full eleven-patch audit passes and rejects unexpected tracked
and untracked source without changing the candidate trees. New semantics SHA:
`4b099e2e4dfec1bf0bcc19a5e3906ebb87cfc30a91d0d51c17039f578ad15ff7`.
Use a new backend/corpus and re-lift PC seeds rather than reusing earlier IR.
These are host instruction tests, not new mobile game frames.

The native VideoOut/event adapter is shared in `graphics/guest-gpu.h`; it retains
checked marshalling and ordered worker/GPU/mapping cleanup. GPU discovery hashes
that header as part of its frozen inputs. UIKit guest integration remains a
separate step from the existing physical renderer-only check.

### Startup SIMD reused from OpenCrown — 2026-11-15

The native 9,521-entry cache stopped on `VEXTRACTPS` at `0x106b796c`.
OpenCrown `9ca8945c` supplies lane insert/extract, variable packed blends,
128-bit extraction/permutation and safe dword shifts. The five patches retain
OpenCrown's added implementations; only SSE append context differs. The full
backend audit expects nineteen ordered patches. Existing backend/cache trees
remain frozen; use a new directory and re-lift guest IR after any semantics
change.

```sh
python3 ios/cpu/check-startup-simd.py --output "$WORK/startup-simd-check" \
  --backends "$WORK/cpu-backends-startup-simd"
```

The generic checker needs no game bytes. It compares 238 routines over 256
vectors against native x86 under Rosetta using Odin's checked-memory runtime.
It covers operand aliases, masked immediates, high shift counts, VEX upper
lanes, preserved source registers, full GPR outputs, unaligned stores and
memory guards. Unsupported instructions use the actual guest recovery path
and fail the test. An optional `--observed-matrix-span PRIVATE_FILE` adds the
84-byte, twelve-store sequence observed in Odin; that private input is never
included in the repository.

The audited built backend passes all 1,590,784 dword comparisons with that
optional case (239 routines), with source/lifter/semantics unchanged during
execution. All 414 real-module comparisons and both mobile SDK archives also
pass. Evidence is `extract-reuse/startup-final/` and
`cpu-backends-startup-simd/` at the external build root. This establishes CPU
semantics for the covered cases. Whole-game execution and physical-device
results with this backend remain separate checks.

### Checked mapping lookup cache — 2026-11-15

Odin adapts OpenCrown `9ca8945c`'s eight-descriptor cache and strong registry
identity, adding an atomic generation for Odin's removable mappings. Successful
map/unmap operations invalidate every thread's copied descriptors and serialize
with CPU/host transfers. A quiescent registry replacement also invalidates the
cache. Entries retain logical bounds and write permissions; zero-length spans
use registration-order traversal because adjacent mappings can both match an
empty boundary. The cache never authorizes the live external direct-memory
adapter, which rechecks aliases, unmaps and permission changes on every access.
The global recursive transfer/atomic/host-service lock remains unchanged.
Striped synchronization from OpenCrown is not adopted here: Odin's external
physical aliases need separate proof. A public `guestPointer` is still borrowed;
hold `GuestMemoryScope` across any access that can race mutation or reclamation.

```sh
python3 ios/cpu/test-shared-memory.py --output "$WORK/cache-normal" \
  --remill "$WORK/cpu-backends-startup-simd"
python3 ios/cpu/test-shared-memory.py --output "$WORK/cache-address" \
  --remill "$WORK/cpu-backends-startup-simd" --sanitizer address
python3 ios/cpu/test-shared-memory.py --output "$WORK/cache-thread" \
  --remill "$WORK/cpu-backends-startup-simd" --sanitizer thread
```

The fixture passes 1,564 checks on native arm64 and Rosetta, plus arm64
ASan/UBSan and TSan with fatal diagnostics. It covers descriptor churn,
unmap/free/remap after warming a cache, changed bounds and read/write rights,
shared-backing aliases with distinct permissions, 128 quiescent registry
replacements, uncached external lookups, exact 20,000 concurrent CAS/host
updates, 2,000 concurrent map cycles, and fault/atomic/cancellation cleanup.
An isolated negative control exposed empty-boundary registration-order changes;
the final implementation and fixture preserve the original behavior. All 414
original-module comparisons and both mobile fixture SDK archives also pass.

A three-repeat alternating synthetic comparison uses 512 mappings: one million
reads cycling eight tail mappings take median 187.09→13.63 ms; eight threads
performing eight million total reads take 4,406.33→348.11 ms wall time. Churning
300,000 reads through all 512 mappings takes 34.47→38.63 ms (about 12% slower).
Checksums match in every run. These selected lookup workloads establish a
tradeoff, not whole-game FPS. Exact inputs/results, sanitizer logs and CPU
manifest are under `checked-memory-scaling/` at the external Odin build root.
The Remill backend and guest State ABI are unchanged; rebuild host runtime
objects/checkpoints normally rather than editing provenance hashes.

### Atomic widths and floating-point control bridges — 2026-11-15

The expanded original-image corpus exposed missing Remill runtime symbols at
link time. Odin adapts OpenCrown `4d90913`'s CAS family and floating-point control
mapping, keeping Odin's recursive lock, checked guest spans and fault recovery.
CAS operands of 8/16/32/64/128 bits update the expected value on success and
failure; the 128-bit desired operand follows Remill's by-reference ABI. A fault
unwinds an enclosing guest atomic scope through the existing recovery path.
This does not qualify x86 alignment-fault behavior for every wide instruction.

Exception test/clear/raise translate Remill's x87 bit positions into Darwin
`FE_*` values. Denormal and stack-fault flags, which lack standard host masks,
are retained per thread. Rounding getters/setters translate all four modes;
invalid intrinsic arguments fail explicitly without throwing through LLVM
frames. This supplies host control/flag bridges, not FP80 arithmetic support.
Extended-precision arithmetic remains an explicit unsupported demand.

```sh
python3 ios/cpu/test-cas-widths.py --output "$WORK/cas-check" --remill "$BACKEND"
python3 ios/cpu/test-fpu-exceptions.py --output "$WORK/fpu-check" --remill "$BACKEND"
```

Each script also accepts `--sanitizer address` (ASan/UBSan) or `--sanitizer thread`
for arm64. Normal runs test native arm64 and x86-64 under Rosetta. All five CAS
widths pass bounds, protected-memory recovery, operand/canary and concurrency
checks, with 16,160 successful concurrent increments per architecture. The FP
fixture passes 266,293 checks per architecture: exception masks/selective clear,
real native arithmetic flags, positive/negative half-ULP rounding, preserved flags
and eight-thread isolation. The runtime is compiled with production optimization
and default FP policy; only the independent arithmetic fixture requests strict
fenv access. Fatal sanitizer diagnostics find no errors. The existing 1,564
shared-memory checks and all 414 original-module comparisons also pass; static
fixture libraries build for both mobile SDKs. These are host/oracle and compile
results, not execution of the new bridges on a physical iPad or gameplay proof.
Evidence: `runtime-bridges/` at the private external build root. Runtime source
hash changes normally invalidate host objects; the backend and guest State/
memory header ABI are unchanged, so matching raw translated IR remains valid.

### Independent atomic instruction oracle — 2026-11-15

`check-cas-instructions.py` compares actual lifted locked/unlocked CMPXCHG
byte/word/dword/qword and CMPXCHG8B/16B with native x86 output: 32 routines,
256 vectors, full result bytes, arithmetic flags, implicit registers, canaries,
RBX preservation and return-stack recovery. Nonwide cases include unaligned
spans; CMPXCHG16B is aligned. This does not qualify alignment faults or game reachability.

A separate x86-64 AMD EPYC 7763 runner avoids depending entirely on Rosetta.
[Oracle run 43719095874](https://github.com/runty/odin-sphere-leifthrasir-pc/actions/runs/43719095874)
uses exact checker source `9683e937`, whose hash and result hash are checked
before a hardware reference is accepted. The artifact contains only generated
test output, never game bytes. The workflow uses pinned checkout/artifact actions,
read-only repository permission and a five-minute ceiling. Download its artifact
before it expires; rerun the workflow at the same checker revision if necessary.

```sh
python3 ios/cpu/check-cas-instructions.py --native-only --output "$NATIVE_OUTPUT"
# Run the native-only command on x86 hardware, then transfer its verified output:
python3 ios/cpu/check-cas-instructions.py --output "$WORK/cas-hardware"   --backends "$WORK/cpu-backends-cmpxchg8b" --native-reference "$NATIVE_OUTPUT"
```

The previous backend fails 3,888 byte/condition comparisons with deliberately
nonzero upper EDX state. `remill-cmpxchg8b.patch` writes zero-extended EDX:EAX
only after a failed CMPXCHG8B, preserving full RDX:RAX on success. The independent
hardware reference passes all 1,056,768 checks after this correction. This is an
actual backend defect found by qualification, not evidence of a reached game fault.

Rosetta separately disagrees with that hardware runner on failed scalar-CAS
non-ZF flags and successful dword-CAS upper RAX bits. Standalone CMP controls
agree with lifted flags, while Rosetta CMPXCHG disagrees in 3,072 observations.
No backend adjustment is made to imitate those Rosetta-only differences. A
Rosetta-only checker invocation retains its failing output for diagnosis; it is
not the acceptance oracle for these instructions. All flags/data are compared
against the source/hash-matched hardware output for final qualification.

The twenty-patch backend is isolated at `cpu-backends-cmpxchg8b`, leaving the
nineteen-patch backend and its raw IR/cache unchanged. Complete source audit,
238-routine / 1,584,128-dword SIMD regression and 414 original-module comparisons
pass; both mobile CPU fixture archives compile. Whole-game IR must be freshly
lifted with the new backend before replay or device installation. Private evidence
is `runtime-bridges/{cas-hardware-fixed,cas-hardware-baseline-rdx,cpu-cmpxchg8b,
startup-simd-cmpxchg8b}/`, plus both hardware-oracle artifacts and audit JSON.

## Experimental registered-memory lock stripes — 2026-11-15

The isolated `014f8cc` candidate adapts OpenCrown's 64 ordered, 256 KiB lock stripes to Odin's stricter
memory ownership. Only a registered descriptor already in the calling thread's
cache can use selected host-address stripes. It checks the mapping generation
again **after** acquiring those stripes, before dereferencing storage. Removal
invalidates the generation under every stripe before backing is destroyed; a
stale descriptor falls back to a fresh lookup. Integer address bits alone are computed before the
lifetime check. The memory/State ABI and current twenty-patch backend are unchanged.

All cache misses, external-pointer accesses, host `GuestMemoryScope` leases,
mapping mutations, guest atomic regions and compare-exchanges continue to take
**every stripe** in ascending order. In particular, different virtual mappings of
one physical pool page are never assigned a guessed independent stripe. External
pointers/permissions remain uncached. Nested host/atomic scopes retain per-thread
recursive ownership, and fault recovery releases native scopes before longjmp.
This deliberately conservative external path differs from OpenCrown's simpler
registered native-address memory model.

The new stress fixture performs 5,000 backing replacement/free cycles against
320,000 racing reads, 160,000 cross-band transfers, and a real shared-file page
mapped at two distinct host virtual addresses. It verifies the external access
excludes a warmed registered alias. It passes 5,024 checks on arm64/Rosetta and
under arm64 ASan/UBSan/TSan. Existing 1,564 shared-memory checks, 20,000 atomic
updates and 2,000 map cycles pass normally on both architectures and under both
sanitizers. All five CAS widths pass 40 cases / 16,160 concurrent increments per
normal host and sanitizer run. Bulk-libc's 686,279 checks pass in all four modes,
with both SDK fixture archives compiled. CPU 414, both SDK CPU/runtime archives,
4,371 complete runner service checks and four injected cleanup cases also pass.

These are memory ownership, ABI and concurrency qualifications. The actual
same-PC-set game comparison does not pass the delivery gate: it ends at the
300.004-second deadline with 41 joined workers, without reaching baseline
`0x105d0b45` (164.248 seconds / 43 joins). Its fresh frame-3,200 readback remains
black; cleanup, unchanged raw/backend/renderer and save hashes pass. Different
scheduling and frame-indexed input timing remain possible explanations; the
precise cause is unproven. The late process sample is from shutdown and is not
a valid startup hot-path profile. No performance benefit is claimed.

The default runtime is restored to the prior recursive global lock (`3217a61a`)
while original-code discovery continues. The stripe candidate, tests, binary,
checkpoint and reports remain preserved in the isolated worktree and
`memory-stripes/`; the iPad was never updated with it. A future revision needs
a new real-game comparison and input/readiness evidence before adoption.

## Confirmed packed SIMD reuse — 2026-11-15

The default backend audit/build now uses twenty-two patches, adding OpenCrown's
confirmed AVX integer selectors and packed floating-point value helpers. The
old twenty-patch checkpoint uses `audit-backend.py --legacy-startup`; the isolated
twenty-one-patch integer checkpoint uses `--legacy-packed-integer`. Neither flag
changes those preserved backends. Fresh game LLVM is mandatory after a semantics
change. `cpu/test-packed-integer.py` retains the integer-only matrix;
`cpu/test-packed-simd.py` qualifies all fifteen added selectors with actual Odin
checked-memory execution and a native x86/Rosetta reference. The full matrix
passes 10,616,320 word checks and 408,320 return/read-only input checks. Float
sticky MXCSR status and #XM delivery are explicitly excluded; integer MXCSR and
all tested control bits are retained. These are CPU tests, not game acceptance.

`cpu/build-packed-probe.py` packages only hash-qualified synthetic fixtures into
a separate UIKit app, with no game data, graphics or saves. It never installs
or launches; device selection remains explicit in the deployment command. The
app writes `Documents/packed-cpu.json` with dimensions, failures, backend/oracle
hashes, device model and OS. For example:

```sh
python3 ios/cpu/test-packed-simd.py --root /Volumes/office/Build/OdinSphere/packed-simd-tests --remill /Volumes/office/Build/OdinSphere/cpu-backends-packed-simd
python3 ios/cpu/build-packed-probe.py --root /Volumes/office/Build/OdinSphere/packed-simd-ipad-app-v2 --fixture /Volumes/office/Build/OdinSphere/packed-simd-tests --remill /Volumes/office/Build/OdinSphere/cpu-backends-packed-simd --team 53MX9ZJV4U
```

The current user authorizes only the iPad; never select the phone as a fallback.
Old zero scripted-fallback counters do not prove zero total guest pad reads;
see the [current startup status](game/README.md#current-engineering-status--2026-11-15).

The physical iPad mini A20 Pro (`iPad19,1`, iPadOS 27.2.1) passes that complete
synthetic matrix with zero failures. Host/backend/oracle hashes match, and the
owned app process is stopped and absent afterward. Evidence:
`packed-simd-ipad-device/verification.json`. No game data/profile is packaged or
modified by this app. The full game still needs a fresh corpus and replay.
