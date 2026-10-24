import Foundation
import UIKit

/// "Record me doing it": while the user does the task by hand, keep a keyframe each time the mirrored screen
/// changes and settles. On stop, the model infers the steps from the keyframes (fingers are invisible to the
/// board, so it guesses from before/after) and the draft opens in the review editor, unsure steps in yellow.
@MainActor final class DemoRecorder: ObservableObject {
    @Published private(set) var recording = false
    @Published private(set) var inferring = false
    @Published private(set) var keyframes = 0
    @Published private(set) var started = Date()
    @Published var error: String?
    private unowned let app: AppModel
    private var folder: URL?
    private var loop: Task<Void, Never>?
    static let maxFrames = 12

    init(app: AppModel) { self.app = app }

    static var root: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("recordings")
    }

    func start() {
        guard !recording, !app.agent.running else { return }
        let f = DateFormatter()
        f.dateFormat = "yyyyMMdd-HHmmss"
        f.locale = Locale(identifier: "en_US_POSIX")
        let dir = Self.root.appendingPathComponent(f.string(from: Date()))
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        folder = dir
        keyframes = 0
        error = nil
        started = Date()
        recording = true
        app.live.start(task: String(localized: "Recording a demonstration"), runID: "rec-" + dir.lastPathComponent,
                       limit: 0, phase: .recording)
        app.runMood = .listening
        loop = Task { [weak self] in await self?.capture() }
    }

    private func capture() async {
        if MockMode.on {
            while recording && !Task.isCancelled {
                save(AppModel.mockFrame)
                try? await Task.sleep(nanoseconds: 1_500_000_000)
            }
            return
        }
        try? await app.feed.stream(true)
        var last: Data?
        while recording && !Task.isCancelled {
            try? await Task.sleep(nanoseconds: 400_000_000)
            guard let frame = app.feed.frame, Vision.changed(last, frame) else { continue }
            await app.feed.settled()  // keep the screen after the change, not mid-animation
            last = app.feed.frame ?? frame
            save(last)
        }
    }

    private func save(_ jpeg: Data?) {
        guard let jpeg, let folder else { return }
        keyframes += 1
        try? jpeg.write(to: folder.appendingPathComponent(String(format: "k%03d.jpg", keyframes)))
        app.live.update(mood: .listening, step: keyframes,
                        line: String(localized: "\(keyframes) screens captured. Stop when you're done."), phase: .recording)
    }

    func cancel() {
        loop?.cancel()
        recording = false
        app.live.end(nil)
        app.runMood = nil
        if let folder { try? FileManager.default.removeItem(at: folder) }
    }

    /// Stop and turn the keyframes into an intent draft in the review editor.
    func stop() async {
        guard recording, let folder else { return }
        loop?.cancel()
        recording = false
        if !MockMode.on { try? await app.feed.stream(false) }
        app.live.end((.thinking, String(localized: "Working out the steps…")))
        inferring = true
        let bg = UIApplication.shared.beginBackgroundTask(withName: "infer")  // stopped from the Dynamic Island
        defer { inferring = false; app.runMood = nil; UIApplication.shared.endBackgroundTask(bg) }
        let frames = (1...max(keyframes, 1)).map { folder.appendingPathComponent(String(format: "k%03d.jpg", $0)) }
            .filter { FileManager.default.fileExists(atPath: $0.path) }
        guard !frames.isEmpty else { error = String(localized: "No screen changes were captured."); return }
        let picked = Self.sample(frames, Self.maxFrames)
        do {
            let reply = MockMode.on ? try await Self.mockReply() : try await infer(picked)
            let draft = Self.draft(reply, frames: picked, folder: folder)
            draft.saveEdits()
            app.notifier.post(.done, title: String(localized: "Intent draft ready"),
                              body: String(localized: "\(draft.steps.count) steps. Check the yellow ones."),
                              run: "rec-" + folder.lastPathComponent)
            app.review = draft
        } catch {
            self.error = error.localizedDescription
        }
    }

    private func infer(_ frames: [URL]) async throws -> String {
        let images = frames.compactMap { try? Data(contentsOf: $0) }
        let r = try await app.settings.client("planner").chat(system: Self.inferPrompt, text:
            "These are \(images.count) keyframes in order (frame 1 first). Infer the steps.",
            images: images, maxTokens: 1200, temperature: 0)
        return r.text
    }

    static func sample(_ frames: [URL], _ n: Int) -> [URL] {
        guard frames.count > n else { return frames }
        return (0..<n).map { frames[$0 * (frames.count - 1) / (n - 1)] }
    }

    static let inferPrompt = """
    You watch keyframes of a person using an iPhone, in order. Fingers are not visible: infer each action \
    from how the screen changed between two frames. Coordinates are 0-1000 of the screen width and height. \
    Reply in this format only:
    NAME: <short name for the task>
    PROMPT: <one instruction that would make an AI agent repeat this task>
    STEP <frame number the action was done on>: <command> | sure or unsure
    Commands: tap X Y, swipe X1 Y1 X2 Y2, type "text", home, back, wait.
    """

    /// Parses the model's reply into a draft (unparseable or "unsure" steps are marked uncertain).
    static func draft(_ reply: String, frames: [URL], folder: URL) -> ReviewDraft {
        var name = "", prompt = "", steps: [DraftStep] = []
        for raw in reply.split(separator: "\n") {
            let line = raw.trimmingCharacters(in: .whitespaces)
            if line.uppercased().hasPrefix("NAME:") { name = String(line.dropFirst(5)).trimmingCharacters(in: .whitespaces) }
            if line.uppercased().hasPrefix("PROMPT:") { prompt = String(line.dropFirst(7)).trimmingCharacters(in: .whitespaces) }
            guard line.uppercased().hasPrefix("STEP"), let colon = line.firstIndex(of: ":") else { continue }
            let n = Int(line[line.index(line.startIndex, offsetBy: 4)..<colon].trimmingCharacters(in: .whitespaces)) ?? 1
            let parts = line[line.index(after: colon)...].split(separator: "|", maxSplits: 1)
            let command = parts.first.map(String.init) ?? ""
            let sure = parts.count > 1 && parts[1].lowercased().contains("unsure") == false
            let action = (try? Actions.parseCommand(command)) ?? ["action": .str("note"), "text": .str(command)]
            steps.append(DraftStep(action: action, frame: frames[max(0, min(frames.count - 1, n - 1))],
                                   uncertain: !sure || action["action"]?.str == "note"))
        }
        return ReviewDraft(source: .recording, folder: folder, runID: nil, outcome: nil, steps: steps, name: name,
                           prompt: prompt, icon: "record.circle")
    }

    private static func mockReply() async throws -> String {
        try await Task.sleep(nanoseconds: 1_500_000_000)
        return """
        NAME: Daily 7 AM alarm
        PROMPT: Open Clock, add an alarm for 7:00 AM that repeats every day, then save it
        STEP 1: tap 500 814 | sure
        STEP 2: type "Clock" | sure
        STEP 3: tap 152 212 | sure
        STEP 4: tap 905 88 | unsure
        STEP 5: swipe 397 300 397 200 | unsure
        STEP 6: tap 150 395 | sure
        """
    }
}
