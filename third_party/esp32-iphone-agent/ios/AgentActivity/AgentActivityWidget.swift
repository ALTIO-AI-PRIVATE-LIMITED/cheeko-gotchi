import ActivityKit
import AppIntents
import SwiftUI
import WidgetKit

@main
struct AgentActivityBundle: WidgetBundle {
    var body: some Widget {
        AgentLiveActivity()
        AgentStatusWidget()
        RunLastIntentControl()
        StopAgentControl()
    }
}

/// The Live Activity and Dynamic Island: running (eyes, "3/12", current step, Stop), needs help
/// (Continue / Skip / Stop) and recording a demonstration (red dot, timer, Stop).
struct AgentLiveActivity: Widget {
    var body: some WidgetConfiguration {
        ActivityConfiguration(for: AgentActivityAttributes.self) { context in
            LockScreenView(context: context)
                .activityBackgroundTint(Color.black.opacity(0.85))
                .activitySystemActionForegroundColor(.white)
                .widgetURL(AgentActivityAttributes.url(run: context.attributes.runID))
        } dynamicIsland: { context in
            let s = context.state
            return DynamicIsland {
                DynamicIslandExpandedRegion(.leading) {
                    Eyes(state: s).frame(width: 44, height: 44).padding(.leading, 4)
                }
                DynamicIslandExpandedRegion(.trailing) {
                    Counter(state: s).font(.headline).padding(.trailing, 4)
                }
                DynamicIslandExpandedRegion(.center) {
                    Text(verbatim: context.attributes.task).font(.headline).lineLimit(1)
                }
                DynamicIslandExpandedRegion(.bottom) {
                    VStack(alignment: .leading, spacing: 8) {
                        Says(state: s, full: true).font(.subheadline.weight(.semibold))
                        if let since = s.waitingSince, !s.finished {
                            Text(since, style: .timer).monospacedDigit().font(.footnote)
                                .accessibilityLabel("Waiting time")
                        }
                        if s.line != s.labelFull && (s.phase != .running || s.finished) {
                            Text(verbatim: s.line).font(.footnote).foregroundStyle(.secondary).lineLimit(2)
                        }
                        if s.phase == .running {
                            ProgressView(value: Double(min(s.step, s.limit)), total: Double(max(s.limit, 1))).tint(.white)
                        }
                        if !s.finished { Buttons(state: s) }
                    }
                }
            } compactLeading: {
                if s.phase == .recording {
                    Image(systemName: "record.circle.fill").foregroundStyle(.red)
                } else {
                    Eyes(state: s).frame(width: 24, height: 24)
                }
            } compactTrailing: {
                if s.phase == .recording && !s.finished {
                    Counter(state: s).font(.caption).frame(maxWidth: 44)
                } else {
                    Says(state: s, full: false).font(.caption.bold()).frame(maxWidth: 92, alignment: .trailing)
                }
            } minimal: {
                if s.phase == .recording {
                    Image(systemName: "record.circle.fill").foregroundStyle(.red)
                } else {
                    // Minimal presentation is the primary surface while Screen Mirroring is active.
                    // Keep it text-only: the former face-over-label layout was too small to read,
                    // and idle gaze updates caused distracting system refresh transitions.
                    // Eyes remains below for compact/expanded presentations and future reuse.
                    Text(verbatim: s.minimalLabel ?? s.label)
                        .font(.caption2.bold()).foregroundStyle(.white)
                        .lineLimit(1).minimumScaleFactor(0.8)
                        .frame(width: 36, height: 24)
                        .accessibilityLabel(Text(verbatim: s.labelFull))
                }
            }
            .widgetURL(AgentActivityAttributes.url(run: context.attributes.runID))
            .keylineTint(s.phase == .help ? .orange : s.phase == .recording ? .red : .white)
        }
    }
}

/// "3/12" while running, the elapsed time while recording.
private struct Counter: View {
    let state: AgentActivityAttributes.ContentState
    var body: some View {
        if state.phase == .recording && !state.finished {
            Text(timerInterval: state.since...Date.distantFuture, countsDown: false)
                .monospacedDigit().foregroundStyle(.red).multilineTextAlignment(.trailing)
        } else {
            Text(verbatim: "\(state.step)/\(state.limit)").monospacedDigit().foregroundStyle(.secondary)
        }
    }
}

