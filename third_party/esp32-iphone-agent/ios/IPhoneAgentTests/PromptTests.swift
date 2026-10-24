import XCTest
@testable import IPhoneAgent

/// The prompts come from the bundled prompts/ folder.
final class PromptTests: XCTestCase {
    func testPromptsLoadFromTheBundle() {
        XCTAssertTrue(Prompts.planner.hasPrefix("You plan tasks on an iPhone."))
        XCTAssertTrue(Prompts.actorServer.contains("DO: <action>"))
        XCTAssertFalse(Prompts.appRules.isEmpty)
        XCTAssertFalse(Prompts.planner.hasSuffix("\n"))  // the file's final newline is not part of the prompt
        XCTAssertEqual(Prompts.actor, Prompts.actorServer + "\n" + Prompts.appRules)
        XCTAssertEqual(Prompts.plannerApp, Prompts.planner + "\n" + Prompts.appRules)
    }

    func testActionsTheParserKnowsAreDocumented() {
        for verb in ["tap X Y", "swipe X1", "path X1", "circle X Y R", "type TEXT", "key COMBO", "home | back | wait",
                     "replan", "done ANSWER", "fail REASON"] {
            XCTAssertTrue(Prompts.actor.contains(verb), verb)
        }
    }
}
