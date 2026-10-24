import Foundation

/// An unsuccessful image check does not imply an authentication or connectivity failure.
enum ModelConnectionCheck {
    static func validate(_ reply: ChatResult) throws {
        let answer = reply.text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !answer.isEmpty else {
            throw LLMError(message: String(localized: "Connected, but the model returned no answer. Try again or choose another vision model."))
        }
        let words = answer.lowercased().split { !$0.isLetter }
        guard words == ["red"] else {
            throw LLMError(message: String(localized: "Connected, but the image check was inconclusive. Try again or choose a specific vision model.")
                           + "\n" + String(localized: "Model reply:") + " " + String(answer.prefix(160)))
        }
    }
}
