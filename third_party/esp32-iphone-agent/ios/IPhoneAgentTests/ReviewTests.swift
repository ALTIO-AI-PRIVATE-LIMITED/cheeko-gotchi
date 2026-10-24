import XCTest
@testable import IPhoneAgent

/// Needs-your-help pauses, intent hints, the AI optimizer's reply and demonstration drafts.
@MainActor final class ReviewTests: XCTestCase {
    func testGivingUpAsksForHelpAndGoesOnAfterTakeOver() async {
        let model = SystemRecorder { n in
            switch n {
            case 1: return "1. open it"
            case 2: return "THINK: I need a login\nDO: fail login required"
            case 3: return "1. go on"
            default: return "THINK: there\nDO: done ok"
            }
        }
        let agent = AgentLoop(phone: FakeGestures(), screen: FakeScreen(), actor: model, planner: model, trace: nil)
        var asked: [String] = []
        agent.onHelp = { reason in asked.append(reason); return .resume }
        await agent.run()
        XCTAssertEqual(asked, ["login required"])
        XCTAssertEqual(agent.steps.map { $0["action"]?.str },
                       ["plan", "fail", "help", "takeover", "replan (stuck)", "done"])
    }

    func testStopFromHelpEndsTheRun() async {
        let model = SystemRecorder { n in n == 1 ? "1. open it" : "THINK: stuck\nDO: fail no button" }
        let agent = AgentLoop(phone: FakeGestures(), screen: FakeScreen(), actor: model, planner: model, trace: nil)
        agent.onHelp = { _ in .stop }
        await agent.run()
        XCTAssertEqual(agent.steps.last?["action"], .str("stopped"))
    }

    func testIntentHintsAreAppendedToTheActorPromptOnly() async {
        let model = SystemRecorder { n in n == 1 ? "1. go" : "THINK: there\nDO: done ok" }
        let agent = AgentLoop(phone: FakeGestures(), screen: FakeScreen(), actor: model, planner: model, trace: nil)
        agent.hints = ["Type the time instead of scrolling"]
        await agent.run()
        let rule = "\n" + AssistiveTouchSpots.rule
        XCTAssertEqual(model.systems[0], Prompts.plannerApp + rule)
        XCTAssertEqual(model.systems[1], Prompts.actor + rule + "\n\nHints for this task:\n- Type the time instead of scrolling")
        agent.hints = []
        XCTAssertEqual(agent.actorPrompt, Prompts.actor + rule)
    }

    func testOptimizerReplyIsParsed() {
        let r = ReviewDraft.parseOptimized("Sure.\nPROMPT: Set a 7 AM alarm\nHINTS:\n- Type the time\n- Tap Save\n")
        XCTAssertEqual(r?.prompt, "Set a 7 AM alarm")
        XCTAssertEqual(r?.hints, ["Type the time", "Tap Save"])
        XCTAssertNil(ReviewDraft.parseOptimized("no idea"))
    }

    func testDemonstrationReplyBecomesADraftWithUnsureSteps() {
        let frames = (1...3).map { URL(fileURLWithPath: "/tmp/k00\($0).jpg") }
        let draft = DemoRecorder.draft("""
        NAME: Alarm
        PROMPT: Set an alarm
        STEP 1: tap 500 814 | sure
        STEP 3: type "0700" | unsure
        STEP 2: something odd | sure
        """, frames: frames, folder: URL(fileURLWithPath: "/tmp/none"))
        XCTAssertEqual(draft.name, "Alarm")
        XCTAssertEqual(draft.prompt, "Set an alarm")
        XCTAssertEqual(draft.steps.map(\.name), ["tap", "type", "note"])
        XCTAssertEqual(draft.steps.map(\.uncertain), [false, true, true])
        XCTAssertEqual(draft.steps[1].frame, frames[2])
    }

    func testCorrectionsReachTheOptimizerInput() {
        let draft = ReviewDraft(source: .run, folder: nil, runID: nil, outcome: .failed,
                                steps: [DraftStep(action: ["action": .str("tap"), "x": .int(1), "y": .int(2)])],
                                name: "", prompt: "Set an alarm")
        draft.setFix(draft.steps[0].id, point: CGPoint(x: 905, y: 88), note: "tap the +")
        draft.insert(after: draft.steps[0].id, text: "then tap Save")
        let text = draft.optimizerInput()
        XCTAssertTrue(text.contains("[USER: wrong]"))
        XCTAssertTrue(text.contains("the right spot is 905 88"))
        XCTAssertTrue(text.contains("[USER NOTE: tap the +]"))
        XCTAssertTrue(text.contains("[ADDED BY USER]"))
        XCTAssertEqual(draft.corrections, 2)
    }
}

/// A fake model that also keeps each system prompt.
@MainActor final class SystemRecorder: ChatModel {
    let model = "fake"
    private(set) var systems: [String] = []
    private let reply: (Int) -> String

    init(_ reply: @escaping (Int) -> String) { self.reply = reply }

    func chat(system: String, text: String, image: Data?, maxTokens: Int, temperature: Double) async throws -> ChatResult {
        systems.append(system)
        return ChatResult(text: reply(systems.count), firstToken: nil, total: 0)
    }
}
