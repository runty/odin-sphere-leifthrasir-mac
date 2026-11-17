# Recursive guest-lock experiment

This isolated Apple-only candidate retains the existing single global guest
critical section. It replaces `std::recursive_mutex` with `os_unfair_lock` and
a per-thread recursion count for this one lock; checked mappings, external
aliases, guest atomics, fault recovery and host memory scopes remain globally
serialized. Non-Apple builds keep the original mutex. Unfair locking does not
promise fairness, so starvation and real-game behavior require testing before
adoption.

The existing shared-memory, all-width compare/exchange and alias/remapping
stress fixtures pass on arm64 and x86-64 macOS, and on arm64 under
ASan/UBSan and ThreadSanitizer. Five alternating synthetic runs on one Mac
showed median 8-thread read wall time 364.8 ms baseline vs 203.6 ms candidate,
and single-thread hot lookup 12.1 vs 8.8 ms. Mapping churn was 41.0 vs 39.2 ms.
These are workload-specific synthetic results, not iPad or game frame rates.
A real-game comparison, lifecycle, GPU alias safety and sustained fairness
must be checked before the candidate is eligible for production use.

Evidence: `/Volumes/office/Build/OdinSphere/unfair-recursive-study/`.
