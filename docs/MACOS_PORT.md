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

## Milestones

| Stage | Work | Acceptance |
| --- | --- | --- |
| M0 · Reproduce the baseline | Pin sources, identify the package by SFO and executable hash, prepare game/boot image, run Mac probes. Record an upstream PC scene and save when available. | Reproduce the US image, CPU comparisons, working alias/fiber and Vulkan feature results. Retain a PC scene fixture for later visual comparisons. |
| M1 · Darwin runtime | Add a Mac build target for the real loader and services. Replace Linux ucontext, /proc paths, thread/TLS setup, clock and filesystem calls. Audit pointers that cross into the guest, including heap handles and stacks. Implement the guest TCB contract without changing Darwin's own TLS. | Build and link x86-64, exercise independent TLS on two guest threads, low allocations under load, fiber switching and fault reporting. Resolve the real image's imports using the runner's check mode. |
| M2 · Headless game | Run the supported image with --cpu-only, explicit disposable user/log roots and a timeout. Trace libc/Fios startup, main and worker progress, and the first unsupported service. | Pass module startup and a repeatable game scene transition with advancing guest events across two runs. A timeout alone is insufficient. |
| M3 · Visible frames | Integrate the Vulkan loader/driver, SDL surface and shader compiler. Compare known frames, blending, text, movies and texture formats with PC. Record target/presentation sizes. | Present title and active gameplay, capture both, record nonempty draw/flip counts and validation results. |
| M4 · Play, audio and saves | Start a new story, reach the first playable battle, progress to a save point, exercise menus, physical controller reconnect, and CoreAudio. | Two complete quit/relaunch cycles recover character, location and progress. Measure frame timing and audio gaps over a 30-minute session. |
| M5 · Launcher and app | Use macOS paths and file picker, configure the player's package and save location, disable incompatible binary updates, bundle the driver and required libraries with notices. Ask for Odin's save preference before installation. | Finder Play/Stop/relaunch from a relocated signed app with the build tree unavailable. Test package removal, app replacement, clean account and persisted saves. Record measured status in the pipeline. |
| Phase 2 · iPhone and iPad | Evaluate offline x86-64 translation or interpretation against the Mac ABI/TLS/fiber fixtures. Verify an arm64 shader path for Odin's generated shaders on each device. Add sandbox import, touch and interruption handling after CPU proof. | Signed standalone routine tests on iPhone 17 Pro Max and iPad mini (A20 Pro), followed by real module startup, title, battle, save and cold reload. Record results per device. |

The next implementation target is M1. Re-estimate gameplay after M2 and a presented scene. Public binary distribution and other game revisions need separate packaging and acceptance results.
