import XCTest

// Launch only after the test runner is ready: runner startup itself backgrounds
// an already-running guest. UI results alone do not prove input or cleanup.
final class OdinTouchGameplayUITests: XCTestCase {
    func testTouchNavigationInSandbox() throws {
        executionTimeAllowance = 900
        let app = XCUIApplication(bundleIdentifier: "com.wadscorp.odinsphere.sandbox69048")
        app.launchEnvironment["OSL_AUDIO"] = "muted"
        app.launchEnvironment["OSL_AUDIO_STATS"] = "1"
        app.launchEnvironment["OSL_TOUCH_CONTROLS"] = "1"
        app.launchEnvironment["OSL_INPUT_MODE"] = "touch-only"
        app.launchEnvironment["OSL_GUEST_RUN_MS"] = "600000"
        app.launchEnvironment["OSL_CAPTURE_DISPLAY_FRAME"] = "1"
        // XCTest screenshots omit the Metal surface on this iPad. Save a bounded
        // renderer sequence around the observed stick gestures and review it
        // against gesture timestamps; this adds diagnostic GPU waits.
        app.launchEnvironment["OSL_FRAME_SNAPSHOT_AT"] = "4400"
        app.launchEnvironment["OSL_FRAME_SNAPSHOT_EVERY"] = "35"
        app.launchEnvironment["OSL_FRAME_SNAPSHOT_COUNT"] = "64"
        app.launch()
        XCTAssertTrue(app.wait(for: .runningForeground, timeout: 20))
        Thread.sleep(forTimeInterval: 12)
        let width = app.frame.width, height = app.frame.height
        XCTAssertGreaterThan(width, 240); XCTAssertGreaterThan(height, 160)
        // The existing iPad fixture measured a 25-point bottom safe inset;
        // capture review and guest counters independently validate targeting.
        let unit = min(58.0, max(18.0, min(width/14, (height-57)/9)))
        let base = app.coordinate(withNormalizedOffset: .zero)
        let cross = base.withOffset(CGVector(dx: width-2*unit, dy: height-25-0.95*unit))
        let stick = base.withOffset(CGVector(dx: 2*unit, dy: height-25-1.8*unit))
        let right = base.withOffset(CGVector(dx: 3*unit, dy: height-25-1.8*unit))
        let left = base.withOffset(CGVector(dx: unit, dy: height-25-1.8*unit))
        let dpadUp = base.withOffset(CGVector(dx: 4.2*unit, dy: height-25-3.9*unit))
        func capture(_ name: String) {
            let image = XCTAttachment(screenshot: XCUIScreen.main.screenshot())
            image.name = name; image.lifetime = .keepAlways; add(image)
        }
        // Time these movements around the observed attic transition. Alternate
        // directions without intervening Cross presses, then try Up at the
        // bookshelf and continue the story. The route is heuristic: images and
        // guest telemetry must be reviewed before claiming any scene or input.
        let navigationStart = Date()
        var didRight = false, didLeft = false, didUp = false
        while Date().timeIntervalSince(navigationStart) < 430 {
            let elapsed = Date().timeIntervalSince(navigationStart)
            if !didRight && elapsed >= 280 {
                capture("before-right")
                stick.press(forDuration: 0.1, thenDragTo: right, withVelocity: .slow, thenHoldForDuration: 5)
                capture("after-right"); didRight = true
            } else if didRight && !didLeft && elapsed >= 300 {
                capture("before-left")
                stick.press(forDuration: 0.1, thenDragTo: left, withVelocity: .slow, thenHoldForDuration: 5)
                capture("after-left"); didLeft = true
            } else if didLeft && !didUp && elapsed >= 320 {
                capture("before-up")
                dpadUp.press(forDuration: 1)
                capture("after-up"); didUp = true
            } else if elapsed < 270 || (didUp && elapsed >= 330) {
                cross.press(forDuration: 0.25)
                Thread.sleep(forTimeInterval: 7)
            } else {
                Thread.sleep(forTimeInterval: 1)
            }
        }
        XCTAssertTrue(didRight && didLeft && didUp)
        // Guest has its own 600-second bound; results do not establish teardown.
        Thread.sleep(forTimeInterval: 200)
        capture("after-bounded-session")
    }
}
