import XCTest
@testable import IPhoneAgent

/// Runs read back from trace folders (what the Runs list and the replay player show), and saved intents.
@MainActor final class StoreTests: XCTestCase {
    func testEmptyActiveRunIsNotFailedButAbandonedRunIs() {
        let run = Run(id: "pending", folder: URL(fileURLWithPath: "/tmp/pending"),
                      task: "Open Notes", date: Date(), steps: [])
        XCTAssertEqual(run.outcome(isActive: true), .running)
        XCTAssertEqual(run.outcome(isActive: false), .failed)
    }

    func testRunIsReadBackWithFramesOutcomeAndDuration() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        let traces = TraceStore(root: root)
        traces.start(task: "Open Settings")
        traces.write(Data([0xFF, 0xD8]), kind: "llm", ["role": .str("actor")])
        traces.write(kind: "step", ["action": .str("tap"), "x": .int(500), "y": .int(300), "changed": .bool(true)])
        traces.write(kind: "step", ["action": .str("done"), "answer": .str("ok")])

        let store = RunStore(traces: traces)
        store.reload()
        let run = try XCTUnwrap(store.runs.first)
        XCTAssertEqual(run.task, "Open Settings")
        XCTAssertEqual(run.steps.count, 2)
        XCTAssertEqual(run.moves.count, 1)
        XCTAssertEqual(run.outcome, .done)
        XCTAssertNotNil(run.steps[0].frame)
        XCTAssertNotNil(run.duration)
        XCTAssertEqual(StepText.title(run.steps[0].action), "Tap at 500, 300")
        try? FileManager.default.removeItem(at: root)
    }

    func testIntentsAreSavedAndDeleted() {
        let defaults = UserDefaults(suiteName: UUID().uuidString)!
        let store = IntentStore(defaults: defaults)
        XCTAssertFalse(store.items.isEmpty)  // seeded with the examples
        store.add(name: "", prompt: "Open Notes and write hello")
        XCTAssertEqual(store.items.first?.name, "Open Notes and write hello")
        XCTAssertEqual(IntentStore(defaults: defaults).items.count, store.items.count)
        store.delete(at: IndexSet(integer: 0))
        XCTAssertEqual(IntentStore(defaults: defaults).items.first?.name, IntentStore.examples[0].name)
    }
}
