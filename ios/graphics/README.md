# iPhoneOS Vulkan-on-Metal experiment

The KosmicKrisp driver compiles as arm64 iPhoneOS, minimum iOS 27.2, with
only Apple/system runtime dependencies. Fresh clean iPad `b1b10cc` launches 18
and 19 execute actual 64-word Vulkan fill, fence and mapped GPU readback on the
A20 Pro, plus real UIKit Vulkan surface/capability checks. CPU 414 and native
memory 19 also pass. iPhone retains `1376d6a`; foreground launch is denied while
locked, so fresh iPhone driver
execution is unverified. No mobile game draw/presentation is established. Keep
the working Mac app/driver unchanged.

The source is Mesa `b628375fb1fd99476bfbaaa0532deb6fe3a6870e`, matching the Mac
KosmicKrisp superbuild. `mesa-performance.patch` retains the SpartanPort
`76830ef10f99cf5ca26f0cd2e41a4ab83207ae5f` driver changes. `mesa-ios.patch`
uses public iOS `vm_map`/`vm_remap`/`vm_deallocate` interfaces and advertises FIFO
presentation on iOS, where CAMetalLayer displaySyncEnabled is unavailable.
macOS/Catalyst retain their existing immediate-presentation behavior. Alias
mapping and actual presentation still require runtime checks.

## Private build

```sh
python3 ios/graphics/build-driver.py --root "$WORK/ios-graphics" \
    --host-tools "$MATCHING_MESA_BUILD_TOOLS" --meson "$MESON"
```

The native shader tools must come from the same Mesa revision. The builder
reads them without modifying the shared SDK and records their binary hashes.
It clones its own Mesa source, audits the complete patch diff with a temporary
Git index and writes iPhoneOS outputs under its own external build root.
`--configure-only` stops before compilation. Driver/source/input manifests and
all generated shader/compiler outputs remain outside Git.

The reference shared shader tools had a missing Homebrew translator runtime.
Do not repair or replace shared libraries under another running project. A
private, hash-pinned Homebrew bottle can supply that host-only runtime:

```sh
brew fetch --force-bottle spirv-llvm-translator
python3 ios/graphics/prepare-host-runtime.py \
    --bottle "$(brew --cache --force-bottle spirv-llvm-translator)" \
    --output "$WORK/ios-graphics/host-runtime-checked"
python3 ios/graphics/build-driver.py --root "$WORK/ios-graphics" \
    --host-tools "$MATCHING_MESA_BUILD_TOOLS" --meson "$MESON" \
    --host-runtime "$WORK/ios-graphics/host-runtime-checked/spirv-llvm-translator/23.1.3/lib"
```

The helper accepts only the recorded 23.1.3 arm64_golden_gate bottle hash,
extracts to a new external prefix, rewrites its LLVM path and signs that private
copy. A newer bottle is rejected. The driver builder relocates private copies
of the native shader tools to that library, leaving originals untouched. This
avoids relying on DYLD variables that system script launchers can strip.

Next: qualify swapchain presentation, then replay an actual captured Odin draw
and integrate title/battle presentation.
Synthetic draws and Simulator results must stay separate from physical game
frames. Module/thread/TLS/fiber, audio/input, Files import and save lifecycle
remain independent work before a playable mobile app exists.


## Current runtime boundary

The device framework packages and a signed UIKit probe links successfully with
it. The probe's offscreen check allocates a Vulkan buffer, fills a nonuniform
32-bit pattern on the GPU, waits on a queue fence, invalidates mapped memory
and verifies all 64 words. This is not game rendering or display presentation.
`probe.mm` is also usable in a native Mac CLI against an arm64 Mac build of the
same source. That path passes all 64 words on the M5 Max; the report explicitly
sets `game_frames=false`. The same 64-word check also passes on physical iPad;
it does not establish a rendered game frame.

The iOS 27.2 Simulator SDK omits the required Metal 4 encoder protocols. The
Simulator driver build therefore stops explicitly; headers are not substituted
from another platform. CPU/startup/lifecycle checks can use Simulator, while
rendering qualification requires hardware. The builder also accepts
`--sdk macosx` in a separate work directory for native arm64 GPU adapter checks.
These results must not be attributed to an iPhone or iPad.

```sh
python3 ios/graphics/package-driver.py --root "$WORK/ios-graphics" \
    --headers "$VULKAN_SDK_INCLUDE"
bash ios/build-probe.sh "$WORK/ios-probe-gpu" "$DEVELOPMENT_TEAM" \
    "$WORK/ios-cpu/fixtures" "$WORK/ios-game" "$WORK/ios-graphics"
```

The packer changes only a private copy, applies a framework install name,
records input metadata/notices, copies Vulkan headers and signs ad hoc; Xcode
then signs the embedded framework with the app's development identity. It
retains old frameworks before replacement. The driver builder audits platform,
arm64 architecture, system dependencies and unresolved strong symbols. The
CoreGraphics dependency is explicit; optional weak Vulkan entrypoints retain
the driver's existing dispatch behavior.


The source includes a repeatable native CLI check:

```sh
python3 ios/graphics/test-native.py --root "$WORK/ios-graphics-native" \
    --headers "$VULKAN_SDK_INCLUDE"
```

