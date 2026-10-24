import Foundation

/// The model prompts, kept as plain text in the repository's `prompts/` folder (bundled with the app) so they
/// can be read and tuned without touching Swift. actor/planner = the shared rules + the app-only rules.
enum Prompts {
    static let planner = load("planner")
    static let actorServer = load("actor")
    static let appRules = load("app-rules")

    static let actor = actorServer + "\n" + appRules
    static let plannerApp = planner + "\n" + appRules

    /// One file from the bundled prompts folder, without the file's final newline.
    static func load(_ name: String) -> String {
        guard let url = Bundle.main.url(forResource: name, withExtension: "md", subdirectory: "prompts"),
              let text = try? String(contentsOf: url, encoding: .utf8) else {
            fatalError("prompts/\(name).md is missing from the app bundle")
        }
        return text.hasSuffix("\n") ? String(text.dropLast()) : text
    }
}
