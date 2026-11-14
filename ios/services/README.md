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

## File and save storage

`MobileStorage` reuses `runtime_file.c` and `runtime_savedata.c`, with the existing
FreeBSD errno mapping extracted into `runtime_errno.c`. File stat and save ABI
layouts are shared headers. The checked bridge copies nested guest pointers,
directory/title records, mount points, parameters, icon buffers and search
arrays through validated host storage. Search results retain their original
guest pointer fields. File reads validate writable destinations before starting
I/O; an invalid/protected destination cannot advance the descriptor.

The probe configures `Guest/App0` and `Guest/MobileProfile` below its private
Documents directory (the corresponding private image directory on the host).
`App0` is currently empty: actual game assets still need importing. Saves are
local, separate from Mac/player progress, and no cloud folder is selected.
Initialization is not presented as a successful in-game save.

Shared storage fixes apply to the Mac source too: metadata/icon replacement
uses a same-directory temporary, checks write/flush/fsync/close and preserves
the previous file on failure. Read-only save mounts reject mutations, writable
descriptors flush on close/unmount, all sixteen save slots fit alongside base
mounts, malformed mount aliases are rejected, and search ignores non-directory
entries. Failed creation removes only the attempt's empty directories. Delete
refuses mounted saves and reports host deletion failures. These checks do not
claim that arbitrary partially failed game-file writes are transactional, or
that power-loss recovery has been established.

```sh
python3 ios/services/test-storage.py --output "$NEW_EXTERNAL_FIXTURE" --remill "$WORK/cpu-backends"
```

Fourteen fresh child processes exercise native arm64/Rosetta creation, repeated
reload, file-size-limit failures, permission failures and deletion, plus the
checked guest bridge with changed guest addresses. A deterministic 1,156,096-byte
fixture is read back and independently byte/hash checked. Failed metadata/icon
writes preserve old hashes and leave no temporaries; failed new-slot creation
leaves no ghost slot. Read-only mutations fail. Guest checks include misaligned
structures, nested pointers, output canaries, protected buffers and invalid-FD
precedence. These are storage fixtures, not Odin game-save acceptance.

Host startup reaches 682 traces / 1,657 dispatches, with both initializers,
users, pad and save initialization complete. The next missing service is
`sceNpSetNpTitleId`. The matching libraries compile for iPhoneOS and Simulator.
The Mac source rebuild/import checks pass; the installed Mac bundle still needs
the updated storage core packaged and verified with a disposable copied save.

## Shared event flags and guarded guest waits

The mobile bridge now calls the production `src/runtime_eventflag.c` for
create/delete, set/clear, poll/wait and cancel. The CUSA05083 image imports the
first six APIs; cancellation is also exercised independently. The shared core
uses opaque monotonic tokens below 40 bits and a reference-counted registry.
Invalid handles never become native object pointers; deleted generations are
not reused. Delete removes the handle, wakes blocked waiters with ESRCH and
frees the object after the final operation releases it, fixing the earlier
blocked-waiter leak and concurrent deletion race.

`eventflag.h` owns at most 4,096 flags per startup session and deletes them at
session teardown. It checks guest output/name/timeout pointers before state
changes, uses aligned locals for misaligned outputs, and rejects another
session's or stale handles. ARM/iOS waits use the production Darwin monotonic
relative condition wait. A blocking guest call is bounded by the checked CPU
execution deadline: expiry of that host guard stops execution, rather than
returning a fabricated guest timeout. An actual shorter guest timeout retains
its normal return/output semantics. Host service scopes unwind before the
outer guest checkpoint resumes.

Run `python3 ios/services/test-eventflag.py --output "$WORK/ios-eventflag-tests"
--remill "$WORK/cpu-backends"` for arm64/Rosetta coverage. Each architecture
passes 4,792 checks, including single/multiple waiter wakeup, cancel, timeout,
eight blocked waiters deleted together, 200 four-thread set/poll/delete races,
stale/foreign handles, the session budget, teardown resource counts, protected/
misaligned outputs and host-deadline expiry. `--arch arm64 --sanitize address`
and `--sanitize thread` also pass. These are native worker tests; translated
guest worker threads and TLS are still separate work. The bridge and native
module compile against both actual mobile SDKs.

