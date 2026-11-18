# Odin Sphere on Apple silicon

The reference target is a local offline Mac app running the CUSA05083 US base build under Rosetta. Fork baseline `d2a9d8a253dd17680c5349cae0e1735c67fe2455` is upstream v0.2.3. Keep `main` at the upstream baseline and develop on `codex/macos-port`. The initial host is an M5 Max running macOS 27.2.1. Other upstream profiles need their own verification.

## Current delivery status — 2026-11-17

| Area | Established | Remaining acceptance |
| --- | --- | --- |
| Mac loader/runtime | Real 307-import check, module startup, title and battle gameplay | Other profiles/hosts are unqualified |
| Mac graphics/performance | Selectable native 4K reaches the Metal drawable on the M5 Max. In eight capture-free minutes spanning a level-2 boss fight and field play, all 95 five-second windows averaged 59.8–60.0 fps. A separate 75 Hz boss test reached a median of 75.0 fps, with a slowest window of 68.4 fps and a worst frame of 106.7 ms. [Measurements](../macos/PERFORMANCE.md) | Occasional long frames and performance across later chapters |
| Mac audio/progress | Player heard clear audio and confirmed Refined progress survived quit/relaunch. Classic Data 00 was saved in the 1080p opening tutorial and cold-loaded into Gwendolyn’s Prelude Act 2 battlefield at 90/90 HP | Longer interruption/failure and device-change coverage |
| Mac app | Build `d3709f6` installed and cold-loaded Gwendolyn in the Act 3 battlefield at native 4K. The relocated app passes 307 imports, seven launcher pages, and a 4K-to-1080p switch with 300 frames at each setting. The installed app also reloads the Act 3 slot and plays at 4K with external build and dump disks inaccessible and outbound IP connections denied on the reference account. A 900-frame SDL dummy-audio run preserved the copied slot. The installed bundle passes a separate 600-frame render. The previous alpha also passed fresh-profile PKG installation and visible keyboard Play. A uniquely registered disposable clone with the identical production runner passes automated mouse Play to the visible save-check screen and native Quit; original/copied save hashes match. Its bootstrap forces isolated data/log roots and dummy audio | Separate OS account and long-session hitch investigation |
| Mac installer | Full x86-64 dependency rebuild, signed bundle, transactional update, verified app backup and byte-preserved existing saves | Public distribution signing and notarization |
| Mobile CPU | Original eboot/libc/Fios2 tests pass 414 comparisons on both physical devices. Reused OpenCrown MOVMSKPS/PD semantics passed 38,400 comparisons. Its ROUND patch passed 13,824,000 native comparisons and the separate suppression/fault checks in OpenCrown's reference harness with Odin's backend. | The native original-game replay passes VROUNDSS after fixing initial MXCSR/x87 controls and worker inheritance. Continue CPU/ABI qualification through combat. |
| Mobile startup | Physical iPad `e5cbb68`, corpus `f55967f7` (60,361 entries), twenty-patch backend and restored global lock. Cold launches 33/34 reach `0x1066bf99` after 131.392/149.200 seconds / 3,321/3,359 presentations; CPU 414, memory 19, GPU 64, 43 joins and cleanup pass. Saves survive update/quit/relaunch. Fresh game/display captures remain black; 148.864 seconds of PCM is silent. The stripe candidate is not adopted. The phone is explicitly excluded. | Recognizable title/town/combat, nonempty audio, physical touch/controller gameplay and sustained pacing. |
| Mobile storage and lifecycle | The guest creates three system-save files on iPad; their exact hashes survive quitting, an app update and cold read-only mounting. Native copied-player-save discovery reaches 42 joined workers and later missing CPU blocks. | Character-progress save/load, interruption recovery, and suspend/resume during gameplay. |
| Mobile engineering harness | Both SDK renderer/game/boundary archives compile. Native AOT background event pumping matches UIKit; 109 native bridge checks pass. The optional iPad UI-test runner compiles/signs but times out enabling automation before any test runs. | Physical touch acceptance, Simulator GPU execution and remaining UIKit appearance warnings. |

Hardware system-data persistence is distinct from loading character progress.
The latest iPad frame-1,300 cold-start game snapshot is black; the independently verified
prompt is a display snapshot from the earlier dialog run. The native 9,920-entry
checkpoint reaches the same missing block with an isolated copy of the working
Mac save; discovery continues beyond it. Candidate availability is not execution evidence.
The bounded native frontend now pumps events without background idling; its
startup comparison advances faster but ends at a different CPU guard, so this
is not a combat-FPS result. [Current evidence and limits](../ios/graphics/README.md).
The player made the iPad available for unattended testing and is taking the
iPhone away. Original player saves, production Mac app and iCloud are untouched.

## OpenCrown reference for upcoming work

