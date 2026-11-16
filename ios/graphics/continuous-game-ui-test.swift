import XCTest

// A bounded automation driver for the disposable sandbox. Guest diagnostics,
// lifecycle ordering and save preservation must be reviewed independently.
final class OdinContinuousGameUITests: XCTestCase {
    func testBackgroundCancellationAndForegroundCleanup() throws {
        executionTimeAllowance = 180
        let app = XCUIApplication(bundleIdentifier: "com.wadscorp.odinsphere.sandbox69048")
        // Attach mode lets an external supervised launch preserve its console
        // and PID, so a termination/relaunch cannot look like clean recovery.
        if ProcessInfo.processInfo.environment["OSL_ATTACH_EXISTING"] == "1" {
            app.activate()
        } else {
            app.launchEnvironment["OSL_AUDIO"] = "muted"
            app.launchEnvironment["OSL_TOUCH_CONTROLS"] = "1"
            app.launchEnvironment["OSL_GUEST_RUN_MS"] = "0"
            app.launch()
        }
        XCTAssertTrue(app.wait(for: .runningForeground, timeout: 20))
        Thread.sleep(forTimeInterval: 15)
        let before = XCTAttachment(screenshot: XCUIScreen.main.screenshot())
        before.name = "continuous-before-background"; before.lifetime = .keepAlways; add(before)
        XCUIDevice.shared.press(.home)
        let background = NSPredicate { _, _ in
            app.state == .runningBackground || app.state == .runningBackgroundSuspended
        }
        expectation(for: background, evaluatedWith: app)
        waitForExpectations(timeout: 20)
        Thread.sleep(forTimeInterval: 5)
        app.activate()
        XCTAssertTrue(app.wait(for: .runningForeground, timeout: 20))
        Thread.sleep(forTimeInterval: 35)
        let after = XCTAttachment(screenshot: XCUIScreen.main.screenshot())
        after.name = "continuous-after-foreground"; after.lifetime = .keepAlways; add(after)
    }
}
