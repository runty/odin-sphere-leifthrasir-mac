# Native Apple arm64 page tracking

The renderer's fault-recovery tracking must use the host's protection granule.
Native Apple arm64 uses 16 KiB pages; Rosetta/x86 keeps its existing 4 KiB policy.
The tracker masks, dirty ranges, watcher accounting and PageManager table/locks
now share that platform-selected granule. Individual 4 KiB mprotect calls must
not simply be rounded independently, which could relax a neighbour still being
watched by another buffer/image.

The native game replay `owner-68811-rtc-sampled-04` reaches its 660-second outer
watchdog without final report or cleanup acceptance. A post-deadline sample
shows a guest worker in `libcBulkReturn`/memmove repeatedly entering the GPU
signal handler, while other CPUs wait on the shared checked-memory lock.
This points to fault recovery, not proof of a mutex ownership bug.

A real host-protection reproduction with the old PageManager header leaves six
of eight subpage read/write faults unresolved with EINVAL on native arm64.
Using the new consistent 16 KiB policy resolves all eight. Both old/new x86
Rosetta runs resolve all eight with their unchanged 4 KiB policy. The fixture
uses real protected mappings, the actual PageManager address policy and actual
IsWriteError, with bounded signal recovery instead of an infinite fault loop.

```sh
python3 ios/graphics/test-page-granularity.py \
  --baseline-source /Volumes/office/Build/OdinSphere/source-before-native-pages \
  --output /Volumes/office/Build/OdinSphere/page-granularity-tests
```

The reference command ran from the isolated page candidate before integration;
the preserved `source-before-native-pages` worktree at `e5588a7` has identical
old-policy inputs for the negative control. Both iPhoneOS/Simulator fixtures and
signal-context sources compile.
Evidence pins all input hashes in `page-granularity-tests/verification.json`.
This is policy/host-fault verification, not full renderer/cache-coherence,
character gameplay, audio quality or physical-device gameplay acceptance.
The rebuilt renderer passes 109 checked-bridge cases. The same-backend muted
native replay completes 577.363 seconds / 5,989 presentations, all 50 joins,
no fault and full cleanup, reaching the later missing-code stop `0x103f15cf`.
The actual local RTC call succeeds. Post-load captures are still black, so
character gameplay and complete rendering remain unverified.

A separately packaged iPad-only probe runs the exact qualified old/new fixture
translation units with distinct namespaces. Physical iPad19,1 / iPadOS 27.2.1
confirms 16 KiB pages, six unresolved baseline faults and zero fixed faults over
16 real cases. Report, raw rows, source and executable hashes verify. No game,
renderer or audio initializes. The existing game's three profile hashes stay
exact. Only this diagnostic PID is terminated; SIGKILL is needed after SIGTERM
remains listed, so no normal game-unload acceptance is implied. Evidence:
`page-policy-ipad-probe/physical-verification.json`.

`build-page-probe.py` packages this standalone test without installing/launching.
`--check-inputs-only` verifies qualified content and baseline/fixed roles across
preserved worktree paths; using the fixed policy as the baseline is rejected.
This avoids relying on the original verification's temporary source directory.
Full renderer/cache-coherence and mobile gameplay acceptance remain open.
