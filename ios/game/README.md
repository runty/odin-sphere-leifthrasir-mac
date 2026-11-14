# Checked module and eboot startup

This is a bounded startup experiment, not a playable mobile game. It runs the
actual libc and Fios2 initializers and then the real eboot entry through signed
arm64 AOT code. Unsupported instructions, missing blocks and host services stop
explicitly. No placeholder service reports full-game success.

## Private build

Keep the original package, prepared images, generated LLVM IR, objects and
reports on a mounted external build disk. Do not distribute game-derived code.
Use an isolated backend built by `ios/build-backend.sh`; sharing a mutable
backend cache with another project can invalidate trace provenance.

```sh
python3 ios/game/prepare.py --image "$IMAGE_DIR" --output "$WORK/ios-game"
python3 ios/game/advance.py --root "$WORK/ios-game" \
    --remill "$WORK/cpu-backends" --rounds 64 --identity
python3 ios/game/build-ios.py --root "$WORK/ios-game" --remill "$WORK/cpu-backends"
bash ios/build-probe.sh "$WORK/ios-probe" "$DEVELOPMENT_TEAM" \
    "$WORK/ios-cpu/fixtures" "$WORK/ios-game"
```

The CPU comparison archive must be rebuilt with the same checked-runtime
sources. The probe builder rejects different CPU/game runtime hashes. Copy
`image.bin` and `image-pointers.bin` into the app's `Documents/Guest/` using the
private device-data staging folder and CoreDevice, then retrieve
`Documents/diagnostics.json`. Device reports identify the source revision,
startup corpus, completed initializers, dispatch count and exact stop reason.
Do not treat a successful diagnostic app install as a playable game install.

`prepare.py` validates the boot container, relocations, segments and supported
profile. It binds 89 imports to the original modules and creates explicit traps
for the remaining host imports. `advance.py` runs a bounded arm64 Mac attempt,
lifts the next executed missing block and retains a hashed build-state ledger.
The ledger binds image, backend, IR, headers and runtime source. A changed
backend requires a new work directory. `--seed-from OLD/build-state.json`
reuses only an executed PC list and re-lifts every block with the new backend;
it never copies the old IR or bypasses provenance validation.

## Addressing and service boundary

The checked memory runtime validates each guest transfer. The default uses
explicit virtual addresses. `--identity` additionally rebases image pointers
and uses actual host addresses for image, stack and GPU-visible allocations,
checking that they fit the frontend's 40-bit address assumption. Host mappings
remain checked; executing from malloc is never required. ASLR changes native
addresses but must preserve the normalized guest checkpoint.

Host services currently cover process parameters, heap registration, selected
thread attributes, real pthread mutexes and bounded read/write flexible memory.
Atomic memory operations are serialized. Unknown services and unsupported
protection/fixed-address cases stop. This is not yet a worker/TLS/fiber runtime,
file/save layer or renderer. Extended x87 instructions remain an explicit
backend limitation; broad traces containing unused formatting paths can be
narrowed to an executed basic block, never replaced by a no-op.

## Established hardware checkpoint

On 2026-11-13 an iPhone18,2 / A19 Pro / iOS 27.2.1 background launch without
LLDB passes all 414 routine comparisons, returns zero from both module
initializers and reaches the eboot entry. The original 244-trace corpus reaches
397 dispatches and stops at logical PC 274339111 with no memory fault, matching
the arm64 Mac checkpoint. Image, stack and 256 KiB flexible allocation use
native addresses below 1 TiB. The device reports Metal 4 family support.

That result used the explicitly identified dirty development build. The signed
iPad probe installs but device lock rejects execution. Fresh isolated-backend
results and committed device baselines must be recorded separately. Neither
platform has visible mobile game frames or full startup yet.


For an established, idle host build, verify fixed/identity-address equivalence
and corrupt/truncated input rejection with copied private fixtures:

```sh
python3 ios/game/test-startup.py --root "$WORK/ios-game" --output "$NEW_CHECK_DIR"
```

Do not run this check while discovery is replacing the runner or its corpus.
The isolated backend also reruns the original 414 comparisons and a synthetic
four-thread 4,000-CAS stress/fault-recovery check through `ios/cpu/build.py`.


## Bounded discovery and protected code

Current discovery reaches 624 traces and 1,499 dispatches, stopping at the first
unimplemented `sceSysmoduleLoadModule` request (ID 7, libSceUlt). Both original
initializers still return zero and no memory fault is recorded. The module ID
is checked against the upstream shadPS4
[module table](https://github.com/shadps4-emu/shadPS4/blob/main/src/core/libraries/sysmodule/sysmodule_table.h).
The Mac reference's blanket module-load success is not copied into this partial
runtime. A provider needs to be implemented before advertising that module.

`guard_ir.py` inserts checkpoints at lifted function entries and CFG backedges,
so a register-only guest loop cannot bypass the dispatch limit. The shared core
limits checkpoints and checks a two-second guest deadline. `test-guard.py`
compiles a synthetic register loop and proves both exits; a forward-only branch
retains its result. The real 624-trace checkpoint is unchanged with guards
(7,419 checkpoints; approximately 3–5 ms of guest work on the reference Mac).
That startup time is not a gameplay/frame-rate measurement.

Guest read-only and executable segments reject writes, including atomic CAS.
This also stops self-modification rather than continuing with stale translated
instructions. Scalar/CAS protection, concurrency and atomic-fault cleanup are
covered by `runtime-test.cpp`. The routine comparison suite still passes 414
checks with the guarded/protected core. Rebuild both static libraries before
linking a device app whenever their shared core changes.


## Scoped service providers

The next host checkpoint reaches 628 registered traces / 1,505 dispatches with
no memory fault, after resident compatibility setup for Fiber (6), Ult (7) and
SystemGesture (206). Ult/Fiber are accepted only when the fixed linked image
imports none of their APIs and has no dynamic module/symbol resolver.
SystemGesture supports exactly the Mac port's five setup/teardown compatibility
calls; this profile has no gesture-event consumer. It does not implement touch
controls or recognize touch events.

SaveDataDialog is module **160 (0xa0)**; Trophy is **173 (0xad)**. The new dialog
provider supports initialization/status/termination and accurate error returns.
Opening a selection stops with `save dialog presenter unavailable`; it never
fabricates a selection or writes progress. The module mapping was corrected
during validation and the CLI regression check distinguishes those IDs.

```sh
"$WORK/ios-game/guest-startup" --service-check
```

This passes 21 checks for load/unload, invalid/unknown IDs, lifecycle/errors,
missing presenters and 16 KiB GPU allocation alignment/zeroed padding. Flexible
memory accounts for rounded native backing sizes. Fault reasons use static
string views, so a fault does not allocate or throw through lifted code. The
414 comparisons, atomic tests, loop guards and copied-input checks still pass.
Trophy module startup and remaining kernel/display services are the next gaps;
this newer host-only checkpoint has not replaced the signed 624-trace baseline.
