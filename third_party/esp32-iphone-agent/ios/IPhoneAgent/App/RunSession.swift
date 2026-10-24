import Combine
import Foundation
import UIKit

/// Working mode: Start → the character takes off → the phone goes to the Home Screen and the agent runs, with
/// the Live Activity as its UI, a low-fps video of the mirror stream, "needs your help" pauses, notifications,
/// and the review editor at the end.
extension AppModel {

    var moveCount: Int { agent.steps.filter { !StepText.notMoves.contains($0["action"]?.str ?? "") }.count }
    var runID: String { traces.dir?.lastPathComponent ?? "" }

    /// Start a task (or a saved intent: its checklist goes to the actor as hints).
    func run(_ task: String, intent: SavedTask? = nil) async {
        guard !startingAgent, !agent.running, !recorder.recording else { return }
        startingAgent = true
        defer { startingAgent = false }
        currentIntent = intent
        lastTask = task
        agent.hints = intent?.checklist ?? []
        departing = true
        try? await Task.sleep(nanoseconds: 800_000_000)  // "See you later" (DepartureOverlay)
        departing = false
        await startAgent(task)
    }

    func startAgent(_ task: String) async {
        if MockMode.on { return mockRun(task) }
        guard settings.isConfigured else {
            say(String(localized: "Set up the model in Settings first."))
            return
        }
        guard await calibrator.ensureCalibrated() else {
            say(String(localized: "The pointer isn't calibrated yet."))
            return
        }
        do {
            agent.actor = try await settings.client("actor")
            agent.planner = try await settings.client("planner")
        } catch {
            say("LLM: \(error.localizedDescription)")
            return
        }
        // Start is tapped in this app, so: Live Activity, go Home, let the screen settle, THEN the first
        // frame for the planner. Each sub-step is timed in the log.
        let t0 = now()
        func mark(_ what: String) { DebugLog.write(String(format: "start: %@ (+%.0f ms)", what, (now() - t0) * 1000)) }
        traces.start(task: task)  // the run folder exists from here: a failed start keeps its log and video
        startVideo()
        FrameMode.fallback = nil  // each run starts in the chosen mode
        let snapMode = FrameMode.current == .snap
        live.start(task: task, runID: runID, limit: agent.maxSteps)
        mark("Live Activity requested")
        do {
            if snapMode {
                if feed.streamOn { try await feed.stream(false) }
                mark("snap-per-step mode (stream off)")
            } else {
                try await feed.stream(true)  // the agent waits for the screen to settle: it needs every change
                mark("stream on")
            }
            backgroundTask = UIApplication.shared.beginBackgroundTask(withName: "agent") { [weak self] in
                self?.say("iOS is ending background time; BLE status keeps the run going")
                self?.endBackgroundTask()
            }
            let left = now()
            var away = await leaveApp()
            if !away { away = await askUserToLeave() }
            guard away else {  // never let the agent start on its own app
                throw DeviceError(message: String(localized: "ESP32 Agent is still on screen"))
            }
            mark("app in background")
            if !snapMode {
                let changed = await feed.fresh(since: left, timeout: 3) != nil
                await feed.settled()
                mark(changed ? "screen changed and settled" : "no screen change seen in 3 s; settled")
            }
            mark("first frame: " + (try await feed.currentFrame()))
            try agent.start(task, traceStarted: true)
            mark("agent started")
            say("started: \(task)")
            began(task, early: true)
        } catch {
            say("cannot start: \(error.localizedDescription)")
            traces.write(feed.frame, kind: "step", ["action": .str("error"),
                                                    "reason": .str("cannot start: \(error.localizedDescription)")])
            videoSink = nil
            saveVideoFrame(feed.frame)
            videoDir = nil
            live.end((.failure, String(localized: "Could not start")))
            endBackgroundTask()
            runs.reload()
            runMood = .failure
            notifier.post(.failed, title: String(localized: "Could not start"),
                          body: error.localizedDescription, run: runID)
            openReview(run: runID)
        }
    }

    /// Emergency stop: cancel the agent and release the mouse button.
    func stopAgent() async {
        answerHelp(.stop)
        mockJob?.cancel()
        agent.stop()
        try? await Task.sleep(nanoseconds: 100_000_000)  // let a gesture in flight finish its own release
        if board.connected && !MockMode.on {
            do { try await pointer.reset() } catch { say("release failed: \(error.localizedDescription)") }
        }
    }

