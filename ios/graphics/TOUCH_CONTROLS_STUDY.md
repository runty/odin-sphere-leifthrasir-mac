# Touch controls development study

This isolated branch adapts the 15-control multitouch overlay and slot-0 merge
rules from runty/OpenCrown-PC revision `9ca8945` (`ios/platform/platform.mm`
and `ios/platform/core.cpp`) to Odin's `PadSample`/`MobilePad` interface.
It is a development option, not the shipping interface: set
`OSL_TOUCH_CONTROLS=1` for a real guest session to attach it to the SDL renderer
window. It defaults off. A requested overlay is rejected when any scripted
input is configured, and injected test sources never merge the virtual pad.
The attach fails closed unless the scene has a distinct renderer window.

The overlay uses the scene's safe-area geometry and lets multiple fingers hold
sticks, D-pad, face buttons, shoulder buttons, triggers, stick clicks, Menu and
Pad simultaneously. A touch remains assigned to its original control as it
moves. Releasing or cancelling it clears that control; resizing or app/session
inactivation clears every held input. Touches outside visible controls pass to
the renderer. `Pad` maps the PS touchpad button through Odin's existing centre
point emulation; arbitrary PS touchpad coordinates are not implemented.

Slot zero merges virtual buttons with a connected physical controller, chooses
the stronger stick axes and maximum trigger values, and keeps physical device
identity while a controller is present. With no controller, an active valid
virtual overlay acts as a connected pad. Read snapshots are mutex-protected;
UIKit operations remain on the main thread. A weak neutral bridge permits
native tools to link without the overlay, while the app provides the strong
bridge. These changes do not implement suspension: the existing engineering
renderer still stops its bounded guest on app inactivity.

Synthetic native tests cover common iPad and phone-size portrait/landscape
layouts, safe areas, every button centre, simultaneous stick and button input,
duplicate contacts, cancellation, invalid geometry, controller merge, and
concurrent mutation/read under AddressSanitizer/UndefinedBehaviorSanitizer and
ThreadSanitizer. The existing 39-check pad contract still passes with the weak
fallback; a separate real `MobilePad` link test confirms the strong bridge is
used and injected script input remains isolated. UIKit adapter, renderer and
pad sources compile for iPhoneOS and Simulator. A separate no-game UIKit fixture
ran on iPad mini A20 Pro: it attached the overlay, rendered a visually inspected
landscape layout, and observed neutral state before attachment and after
deactivation and detach. A separate XCUITest then injected a face-button press
and a stick drag into a UIKit fixture on that physical iPad, with the resulting
button and axis state checked in the app. The same test passed on a separately
created iPad mini simulator in portrait. These are automated events, not a
human ergonomics test, and neither fixture runs inside the SDL game window.

Build the reproducible standalone test with `ios/graphics/build-touch-ui-test.py
--root <new external directory> --sdk iphoneos --team <development team>` (or
`--sdk iphonesimulator` without a team). It does not contact devices. Run the
resulting `.xctestrun` only on an explicitly selected authorized destination.
The branch has not been adopted; touch input during the actual game, multitouch
on device, accessibility, controller reconnect, performance and full gameplay
remain unverified. Do not use the phone until authorized.
