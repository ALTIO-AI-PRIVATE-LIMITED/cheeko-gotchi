import Foundation
import UIKit

/// The agent works on the phone this app runs on, so the app must be out of the way: Start sends it to the
/// Home Screen (and checks that iOS really put it in the background), and a run holds while it is in front.
extension AppModel {

    private var inBackground: Bool { UIApplication.shared.applicationState == .background }

    /// Leaves our own app without touching the screen: the board's Cmd+H, then its home flick as a fallback.
    /// The model is never asked to do this.
    func leaveApp() async -> Bool {
        for method in ["cmd+h", "home swipe"] {
            if inBackground { return true }
            do {
                switch method {
                case "cmd+h": try await pointer.key("cmd+h")
                default: try await pointer.goHome()
                }
            } catch {
                DebugLog.write("leave app: \(method) failed: \(error.localizedDescription)")
                continue
            }
            if await waitForBackground(2) { DebugLog.write("leave app: in background after \(method)"); return true }
            DebugLog.write("leave app: still in front after \(method)")
        }
        return false
    }

    /// Neither way worked: ask the user to swipe up, and wait for it (bounded).
    func askUserToLeave() async -> Bool {
        homeHint = String(localized: "Swipe up to go to the Home Screen, and I'll start.")
        defer { homeHint = nil }
        return await waitForBackground(30)
    }

    /// Before each look and gesture of a run. If the agent itself brought the app to the front (it tapped our
    /// Live Activity, a notification or an intent), or Continue was just tapped here, send the app away at once;
    /// if the user opened it, wait until they leave. The model never sees or taps this app's own screens.
    func holdWhileInFront() async throws {
        guard UIApplication.shared.applicationState == .active, agent.running, !MockMode.on else { return }
        let ours = now() - agent.lastActed < 3 || now() - resumeAsked < 5
        DebugLog.write("run: ESP32 Agent is in front (\(ours ? "after an agent action" : "opened by the user")); holding")
        let since = now()
        var away = false
        if ours { away = await leaveApp() }
        if !away {
            homeHint = String(localized: "Paused while you're here. Go to the Home Screen and I'll go on.")
            live.show(.pausedInApp, mood: .thinking)
            defer { homeHint = nil }
            while !inBackground {
                try Task.checkCancellation()
                if !feed.live {
                    _ = try await agent.waitForScreen()
                    live.show(.pausedInApp, mood: .thinking)
                }
                try await Task.sleep(nanoseconds: 300_000_000)
            }
        }
        _ = await feed.fresh(since: since, timeout: 3)  // a frame of the screen that is in front now
        await feed.settled()
        DebugLog.write("run: app in background again, going on")
    }

    private func waitForBackground(_ seconds: Double) async -> Bool {
        let deadline = now() + seconds
        while !inBackground {
            if now() > deadline { return false }
            try? await Task.sleep(nanoseconds: 100_000_000)
        }
        return true
    }
}
