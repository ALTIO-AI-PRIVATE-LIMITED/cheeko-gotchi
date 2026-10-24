import SwiftUI

/// The Agent tab: the character, what is ready, the task, saved intents, and Start pinned at the bottom.
struct HomeView: View {
    @ObservedObject var app: AppModel
    @ObservedObject var board: BoardLink
    @ObservedObject var feed: VideoFeed
    @ObservedObject var agent: AgentLoop
    @ObservedObject var settings: Settings
    @ObservedObject var intents: IntentStore
    @ObservedObject var recorder: DemoRecorder
    @State private var task = ""
    @State private var recordSheet = false
    @State private var askNotifications = false
    @State private var pending: (String, SavedTask?)?
    @State private var naming = false
    @State private var intentName = ""
    @FocusState private var editing: Bool
    @State private var renaming = false
    @State private var pullingToRefresh = false

    var body: some View {
        NavigationStack {
            List {
                Section { hero }
                    .listRowBackground(Color.clear)
                    .listRowInsets(EdgeInsets())
                if recorder.recording || recorder.inferring || recorder.error != nil {
                    RecordingPanel(recorder: recorder)
                }
                if app.help != nil { HelpPanel(app: app) }
                if let stage = app.recovering { RecoveryPanel(app: app, stage: stage) }
                if agent.running || app.runMood != nil { liveSection }
                taskSection
                intentSection
            }
            .listStyle(.insetGrouped)
            .refreshable {
                pullingToRefresh = true
                defer { pullingToRefresh = false }
                await app.refreshHomeStatus()
            }
            .overlay(alignment: .top) {
                if app.refreshingStatus && !pullingToRefresh {
                    ProgressView()
                        .accessibilityLabel(Text("Refreshing status…"))
                        .padding(.top, Space.s)
                        .allowsHitTesting(false)
                }
            }
            .scrollDismissesKeyboard(.interactively)
            .navigationTitle(Text(verbatim: settings.agentName))
            .navigationBarTitleDisplayMode(.inline)
            .navigationDestination(for: UUID.self) { id in IntentDetailView(app: app, intents: intents, id: id) }
            .toolbar {
                ToolbarItem(placement: .principal) {
                    Button { renaming = true } label: {
                        HStack(spacing: 4) {
                            Text(verbatim: settings.agentName).font(.headline).foregroundStyle(.primary)
                            Image(systemName: "pencil").font(.caption.weight(.semibold)).foregroundStyle(.tertiary)
                        }
                    }
                    .accessibilityLabel(Text("Rename \(settings.agentName)"))
                }
                ToolbarItem(placement: .topBarTrailing) {
                    Button { recordSheet = true } label: {
                        Image(systemName: recorder.recording ? "record.circle.fill" : "record.circle")
                            .foregroundStyle(recorder.recording ? Color.red : Color.accentColor)
                            .symbolEffect(.pulse, options: .repeating, isActive: recorder.recording)
                    }
                    .accessibilityLabel(Text("New intent"))
                    .disabled(recorder.recording || recorder.inferring)
                }
            }
            .sheet(isPresented: $recordSheet) { RecordSheet(app: app, recorder: recorder) }
            .sheet(isPresented: $renaming) { RenameSheet(settings: settings) }
            .alert("Get notified?", isPresented: $askNotifications) {
                Button("Continue") { Task { await app.notifier.requestPermission(); go() } }
                Button("Not Now", role: .cancel) { UserDefaults.standard.set(true, forKey: Notifier.askedKey); go() }
            } message: {
                Text("I'll tell you when a run is done or needs your help, since you won't see this app while I work.")
            }
            .overlay { if app.departing { DepartureOverlay() } }
            .overlay(alignment: .top) {
                if let hint = app.homeHint {
                    Label { Text(verbatim: hint) } icon: { Image(systemName: "arrow.up.to.line") }
                        .font(.subheadline.weight(.semibold))
                        .padding(.horizontal, Space.l).padding(.vertical, Space.s)
                        .background(.regularMaterial, in: Capsule())
                        .padding(.top, Space.s)
                        .transition(.move(edge: .top).combined(with: .opacity))
                }
            }
            .animation(.easeOut(duration: 0.25), value: app.homeHint)
            .overlay(alignment: .topTrailing) {  // right under the record button
                if !agent.running {
                    CalibrateButton(calibrator: app.calibrator, board: board, pointer: app.pointer)
                        .padding(.trailing, Space.l).padding(.top, Space.xs)
                }
            }
            .safeAreaInset(edge: .bottom) { bottomBar }
            .alert("Save as intent", isPresented: $naming) {
                TextField("Name", text: $intentName)
                Button("Save") { intents.add(name: intentName, prompt: task); Haptics.success() }
                Button("Cancel", role: .cancel) {}
            } message: {
                Text("Run it again later with one tap.")
            }
            .animation(.spring(response: 0.4, dampingFraction: 0.85), value: agent.running)
        }
    }

