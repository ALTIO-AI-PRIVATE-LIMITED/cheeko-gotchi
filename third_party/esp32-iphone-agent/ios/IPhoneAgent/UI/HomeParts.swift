import SwiftUI

/// The character takes off before a run: a hop, then up and away (a fade under Reduce Motion).
struct DepartureOverlay: View {
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @State private var gone = false

    var body: some View {
        ZStack {
            Rectangle().fill(.ultraThinMaterial).ignoresSafeArea()
            VStack(spacing: Space.l) {
                AgentFace(mood: .acting)
                    .frame(width: 140, height: 157)
                    .offset(y: gone && !reduceMotion ? -520 : 0)
                    .scaleEffect(gone && !reduceMotion ? 0.6 : 1)
                    .opacity(gone ? 0 : 1)
                Text("See you later!").font(.title2.bold()).opacity(gone ? 0 : 1)
            }
        }
        .transition(.opacity)
        .onAppear {
            withAnimation(.easeIn(duration: 0.4).delay(0.35)) { gone = true }  // 0.75 s, inside the 0.8 s wait
        }
        .accessibilityLabel(Text("Starting"))
    }
}

/// "Needs your help": why, and Take over / Continue / Skip / Stop (the same as the Live Activity).
struct HelpPanel: View {
    @ObservedObject var app: AppModel

    var body: some View {
        Section {
            Label { Text(verbatim: app.help ?? "") } icon: {
                Image(systemName: "hand.raised.fill").foregroundStyle(Palette.warning)
            }
            .font(.body.weight(.semibold))
            if app.takingOver {
                Text("Do the step yourself, then tap Continue.").font(.subheadline).foregroundStyle(.secondary)
            }
            HStack(spacing: Space.s) {
                if app.takingOver {
                    Button { app.answerHelp(.resume) } label: { Label("Continue", systemImage: "play.fill") }
                        .buttonStyle(.borderedProminent).tint(Palette.ok)
                } else {
                    Button { app.takeOver() } label: { Label("Take Over", systemImage: "hand.point.up.left.fill") }
                        .buttonStyle(.borderedProminent)
                }
                Button { app.answerHelp(.skip) } label: { Text("Skip Step") }.buttonStyle(.bordered)
                Spacer()
                Button(role: .destructive) { Task { await app.stopAgent() } } label: { Image(systemName: "stop.fill") }
                    .buttonStyle(.bordered).accessibilityLabel(Text("Stop"))
            }
            .controlSize(.regular)
        } header: {
            Text("Needs your help")
        }
    }
}

/// The run is paused for a lost link: what is missing, how long, and what (if anything) the user should do.
struct RecoveryPanel: View {
    @ObservedObject var app: AppModel
    let stage: Recovery.Stage

    var body: some View {
        Section {
            HStack(spacing: Space.m) {
                if stage.ask {
                    Image(systemName: "hand.raised.fill").foregroundStyle(Palette.warning)
                } else {
                    ProgressView()
                }
                Text(verbatim: stage.status.full).font(.body.weight(.semibold))
                Spacer()
                Text(timerInterval: app.recoverySince...Date.distantFuture, countsDown: false)
                    .font(.subheadline.monospacedDigit()).foregroundStyle(.secondary)
            }
            Text(verbatim: stage.line).font(.subheadline).foregroundStyle(.secondary)
            HStack {
                Spacer()
                Button(role: .destructive) { Task { await app.stopAgent() } } label: { Label("Stop", systemImage: "stop.fill") }
                    .buttonStyle(.bordered)
            }
        } header: {
            Text("Connection lost, run paused")
        }
    }
}

/// A demonstration in progress: timer, screens captured, Stop / Cancel; then "working out the steps".
struct RecordingPanel: View {
    @ObservedObject var recorder: DemoRecorder

    var body: some View {
        Section {
            if recorder.recording {
                HStack {
                    Image(systemName: "record.circle.fill").foregroundStyle(.red)
                        .symbolEffect(.pulse, options: .repeating)
                    Text(timerInterval: recorder.started...Date.distantFuture, countsDown: false)
                        .font(.title3.monospacedDigit().weight(.semibold))
                    Spacer()
                    Text("\(recorder.keyframes) screens").foregroundStyle(.secondary).monospacedDigit()
                }
                Text("Go to the Home Screen and do the task. Stop here or in the Dynamic Island.")
                    .font(.subheadline).foregroundStyle(.secondary)
                HStack {
                    Button { Task { await recorder.stop() } } label: { Label("Stop Recording", systemImage: "stop.fill") }
                        .buttonStyle(.borderedProminent).tint(.red)
                    Spacer()
                    Button("Cancel", role: .destructive) { recorder.cancel() }
                }
            } else if recorder.inferring {
                HStack(spacing: Space.m) {
                    ProgressView()
                    Text("Working out the steps from \(recorder.keyframes) screens…")
                }
            } else if let error = recorder.error {
                Label { Text(verbatim: error) } icon: { Image(systemName: "exclamationmark.triangle.fill") }
                    .foregroundStyle(Palette.warning)
                Button("OK") { recorder.error = nil }
            }
        } header: {
            Text("Recording")
        }
    }
}
