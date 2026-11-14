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
protection/fixed-address cases stop. Private file/save and offline NP setup now have checked bridges. This is not
yet a worker/TLS/fiber runtime or a connected game renderer. Extended x87 instructions remain an explicit
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


## Portable Trophy and lifecycle adapters

The runner now links the existing portable TrophyService with an Apple crypto
backend. [Service scope and crypto checks](../services/README.md). Trophy
context/handle/registration/unlock-state semantics pass in the host service
suite; state is explicitly transient until asset/profile configuration is wired.
The offline streaming provider accepts only this image's Initialize/Terminate
entrypoints and never starts recording, a network stream or a permission flow.
Message-dialog residency is accepted only because this image imports no such
API. Error/Save dialog lifecycle is implemented; missing presenters stop.

The earlier host startup reached **635 traces / 1,516 dispatches**, both original
initializers return zero, and no memory fault occurs. The PS4 direct-memory
capacity query returns the production port's 5,056 MiB value. The next gap is
`sceKernelAllocateDirectMemory`, requesting 51 MiB. A sparse backing/alias
adapter is required; the capacity query does not allocate or prove that pool.
The service objects compile successfully for both iPhoneOS and Simulator.

Native service messages remain in private logs. The CLI emits exactly one
`OSL_STARTUP_RESULT:` record so diagnostic text cannot be mistaken for its JSON
checkpoint. The UIKit API still returns JSON directly. This host checkpoint has
not replaced the clean signed 624-trace baseline installed on devices.

## Sparse memory checkpoint

The checked direct-memory adapter now completes real physical allocation and
mapping, including the first 51 MiB heap and subsequent game heaps. Host startup
reaches **664 traces / 1,638 dispatches**, with both module initializers returning
zero and no memory fault. It owns **1,070,596,096 direct bytes** at this checkpoint.
The next missing service is `sceUserServiceInitialize`; this is still not a game
boot. The native memset adapter processes 49 calls / 955,255,600 bytes and avoids
the large translated initialization loops. [Memory scope and checks](../services/README.md).

The fixed-address and two ASLR identity runs reach matching checkpoints. Copied
corrupt/truncated image and pointer-table fixtures are rejected. Native memory,
service, CPU comparison, atomic/fault and loop guard checks pass. iPhoneOS and
Simulator exports validate their inputs again before publishing an archive, so
an edit during compilation cannot silently publish mixed inputs. Physical
devices still had the clean signed 624-trace baseline at the first host check;
newer host/Simulator evidence does not qualify execution on a locked device.

Clean `dcd85de` Simulator execution now repeats the 664-trace checkpoint twice
(launch counts 5 and 6), with 414 CPU checks passing, preserved private input,
both initializers returning zero and no memory fault. The signed iPhoneOS build
includes the same audited Metal driver/framework and installs on both devices.
iPhone input copy succeeds but launch is denied Locked. The iPad input transfer
times out after installation, so launch is skipped and its new inputs are not
qualified. No fresh physical CPU or GPU execution is inferred from those installs.

The subsequent shared-user adapter reuses the production offline-user code and
reaches **666 traces / 1,640 dispatches** on the host, stopping at `scePadInit`.
The 104 AOT service checks, 58 standalone user checks (arm64 and Rosetta), copied
input checks and Mac 307-import regression pass. Both mobile libraries compile;
the installed clean probes still represent the earlier 664-trace milestone.

The subsequent GameController adapter compiles for iPhoneOS and Simulator,
passes 38 backend and 121 service checks, and reaches **680 traces / 1,655
dispatches** on the host. User/pad initialization and five gesture setup calls
complete; `sceSaveDataInitialize` is the next missing service. No dialog response,
save initialization or game frame is fabricated to advance beyond it.

Clean source `89184da` now reaches that same 680-trace checkpoint on two
Simulator launches (counts 7 and 8), with 414 CPU comparisons passing, users/pad
initialization complete, five gesture setup calls and no memory fault. Its
development-signed iPhoneOS app/framework also builds and verifies, staged in
the private external tree. Physical devices retain the installed `dcd85de`
baseline; no redundant locked-device install/launch was attempted for this
increment. Real hardware input/haptics and mobile game frames remain unverified.

The next storage adapter can reuse `src/runtime_savedata.c` and
`src/runtime_file.c`: both compile against the actual iPhoneOS SDK in a private
spike. The latter additionally needs the portable `runtime_guest_errno` mapping
and existing `runtime_errno`/resolver shims. Configure an isolated app-private
profile and map nested guest pointers through checked aligned locals before
exposing the save APIs; initialization alone is not persistence proof. Preserve
the existing Mac and player saves and test storage roundtrips/failures with
disposable profiles.

