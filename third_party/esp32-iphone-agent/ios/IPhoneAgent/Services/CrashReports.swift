import Foundation
import MetricKit

/// Crash, hang and CPU/disk diagnostics from iOS (MetricKit), kept on the phone in Documents/diagnostics/.
/// iOS hands them over at the next launch (up to a day later). They ride along in Logs › Export all logs and
/// in Report a bug; nothing is sent anywhere by the app. TestFlight builds also send crashes to App Store Connect.
final class CrashReports: NSObject, MXMetricManagerSubscriber {
    static let shared = CrashReports()
    static let dir = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("diagnostics", isDirectory: true)

    func start() { MXMetricManager.shared.add(self) }

    /// Reports kept so far, newest first.
    static var files: [URL] {
        let all = (try? FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil)) ?? []
        return all.sorted { $0.lastPathComponent > $1.lastPathComponent }
    }

    func didReceive(_ payloads: [MXDiagnosticPayload]) {
        try? FileManager.default.createDirectory(at: Self.dir, withIntermediateDirectories: true)
        for p in payloads {
            let counts: [(String, Int)] = [("crash", p.crashDiagnostics?.count ?? 0), ("hang", p.hangDiagnostics?.count ?? 0),
                                           ("cpu", p.cpuExceptionDiagnostics?.count ?? 0),
                                           ("disk", p.diskWriteExceptionDiagnostics?.count ?? 0)]
            let kinds = counts.filter { $0.1 > 0 }.map { "\($0.1) \($0.0)" }
            let name = "\(Int(p.timeStampEnd.timeIntervalSince1970))-\(UUID().uuidString.prefix(6)).json"
            try? p.jsonRepresentation().write(to: Self.dir.appendingPathComponent(name))
            DebugLog.write("diagnostics: \(kinds.isEmpty ? "report" : kinds.joined(separator: ", ")) from iOS, saved \(name)")
        }
    }
}
