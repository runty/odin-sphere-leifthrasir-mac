import XCTest

// Renderer frames, not elapsed wall time, drive this exploratory route. The
// counter is a heartbeat, so captures and guest telemetry remain necessary.
final class OdinTouchGameplayUITests: XCTestCase {
    func testTouchNavigationInSandbox() throws {
        executionTimeAllowance = 900
        let app = XCUIApplication(bundleIdentifier: "com.wadscorp.odinsphere.sandbox69048")
        app.launchEnvironment["OSL_AUDIO"] = "muted"
        app.launchEnvironment["OSL_AUDIO_STATS"] = "1"
        app.launchEnvironment["OSL_TOUCH_CONTROLS"] = "1"
        app.launchEnvironment["OSL_TOUCH_FRAME_CLOCK"] = "1"
        app.launchEnvironment["OSL_GUEST_RUN_MS"] = "600000"
        app.launchEnvironment["OSL_CAPTURE_DISPLAY_FRAME"] = "1"
        app.launchEnvironment["OSL_FRAME_SNAPSHOT_AT"] = "4650"
        app.launchEnvironment["OSL_FRAME_SNAPSHOT_EVERY"] = "35"
        app.launchEnvironment["OSL_FRAME_SNAPSHOT_COUNT"] = "80"
        app.launch()
        XCTAssertTrue(app.wait(for: .runningForeground, timeout: 20))
        let clock = app.otherElements["touch-frame-clock"]
        XCTAssertTrue(clock.waitForExistence(timeout: 30), "Opt-in frame clock is missing")
        Thread.sleep(forTimeInterval: 10)
        let width = app.frame.width, height = app.frame.height
        XCTAssertGreaterThan(width, 240); XCTAssertGreaterThan(height, 160)
        let unit = min(58.0, max(18.0, min(width/14, (height-57)/9)))
        let base = app.coordinate(withNormalizedOffset: .zero)
        let cross = base.withOffset(CGVector(dx: width-2*unit, dy: height-25-0.95*unit))
        let stick = base.withOffset(CGVector(dx: 2*unit, dy: height-25-1.8*unit))
        let right = base.withOffset(CGVector(dx: 3*unit, dy: height-25-1.8*unit))
        let deadline = Date().addingTimeInterval(570)
        var lastFrame: UInt64 = 0, lastCross: UInt64 = 0
        var advanced = false, entered = false, readCount = 0
        while Date() < deadline {
            guard app.state == .runningForeground else {
                XCTFail("Sandbox left foreground at frame \(lastFrame)"); return
            }
            guard let value = clock.value as? String, let frame = UInt64(value) else {
                XCTFail("Invalid frame clock value: \(String(describing: clock.value))"); return
            }
            XCTAssertGreaterThanOrEqual(frame, lastFrame, "Frame clock moved backwards")
            lastFrame = frame; readCount += 1
            if readCount % 10 == 0 { print("OSL frame clock: \(frame)") }
            if frame >= 4850 && !advanced {
                print("OSL action: move right at \(frame)")
                stick.press(forDuration: 0.1, thenDragTo: right, withVelocity: .slow, thenHoldForDuration: 2)
                advanced = true
            } else if frame >= 5100 && advanced && frame >= lastCross + 100 {
                print("OSL action: cross at \(frame)")
                cross.press(forDuration: 0.35); lastCross = frame; entered = true
            } else if frame < 4650 && (lastCross == 0 || frame >= lastCross + 100) {
                print("OSL action: advance intro at \(frame)")
                cross.press(forDuration: 0.25); lastCross = frame
            }
            if frame >= 7200 { break }
            Thread.sleep(forTimeInterval: 2)
        }
        XCTAssertTrue(advanced, "No attic movement attempted; last frame \(lastFrame)")
        XCTAssertTrue(entered, "No stand interaction attempted; last frame \(lastFrame)")
        XCTAssertGreaterThan(lastFrame, 5200, "Guest did not progress far enough")
        print("OSL last observed frame: \(lastFrame)")
        // Guest's own 600-second stop is verified separately from this test.
        Thread.sleep(forTimeInterval: 100)
    }
}
