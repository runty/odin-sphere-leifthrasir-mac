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
with LLVM 23.1.5 and the four adjacent patches. Those compatibility/semantics
patches were developed in the OpenCrown port and are retained here so this
experiment has reproducible source inputs. Existing matching OpenCrown backend
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
