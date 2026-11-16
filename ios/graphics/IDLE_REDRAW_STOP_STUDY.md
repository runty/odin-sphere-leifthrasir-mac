# Closed-port idle redraw study

The physical iPad continuous-session negative control stopped its guest on
background, then continued automatic blank presentation after the video port
closed; the Vulkan device was lost before foreground teardown. The earlier
UI-only test hid that failure by relaunching a fresh process. The tracked
original console and device result are in
`mobile-continuous-69136/device/attach/verification.json` under the Odin build
root.

This isolated candidate suppresses automatic blank redraws after a game video
port closes, while retaining initial blank presentation before first open and
restoring it on reopen. The close publishes suppression before the closed-port
state, so the presentation thread sees the intended policy. Existing queued
real frames, an already-running draw, GPU command work, and general iOS
background transitions are not drained or quiesced by this change. It is
therefore a narrow hypothesis to test, not a complete lifecycle fix.

OpenCrown's iOS Metal path uses a broader active-change gate that suspends
replay and waits for GPU idle. Odin has a separate Vulkan renderer and must
validate its own worker and presentation ownership before adopting any such
behavior. The concurrent close/reopen policy fixture passes ASan/UBSan and
TSan; no renderer or device result is implied by that fixture.
