# Checked module and eboot startup

This is a bounded startup experiment, not a playable mobile game. It runs the
actual libc and Fios2 initializers and then the real eboot entry through signed
arm64 AOT code. Unsupported instructions, missing blocks and host services stop
explicitly. No placeholder service reports full-game success.

## Current engineering status — 2026-11-15

Latest native execution uses **67,546 entries**: corpus `fc0deff0`, 171.070
seconds / 3,010 presentations, next missing PC `0x106e297e`, 43 joined workers,
no memory fault, renderer cleanup and unchanged original/copied player saves.
The fresh frame-2,600 image is decoded and visually inspected opaque black.
The input file hash matches, but no guest input reads occur before the worker
stop. The input gate explicitly fails; this establishes startup/integrity only,
not a timing comparison, scripted route, visible scene or physical controls.
The failed input-required verifier and a separate limited report are preserved
in `inrange-jbe-discovery/` and `checkpoint-67546/`.

Both actual worker gaps, `0x106e297e` and `0x106af835`, are included in the next
batch. A read-only survey now includes historical worker missing-PC reports:
23 gap owners, 492 statically selected owners within two direct-call hops.
This is selection, not a claim that all selected owners execute. All **926**
new first blocks accept and pass independent XED checks, **5,325 instructions**,
including two retained fatal UD2 stops. Audited publication preserves every
prior raw hash and grows availability to **68,472 entries**. Its bounded native
build/replay is running, with a unique frame-2,900 capture and post-checkpoint
freeze; no execution result is claimed yet. Phase:
`observed-owner-batches/after-67546-worker-two-hop/`, inputs `33717ed4`,
records `30279b91`. The previous 5,718-entry audit remains immutable.

The revised boundary audit is separately versioned: exact original 0f0b,
XED ud2/length 2 and a retained remill-error stop are required. Its qualified
bridge fixture can be reused across batches without pretending the fixture's
original PC was selected again. Creation pins its raw LLVM, verifies the
original image's trap bytes and reruns the actual guarded fixture with the
unchanged production runtime. A wrong original fixture is refused before any
revision is created. Parent producers, records, inputs and helpers are unchanged.
No instruction semantics or runtime branch policy is changed.

The iPad is wired/paired with Developer Mode enabled, and only its explicit
UDID is queried. Before another update, its three existing system-save files
are backed up and match the previous verified hashes. Only the older stopped
Odin probe is closed; other apps are untouched. **The installed device build is
still the 60,361-entry checkpoint below.** A 67,546-entry SDK recipe is held
because native scenes remain black and input is unobserved; continue actual
worker-gap recovery before rebuilding the device app. The phone is excluded.
Recognizable mobile scenes, audible output, physical controls and character-
progress save/load remain open.

The current physical iPad checkpoint is signed `e5cbb68`, corpus `f55967f7`,
with 60,361 entries, the twenty-patch backend and restored recursive global
memory locking. Launches 33/34 pass CPU 414, memory 19 and GPU readback 64,
join all 43 workers and complete cleanup without a memory fault. They reach
`0x1066bf99`, matching the executed native checkpoint, after 131.392/149.200
seconds and 3,321/3,359 presentations. The actual device executes the new native
copy/move adapters; this is stronger evidence than their SDK fixture compilation.

