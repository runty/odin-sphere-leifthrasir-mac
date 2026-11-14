# Portable startup services

The startup runner now reuses the existing TrophyService, store, metadata and
archive code instead of inventing trophy results. Its six imported functions
validate guest pointers and copy scalar/flag outputs through aligned host
locals. Context/handle creation, registration, unlock bits, duplicate unlocks,
invalid handles and misaligned guest output addresses are exercised by the
runner's `--service-check` mode (88 checks across service families and the libc adapter).

This experiment has not configured an asset/profile directory for trophies.
The existing service therefore uses its metadata-unavailable RAM fallback;
`trophy_persistence=false` is explicit in diagnostics. It does not read or request
console/trophy keys. Persistence and metadata import remain separate work before
a playable app is delivered. RAM trophy checks are not game-save verification.

`crypto_commoncrypto.cpp` supplies Apple SHA-1, SHA-256, streaming SHA-256,
HMAC-SHA256 and unpadded AES-128 ECB/CBC for these host services. RSA PKG decryption
returns failure explicitly; the startup backend does not import encrypted PKGs.
The Mac port's existing OpenSSL backend remains unchanged.

```sh
python3 ios/services/test-crypto.py --root "$NEW_EXTERNAL_FIXTURE"
```

The native arm64 comparison passes 263 Apple/OpenSSL checks using ephemeral
random inputs generated in memory. No user keys or raw test key/data values are
read, saved or logged. The tests compare encryption bytes, cross-decryption,
hashes, streaming and HMAC results and reject unsupported/invalid operations.

`ios/game/service_build.py` compiles these sources into cached objects for the
Mac/iPhoneOS/Simulator runners. The startup ledger hashes the service sources
and headers; an iOS export refuses source drift from the established host run.

## Sparse direct memory

`memory.cpp` calls the production `src/runtime_memory.c` allocator through exact
typed native signatures. Its `OSL_AOT_CPU` mode reserves an owned 8 GiB movable
window below 1 TiB, with a sparse temporary-file backing for the retail direct
pool and flexible arena. The reservation and file size are virtual capacity;
neither eagerly allocates 5 GiB of RAM. Fixed mappings may replace only pages
inside the owned reservation. Guest execute permission never creates executable
host pages. The normal Rosetta build retains its existing address layout.

Each checked AOT CPU access consults the live mapping/protection table. Releasing,
unmapping or protecting a range cannot leave a stale writable region in the CPU
adapter. CPU write permission also permits reads, as in the x86 host path;
GPU-only permissions do not authorize CPU accesses. Startup sessions are
serialized and release their views, backing file and physical allocations on
teardown. Guest worker threads are still unavailable.

```sh
python3 ios/services/test-memory.py --output "$NEW_EXTERNAL_FIXTURE" --remill "$WORK/cpu-backends"
```

The 96 native checks cover sparse footprint, physical allocation accounting,
alignment/bounds/overflow, coherent aliases, cross-page accesses, protection
changes, unmap/release invalidation, zeroed reuse, session reset and rejecting
fixed mappings over a host canary. The kernel APIs also pass through the service
suite's deliberately misaligned guest output pointers. Flexible allocations
still use the earlier bounded aligned allocator; integrating those with the
native memory table remains necessary before GPU integration.

The exact hash-checked base image's public libc `memset` export (offset
`0xcd0ce0`) has a checked native C ABI adapter. Its length and complete writable
range are validated before touching bytes. It preserves the return pointer,
return stack/PC and callee-saved state; boundary, zero-length and failed-range
cases are checked. This is not an x86 instruction/flags equivalence claim. No
other image may enable this specialization. A host comparison reaches the same
1,638-dispatch checkpoint with the helper in roughly 0.11 seconds, versus
3.9 seconds for translated fills; this is startup evidence, not mobile gameplay
performance. The default one-million-checkpoint/two-second guards remain active.

## Shared offline users

The production user implementation is now in `src/runtime_user.c`, shared by the
Mac service resolver and the mobile adapter. It retains the same offline player
IDs, idempotent initialization, user names, up to four logged-in players and
once-per-player login events. This does not represent a PSN account. The AOT
startup owner resets user state between isolated sessions while holding its
service lease; the Mac lifetime is unchanged.

```sh
python3 ios/services/test-user.py --output "$NEW_EXTERNAL_FIXTURE"
```

The standalone service passes 58 checks on native arm64 and x86-64 under Rosetta,
including eight concurrent login workers, no duplicate events, invalid IDs,
short/null name buffers and reset. The AOT service suite now has 104 checks,
including misaligned ID/list outputs and name-buffer canaries. The rebuilt Mac
runner still resolves all 307 real imports. Startup reaches 666 traces / 1,640
dispatches with user initialization complete, then stops at `scePadInit`.

## GameController pad backend

`MobilePad` implements this image's five imported pad APIs: Init, Open,
GetControllerInformation, ReadState and SetVibration. The Mac SDL producer and
mobile producer share `src/runtime_pad_types.h`, including the console's 120-byte
state and 28-byte information layouts. All guest outputs are checked and copied
through aligned host locals, including deliberately misaligned guest buffers.

The default source enumerates real GameController extended profiles on the main
queue. Stable slots track disconnect/reconnect and controller replacement,
logging in additional local players through the shared user service. Buttons,
sticks, triggers, menu and optional touchpad click map into the console layout;
dead zones, finite-value clamping, timestamps and neutral disconnected states
are explicit. No pairing/discovery UI is started. CoreHaptics handles optional
rumble where the controller offers engines/localities; hardware effects have
not been verified. The tests never vibrate physical hardware.
The app declares the ExtendedGamepad profile following Apple's
[Game Controllers configuration](https://developer.apple.com/documentation/xcode/configuring-game-controllers).

```sh
python3 ios/services/test-pad.py --output "$NEW_EXTERNAL_FIXTURE"
```

38 automated checks exercise state/error contracts, slots, reconnect counts,
neutral releases, analog extremes/NaN/dead zones, touchpad-click representation,
output canaries and actual framework-created controller snapshots. The first
fixture incorrectly set a derived D-pad button rather than its axes; using
GameController's documented directional-pad setter fixed the fixture. Snapshot
mapping then passes without weakening the expected button mask. This is not a
hands-on physical controller test. The complete AOT service suite passes 121
checks; the sparse-memory suite still passes all 96.

Startup now reaches 680 traces / 1,655 dispatches, both initializers returning
zero, and stops at `sceSaveDataInitialize`. Touch controls, motion sensing,
foreground input suppression/suspend handling and in-game physical controller
acceptance remain work before a finished mobile port. The UIKit probe still
contains no game renderer or touch overlay.
