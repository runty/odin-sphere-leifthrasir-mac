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
        app.launchEnvironment["OSL_GUEST_RUN_MS"] = "600000"
        app.launchEnvironment["OSL_CAPTURE_DISPLAY_FRAME"] = "1"
        // XCTest screenshots omit the Metal surface on this iPad. Save a bounded
        // renderer sequence around the observed stick gestures and review it
        // against gesture timestamps; this adds diagnostic GPU waits.
        app.launchEnvironment["OSL_FRAME_SNAPSHOT_AT"] = "4000"
        app.launchEnvironment["OSL_FRAME_SNAPSHOT_EVERY"] = "20"
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
        func capture(_ name: String) {
            let image = XCTAttachment(screenshot: XCUIScreen.main.screenshot())
            image.name = name; image.lifetime = .keepAlways; add(image)
        }
        // The prior test reached the attic in about 327 seconds but a later
        // accessibility query timed out. Capture multiple earlier gestures so
        // evidence survives a subsequent test failure; review the scene and
        // guest input counters separately before claiming movement.
        let navigationStart = Date()
        let movementTimes: [TimeInterval] = [240, 280, 310]
        var movementIndex = 0
        while Date().timeIntervalSince(navigationStart) < 320 {
            let elapsed = Date().timeIntervalSince(navigationStart)
            if movementIndex < movementTimes.count && elapsed >= movementTimes[movementIndex] {
                capture("before-stick-\(movementIndex)")
                stick.press(forDuration: 0.1, thenDragTo: right, withVelocity: .slow, thenHoldForDuration: 3)
                capture("after-stick-\(movementIndex)")
                movementIndex += 1
            } else {
                cross.press(forDuration: 0.25)
                Thread.sleep(forTimeInterval: 7)
            }
        }
        XCTAssertEqual(movementIndex, movementTimes.count)
        // Keep the runner alive until the guest's own bounded stop. Do not
        // treat the XCTest result alone as evidence of guest cleanup.
        Thread.sleep(forTimeInterval: 300)
        capture("after-bounded-session")
    }
}