The Mac runtime synchronization suite passes, and a rebuilt development runner
presents 600 bounded frames / 50,945 draws / 3,187 submissions, mounts the copied
save read-only and leaves both copied/original save hashes intact. This is
startup/regression evidence; no saved-slot click-through or 30-minute gameplay
result is inferred. Private evidence is under `ios-eventflag-tests/`,
`ios-eventflag-{asan,tsan}/` and `ios-eventflag-mac-smoke/` below the Odin build
root. The clean packaged Mac update has its own delivery checks.

### Interrupting a parked event waiter

The host-only `runtime_eventflag_wait_guarded` entrypoint checks cancellation
every five milliseconds while keeping the same waiter in the native queue.
It preserves FIFO, clear modes and single-waiter exclusion across those checks;
the original guest export and ABI retain their existing wait implementation.
The guard only reads atomic cancellation state. The checked bridge restores
its memory/state leases before stopping the guest, outside the native lock,
and leaves caller result/timeout bytes unchanged on host interruption.

`test-eventflag-stop.py` exercises 28 cancellation, FIFO, cancel/delete, timeout
and guest-output checks. Canonical arm64 ASan/TSan and Rosetta runs pass, along
with the existing 4,792-check suite (200 deletion races) in all three modes.
HLE/core checks remain 4,336 with three worker cleanup fixtures under ASan/TSan;
translated IR is uninstrumented. The Mac synchronization executable also passes.
Private evidence: `eventflag-stop-qualified/` under the Odin build root.

A frozen 1,113-entry real startup replay, with 17 game-created workers in each
run, took 2,004.860 ms before the change and 284.337 ms with guarded waits.
Both stopped at the same explicit untranslated block, with all children joined
and GPU mappings released. This is one startup/stop comparison, not a gameplay
frame-rate measurement. Evidence: `eventflag-stop-experiment/game-replay/`.

## Checked condition variables

`condition.h` supplies session-owned real pthread conditions with opaque,
monotonically allocated guest tokens and a 4,096-object budget. Initialization,
signal, broadcast, destroy, static lazy initialization, wait and relative timed
wait are supported. Only default condition attributes are accepted. Waits use
Darwin's relative timed API and preserve the associated mutex's release/relock
contract. The shorter guest timeout returns the console's timeout code; expiry
of the host execution budget stops the guest explicitly. Removing a condition
with blocked waiters returns busy, and native objects stay referenced through
active calls. The owner must outlive/join all workers before teardown.

Checked ABI adapters use local aligned handle values, validate complete guest
spans and never expose native condition pointers. The existing AOT mutex lookup
supplies the actual paired pthread mutex. Guest CPUs now run concurrently in the native engineering frontend. Shared
HLE state and lazy guest-slot initialization are serialized by frontend leases,
which are released around native waits and reacquired before output copies.
The latest runtime qualification is recorded in the startup runbook.

`python3 ios/services/test-condition.py --output "$WORK/ios-condition-tests"
--remill "$WORK/cpu-backends"` passes 4,156 checks on each Mac CPU, including
real timeout/relock, four-waiter broadcast, busy deletion, 800 concurrent native
create/notify/delete cycles, stale/foreign tokens, object budget, protected/
misaligned/canary outputs and host-deadline stops. `--sanitizer address` and
`--sanitizer thread` pass on arm64. Eight additional actual import-dispatch
checks bring the AOT service suite to 4,266. These checks do not establish
translated guest worker execution or mobile gameplay.


## Native AOT worker and clock integration

The frontend now connects real game-created host workers to separate checked
CPU state and owned stack/TLS storage. See the [current worker checkpoint](../game/README.md#current-engineering-status--2026-11-14)
and [failure/clock qualification](../game/README.md#owned-guest-workers-clocks-and-failure-cleanup).
Four native game workers run and join before renderer/memory release. Both
mobile SDKs compile; a complete mobile rendering frontend remains separate.
The expanded HLE suite passes 4,322 cases plus two parent/child exception-unwind
cases under normal execution, ASan and TSan. Those sanitizer runs instrument
all portable provider/runtime C/C++/Objective-C++ sources, with explicitly
uninstrumented linked guest IR. They do not establish sanitized full gameplay.
