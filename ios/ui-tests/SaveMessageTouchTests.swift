import XCTest
import UIKit

final class SaveMessageTouchTests: XCTestCase {
    // Locate the actual filled accent button in the device screenshot. Long
    // text strokes do not satisfy the width/height/area checks. The normalized
    // point comes from pixels, independently of the renderer's hit-test data.
    private func positiveButton(in image: UIImage) -> CGVector? {
        guard let cg = image.cgImage else { return nil }
        let width = cg.width, height = cg.height
        guard width > 100, height > 100, width <= 4096, height <= 4096 else { return nil }
        var pixels = [UInt8](repeating: 0, count: width * height * 4)
        let copied = pixels.withUnsafeMutableBytes { bytes -> Bool in
            guard let context = CGContext(data: bytes.baseAddress, width: width, height: height,
                                          bitsPerComponent: 8, bytesPerRow: width * 4,
                                          space: CGColorSpaceCreateDeviceRGB(),
                                          bitmapInfo: CGBitmapInfo.byteOrder32Big.rawValue |
                                            CGImageAlphaInfo.premultipliedLast.rawValue) else { return false }
            context.draw(cg, in: CGRect(x: 0, y: 0, width: width, height: height))
            return true
        }
        guard copied else { return nil }
        func pink(_ x: Int, _ y: Int) -> Bool {
            let at = (y * width + x) * 4
            let r = Int(pixels[at]), g = Int(pixels[at + 1]), b = Int(pixels[at + 2])
            return r > 170 && g < 120 && b > 90 && b < 210 && r > g * 2
        }
        var best = (length: 0, x: 0, y: 0)
        for y in (height / 4)..<(height * 3 / 4) {
            var start = 0
            for x in 0...width {
                if x < width && pink(x, y) { continue }
                if x - start > best.length { best = (x - start, start, y) }
                start = x + 1
            }
        }
        guard best.length > width / 10, best.length < width * 3 / 5 else { return nil }
        let edge = best.x + best.length / 6
        var top = best.y, bottom = best.y
        while top > 0 && pink(edge, top - 1) { top -= 1 }
        while bottom + 1 < height && pink(edge, bottom + 1) { bottom += 1 }
        let buttonHeight = bottom - top + 1
        guard buttonHeight > height / 60, best.length * buttonHeight > width * height / 300 else { return nil }
        return CGVector(dx: CGFloat(best.x + best.length / 2) / CGFloat(width),
                        dy: CGFloat(top + buttonHeight / 2) / CGFloat(height))
    }

    @MainActor
    func testYesThroughDeviceTouch() throws {
        continueAfterFailure = false
        let app = XCUIApplication(bundleIdentifier: "com.wadscorp.odinsphere.startup")
        app.launchEnvironment = ["OSL_GUEST_RUN_MS": "180000", "OSL_INPUT_SCRIPT": "DisplayInput/pending.txt"]
        app.launch()
        XCTAssertEqual(app.state, .runningForeground)
        let until = Date().addingTimeInterval(100)
        var target: CGVector?
        var before: XCUIScreenshot?
        while Date() < until && target == nil {
            let shot = app.screenshot()
            target = positiveButton(in: shot.image)
            if target != nil { before = shot; break }
            Thread.sleep(forTimeInterval: 1)
        }
        guard let point = target, let before else {
            let attachment = XCTAttachment(screenshot: app.screenshot()); attachment.lifetime = .keepAlways; add(attachment)
            XCTFail("No complete positive save-message button appeared"); return
        }
        let attachment = XCTAttachment(screenshot: before); attachment.name = "actual-device-dialog-before-touch"; attachment.lifetime = .keepAlways; add(attachment)
        let location = XCTAttachment(string: "normalized tap: \(point.dx), \(point.dy)"); location.lifetime = .keepAlways; add(location)
        app.coordinate(withNormalizedOffset: point).tap()

        // The app returns its full structured report to the original UITextView
        // only after guest joins, GPU teardown and window restoration. Verify
        // the real outcome, rather than accepting disappearance of a button.
        let done = Date().addingTimeInterval(65)
        var report: [String: Any]?
        while Date() < done {
            if let value = app.textViews.firstMatch.value as? String,
               let data = value.data(using: .utf8),
               let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
               let renderer = object["renderer"] as? [String: Any],
               renderer["shutdown_complete"] as? Bool == true { report = object; break }
            Thread.sleep(forTimeInterval: 1)
        }
        guard let report, let renderer = report["renderer"] as? [String: Any],
              let guest = report["game_startup"] as? [String: Any] else {
            let failed = XCTAttachment(screenshot: app.screenshot()); failed.lifetime = .keepAlways; add(failed)
            XCTFail("No completed renderer report after device touch"); return
        }
        XCTAssertEqual(renderer["passed"] as? Bool, true)
        XCTAssertEqual(guest["save_dialog_completions"] as? Int, 1)
        XCTAssertEqual(guest["save_dialog_button"] as? Int, 1)
        XCTAssertEqual(guest["save_dialog_result"] as? Int, 0)
        XCTAssertEqual(guest["fault_address"] as? Int, 0)
        XCTAssertEqual(guest["workers_started"] as? Int, guest["workers_finished"] as? Int)
        let result = XCTAttachment(string: String(data: try JSONSerialization.data(withJSONObject: report, options: .prettyPrinted), encoding: .utf8)!)
        result.name = "actual-device-completed-report"; result.lifetime = .keepAlways; add(result)
    }
}
