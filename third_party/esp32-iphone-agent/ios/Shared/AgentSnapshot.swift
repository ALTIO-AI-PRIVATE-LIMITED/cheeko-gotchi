import Foundation
import WidgetKit

/// What the widgets, controls and the intent entity query need from the app, written by the app to the
/// App Group (falls back to the app's own defaults when the group isn't signed in, e.g. the open-source build).
struct AgentSnapshot: Codable, Equatable {
    enum Status: String, Codable { case ready, running, offline, needsSetup }

    struct Item: Codable, Equatable, Identifiable {
        var id: UUID
        var name: String
        var prompt: String
        var symbol: String
    }

    struct LastRun: Codable, Equatable {
        var task: String
        var ok: Bool
        var line: String  // the answer, or why it stopped
        var date: Date
    }

    var status: Status
    var step = 0
    var task = ""
    var intents: [Item] = []
    var lastIntent: UUID?
    var lastRun: LastRun?
    var demo = false

    var mood: Mood {
        switch status {
        case .ready: return .idle
        case .running: return .thinking
        case .offline, .needsSetup: return .sleeping
        }
    }

    /// The intent "Run last intent" starts: the last one run, else the first saved.
    var lastItem: Item? { intents.first { $0.id == lastIntent } ?? intents.first }

    /// Mock data for widget previews and the gallery.
    static let preview = AgentSnapshot(
        status: .ready,
        intents: [Item(id: UUID(), name: String(localized: "Daily 7 AM alarm"), prompt: "", symbol: "alarm.fill"),
                  Item(id: UUID(), name: String(localized: "Draw a smiley"), prompt: "", symbol: "scribble"),
                  Item(id: UUID(), name: String(localized: "Wi-Fi settings"), prompt: "", symbol: "wifi"),
                  Item(id: UUID(), name: String(localized: "Note: hello"), prompt: "", symbol: "note.text")],
        lastRun: LastRun(task: String(localized: "Draw a smiley"), ok: true, line: String(localized: "Smiley drawn"),
                         date: Date().addingTimeInterval(-1800)),
        demo: true)
}

enum SharedState {
    /// group.<bundle ID>, set at build time (IA_BUNDLE_ID in Config/*.xcconfig)
    static let group = Bundle.main.object(forInfoDictionaryKey: "IAAppGroup") as? String ?? "group.com.example.esp32agent"
    static let kind = "AgentStatus"
    private static let key = "agentSnapshot"
    static var defaults: UserDefaults { UserDefaults(suiteName: group) ?? .standard }

    static func load() -> AgentSnapshot? {
        defaults.data(forKey: key).flatMap { try? JSONDecoder().decode(AgentSnapshot.self, from: $0) }
    }

    static func save(_ snapshot: AgentSnapshot) {
        defaults.set(try? JSONEncoder().encode(snapshot), forKey: key)
    }
}
