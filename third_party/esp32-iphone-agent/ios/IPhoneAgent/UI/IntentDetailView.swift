import SwiftUI

/// An intent: run it, its stats (last run, success rate, average time), its words, edit and delete.
struct IntentDetailView: View {
    @ObservedObject var app: AppModel
    @ObservedObject var intents: IntentStore
    let id: UUID
    @Environment(\.dismiss) private var dismiss
    @State private var confirmDelete = false

    var body: some View {
        if let item = intents.item(id) {
            List {
                Section {
                    VStack(spacing: Space.m) {
                        Image(systemName: item.symbol)
                            .font(.system(size: 34, weight: .semibold))
                            .foregroundStyle(.white)
                            .frame(width: 72, height: 72)
                            .background(RoundedRectangle(cornerRadius: 18, style: .continuous).fill(Color.accentColor))
                        Text(verbatim: item.name).font(.title2.bold()).multilineTextAlignment(.center)
                    }
                    .frame(maxWidth: .infinity)
                    .listRowBackground(Color.clear)
                }
                Section {
                    stat("Last run", item.runs.last.map { ($0.ok ? "✓ " : "✗ ") + $0.date.formatted(.relative(presentation: .named)) })
                    stat("Success rate", item.successRate.map { $0.formatted(.percent.precision(.fractionLength(0))) })
                    stat("Average time", item.averageSeconds?.durationText)
                    stat("Runs", item.runs.isEmpty ? nil : "\(item.runs.count)")
                }
                Section("Instruction") { Text(verbatim: item.prompt) }
                if !item.checklist.isEmpty {
                    Section("Checklist") {
                        ForEach(item.checklist, id: \.self) { h in
                            Label { Text(verbatim: h) } icon: { Image(systemName: "checkmark.circle").foregroundStyle(Palette.ok) }
                        }
                    }
                }
                Section {
                    Button { edit(item) } label: { Label("Edit", systemImage: "slider.horizontal.3") }
                    if let last = item.runs.last?.run ?? item.refRun {
                        Button { app.openReview(run: last) } label: { Label("Review Last Run", systemImage: "play.rectangle") }
                    }
                    Button(role: .destructive) { confirmDelete = true } label: { Label("Delete Intent", systemImage: "trash") }
                }
            }
            .navigationTitle(Text(verbatim: item.name))
            .navigationBarTitleDisplayMode(.inline)
            .safeAreaInset(edge: .bottom) {
                BottomBar {
                    Button {
                        Haptics.start()
                        dismiss()
                        Task { await app.run(item.prompt, intent: item) }
                    } label: { Label("Run", systemImage: "play.fill") }
                    .buttonStyle(BigButton())
                    .disabled(app.agent.running)
                }
            }
            .confirmationDialog("Delete this intent?", isPresented: $confirmDelete, titleVisibility: .visible) {
                Button("Delete", role: .destructive) { intents.delete(id); dismiss() }
            }
        } else {
            ContentUnavailableView("Intent deleted", systemImage: "trash")
        }
    }

    private func stat(_ title: LocalizedStringKey, _ value: String?) -> some View {
        LabeledContent(title) { Text(verbatim: value ?? "–").monospacedDigit() }
    }

    /// The same editor as a run's review: with its last run's steps if it has one.
    private func edit(_ item: SavedTask) {
        let ref = (item.runs.last?.run ?? item.refRun).flatMap { app.runs.run($0) }
        app.review = ref.map { ReviewDraft(run: $0, intent: item) } ?? ReviewDraft(intent: item)
    }
}

/// The ⏺ sheet: record a demonstration, or save the last run as an intent.
struct RecordSheet: View {
    @ObservedObject var app: AppModel
    @ObservedObject var recorder: DemoRecorder
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            List {
                Section {
                    Button {
                        recorder.start()
                        dismiss()
                    } label: {
                        option("Record me doing it", "record.circle.fill", .red,
                               "Do the task yourself. I watch the mirrored screen and write the steps down.")
                    }
                    .disabled(!(app.board.connected && app.board.status?.ap == "streaming") || app.agent.running)
                    Button {
                        guard let last = app.runs.runs.first else { return }
                        dismiss()
                        DispatchQueue.main.asyncAfter(deadline: .now() + 0.45) {  // after the sheet is gone
                            app.openReview(run: last.id)
                            app.review?.showSaveOnOpen = true
                        }
                    } label: {
                        option("Save last run as intent", "bookmark.fill", .accentColor,
                               "Keep the last run as a reference and reuse it with one tap.")
                    }
                    .disabled(app.runs.runs.isEmpty)
                } footer: {
                    Text("Recording needs Screen Mirroring. Your fingers are not visible to the board, so I guess each step from the screen and mark the unsure ones yellow.")
                }
            }
            .navigationTitle("New Intent")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } } }
        }
        .presentationDetents([.medium])
    }

    private func option(_ title: LocalizedStringKey, _ symbol: String, _ color: Color, _ text: LocalizedStringKey) -> some View {
        HStack(spacing: Space.m) {
            SettingsIcon(symbol: symbol, color: color)
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.body.weight(.semibold)).foregroundStyle(.primary)
                Text(text).font(.footnote).foregroundStyle(.secondary)
            }
        }
        .padding(.vertical, Space.xs)
    }
}
