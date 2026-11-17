# Foreground timed stop for continuous guest diagnostics

`OSL_GUEST_RUN_MS=0` opts into the existing cancellable continuous guest.
For supervised development replays only, `OSL_GUEST_STOP_AFTER_MS` may be an
integer from 100 through 1,800,000. It requires continuous mode. A monotonic
main-thread clock requests the ordinary guest stop when due; workers are still
joined and GPU resources are retired by the existing session cleanup. The
setting does not enable background suspend/resume and does not make continuous
mode a production default. Inactive-scene handling and its known device-loss
risk remain unchanged.

Parser and clock boundary tests pass with address/undefined sanitizers. A
physical replay must verify the actual timed-stop report, worker joins,
renderer cleanup and both save inventories; compiling this helper alone is not
proof of lifecycle or gameplay correctness. The intended run uses muted audio
and a disposable read-only profile.
