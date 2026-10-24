import Foundation

/// Where calibration clicks went missing: the AssistiveTouch floating button swallows clicks, so a probe
/// that never reaches the app most likely hit it. The last few spots (screen fractions) are kept and shown
/// to the models so they keep away from it.
enum AssistiveTouchSpots {
    private static let key = "atButtonSpots"
    private static let limit = 3

    static var all: [(x: Double, y: Double)] {
        let raw = UserDefaults.standard.array(forKey: key) as? [[Double]] ?? []
        return raw.compactMap { $0.count == 2 ? ($0[0], $0[1]) : nil }
    }

    static func remember(x: Double, y: Double) {
        var raw = UserDefaults.standard.array(forKey: key) as? [[Double]] ?? []
        raw.removeAll { abs($0[0] - x) < 0.08 && abs($0[1] - y) < 0.08 }
        raw.append([x, y])
        UserDefaults.standard.set(Array(raw.suffix(limit)), forKey: key)
        DebugLog.write(String(format: "AssistiveTouch spot remembered at (%.2f, %.2f); known: %d", x, y, raw.suffix(limit).count))
    }

    /// The prompt rule, with the remembered spots in the models' 0–1000 coordinates.
    static var rule: String {
        var text = """
Never tap the AssistiveTouch floating button (a small translucent round button at a screen edge). \
If an AssistiveTouch menu panel is visible, close it by tapping outside it.
"""
        let spots = all.map { "(\(Int($0.x * 1000)), \(Int($0.y * 1000)))" }
        if !spots.isEmpty { text += " It was last seen near " + spots.joined(separator: ", ") + "; keep away from there." }
        return text
    }
}