    /// A run began: the character thinks, the Live Activity appears, the video starts.
    /// early: the Live Activity and the video were started before the agent (a real run's start sequence).
    func began(_ task: String, early: Bool = false) {
        moodReset?.cancel()
        runMood = .thinking
        runStarted = Date()
        if !early {
            live.start(task: task, runID: runID, limit: agent.maxSteps)
            if !MockMode.on { startVideo() }  // always: the review needs it, even for a run that stops at once
        }
        runs.reload()
    }

    /// After each recorded step: a short "acting" hop, then back to thinking while the model looks again.
    func stepped() {
        runs.reload()
        guard agent.running, let last = agent.steps.last, help == nil else { return }
        let moved = !StepText.notMoves.contains(last["action"]?.str ?? "")
        runMood = moved ? .acting : .thinking
        live.update(mood: runMood ?? .thinking, step: moveCount, line: StepText.line(last), phase: .running)
        if moved {
            moodReset?.cancel()
            moodReset = Task { [weak self] in
                try? await Task.sleep(nanoseconds: 700_000_000)
                guard !Task.isCancelled, self?.agent.running == true, self?.help == nil else { return }
                self?.runMood = .thinking
            }
        }
    }

    /// Between steps: what the character says in the Dynamic Island and where its eyes go.
    func doing(_ d: AgentLoop.Doing) {
        guard agent.running, help == nil else { return }
        switch d {
        case .looking: live.show(.looking, mood: .thinking)
        case .waiting: live.show(.waiting, mood: .thinking)
        case .planning: live.show(.planning, mood: .thinking)
        case .thinking: live.show(.thinking, mood: .thinking)
        case .retrying: live.show(.retrying, mood: .thinking)
        case .acting(let act): live.show(.acting(act), mood: .acting)
        }
    }

    /// The run ended: outcome on the character, Live Activity and a notification; then the review opens.
    func ended() {
        videoSink = nil
        saveVideoFrame(feed.frame)  // the last screen too, however the run ended
        videoDir = nil
        help = nil
        takingOver = false
        recovering = nil
        notifier.clearHelp()
        notifier.clearLinkAsks()
        let last = agent.steps.last
        let outcome = Outcome(last: last?["action"]?.str, running: false)
        let seconds = Date().timeIntervalSince(runStarted)
        runMood = outcome.mood
        live.end((outcome.mood, last.map(StepText.line) ?? ""))
        runs.reload()
        let summary = String(localized: "\(moveCount) steps · \(seconds.durationText)")
        switch outcome {
        case .done:
            Haptics.success()
            let answer = last?["answer"]?.str ?? agent.task
            notifier.post(.done, title: String(localized: "Done: \(answer)"), body: summary, run: runID)
        case .failed:
            let why = last?["reason"]?.str ?? ""
            notifier.post(.failed, title: String(localized: "Not finished: \(why)"), body: summary, run: runID)
        default: break
        }
        if let intent = currentIntent {
            intents.record(intent.id, RunStat(run: runID, ok: outcome == .done, seconds: seconds))
        }
        if outcome != .stopped || UIApplication.shared.applicationState == .active { openReview(run: runID) }
        moodReset?.cancel()
        moodReset = Task { [weak self] in
            try? await Task.sleep(nanoseconds: 5_000_000_000)
            guard !Task.isCancelled, self?.agent.running == false else { return }
            self?.runMood = nil
        }
    }

    func agentFinished() {
        ended()
        if let last = agent.steps.last { say("finished: \(last["action"]?.str ?? "") \(last["reason"]?.str ?? last["answer"]?.str ?? "")") }
        Task { try? await feed.stream(false) }
        endBackgroundTask()
    }

    func endBackgroundTask() {
        if backgroundTask != .invalid { UIApplication.shared.endBackgroundTask(backgroundTask) }
        backgroundTask = .invalid
    }

    // MARK: - review

    func outcome(for run: Run) -> Outcome {
        run.outcome(isActive: run.id == runID && (startingAgent || agent.running))
    }

    /// Opens the review editor for a run (with its intent, if it ran one).
    func openReview(run id: String) {
        runs.reload()
        guard let run = runs.run(id) else { return }
        let intent = intents.items.first { $0.refRun == id || $0.runs.contains { $0.run == id } }
        review = ReviewDraft(run: run, intent: intent)
    }

