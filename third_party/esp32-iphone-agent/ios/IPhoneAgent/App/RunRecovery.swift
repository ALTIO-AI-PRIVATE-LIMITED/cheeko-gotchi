import Foundation

/// Self-healing runs: a lost board, hotspot or mirroring pauses the run (AgentLoop.waitForScreen); this side
/// reports each stage (log, Live Activity, in-app banner, a notification when the user has to act) and
/// restores the board's state once the screen is live again.
extension AppModel {

    func wireRecovery() {
        agent.link = { [unowned self] in
            Recovery.Link(connected: board.connected, sta: board.status?.sta, ap: board.status?.ap,
                          frameAt: FrameMode.current == .snap ? now() : feed.updated)  // snap mode: restore snaps a fresh one
        }
        agent.onRecover = { [weak self] stage in self?.recover(stage) }
        agent.restore = { [weak self] in try await self?.restoreRun() }
    }

    func recover(_ stage: Recovery.Stage?) {
        let was = recovering
        recovering = stage
        guard let stage else {
            notifier.clearLinkAsks()
            runMood = .thinking
            return
        }
        if was == nil { recoverySince = Date() }
        if stage.part != .board, was == nil || was?.part == .board, let up = board.status?.up, up < 60_000 {
            DebugLog.write("recover: board rebooted \(up / 1000) s ago (reset: \(board.status?.rst ?? "?"))")
        }
        DebugLog.write("recover: \(stage.logLine)")
        runMood = .listening
        live.update(mood: .listening, step: moveCount, line: stage.line, phase: .running, status: stage.status)
        if stage.ask { notifier.post(kind(stage.part), title: title(stage.part), body: stage.line, run: runID) }
    }

    /// Live again: the board may have rebooted (stream off, button state unknown) and the screen moved on.
    func restoreRun() async throws {
        DebugLog.write(String(format: "recover: live again after %.0f s", Date().timeIntervalSince(recoverySince)))
        if FrameMode.current == .stream && !feed.streamOn { try await feed.stream(true) }
        DebugLog.write("recover: fresh frame (" + (try await feed.currentFrame()) + ")")
        try await pointer.reset()  // a gesture cut off mid-way may have left the button down
        DebugLog.write("recover: button released, pointer re-homed; going on from step \(moveCount)")
        live.update(mood: .thinking, step: moveCount, line: String(localized: "Back online. Going on from here…"),
                    phase: .running, status: .looking)
    }

    private func kind(_ part: Recovery.Part) -> Notifier.Kind {
        switch part {
        case .board: return .board
        case .hotspot: return .wifi
        case .mirroring: return .mirroring
        }
    }

    private func title(_ part: Recovery.Part) -> String {
        switch part {
        case .board: return String(localized: "Board disconnected")
        case .hotspot: return String(localized: "iPhone left the board's Wi-Fi")
        case .mirroring: return String(localized: "Start Screen Mirroring to continue")
        }
    }
}
