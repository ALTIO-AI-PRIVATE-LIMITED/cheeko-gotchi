import ActivityKit
import Foundation

/// Starts, updates and ends the Live Activity of a run or a recording. While the agent works the app is in
/// the background, so this is the user's view of the run; the app updates it whenever BLE wakes it.
///
/// Larger presentations animate gaze changes when actions update `look`. The former random idle gaze
/// loop is preserved but disabled to keep the primary text-only minimal presentation stable.
/// Updates go through a token bucket (burst 2, then 1 per second), so the system never sees more than ~1/s.
@MainActor final class LiveActivityController {
    typealias State = AgentActivityAttributes.ContentState
    private var activity: Activity<AgentActivityAttributes>?
    private var last: State?    // the newest state (sent or waiting for a token)
    private var sent: State?
    private var tokens = 2.0, refilled = 0.0
    private var flush: Task<Void, Never>?
    private var darting: Task<Void, Never>?

    func start(task: String, runID: String, limit: Int, phase: AgentActivityAttributes.Phase = .running) {
        end(nil)
        guard ActivityAuthorizationInfo().areActivitiesEnabled else { return DebugLog.write("Live Activity: disabled in Settings") }
        let line = phase == .recording ? String(localized: "Do the task yourself. I'm watching.")
            : String(localized: "Reading the screen…")
        let status = phase == .recording
            ? IslandStatus(short: String(localized: "Watching you"), full: String(localized: "Watching you"), look: CGPoint(x: 0, y: 0.6))
            : IslandStatus.looking
        var state = State(mood: phase == .recording ? .listening : .thinking, step: 0, limit: limit, line: line,
                          phase: phase, since: Date())
        apply(status, to: &state)
        do {
            activity = try Activity.request(attributes: AgentActivityAttributes(task: task, runID: runID, name: Settings.storedAgentName),
                                            content: .init(state: state, staleDate: nil))
            last = state
            sent = state
            tokens = 1
            refilled = now()
            DebugLog.write("Live Activity: started (\(phase.rawValue)) run \(runID)")
            if status.waits { startDarting() }
        } catch {
            activity = nil
            DebugLog.write("Live Activity: start failed: \(error.localizedDescription)")
        }
    }

    /// Step counter, the model's words, the mood; `status` also changes what the character says and where it looks.
    func update(mood: Mood, step: Int, line: String, phase: AgentActivityAttributes.Phase? = nil, status: IslandStatus? = nil) {
        guard var state = last else { return }
        state.mood = mood
        state.step = step
        state.line = line
        if let phase { state.phase = phase }
        if let status { apply(status, to: &state) }
        push(state)
    }

    /// What the character is doing now: looking / thinking (the eyes dart) or a gesture (the eyes go to its target).
    func show(_ status: IslandStatus, mood: Mood? = nil) {
        guard var state = last, !state.finished else { return }
        apply(status, to: &state)
        if let mood { state.mood = mood }
        push(state)
    }

    /// Shows the outcome for a while, then the system removes it. Tapping it opens the review.
    func end(_ outcome: (mood: Mood, line: String)?) {
        stopDarting()
        flush?.cancel()
        flush = nil
        guard let activity else { return }
        var state = last ?? State(mood: .idle, step: 0, limit: 0, line: "")
        if let outcome {
            state.mood = outcome.mood
            state.line = outcome.line
            if state.phase != .recording { apply(.ended(outcome.mood), to: &state) }
        }
        state.finished = true
        state.waitingSince = nil
        DebugLog.write("Live Activity: ended" + (outcome.map { " (\($0.mood.rawValue))" } ?? ""))
        self.activity = nil
        last = nil
        sent = nil
        Task {
            await activity.end(.init(state: state, staleDate: nil),
                               dismissalPolicy: outcome == nil ? .immediate : .after(.now + 15 * 60))
        }
    }

    // MARK: - status and eyes

    private func apply(_ status: IslandStatus, to state: inout State) {
        if status.waits {
            if state.label != status.short || state.waitingSince == nil { state.waitingSince = Date() }
        } else { state.waitingSince = nil }
        state.minimalLabel = status.minimal ?? status.short
        state.label = status.short
        state.labelFull = status.full
        if let look = status.look {
            state.lookX = look.x
            state.lookY = look.y
        }
        if status.waits { startDarting() } else { stopDarting() }
    }

    /// While waiting: glance somewhere else every 1.4-2.2 s, mostly sideways, never twice at the same spot.
    private func startDarting() {
        // Preserve the gaze implementation, but do not send cosmetic updates while the primary
        // minimal presentation is text-only. ActivityKit does not tell this controller which
        // presentation is visible; repeated updates can animate the entire island. Action-driven
        // gaze and mood updates remain enabled in the larger presentations.
        guard Self.idleGazeEnabled else { return }
        guard darting == nil else { return }
        darting = Task { [weak self] in
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: UInt64.random(in: 1_400_000_000...2_200_000_000))
                guard !Task.isCancelled, let self, var state = self.last, !state.finished else { return }
                var x = state.lookX
                while abs(x - state.lookX) < 0.5 { x = [-0.9, -0.55, 0, 0.55, 0.9].randomElement()! }
                state.lookX = x
                state.lookY = Double.random(in: -0.35...0.15)
                self.push(state)
            }
        }
    }

    private func stopDarting() {
        darting?.cancel()
        darting = nil
    }

    // MARK: - rate limit

    /// Sends now if a token is left, else once one refills (the newest state wins).
    private func push(_ state: State) {
        guard activity != nil, state != last || state != sent else { return }
        last = state
        guard flush == nil else { return }  // a send is already scheduled; it picks up `last`
        refill()
        if tokens >= 1 { return send() }
        let wait = (1 - tokens) * Self.perSecond
        flush = Task { [weak self] in
            try? await Task.sleep(nanoseconds: UInt64(wait * 1_000_000_000))
            guard !Task.isCancelled, let self else { return }
            self.flush = nil
            self.refill()
            self.send()
        }
    }

    private static let perSecond = 1.0, burst = 2.0
    private static let idleGazeEnabled = false

    private func refill() {
        let t = now()
        tokens = min(Self.burst, tokens + (t - refilled) / Self.perSecond)
        refilled = t
    }

    private func send() {
        guard let activity, let state = last, state != sent else { return }
        tokens -= 1
        sent = state
        Task { await activity.update(.init(state: state, staleDate: nil)) }
    }
}
