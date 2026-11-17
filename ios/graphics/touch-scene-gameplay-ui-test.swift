import XCTest

// Exploratory, disposable-profile navigation. Actions require a fresh captured
// frame; reaching the castle or combat must be verified from separate evidence.
final class OdinTouchSceneGameplayUITests: XCTestCase {
    func testSceneGuidedNavigationInSandbox() throws {
        executionTimeAllowance = 1200
        let app = XCUIApplication(bundleIdentifier: "com.wadscorp.odinsphere.sandbox69048")
        app.launchEnvironment["OSL_AUDIO"] = "muted"
        app.launchEnvironment["OSL_AUDIO_STATS"] = "1"
        app.launchEnvironment["OSL_TOUCH_CONTROLS"] = "1"
        app.launchEnvironment["OSL_TOUCH_FRAME_CLOCK"] = "1"
        app.launchEnvironment["OSL_TOUCH_SCENE_OCR"] = "1"
        app.launchEnvironment["OSL_GUEST_RUN_MS"] = "0"
        app.launchEnvironment["OSL_GUEST_STOP_AFTER_MS"] = "900000"
        app.launchEnvironment["OSL_CAPTURE_DISPLAY_FRAME"] = "1"
        app.launchEnvironment["OSL_FRAME_SNAPSHOT_AT"] = "1200"
        app.launchEnvironment["OSL_FRAME_SNAPSHOT_EVERY"] = "150"
        app.launchEnvironment["OSL_FRAME_SNAPSHOT_COUNT"] = "64"
        app.launch()
        XCTAssertTrue(app.wait(for: .runningForeground, timeout: 20))
        let clock = app.otherElements["touch-frame-clock"]
        XCTAssertTrue(clock.waitForExistence(timeout: 30))
        Thread.sleep(forTimeInterval: 10)
        let width = app.frame.width, height = app.frame.height
        XCTAssertGreaterThan(width, 240); XCTAssertGreaterThan(height, 160)
        let unit = min(58.0, max(18.0, min(width/14, (height-57)/9)))
        let base = app.coordinate(withNormalizedOffset: .zero)
        let cross = base.withOffset(CGVector(dx: width-2*unit, dy: height-25-0.95*unit))
        let circle = base.withOffset(CGVector(dx: width-1.15*unit, dy: height-25-1.8*unit))
        let up = base.withOffset(CGVector(dx: 4.2*unit, dy: height-25-3.95*unit))
        let down = base.withOffset(CGVector(dx: 4.2*unit, dy: height-25-2.85*unit))
        let stick = base.withOffset(CGVector(dx: 2*unit, dy: height-25-1.8*unit))
        let right = base.withOffset(CGVector(dx: 3*unit, dy: height-25-1.8*unit))
        let deadline = Date().addingTimeInterval(840)
        var lastFrame: UInt64 = 0, lastObservation: UInt64 = 0
        var lastAction = Date.distantPast, confirmedRead = false
        while Date() < deadline {
            guard app.state == .runningForeground else { XCTFail("Sandbox left foreground at \(lastFrame)"); return }
            let raw = clock.value as? String ?? ""
            let parts = raw.split(separator: ";", maxSplits: 2, omittingEmptySubsequences: false)
            guard parts.count == 3, let frame = UInt64(parts[0]), let observed = UInt64(parts[1]) else {
                XCTFail("Invalid scene clock: \(raw)"); return
            }
            XCTAssertGreaterThanOrEqual(frame, lastFrame)
            lastFrame = frame
            if observed > lastObservation {
                lastObservation = observed
                let scene = String(parts[2]).lowercased()
                print("OSL scene frame \(observed): \(scene)")
                if !confirmedRead && Date().timeIntervalSince(lastAction) >= 3 {
                    var action = ""
                    if scene.contains("do you want to read") && scene.contains("yes") {
                        action = "confirm read"; cross.press(forDuration: 0.3); confirmedRead = true
                    } else if scene.contains("put down book") && scene.contains("read") {
                        action = "read"; up.press(forDuration: 0.3)
                    } else if scene.contains("pick up book") {
                        action = "pick up book"; down.press(forDuration: 0.3)
                    } else if scene.contains("bookshelf") || scene.contains("socrates") {
                        action = "walk right"; stick.press(forDuration: 0.1, thenDragTo: right, withVelocity: .slow, thenHoldForDuration: 0.15)
                    } else if scene.contains("story archive") || scene.contains("text archive") {
                        action = "leave archive"; circle.press(forDuration: 0.3)
                    } else if scene.contains("load") || scene.contains("data 1") || scene.contains("new game") || scene.contains("yourself") {
                        action = "advance load"; cross.press(forDuration: 0.3)
                    }
                    if !action.isEmpty { print("OSL action at \(observed): \(action)"); lastAction = Date() }
                }
            } else if observed == 0 && frame < 1200 && Date().timeIntervalSince(lastAction) >= 12 {
                print("OSL action at \(frame): advance intro"); cross.press(forDuration: 0.25); lastAction = Date()
            }
            Thread.sleep(forTimeInterval: 2)
        }
        print("OSL last presented \(lastFrame), observed \(lastObservation), read confirmed \(confirmedRead)")
        XCTAssertGreaterThan(lastObservation, 0, "No captured scene was recognized")
        Thread.sleep(forTimeInterval: 100)
    }
}