At the start of each port milestone or newly observed failure, check the current
OpenCrown port and its regression tests before building an Odin implementation.
Record the revision, relevant source and Odin verification in the milestone log.
The imported nineteen-patch backend and mapped-cache comparison used [OpenCrown 9ca8945c](https://github.com/runty/OpenCrown-PC/tree/9ca8945c10e913260b1b3e22a6c680f0396e05e6).
For upcoming work also inspect the active mobile branch, currently
[ce065bb](https://github.com/runty/OpenCrown-PC/commit/ce065bbb9e58337d64eedc3e580becf94fb26ba0)
(`OpenCrown-town` / `codex/ios-town-performance`); the older main port checkout
remains frozen at `9ca8945c`. The newer MOVBE-store patch/differential tests
and read-only cursor feedback are available there for future Odin qualification. Backend changes still
need Odin qualification and a fresh re-lifted corpus. Do not replace a live
backend/cache in place.

| Odin milestone | OpenCrown implementation to check | Odin verification |
| --- | --- | --- |
| Missing instruction or translated block | `ios/remill-*.patch`, `ios/cpu/review`, `ios/cpu/fp80`, `ios/game/control-flow` and `unwind_atlas.py` | Reproduce the stopped PC, compare semantics with Rosetta, replay Odin’s actual modules. Use switch candidates when observed PCs or profiles justify precompiling them. |
| First frame and combat pacing | `ios/game/gnm-services.cpp`, `ios/render/capture-fixture.cpp`, `ios/game/vblank-clock-fixture.cpp`, `ios/host/routes` | Exercise Odin’s Vulkan-on-Metal command path, capture an actual guest image and time a repeatable Odin battle. The two ports have different rendering backends and shaders. |
| Audio, touch, controller and lifecycle | `ios/platform/{core.cpp,platform.mm,probe.cpp}`, `ios/app.mm`, `ios/game/scripted-input-fixture.cpp` | Check Odin button masks and axes, touch release and controller reconnect. Capture nonempty game PCM and verify background/foreground transitions with the game producing frames. |
| Saves and device packaging | `ios/storage/save-transactions.cpp`, `ios/storage/save-service-integration.cpp`, `ios/game/save-seed-test.py`, `ios/bootstrap.py` | Use an isolated Odin slot. Save progress, quit, cold-load on each device, compare progress and test interrupted writes. Preserve Odin’s bundle and save location. |

The OpenCrown unwind reader already supplies Odin’s 33,413 validated function
ranges. Its unmodified control-flow tools found 5,584 strict switch targets
across Odin's original three modules. All executable segments and the accepted
tables matched the relocated image. None of 40 distinct recently observed
missing PCs was in that set, so startup discovery continues from observed PCs.

Earlier bring-up checkpoints below are retained as history; the table above
is current. The first module-startup hardware result is explicitly a development build:
244 traces, two zero-return initializers, 397 dispatches, missing logical PC
274339111, no memory fault and 256 KiB flexible memory. The isolated-backend
371-trace corpus reaches 513 dispatches on Mac and has a signed device build,
but its locked-device launch failures are not counted as execution. See
[iOS runtime instructions](../ios/game/README.md). Continue independent work;
no unlock or player assistance is required for host engineering.

Later unattended work reaches a guarded 624-trace corpus and the same next
module-service gap on arm64 Mac and an isolated iPhone Simulator. Both
initializers return zero; 414 routine comparisons still pass. The full
arm64 iPhoneOS graphics driver and signed framework probe build, and its native
arm64 Mac submit/fence/readback check passes all 64 words. The Simulator SDK
omits the driver's required Metal 4 protocols; it supplies CPU/lifecycle
evidence only. [Graphics adapters and current limits](../ios/graphics/README.md).
Physical mobile rendering and full guest startup remain open.

The next host/clean Simulator checkpoint completes both initializers and 1,638 dispatches with
1,070,596,096 direct bytes allocated, then stops at `sceUserServiceInitialize`.
The sparse memory and checked libc adapters pass 96 memory and 88 service checks;
fixed-address and repeated ASLR runs agree. Clean `dcd85de` Simulator runs pass
414 comparisons, repeat the 664-trace result at launch counts 5 and 6, and
preserve their input container across app replacement.
These later results do not turn an installation on locked hardware into an
execution result. [Current startup scope](../ios/game/README.md).

Shared users (`90056b3`) retain production behavior and pass 58 native/Rosetta
checks; the Mac runner still resolves all 307 real imports. GameController
`89184da` adds five checked pad APIs, passes 38 backend and 121 service checks,
and repeats startup at 680 traces / 1,655 dispatches in the clean Simulator
(launch counts 7 and 8). Both initializers return zero, users/pad initialization
complete, and the next missing service is `sceSaveDataInitialize`. Its signed
iPhoneOS app/framework is staged; physical devices retain `dcd85de`. No physical
input/haptics or new hardware execution is claimed by those automatic checks.

Shared storage `f6f6aee` now passes cross-process native arm64/Rosetta and checked
guest fixtures, including metadata write/permission failures and read-only
mounts. Clean Simulator launches 9 and 10 reach 682 traces / 1,657 dispatches,
create their private roots and complete save initialization, then stop at
`sceNpSetNpTitleId`. The signed device app is staged; hardware retains `dcd85de`.
The same source is now installed on Mac after clean pinned build, relocated
bundle/seven-page/restart checks and an installed 600-frame run. The copied
existing save mounts read-only, its bytes remain intact, and all 302 checked
files in the original profile retain their hashes. Secret/signing files were
excluded from those checks. Normal launch logs a fresh Metal launcher shown;
native capture still reports `cgWindowNotFound`. No visible-slot load or longer
gameplay acceptance is inferred from these bounded checks.


## Unattended completion order

The player requested an ongoing goal to finish the Mac port, then the iPhone
and iPad port, without waiting for assistance. The player-assisted 30-minute
gameplay session is explicitly deferred. Continue implementation and automated
checks; record physical-device lock/disconnect failures without asking the
player to stay awake. Use backed-up local progress until an Odin cloud-save
folder is explicitly selected. Remaining hands-on acceptance stays visible and
does not block independent engineering work.

1. Finish the Finder bootstrap, relocatable driver bundle, launcher integration,
   safe pipeline install and repeated packaged-app checks. See
   [Mac app build and verification](../macos/README.md).
2. Complete bounded automated runtime, storage-failure and lifecycle checks;
   retain the earlier player's combat/audio/save evidence and defer the soak.
3. Build a signed Odin arm64 diagnostic/CPU app for both reference devices.
   Reuse proven infrastructure with Odin-specific inputs, identity and checks.
4. Establish translated real-module execution, then GPU replay and visible
   scenes; proceed through mobile input/audio/suspend/save/install milestones.
   Rosetta is not a mobile backend. Report the actual reached checkpoint and
   any CPU/renderer gap instead of treating a diagnostic shell as the game.

## First bring-up

Use an external work directory, with a link to the player's package in `WORK/game/`. Upstream's Python preparation tools require cryptography. Run from the checkout:

```sh
python3 scripts/prepare_game.py --game-root "$WORK/game" --scope full
python3 scripts/prepare_image.py "$WORK/game/prepared/CUSA05083-1.00" --out "$WORK/build/image/CUSA05083-1.00"
VK_DRIVER_FILES="$VK_PREFIX/share/vulkan/icd.d/kosmickrisp_mesa_icd.json" macos/probe.sh "$WORK" "$VK_PREFIX"
```

The last argument is an optional x86-64 Vulkan development prefix. Omitting it runs the ISA, memory, fiber and dynamic TLS checks. The script writes binaries into WORK. Keep game bytes, generated boot images, saves and device logs outside Git.

The supplied base package matches the published executable hash `7c517194aba4e5eea55d9197efae47d1567bd785b8c0dced7d1b0f4028d3eed6`. The prepared 15,746,416-byte image has entry 0x67eee0 and includes libc and libSceFios2. Of 307 imports, 89 bind to guest modules. 218 need host services. Import coverage at execution remains to be measured.

The translated ISA suite passed 11,088 MOVBE and 25,644 BMI1 differential cases with AVX advertised before launch. It checked 49 LZCNT replacement ranges in the actual US image. Tests for four other images reported skips. Darwin code now runs the existing fiber stack switch and verifies a yield and resume with distinct arguments. A sparse temporary file backs the shared direct-memory pool. The stock reservation starting at 64 GiB collided with an existing translated-process mapping. The Mac allocator now requests low allocations starting at 448 GiB and guest mappings at 512 GiB, checks exact placement, and keeps both below 1 TiB. Two direct aliases shared writes and released pages read as zero on reuse. Guest assumptions about fixed addresses and pointers returned by ordinary host malloc still require validation.

The US boot image has an empty main TLS segment and preparation found zero matching static FS loads across its modules. libc has 1,168 bytes of dynamic thread-local storage, including 416 initialized bytes. Fios2 has 64 zero-initialized bytes. The Darwin dynamic TLS service now returns low addresses. A two-thread probe against the production service passed template copy, zero-fill, write/read and distinct-address checks for these sizes. The full guest thread startup and guest-visible handle allocation still need a Darwin implementation.

Odin uses shadPS4's Liverpool/GNM frontend, runtime shader recompiler and Vulkan renderer. Keep that frontend and test KosmicKrisp as the first Metal backend. The x86-64 driver from the SpartanPort build exposed Vulkan 1.4, all four required extensions, robust buffer and image access, null descriptors and instance-rate divisors. Creating a device and completing a graphics queue fence passed. Scene rendering, required optional paths actually selected by the game, presentation, and shipping a relocatable driver remain open. OpenCrown's fixed set of SDL Metal shaders cannot represent this renderer.

## Delivery sequence and effort

The first deliverable a player can use is an offline local Mac app. Success means starting the US base game from Finder, reaching a playable battle with visible graphics and sound, saving at a normal save point, and recovering that progress after quitting. Use the M5 Max on macOS 27.2.1 as the reference. Report Refined and Classic mode results separately. Additional regions, macOS versions and a downloadable release have separate qualification work.

| Issue | Milestone | Prerequisites | Remaining active engineering days |
| --- | --- | --- | ---: |
| [1](https://github.com/runty/odin-sphere-leifthrasir-pc/issues/1) | M0 · Baseline and comparison fixtures | Existing probes | 0.5–1 |
| [2](https://github.com/runty/odin-sphere-leifthrasir-pc/issues/2) | M1 · Darwin runtime and real import check | Verified US image | 3–7 |
| [3](https://github.com/runty/odin-sphere-leifthrasir-pc/issues/3) | M2 · Headless guest progress | Linked M1 runner | 2–5 |
| [4](https://github.com/runty/odin-sphere-leifthrasir-pc/issues/4) | M3 · Visible Vulkan-on-Metal frames | GPU probe for standalone tests, M2 for live scenes | 4–10 |
| [5](https://github.com/runty/odin-sphere-leifthrasir-pc/issues/5) | M4 · Offline play, sound, controller and saves | Live M3 frames | 3–6 |
| [6](https://github.com/runty/odin-sphere-leifthrasir-pc/issues/6) | M5 · Finder launcher, packaging and verified local alpha | Paths and launcher can start at M1, final acceptance needs M4 | 3–6 |
| [7](https://github.com/runty/odin-sphere-leifthrasir-pc/issues/7) | Phase 2 · iPhone and iPad | Signed CPU fixture can start early, real-game comparisons need Mac fixtures | 3–5 for first experiment only |

Mac work totals **15.5–35 active developer days** from the current probe build. These are planning allowances for one engineer. Waiting for a PC reference, a second Mac or player feedback is additional elapsed time. The range assumes the selected Vulkan driver handles representative game shaders. Revise the estimate after first guest progress and again after the first correct battle frame. A renderer replacement would need a separate estimate.

## M0 · Reproducible inputs and reference route

**Deliverable.** A manifest and comparison route usable by someone with their own matching package. Start from the pinned profile, preparation scripts and upstream's first-boot log vocabulary. [Package profiles](../scripts/game_profiles.py) · [Image builder](../scripts/prepare_image.py) · [First boot](FIRST_BOOT.md)

- [x] Verify the US base package by SFO, file bounds and eboot hash. Prepare the full app0 and boot image on the external build disk. Record entry, module list, TLS sizes and import split.
- [x] Run the Rosetta, memory, fiber, TLS and Vulkan capability probes on the reference Mac. Record actual case counts and skipped image variants.
- [ ] Pin the driver build, dependency versions, compiler, SDK, architecture and launch environment in a reproducible manifest. Confirm a clean checkout reproduces the boot-image metadata and probe results.
- [ ] On upstream Windows, capture a repeatable title → story selection → first battle → save route on the same game version. Record inputs, mode, resolution, one stable image per scene and visible save fields. If the reference machine is unavailable, continue host work and mark cross-platform image parity pending.

## M1 · Build and run the real Darwin loader

**Deliverable.** An x86-64 command-line runner that validates the prepared game and reports an import inventory. Build with Apple Clang. Preserve per-platform CMake choices for Windows and Linux. The initial object inventory had 13 passing and eight failing runtime translation units. All 21 production runtime units now compile for Darwin, and the upstream synchronization suite passes. Start with a repeatable CPU target independent of launcher UI initialization. Explicitly list the libraries and symbols still needed when graphics is disabled. [CMake](../CMakeLists.txt) · [Loader](../src/loader.c) · [Threads](../src/runtime_thread.c) · [Memory](../src/runtime_memory.c)

- [x] Validate low memory aliases and reuse, a yield/resume round trip, and dynamic libc/Fios2 TLS isolation on two host workers in the Mac probes.
- [ ] Establish a clean build with pinned SDL3, FreeType and the required launcher/core dependencies. Check every linked architecture using `file` and `otool -L`. Supply font path definitions from CMake and adapt the embedded font section and labels for Mach-O.
- [ ] Adapt Darwin stat fields for game files and pad settings. Implement the guest clock IDs, monotonic audio deadline and timed mutex/semaphore waits. Verify timeout, wakeup and cancellation with real worker threads, including an already expired deadline.
- [ ] Port guest thread attach, naming, stack ownership and teardown. Exercise libc and Fios2 TLS from threads created through the guest API, including reuse after join. The US image has zero recognized static FS loads. Validate the prepared profile before relying on that property.
- [ ] Audit every pointer returned to the guest, including handles, callbacks, TLS, stacks and strings. Stress low-address allocations and verify graceful handling of reservation collisions, alias removal, protection changes and address reuse. Re-run the working fiber and memory probes.
- [ ] Implement Darwin register/context access and bounded fault diagnostics. Verify a handled guest condition resumes at the expected instruction, and an unrelated terminal fault reports its module, offset and thread. Check watchdog timeout and ordinary exit as separate outcomes.
- [ ] Run `--check-game` on the prepared app0/image and `--check-imports` against the actual tables. Reconcile each of the 307 imports by module export, host function/data or missing service. Record CPU-only and graphics-enabled configurations separately. The loader can skip GPU registration in CPU-only mode. Require zero unexplained missing imports and zero missing imports in the full configuration before playable acceptance.
- [ ] Build or run affected upstream tests on their original host, including ISA and runtime synchronization, and record the host and skips.

## M2 · Execute a repeatable headless scene

**Deliverable.** A bounded smoke command, disposable state directory and two comparable logs. Use the real `src/loader.c` and production HLE paths. Proposed invocations after M1, using the built runner and a fresh fixture directory:

```sh
"$RUNNER" --check-game "$APP0" "$IMAGE_DIR"
env ROSETTA_ADVERTISE_AVX=1 "$RUNNER" "$IMAGE_DIR/boot.bin" --cpu-only --check-imports --app0 "$APP0" --user "$FIXTURE/user" --logs "$FIXTURE/logs"
env ROSETTA_ADVERTISE_AVX=1 "$RUNNER" "$IMAGE_DIR/boot.bin" --cpu-only --osl-skip-launcher --timeout 120 --app0 "$APP0" --user "$FIXTURE/user" --logs "$FIXTURE/logs"
```

- [ ] Log effective CPU features, mapped segments, applied relocations, TLS templates, native bindings and the guest entry address. Both linked module initializers must return successfully before entering the eboot.
- [ ] Capture the first divergence from the PC route. For a missing service, record import, caller and required result. For a wait, identify who must signal it. Implement the observed service contract and replay the same input.
- [ ] Reach a named post-startup game transition on two fresh processes. Use actual guest events, asset reads and worker progress to identify it. Capture a second run with a previously created disposable profile.
- [ ] Run a ten-minute bounded soak after the transition. Check periodic progress, thread count, memory growth and log endings. If video synchronization prevents headless progress, isolate the required event or fence in a small test and verify its real producer in M3.

## M3 · Render and present through Metal

**Deliverable.** Screenshots and timing logs from live title and battle frames. Retain Odin's GNM command processing and shader recompiler. Bring up KosmicKrisp in the translated process first. [Renderer](../gpu/shadps4/video_core/renderer_vulkan/vk_instance.cpp) · [Shader translation](../gpu/shadps4/shader_recompiler) · [Host window](../gpu/shim/window.cpp) · [Video integration](../gpu/shim/oslgpu.cpp)

- [x] Create the logical device with the required extensions and robust features, then observe graphics queue fence completion on the M5 Max.
- [ ] Verify SDL surface creation, drawable size, swapchain formats, present mode, acquire/submit/present and resize using a known test image. Save a readback and verify representative pixels.
- [ ] Run guest GNM submissions. Log first shader translation, pipeline creation, draw, flip and presentation. Validate descriptor bindings, null descriptors, divisors, tiled textures and CPU/GPU memory visibility. Capture the first failing shader/pipeline privately with sufficient state for replay.
- [ ] Inspect title, animated sprites over backgrounds, UI text, transparency and battle effects against M0 fixtures. Record any tolerated pixel differences and reasons. Verify native size first, then window resizing, 720p and 4K target selection separately.
- [ ] Measure cold and warm shader-cache behavior and per-frame CPU, GPU and present times in the same scene. If a feature query passes but a draw fails, replay the smallest failing draw before deciding between a driver fix, a renderer fallback or a larger redesign.

## M4 · Play, audio, controller and save persistence

**Deliverable.** A documented playable session with recoverable progress. Use disposable state until the save route is verified. [Guest audio](../src/runtime_audio.c) · [Controls](../src/runtime_pad.c) · [Save services](../src/runtime_savedata.c) · [Local trophies](../src/trophies)

- [ ] Start Refined mode, select a story, play the first battle, change equipment or inventory and reach a normal save point. Record chapter, location, character and an observable progress change. Check Classic mode separately and report its tested extent.
- [ ] Verify keyboard and one physical controller through navigation and combat. Disconnect while holding an action and reconnect. Check for stuck buttons, wrong prompts and lost focus after switching windows.
- [ ] Measure actual CoreAudio sample rate, device period and queue behavior during dialogue, music, combat and scene changes. Test interruption or output change. Use a tone and pre-device capture if pops recur. Select the device period from Odin measurements.
- [ ] Save, exit the entire process and reload twice. Compare the visible slot and recorded progress against the backup. Test interrupted writes and unwritable storage against copies. Check settings and local trophy persistence independently.
- [ ] Run 30 minutes of story progression and combat at a recorded render size, refresh and preset. Record median and 95th-percentile frame times, simulation speed, peak memory, audio gaps, faults and cache conditions. Use 16.7 ms as the initial 60 Hz frame budget and report the measured result.

## M5 · Finder app, build pipeline and offline alpha

**Deliverable.** A repeatable local install with a launcher a player can use from Finder. The upstream executable hosts the launcher, then enters the game. Preserve that behavior or document the reason for a separate runner. [Path resolution](../launcher/paths.cpp) · [Game installation](../launcher/game_install.cpp) · [Updater](../launcher/updater) · [Upstream packaging checks](LINUX.md)

- [ ] Make Finder launch inherit the required CPU settings before translated game startup. Log effective AVX. Use the app resource directory for UI artwork, packaged frameworks for the Vulkan loader/driver, Application Support for writable state and Library/Logs for diagnostics.
- [ ] Exercise native package selection, cancel, unsupported revision, missing external disk, reconnect, and paths containing spaces or Unicode. Preserve the active installation if verification or copying fails. Test Play, quit and relaunch from Finder.
- [ ] Ask for Odin's save location before connecting live progress. If cloud is chosen, seed with a backup and verify a disposable handoff before claiming multi-Mac sync. Ensure the updater cannot install upstream Windows/Linux assets.
- [ ] Pin and build the Vulkan driver and transitive x86-64 libraries from documented inputs. Bundle the ICD manifest with a relocatable library path. Include notices. Audit nested signatures, dependencies, bundle inventory and deployment target on the final artifact.
- [ ] Add an Odin wrapper to the private console pipeline. Verify mount selection, package identity, available space, incremental build and staged app replacement. Keep generated game data and caches on the external build disk. Test interrupted replacement and recovery.
- [ ] Open the relocated app from Finder in a fresh account with the build tree inaccessible. Confirm title, battle and save reload from the installed copy. Replace the app and compare saves, settings and trophies. Record the build, host and known failures in the games list only after this verification.

Local alpha acceptance is M2 guest progress, M3 visible scenes, M4 playable save/reload and M5 installed-app evidence on the reference Mac. A second machine or OS version receives its own result. A public binary additionally needs a distributable inventory and notices, distribution signing, notarization and a downloaded-copy launch check.

## Phase 2 · iPhone and iPad

**Deliverable.** A locally provisioned arm64 app tested separately on iPhone 17 Pro Max and iPad mini (A20 Pro). Start with a private signed fixture. OpenCrown's Remill harness and device deployment can supply test infrastructure. Odin's module layout, guest ABI, fiber behavior and generated GPU shaders need their own fixtures.

- [ ] **P0, device baseline.** Pin the Odin inputs and Mac comparison outputs. Launch a signed arm64 shell on both devices without a debugger, export model/OS/page size and establish a disposable sandbox. Budget 3–5 days for P0 plus the initial P1 feasibility experiment.
- [ ] **P1, CPU decision.** Compare ahead-of-time translated blocks with the Mac x86-64 reference, including flags, SIMD, indirect calls, host callbacks, two-thread module TLS and fiber switching. Execute a routine from each of eboot, libc and Fios2 and compare register/memory results. Benchmark any interpreter fallback for uncovered blocks. Select the backend only after correctness and timing measurements on both devices.
- [ ] **P2, module startup.** Load game data into an explicit guest address space, route pointers and imports through the signed runtime and repeat M2. Measure peak memory and guest work per frame. Re-estimate mobile delivery at the first real transition. Current partial result: fresh iPad `1376d6a` completes both module initializers and reaches VideoOut after the bounded-alias fix, without a memory fault; full M2/frames and peak-memory workload remain open.
- [ ] **P3, graphics decision.** Replay a captured Odin draw on device using a viable arm64 backend. Verify shader conversion, texture formats, memory footprint and actual presentation. Compare against M3 before integrating title and battle frames.
- [ ] **P4, offline play.** Import the player's files through Files with size, profile and cancellation checks. Add touch-only navigation and simultaneous combat inputs, physical controller reconnect and audio interruption recovery. Suspend/resume, terminate, reload twice and repeat M4's 30-minute session on each device.
- [ ] **P5, install and handoff.** Verify repeat install and offline restart on both devices. If the player chooses shared saves for Odin, use copies to test Mac → phone → tablet → Mac, offline divergence and restore before using live progress.

## Verification and decision log

For each check, attach the commit, build command, dependency manifest, host/device and OS build, game profile, input route, expected event, observed event and exit reason to its tracking issue. Keep source fixtures and synthetic tests in Git. Retain game-derived captures, saves, package data and raw logs in the private build tree. Re-run failed cases on the same fixture before changing the comparison baseline.

| Decision point | Evidence needed | If it fails |
| --- | --- | --- |
| Loader maps safely | Real low pointers, overlapping reservation rejected, aliases coherent and guest-created worker stacks/TLS valid | Isolate the escaped pointer or collision before increasing memory limits |
| Guest reaches a scene | Module init, guest entry and reproducible scene/event transition | Trace first blocked import or wait, repeat the same bounded run |
| Driver is viable | Actual guest draw and correct presented image | Reduce the failing draw and measure the cost of the proposed fix |
| Game is playable | Battle, input/audio and twice-reloaded normal save | Keep last complete slot and record where progress diverges |
| App is installable | Fresh-account Finder launch and replacement preserving state | Inspect dependency/ICD paths, signature and writable folders |
| Mobile is feasible | Correct signed CPU routines and representative shader draw on each device | Report first unsupported operation and updated effort before full gameplay work |


## Darwin runtime build progress

The M1 runtime build compiles all 21 production runtime sources into an x86-64
archive with warnings treated as errors. It embeds the two Noto font payloads in
Mach-O sections, adapts stat timestamps, monotonic audio sleeps, monotonic timed
mutex/semaphore waits, secure random bytes, CPU-time queries and pthread names.
The upstream synchronization executable links these production thread and wait
services and passes its mutex, semaphore, eventflag, join and cancellation cases.
The original ISA/memory/fiber/TLS/Vulkan probe command also passes again.

Build the runtime archive and run its synchronization tests with:

```sh
macos/build-runtime.sh "$WORK/darwin-runtime" "$SDL_PREFIX" "$FREETYPE_INCLUDE"
```

SDL_PREFIX supplies the SDL3 headers. FREETYPE_INCLUDE contains ft2build.h.
The fonts are embedded directly from third_party/fonts. Outputs and the test
executable stay in the external directory. Keep the script's build log with the
dependency versions used. The test archive has no dependency on ARM Homebrew
libraries.

The observed US image has empty main/static TLS. The Darwin thread adapter
rejects nonempty static guest TLS, and the existing dynamic service supplies
libc/Fios2 TLS. Validate that profile property at the eventual loader entry
before enabling other builds. Auditing all heap handles returned to the guest
remains part of M1.

The full-project configure experiment also exposed Darwin changes in the loader
and video core. The loader needs Mach context registers, fault-safe reads and a
replacement for /proc thread enumeration. GPU compilation needs thread
identity/affinity and CPU-clock adapters plus LP64 type corrections. Some
automatic library searches selected native ARM Homebrew libraries and need
explicit x86-64 dependencies. Next work is a linked runner and the real import
check. Guest scene progress and a full graphics-enabled build remain pending.

## Linked loader, import check and first guest boot

The full x86-64 executable now links on the reference M5 Max/macOS 27.2.1.
Its current deployment target is macOS 27.2. Darwin adapters cover register
contexts, fault-safe frame reads, executable discovery, GPU thread IDs and
background QoS, Mach-O embedded overlay fonts and LP64 type differences.
A focused signal test resumes after UD2 with the expected instruction pointer
and return register. Safe reads reject inaccessible and invalid pages.

The Vulkan library links the real GNM, VideoOut and kernel services. Import
checking now skips window/device construction and uses their actual symbol
registries. Both normal and CPU-only import checks report **307 imports, zero
missing**, with 241,949 relocations and 89 native module bindings. The prepared
US base image also passes --check-game.

The first bounded CPU-only boot initializes libc and Fios2, both returning zero,
then enters the original eboot at 0x67eee0. It registers the game heap, initializes
services and makes direct/flexible allocations. The game then opens video output.
CPU-only execution has no presenter, so it now stops with a diagnostic and exit
20. The initial attempt exposed a null VideoOut driver fault at that point.
The explicit guard preserves a useful log and identifies the next required
subsystem. A named title or battle scene remains to be reached.

The full CMake test targets for ISA, synchronization and tiling pass. The tiling
suite reports 1,584 checks with zero failures. The probe suite also passes again,
including real US LZCNT sites, memory aliases, fibers, dynamic TLS and the Vulkan
fence. Other game profiles in the ISA suite are skipped when their images are
absent.

### Build inputs

Use macos/build.sh with six explicit external paths:

```sh
macos/build.sh "$WORK/macos-build" "$VK_PREFIX" "$DEPS_PREFIX" "$FREETYPE_BUILD" "$FREETYPE_INCLUDE" "$SDL3_CMAKE_DIR"
```

The script fetches the pins in gpu/deps.txt without replacing an existing
different dependency. VK_PREFIX supplies the x86-64 Vulkan/SDL/fmt/Zydis/xxHash
stack. Header-only Boost, robin-map and VulkanMemoryAllocator come from Homebrew.
DEPS_PREFIX supplies static x86-64 OpenSSL 3.5.4 and libpng 1.6.50. FreeType uses
the x86-64 static build already tested with OpenCrown. The resulting developer
executable currently links SDL and Vulkan through absolute paths in VK_PREFIX.

For matching source archives, the SHA-256 values are:

| Source | Digest |
| --- | --- |
| OpenSSL release openssl-3.5.4.tar.gz from github.com/openssl/openssl | 967311f84955316969bdb1d8d4b983718ef42338639c621ec4c34fddef355e99 |
| libpng codeload tag v1.6.50 from github.com/pnggroup/libpng | 71158e53cfdf2877bc99bcab33641d78df3f48e6e0daad030afe9cb8c031aa46 |

Configure OpenSSL with darwin64-x86_64-cc, no-shared and no-tests, and install_sw
into DEPS_PREFIX. Configure libpng with CMAKE_OSX_ARCHITECTURES=x86_64,
PNG_SHARED=OFF and PNG_TESTS=OFF. Preserve the dependency sources and build logs
for reproduction.

The reused SpartanPort SDL build cannot open a desktop video device. Live
rendering needs a Cocoa-capable SDL build, a Metal surface, and main-thread
window/event handling. The current Darwin watchdog reports the current thread.
Collecting every other thread's Mach register state remains outstanding.
Guest-visible heap handles and longer execution still need the M1/M2 checks.


## Cocoa and visible game frames

Cocoa-capable SDL 3.4.18 now creates the window and Metal view on the process main
thread. The Vulkan surface receives SDL's CAMetalLayer. The main thread pumps
window events while a dedicated guest thread initializes libc/Fios2 and runs the
eboot on its low stack. Windows/Linux retain their existing window-thread paths.

The Vulkan launcher check and renderer now locate the already linked Mac loader
through dladdr. Instance creation respects the selected ICD. This fixes the
initial misleading Vulkan-not-installed dialog and a subsequent instance error
caused by replacing the chosen driver path with an executable directory.

Native inspection verified the no-save-data notice and animated opening graphics
in the Cocoa game window. Logs identify the 1920x1080 source, swapchain and window.
Generated guest shaders produce the animated images through KosmicKrisp. A later
write to a GPU-protected page exposed Darwin's SIGBUS delivery. The fault handler
now forwards both SIGBUS and SIGSEGV to the page tracker.

Presentation counters log advancing frames, draws and submissions. Set
OSL_FRAME_STATS=1 for timing/counter diagnostics. OSL_TEST_PRESENT_LIMIT exits
cleanly after an explicit number of presented frames, for bounded smoke runs.
Audio was disabled during this rendering pass, and state was disposable.
Gameplay, PC pixel comparisons, live audio and normal save/reload still need
their milestone checks.

Build with SDL3_CMAKE_DIR pointing to the Cocoa-capable SDL build's CMake config.
The development bundle used for native inspection stays in the external build
tree and uses external game/dependency paths. Installation remains in M5.

The corrected rendering run completed 10,000 presented frames and exited zero
at its requested limit. At frame 9,600 the counters recorded 2,904,448 draws and
57,520 submissions. Native screenshots showed the no-save-data notice and the
animated opening with textured geometry, stars, wings and translucent effects.
The native images were inspected in the test session. The regression record
retains log counters, with game-derived data kept outside the repository.

The updated build entry point takes the Cocoa SDL CMake directory as its sixth
argument. For a bounded live render check after building:

```sh
env ROSETTA_ADVERTISE_AVX=1 VK_DRIVER_FILES="$ICD_JSON" OSL_AUDIO=none \
    OSL_FRAME_STATS=1 OSL_TEST_PRESENT_LIMIT=10000 \
    "$RUNNER" "$IMAGE_DIR/boot.bin" --osl-skip-launcher --timeout 240 \
    --app0 "$APP0" --user "$FIXTURE/user" --logs "$FIXTURE/logs"
```

The earlier Vulkan availability dialog is resolved. A long gameplay session,
title-menu/battle acceptance and save/quit/reload remain open. PC frame parity,
resize/display changes and audible CoreAudio need separate checks.

## Live gameplay and CoreAudio

The next disposable-profile session uses a 960x540 window, the US base image,
and the same Cocoa/Metal build. Native inspection reached Gwendolyn's opening
story scene, and the player subsequently confirmed that playing through works.
The player also confirmed that game audio sounds good. These observations do
not yet establish the full Refined/Classic, equipment, controller or save criteria
in M4.

For this audio run, leave OSL_AUDIO unset and set OSL_AUDIO_STATS=1 and
SDL_AUDIO_DEVICE_SAMPLE_FRAMES=256. The guest opens 48 kHz, eight-channel float
audio in 256-frame blocks; the existing mixer downmixes it to stereo. The initial
48 five-second queue reports show one startup starvation and none thereafter,
with 938 blocks per interval and a largest submit gap of 27.51 ms. A pre-device
stereo capture of 330.976 seconds contains no non-finite samples. It is separate
from the player's listening check and cannot detect output-device glitches.

Audio startup now logs the opened SDL output device's actual sample rate,
channels, format, frame count and period. This distinguishes the output period
from the guest's block size and the requested SDL hint. Output changes and
interruptions still need testing. Keep raw captures and save data outside Git.

The full first audio session exits with code 0 after the player quits. Its 122
five-second audio reports retain the single startup starvation and zero later
starvations. The last presentation counter is 26,400 frames and 11,754,107 draws;
submission instrumentation was disabled in that run. On the next process,
the opened CoreAudio output reports **48000 Hz, stereo SDL_AUDIO_F32LE, 256
frames (5.33 ms)**. Native inspection also reaches the opening combat tutorial,
with enemies, attack effects and the pause menu showing Valkyrie, Prologue,
Act 2, Storming Battlefields, Normal difficulty.

The first session did not change the initial profile's save bytes or timestamps.
A backup was taken before the second launch. The second session writes new
SAVEDATA00 contents and exits zero. After a third process launch, the player
confirms that the save worked, then quits; that process also exits zero. The
1,156,096-byte game save retains the same SHA-256 across the third process.
The latest profile, earlier copies and hash manifests stay in the private test
directory. This establishes one player-confirmed save/reload, not the full M4
repeat-reload and storage-failure checks. Larger window/fullscreen changes in
the second session also show frame-time spikes, so 60 fps gameplay acceptance
remains open.

## Combat performance follow-up

The Mac triangle-fan path now avoids a per-draw compute conversion in the
Vulkan-to-Metal driver. In selected compile-free busy windows from the player's
early battlefield tests, median window performance improves from 34.1 fps to
60.0 fps at native 1920x1080, with similar median draw counts. The player confirms
smoother combat and correct sprites/effects. Geometry tests and the final
1,800-frame default-settings smoke pass. See [the profiling method, safeguards,
measurements and fallback switches](../macos/PERFORMANCE.md).

Occasional hitches remain, and the selected optimized windows span 42.7–60.0
fps. The M4 30-minute soak, Classic mode and other profile acceptance remain
open. These scene measurements do not establish full-game performance.


Shared event-flag source `e266020` fixes native deletion/waiter lifetime and
stale-handle reuse before exposing the same code to the AOT guest. Native
arm64/Rosetta tests each pass 4,792 checks, including eight blocked waiters
removed together and 200 four-thread set/poll/delete races. AddressSanitizer
and ThreadSanitizer arm64 runs pass. The full Mac synchronization suite passes,
and the development runner presents 600 frames / 50,945 draws / 3,187
submissions against a copied profile without changing copied or live saves.

Clean Mac `c28064c` is now installed after the relocated Unicode/minimal-PATH
bundle, seven launcher pages, concurrent-profile rejection and two 300-frame
restart checks. The normal LaunchServices path reaches `Launcher: shown` and
the assistant-owned test process is closed. All 166 original profile files in
the conservative non-secret hash selection retain their bytes. Existing local
runtime/library/configuration and the previous app backup are preserved.
Fresh visible saved-slot selection and the deferred 30-minute session remain
unclaimed. Evidence: `macos-eventflag-candidate-check/`,
`ios-eventflag-mac-smoke/` and `macos-c28064c-{candidate,install}.log` in the
private Odin build root.

The corresponding mobile host now creates twelve real event flags and reaches
859 traces / 2,568 dispatches / 28,275 checkpoints with no memory fault. Both
initializers complete and the next missing API is `sceVideoOutOpen`; the guest
has not been connected to the native renderer. Direct allocation owns
1,294,155,776 bytes. Fixed and repeated ASLR runs reach the same VideoOut boundary, allocations
and flag count. The raw count may be 2,562 or 2,568: three pointer-range checks
conditionally visit two extra comparison blocks based on relative pointer
ordering. Complete logical dispatch traces match after normalizing only the
exact byte-pinned range-check path; all other differences still fail. The
combined corrupted/truncated image/pointer rejection tests then pass. This is
canonical code-target/boundary evidence, not full CPU-state equivalence. Clean `10d9c3a` Simulator launches 13 and 14 now repeat the verified fixed-
address host result: 859 traces / 2,562 dispatches, 414 CPU checks, both
initializers complete, twelve flags and no memory fault. Private inputs survive
replacement/relaunch. The matching signed iPhoneOS/GPU app builds and verifies,
staged only. Physical devices retain `dcd85de`. [Shared event-flag scope](../ios/services/README.md#shared-event-flags-and-guarded-guest-waits).


### 2026-11-14 mobile integration checkpoint

The installed Mac stays validated `af52e09`, with player-confirmed gameplay,
audio/save reload and the earlier combat improvement. The user deferred the
30-minute hands-on soak; it and fresh-account click-through remain open.

The physical iPad now passes clean `b1b10cc` launches 18/19: CPU 414, native
memory 19, GPU readback 64 and actual main-thread SDL UIKit/Metal-to-Vulkan
surface creation/capability checks. Both original input files survive update
and relaunch. Five formats, FIFO, graphics/present queue 0, three-image min/max,
2316×1512 extent, Vulkan/SDL cleanup and original UIKit key window preservation
pass. Simulator clean launches 22/23 pass their Metal-layer-only path. iPhone
retains `1376d6a`; fresh execution remains denied while locked. No swapchain
presentation, mobile guest draw or playable mobile game is established.

A new isolated backend passes exact VBLENDPS and lane-local VSHUFPS contracts,
each across 24,576 output lanes including destructive aliases. Reciprocal and
VPERMILPS regressions and all 414 real-routine checks pass. A full-window
re-lift of all 1,037 seed PCs, without copying old IR across semantics, now
advances through both executed instruction gaps. Strict native Mac GPU
qualification passes 60 bridge checks, four joined workers, three 1080p buffers
and ordered cleanup; headless input-rejection checks pass. New GPU corpus
`c039c7a4` still stops explicitly at VPERMILPD `0x106aad24`, without memory fault
or frames. These host experiments are separate from device memory corpus
`6be8c7c0`; instruction-test success is not physical game execution.

Both mobile SDKs now have independently built/audited SDL, fmt, xxHash, Zycore,
Zydis and robin-map. Support-library CMake consumer/link checks pass, with all
34 non-SDL archive objects per SDK platform-audited. Remaining work includes
PNG/header dependencies, the next executed CPU gap, main-thread/foreground
renderer integration, swapchain/full guest presentation, asset import and
mobile lifecycle/input/audio/save acceptance. [Current graphics scope](../ios/graphics/README.md).


The next checkpoint implements immediate VPERMILPD (`34b7b10`): 6,144 exact
native/AOT 64-bit lanes including destructive aliases, 24,576 shuffle regression
lanes and CPU 414 all pass, with both mobile fixture archives. A new full-window
re-lift advances to 1,039 entries and explicit VRSQRTPS `0x106aa80c`; no memory
fault, four joined workers and three buffers. Qualification is recorded separately
from this discovery. Register-controlled VPERMILPD remains unimplemented.

The full iPhoneOS and Simulator renderer archives now compile with the real
shader recompiler, GnmDriver, VideoOut, presenter and UIKit backend; every one of
173 objects per SDK is platform-audited. PNG (18 objects), Boost and VMA inputs
are independently pinned and the target SDK supplies zlib. A full consumer links
against the real signed iPhoneOS driver and allocator/services, without unresolved
symbols, but is not installed or run. UIKit session/lifecycle and actual game GPU
callback/asset/presentation integration are still ahead. The verified iPad app
and installed Mac remain unchanged. [Build and evidence scope](../ios/graphics/README.md#full-mobile-renderer-archive-and-consumer-link).


### 2026-11-14 renderer ownership and physical initialization

Packed reciprocal square root now passes six forms/8,192 inputs/32 MXCSR
settings/1,572,864 output lanes against independent native/AOT ISA contracts;
this does not match a specific CPU approximation LUT bit-for-bit. New GPU-backed
startup advances to explicit VBLENDPD `0x106aa939`, with four joined workers,
three buffers and no memory fault or game frame.

A polled Apple renderer API and explicit teardown now join GPU/presentation/cache
workers, reset IRQ/overlay/mapping state and drain accepted cache writes. Native
normal and AddressSanitizer tests each pass three restart cycles and verify 192
queued private cache records after shutdown, with normal process exit. Driver/
system libraries are uninstrumented; leak sanitizer is disabled.

A separate signed `com.wadscorp.odinsphere.renderer` UIKit check now initializes
the full renderer on actual A20 Pro hardware, opens/closes VideoOut, completes
30 main-thread polls and restores its original key window after shutdown. First
attempts exposed a null loader-layer entry point in direct ICD bootstrap; the
empty-layer path no longer calls it, with required API presence checks retained.
Appearance-transition warnings remain when restoring the diagnostic view; the
UI candidates are not gameplay or full lifecycle acceptance. No mobile game
content/frame exists yet. The normal probe and original saves remain separate.


Clean `4c2cbcf` now repeats complete physical renderer initialization/shutdown
on launches 6/7 (distinct PIDs, prior PID absent), retaining CPU 414, memory 19,
GPU 64 and native surface checks. Both original normal-probe input hashes survive
in their separate container. The standalone renderer process is closed with a
fresh exact bundle-path/PID match and verified absence; no Odin physical test
remains active after this checkpoint. Latest bootstrap host GPU corpus `584862ba`
repeats strict 60-case and cleanup qualification, stopping at VBLENDPD. No game
frame/mobile save acceptance is claimed, and UIKit appearance warnings remain.

### UIKit guest integration

Clean iPad startup `d3d876f` now runs the actual guest with SDK `0x03008041`,
four joined workers, three registered buffers and full queue/mapping/window/
signal ownership cleanup. Controlled cancellation across a real Settings
foreground transition also passes, with an explicit diagnostic gate and the
same PID after reactivation. Assets are imported and 33 selected files match
by round-trip hashes. There are still zero presented game frames; CPU startup
stops at a missing translated block. These are bounded startup checks, not
full gameplay or new in-game save/resume acceptance. The Mac remains `af52e09`.
[Scope and reproducible bridge build](../ios/graphics/README.md#uikit-guest-connection--2026-11-14).

## Discovery and startup audio checkpoint — 2026-11-15

Original unwind metadata now optionally supplies bounded prefetch candidates.
The first 1,024-entry atlas batch accepts 1,023 and skips one unsupported static
candidate. The 11,333-entry native replay reaches a new executed function entry
`0x101f79d0` after 159.484 seconds / 2,764 presentations, joins 42 workers and
cleans up without a memory fault. Its frame-2,700 image remains black, so title
and gameplay acceptance remain open. Static candidate provenance/raw hashes,
59 unit checks, four CLI rejects, 28 bounds checks, eight frozen-input checks
and a real concurrent-source-edit rejection pass. [Prefetch policy](../ios/game/README.md#bounded-prefetch-from-verified-unwind-entries--2026-11-15).

A second unchanged iPad build/run captures actual converted stereo PCM: 158.987
seconds, all silent, no nonfinite samples. Five sampled empty-queue observations
remain recorded; audible game output is unqualified. System-save hashes and
cleanup pass again. [Exact capture limits](../ios/graphics/README.md#startup-pcm-capture--2026-11-15).

The working Mac reference has `skip_intro=1`; mobile does not yet expose the
launcher's checked Skip intro adapter. `launcher/skip_intro.c` and its supported
build sites are the existing implementation to reuse when that option is
ported. Its native-x86 stubs cannot be injected into signed arm64 execution.
Preserve save/trophy checks and the game's own scene transitions when adapting
it. This setting difference does not establish the cause of a black mobile
frame or justify claiming title progress. Current startup discovery remains
from actual original-image stops.


## Runtime bridge and seed qualification — 2026-11-15

The latest completed native replay contains 19,535 entries and reaches actual
gap `0x1056ac97` at 152.895 seconds, with no memory fault and 42 joined workers.
Later expanded-corpus linking exposed missing FP exception helpers. OpenCrown's
CAS family and FP exception/rounding mappings now supply those bridges using
Odin's checked memory, locking and recovery. Native arm64/Rosetta, fatal sanitizer,
414 original-module and both mobile SDK fixture builds pass. FP80 arithmetic and
full wide-instruction alignment-fault qualification remain open.
[Runtime verification](../ios/README.md#atomic-widths-and-floating-point-control-bridges--2026-11-15).

All original verified unwind entries and strict switch targets are staged;
42,105 raw entries are available after deduplication against demand discovery.
This is static availability, awaiting normal compilation and replay. It supplies
no new physical-device/gameplay/audio/save acceptance.
[Publication and provenance](../ios/game/README.md#complete-validated-seed-catalog--2026-11-15).

The active OpenCrown mobile branch has also advanced to `ce065bb`. Its new
read-only cursor feedback and relative-touch model are references for later
input work, not directly transferable Odin cursor addresses or game layouts.
The primary imported OpenCrown baseline stays `9ca8945c`. Inspect both worktrees
for upcoming lifecycle/input/audio/packaging work and qualify each adopted adapter
against Odin's own ABI, original-image identities and actual execution evidence.


## Independent x86 atomic qualification — 2026-11-15

A dedicated x86 hardware oracle exposes a CMPXCHG8B upper-register write defect.
The corrected twenty-patch backend passes all 1,056,768 instruction checks,
1,584,128 SIMD dword comparisons and 414 original-module checks; both mobile
CPU archives compile. Rosetta-only CAS flag/register disagreements are retained
as separate evidence, without modifying semantics to match them. Fresh game IR
is required; neither the nineteen-patch raw cache nor the installed iPad is
silently upgraded. [Exact oracle, tests and scope](../ios/README.md#independent-atomic-instruction-oracle--2026-11-15).

The last nineteen-patch native checkpoint has 42,107 compiled entries and stops
at actual missing block `0x105c04b1` after 158.430 seconds, without a memory fault
and with 42 joined workers. It finishes before the requested frame-3,200 image,
so there is no fresh snapshot from that run. Production Mac, original saves,
iCloud and the away phone remain untouched. The iPad stays available for the
next qualified corpus; recognizable title/gameplay, nonempty audio and character
progress reload still need actual device verification.


## Physical correction and continued code discovery — 2026-11-15

The new twenty-patch checkpoint is verified on the iPad across an app update and
a second cold launch: CPU/memory/GPU checks, 42 joins, exact system-save hashes
and full cleanup pass. Its fresh later frame remains black; character progress
and audible output are not qualified. [Physical evidence and limits](../ios/graphics/README.md#expanded-physical-startup-and-cold-repeat--2026-11-15).

Native discovery has advanced to 53,312 compiled entries, actual gap
`0x105b4083`, with cleanup and no memory fault. Broader read-only analysis reuses
OpenCrown's validator over all original eboot owners and observed roots. A
bounded 7,027-target next cohort publishes 6,995 candidates with 32 explicit FP80
skips, retaining incomplete-owner assumptions. Eight original switch siblings of
an observed case are also published, with the rejected ABI/index proof explicit.
Native compilation/replay starts from 60,315 available entries; no new execution
is implied by publication. Runtime branches and strict proof policy are unchanged.
[Inventory and staging scope](../ios/game/README.md#anchored-owner-inventory-and-bounded-incomplete-candidates--2026-11-15).

An isolated Mac launcher copy opens/shuts down and preserves its copied/original
profile hashes, but CUA returns `cgWindowNotFound` before UI inspection or input.
Mouse Play acceptance therefore remains open. The production bundle is unchanged.


## Latest physical startup and table expansion — 2026-11-15

The frozen 60,361-entry global-lock checkpoint is installed and verified across
an update and two physical iPad cold starts. New copy/move adapters execute in
the actual guest. Exact save hashes, CPU/memory/GPU checks, 43 joins and cleanup
pass. Later game and display-swapchain pixels remain black and recorded PCM is
silent; no playable-scene or audible-output acceptance is claimed.
[Exact hardware evidence](../ios/graphics/README.md#expanded-global-lock-ipad-and-cold-audio--2026-11-15).

Original-byte in-range JBE analysis recovers the repeated missing cases without
changing runtime conditions. A wider locally validated cohort publishes 843 new
entries and preserves three duplicates, with unproved global guard/CFG properties
explicit. Native availability is now 61,204; new replay is independent evidence.
The default lock is restored after the stripe candidate did not reach the baseline
within its budget. Original player data, production Mac and the iPhone are untouched.

The wider native replay passes the old demand and reaches original loader owner
`0x1070b650..0x1070da14`; its 61,206-entry checkpoint freezes the third gap with
43 joins and cleanup. Bounded original-branch/table staging plus the existing
first-block narrowing policy now provide 61,550 entries. Independent decoding
checks all 209 shortened prefixes; execution and visible-scene acceptance remain
separate. [Scope and evidence](../ios/game/README.md#observed-loader-owner-and-first-block-windows--2026-11-15).

Actual 61,550-entry replay now reaches a later original call continuation with
43 joins, no fault, cleanup and unchanged save hashes; its fresh image is still
black. Another 156 original decoded call-return entries are audited into static
availability (61,706), with execution pending. This addresses exact translated
return locations without changing guest instructions or CPU return behavior.
The installed iPad remains the separately verified 60,361-entry engineering build;
recognizable mobile gameplay and audible audio are still unverified.

The 61,706-entry original-return cohort actually clears that owner and reaches a
new loader routine, with cleanup, exact save hashes and a fresh black image.
A reproducible private pipeline command now packages the byte-checked owner
recovery workflow and independently audits its first 40-entry batch. Static
availability reaches 61,746; the new native execution is pending. This still does
not establish recognizable mobile gameplay, sound or character-progress loading.
[Exact scope](../ios/game/README.md#reusable-observed-owner-recovery--2026-11-15).

The next actual case exposes an incomplete earlier owner inventory. Reusing the
existing import/ABI no-return policy validates its original switch and following
call paths; all 20 new first-block entries pass independent byte/decoder audits
and publish, growing availability to 61,766. The external original failure-callee
body is absent, full guard proof remains ungranted, and runtime semantics are
unchanged. The next actual test is running; the latest inspected image is black.
[Evidence and qualification scope](../ios/game/README.md#local-tables-and-the-existing-no-return-policy--2026-11-15).

### Mobile backend and input diagnostics update — 2026-11-15

Native AOT startup now executes a 68,656-entry corpus (`4e7130e0`), stopping
explicitly at original `VPUNPCKLDQ`, PC `0x106f0c48`, after 227.293 seconds /
3,288 presentations. All 43 workers join, renderer resources clean up and
original/copied saves match. The requested frame 4,000 is not reached; no new
capture is claimed. Two separately checked local tables expose ten case targets
and 180 newly qualified first blocks, without claiming full guard/index proof.

OpenCrown's packed SIMD patches are qualified on an isolated twenty-two-patch
backend: 44 routines, 9,280 vectors, 10,616,320 word checks and 408,320
return/read-only input checks pass with Odin's actual runtime. Float sticky
exception status and #XM delivery remain excluded. Whole-game LLVM still needs
fresh lifting before this backend can run the game. Older backends and the
installed iPad engineering app remain intact. The separate synthetic comparison
app passes the same 10,616,320 word checks on the physical iPad mini A20 Pro
(iPadOS 27.2.1), with matching fixture/backend/oracle hashes and verified owned
process cleanup. The game app and its profile are untouched.

A reporting correction preserves old evidence: zero scripted-fallback samples
does not prove zero guest pad polls because physical input has priority. The
old total is unknown and the scripted acceptance gate still fails. New counters
distinguish total/primary/fallback/disconnected reads; explicit script-only mode
makes future engineering runs deterministic without changing normal defaults.
Mobile scenes, audible game output, physical controls and character-progress
save/load remain open. See [current mobile status](../ios/game/README.md).

### Fresh native checkpoint and eboot import recovery — 2026-11-15

The twenty-two-patch fresh 68,656-entry corpus (`eb947934`) passes the prior
VPUNPCKLDQ stop and reaches uncompiled PLT `0x107d5370` after 132.145 seconds /
3,544 presentations. All 43 workers join, memory fault is zero, renderer cleanup
and original/copied save hashes pass. Explicit script-only delivery is now
observed: 7,442 valid fallback pad reads, 582 active reads, no primary or
disconnected reads. Changed inputs prevent timing comparison. Frame 4,000 is
not reached, so no fresh image is claimed. Destination libc `modf` is already
translated. This is progress beyond the old instruction stop, not game acceptance.

Fifteen missing declared eboot PLT first blocks are qualified against original
ELF metadata, readonly bytes, prepared GOT bindings and independent six-byte
decoding. Production-runtime guarded jump/fault fixtures pass 60 cases. An
isolated archived-data copy proves interruption recovery through the actual
consumer prefix and corrupted-staging refusal, with original hashes preserved.
Frozen v2 publication adds 15: 68,671 available. Subsequent actual replays reach
`0x107368a1` (68,671 entries), `0x106b9cbf` (68,672) and `0x106b9ccc`
(68,677). The last runs 182.839 seconds / 3,841 presentations with 43 joins,
no fault, cleanup and unchanged player/copied saves. Its visually inspected
display-frame-2,500 PNG is black, as is the earlier 68,672 game PNG. Three
timed captures show identical clear-compute/depth-only passes, no recorded
color target/sampled image. Converted PCM has 448,896 nonzero samples beginning
around 167 seconds; this is not audible-device or recognizable-scene acceptance.
Explicit missing-block roots refresh the unchanged OpenCrown CFG validator
inventory; interrupted worker PCs are excluded as anchors. Three further
original blocks independently qualify and publish, preserving old raw hashes:
68,680 available. The controlled same-backend replay requested later display
frame 3,500 and draw/audio diagnostics; its result is recorded below. Prior
frozen evidence is `packed-simd-discovery/checkpoint-owner-68677/` and adjacent
`owner-68677-diagnostics-review.json`; new availability is in
`observed-owner-batches/after-owner-68677/`. First native
checkpoint evidence is `packed-simd-discovery/checkpoint-first/`; PLT evidence
is `plt-recovery-after-packed-simd-v2/` and
`plt-publication-interruption-test-v2/`. The 25-patch candidate and physical
game update remain separate; recognizable scenes and mobile gameplay stay open.

The 68,680 replay then reaches `0x106e585f` in a different owner after
196.426 seconds / 3,849 presentations, with all 43 joins, no fault, cleanup
and unchanged saves. Display frame 3,500 is visibly black; timed draws remain
clear/depth-only. Converted PCM contains 286,592 nonzero samples, without
audible-device acceptance. One further original five-instruction block qualifies
and publishes: 68,681 available. Next bounded replay requests frame 4,000
and later timed draw captures. Latest evidence:
`packed-simd-discovery/checkpoint-owner-68680/`,
`owner-68680-diagnostics-review.json`, `observed-owner-batches/after-owner-68680/`.

Actual 68,681 replay (`00d0da75`) subsequently reaches `0x1067aba7`: 187.826
seconds / 3,920 presentations, all 44 workers joined, no fault, cleanup and
unchanged saves. Late captures now show textured color draws, including 77
into a display target, but requested frame 4,000 is not reached. This is command
progress, with no new image or recognizable-scene acceptance. Converted PCM
contains 1,144,896 nonzero samples, without audible-device acceptance.

The producer now pins the exact observed joined count (14 generation/rejection
checks on actual 43/44-worker archives and malformed task-owned copies pass).
129 further original blocks / 450 instructions qualify and publish, old raw
hashes preserved: 68,810 available. Core `dcd37af` adds bounded repeat screenshots
to avoid missing late startup transitions: 85 normal plus 85 sanitizer checks,
six independent PNG decodes per mode and both SDK header compiles pass. Exact
source transition verifies all 481 captured inputs and changes only two GPU
diagnostic files; CPU backend/runtime unchanged. Controlled replay
`odin-packed-simd-owner-68810` requests display frames 1,000..16,750 every 250,
at most 64 PNGs, plus timed draws/PCM. Sequence output is recorded below.
Latest evidence: `packed-simd-discovery/checkpoint-owner-68681/`,
`owner-68681-diagnostics-review.json`, `observed-owner-batches/after-owner-68681/`,
`owner-worker-count-tests/`, `snapshot-sequence-tests/`, `snapshot-sequence-integration/`.

The 68,810 sequence yields fourteen independently decoded PNGs. Frames
4,000/4,250 visibly show the native title menu with Load highlighted, then
a fade. This is native-host pixel evidence, not physical iPad or full gameplay
acceptance. The player reports lots of popping static. Only the owned process
is silenced and terminated after retaining partial images/PCM/log/binary;
original/copied saves remain unchanged. No clean checkpoint or cleanup is
claimed. Evidence: `packed-simd-discovery/interrupted-owner-68810-noise/`.

Silent timer control `odin-packed-simd-owner-68810-silent-02` completes with PCM and
display captures on unchanged core dcd37af. Initially separate audio candidate
6346f40 reuses Crown's 256-frame Apple SDL preference and adds verified host
gain zero for real-device pacing tests. Zero-PCM hardware checks verify baseline
1,024 / candidate 256 / explicit override and mute; both SDK runtime compiles
pass. Game audio quality remains unverified. No further audible engineering
playback is needed during this investigation; installed apps/devices are unchanged.

The timer control completes 337.345 seconds / 4,532 presentations with 44 joins,
no fault, cleanup and unchanged saves; missing `0x10736894`. Fifteen PNGs
decode; visual review verifies startup/title/copied Data 1 metadata, not loaded
character progress. Exact offline downmix confirms source gaps persist without
device output, so smaller device buffers alone are not an established fix. One
further original five-instruction block qualifies/publishes: 68,811 available.
After this clean checkpoint/publication, audio candidate merges as 9bf8939,
changing only runtime_audio.c among captured inputs. Host-muted real-CoreAudio
comparison `odin-packed-simd-owner-68811-muted-01` is running with retained PCM
and display captures; no new sound-quality result yet. Evidence:
`packed-simd-discovery/checkpoint-owner-68810-silent-02/`,
`owner-68810-silent-02-visual-review.json`, `silent-audio-pcm-comparison/`,
`observed-owner-batches/after-silent-68810/`, `audio-quantum-integration/`.

The 68,811 host-muted comparison completes 535.466 seconds / 4,618 presentations,
44 joins, no fault, cleanup and unchanged saves; it reaches missing local-clock
RTC service 0x60056000. Viewed captures show the Data 1 load confirmation with
YES selected. Source PCM gaps persist, so the buffer preference is not a full
fix. Checked RTC implementation 5d205d1 reuses Crown's local date calculation:
158 checks per CPU/sanitizer mode, both SDK compiles, 4,378 integrated service
and four worker-cleanup checks pass. Only guest_clock.h/runner.cpp change among
captured inputs; CPU/backend/audio runtime remain unchanged. Host-muted RTC
replay is active; actual service use is still pending. Latest evidence:
`packed-simd-discovery/checkpoint-owner-68811-muted-01/`,
`owner-68811-muted-01-visual-review.json`, `audio-quantum-muted-comparison/`,
`rtc-local-clock/`.
