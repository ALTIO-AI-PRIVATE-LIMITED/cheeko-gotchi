import SwiftUI

@main
struct IPhoneAgentApp: App {
    @StateObject private var app = AppModel.shared

    init() {
        _ = AppModel.shared  // also on a background launch for an App Intent, which shows no scene
        CrashReports.shared.start()
    }

    var body: some Scene {
        WindowGroup {
            RootView(app: app, settings: app.settings)
        }
    }
}

enum AppTab: Hashable { case agent, runs, settings }

/// The setup screens; Home's status chips open the one that fixes a problem.
enum SetupPage: Int, CaseIterable, Identifiable {
    case welcome, connect, see, act, brain
    var id: Int { rawValue }
}

struct RootView: View {
    @ObservedObject var app: AppModel
    @ObservedObject var settings: Settings
    @AppStorage(RunShare.askedKey) private var privacyAsked = false
    @State private var consent = false

    var body: some View {
        TabView(selection: $app.tab) {
            HomeView(app: app, board: app.board, feed: app.feed, agent: app.agent, settings: settings,
                     intents: app.intents, recorder: app.recorder)
                .tabItem { Label("Agent", systemImage: "face.smiling") }
                .tag(AppTab.agent)
            RunsView(app: app, runs: app.runs, agent: app.agent)
                .tabItem { Label("Runs", systemImage: "play.rectangle.on.rectangle") }
                .tag(AppTab.runs)
            SettingsView(app: app, settings: settings, board: app.board)
                .tabItem { Label("Settings", systemImage: "gearshape") }
                .tag(AppTab.settings)
        }
        .fullScreenCover(item: $app.review) { draft in ReviewView(draft: draft, app: app) }
        .background {
            Color.clear.fullScreenCover(item: $app.setupPage) { page in
            OnboardingView(app: app, board: app.board, feed: app.feed, settings: settings, calibrator: app.calibrator,
                               pointer: app.pointer, start: page)
                .id(page)  // a fresh setup (its own page state) for every start page, e.g. "Run setup again"
            }
        }
        .background { Color.clear.sheet(isPresented: $consent, onDismiss: setUp) { ConsentSheet() } }
        .onAppear {
            if privacyAsked { setUp() } else { consent = true }  // what is kept, before setup
            app.probe.install(CalibrationOverlay(calibrator: app.calibrator, probe: app.probe, pointer: app.pointer))
        }
        .onOpenURL { url in
            if url.isFileURL && url.pathExtension.lowercased() == "zip" {  // a shared run archive
                do { try app.importRuns(url) } catch { app.say("import: \(error.localizedDescription)") }
                return
            }
            if app.handleDebug(url) { return }  // iphoneagent://run?task=…, calibrate, stop, bench, snap
            // iphoneagent://run/<id> from the Live Activity
            guard url.scheme == "iphoneagent", url.host == "run" else { return }
            let id = url.lastPathComponent
            if id.hasPrefix("rec-") { return }  // a recording: its draft opens by itself
            if app.agent.running { app.tab = .agent } else { app.openReview(run: id) }
        }
    }

    private func setUp() {
        privacyAsked = true
        if !settings.onboarded && app.setupPage == nil { app.setupPage = .welcome }
    }
}
