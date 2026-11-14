# iPhoneOS Vulkan-on-Metal experiment

The full KosmicKrisp driver now compiles as arm64 iPhoneOS, minimum iOS 27.2,
with only Apple/system runtime dependencies. This is a build/platform-audit
result; it has not yet executed on a physical device and does not render the
game. Both reference devices currently deny app launch while locked. Keep the
working Mac app and its driver unchanged.

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

Next: execute the signed framework probe on physical devices, then replay an
actual captured Odin draw and integrate title/battle presentation.
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
sets `game_frames=false`. Device execution remains pending.

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
