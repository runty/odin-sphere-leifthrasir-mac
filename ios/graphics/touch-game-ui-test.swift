import XCTest

// Test the isolated Odin sandbox only. A pass means the app stayed available
// through dispatched UIKit gestures; screenshot/guest-log review is separate.
final class OdinTouchGameUITests: XCTestCase {
    func testSandboxOverlayDelivery() throws {
        executionTimeAllowance = 180
        let app = XCUIApplication(bundleIdentifier: "com.wadscorp.odinsphere.sandbox69048")
        app.launchEnvironment["OSL_AUDIO"] = "muted"
        app.launchEnvironment["OSL_TOUCH_CONTROLS"] = "1"
        app.launchEnvironment["OSL_GUEST_RUN_MS"] = "45000"
        app.launch()
        XCTAssertTrue(app.waitForExistence(timeout: 20))
        Thread.sleep(forTimeInterval: 12)
        func capture(_ name: String) {
            let attachment = XCTAttachment(screenshot: XCUIScreen.main.screenshot())
            attachment.name = name
            attachment.lifetime = .keepAlways
            add(attachment)
        }
        capture("before-touch")
        let width = app.frame.width, height = app.frame.height
        XCTAssertGreaterThan(width, 240)
        XCTAssertGreaterThan(height, 160)
        // The fixture measured a 25-point bottom inset and no horizontal
        // inset on this iPad. Review the captured screenshot if it rotates.
        let unit = min(58.0, max(18.0, min(width/14, (height-57)/9)))
        let base = app.coordinate(withNormalizedOffset: .zero)
        let cross = base.withOffset(CGVector(dx: width-2*unit, dy: height-25-0.95*unit))
        cross.press(forDuration: 0.6)
        let move = base.withOffset(CGVector(dx: 2*unit, dy: height-25-1.8*unit))
        let moved = base.withOffset(CGVector(dx: 3*unit, dy: height-25-1.8*unit))
        move.press(forDuration: 0.2, thenDragTo: moved)
        capture("after-touch")
        // Let the bounded guest finish and release its own GPU state before
        // XCUITest tears down the app. This is a disposable copied profile.
        Thread.sleep(forTimeInterval: 45)
        capture("after-bounded-session")
    }
}
