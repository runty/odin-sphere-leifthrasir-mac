# Odin Sphere on Apple silicon

The reference target is a local offline Mac app running the CUSA05083 US base build under Rosetta. Fork baseline `d2a9d8a253dd17680c5349cae0e1735c67fe2455` is upstream v0.2.3. Keep `main` at the upstream baseline and develop on `codex/macos-port`. The initial host is an M5 Max running macOS 27.2.1. Other upstream profiles need their own verification.

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
- [ ] **P2, module startup.** Load game data into an explicit guest address space, route pointers and imports through the signed runtime and repeat M2. Measure peak memory and guest work per frame. Re-estimate mobile delivery at the first real transition.
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
