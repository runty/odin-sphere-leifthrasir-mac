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
The initial app build identifies the worktree as dirty; a committed build and
repeat install/relaunch are the next qualification steps.

The iPad install succeeds, but launch is rejected with the explicit `Locked`
error, so no iPad routine-execution result is claimed yet. The user's hands-on
30-minute Mac gameplay session remains deferred. Next: establish a committed
device baseline, then extend the checked runtime to the actual linked-module
initializers and guest startup before selecting the final mobile CPU backend.
