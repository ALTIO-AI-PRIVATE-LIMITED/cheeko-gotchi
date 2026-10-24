import XCTest
@testable import IPhoneAgent

/// What the character says in the Dynamic Island, and where its eyes go.
@MainActor final class IslandTests: XCTestCase {
    func testSwipeGazeUsesDirectionRatherThanScreenPosition() {
        let right: JObject = ["action": .str("swipe"), "x1": .int(100), "x2": .int(200), "y1": .int(200), "y2": .int(200)]
        XCTAssertEqual(IslandStatus.acting(right).short, "Swiping right")
        XCTAssertEqual(IslandStatus.gaze(right), CGPoint(x: 1, y: 0))
        let up: JObject = ["action": .str("swipe"), "x1": .int(800), "x2": .int(800), "y1": .int(900), "y2": .int(700)]
        XCTAssertEqual(IslandStatus.acting(up).short, "Swiping up")
        XCTAssertEqual(IslandStatus.gaze(up), CGPoint(x: 0, y: -1))
    }

    func testMinimalTextKeepsPendingPlanningThinkingAndRetryDistinct() {
        let states = [IslandStatus.waiting, .planning, .thinking, .retrying]
        XCTAssertEqual(states.compactMap(\.minimal), ["Wait", "Plan", "Think", "Retry"])
        XCTAssertTrue(states.allSatisfy(\.waits))
        XCTAssertEqual(IslandStatus.acting(["action": .str("swipe")]).minimal, "Swipe")
        XCTAssertEqual(IslandStatus.acting(["action": .str("wait")]).minimal, "Wait")
    }

    func testTargetFromThought() {
        XCTAssertEqual(IslandStatus.target("I am on the home screen. I tap the Search pill to find Clock."), "Search")
        XCTAssertEqual(IslandStatus.target("My last tap opened Alarms. I tap + to add an alarm."), "+")
        XCTAssertEqual(IslandStatus.target("The time is 08:00. I open Repeat."), "Repeat")
        XCTAssertEqual(IslandStatus.target("I tap “Save” at the top right."), "Save")
        XCTAssertNil(IslandStatus.target("Clock is the top result, so I open it."))
    }

    func testLabelsFitTheCompactIsland() {
        let tap: JObject = ["thought": .str("I tap the Clock app."), "action": .str("tap"), "x": .int(900), "y": .int(100)]
        let s = IslandStatus.acting(tap)
        XCTAssertEqual(s.full, "Tapping Clock")
        XCTAssertLessThanOrEqual(s.short.count, IslandStatus.compactLimit)
        XCTAssertEqual(s.look!.x, 0.8, accuracy: 0.001)
        XCTAssertEqual(s.look!.y, -0.8, accuracy: 0.001)
        let long = IslandStatus.acting(["action": .str("type"), "text": .str("Set an alarm for eight o'clock")])
        XCTAssertLessThanOrEqual(long.short.count, IslandStatus.compactLimit)
        XCTAssertEqual(IslandStatus.acting(["action": .str("type"), "text": .str("0800")]).short, "Typing 0800")
        XCTAssertTrue(IslandStatus.thinking.waits)
    }
}
