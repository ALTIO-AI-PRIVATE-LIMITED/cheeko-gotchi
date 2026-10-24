import Foundation

/// Sends a log archive somewhere for debugging. No endpoint ships yet: `LogUpload.uploader` is nil and
/// uploading is off until the user agrees (a first-launch agreement listing `collected` will set the flag).
protocol LogUploader {
    func upload(_ archive: URL) async throws
}

enum LogUpload {
    static let consentKey = "logUploadConsent"

    /// The user agreed to send logs (off by default).
    static var consented: Bool {
        get { UserDefaults.standard.bool(forKey: consentKey) }
        set { UserDefaults.standard.set(newValue, forKey: consentKey) }
    }

    /// Set when an endpoint exists; nil = nothing is ever sent.
    @MainActor static var uploader: LogUploader?

    /// What the archive holds, for the agreement screen. Never the API key.
    static let collected = [
        "debug.log: app events, board commands and status, BLE disconnects, model request timing",
        "Runs: each run's steps, the model's prompts and replies, screenshots and the mirrored-screen video",
    ]

    /// Upload only with consent and an endpoint; returns whether it was sent.
    @MainActor static func uploadIfAllowed(_ archive: URL) async -> Bool {
        guard consented, let uploader else { return false }
        do {
            try await uploader.upload(archive)
            DebugLog.write("logs uploaded")
            return true
        } catch {
            DebugLog.write("log upload failed: \(error.localizedDescription)")
            return false
        }
    }
}
