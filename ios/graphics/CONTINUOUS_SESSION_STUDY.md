# Experimental cancellable continuous session

This isolated branch explores `OSL_GUEST_RUN_MS=0` as an explicit developer
opt-in for a session without the current wall-clock, checkpoint or dispatch
ceilings. Zero is accepted only when a stop signal is available. Existing
100–600,000 ms limits and the default 2,000 ms session remain unchanged. The
main-thread shell waits for an opt-in session's completion instead of reporting
a diagnostic timeout after 615 seconds. Dispatch counting uses 64 bits.

A fixture using the actual checked runtime checks boundary values, saturating
deadline calculation and an executing guest tick loop interrupted by the stop
signal. It passes native arm64, ASan/UBSan and ThreadSanitizer. The original
bounded guard test passes, and the runner, guest boundary and UIKit source pass
syntax checks for iPhoneOS and Simulator. These are synthetic and compile-time
checks: no continuous game, app lifecycle or physical-device run is claimed.

This does not make the engineering probe a playable app. Resigning active still
stops rather than suspends the guest; an eventual product needs correct
background/foreground behavior and user controls. Worker records and their stack
mappings remain retained until GPU/session cleanup, with an explicit 4,096
lifetime cap; removing it without proving GPU retirement would risk use-after-
free. These and full-game/audio/save coverage must be resolved before making
continuous mode the default or adopting this branch.

OpenCrown `a3e07c2` separates diagnostic deadlines from normal play and uses an
explicit guest stop request (`ios/app.mm`, `ios/game/runtime.cpp`). Odin's
separate checked runtime and lifecycle require their own qualification. Evidence
and exact source/test hashes live under `/Volumes/office/Build/OdinSphere/continuous-session-study-v1/`.

The separate mobile-integration branch combines this study with opt-in touch,
audio headroom, and paired audio diagnostics. Its native boundary fixture
adds a zero-budget guest cancelled after the real GPU buffer-registration
gate; a separate XCTest driver backgrounds and reactivates only the disposable
sandbox. Building a test driver does not establish its runtime result. The
current lifecycle intentionally cancels on background, so neither driver
establishes suspend/resume or uninterrupted gameplay.

The first physical-iPad background/activate driver reported an XCTest pass,
but the final diagnostics belonged to a new default 2,000 ms process rather
than the requested continuous session. It therefore did not verify lifecycle
cleanup. The driver also supports attaching to an independently launched,
supervised sandbox so its original PID and console can be checked before and
after the background transition; a UI test pass alone remains insufficient.