    // MARK: - hero

    private var hero: some View {
        VStack(spacing: Space.m) {
            PokeableFace(mood: editing ? .listening : app.mood, look: lookAt, busy: app.agent.running,
                         onInteract: { Task { await app.refreshHomeStatus() } })
                .frame(width: 128, height: 143)
                .padding(.top, Space.s)
            Text(greeting)
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, Space.xl)
            StatusChips(app: app, board: board, pointer: app.pointer, calibrator: app.calibrator)
            if !app.refreshingStatus, let error = app.statusRefreshError {
                Text(verbatim: error).font(.footnote).foregroundStyle(.secondary)
                    .multilineTextAlignment(.center).padding(.horizontal, Space.l)
            }
        }
        .frame(maxWidth: .infinity)
        .padding(.bottom, Space.s)
    }

    /// Where the eyes look while acting: toward the last tap.
    private var lookAt: CGPoint {
        guard let s = agent.steps.last, let x = s["x"]?.double, let y = s["y"]?.double else { return .zero }
        return CGPoint(x: x / 500 - 1, y: y / 500 - 1)
    }

    private var greeting: LocalizedStringKey {
        if app.help != nil { return "I need your help with one step." }
        if app.recovering != nil { return "The connection dropped. I'll go on when it's back." }
        if recorder.recording { return "I'm watching. Do the task, then stop the recording." }
        if agent.running { return "Working on it. Watch the Dynamic Island, or stop me anytime." }
        switch app.runMood {
        case .success: return "Done! Open Runs to replay what I did."
        case .failure: return "That did not work out. The replay shows where I got stuck."
        default: break
        }
        if !board.connected { return "Plug me into the USB-C port to wake me up." }
        if blocker != nil { return "Almost there. Tap an orange item to finish setup." }
        return "Hi, I'm \(settings.agentName). Tell me what to do on this iPhone."
    }

    // MARK: - sections

    private var liveSection: some View {
        Section {
            HStack(alignment: .top, spacing: Space.m) {
                Group {
                    if let jpeg = feed.frame, let img = UIImage(data: jpeg) {
                        Image(uiImage: img).resizable().scaledToFit()
                    } else {
                        Color(uiColor: .tertiarySystemFill)
                    }
                }
                .frame(width: 64, height: 138)
                .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
                VStack(alignment: .leading, spacing: Space.s) {
                    Text(verbatim: agent.task).font(.headline).lineLimit(2)
                    if let last = agent.steps.last {
                        Label { Text(verbatim: StepText.line(last)).lineLimit(3) } icon: {
                            Image(systemName: StepText.symbol(last["action"]?.str))
                        }
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                    }
                    ProgressView(value: Double(min(app.moveCount, agent.maxSteps)), total: Double(agent.maxSteps))
                    Text("Step \(app.moveCount) of up to \(agent.maxSteps)")
                        .font(.caption.monospacedDigit()).foregroundStyle(.secondary)
                }
            }
            .padding(.vertical, Space.xs)
        } header: {
            Text(agent.running ? "Now" : "Last run")
        }
    }

    private var taskSection: some View {
        Section {
            TextField("What should I do on this iPhone?", text: $task, axis: .vertical)
                .lineLimit(3...6)
                .focused($editing)
                .submitLabel(.done)
            if !task.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                Button { intentName = ""; naming = true } label: {
                    Label("Save as intent", systemImage: "bookmark")
                }
            }
        } header: {
            HStack {
                Text("Task")
                Spacer()
                Menu {
                    ForEach(IntentStore.examples) { e in
                        Button(e.name) { task = e.prompt; Haptics.tick() }
                    }
                } label: {
                    Label("Examples", systemImage: "sparkles").font(.footnote.weight(.semibold)).textCase(nil)
                }
            }
        }
    }

    private var intentSection: some View {
        Section {
            ForEach(intents.items) { item in
                NavigationLink(value: item.id) {
                    HStack(spacing: Space.m) {
                        SettingsIcon(symbol: item.symbol, color: .accentColor)
                        VStack(alignment: .leading, spacing: 2) {
                            Text(verbatim: item.name).font(.body).foregroundStyle(.primary)
                            Text(verbatim: item.prompt).font(.caption).foregroundStyle(.secondary).lineLimit(1)
                        }
                        Spacer()
                        Button { run(item) } label: {
                            Image(systemName: "play.circle.fill").font(.title2).foregroundStyle(Color.accentColor)
                                .opacity(blocker == nil && !agent.running ? 1 : 0.35)
                        }
                        .buttonStyle(.borderless)
                        .accessibilityLabel(Text("Run \(item.name)"))
                    }
                }
                .contextMenu {
                    Button { task = item.prompt } label: { Label("Edit as task", systemImage: "pencil") }
                    Button(role: .destructive) { intents.delete(item.id) } label: { Label("Delete", systemImage: "trash") }
                }
                .swipeActions(edge: .trailing) {
                    Button(role: .destructive) { intents.delete(item.id) } label: { Label("Delete", systemImage: "trash") }
                }
            }
            HStack(spacing: Space.m) {
                SettingsIcon(symbol: "record.circle", color: Color(uiColor: .systemGray3))
                Text("Record an intent").foregroundStyle(.secondary)
                Spacer()
                Text("Coming soon").font(.caption.weight(.semibold)).foregroundStyle(.secondary)
                    .padding(.horizontal, Space.s).padding(.vertical, 3)
                    .background(Capsule().fill(Color(uiColor: .tertiarySystemFill)))
            }
        } header: {
            Text("My intents")
        } footer: {
            Text("Tap ▶ to run, tap the row for details. Swipe left to delete.")
        }
    }

    // MARK: - bottom

    private var bottomBar: some View {
        BottomBar {
            if agent.running {
                Button {
                    Haptics.stop()
                    Task { await app.stopAgent() }
                } label: { Label("Stop", systemImage: "stop.fill") }
                .buttonStyle(BigButton(tint: Palette.stop))
            } else {
                Button { start(task) } label: { Label("Start", systemImage: "play.fill") }
                    .buttonStyle(BigButton())
                    .disabled(task.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty || blocker != nil
                              || app.departing || app.startingAgent || recorder.recording)
                if let blocker {
                    Button { app.setupPage = blocker.page } label: {
                        Label(blocker.text, systemImage: "exclamationmark.circle").font(.footnote)
                    }
                    .tint(Palette.warning)
                }
            }
        }
    }

    private func run(_ item: SavedTask) {
        task = item.prompt
        guard blocker == nil, !agent.running else { return }
        start(item.prompt, intent: item)
    }

    /// The first Start asks for notifications (one line why), then the character takes off.
    private func start(_ text: String, intent: SavedTask? = nil) {
        Haptics.start()
        editing = false
        pending = (text, intent)
        if Notifier.asked { go() } else { askNotifications = true }
    }

    private func go() {
        guard let (text, intent) = pending else { return }
        pending = nil
        Task { await app.run(text, intent: intent) }
    }

    /// The first thing missing before a run, and the setup page that fixes it.
    private var blocker: (text: LocalizedStringKey, page: SetupPage)? {
        if !board.connected { return ("Connect the board", .connect) }
        if board.status?.ap != "streaming" { return ("Start Screen Mirroring", .see) }
        if !app.pointer.calibrated { return ("Calibrate the pointer", .act) }
        if !app.modelReady { return ("Set up the model", .brain) }
        return nil
    }
}

/// Rename the agent (title, greeting, Live Activity).
struct RenameSheet: View {
    @ObservedObject var settings: Settings
    @Environment(\.dismiss) private var dismiss
    @State private var name = ""

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    TextField("Name", text: $name).submitLabel(.done).onSubmit(save)
                } footer: {
                    Text("Shown on the Agent tab, in greetings and in the Live Activity.")
                }
            }
            .navigationTitle("Rename Agent")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } }
                ToolbarItem(placement: .confirmationAction) { Button("Save", action: save) }
            }
            .onAppear { name = settings.agentName }
        }
        .presentationDetents([.height(240)])
    }

    private func save() {
        settings.agentName = name
        dismiss()
    }
}