The isolated iPhone Simulator CPU/startup app passes all 414 routine checks,
returns zero from both modules and matches the 624-trace / 1,499-dispatch /
7,419-checkpoint stop at module ID 7. Its Metal 4 query is `null` with an
explicit SDK-unavailable status; no unsupported enum or header is substituted.
No Simulator graphics-driver success is claimed.


## Native arm64 game-frontend initialization

The game renderer itself now builds and links as native arm64 macOS. The draw
pipeline, binding helper, recorder and profiling paths use architecture-specific
host ticks and spin hints; x86 keeps its existing unfenced counter. Darwin's
x86 signal-register layout is exposed only on x86, and the x86 guest-register
write-log walk is explicitly unavailable for an arm64 renderer fault. The
shared frame reader uses public `vm_read_overwrite` on iOS.

`OSL_BUILD_ARM64_GPU_PROBE=ON` adds the isolated `osl-native-gpu-probe` target
without linking or executing the x86 loader. On the M5 Max it creates a main-
thread SDL window, selects the audited native KosmicKrisp driver, creates the
Vulkan/Metal surface and presenter, registers 585 GPU/event symbols, and opens
and closes a real VideoOut handle. A bounded SDL quit exits successfully.
**Zero guest draws, submissions or presented frames are established.** This is
native Mac engineering evidence; it has not executed on iPhone or iPad.

The first link experiment exposed an x86 Zycore archive through the shared SDK.
A private native Zydis 4.1.1/Zycore build fixes that; the final link has no ignored
architecture libraries. Current native frontend dependency builds use Homebrew
SDL 3.4.20/fmt 12.2.1/xxHash 0.8.4/PNG 1.6.59/Vulkan 1.4.363 and are development
inputs, not the pinned release dependency reproduction. They do not replace the
installed Mac alpha or modify another project's SDK.

`osl-gpu-worker-tests` exercises 20,000 binding-helper handoffs and 180,000
variable-size draw-ring packets, crossing the ring capacity repeatedly, with
idle sleep/wake and destructor checks. Both arm64 and Rosetta x86 pass; the
x86 triangle-fan regression also passes 16,376 cases. Private evidence lives in
`native-gpu-arm64/frontend-verification.json`, `gpu-worker-verification.json`
and the corresponding build/run logs below the Odin build root.

Next connect the checked AOT guest and shared GPU mappings to this renderer,
then port its surface/window and dependency interfaces to UIKit/iPhoneOS. The
existing driver-only buffer readback result and this frontend initialization
result do not establish a rendered game frame.

## Checked guest connected to the native renderer

`osl-aot-gpu-probe` now connects the checked arm64 AOT startup library to the
real renderer through `MobileGpuServices`. It uses the prepared image's SDK
version, creates SDL/Metal on the main thread, and runs the guest on a worker.
VideoOut open, event-queue creation, flip/vblank event registration, buffer
attributes and buffer registration have typed, checked argument adapters.
Seven-argument attributes read the seventh SysV argument from the checked
guest stack. Unsupported formats, layouts or buffers outside the shared native
GPU mappings stop explicitly. Other GPU calls are still unsupported.

The real guest opens VideoOut and registers three 1920×1080 buffers, two queues
and two events. Session teardown finishes the currently supported GPU work,
closes the port, removes owned queues, unregisters GPU mappings while their CPU
memory is still valid, and releases the direct-memory pool and event flags.
No guest draw/submission or mobile game frame is established. General Gnm
submission will require full producer/presentation quiescence beyond this
initialization-only shutdown contract.

Native bridge checks cover 60 cases, including protected/misaligned/canary
outputs, the seventh stack argument, a real vblank event, invalid layouts,
shared buffer registration and cleanup. They also compare the renderer's TSC
counter/frequency pair with wall time: the native arm64 nanosecond counter must
not be paired with raw hardware-counter ticks. A small synthetic buffer can be
presented by this test; it is explicitly not guest execution or a game frame.

GPU-backed discovery requires an isolated native arm64 renderer build and an
explicit native driver manifest:

```sh
python3 ios/game/advance.py --root "$WORK/ios-game-vpermilps" \
    --remill "$WORK/cpu-backends" --identity --rounds 64 \
    --gpu-build "$WORK/native-gpu-arm64" \
    --gpu-driver-manifest "$WORK/native-gpu-arm64/kosmickrisp-native.json"
```

Set `OSL_AOT_ROOT` and `OSL_REMILL_INCLUDE` when configuring the isolated
renderer build. Each discovery round links the exact checked startup objects,
then records renderer, executable, driver and loader hashes. The runtime input
ledger records App0's resolved path and eboot hash; it does not hash every
asset. Only the native host has been provisioned with the read-only real App0
tree. Simulator and physical probes have not imported those assets.

After discovery has stopped, repeat the frozen integration checks with:

```sh
python3 ios/graphics/test-aot.py --root "$WORK/ios-game-vpermilps" \
    --build "$WORK/native-gpu-arm64" \
    --driver-manifest "$WORK/native-gpu-arm64/kosmickrisp-native.json" \
    --output "$WORK/new-aot-gpu-check"
```

The script rejects changed source/build inputs and a checkpoint that changes
during verification. Stop discovery before running it.
Its result is native macOS evidence. The default mobile archive has no GPU
callback provider and retains its explicit VideoOut stop; exporting a corpus
whose host reference used the GPU does not link that frontend into UIKit.