private struct Buttons: View {
    let state: AgentActivityAttributes.ContentState
    var body: some View {
        HStack(spacing: 8) {
            switch state.phase {
            case .help:
                Button(intent: ContinueAgentIntent()) { Label("Continue", systemImage: "play.fill").frame(maxWidth: .infinity) }
                    .tint(.green)
                Button(intent: SkipStepIntent()) { Label("Skip", systemImage: "forward.fill").frame(maxWidth: .infinity) }
                    .tint(.gray)
                Button(intent: StopAgentIntent()) { Image(systemName: "stop.fill") }.tint(.red)
            case .recording:
                Button(intent: StopRecordingIntent()) {
                    Label("Stop Recording", systemImage: "stop.fill").frame(maxWidth: .infinity)
                }
                .tint(.red)
            case .running:
                Button(intent: StopAgentIntent()) { Label("Stop", systemImage: "stop.fill").frame(maxWidth: .infinity) }
                    .tint(.red)
            }
        }
        .buttonStyle(.borderedProminent)
    }
}

/// The character's face without the USB tab. Widgets do not animate on their own, but a change of `look`
/// between action updates springs toward the gesture's target before the pointer moves.
/// Random idle gaze updates are disabled for the text-only minimal presentation.
private struct Eyes: View {
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    let state: AgentActivityAttributes.ContentState
    var body: some View {
        AgentFace(mood: state.mood, look: state.look, lookRange: 2.4, animated: false, showsTab: false)
            .animation(reduceMotion ? nil : .spring(response: 0.3, dampingFraction: 0.65), value: state.look)
    }
}

/// What the character says ("Thinking…", "Tapping Clock", "Done!"), colored by how things stand.
private struct Says: View {
    let state: AgentActivityAttributes.ContentState
    let full: Bool
    var body: some View {
        let text = full ? state.labelFull : state.label
        Text(verbatim: text.isEmpty ? state.line : text)
            .lineLimit(1)
            .minimumScaleFactor(0.75)
            .foregroundStyle(color)
            .contentTransition(.opacity)
            .animation(.easeOut(duration: 0.25), value: text)
    }

    private var color: Color {
        if state.phase == .help { return .orange }
        if state.finished { return state.mood == .success ? .green : state.mood == .failure ? .orange : .secondary }
        return .white
    }
}

private struct LockScreenView: View {
    let context: ActivityViewContext<AgentActivityAttributes>

    var body: some View {
        let s = context.state
        HStack(alignment: .top, spacing: 14) {
            if s.phase == .recording && !s.finished {
                Image(systemName: "record.circle.fill").font(.system(size: 40)).foregroundStyle(.red).frame(width: 52)
            } else {
                Eyes(state: s).frame(width: 52, height: 52)
            }
            VStack(alignment: .leading, spacing: 6) {
                if let name = context.attributes.name {
                    Text(verbatim: name).font(.caption.weight(.semibold)).foregroundStyle(.secondary)
                }
                HStack {
                    Text(verbatim: context.attributes.task).font(.headline).lineLimit(1)
                    Spacer()
                    Counter(state: s).font(.subheadline)
                }
                if !s.finished && !s.labelFull.isEmpty {
                    Says(state: s, full: true).font(.subheadline.weight(.semibold))
                }
                if s.phase != .running || s.finished {
                    Text(verbatim: s.line).font(.subheadline).foregroundStyle(.secondary).lineLimit(2)
                }
                if s.finished {
                    Label(s.mood == .success ? "Done · tap to review" : "Ended · tap to review",
                          systemImage: s.mood == .success ? "checkmark.circle.fill" : "play.circle")
                        .font(.footnote.weight(.semibold))
                } else {
                    if s.phase == .running {
                        ProgressView(value: Double(min(s.step, s.limit)), total: Double(max(s.limit, 1))).tint(.white)
                    }
                    Buttons(state: s).controlSize(.small)
                }
            }
        }
        .foregroundStyle(.white)
        .padding(16)
    }
}
