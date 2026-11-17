import XCTest

final class OdinTouchUITests: XCTestCase {
    func testFrameClockIsOptIn() throws {
        let app = XCUIApplication()
        app.launch()
        let status = app.staticTexts["touch-status"]
        XCTAssertTrue(status.waitForExistence(timeout: 20))
        let ready = NSPredicate(format: "label BEGINSWITH 'ready;'")
        expectation(for: ready, evaluatedWith: status)
        waitForExpectations(timeout: 20)
        XCTAssertFalse(app.otherElements["touch-frame-clock"].exists)
    }

    func testActualTouchEvents() throws {
        let app = XCUIApplication()
        app.launchEnvironment["OSL_TOUCH_FRAME_CLOCK"] = "1"
        app.launch()
        let frameClock = app.otherElements["touch-frame-clock"]
        XCTAssertTrue(frameClock.waitForExistence(timeout: 20))
        XCTAssertEqual(frameClock.value as? String, "4321")
        let status = app.staticTexts["touch-status"]
        XCTAssertTrue(status.waitForExistence(timeout: 20))
        let ready = NSPredicate(format: "label BEGINSWITH 'ready;'")
        expectation(for: ready, evaluatedWith: status)
        waitForExpectations(timeout: 20)
        let fields = status.label.split(separator: ";").map(String.init)
        XCTAssertEqual(fields.count, 9)
        guard fields.count == 9,
              let width = Double(fields[1]), let height = Double(fields[2]),
              let left = Double(fields[3]), let top = Double(fields[4]),
              let right = Double(fields[5]), let bottom = Double(fields[6]) else {
            XCTFail("Invalid geometry from fixture"); return
        }
        let unit = min(58.0, max(18.0, min((width-left-right)/14.0, (height-top-bottom)/9.0)))
        let base = app.coordinate(withNormalizedOffset: .zero)
        let cross = base.withOffset(CGVector(dx: width-right-2*unit, dy: height-bottom-1.8*unit+0.85*unit))
        cross.press(forDuration: 0.6)
        let move = base.withOffset(CGVector(dx: left+2*unit, dy: height-bottom-1.8*unit))
        let moved = base.withOffset(CGVector(dx: left+3*unit, dy: height-bottom-1.8*unit))
        // Hold at the destination so the fixture's periodic sampler observes
        // the stick even when the simulator is under load.
        move.press(forDuration: 0.2, thenDragTo: moved, withVelocity: .default, thenHoldForDuration: 0.8)
        let observed = NSPredicate(format: "label ENDSWITH ';1;1'")
        let result = XCTWaiter.wait(for: [expectation(for: observed, evaluatedWith: status)], timeout: 10)
        XCTAssertEqual(result, .completed, "Touch fixture status: \(status.label)")
    }
}