All three system-save creation hashes survive update, quitting and the same-build
cold relaunch. Character-progress save/load remains unverified. Scripted active
reads are 18/21, not physical touch/controller acceptance. Fresh game and display-
swapchain captures at requested frame 2,600 are opaque black (1512×850 and
1512×2316); the display capture includes the Vulkan host overlay, not iPadOS or
UIKit layers. The cold run captures 57,163,776 bytes / 148.864 seconds of converted
stereo float PCM: entirely silent and finite. Three sampled empty-queue observations
are not three proven audible glitches. Audible output stays unqualified.
[Physical results and limits](../graphics/README.md#expanded-global-lock-ipad-and-cold-audio--2026-11-15).

Both SDK game/boundary archives compile from the frozen 60,361-entry checkpoint;
843 newer unexecuted entries are deliberately excluded from that device build.
The stripe experiment is not adopted after its deadline-only game result; its
separate worktree/reports remain preserved. Owned device PIDs 49979/50093 are
stopped and absent, other apps are untouched and the iPhone is excluded by the
player's explicit instruction. UIKit appearance-transition warnings remain.

Separately, audited local in-range JBE table candidates grow native availability
to 61,204 entries. Their full guard/index/CFG proofs remain explicitly absent;
no runtime branch policy changes. Wider native replay uses additional diagnostic
skip/confirm presses on disposable data and a new frame-4,000 capture path. Its
outcomes require separate execution/image verification. Recognizable title/town/
combat, nonempty game audio, physical input and character progress remain open.

### Earlier initial-notice checkpoint

The earlier iPad build is signed `fd55ae1`, corpus `f4b3357e`, with 9,585
entries. A real GPU pixel readback shows upright, readable initial guest UI:
**“There is no save data.”** Native capture is 960×540; physical iPad capture
is 1512×850. This verifies the initial save-data notice, not title/town/combat.
The device's 90-second run presents 4,045 buffers and joins all 23 workers,
with CPU 414, memory 19, GPU readback 64, no memory fault and full cleanup.
Its three empty profile directories remain unchanged; owned PID 43635 is
stopped and absent. Both SDK renderer/game/boundary archives compile; the
UIKit capture source also compiles for Simulator. The iPhone stays untouched.
Evidence is `frame-snapshot-ipad/device/verification.json` and
`native-first-frames/frame-snapshot-native/verification.json` at the build root.

At that checkpoint the next step was input-driven menu progress. The notice explains why the earlier
unattended runs kept drawing the same UI. Consult OpenCrown's frame-indexed
input grammar and real-input precedence, together with Odin's existing desktop
pad replay, before adding a bounded scripted source to the AOT harness.
Touch controls, playable scenes/audio and new-save cold reload remain open.

The preceding physical iPad build is signed `fe01964`, corpus `5095116e`, with
9,585 entries. Launch 18 runs 166.7 seconds, presents 8,641 buffers and
submits 43,205 Gnm batches. Its explicit 9,000,000-dispatch engineering limit
stops execution before the 180-second ceiling. It has no missing-block stop
or memory fault, joins all 23 workers and passes CPU 414, memory 19, GPU
readback 64 and full cleanup. Its three empty profile directories remain
unchanged; the exact owned PID is stopped and absent. Both SDK game/boundary
archives compile. This does not establish recognizable imagery or gameplay.
Evidence is `startup-simd-visible-ipad/device/verification.json`.

The preceding iPad mini A20 Pro checkpoint runs signed `dcf581b`, corpus `71eddd63`
with 9,521 entries and the nineteen-patch backend. Cold launches 16/17 use a
180,000 ms engineering ceiling and run 35.1/32.3 seconds. They present
645/599 buffers and submit 3,228/2,998 Gnm batches, stopping at the same
missing block `0x106b9c7a` reached by the native Mac. CPU 414, memory 19,
GPU readback 64, all 22 worker joins and complete cleanup pass. The original
empty three-directory profile is unchanged; both owned processes are stopped
and absent. Recognizable mobile imagery, gameplay and new in-game save/reload
remain open. The iPhone retains `460ecdb` while away and is not contacted.
Both SDK game/boundary archives compile; Simulator renderer-consumer linking
remains open. Evidence is `startup-simd-ipad/device/verification.json`.

The preceding fourteen-patch native cache reached 9,521 entries and an actual `VEXTRACTPS`
fallback at `0x106b796c` after 63.4 seconds, with all 22 workers joined.
The capture still contains a black clear and depth-only draw. OpenCrown's
startup lane patch is now rebased onto Odin with its added implementation
unchanged. It supplies `VPINSRB`, `VPINSRD` and `VEXTRACTPS` register/memory
forms. The new audited backend passes 467,456 native-Rosetta comparisons
using Odin's checked-memory runtime: 82 lane-form routines plus the original
12-instruction matrix-extraction span, including unaligned-store canaries,
source preservation and full GPR results. The observed instruction also
lifts without the fallback and compiles for both mobile SDKs.

A broader check found missing variable blends, 128-bit extraction/permutation
and AVX dword shifts. OpenCrown's four related patches now join the lane patch;
the complete audit expects nineteen patches. The new built backend passes
1,590,784 dword comparisons across 239 routines and 256 vectors, including the
original twelve-store sequence and full-width memory-store guards. All 414
real-module comparisons and both SDK archives pass. The reusable checker is
`ios/cpu/check-startup-simd.py`.

Build this backend in a new directory. Existing fourteen/fifteen-patch caches
remain frozen. Regenerate guest IR for the new lifter/semantics identities,
using old manifests only as address availability seeds. A fresh
`ios-game-startup-simd` cache regenerated all 9,521 seeded addresses without
copying old IR or objects. Seeded availability remains execution-unverified.
Real startup passes the VEXTRACTPS stop and reaches `0x106b9c7a` after
65.9 seconds with no memory fault and 22 joined workers. Its frozen checkpoint
is the one tested twice on the iPad above.

Further native discovery reaches 9,585 entries. The completed ten-minute
native run presents 9,036 buffers and joins all 23 workers without a missing
block or memory fault. Five flip-interval captures contain 25 textured color
draws per frame into 1920×1080 RGBA8 sRGB, sampling two BC3 textures. These
are renderer command captures, not pixel readbacks; a recognizable scene
remains unverified. The run stops at its host deadline and cleanup passes.
Evidence is `extract-reuse/startup-final/`, `cpu-backends-startup-simd/`,
`startup-simd-ipad/`, `startup-simd-visible-ipad/` and
`native-first-frames/startup-simd-600s-capture/` at the build root.

Native development now uses [the adapted OpenCrown switch tools](control-flow/README.md).
The expanded conditional fixture passes 42 checks, with strict output bytes
identical to the original tools; the unchanged strict and late-copy matrices
pass 118 and 49 checks. A separately derived stack-protector termination map
uses OpenCrown's 23-check original-ELF import validator. Its three matching
Odin module thunks let the scanner stop before embedded table data.

The private cache has 624 more case targets from 79 selected tables, 239 more
direct callees and 373 further direct-call descendants. All compile with the
original image/backend/function bounds and production guards. Conditional
index assumptions remain explicitly unproven in the ledger. A final direct
scan finds no new unattempted callee; three speculative x87 candidates remain
excluded and actual runtime demands stay authoritative. This extends available
code, not verified execution. Continued real startup is required.

Evidence is under `extended-ipad-budget/`, `conditional32-{fixture,matrix,late-index}/`,
`noreturn-reuse/`, `conditional32-noreturn{,-strict}/` and
`native-first-frames/` at the external Odin build root. Earlier checkpoints
follow as history.

## Earlier module and memory checkpoints

Clean signed `1376d6a` iPad mini A20 Pro launches 12 and 13 now complete
both actual module initializers and allocate 1,294,155,776 bytes from the
direct pool in a 4 GiB owned guest window, without the old null-memory fault. The permanent
backing view is absent. Real startup reaches the explicit `sceVideoOutOpen`
service gap at 1,027 entries / 2,562 dispatches, with no memory fault. Both
launches pass all 414 CPU cases, 19 actual alias/protection/zero-reuse/teardown
checks and 64-word Vulkan fill/fence/readback. Private inputs remain identical.
This is real physical memory/startup progress; no mobile game frame is claimed.

The AOT provider retains full physical-pool accounting and uses transient
backing aliases capped at 4 MiB per chunk. Its owned window tries 8/4/2 GiB,
rejecting mappings that do not fit. Tests pass 138 contracts normally and under
ASan/TSan, including concurrent writeback and forced zeroing when hole punching
is unavailable. Headless fixed/identity/corrupt-input checks still pass. Corpus:
`6be8c7c08d02a24ca7373c0a79ec785a4f44001c798ee31f33b87109106fff23`.
It reuses verified IR from the unchanged older backend in a separate directory.

Both devices now list clean `1376d6a`, installed after Documents backups.
iPhone foreground launch is denied while locked, so fresh iPhone execution
remains unverified. Clean `00c26a1` Simulator launches 17 and 18 pass CPU 414 and native memory
19, preserve inputs, and reach VideoOut (1,027 entries / 2,562 dispatches,
8 GiB guest window, zero permanent backing view). This is Simulator evidence,
not fresh iPhone execution. Mobile App0 assets and
the actual game GPU frontend are still unconnected. The packaged Mac remains
on validated `af52e09`; the player-assisted 30-minute session is deferred.

The earlier qualified native arm64 Mac AOT/GPU corpus `25594870` ran four
actual game-created workers with owned state/stack/TLS and checked shared
mappings: 1,022 entries / 3,217 dispatches / 31,840 total checkpoints, both
initializers zero, three 1080p buffers, no memory fault, zero guest frames.
The later executed VRCPPS gap now has qualified six-form reciprocal semantics
(1,572,864 output lanes per CPU path, ISA contracts rather than a bit-exact
processor approximation table). A fresh full-window re-lift now regains four
joined workers and three real 1080p buffers at 1,036 entries / 3,169 dispatches.
It executes past VRCPPS and stops at the next explicit unsupported `VBLENDPS`
(`0x106aa713`), with no memory fault and zero game frames. Corpus `149b9196`.
Normal GPU qualification still passes 60 bridge cases and ordered cleanup;
fixed/identity/corrupt-input checks also pass. The earlier narrow-window cache
is retained separately, not used to claim this recovered milestone.

The sections below retain earlier checkpoints and their limitations as history.
The latest worker/clock qualification is described at the end of this file.

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
reuses only the compiled PC list (including any static candidates) and re-lifts every block with the new backend;
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
protection/fixed-address cases stop. Private file/save and offline NP setup now have checked bridges. The worker/TLS foundation is implemented; guest callbacks/fibers and a connected mobile game renderer remain incomplete. Extended x87 instructions remain an explicit
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

## Worker memory and TLS foundation

Checked CPUs now share a synchronized mapping table while retaining separate
faults, checkpoints, register state and interruption flags. Unmapping removes a
descriptor from every attached CPU; the session must keep its backing storage
alive until all CPUs and GPU users have stopped. Mapping locks leave scope
before a fault can longjmp. `GuestMemoryScope` lets host services use the same
recursive lock as scalar/CAS transfers and must be released around waits.

`thread_storage.h` owns each worker's aligned stack and lazy module TLS. It
copies the real initialized template, zero-fills the rest, validates module/
offset/index spans, publishes mappings to attached CPUs and removes them before
freeing its bytes. Primary startup now configures libc/Fios2 templates and the
real `__tls_get_addr` adapter; actual worker creation remains unsupported until
execution ownership, blocking-service scopes and join/teardown are connected.
No additional game dispatch progress or guest worker execution is claimed.

The shared-map checks pass on arm64/Rosetta with 20,000 synchronized CPU/host
updates, 2,000 concurrent map cycles, protected-fault lock recovery and cross-
thread stop/cancel interruption. Arm64 ASan/TSan pass. Real-template storage
checks pass 55 arm64 / 52 Rosetta cases with four native workers, separate TLS
values, stack limits, invalid/protected spans and complete mapping teardown.
Arm64 TSan passes all 55, including native identity addresses. ASan passes 53:
its heap lies above 40 bits, and the identity case explicitly verifies rejection
rather than truncating or substituting an address. This is native engineering
coverage, not translated guest-worker execution or a physical-device result.

The rebuilt three-routine/two-worker CPU comparisons still pass all 414 checks;
fixtures and both mobile runtime archives are in `ios-cpu-workers/fixtures/`.
Core hash `8008e1e3c1c1ea991971c5bd893b4099642223403aa6033e259b395d998c6c3c`.
All 4,293 imported-service checks, the 60 real GPU-boundary checks, headless
address/input rejection and both mobile SDK archive builds pass. Current native
GPU-backed corpus `29361ce73a39bb8c162f9412b7400e27bc4e8a0664e1d1c0e60a615e3f8dc606`
still stops at `scePthreadCreate`, 938 entries / 2,953 dispatches. Old installed
mobile probes and their earlier CPU fixture hashes remain separate baselines.


## Owned guest workers, clocks and failure cleanup

`workers.h` owns real host threads and preserves their CPU/stack/TLS storage
through join until the frontend drains its GPU. Handles are monotonic, live
workers are limited to 64 and retained records to 4,096. A session stop prevents
new dispatch/HLE work, interrupts checked loops and bounded waits, releases
thread-owned mutexes and joins every worker before GPU/CPU memory teardown.
The frontend attachment guard also releases ownership during native C++
exceptions. Cancellation requests are deferred; arbitrary host pthreads are
never killed asynchronously. Full guest cleanup callbacks, keys/once/rwlocks
and any remaining thread APIs still need their actual providers when reached.

Host-service scopes serialize shared frontend state and raw guest transfers.
They release both leases around blocking mutex/condition/event waits, joins,
sleep and yield, then reacquire them before touching guest outputs. Actual
concurrent import-dispatch fixtures cover another CPU setting a waited flag,
releasing a waited mutex/condition and completing a join. Exception fixtures
cover parent unwind while a child must reacquire its mutex, and child unwind
while it owns a mutex. Optimized-library and HLE faults now use the common CPU
fault resumer, releasing an outstanding guest atomic lock before longjmp.

Kernel TSC calls and the renderer share raw monotonic nanosecond ticks and a
1 GHz frequency. Process counters share a relative monotonic epoch; process
time uses microseconds. `ClockGettime` follows the native port's clock-ID map,
while `Gettimeofday` uses actual wall time. Output adapters validate complete
16-byte spans and use aligned fixed-width values rather than casting guest
memory to Darwin structures. The clock tests pass 136 cases / 80,000 monotonic
reads per Mac architecture and on arm64 ASan/TSan.

Executed-target discovery records each worker's logical missing PC/service,
fault status and checkpoint count. It can lift several genuinely executed
missing blocks in a round; `test-discovery-targets.py` rejects malformed,
non-executable and already-known targets in 22 cases. Reports no longer dump
argument registers. Concurrent scheduling may expose a different first gap, so
the frozen GPU check validates every joined CPU's executable/import boundary;
it does not claim canonical path, register or memory equivalence. Headless
fixed/ASLR startup retains its stricter byte-pinned target-trace check.

Qualification in the private Odin build root:

- `ios-workers-guard-qualified/`: 4,322 real HLE checks and two exception cases.
- `ios-workers-guard-{address,thread}/`: the same complete C/C++/Objective-C++
  provider/checked-runtime fixtures pass ASan and TSan. Linked guest IR is
  explicitly uninstrumented; this is not a full-game sanitizer qualification.
- `ios-workers-guard-gpu/`: 60 actual GPU bridge cases and a fresh four-worker
  guest run with three 1080p buffers, balanced queues/port and ordered cleanup.
- `ios-workers-guard-headless/`: fixed/ASLR and corrupt/truncated-input checks.
- `ios-workers-fault-owner-{tests,address,thread}/`: 168 worker lifecycle checks,
  four actual AOT libc routine workers, a real 64-live-worker budget and cleanup.
- `ios-worker-fault-memory*`: 17 cases, 20,000 CPU/host atomic updates and 2,000
  concurrent map cycles, including recovery from native-scope atomic faults.
- `ios-cpu-worker-faults/fixtures/`: all 414 real routine comparisons and matching
  mobile runtime archives; core hash
  `ff76064aabf9066e80742ea14471617d2da5bbfac6d39d155b966987c0e3be86`.

`ios/game/test-worker-services.py --root ... --output NEW --remill ...
--sanitizer address|thread` rebuilds all portable providers against a frozen
source/IR/object ledger. Stop discovery first. The real assets remain read-only
on the host only; physical/Simulator installed baselines are independent.


### Mobile memory capabilities and provisioning

`OSL_PROBE_EXTENDED_MEMORY=1 bash ios/build-probe.sh ...` requests
`com.apple.developer.kernel.extended-virtual-addressing` and
`com.apple.developer.kernel.increased-memory-limit` from the checked-in
`ios/OdinSphere.entitlements`. These are normal Apple capabilities, not JIT
or unsigned-code permissions. See Apple's [extended virtual addressing](https://developer.apple.com/documentation/BundleResources/Entitlements/com.apple.developer.kernel.extended-virtual-addressing)
and [memory limit](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.kernel.increased-memory-limit)
documentation. Additional physical memory is device dependent; callers must
still handle allocation failures.

The current cached wildcard provisioning profile lacks both capabilities;
Xcode reports No Accounts when asked to create a matching profile. Of 36 cached
profiles, one has both memory capabilities but belongs to another bundle, so it
must not be reused to overwrite that app. The capability-enabled probe has NOT
been signed or installed, and no fix is inferred from merely adding entitlements.
The baseline probe remains buildable with the default capability flag off.
Unattended memory-provider diagnostics and backend work can continue separately.


The signed baseline `cccfa7e9d1838ba056f2884c642836f346ad203b` iPad launch 8
adds a kernel-chosen reservation attempt before desktop hints. It still reports
`virtual window reservation (errno 12)` for the 8 GiB window. CPU 414 and the
actual 64-word GPU readback continue to pass; both original private input files
retain their hashes. Corpus `68abd973c7b555821aad1f2a73230c09d8bc3dd14b064a33c5cf6e348e500365`
is frozen at 1,027 host entries / 3,220 dispatches / four joined workers and an
explicit VRCPPS backend gap. Host memory 101, fixed/identity rejection tests and
60 real GPU bridge checks pass, including ordered cleanup and no game frames.
A separate baseline Simulator archive compiles from the same sources.

The capability-enabled build is still unsigned: no matching profile/account is
available. Entitlements have therefore not been proven to fix this reservation.
Next physical memory work must either validate a correctly provisioned extended
address space or qualify a smaller/chunked mapping design against actual title
requirements. Do not shrink the reported PS4 physical pool or claim unsupported
allocations succeed. Missing VRCPPS translation can be developed independently
in a new backend/cache without invalidating this frozen corpus.

### Opt-in physical VM limits probe

Developer launches may set `OSL_VM_PROBE=1`. `virtual_memory` in diagnostics then
records anonymous reservation and sparse backing-map results after the CPU/game/
GPU probes. It touches no pages in these test mappings, writes no sparse-file
data, records no addresses and removes every mapping/file handle. This measures
virtual capacity, not resident-memory/GPU workload endurance or gameplay.

Actual iPad `d7f6603-dirty` launch 9 (diagnostic candidate) passes CPU 414 and GPU
64 words. Eight GiB anonymous reservation fails ENOMEM; 4, 2 and 1 GiB reserve
successfully below the GPU address limit. The current default 6,080 MiB sparse
backing maps alone and with a 1 GiB window, but fails with a 2 GiB window.
All cleanup succeeds. This is evidence for eliminating the full-pool backing
view via bounded aliases and using a qualified smaller guest window; it is NOT
an implemented allocator fix. The reported PS4 physical pool must remain intact,
with allocations/mappings rejected explicitly when actual host limits are met.


Clean signed `1a2bba4` repeats the VM measurements on iPad launch 10, with CPU
414 and GPU 64-word checks passing. Default launch 11 omits the VM diagnostics
as intended and passes both check sets. Actual game startup is still blocked by
the 8 GiB allocator window. New-backend corpus `01c7d8a3` now has 1,076 entries /
652 dispatches, both initializers zero and no memory fault, rebuilding narrower
seed coverage; it has not yet regained the older VideoOut/four-worker checkpoint.
The game reciprocal path and mobile gameplay remain unverified.

### Bounded AOT backing aliases

The AOT memory provider now keeps the full 5,056 MiB physical-pool accounting and
sparse file, but omits the permanent 6,080 MiB backing view. Host/GPU writeback
uses checked transient aliases of at most 4 MiB per operation chunk. Guest page
protection remains in place. The owned guest window tries 8, 4 and 2 GiB in
order, with non-destructive placement and the same 40-bit checks. Requests that
do not fit the actual window still fail; no physical capacity is fabricated.
Normal x86 native builds retain their full backing view and reservation policy.

`direct_window_bytes` and `direct_backing_view_bytes` report the actual choice.
The native `memory` probe independently checks shared alias visibility, a write
larger than the alias chunk across read-only pages, preserved protection,
release/zero-on-reuse and teardown. It is synthetic and reports no game frames.

Allocator tests pass 138 checks normally and with all portable providers/core
instrumented under ASan and TSan, including concurrent disjoint writebacks.
`--force-zero-discard` qualifies bounded alias clearing when hole punching is
unavailable; unrecoverable clear/alias cleanup failure stops rather than
recycling uncleared storage. A native ObjC++ probe passes 19 contracts. Headless
fixed/identity/corrupt-input qualification still passes. Actual device tests are
pending; host success is not physical memory qualification.

The isolated `ios-game-bounded-memory/` experiment reuses only hash-verified IR
from the unchanged earlier backend, not the new reciprocal backend. It preserves
the original frozen directory. Host startup reaches VideoOut at 1,027 entries /
2,568 identity dispatches, both initializers zero, 1,294,155,776 bytes allocated,
8 GiB window and zero permanent backing-view bytes, without a memory fault.
This does not establish mobile GPU submission or game frames. New reciprocal
coverage continues separately in `ios-game-rcpps/`.

### Backend provenance when building SDK archives

`build-ios.py` requires the Remill commit, lifter binary and semantics binary
to match the frozen trace ledger before it creates output. Its manifest carries
those inputs into packaging. `build-probe.sh` then requires the CPU fixtures
and game library to match all three backend fields as well as the checked
runtime. Archives made before this metadata was recorded need rebuilding with
their original matching backend. A rejection does not rewrite an existing
archive. This prevents a passing diagnostic from silently qualifying a game
library built with different instruction semantics.

### Packed-double host checkpoint

The isolated eleven-patch backend re-lifts all 1,039 seed PCs at 65,536-byte
windows and advances through the previous VBLENDPD stop. Twenty bounded native
GPU discovery attempts establish 1,058 entries and then stop at the next missing
translated block, `0x107d50a0`; this is an executed path still to extend, not a
newly identified unsupported instruction. Frozen corpus:
`ab9deb922fdb359e39534f205141355b49567f785e581820e2d9a9a90adffd68`.

Strict qualification repeats all 60 GPU bridge checks, both zero-return module
initializers, four joined guest workers, three registered 1080p buffers, two
created/deleted queues, GPU mapping release and zero presented game frames.
Concurrent dispatch/checkpoint totals vary; the check validates every stopped
CPU against executable/import boundaries rather than claiming identical thread
schedules. Headless fixed/identity/corrupt-image/pointer checks also pass.

UIKit guest integration must read the SDK from the validated private image's
procparam, as the native frontend does. This US base image contains `0x03008041`;
the renderer-only diagnostic's fixed SDK must not be reused as game metadata.
The shared VideoOut adapter does not yet connect the UIKit session to guest
execution. Physical apps and the installed Mac remain unchanged.

Both matching SDK archives now compile this checkpoint: 1,079 arm64 objects
per archive, all platform-audited as iOS or iOS Simulator. Their manifests match
the qualified CPU fixture backend. A complete default Simulator diagnostic app
also builds and passes bundle signature verification with the new packaging
provenance checks. These outputs are staged, not installed or executed; the
archive manifest still marks `gpu_frontend_linked=false` and no full-game boot.

### Frontend cancellation and actual UIKit execution

Corpus `6cb0f60193353d971a5091216f51cc7d267d0ea79951b8b2a050208e8ab01898`
refreshes 1,059 entries after the optional frontend stop flag is shared by the
primary and child CPUs. Real native GPU 60, HLE/core ASan/TSan 4,322 and three
cleanup/stop fixtures pass; both SDK archives build with matching provenance.
The actual iPad startup app now links the separate audited GPU frontend and
executes this corpus: two zero-return initializers, four joined child workers,
three video buffers, missing block `0x106af238`, no fault or presented frame.
The library manifest's `gpu_frontend_linked=false` describes that archive by
itself; the app links the frontend separately and reports the real adapter active.

Normal cold launch/relaunch, explicit buffer-boundary stop and controlled actual
foreground-change cleanup are verified. The scene test cancels the guest and
finishes teardown after reactivation; it does not resume gameplay. App0 assets
are now present on iPad with round-trip SHA checks for all 33 selected files,
excluding one keystone under the key-path rule. iPhone retains the older probe.
[Full integration scope](../graphics/README.md#uikit-guest-connection--2026-11-14).

Independent same-backend host discovery now reaches 1,112 entries and 17 joined
game-created workers, stopping at `sceCoredumpRegisterCoredumpHandler` rather than
a CPU block. Corpus `95784f27fe7daab11d6be795ee828b3d927f2d33395579846af9f52031a8bd22`
passes native GPU 60/queue-mapping cleanup and headless/ASLR/input rejection
checks. No new mobile archive/install or frame is inferred from that later host
checkpoint; physical startup remains on the qualified 1,059-entry corpus.
The next service needs its actual ABI/capability checked; the desktop's generic
no-op registration is not evidence of a functioning mobile crash facility.

### Crash-report capability and initial Gnm packet adapters

`bc3c9f0` returns explicit coredump errors without retaining callbacks or reading
user data. A non-null handler with an otherwise valid stack size receives
`0x81180004` (crash-thread creation failure); invalid registration parameters,
unregister and out-of-handler writes retain their respective library errors.
The ABI/error reference is [shadPS4 at the pinned revision](https://github.com/shadps4-emu/shadPS4/tree/a26cac679b3f95fee5b591689adec9e53b62adfb/src/core/libraries/kernel/coredump).
The actual guest proceeds past its registration attempt, and the report keeps
`crash_callback_supported=false`. No crash-report facility is claimed. Fourteen
additional dispatch/canary/boundary cases bring HLE/core tests to 4,336, passing
normally and under ASan/TSan with the existing three cleanup fixtures.

`7761749` adds typed adapters for DrawInitDefaultHardwareState200 and
DispatchInitDefaultHardwareState. They call the actual backend through aligned
scratch storage, preserve untouched packet padding, copy out checked bytes and
reject protected/unmapped outputs. Small buffers retain the backend's no-write
return. Native comparisons cover unaligned guest pointers, huge size values,
canaries and exact 256-dword packet contents; total GPU bridge checks are 109.
The boundary/fixture ASan also runs these checks and the three guest lifecycle
modes plus five input negatives. Renderer/game IR/driver instrumentation limits
remain explicit. Both SDK boundary archives compile; no new device build/run is
inferred. These packet writers do not submit GPU commands or establish a frame.

Checkpoint `567743a3224459a4899cdc23376d854aae74cdab571f421de7d90966ea1220dd`
has 1,113 entries and continues past crash registration to missing CPU block
`0x106adf60`. The packet adapters are qualified independently; that guest path
has not called them yet. Further native discovery is extending the executed path.

### Interruptible event waits and the 1,178-entry checkpoint

`facb6f5` lets native event waiters observe frontend/session cancellation without
waiting for the full two-second startup deadline. Their queue identity remains
intact, and host interruption never becomes a fabricated guest timeout or EINTR.
[Native and checked-bridge evidence](../services/README.md#interrupting-a-parked-event-waiter).

Checkpoint `532589e301a7b99c7952b44e63ff271eb816255ef72fa11d838d245c641e8e40`
contains 1,178 entries. The current native arm64 renderer run passes both module
initializers, starts/joins 17 workers, registers three buffers and stops at the
next untranslated block `0x10745e30`, with no memory fault or game frame. The
strict renderer replay takes 350.250 ms and passes 109 bridge checks plus queue/
mapping cleanup. Concurrent/timing-dependent paths may stop at other executable
gaps; this does not establish complete startup or equivalence across schedules.

The separate ASan boundary/fixture run passes the bridge and normal/pre-stop/
live-stop lifecycle modes plus five image/pointer validation cases. Its normal
run reaches an earlier executable gap with zero child workers; it does not
qualify seventeen-worker boundary cancellation. Renderer, game archive, guest IR,
driver and system libraries remain uninstrumented in that test. Evidence is in
`eventflag-stop-qualified/{native-gpu,guest-session-address}/verification.json`.
After refreshing all three SDK game archives, the stricter boundary verifier
also pins linked archive hashes, checks the actual trace count and rejects a
normal stop without a genuine untranslated executable gap. That ASan rerun is
`guest-session-address-refreshed/`. iPhoneOS/Simulator game archives now contain
all 1,178 entries; sampled runner, thunks and eventflag objects have the expected
SDK platform. Separately compiled boundary objects pass their SDK platform audit
at `ios-guest-session-event-stop/`. Compilation is not device execution.
Physical iPad remains on the earlier 1,059-entry startup app. No later iPhone,
iPad or mobile gameplay/frame result is inferred from these host checks.

### Shader packet integration and continued CPU discovery

`20b971a` adds the five checked PS/VS/CS packet adapters in the shared native
frontend. [Native comparisons and limits](../graphics/README.md#checked-shader-packet-writers--2026-11-14).
Qualified checkpoint `7a226984eb64f244a5428f0a512aac587adebb8aa0936eaa2848f7e980477dae`
has 1,197 entries, two successful initializers, 17 joined workers, three buffers,
zero shader-packet calls and no frame; next explicit CPU gap is `0x107d4d50`.
Strict native replay takes 240.373 ms. The separate boundary ASan normal mode
again takes a different executable path before child threads start. Mobile SDK
archives compile; the physical apps are unchanged.

The previous batch was deliberately interrupted at a verified 1,196-entry
checkpoint to connect the independently tested adapters. All generated header
hashes and the native binary matched before interruption; the extra completed
raw lift passed LLVM parsing and was preserved. No cache was deleted.

`66d70af` adds `--stop-file <path>`: creating that file asks discovery to finish
its current real startup attempt and publish a complete checkpoint, then exit
before lifting the next target. It does not interrupt a compiler/guest or pause
the overall porting goal. A three-round request with an existing stop file
completes exactly one attempt, exits normally, matches every generated header
and leaves no raw IR outside its checkpoint. Evidence:
`gnm-shader-packet-qualified/checkpoint-stop-verification.json` (1,198 entries,
17 joined workers, missing CPU block `0x10688120`, no frame).

### GPU retirement checkpoint

`99a1f93` adds an explicit native command-processor retirement barrier before
the frontend releases shared mappings, including contexts with no VideoOut.
[Real queued-work tests and instrumentation limits](../graphics/README.md#retiring-queued-gpu-work-before-mapping-teardown).
Checkpoint `8d8e27dad08a4f7b08069a4c625895d5151ab9dca3a2a3b830114e320ffe0e69`
has 1,210 entries; both initializers, 17 joined workers, three buffers and strict
renderer cleanup pass. The independent strict replay stops at executable gap
`0x10004730`, taking 356.609 ms; concurrency may expose another valid gap.
The game still has zero shader-adapter calls and no frame. Both mobile renderer,
game and boundary SDK archives are refreshed; physical apps are unchanged.
Private evidence is in `gpu-retirement-qualified/`, `ios-guest-session-retirement/`
and `ios-game-uikit-next/checkpoint-retirement/`. Further native discovery uses
the existing checkpoint-stop marker and the unchanged CPU backend/cache.

### Submission adapters and the 1,222-entry checkpoint

`0977a20` adds four typed batch/flip submission ABIs and SubmitDone, with checked
descriptor/command/label/stack ranges and HLE leases released for native waits.
[Native queue/presentation checks and scope](../graphics/README.md#typed-guest-submissions-and-diagnostic-flips).
The interface change is carried by matching game/frontend SDK archives; both
SDK builds pass. Eight synthetic flip buffers are observed by the native presenter
in the 245-check ASan/UBSan fixture. The actual guest has not submitted one yet.

Checkpoint `68cead5ee8011394c250a2e9c00434b087a07d711fe64195dcda8893fa1d15e9`
has 1,222 entries. Strict native startup completes both initializers, starts/
joins 17 workers, registers three buffers and stops at executable gap
`0x1075d142` in 380.112 ms, with no memory fault or game submission/frame.
Bridge 109, shader 305, separate boundary ASan lifecycle/five input negatives,
4,336 service checks and four worker cleanup fixtures pass. The separate boundary
normal path still reaches an earlier gap before child CPUs start. Instrumentation
limits remain explicit in each report. Physical apps/saves are unchanged.

### Presentation synchronization checkpoint

`51fc298` fixes DWORD alignment of serialized WriteData packets without changing
their bytes. `a2ef8e2` adds checked flip-wait packets and guarded native vblank
waiting, plus the backend performance-capability query. The imported synchronization
path uses VideoOutWaitVblank; no direct WaitEqueue import appears in this image.
[Native synchronization and layout evidence](../graphics/README.md#dword-packet-alignment-and-guarded-presentation-synchronization).

Qualified checkpoint `1a6a2ecae21120fae8590f8573ed63e9b6ff5682b04943febde523f60d8fc19d`
has 1,243 entries. Strict native startup again completes both initializers,
joins 17 workers, registers three buffers and releases mappings/queues. It stops
at executable CPU gap `0x106bedd0` in 612.855 ms, with zero game vblank/flip-wait/
submission calls or frames. Concurrent replay timing/path remains diagnostic.
The 99-check presentation fixture uses two fixed diagnostic buffers; actual game
use remains unverified. Both SDK renderer/game/boundary archives compile with
matching layouts and manifests. Source/state/SDK metadata is preserved in
`ios-game-uikit-next/checkpoint-presentation/`; physical apps/saves are unchanged.

### Faster preparation for executed-path discovery

`4e47e6f` caches small, in-process trace metadata and verifies the actual generated
guarded file on every hit. Changed raw IR, missing/tampered output and changed
helper/image modes regenerate through the existing guard transform. Previously
narrowed Remill-error blocks are reused only by their exact resulting raw hash
within the same pinned-image/lifter process; x87 remains an explicit unsupported
gap. No guest instruction, checkpoint or runtime/GPU behavior is removed.

The 18-case invalidation contract passes. A full 1,243-file audit matches the
legacy unsupported flags, definitions/declarations, guarded hashes and inserted
checkpoint counts. A six-attempt real native discovery run grows to 1,288
entries: preparation falls from 30.228 seconds cold to 0.787–1.066 seconds
warm, with one new miss per attempt and all 21 previously narrowed files reused.
These are preparation timings, not game FPS or whole-build timings. The final
trace header and unresolved thunks are byte-identical to the previous algorithm,
all compiled-object guard stamps match, and all 1,267 prior raw traces remain
unchanged. The guard-transform source itself is unchanged.

Checkpoint `ebe8af74a0bf70b7cd511736b30d97e5979d40368a7cba221c1ee220190f30be`
has 1,288 entries. Strict native replay completes both initializers, starts/joins
17 workers, registers three buffers and releases queues/mappings. It stops at
`0x106c8380` in 389.626 ms, with seven initialization GPU calls and zero actual
game Gnm submissions, vblank/flip-wait calls or frames. Bridge 109 and separate
boundary ASan lifecycle/input-negative checks pass. Both SDK game/boundary
archives compile; physical apps and saves are unchanged.

Evidence is under `discovery-profile/{cache-analysis-contract,full-analysis-audit,
final-corpus,native-gpu,guest-session-address}/`, with frozen state/headers/SDK
metadata in `ios-game-uikit-next/checkpoint-trace-cache/`. Native CPU discovery
continues toward the first actual game draw.

### Static candidates alongside executed demands

`catalog-traces.py` reads a frozen checkpoint plus the exact hashed prepared
image and pointer ledger. It inventories explicit unresolved-call declarations
and aligned executable destinations in checked relocation fields. It does not
scan arbitrary code bytes for guessed entries. Every output is labeled a static
candidate, with reachability/execution unverified. On the current private image,
the frozen 1,288-entry checkpoint supplies 22,168 unique candidates: 107 explicit
call references and 22,064 relocation destinations, with three in both sets.

```sh
python3 ios/game/catalog-traces.py --root "$ROOT" \
    --checkpoint "$ROOT/checkpoint-trace-cache" --output "$WORK/static-catalog"
# Optional extra compilation per discovery attempt; default remains zero:
python3 ios/game/advance.py --root "$ROOT" --remill "$BACKEND" \
    --rounds 64 --window-bytes 65536 --prefetch 12 --identity \
    --gpu-build "$NATIVE_GPU" --gpu-driver-manifest "$DRIVER_MANIFEST" \
    --stop-file "$ROOT/stop-after-checkpoint"
```

Mandatory parent/worker missing PCs retain their original checked selection and
lift path, including unaligned executed addresses. Optional candidates are ranked
by distance from those PCs, staged separately and admitted only when their raw
IR has the exact expected definition and no extended-precision/Remill-error
path. Rejected candidates remain recorded; an actual later demand is still
authoritative and is never skipped because of a speculative rejection.
Accepted raw IR goes through the same guard insertion, compilation and exact-PC
dispatch as demanded code. This precompiles more code without relaxing runtime
checks or claiming that every compiled function executed.

`static-traces.json` records input identity, each candidate's provenance,
accept/reject disposition and accepted raw hash. The accepted staged file is
retained as a verified recovery source. The ledger is published before atomic
promotion; a new process recovers a missing promotion only after checking its
hash, and rejects changed staged/promoted IR or different image/backend inputs.
The completed build state includes the static ledger in its corpus hash. As
before, a final prefetched raw file need not be part of the last compiled
checkpoint; SDK builders use only the frozen state's exact IR inventory.

The owned candidate matrix passes 39 checks, including invalid image/pointer
hashes and bounds, sorted/duplicate pointer offsets, malformed thunk source,
unaligned actual/direct targets and separation of known/demanded/speculative PCs.
The independent executed-target matrix still passes 22 checks. Actual game
frames remain a separate acceptance gate.

The first four-attempt native trial records 46 accepted and two skipped static
candidates, growing the compiled corpus from 1,362 to 1,399 entries. A new
process restores one deliberately missing pending promotion from its checked
staged file, then an existing stop marker ends exactly one completed attempt at
1,412 entries. All 1,399 prior raw hashes are unchanged, all 46 accepted static
files are in the frozen inventory, and the completed static ledger matches it.
The corpus is `7ed52c1a666bc906be8a1399b1a92f9063c17f6447b235df873ee0e7c317eb31`.

Strict native replay takes 557.880 ms, completes both initializers, starts/joins
17 workers, registers three buffers and stops at `0x106c0f6e`. Bridge 109 and
boundary ASan bridge/normal/pre-stop/live-stop plus five image negatives pass.
Both SDK game and separately platform-audited guest-boundary archives compile
with this same corpus. Eight separate catalog
input tests accept the frozen input and reject changed base/size/segments,
pointer hash/count or generated header/thunk hash, preserving original inputs.
Private evidence: `static-prefetch-qualified/`,
`ios-game-uikit-next/checkpoint-static-prefetch/`. Actual game Gnm submissions
and frames remain zero; compiled-entry growth alone is not gameplay coverage.

### Verified function bounds and retained error branches

`build-function-bounds.py` reuses the source tree's plaintext SELF parser and
an attributed copy of the OpenCrown unwind reader at `d6f8110d`. It validates
CFI record framing, CIE/FDE encoding/bounds, the sorted index and executable
function ranges. Original frame/index bytes must match the immutable prepared
image for eboot, libc and Fios2. This yields 30,612 / 2,084 / 717 ranges,
33,413 total; these describe function bounds, not executed functions or complete
control-flow coverage. CFI programs and LSDA/personality behavior are not
interpreted. Owned reader tests pass 26 checks; range/input/lookup tests pass 28.

```sh
python3 ios/game/build-function-bounds.py --root "$ROOT" --output "$WORK/function-bounds"
# Add to an ordinary advance.py invocation:
# --function-bounds "$WORK/function-bounds/bounds.json" --retain-error-paths
```

`--function-bounds` clips a newly requested lift to its containing validated
range. Unindexed assembly/leaves/interior targets retain the existing segment
and window fallback. The consumer verifies the image/layout, reader/builder,
original module hashes and sorted nonoverlapping executable ranges, and discovery
rechecks the manifest/module inputs before publication. Existing translated IR
is retained; this option does not relabel or regenerate old code. Both options
are explicit; the previous default behavior remains available.

A containing function can still have a valid unexecuted trap/error branch.
Bounds alone do not remove that branch: two private trial lifts at
`0x1000473c` and `0x106b50f0` still contain Remill error calls when bounded to
1,440 and 460 bytes. `--retain-error-paths` keeps compilable error branches
instead of shrinking the entire trace because one exists. Executing the branch
still calls the original checked runtime stop; it never returns success or
turns an unsupported instruction into a no-op. FP80 remains a separate explicit
gap, and actual later demand failures remain authoritative.

A real lifted owned x86 fixture has a normal return branch and a UD2 branch.
Native arm64, ASan and UBSan runs prove that the normal branch returns its
expected value without changing input or stack bytes and that the retained trap exits
through the enclosing guest checkpoint without returning to guest code. The
backend reports the post-UD2 PC (`start+12` for this fixture), so this is not
an exact instruction-start PC claim. The sanitizer build uses one LLVM toolchain
for lifted objects and the C++ runtime/harness; mixing LLVM 23 ASan objects with
Apple's older runtime fails to link and is not counted as a successful check.

The static ledger records the admission policy. Previously rejected speculative
error branches can be retried under the explicit retained-error option; accepted
raw hashes remain fixed, and FP80 candidates are still rejected. The completed
corpus hash includes the translation policy and function-bounds manifest.
Private evidence is in `unwind-bounds-qualified/`; source fixtures contain no
game-derived code.

The first eight real native attempts with both options grow from 1,909 to
2,000 compiled entries; a checkpoint-only flush reaches 2,013. All 1,909 prior
raw hashes are unchanged. The corpus contains 599 admitted static candidates
and 21 recorded speculative rejections; individual candidate execution remains
unverified. Strict native replay completes both initializers, starts/joins
17 workers, registers three buffers and stops at `0x107d4870` in 386.608 ms.
Bridge 109 and separate boundary ASan lifecycle/five input-negative checks pass.
The boundary normal path still has no child workers. Both mobile game and separately platform-audited guest-boundary SDK
archives compile at corpus
`52abc4684c1cd5908243b1980ed20b0bf9fca0bf5b753fa763ef06358dc7c30f`.
Actual guest Gnm submission and presented-frame counts remain zero.
Frozen state/headers/ledger: `ios-game-uikit-next/checkpoint-function-bounds/`.

### OpenCrown reuse and the 4,022-entry checkpoint

OpenCrown revision `68cb2a7b` supplies the MOVMSKPS/PD patch used by this
backend. Odin's original routine containing the stopped VMOVMSKPS returns the
same bits as Rosetta on 1,728 finite input triples. The adapted instruction
matrix passes 38,400 comparisons. Discovery subsequently reached the actual
`sceCommonDialogIsUsed` import. The idle query now follows OpenCrown's zero
result. This frontend stops on dialog Open until a presenter is available.

At 4,022 entries, the native GPU replay passes that query and stops in a child
at missing `0x10743600`. Both initializers complete, all 18 workers join,
three buffers register and teardown releases them. Game submissions and frames
remain zero. ASan covers 4,340 imported-service checks and four worker/exception
cleanup checks at corpus `cf0c090c`. Private evidence is in
`common-dialog-qualified/` and `ios-movemask-common-dialog-check.log`.

Discovery now accepts `--jobs 1..32`, default four, for independent seed lifts
and guarded object compilation. Ordered results preserve archive order. Each
compiler writes a temporary object before publishing it and its cache stamp.
The shared seed decoder is built before starting concurrent lifts. Serial and
four-worker four-entry seed runs produced identical trace/object hashes and the
same first missing block. A concurrent provider edit was rejected before a
mixed checkpoint could publish. Evidence is in `parallel-discovery/`.

Check the OpenCrown sources and tests before each subsequent milestone as
recorded in the main port plan.

### Rounding semantics reused from OpenCrown

After the common-dialog query, the native 4,061-entry run stopped on VROUNDSS
at `0x10744905` in a child. `remill-sse41-rounding.patch` takes OpenCrown's
implementation at `68cb2a7b` unchanged and rebases its insertion context onto
Odin's patch sequence. The backend audit covers the complete resulting diff.

The original OpenCrown `ios/cpu/review/rounding.py` ran with this backend and
its own reference runtime. It passed 360 routines, 1,536 vectors, 13,824,000
native comparisons, 2,560,000 suppression-invariance checks and two unmasked
fault-before-destination-write cases. Rosetta forces MXCSR masks, so the latter
cases are AOT properties. Architectural guest #XM delivery remains open.
Odin's original eboot/libc/Fios2 fixtures separately pass all 414 comparisons
and compile for both SDKs. Evidence is in `rounding-reuse/oracle/review.json`
and `ios-rounding-cpu-sdk.log`.

The fresh discovery tree is `ios-game-rounding/`, using
`cpu-backends-rounding/` and `native-gpu-rounding-arm64/`. Every seed PC
is lifted again, retaining the original unwind bounds and runtime guards.
Check the producer before changing its inputs or restarting discovery.

### Floating-point defaults and the first real initialization packet

The first replay with the rounding implementation hit its unmasked exception
guard at the same VROUNDSS. The primary entry and module initializers had used
zero-initialized MXCSR and x87 control state. Following OpenCrown's startup
defaults, those entry states now use MXCSR `0x1f80` and x87 control `0x037f`.
New guest workers snapshot their creator's MXCSR and x87 control word before
starting. Explicit guest changes to those controls carry into the child.

With those defaults, the original 4,061-entry guest passes the stopped
VROUNDSS and stops at another missing translated block. The actual native
frontend records its first Gnm initialization call writing 256 words, one
flip-wait packet and one vblank wait. All 18 workers join and all three
registered buffers release. These are startup packets; no game command batch
or presented game frame is recorded. ASan and TSan each pass 4,342 imported
service cases and four worker/exception cleanup cases. Evidence is
`ios-rounding-fenv-check.log`, `ios-game-rounding/attempt-00001.log` and
`rounding-fenv-qualified/{address,thread}/verification.json`.

## Bounded startup input — 2026-11-15

`OSL_INPUT_SCRIPT=/absolute/private/input.txt` loads one immutable input script
for an explicitly scripted startup run. The format follows OpenCrown `9ca8945c`:
`frame buttons`, comments after `#`, names joined by `+` or `|`, and `0` for
release. Events persist until replaced; unsorted events are sorted stably and
later duplicate-frame lines win. Numeric frames/masks are checked; unknown
button bits, malformed lines, symlinks/nonregular files and files over 1 MiB
reject. Empty files mean a connected idle test pad. No script remains the
ordinary GameController path. A connected primary pad takes precedence over
the scripted fallback; test input never replaces that pad's values.

The optional renderer clock counts completed presentations from the session's
baseline, not CPU speed or wall time. Script-only left-stick bits become axes
and never reach guest button masks. Triggers and console pad conversion use the
existing MobilePad implementation. The report includes exact input SHA-256,
event/sample counts, and separate fallback read/active-read counts; controller
information queries do not count as delivered button reads. `advance.py`
fingerprints the file and checks that the guest confirms the same bytes.
A script without a renderer clock rejects before guest execution.

On the UIKit probe, a relative `OSL_INPUT_SCRIPT` path is under Documents;
parent traversal rejects and the prior environment is restored at teardown.
Copy the script through the app container, then launch with e.g.
`OSL_INPUT_SCRIPT=Input/accept-notice.txt`. This developer harness is separate
from production touch controls and hands-on controller acceptance.

The adapter passes 130 checks normally and under ASan/UBSan, both SDK fixture
compilations, and the unchanged 38 pad checks. Six real native bootstrap cases
cover malformed/oversize/symlink/missing files, empty and unscripted runs;
headless clock rejection and all 4,342 actual HLE service checks also pass.
Evidence is `scripted-pad-contracts-final/`, `scripted-pad-regression-final/`
and `scripted-startup/bootstrap-cases/` at the external build root.

The actual guest receives a two-frame Cross press at frame 1,100: five active
pad reads are delivered, with the exact script hash confirmed. It advances past
the verified no-save notice and reaches the explicit `sceSaveDataDialogOpen`
presenter gap (mode 2, display type 1) after 85.2 seconds, without a memory
fault and with all 23 workers joined/cleaned up. The disposable profile stays
empty. Evidence is `scripted-startup/native-verification.json`. This is input
progress, not a completed save dialog or playable-game claim. The next adapter
must marshal that dialog and present its actual choices; auto-accepting it
without a presenter does not satisfy this milestone.

The same immutable script is now verified on the physical iPad mini A20 Pro.
Signed `6608d05`, corpus `628467a7`, launch 21 delivers two active Cross reads
and stops at the same actual mode-2/save dialog after 41.59 seconds and 1,119
presentations. CPU 414, memory 19, GPU readback 64 and renderer cleanup pass;
all 23 workers join without a memory fault. The frame-1,000 PNG is byte-for-byte
the previously verified no-save notice, before the press at frame 1,100.
It does not depict the next dialog. The profile remains three empty directories,
and exact owned PID 44104 is stopped and absent. Both SDK game/boundary archives
compile. Evidence is `scripted-input-ipad/device/verification.json`. The iPhone
was not contacted; production Mac and player saves are untouched.

## Save-data user messages

The AOT runner now marshals mode-2 save messages into a host-owned string and
an explicit presenter. The guest's title/display type, error style, OK/Yes-No/
None/OK-Cancel buttons and back option are preserved. Guest parameter, message
and result pointers are checked; malformed headers, oversized strings, aliasing
output ranges and invalid nested destinations cannot partially write a result.
The result preserves caller pointer fields, reserved bytes and opaque user data.
Save items/lists, system/error and progress modes remain explicit gaps; no item
selection or metadata is silently discarded as success.

The presenter reuses Odin's overlay, with wrapped message text and clickable
buttons. Controller/script input is polled by the guest worker after releasing
guest state/memory leases; the rendering thread never calls the pad provider.
An opening held button must release before confirmation. NONE remains pending
until Close. Input is held neutral for the game while the OS message is open;
closing, terminating and session cleanup retire the UI. Touch events are
normalized to the rendered viewport, secondary fingers are ignored, and a
cancelled touch releases outside the button. Physical touch acceptance remains
separate from injected-event checks.

`ios/services/test-save-dialog.py` passes 388 checks normally and under
ASan/UBSan, plus both SDK fixture compilations. The actual native renderer API
passes 69 checks across two initialization/shutdown cycles, including held-input
suppression, deliberate Yes/No/cancel, disabled Back, NONE and injected touch
transitions. These checks do not claim pixel or physical-touch verification.
The unchanged 4,342 HLE and 38 pad regression cases also pass. Evidence:
`save-message-contracts-final/`, `save-message-presenter-check/` and
`save-message-startup/{service-check,pad-regression}.log` under the build root.

Actual native attempt 11 opens a pending Yes/No message: **“Create new save
data? Save data is needed in order to begin.”** It records one open and zero
completions, reaches 120 seconds / 1,828 presentations, joins all 23 workers and
passes cleanup without a memory fault. This verifies the real marshal/open/
pending path, not visible dialog pixels or completed save creation. Its exact
input remains the prior two-frame Cross press at frame 1,100. The iPad renderer
archive and signed-driver consumer link compile; installation of this adapter
is a subsequent checkpoint. The installed iPad evidence above still names
`6608d05`.

ABI/state reference: [pinned shadPS4 dialog headers](https://github.com/shadps4-emu/shadPS4/blob/4e22b3ba29d0133ef97477e5740df0c85c61c2be/src/core/libraries/save_data/dialog/savedatadialog_ui.h),
cross-checked with Odin's desktop marshaling. OpenCrown's generic headless
auto-success behavior is intentionally not used for this adapter.

The adapter is now installed as signed clean `30b626b` on the physical iPad
mini A20 Pro, corpus `54b1a9ba`, still 9,585 entries. Launch 22 leaves that
Yes/No dialog pending through 90 seconds / 3,990 presentations. Launch 23 adds
a deliberate second Cross press at frame 1,500: it completes the dialog with
button 1 / result 0 and stops at the newly reached translated-code gap
`0x107d52b0`, after 48.15 seconds / 1,500 presentations. The native confirmation
reaches that same gap after 103.51 seconds. These are actual guest transitions;
the message is not auto-accepted by the adapter.

Both physical runs pass CPU 414, memory 19, GPU readback 64 and complete
renderer/resource cleanup, and join all 23 workers without a memory fault.
The profile remains three empty directories: no new in-game save has been
created or reloaded. Exact owned PIDs 44473 and 44503 are stopped and absent.
The pending run's stop is the bounded mutex-wait deadline; it is not a new
service gap. Both SDK renderer/game/boundary archives compile; the device
renderer consumer links its signed ICD. No Simulator driver-execution claim.
Evidence: `save-message-ipad/{device,device-confirm}/verification.json`,
`save-message-startup/{pending-verification,confirm/verification}.json`.

Native discovery is continuing from the reached PC using the unchanged
19-patch backend and original unwind bounds. Dialog pixel verification and
physical touches remain open; the existing game-frame PNG captures precede the
host overlay. The iPhone remains excluded, and the production Mac/player saves
are untouched.

### Bounded prefetch from verified unwind entries — 2026-11-15

Fourteen of fifteen actual missing-block stops in native attempts 17–36 are
exact function entries in the independently verified original unwind atlas.
OpenCrown `9ca8945c` supplied the unwind reader/bounds qualification and its
nearby-callee prefetch was rechecked. Odin now optionally includes those known
entries in its static candidate catalog, alongside explicit call declarations
and checked relocation fields. It never scans arbitrary bytes for function
starts. This does not establish reachability or execution of candidate code.

Use `--prefetch-unwind --function-bounds VERIFIED_BOUNDS --prefetch 1024` with
`advance.py`. The flag requires positive prefetch and the existing strict
image/module/reader-verified `FunctionBounds` adapter. Extra static batches are
capped at 4,096 with this flag; the default mode retains its 256 cap. The
nearest candidate selection keeps actual executed demands mandatory and
separate, excludes known code, and preserves deterministic tie ordering.
Each entry records its verified-unwind provenance and exact bounds-manifest
hash. Existing operand/error-path analysis, function/window clipping, complete
instruction decoding, checked-memory instrumentation, native compilation and
atomic ledger/checkpoint publication remain in force. Unsupported static
candidates are logged/skipped; an actual unsupported demand still fails.

59 candidate checks cover provenance merging, unaligned atlas entries, large
bounded batches, malformed/overlapping/out-of-executable ranges and missing/
malformed manifest hashes, plus the previous image/pointer/declaration rejects.
Four CLI cases reject invalid flag/cap combinations before any build. Original
function-bounds fixtures pass 28 checks; discovery/stop tests pass 22 target and
7 deadline cases. Evidence: `unwind-prefetch/` at the external build root.
Runtime/backend/State ABI are unchanged. Only real replay can establish the
next title/gameplay transition; static entry counts alone cannot.

### Parallel static lifting — 2026-11-15

`advance.py` uses the existing `--jobs` bound for independent static lifters
as well as compilation. Each lifter owns a distinct staging and log file.
The decoder is built before worker startup. Analysis, ordered ledger updates,
raw-trace promotion and their atomic writes stay on the coordinator thread.
Unexpected errors cancel queued futures and join active lifters before failure
returns; executed demands still lift separately and cannot be skipped as static
failures. `STATIC_PREFETCH` reports batch counts, worker bound and elapsed staging
time without asserting runtime coverage.

`test-prefetch-parallel.py` runs the real original-image pipeline from a
hash-verified first-module trace in isolated external fixtures, using one then
four workers and two 64-candidate batches each. Exact raw trace hashes, ordered
records, ledgers, checkpoint IR inputs and generated headers match. The same
five unsupported static candidates remain skipped. Process observations prove
maximum concurrency one/four, within the requested bounds. In one comparison,
64-candidate staging takes 4.18/4.95 seconds with one worker versus 1.27/1.57
with four. This is a selected staging comparison, not full-game boot or FPS.
Evidence: `prefetch-parallel/verification.json` and `final-check/verification.json`
at the external build root. An initial empty-cache test fixture hit the existing
zero-trace-table build limitation; the final fixture uses a real verified module
entry, matching established-cache discovery. No runtime/backend/State ABI change.

```sh
python3 ios/game/test-prefetch-parallel.py --root "$ESTABLISHED_CACHE" \
  --remill "$BACKEND" --function-bounds "$VERIFIED_BOUNDS" \
  --output "$NEW_EXTERNAL_FIXTURE"
```

### Complete validated seed catalog — 2026-11-15

Offline staging combines all 33,413 verified unwind entries with 7,530 targets
from 723 strict, original-byte-validated switch tables. This 40,943-entry catalog
contains metadata-qualified entries, not every possible executed block. Original
module/image/ELF/table/owner bytes, signed table offsets, executable ranges,
no-return inputs, reader outputs and the exact lifter/semantics identity are
revalidated before publication. The generic control-flow analysis comes from
OpenCrown; Odin's original inputs and qualification remain separate.

Staging prepares 19,331 pending candidates: 19,185 accepted, 146 unsupported
static candidates skipped. Meanwhile demand discovery advances the live cache.
Publication compares against that current cache, preserves 712 byte-identical
duplicates and four existing skips, and adds 18,473 accepted/142 skipped records.
All prior raw hashes remain exact. Recovery copies are written first, one atomic
merged ledger second, then raw files are promoted and audited. The raw cache now
contains 42,105 entries, including previously discovered blocks outside this
catalog. No existing raw trace is overwritten. The offline manifest stays immutable.

A raw-IR inventory against the new runtime finds no missing Remill bridges in
that available set. Linking and actual replay remain independent verification.
Discovery now uses `--prefetch 0` with the same verified function bounds, retaining
actual missing-block discovery and explicit unsupported-demand failures. Static
counts do not establish title/gameplay coverage. Private evidence:
`qualified-seeds/{inputs,prepared,published}.json`, `publish.py`, and
`runtime-bridges/bridge-inventory.json` at the external build root. No game bytes,
translated IR or captured player data are committed to the source repository.

### Qualified switch joins after longer setup — 2026-11-15

The bounded table-base extension in `6bd9670` resolves the previously unknown
primary dispatcher in the owner containing actual stop `0x105c04b1`. With the
same original image, ELF, validated unwind atlas and separately verified
no-return declaration, strict analysis grows from 723 tables / 7,530 targets to
733 / 7,690, retaining every previous target. The newly complete owner contains
ten tables, including the actual missing case. Bounds/guard dominance, original
table bytes, executable destinations, decoded case reachability and nonoverlap
still pass. An initial analysis omitted the explicit no-return CLI argument;
its outputs are retained separately and are not used for qualified seeding.

A separate offline cohort also gathers ordinary relative branch destinations
inside complete validated owners. Source and target must both be independently
decoded instruction boundaries in the same nonoverlapping unwind range; the
original relative encoding is reconstructed and checked against prepared bytes.
Unfamiliar encodings are skipped. This includes the observed direct-branch join
`0x105d3b29`, which is not itself a switch slot. It does not linearly scan
arbitrary bytes for instruction starts or infer destinations from sampled PCs.

The cohort contains 18,885 unique strict-switch/direct-branch targets, with
11,336 absent from the frozen 42,107-PC seed. Staging accepts 11,201 and
explicitly skips 135 static FP80 candidates. Recovery-first publication audits
all immutable inputs and every previous/promoted raw hash, growing the cache to
53,308 entries; four later actual demands grow the compiled checkpoint to 53,312.
The active twenty-patch backend is used throughout, with no old backend IR copied.
The frozen checkpoint is installed and independently replayed on the iPad.
Private evidence: `long-base-setup/{verification.json,catalog-noreturn,seeds}`
and `atomic-ipad-expanded/`. Static staging counts do not prove every entry runs.
The prior nineteen-patch code/cache remains preserved.

### Anchored owner inventory and bounded incomplete candidates — 2026-11-15

The unchanged OpenCrown-derived CFG validator is also exercised across all
30,612 original eboot unwind owners, rather than only recognized-table owners.
Six actual missing-PC reports add explicitly observed component roots inside
their original bounds. Source/ELF/prepared text, no-return declarations, decoded
instruction intervals and relative encodings are revalidated. This is read-only
analysis; it emits no IR and changes no game cache or runtime branch policy.

The inventory finds 12,242 closed known CFGs and 18,370 incomplete owners;
none have decoder failures or instruction overlaps, including across observed
components. The analysis includes 733 strict tables and 391 locally recognized
incomplete-owner tables. Those local tables retain their unknown-edge/bypass
limitations; a closed decoded component is not a new strict index/guard proof.
There are 128,293 unique ordinary relative-branch destinations, 116,437 absent
from the current cache. These counts do not establish whole-game reachability,
complete code coverage or execution. Evidence: `all-owner-cfg/`.

A separate bounded cohort uses the existing explicit incomplete-owner prefetch
tier plus 4,096 nearby anchored branch candidates. It selects 7,027 unique missing
PCs (hard cap 8,192), including mandatory actual demand `0x105b4083`, for offline
lifting with the same backend. Per-target metadata preserves incomplete CFGs and
all unknown exits. Original bytes and checked memory remain in force; no index
is clamped, no jump is restricted, and an unsupported actual demand still fails.
This cohort is prepared separately under `nearby-owner-prefetch/`: 6,995 accepts
and 32 explicit static FP80 skips. Recovery/raw/ledger/input hashes and the
intrinsic inventory pass before publication, growing availability to 60,307.
It is not part of the installed app or an execution claim.

An additional actual stop `0x10219217` identifies its original relative switch
table in owner `0x10219190..0x10219458`. The unchanged strict validator rejects
guard/index equivalence across a conditional call. Reading original table bytes
identifies ten slots / nine unique destinations; eight absent siblings are lifted
and published with the same backend, growing availability to 60,315. Their ABI/
index proof remains **unproven**. Observing one case does not prove index bounds;
these candidates do not relax the strict validator, clamp indices or change
runtime jump policy. Evidence: `observed-abi-switch/`. The next native replay,
not these availability counts, determines whether startup actually advances.

### Checked native bulk libc adapters — 2026-11-15

The OpenCrown bulk-libc/RET contract is adapted for Odin's removable mappings,
external physical aliases, recursive memory scope and longjmp fault recovery.
The exact prepared image selects only original libc exports `memcpy` at
`0x10cd0ba0`, `memmove` at `0x10cd0c40` and existing `memset` at `0x10cd0ce0`.
`verify-libc-exports.py` independently checks their NIDs, one defined executable
export each, original byte spans, pinned module hash and prepared-image equality.
Other libc routines, including `strcmp` and `strlen`, remain translated.

The full destination/source and RET slot are checked before writing. The helper
holds the existing memory scope once; zero count touches neither byte argument.
It reads the saved return value after copying/filling, including an overlapping
return slot. Successful return changes only RAX/RIP/RSP; fill uses RSI's low byte.
Failed validation leaves bytes and return State untouched. Cancellation after a
committed bulk operation can leave bytes committed with RET pending; this is
cooperative cancellation, not rollback. Overlapping memcpy uses deterministic
memmove bytes, outside the standard memcpy overlap contract. Per-session reports
retain fill counters and add copy/move call and byte counters. Raw guest IR and
backend semantics remain unchanged; only the exact export's guarded wrapper is
substituted, with the existing entry budget/cancellation guard.

`test-libc-bulk.py` imports the production IR instrumenter and checked helper,
compiles the actual generated LLVM shims and verifies 686,279 checks on arm64,
Rosetta x86-64, arm64 ASan/UBSan and TSan. Its thin fixture exports mirror the
session wrappers; the complete runner/service integration is verified separately.
It covers all alignments modulo 64, size edges, both overlap directions, a 16 MiB
copy, whole-buffer guards, every other State byte, RET-slot overlap, zero/invalid
arguments, protected and exact-end mappings, physical alias overlap, cancellation
before and after commit, atomic-region fault cleanup and eight concurrent CPUs.
Both mobile SDK helper/IR fixture archives compile; these are not physical-device
or gameplay performance results. Sanitized IR and C++ use the same Apple
compiler-rt ABI; normal shims use the production Homebrew LLVM compiler.
Private evidence: `native-libc-bulk/{exports.json,final-checks/verification.json}`.

The complete candidate runner links against the frozen, ABI-matching 60,317-entry
object corpus and passes 4,371 service checks plus all four injected worker/host
cleanup cases. This includes actual copy/move wrapper State/bytes and per-session
counter checks. The isolated normal native comparison then executes the actual
game with exactly the same raw PC hashes, backend, CPU State/runtime, renderer,
driver, input script and copied save bytes. All 43 workers join, cleanup passes,
and it reaches the same `0x105d0b0b` gap. Candidate `8346836e` takes 149.667 seconds
and presents 3,425 buffers; baseline `634f4cd5` takes 170.964 seconds / 2,911 buffers.
These are single host-startup observations, not sustained gameplay/FPS evidence.
The candidate executes 545,927 copy and 3,049 move calls over 447,897,985 bytes,
plus 104,095 fill calls over 1,255,747,974 bytes. A fresh frame-3,200 game PNG is
960×540 opaque black, independently decoded and visually inspected. Original
player save hashes and all three disposable existing-save hashes stay unchanged;
character-progress loading is not verified by retaining file hashes.

The first preparation process exits 1 without output or a new checkpoint, cause
unknown. A supervised unbuffered retry finishes normally; that completed run is
the accepted evidence, with raw/header/source/backend validation. After it ends,
41 further original-byte candidates from two actually reached owners are audited
and published separately, growing availability to 60,358. Their six incomplete
tables retain explicit proof limitations; no strict guard/index or runtime branch
policy changes. New native replay remains separate from these static counts.
Evidence: `native-libc-bulk/{comparison.json,snapshot-verification.json,
services/verification.json,next-owner-seeds/published.json}`. The original global-
lock baseline and production Mac/iPad versions stay preserved.

### Observed in-range table cases — 2026-11-15

Original-byte inspection explains the repeated `0x105d0b45` stop: owner
`0x105d0aa0..0x105d0bec` dispatches through a five-slot relative table using an
in-range `JBE` taken successor. The strict scanner expects a nearby unsigned
`JA` fallthrough guard and rejects this layout. This is not an ordinary call
continuation. Original table/file/prepared bytes, bounds and all five decoded
case destinations agree. The remaining three original cases are lifted and
recovery/raw/ledger/backend audited into a fresh discovery clone, growing static
availability from 60,358 to 60,361. The virtual tail jump at `0x105d0bd3` remains
unresolved; full guard/incoming CFG proof is not granted, no index is clamped,
and the unchanged strict scanner does not accept this owner. Only availability
is added; later guest execution still decides which case is used.

A read-only inventory of the same local layout recognizes 132 original patterns;
101 tables in 97 owners pass local decode/overlap/case-reachability checks.
There are 856 unique table targets, 846 absent from the frozen 60,358-PC baseline.
These are local candidates with explicitly unproved global guard/index/CFG
properties, not whole-game coverage or execution. Offline preparation accepts all
846 with zero skips; at a completed idle checkpoint, recovery/raw/ledger/backend
publication adds 843 and preserves three byte-identical duplicates. Every prior
raw hash remains unchanged, growing availability to 61,204. Actual replay remains
separate from publication. No original index or runtime branch is changed.
Evidence: `repeated-owner-analysis/{jbe-seeds,inrange-inventory,inrange-seeds}`
and `inrange-jbe-discovery/` at the external build root.

REA/Hopper's original-ELF session confirms target identity but finds no containing
procedure at this address; its individual-instruction operation is unavailable.
That negative/incomplete evidence is saved and the session is closed. The positive
code/table findings above come from the existing checked XED decoder and original
ELF/file bytes, without inferring a function name from the unavailable decompiler.


The restored-global 60,361-entry native replay actually passes the three recovered
cases and reaches `0x1066bf99` after 204.104 seconds / 3,647 presentations, all
43 workers joined, no memory fault and full cleanup. A fresh frame-3,200 readback
is opaque black; original and copied-save hashes match. That next actual demand
is independently present in the prepared wider JBE cohort. Evidence:
`inrange-jbe-discovery/checkpoint-60361/verification.json`. It is a startup
checkpoint, not a title/gameplay acceptance result.

### Observed loader owner and first-block windows — 2026-11-15

The wider JBE replay advances past `0x1066bf99` and reaches three actual gaps
in owner `0x1070b650..0x1070da14`: `0x1070b650`, `0x1070b6ab`, then
`0x1070b6b7`. The last is frozen at 61,206 entries / corpus `77cb802a`,
153.667 seconds / 3,414 presentations, 43 joins, no memory fault and verified
renderer cleanup. It stops before the requested frame 4,000; no new scene
capture is inferred. The iPad still runs the separately qualified 60,361-entry
checkpoint described above.

The existing OpenCrown-derived original CFG/relative-branch inventory and
incomplete-table checks select this observed owner. A first bounded cohort
publishes 73 entries; 114 broad windows contain unsupported x87 arithmetic and
remain unavailable. This does not establish that the executed first blocks use
x87. A second cohort applies the **same first-control-flow-boundary narrowing
already used for actual demand**, preserving the broad files. Its candidates
also include original conditional fallthroughs confirmed as decoded boundaries.
All 271 accept; 209 need narrowing, with no remaining FP80 operation in their
resulting prefixes. Independent XED decoding agrees with production Zydis on
all 209 prefix boundaries / 913 instructions, each ending at a control-flow
instruction. A genuine unsupported first block would remain a gap.

Recovery-first publication rechecks the original bytes, bounds, backend ABI,
record/raw/source hashes and existing ledger. It preserves every executable/raw
entry and only upgrades previously skipped records that have no raw consumer,
retaining the prior skipped-record checksum. Availability grows to 61,550.
These are static candidates, not full guard/CFG proof or execution coverage;
no original instruction, index or runtime branch is changed. The next native
startup/capture is separate acceptance evidence. Private reports are under
`repeated-owner-analysis/{loader-owner-seeds,loader-owner-narrow-seeds}/`,
including `narrow-qualification.json`, and
`inrange-jbe-discovery/checkpoint-61206/verification.json`.

The 61,550-entry cohort is now actually replayed: corpus `0d944f6e`,
165.229 seconds / 3,768 presentations, 43 joined workers, no fault and renderer
cleanup. The new demand is `0x1070b871`, the original continuation after the
call at `0x1070b86c`. Its fresh frame-2,600 PNG is independently decoded and
visually inspected as opaque black. Original and copied player-save hashes
match. Frozen evidence: `inrange-jbe-discovery/checkpoint-61550/verification.json`.
No visible-scene or gameplay acceptance follows from this startup progress.

The same decoded owner contains 188 original call-return boundaries, 156 still
absent from the executed corpus. Each has a decoded original call and an
independently validated next-instruction boundary; non-returning/unreachable
continuations are not invented. All 156 are lifted as single basic blocks,
accepted and recovery/raw/ledger/backend audited. Independent XED decoding
checks all 156 prefixes / 659 instructions against the production decoder.
Previous raw entries stay byte-identical and the original return behavior is
not restricted. Availability is 61,706; the new cohort is execution-unverified.
Reports: `repeated-owner-analysis/loader-owner-return-seeds/`, including
`return-qualification.json`. The next bounded replay remains a separate gate.

### Reusable observed-owner recovery — 2026-11-15

Actual replay of the 61,706-entry return cohort clears the prior owner and reaches
`0x106f92e5` in owner `0x106f90e0..0x106f9464`: corpus `9999cee8`, 162.054 seconds,
3,500 presentations, 43 joined workers, no memory fault and renderer cleanup.
Exact original/copied save hashes match. The fresh frame-2,600 PNG is decoded
and visually inspected as opaque black. Evidence is frozen under
`inrange-jbe-discovery/checkpoint-61706/`; this remains startup progress rather
than visible gameplay.

The private pipeline now packages the qualified producers in
`ps4/odin-ios-owner-batch.py`. It pins its templates and completed checkpoint,
keeps immutable generated helpers, and selects original owner branches,
conditional fallthroughs, decoded call returns and locally checked tables.
Every new entry is a single basic block; real unsupported first blocks stay
gaps. Full guard/CFG closure is not inferred, nor are runtime jumps restricted.
Preparation, independent XED qualification and publication are separate commands.
Publication requires the independent receipt and replays the original byte/
bounds/backend/raw/ledger qualification before touching the inactive cache.
It refuses a live consumer and altered generated helpers.

The first new-owner batch prepares, independently decodes and publishes all
40 entries, with zero skips and every previous raw hash preserved. Independent
checks cover 40 prefixes / 155 instructions. Availability grows to 61,746;
execution of that final cohort is pending. Evidence:
`observed-owner-batches/after-61706/` (inputs `1a652eba`, records `9ac3649a`),
and `observed-owner-batches/factory-checks/`. No device action is part of this
helper; the installed iPad remains the separate 60,361-entry engineering build.

### Local tables and the existing no-return policy — 2026-11-15

The 61,746-entry native replay reaches another original case, `0x106f930c`,
in the same owner: corpus `83293013`, 157.672 seconds / 3,607 presentations,
43 joins, no fault, renderer cleanup and unchanged original/copied save hashes.
Its fresh frame-2,600 image is decoded and visually inspected as opaque black;
`inrange-jbe-discovery/checkpoint-61746/` freezes the exact evidence.

That case was absent from the old primary CFG: the owner's indirect switch is
unresolved there. The earlier local JBE inventory recognizes its four-slot table
but rejects the owner after following a stack-failure call into table data and
encountering a decode failure. Re-running that inventory with the **existing
qualified ABI no-return rule** for `__stack_chk_fail` validates 125 tables in
121 owners, versus 101 tables previously. This qualification establishes the
import/relocation identity and ABI policy; the external original callee body is
absent. It does not grant full index/guard proof or alter runtime call behavior.
Original table/file/prepared bytes and instruction boundaries are still checked.

The owner helper now merges only that observed owner's locally validated
instruction boundaries. It adds the excluded table cases and their decoded
branches, conditional fallthroughs and call returns. All 20 new first-block
entries accept and independently decode, 83 instructions, including the actual
`0x106f930c` case and its following `0x106f931e` call return. Audited publication
preserves all previous raw entries; availability is 61,766, execution pending.
Reports: `repeated-owner-analysis/inrange-inventory-noreturn/` (inputs `491cdff4`,
tables `a6ceb8bf`) and `observed-owner-batches/after-61746/` (inputs `97a59da5`,
records `0edab459`).

The private pipeline includes its pinned source templates, requires assertions
for verification and starts helper Python with environment flag overrides
disabled. Default generation uses frozen checkpoint metadata only; positive
independent qualification and separate tampered-helper/live-consumer/optimized-
Python refusals are recorded in `observed-owner-batches/factory-checks-v3/`.
These checks do not substitute for native execution or physical gameplay.

Actual 61,766-entry replay passes that table owner and reaches `0x10736887`
in new owner `0x10736530..0x107368dc`: corpus `789d259f`, 159.424 seconds /
3,843 presentations, 43 joins, no fault, renderer cleanup and matching original/
copied save hashes. A fresh frame-2,600 image is still opaque black, decoded and
visually inspected. Exact evidence is frozen in
`inrange-jbe-discovery/checkpoint-61766/`.

The committed factory then prepares, independently qualifies and publishes 62
first-block entries in that next owner, 236 instructions, with zero skips and
previous raw hashes unchanged. This uses the bundled pinned templates and the
assertion-enabled helper path on a real new checkpoint. Static availability is
61,828; its actual replay is pending. Phase `observed-owner-batches/after-61766/`,
inputs `8135b271`, records `c2efecb5`. Recognizable mobile gameplay remains open.