    /// The model rewrites the prompt and checklist from the trace and the user's corrections.
    func optimize(_ draft: ReviewDraft) async throws -> (prompt: String, hints: [String]) {
        if MockMode.on {
            try await Task.sleep(nanoseconds: 1_400_000_000)
            return (String(localized: "Open Clock › Alarms, tap +, type 0700 for the time, set Repeat to every day, then tap Save"),
                    [String(localized: "Type the time instead of scrolling the wheel"),
                     String(localized: "The + button is at the top right of Alarms"),
                     String(localized: "Check all seven days under Repeat, then tap Back")])
        }
        let reply = try await settings.client("planner").chat(system: ReviewDraft.optimizerPrompt,
                                                              text: draft.optimizerInput(), image: nil,
                                                              maxTokens: 800, temperature: 0.2)
        guard let result = ReviewDraft.parseOptimized(reply.text) else {
            throw LLMError(message: String(localized: "The model gave no improved prompt."))
        }
        return result
    }

    // MARK: - needs your help

    /// The agent is stuck: pause, tell the user (Live Activity + notification) and wait for their choice.
    func needsHelp(_ reason: String) async -> AgentLoop.HelpChoice {
        help = reason.isEmpty ? String(localized: "I'm stuck on this step.") : reason
        runMood = .failure
        let line = String(localized: "Needs your help: \(help ?? "")")
        live.update(mood: .failure, step: moveCount, line: line, phase: .help, status: .help)
        notifier.post(.help, title: String(localized: "Needs your help"), body: help ?? "", run: runID)
        return await withCheckedContinuation { helpWait = $0 }
    }

    func answerHelp(_ choice: AgentLoop.HelpChoice) {
        guard let wait = helpWait else { return }
        resumeAsked = now()  // Continue tapped in this app: the run sends it away before looking again
        helpWait = nil
        help = nil
        takingOver = false
        notifier.clearHelp()
        runMood = choice == .stop ? .idle : .thinking
        live.update(mood: runMood ?? .thinking, step: moveCount, line: String(localized: "Going on from here…"), phase: .running,
                    status: .looking)
        wait.resume(returning: choice)
    }

    /// The user does the step by hand, then taps Continue.
    func takeOver() {
        guard help != nil else { return }
        takingOver = true
        live.update(mood: .listening, step: moveCount, line: String(localized: "Your turn. Tap Continue when you're done."),
                    phase: .help, status: .yourTurn)
        notifier.post(.takeover, title: String(localized: "Your turn"),
                      body: String(localized: "Do the step yourself, then tap Continue."), run: runID)
    }

    /// Live Activity buttons.
    func control(_ command: AgentCommand) async {
        switch command {
        case .stop: await stopAgent()
        case .resume: answerHelp(.resume)
        case .skip: answerHelp(.skip)
        case .takeOver: takeOver()
        case .stopRecording: await recorder.stop()
        }
    }

    /// Notification actions; tapping the notification itself opens the review.
    func notificationAction(_ action: Notifier.Action?, run: String) {
        switch action {
        case .stop?: Task { await stopAgent() }
        case .resume?: answerHelp(.resume)
        case .skip?: answerHelp(.skip)
        case .takeOver?: takeOver()
        case .retry?: Task { await self.run(lastTask, intent: currentIntent) }
        case .review?, nil:
            if agent.running { tab = .agent } else if !run.hasPrefix("rec-") && review == nil { openReview(run: run) }
        }
    }

    // MARK: - recording the run

    /// Saves the mirror stream at ~2 fps into the run folder (video/<ms>.jpg), for the review's video view.
    private func startVideo() {
        guard let dir = traces.dir?.appendingPathComponent("video") else { return }
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        videoDir = dir
        videoSink = feed.$frame.compactMap { $0 }
            .throttle(for: .milliseconds(500), scheduler: RunLoop.main, latest: true)
            .sink { [weak self] jpeg in self?.saveVideoFrame(jpeg) }
    }

    /// Each frame is written at once, so a run that stops or fails early still has its video.
    func saveVideoFrame(_ jpeg: Data?) {
        guard let jpeg, let dir = videoDir else { return }
        try? jpeg.write(to: dir.appendingPathComponent("\(Int(Date().timeIntervalSince1970 * 1000)).jpg"))
    }
}
