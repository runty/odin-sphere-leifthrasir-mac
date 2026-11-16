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
  --baseline-source /Volumes/office/Build/OdinSphere/source \
  --output /Volumes/office/Build/OdinSphere/page-granularity-tests
```

The reference command ran from the isolated page candidate before integration;
the baseline argument must name an unchanged old-policy tree for its negative
control. Both iPhoneOS/Simulator fixtures and signal-context sources compile.
Evidence pins all input hashes in `page-granularity-tests/verification.json`.
This is policy/host-fault verification, not full renderer/cache-coherence,
character gameplay, audio quality or physical-device acceptance. Full renderer
and same-backend, muted guest replay remain required.
