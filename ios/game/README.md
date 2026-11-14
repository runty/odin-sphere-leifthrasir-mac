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
