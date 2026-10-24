import UIKit

/// Strips what must never leave the phone from shared run data: secrets (API key, base URL and its host, the
/// board's Wi-Fi name and password), IP addresses and typed text; and blacks out the top band of screenshots
/// (status bar, Dynamic Island with the Live Activity, where notification banners drop in).
struct Redactor {
    var secrets: [String]
    static let mask = "[redacted]"
    static let typed = "•••"
    static let band = 0.07  // top share of each screenshot that is blacked out

    private static let rules: [(String, String)] = [
        (#"\b(?:\d{1,3}\.){3}\d{1,3}\b"#, mask),                                      // IPv4
        (#"("action": ?"type", ?"text": ?)"(?:[^"\\]|\\.)*""#, "$1\"\(typed)\""),     // history in prompts
        (#"(DO: type) [^\n]*"#, "$1 \(typed)"),                                         // model replies
        (#"(cmd #\d+ type) .*?( →|$)"#, "$1 \(typed)$2"),                              // debug.log commands
    ]

    /// Any text: a log line, metrics, a prompt.
    func text(_ s: String) -> String {
        var out = s
        for x in secrets.filter({ $0.count >= 4 }).sorted(by: { $0.count > $1.count }) {
            out = out.replacingOccurrences(of: x, with: Self.mask)
        }
        for (pattern, template) in Self.rules {
            out = out.replacingOccurrences(of: pattern, with: template, options: .regularExpression)
        }
        return out
    }

    /// One steps.jsonl line: every string redacted, and the text of `type` actions replaced.
    func jsonLine(_ line: String) -> String {
        guard let any = try? JSONSerialization.jsonObject(with: Data(line.utf8)),
              let data = try? JSONSerialization.data(withJSONObject: value(any), options: [.sortedKeys]) else {
            return text(line)
        }
        return String(decoding: data, as: UTF8.self)
    }

    private func value(_ v: Any) -> Any {
        switch v {
        case let s as String: return text(s)
        case let a as [Any]: return a.map(value)
        case var d as [String: Any]:
            for (k, x) in d { d[k] = value(x) }
            if d["action"] as? String == "type", d["text"] != nil { d["text"] = Self.typed }
            return d
        default: return v
        }
    }

    /// A screenshot with its top band blacked out; nil if it is not an image.
    func image(_ jpeg: Data) -> Data? {
        guard let img = UIImage(data: jpeg) else { return nil }
        let format = UIGraphicsImageRendererFormat()
        format.scale = img.scale
        return UIGraphicsImageRenderer(size: img.size, format: format).jpegData(withCompressionQuality: 0.8) { ctx in
            img.draw(at: .zero)
            UIColor.black.setFill()
            ctx.fill(CGRect(x: 0, y: 0, width: img.size.width, height: img.size.height * Self.band))
        }
    }
}
