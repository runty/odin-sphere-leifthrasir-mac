import XCTest

// The externally launched, muted sandbox owns its copied save, PID and console.
// UI results alone cannot prove guest input, scene progress or clean shutdown.
final class OdinTouchGameplayUITests: XCTestCase {
    func testTouchNavigationInExistingSandbox() throws {
        executionTimeAllowance = 900
        let app = XCUIApplication(bundleIdentifier: "com.wadscorp.odinsphere.sandbox69048")
        app.activate()
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
        // Discrete confirmation attempts mirror the scripted startup route.
        // Scene and save-dialog progress are reviewed from captured evidence.
        let navigationDeadline = Date().addingTimeInterval(450)
        while Date() < navigationDeadline {
            guard app.state == .runningForeground else { XCTFail("Sandbox left foreground"); return }
            cross.press(forDuration: 0.25)
            Thread.sleep(forTimeInterval: 4)
        }
        Thread.sleep(forTimeInterval: 20)
        capture("before-stick")
        for _ in 0..<2 {
            guard app.state == .runningForeground else { XCTFail("Sandbox left foreground"); return }
            stick.press(forDuration: 0.1, thenDragTo: right, withVelocity: .slow, thenHoldForDuration: 3)
        }
        capture("after-stick")
        // Allow the guest's own bounded stop and cleanup; do not count this
        // XCTest assertion as proof that the original guest stayed alive.
        Thread.sleep(forTimeInterval: 130)
        capture("after-bounded-session")
    }
}
