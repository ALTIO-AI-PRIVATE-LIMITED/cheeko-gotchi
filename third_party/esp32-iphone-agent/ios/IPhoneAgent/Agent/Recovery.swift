import Foundation

/// Self-healing after a lost link: which part is missing (board, hotspot, mirroring), when to ask the user,
/// and when to give up. Pure: the caller polls it with the link state and the time.
struct Recovery {
    /// What the app sees of the link right now.
    struct Link: Equatable {
        var connected: Bool       // BLE link to the board is up
        var sta: Int?             // phones joined to the board's hotspot (nil = unknown)
        var ap: String?           // idle | connected | streaming
        var frameAt: Double       // when the newest decoded frame arrived
    }

    enum Part: Equatable { case board, hotspot, mirroring }

    struct Stage: Equatable {
        var part: Part
        var ask: Bool  // waited long enough: the user has to do something
    }

    enum Next: Equatable {
        case wait(Stage)
        case live
        case giveUp(String)
    }

    /// How long each part may come back by itself before the user is asked; then the whole limit.
    static let quiet: [Part: Double] = [.board: 30, .hotspot: 30, .mirroring: 20]
    static let limit = 300.0

    let started: Double
    let limit: Double
    private(set) var stage: Stage?
    private var partSince = 0.0

    init(at t: Double, limit: Double = Self.limit) { started = t; self.limit = limit }

    /// Live once the board is back, mirroring streams and a frame newer than the loss has been decoded.
    mutating func next(_ l: Link, at t: Double) -> Next {
        let part: Part? = !l.connected ? .board
            : l.sta == 0 ? .hotspot
            : l.ap != "streaming" || l.frameAt <= started ? .mirroring
            : nil
        guard let part else { stage = nil; return .live }
        if part != stage?.part { partSince = t }
        stage = Stage(part: part, ask: t - partSince >= Self.quiet[part]!)
        if t - started >= limit { return .giveUp(Self.reason(part)) }
        return .wait(stage!)
    }

    static func reason(_ part: Part) -> String {
        switch part {
        case .board: return String(localized: "the board did not come back in 5 minutes")
        case .hotspot: return String(localized: "the iPhone did not rejoin the board's Wi-Fi in 5 minutes")
        case .mirroring: return String(localized: "Screen Mirroring did not come back in 5 minutes")
        }
    }
}

extension Recovery.Stage {
    /// For the log: "recover: …".
    var logLine: String {
        switch (part, ask) {
        case (.board, false): return "BLE link lost; reconnecting"
        case (.board, true): return "board still gone; asked the user to check it"
        case (.hotspot, false): return "board up, iPhone not on its hotspot (sta 0); waiting for iOS to rejoin"
        case (.hotspot, true): return "iPhone still off the hotspot; asked the user to join it"
        case (.mirroring, false): return "board and hotspot up, no mirrored frame; waiting for iOS to resume mirroring"
        case (.mirroring, true): return "mirroring still off; asked the user to start it"
        }
    }

    /// Dynamic Island label: compact and full.
    var status: IslandStatus {
        switch (part, ask) {
        case (.board, _):
            return IslandStatus(short: String(localized: "Reconnecting…"), full: String(localized: "Reconnecting…"), look: nil)
        case (.hotspot, false):
            return IslandStatus(short: String(localized: "Joining Wi-Fi…"), full: String(localized: "Rejoining the board's Wi-Fi…"), look: nil)
        case (.hotspot, true):
            return IslandStatus(short: String(localized: "Join Wi-Fi"), full: String(localized: "Join the board's Wi-Fi to continue"), look: nil)
        case (.mirroring, false):
            return IslandStatus(short: String(localized: "Mirroring…"), full: String(localized: "Waiting for mirroring…"), look: nil)
        case (.mirroring, true):
            return IslandStatus(short: String(localized: "Start Mirroring"),
                                full: String(localized: "Tap Screen Mirroring → ESP32 Agent to continue"), look: nil)
        }
    }

    /// One sentence for the Live Activity line, the in-app banner and the notification body.
    var line: String {
        switch (part, ask) {
        case (.board, false): return String(localized: "Lost the board. Reconnecting…")
        case (.board, true): return String(localized: "The board is still gone. Check that it is plugged in; the run goes on when it is back.")
        case (.hotspot, false): return String(localized: "The board restarted. Waiting for the iPhone to rejoin its Wi-Fi…")
        case (.hotspot, true): return String(localized: "Join the board's Wi-Fi in Settings › Wi-Fi; the run goes on when it is back.")
        case (.mirroring, false): return String(localized: "Board is back. Waiting for Screen Mirroring to resume…")
        case (.mirroring, true): return String(localized: "Open Control Center, tap Screen Mirroring → ESP32 Agent; the run goes on from here.")
        }
    }
}