The frozen native-host checkpoint has 927 translated entries, 2,938 dispatches
and 30,174 checkpoints. Both initializers return zero, no memory fault is
recorded and the next missing service is `scePthreadCondInit`. Corpus:
`56494c9841212760f7f43bd409fffa98c952d0d11daf6b06e40b51e54250941e`.
The repeatable bridge and guest checks pass in `ios-aot-gpu-frozen-checks/`.
This remains an initialization checkpoint with zero guest frames.


The later native AOT integration now starts and joins four actual guest workers.
Its 1,022-entry corpus passes a frozen GPU repeat with zero guest frames; all
workers finish before VideoOut/event deletion and shared direct-memory reset.
Guest/GPU TSC ticks now come from one raw-nanosecond provider, with matching
1 GHz frequency and a shared relative process epoch. CPU/provider failure
fixtures pass address/thread sanitizers; guest IR and GPU driver are not covered
by that sanitizer claim. Read [current worker qualification](../game/README.md#owned-guest-workers-clocks-and-failure-cleanup)
for exact artifacts and remaining service/rendering boundaries. This remains
native Mac engineering evidence, not an iPhone/iPad frame or gameplay result.

### Early-stop discovery versus qualification

`osl-aot-gpu-probe ... --discover` permits a bounded CPU stop before VideoOut
has been created. Its exit status checks cleanup of any resources that actually
exist; the real guest error remains in the report, and `discovery_only:true`
explicitly identifies the mode. `advance.py` uses this while rebuilding a new
corpus. It is not renderer qualification or game boot success.

The normal mode retains its VideoOut/tracked-mapping requirements.
`test-aot.py` rejects a discovery-mode cleanup report and still requires three
real 1080p buffers and the full cleanup contract. An isolated 1,028-entry
reciprocal-backend early checkpoint was tested both ways: normal exit 1,
discovery exit 0, identical missing block and no resources/frames, all cleanup
complete. The same newly built renderer passes all 60 actual bridge cases.


## Isolated SDL UIKit dependency

`build-sdl.py` builds SDL 3.4.18 from the existing checked archive lock into a
separate iPhoneOS or Simulator prefix. It audits archive/source hashes, uses the
actual selected iOS SDK and arm64 target, and checks every archive member's Mach-O platform so a
Mac arm64 library is not mistaken for an iPhoneOS one. No shared SDK or other
project's files are modified.

```sh
python3 ios/graphics/build-sdl.py --root "$WORK/ios-sdl" --sdk iphoneos
python3 ios/graphics/build-sdl.py --root "$WORK/ios-sdl" --sdk iphonesimulator
```

The configuration supplies UIKit/Metal views, audio and controller primitives;
SDL's own GPU/render/OpenGL/Vulkan-loader backends are disabled. The title uses
its separate Vulkan-on-Metal renderer. A built static library does not create a
UIKit window or link the full game frontend. Next qualify main-thread UIKit
window/Metal-layer ownership, a direct bundled ICD route, the remaining iOS
renderer dependencies and actual VideoOut/Gnm calls. Preserve separate synthetic
surface/readback and real guest-frame evidence.

## UIKit/SDL Metal-layer ownership probe

Set `OSL_PROBE_SDL_PREFIX` to the matching audited SDL prefix when calling
`ios/build-probe.sh`. The app copies and validates that static archive, links
its Apple framework dependencies and calls the surface probe on UIKit's main
queue after startup diagnostics. A hidden SDL UIKit window creates a Metal view
and nonzero drawable extent, then destroys both and releases its video lease.
The original app key window must remain unchanged. Wrong-platform prefixes and
changed archives are rejected before Xcode project generation.

Clean `7c25fe7` passes twice on owned Simulator (launches 20/21,
1320×2868) and actual iPad (launches 15/16, 2316×1512), preserving both original
input files. CPU 414 and native memory 19 pass; iPad GPU readback remains 64/64.
Those clean checks cover UIKit/Metal-layer lifecycle, without a Vulkan surface
or presentation. iPhone retains the bounded-memory probe, with launch denied
while locked; no new iPhone execution is claimed.

When the bundled GPU driver is also linked, the surface probe negotiates its
ICD directly, verifies/enables KHR surface and EXT Metal surface extensions,
creates `VkSurfaceKHR` from the still-live SDL Metal layer, and queries surface
capabilities, actual formats, FIFO mode and graphics/presentation queue support.
It destroys the Vulkan surface/instance before releasing the layer/window.
Clean `b1b10cc` passes twice on the physical iPad (launches 18/19), preserving
preexisting inputs: five formats, FIFO,
queue 0, three-image minimum/maximum and matching 2316×1512 extent. Original
UIKit window and SDL cleanup still pass. Simulator builds without a GPU driver
retain the Metal-layer-only path because its SDK lacks required Metal 4 types.
That clean path passes Simulator launches 22/23, with CPU/memory/startup checks
and preserved inputs.

Diagnostics explicitly report `presentation_tested:false` and
`game_frames:false`. This is actual surface creation/capability qualification,
not swapchain image acquisition, presentation, a guest draw or the full mobile
game frontend. Next connect the native renderer while preserving main-thread
ownership, resource lifetime and foreground constraints.

## Isolated renderer support libraries

```sh
python3 ios/graphics/build-dependencies.py --root "$WORK/ios-renderer-deps" --sdk iphoneos
python3 ios/graphics/build-dependencies.py --root "$WORK/ios-renderer-deps" --sdk iphonesimulator
```

`dependency-lock.json` pins fmt, xxHash, Zycore, Zydis and header-only robin-map.
The builder clones private sources, rejects changed pins/source edits, serializes
builds in its root and installs each SDK to its own prefix. Package/include/
library discovery stays within that target prefix and SDK; host tools remain
host executables. A generated CMake include supplies the legacy `Zycore` alias
expected by this Zydis pin without editing either dependency's source.

Both SDK builds pass: every object in fmt (2), xxHash (1), Zycore (13) and
Zydis (18) is audited for the appropriate iOS/iOS Simulator Mach-O platform,
in addition to arm64 architecture. Manifests retain hashes, pins and options.
These support-library builds do not link or execute the complete mobile game
renderer. The later renderer checkpoint below adds PNG, Boost and VMA. Runtime frontend
integration still needs qualification; Mac caches remain intact.

## Full mobile renderer archive and consumer link

`build-dependencies.py` now includes libpng 1.6.50 from the existing Mac archive
lock, using the target SDK's explicit zlib header/library. All 18 PNG objects
per SDK are platform-audited (52 support-library objects total). An optional
`--archive-cache` reads the hash-pinned archive without modifying that cache.
Boost 1.92.0 headers come from the SHA-pinned release archive in
`archive-lock.json`; VMA 3.1.0 comes from its pinned Git commit. Their source
inventories/notices are retained in the private prefix. New Git fetches request
only the exact shallow revision; no shared project's dependency tree is edited.

```sh
python3 ios/graphics/build-renderer.py --root "$WORK/ios-renderer" \
    --dependencies "$WORK/ios-renderer-deps" \
    --sdl-prefix "$WORK/ios-sdl/prefix-iphoneos" \
    --headers "$WORK/ios-graphics/Headers" --sdk iphoneos \
    --driver-root "$WORK/ios-graphics"
```

`OSL_BUILD_IOS_GPU_LIBRARY` builds the real renderer, GnmDriver, VideoOut and
shader recompiler without the desktop launcher/x86 loader. iOS uses the signed
bundled ICD directly in both renderer and device-capability enumeration; the
existing Mac dynamic loader path is retained. SDL UIKit is accepted as a Metal
window backend, with main-thread ownership and high-density sizing. This does
not supply a UIKit session controller or lifecycle by itself.

Both SDKs compile all renderer archives: oslgpu 149 objects, miniz 4, sirit 19
and keys 1; all 173 objects per SDK have matching Mach-O platforms. The optional
matching signed driver links a complete iPhoneOS consumer using the real
allocator and clock/memory services, without unresolved renderer symbols.
Its Mach-O platform is checked. That executable is **not installed or run**;
it is a link/ABI check, not a UIKit app or device-renderer initialization.
Simulator compiles the archive without a driver/consumer link, since its SDK
still lacks the required Metal 4 driver protocols. No driver is substituted.

Builder manifests retain source/header hashes, archive hashes and native shader
tool identity. Inputs are checked before publishing; wrong-SDK SDL and a changed
private-copy archive are rejected before configuration. No game data or saves
are used by these library/link checks. Next: a nonblocking UIKit renderer session,
actual physical initialization/swapchain ownership, guest GPU callback wiring,
asset import and presentation. No mobile game draw/frame is established.

## Polled renderer session and explicit shutdown

The Apple C API now supplies `oslgpu_poll_main_thread()` without the idle wait
and `oslgpu_shutdown_main_thread()`. Callers must stop/join guest CPUs and close
their queues first, retaining a foreground drawable while GPU work drains.
Shutdown stops kernel/GPU/presentation workers, clears IRQ handlers and guest
mapping callbacks, synchronizes the pipeline cache, releases overlay/device
resources and destroys the SDL window. iOS bypasses desktop display-mode sync.
Heavy-content background/resume behavior is not qualified by this API check.

Real native Mac lifecycle tests pass three init/open/close/poll/shutdown cycles,
reject wrong-thread polling/shutdown and duplicate starts, verify no named
session workers remain and all SDL video/gamepad leases are released. All 192
queued private cache records are read back byte-for-byte after shutdown. The
same three cycles pass with the core instrumented by AddressSanitizer; driver/
system libraries are uninstrumented, and leak sanitizer is disabled. Tests
exit normally, without relying on process termination for renderer cleanup.
No guest code/game frame is involved in this lifecycle check.

Testing found and fixed a stale IRQ registration across restarts, Apple warm-up
workers previously left parked, the cache I/O worker retained until process exit,
and cache close stopping before accepted writes completed. Other-host warm-up
workarounds are retained. Generated diagnostic records remain in disposable
private profiles; real game saves/caches are untouched.

Set `OSL_PROBE_RENDERER_ROOT` alongside matching GPU/SDL inputs to build the UIKit
session check. It uses CADisplayLink for 30 polls, native VideoOut open/close,
explicit teardown and original key-window restoration. Stages are persisted
before initialization for failure diagnosis. The inactive observer stops this
short diagnostic on loss of foreground; this does not qualify full gameplay
background/resume. The signed engineering candidate builds as separate
`com.wadscorp.odinsphere.renderer` / “Odin Renderer Check” using optional
`OSL_PROBE_BUNDLE_ID` and `OSL_PROBE_DISPLAY_NAME`, keeping the normal probe's
container independent. Renderer archives/support inputs and source hashes are
validated before packaging. Both mobile SDK libraries/phone consumer link pass.
The UIKit candidate is staged only, not installed or device-executed; hardware
validation follows when it can preserve the other active game session.

## Physical renderer bootstrap boundary

The standalone UIKit renderer check now initializes the complete renderer on the
physical A20 Pro: real device/swapchain/presenter, native VideoOut open/close,
30 main-thread polls, explicit shutdown, SDL lease release and restoration of
the original key window. CPU 414, native memory 19 and GPU readback 64 still pass.
Clean `4c2cbcf` now passes physical launches 6/7 with distinct process IDs and
verified previous-process absence; no guest code or game frame is established
by this check. Existing normal probe data remains in a separate container; both original input
hashes are preserved, and the standalone renderer process is force-stopped only after its
passing report, with fresh PID/bundle-path matching and an absence recheck.

The first full-device attempt terminated at a null instruction address. The
bundled ICD does not supply the loader's instance-layer enumerator; the old
renderer queried it even when requesting no layers. Empty layer requests now
return without that optional API; explicit requested layers require a real
enumerator. Required bootstrap function pointers are checked and their presence
logged. Hardware reports `layers=false` and then completes initialization with
the correction. No synthetic layer-enumeration success or signing bypass exists.
Both SDK libraries/consumer linking and native normal/ASan lifecycle checks pass.

UIKit still logs appearance-transition warnings when the temporary SDL window
returns to the diagnostic view. Moving teardown outside the display-link callback
did not remove them; the simpler checked ownership path is retained. This is an
open UI integration issue, and gameplay/background/resume remains unqualified.
`UIApplicationSupportsIndirectInputEvents` is now declared for SDL mouse support.
An earlier SIGTERM only acknowledged signal delivery and left the old normal
probe resident; teardown verification now checks actual process absence.

## UIKit guest connection — 2026-11-14

The separately signed `com.wadscorp.odinsphere.startup` app now executes the
actual guest with the real iPad A20 Pro renderer. Clean `0dea990` cold launches
1/2 complete both module initializers, start/join four game-created workers,
register three 1080p buffers and release two native queues and all tracked GPU
mappings. Clean `d3d876f` repeats normal startup on launch 7, without a test gate
or injected stop. CPU 414, memory 19, GPU readback 64 and real surface checks
remain green. This establishes guest-to-renderer integration, **not game frames**:
presentation stays at zero and execution stops at missing translated block
`0x106af238`. Mobile game input/audio/save loading and actual draws remain open.

`guest-session.cpp` owns the shared checked VideoOut adapter and optional
frontend cancellation flag. UIKit validates the full image/relocation hashes
before reading the actual procparam SDK (`0x03008041`), initializes on main and
runs guest execution on an owned C++ thread. After the guest report, main joins
that thread before stopping the renderer, restoring previous SIGSEGV/SIGBUS
handlers and deleting its context. Guest cleanup itself joins child CPUs, drains
GPU work, closes queues and releases mappings in that order. A scene transition
requests cooperative cancellation; if inactive, renderer teardown waits for a
foreground return. Expiring background time ends its task lease after requesting
stop. This is a bounded startup lifecycle, not gameplay pause/resume.

Actual iPad launch 3 passes cancellation after buffer registration. Launch 6
uses an explicitly injected diagnostic pause at seven real GPU callbacks, then
activates Settings. The same process acknowledges guest join in the background,
returns to the foreground and completes renderer/window/signal cleanup. This
controlled case has three buffers and no child guest threads yet; the owned
frontend thread is joined. Normal cold launches independently cover four child
workers. The gate is at most one second and respects the guest deadline; it
is never enabled by default and its timing is not performance evidence.

Native real-renderer fixtures cover normal/pre-stop/live-stop sessions, rejected
reuse/wrong-thread ownership, prior-handler chaining/restoration and five image/
pointer validation cases. Boundary/fixture AddressSanitizer also passes; renderer,
game archive and driver/system libraries are uninstrumented in that test. Separate
actual HLE/core ASan and TSan pass 4,322 service checks and three exception/stop
fixtures, including an external flag interrupting a running checked worker. Guest IR
is uninstrumented. UIKit appearance-transition warnings persist on restoration,
although key-window identity and resource-release checks pass.

Build the separately audited SDK boundary before packaging a renderer session:

```sh
python3 ios/graphics/build-guest-session.py --root "$WORK/ios-guest-session" \
  --game "$STARTUP_CORPUS" --remill "$BACKEND" \
  --renderer "$WORK/ios-renderer" --dependencies "$WORK/ios-renderer-deps" \
  --headers "$WORK/ios-graphics/Headers" --sdk iphoneos
OSL_PROBE_RENDERER_ROOT="$WORK/ios-renderer" \
OSL_PROBE_GUEST_ROOT="$WORK/ios-guest-session" \
OSL_PROBE_SDL_PREFIX="$WORK/ios-sdl/prefix-iphoneos" \
OSL_PROBE_BUNDLE_ID=com.wadscorp.odinsphere.startup \
OSL_PROBE_DISPLAY_NAME='Odin Sphere Startup' \
  bash ios/build-probe.sh "$WORK/ios-uikit-app" "$DEVELOPMENT_TEAM" \
    "$CPU_FIXTURES" "$STARTUP_CORPUS" "$WORK/ios-graphics"
```

Without `Documents/Guest/image.bin` this still runs the renderer-only check.
With the validated image and adjacent `image-pointers.bin`, it starts the actual
bounded guest. `Documents/Guest/App0` is its read-only game root; progress uses
its separate `Guest/MobileProfile`. Private assets were copied into the actual
iPad container and round-tripped: 33 selected files / 3,588,577,051 bytes match
the source SHA inventory; one keystone control file is excluded by the key-path
rule. Neither this transfer nor a matching input hash proves an in-game save.
The existing Mac app and earlier probe containers remain separate.

Developer diagnostics only: `OSL_GUEST_STOP_AFTER_GPU_CALLS=7` requests a stop
from a display-link poll; `OSL_GUEST_TEST_GATE=after-buffers` enables the bounded
pause for controlled scene-transition testing. Live console markers observe the
pause and the background join without adding file-transfer latency. The earlier
ungated scene attempt completed too quickly, and an initial file-based observer
timed out; neither is counted as background acceptance. Both SDK boundary
archives compile, and the default non-renderer Simulator app still builds. No
fresh physical iPhone execution or Simulator Metal 4 driver run is claimed.

### Checked shader packet writers — 2026-11-14

`20b971a` connects typed native SetPsShader, UpdatePsShader, SetVsShader,
UpdateVsShader and SetCsShader adapters in the shared `NativeGpu` path. They
use aligned local storage and copy only the fixed output footprint (PS 40,
VS 29, CS 25 dwords), preserving unwritten trailing NOP bytes even when the
caller advertises a much larger capacity. Register inputs are 12/7/7 dwords.
Native null/size/register/modifier validation order is preserved without reading
invalid guest ranges. Null PS registers disable the pixel shader as upstream.
Read-only register mappings and unaligned separate guest pointers work. Aliases
with equal relative alignment use one shared scratch canvas, preserving the
backend's sequential read/write behavior; aliases with incompatible byte
alignment stop explicitly before changing guest bytes.

The standalone `test-gnm-shader-packets.py` runs 305 checks through the actual
shared adapter against direct backend exports under ASan and UBSan. It covers
packet bytes and padding, canaries, size/null/error priority, input/output bounds,
read-only registers, modifiers and overlapping layouts. It snapshots supporting
archives before linking, so immutable packet tests can run independently of CPU
discovery. Only the checked adapter/fixture is instrumented; the supporting HLE
archive, CPU runtime, renderer, driver and system libraries are uninstrumented.
Private evidence: `gnm-shader-packet-qualified/{address,undefined}-integrated/`.

At the 1,197-entry checkpoint, the existing 109-check bridge, strict native
startup/cleanup and ASan boundary lifecycle/input-negative suite pass. Both
mobile SDK game archives and separately audited boundary archives compile.
Evidence: `gnm-shader-packet-qualified/{native-gpu,guest-session-address}/` and
`ios-guest-session-shader-packets/`. The actual guest still has zero calls to
the shader adapters (`gnm_shader_calls=0`); this qualifies their marshalling,
not their eventual use by the game. No command is submitted or frame presented.
Remaining submissions/flip, queue waits and asynchronous mapping lifetimes need
their own typed checked adapters and lifecycle verification.

### Retiring queued GPU work before mapping teardown

`99a1f93` adds a host retirement barrier to Liverpool. It queues a monotonic
ticket and acknowledges that ticket only at the end of the processor's full
draw/deferred-signal retirement phase. An earlier idle phase or a completed
queue callback cannot satisfy a newer request. This neither submits a guest
frame nor calls SubmitDone. `NativeGpu::finish` now waits for retirement and
renderer fences before closing VideoOut/deleting queues and allowing direct
memory reset. It also drains a context that never opened VideoOut. The command
processor remains alive until main-thread renderer shutdown, so mapping teardown
can still use renderer-owned services. Guest submitters must already be joined.

`test-gpu-retirement.py` qualifies a frozen renderer checkpoint and pins its
source/archive hashes to avoid mixing a new processor layout with an old native
archive. The actual native renderer passes 61 checks under ASan and UBSan:
twelve queued WriteData acknowledgements, cleanup held behind a blocked command
callback with no VideoOut, four concurrent retirement waiters, preservation of
the live mapping until retirement and release afterward. The test presents no
frame and executes no game code. Only the inline host barrier/fixture is
instrumented; the processor implementation, supporting HLE archive, CPU runtime,
renderer, driver and system libraries remain uninstrumented. Private evidence:
`gpu-retirement-qualified/{address,undefined}-pinned/`.

The same 1,210-entry native checkpoint passes 109 existing bridge checks and
normal startup/mapping cleanup, plus the separate boundary ASan bridge/lifecycle
and five input-negative cases. Both mobile renderer archives are rebuilt with
matching processor layouts: 149 renderer objects / 173 total archive members
per SDK pass arm64/platform audit. The iPhoneOS SDK consumer links the signed
ICD; Simulator has no driver link or execution claim. Both game archives and
the separate SDK boundary archives compile. This is host/SDK evidence, not an
updated physical app or a game-frame/submission result. Typed guest submissions,
blocking queue waits and gameplay lifecycle are still open.

### Typed guest submissions and diagnostic flips

`0977a20` connects SubmitCommandBuffers, SubmitCommandBuffersForWorkload,
SubmitAndFlipCommandBuffers, SubmitAndFlipCommandBuffersForWorkload and
SubmitDone through exact native function types. The adapter snapshots unaligned
guest descriptor tables, preserves native size validation and zero-count
non-flip initialization, and checks each nonempty DCB/CCB against aligned,
permitted shared GPU mappings. Only tables are copied by the adapter. Callers
retain command/label mappings through GPU completion; final Session teardown
uses the processor-retirement barrier before releasing the mapping owner.

Flip calls decode three or four additional PS4 stack arguments with checked
overflow/ranges, preserving signed 64-bit flip arguments. Only the owned VideoOut
and its successfully registered slots are accepted. The final 64-dword prepare-
flip packet and its four supported payload kinds are validated before native
patching, including writable patch/label ranges. Interrupt-label address flags
are masked as in the backend; its label write is exactly four bytes. Explicit
guards cover the backend's zero-count flip underflow, inconsistent CCB pointer
pairs and a 4,096-buffer adapter budget. Unsupported ranges stop explicitly.

`MobileGpuServices` now forwards the HLE state/memory wait scope. Native GPU
waits release both leases and restore them on return or C++ exception. Host
cancellation is checked before and after the native call, outside native locks.
The callback interface change requires matching game/frontend archives. Four
worker cleanup fixtures, including dispatch-level lease forwarding, and 4,336
service checks pass ASan/TSan. This does not add bounded cancellation of a hung
GPU stream or implement guest Equeue waits.

`test-gnm-submissions.py` passes 245 checks under ASan and UBSan through the
shared adapter: multi-buffer/CCB WriteData acknowledgements, read-only commands,
descriptor/mapping/stack/slot bounds, real SubmitDone blocking while another CPU
acquires both leases, post-wait cancellation, and all four prepare-flip variants
through both flip ABIs. Eight fixed diagnostic buffers reach the native presenter;
flip status confirms each buffer and complete signed 64-bit argument. A label
at the final four bytes of a mapping is accepted without touching its canary.
All eight frames come from the fixture. Actual game submission/frame counts
remain zero. Only the adapter/inline barrier/fixture is instrumented; supporting
HLE/CPU runtime, renderer/processor, driver and system libraries are uninstrumented.
Evidence: `gnm-submission-qualified/{address,undefined}-bounds/`.

The 1,222-entry real startup checkpoint still joins 17 workers and releases its
three buffers/queues/mappings; bridge 109, boundary ASan lifecycle/input negatives
and shader 305 checks pass. Both SDK game and separate boundary archives compile
against the matching renderer; physical apps are unchanged. Evidence:
`gnm-submission-qualified/`, `ios-guest-session-submissions/`, and
`ios-game-uikit-next/checkpoint-submissions/` under the private build root.
CPU discovery continues toward the game's first use of these adapters. Flip-wait
packets, bounded Equeue waits and further VideoOut services remain to be connected
as encountered; visible mobile game frames and gameplay acceptance remain open.

### DWORD packet alignment and guarded presentation synchronization

`51fc298` fixes WriteData's C++ alignment for serialized PM4 streams. Its u64
address union previously imposed eight-byte alignment, while a packet following
an odd number of dwords starts on a four-byte boundary. Four-byte packing keeps
all serialized offsets/sizes unchanged; static assertions pin alignment, address
offset and payload offset. A byte-exact/canary test at a four-byte offset passes
UBSan on arm64 and Rosetta. The previous layout fails the same test on both CPUs.
Evidence: `presentation-qualified/layout-{arm64,x86_64}/` and `layout-baseline/`.

`a2ef8e2` adds checked InsertWaitFlipDone packets, native IsUserPaEnabled queries
and guarded VideoOutWaitVblank. Flip waits require the exact seven-dword capacity,
an owned port, an index within the 16 native labels and checked writable output.
Aligned scratch also supports unaligned guest output. A label does not need a
registered display buffer yet. Performance queries return the backend's retail
capability result; no profiling facility is implemented.

The original native vblank export retains its condition-variable wait. The
host-only guarded entry observes cancellation/deadlines at five-millisecond
intervals while waiting on that same condition variable, preserving notification-
driven wakeups. Its guard only observes host state under the port mutex. The
frontend releases both HLE leases via the shared `NativeGpuWait` scope, restores
them after native return and reports interruption outside native locks, without
returning host EINTR as a guest result. No guest Equeue-wait adapter is inferred:
this image directly imports VideoOutWaitVblank, with no direct WaitEqueue import.

The native ASan/UBSan presentation fixture passes 99 checks in the recorded runs:
exact packet bytes/bounds/canaries, legacy and guarded vblank counter advances,
a guard firing after an actual parked CV interval (5.765 / 6.006 ms in initial
aligned runs), state/memory leases acquired by another CPU, cancellation/deadline
handling with caller bytes/result preserved, and a real queued flip-wait packet.
For the last case, a prepared diagnostic presentation is delayed; GPU retirement
remains blocked until presentation releases the current buffer label, then the
following WriteData acknowledgement completes. Two diagnostic buffers are
presented. Native CPU flips must be prepared before blocking the command thread,
since their preparation itself queues on that thread. Initial fixture ordering
was corrected to respect this dependency.

Only adapter/inline barrier/fixture code is instrumented in those GPU runs;
native vblank/processor/renderer bodies, supporting HLE/CPU runtime, driver and
system libraries remain uninstrumented. The separate layout test instruments
the packet accesses on both host architectures. Batch/flip 245 checks still pass
under both sanitizers; bridge 109 and boundary ASan lifecycle/input negatives pass.
Both mobile renderers are rebuilt and all 173 object platform audits per SDK
pass; iPhoneOS consumer links the signed ICD, Simulator has no driver execution
claim. Matching game/boundary archives compile. Private evidence:
`presentation-qualified/`, `ios-guest-session-presentation/`, and
`ios-game-uikit-next/checkpoint-presentation/`. Actual game calls/frames and
physical apps are unchanged; further executed-path discovery remains active.

## Portable shader resource tables

The first real native guest shader setup reached the renderer's arm64 stub for
`FlattenExtendedUserdataPass`. The pass now shares its existing GVN, pointer
tree, duplicate handling and IR rewrite with an arm64 bytecode generator.
`portable_srt.h` evaluates the supported offset arithmetic and nested table
reads. Each draw refreshes its flattened resources from the current user data.

Programs have a versioned checked encoding, at most 65,536 instructions and
flat words, expression depth 128, table nesting 64 and a process code-blob
budget of 32 MiB. Decoding checks opcodes, references, buffer extents and
balanced pointer operations. Runtime also checks the program's flat extent
against the draw's buffer metadata. Registered programs are immutable.
Apple's same-process `vm_read_overwrite` supplies checked descriptor reads.
An inaccessible pointer yields zero for that load, allowing a later refresh
to observe newly populated data.

The existing persistent code slot now holds a data pointer. Invocation goes
through `PersistentSrtInfo::Walk`. x86-64 continues to execute its generated
walker. arm64 interprets the encoded program without allocating executable
memory. Pipeline cache reload registers and validates the encoded bytes.

`osl-srt-pass-tests` exercises the actual IR pass, nested pointers, dynamic
offsets, duplicate offsets, refresh and re-registration on both Mac CPUs.
`osl-portable-srt-tests` passes 386 checks and 800 concurrent walks on both
CPUs. Its arm64 ASan and TSan builds also pass. Both device and Simulator
renderer archives compile. The device consumer links against the signed ICD.
Simulator consumer-link verification remains open. Evidence is under
`portable-srt-tests/` and `portable-srt-sdk/` at the external Odin build root.

The native guest now passes the former shader assertion and continues through
additional missing CPU blocks. No game frame is established. The physical iPad now has the later `c6ab0ce` checkpoint described below.
The iPhone retains its earlier `460ecdb` startup build while away.

## Longer bounded startup discovery

The native replay subsequently reached 4,476 compiled entries and actual guest
command submissions. Three two-second attempts presented 19, 20 and 21 buffers.
A longer run presented 102 buffers before a new worker reached another missing
block. The CUA screenshot captured black output, so a recognizable game scene
still needs verification. Physical devices still have the earlier startup app.

`osl-aot-gpu-probe IMAGE PROFILE --run-ms N` accepts 100–60,000 ms.
The default remains 2,000 ms. Invalid, partial, negative and out-of-range
arguments exit before renderer initialization. `MobileGpuServices` carries
the bounded duration to the session. Dispatch and checkpoint budgets scale
with the duration, and new workers inherit the parent's checkpoint limit and
deadline. Cancellation still reaches every worker and host wait.

`advance.py --guest-budget-ms 20000 --gpu-build ...` discovers later
startup paths using twenty-second attempts. The requested duration is part of
the corpus identity. A changed duration gets an appropriately bounded process
timeout. The non-GPU path retains its original two-second duration.

Six invalid CLI durations are rejected. The extended real run reaches the
19th worker beyond the old two-second window. The worker ownership fixture,
including inherited checkpoint limits and original AOT strlen workers,
passes 172 checks under both ASan and TSan. Evidence is in
`native-first-frames/` at the external Odin build root.


## First physical guest submissions

Signed `c6ab0ce`, corpus `c45c17cc` with 4,658 entries, ran twice on
the iPad mini A20 Pro. Cold launches 11/12 submitted 95/105 batches and
presented 19/21 buffers. Both passed CPU 414, memory 19 and readback 64,
joined all 18 workers and released buffers, mappings, queues, fault handlers
and the UIKit window. Both stopped at the checkpoint budget. Their isolated
MobileProfile bytes match the pre-update backup. Neither run proves image
content, a playable scene or new in-game progress.

The device renderer consumer links against the signed ICD. Simulator
verification covers SDK archive compilation; its consumer link remains open.
Device records are in `ipad-first-present/device-ipad/verification.json`.
Use the iPad for unattended tests while the player has the iPhone.

## Diagnosing a presented black buffer

The 4,784-entry native guest executes one compute dispatch and one draw per
presentation. The original display-target capture remained armed because this
path's draw has no color attachment. `OSL_CAPTURE_FLIP_INTERVAL=1` now lets
the existing `OSL_CAPTURE_TRIGGER` and `OSL_CAPTURE_DIR` capture the interval
between completed flips. The display-pass capture remains the default.

Three captures at approximately 4, 16 and 35 seconds show the same two passes.
The compute pass writes the registered display buffer with black. The draw
writes depth only, with no color attachment or sampled texture. These commands
explain the black output at the captured points; they do not establish a
textured scene. The run stops at a later missing CPU block after about
48 seconds, with all 22 workers joined.

Captures and frame statistics are under `native-first-frames/draw-capture/`.
The x86-64 IR-pass/cache-reload and 386-check portable-program fixtures pass
after the diagnostic change. Continue the later CPU/loading path and recapture
once scene draws appear.