The storage bridge is now implemented and host-tested. It configures real local
`App0` / `MobileProfile` roots next to the private image, translates the imported
save/file APIs and reaches **682 traces / 1,657 dispatches** with save
initialization complete. Actual assets are not yet in `App0`. The next gap is
`sceNpSetNpTitleId`; game startup/rendering are still unproven.
[Storage scope and cross-process/failure checks](../services/README.md#file-and-save-storage).
Both mobile libraries compile and the rebuilt Mac source resolves all 307
imports. The updated Mac `f6f6aee` is now installed after copied-profile package/restart
checks; the physical device baseline remains `dcd85de`. See
[Mac delivery evidence](../../docs/MACOS_PORT.md).

Clean source `f6f6aee` now matches the 682-trace / 1,657-dispatch checkpoint on
two Simulator launches (counts 9 and 10). All 414 CPU comparisons pass; private
roots exist, save initialization completes, and no memory fault occurs. This is
not an in-game save/reload result. The signed iPhoneOS app/framework builds and
verifies, staged only; physical devices retain `dcd85de`.


## Offline NP and immediate-shuffle checkpoint

Clean source `0c6414d` reaches **751 traces / 2,236 dispatches / 26,718
checkpoints** on arm64 Mac and two Simulator launches (counts 11 and 12).
Both module initializers return zero, private storage/users/pad initialize,
offline NP setup completes, and no memory fault occurs. The next missing API
is `sceKernelCreateEventFlag`. Direct allocation owns 1,240,776,704 bytes.
The startup corpus is
`4385c7a6910d32b0b2d3ae9a1a2b1a6b3f84e22e6b058bda955a46b5f2267d28`.

The new isolated trace directory re-lifts the old executed PC list after the
VPERMILPS backend change; old generated IR remains intact. All 414 real-routine
CPU comparisons pass. Fixed/repeated ASLR checkpoints agree; copied corrupt/
truncated images and invalid/missing pointer tables are rejected. The exact
unsupported shuffle was identified with local Zydis after REA refused the
prepared PS4 format; no REA analysis result is claimed. The four immediate
shuffle forms pass all 256 immediates against native x86 execution, including
upper-YMM clearing and preserved flags. Other synchronous instruction fallbacks
still fault at the exact guest PC.

The offline NP/NetCtl bridge does not dereference or retain credential pointers.
It reports signed-out/disconnected state, tracks bounded title/request/callback
handles and refuses identity/data transfer. There are no synthetic online state
transitions or callback executions. Its lifecycle/ownership, budget, canary and
invalid-pointer tests bring the AOT service suite to 4,247 passing checks.

The matching iPhoneOS app includes the audited GPU framework and passes strict
signature verification in `ios-probe-offline-gpu/`. It is staged only; physical
devices retain `dcd85de`. `ios-probe-offline-simulator/verification.json` records
the fresh repeated execution and preserved inputs. App0 is still empty, so no
asset import, in-game save, battle, audio or mobile-frame result is inferred.
The [native arm64 renderer initialization](../graphics/README.md#native-arm64-game-frontend-initialization)
is a separate Mac check with zero guest draws; connect shared guest mappings
and the CPU runtime before adapting presentation to UIKit.


## Event flags and the renderer boundary

The shared event-flag bridge progresses through twelve real flag creations to
**859 traces / 2,568 dispatches / 28,275 checkpoints** on the arm64 Mac host.
Both actual initializers return zero, no memory fault occurs and the next
missing service is `sceVideoOutOpen`. Direct allocation owns 1,294,155,776
bytes. This stop is deliberate: no dummy VideoOut handle is returned and the
AOT guest has not yet been connected to the real presenter.
The corpus is
`43eadb7ca8528c099d9736565a0ab8579eed1db4dcae9f10fe74a9b53a7cde4a`.
Fixed/repeated ASLR runs reach the same functional boundary. Raw dispatch
counts can be 2,562 or 2,568 because three unsigned pointer-range checks may
visit two extra comparison blocks when ASLR changes relative pointer ordering.
Local Zydis decoding from the executed boundary and complete logical target
traces identify the exact branch and common join. `test-startup.py` normalizes
only that four-target pattern, pins its 47 instruction bytes by SHA-256, and
requires every other dispatch target to match. This compares canonical code
targets, not all CPU registers or memory. The resulting canonical traces and
all copied corrupt/truncated image/pointer rejection cases pass. The unchanged CPU
comparison core still has its separately qualified 414-check fixture archive.

Discovery now snapshots native source inputs before compilation and checks
them again before publishing; it cannot stamp a changed source hash onto an
older service object. The negative integration test runs the real service
compiler in a retained private worktree, edits only that worktree's user
service after compilation starts, and verifies failure without a published
checkpoint or any change to the original service source. Run
`ios/game/test-discovery-inputs.py --root "$WORK/ios-game-vpermilps" --output
"$WORK/new-provenance-fixture" --remill "$WORK/cpu-backends"` with Python.
Checkpoint JSON is written through a same-directory fsynced temporary file and
atomic replacement, preserving the prior report on an interrupted write. This
is not a transactional multi-file/power-loss durability claim.

Next connect the checked guest to the native arm64 renderer, including its
main-thread window, real VideoOut/Gnm/event calls, shared GPU mappings and
fault handling. Then adapt that path to UIKit/iPhoneOS, worker/TLS/fiber and
actual asset import. Simulator/physical-device execution remains independently
qualified; the current installed physical probe is still `dcd85de`.


The follow-up CLI target-only trace support produces current corpus
`03005ea232b97bd8807d55a8fdc55e33c00d8777d1c38e46ca5766fc46b68c40`.
Its AOT service suite passes 4,258 checks. The verified address-mode test records
raw counts 2,562 / 2,562 / 2,568 and normalized range-check visits 0 / 0 / 3;
unknown dispatch differences still fail. Evidence is in
`ios-eventflag-address-traces/` and `ios-eventflag-startup-tests-verified/`.
`OSL_STARTUP_DISPATCH_TRACE` is CLI-only and records normalized code targets,
never guest register, stack or credential contents.


Clean `10d9c3a` Simulator launches 13 and 14 now repeat 859 traces / 2,562
dispatches, matching the host's fixed-address mode. Both initializers complete,
twelve event flags exist, all 414 CPU comparisons pass and no memory fault
occurs. Input hashes survive replacement/relaunch; actual App0 assets remain
unimported. The matching signed iPhoneOS app/framework builds and passes strict
signature verification, staged only in `ios-probe-eventflag-gpu/`. Physical
devices retain `dcd85de`; no physical launch or guest frame is inferred.
Evidence: `ios-probe-eventflag-simulator/verification.json` and
`ios-probe-eventflag-gpu/verification-status.json` in the private build root.

## Native GPU-backed discovery

The [checked native renderer bridge](../graphics/README.md#checked-guest-connected-to-the-native-renderer)
now passes real VideoOut initialization instead of returning a placeholder
handle. The guest registers three 1080p buffers through shared direct-memory
mappings and owns two real event queues. GPU-enabled sessions force native
identity addresses and release GPU references before freeing CPU memory.
Missing Gnm/worker services remain explicit; no game frame is claimed.

System settings now share the Mac provider for language, date/clock/time zone,
offline foreground status, full virtual-canvas safe area and splash hiding.
Checked adapters use aligned temporary outputs and reject protected or partial
guest spans. Both arm64 and Rosetta pass 74 dedicated system-service checks.
Unknown parameter IDs stop the partial guest instead of exiting its host app.
The full virtual canvas must eventually be placed inside UIKit's physical safe
area; this provider does not implement mobile presentation or app lifecycle.

`advance.py --gpu-build ... --gpu-driver-manifest ... --identity` records a
`native-arm64-gpu` reference with explicit native dependency hashes. Exports
identify that reference mode separately from `gpu_frontend_linked=false`.
Without a GPU callback provider, a mobile/CLI consumer still stops at VideoOut
even when its archive contains later translated blocks. `build-ios.py --sdk
macosx` can export a native startup archive for this engineering frontend.
Physical-device and Simulator baselines above remain independently qualified.

The next native GPU-backed checkpoint passes two actual condition creations and
reaches `scePthreadAttrSetdetachstate`: 934 translated entries / 2,949 dispatches /
30,230 checkpoints, both initializers complete, no memory fault and no guest
frame. Corpus `3aa148b3eed6b702e30222523f1013958be59bc6ebf79eb0dd2718921ea982e5`.
The real condition adapter and its tests are described in
[service coverage](../services/README.md#checked-condition-variables).
The native GPU/source ledger now also covers renderer CMake and GLSL/HLSL helper
inputs. This does not enable translated workers or change installed mobile apps.

Thread attributes now retain detach state, scheduler policy/priority,
inheritance, stack size and affinity with the same validation as the native
Mac provider. Complete output spans are checked before allocation/destruction,
and the session limits live attribute objects to 4,096. These are stored
virtual-console settings; no Darwin/PS4 scheduler equivalence is claimed.
Nineteen additional import-dispatch boundary/canary checks pass (4,285 total).
The native GPU-backed guest now reaches the first real `scePthreadCreate`:
938 entries / 2,953 dispatches / 30,237 checkpoints, both initializers zero,
two real conditions and no memory fault. Corpus:
`d0c2a3364f8c345b1b8849229badf55a6adc9589a50eb059d33c06fd44137d28`.
The first requested worker entry normalizes to `0x10738d70`. Worker creation is
still an explicit missing service; a successful spawn is not fabricated.
Both mobile SDKs compile this corpus; neither installed mobile app changes.
