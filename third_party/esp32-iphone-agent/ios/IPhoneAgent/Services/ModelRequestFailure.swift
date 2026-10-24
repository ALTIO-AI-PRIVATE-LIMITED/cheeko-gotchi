import Foundation

enum ModelRequestFailure {
    static func isTimeout(_ error: Error) -> Bool {
        (error as? URLError)?.code == .timedOut
    }

    static func message(_ error: Error) -> String {
        if isTimeout(error) {
            return String(localized: "The model request timed out before it finished. Try again or choose a specific vision model instead of automatic routing.")
        }
        return error.localizedDescription
    }
}
