import SwiftUI
import UIKit

/// The character, pokeable. One touch gesture drives everything:
/// - tap: squish + bounce, happy eyes, light haptic
/// - double tap: a little jump and a wink
/// - 3+ quick taps: dizzy spiral eyes, then it shakes it off
/// - long press: leans into the finger, eyes close contentedly (petting), soft haptics
/// - drag: eyes follow the finger, the body leans after it; on release it springs back
/// While the agent works (`busy`), a poke only gets a brief glance. Springs ≤ 0.6 s; under Reduce Motion
/// only the eyes and haptics react.
struct PokeableFace: View {
    var mood: Mood
    var look: CGPoint = .zero
    var busy = false
    var onInteract: (() -> Void)?
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @State private var override: Mood?
    @State private var gaze: CGPoint?
    @State private var squish = CGSize(width: 1, height: 1)
    @State private var lift: CGFloat = 0
    @State private var lean = CGSize.zero
    @State private var tilt = 0.0
    @State private var taps: [Date] = []
    @State private var touchStart: Date?
    @State private var dragging = false
    @State private var petting = false
    @State private var reset: Task<Void, Never>?
    @State private var pet: Task<Void, Never>?

    var body: some View {
        GeometryReader { g in
            AgentFace(mood: override ?? mood, look: gaze ?? look, reacts: override == nil)
                .scaleEffect(x: squish.width, y: squish.height, anchor: .bottom)
                .rotationEffect(.degrees(tilt), anchor: .bottom)
                .offset(x: lean.width, y: lift + lean.height)
                .frame(width: g.size.width, height: g.size.height)
                .contentShape(Rectangle())
                .gesture(touch(in: g.size))
        }
        .accessibilityAction(named: Text("Poke")) { tapped(); onInteract?() }
        .accessibilityAction(named: Text("Refresh status")) { onInteract?() }
    }

    // MARK: - gesture

    private func touch(in size: CGSize) -> some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { v in
                if touchStart == nil { began() }
                let moved = hypot(v.translation.width, v.translation.height)
                if moved > 8 && !petting { dragging = true }
                if dragging { follow(v.location, in: size, translation: v.translation) }
            }
            .onEnded { _ in ended() }
    }

    private func began() {
        touchStart = Date()
        pet?.cancel()
        guard !busy else { return }
        pet = Task {  // held still: petting
            try? await Task.sleep(nanoseconds: 400_000_000)
            guard !Task.isCancelled, !dragging else { return }
            petting = true
            reset?.cancel()
            override = .content
            spring(0.5, 0.6) { squish = CGSize(width: 1.06, height: 0.95); tilt = 4 }
            while !Task.isCancelled && petting {
                UIImpactFeedbackGenerator(style: .soft).impactOccurred(intensity: 0.6)
                try? await Task.sleep(nanoseconds: 650_000_000)
            }
        }
    }

    private func follow(_ p: CGPoint, in size: CGSize, translation: CGSize) {
        let dir = CGPoint(x: max(-1, min(1, (p.x / max(size.width, 1)) * 2 - 1)),
                          y: max(-1, min(1, (p.y / max(size.height, 1)) * 2 - 1)))
        gaze = dir
        guard !busy, !reduceMotion else { return }
        override = override ?? .listening
        // rubber band: the body follows a little, never far
        let k: CGFloat = 0.18, limit: CGFloat = 22
        lean = CGSize(width: max(-limit, min(limit, translation.width * k)),
                      height: max(-limit, min(limit, translation.height * k)))
        tilt = Double(lean.width) * 0.35
    }

    private func ended() {
        defer { onInteract?() }
        pet?.cancel()
        let held = touchStart.map { Date().timeIntervalSince($0) } ?? 0
        touchStart = nil
        if petting || dragging {
            let wasDragging = dragging
            petting = false
            dragging = false
            settle(after: wasDragging ? 0 : 0.35)
            spring(0.45, 0.55) { lean = .zero; tilt = 0; squish = CGSize(width: 1, height: 1) }
            if !busy { UIImpactFeedbackGenerator(style: .light).impactOccurred(intensity: 0.5) }
            return
        }
        if held < 0.4 { tapped() } else { settle(after: 0) }
    }

    // MARK: - reactions

    private func tapped() {
        if busy { return glance() }
        let now = Date()
        taps = taps.filter { now.timeIntervalSince($0) < 1.2 } + [now]
        if taps.count >= 3 { return dizzy() }
        if taps.count == 2, now.timeIntervalSince(taps[0]) < 0.35 { return jumpAndWink() }
        UIImpactFeedbackGenerator(style: .light).impactOccurred()
        override = .success
        squash(depth: 0.18)
        settle(after: 0.7)
    }

    /// Busy: just look up at the finger for a moment.
    private func glance() {
        UISelectionFeedbackGenerator().selectionChanged()
        gaze = CGPoint(x: 0, y: -0.8)
        reset?.cancel()
        reset = Task {
            try? await Task.sleep(nanoseconds: 600_000_000)
            guard !Task.isCancelled else { return }
            gaze = nil
        }
    }

    private func jumpAndWink() {
        UIImpactFeedbackGenerator(style: .medium).impactOccurred()
        override = .wink
        if !reduceMotion {
            spring(0.22, 0.6) { lift = -26; squish = CGSize(width: 0.94, height: 1.08) }
            after(0.2) { spring(0.38, 0.55) { lift = 0; squish = CGSize(width: 1, height: 1) } }
        }
        settle(after: 0.8)
    }

    private func dizzy() {
        taps = []
        UIImpactFeedbackGenerator(style: .rigid).impactOccurred()
        override = .dizzy
        if !reduceMotion { spring(0.3, 0.4) { tilt = 8 } }
        reset?.cancel()
        reset = Task {
            try? await Task.sleep(nanoseconds: 1_300_000_000)
            guard !Task.isCancelled else { return }
            override = .idle  // shakes it off
            UIImpactFeedbackGenerator(style: .light).impactOccurred()
            if !reduceMotion {
                for x in [-10.0, 9, -6, 4, 0] {
                    spring(0.09, 0.4) { tilt = x / 2; lean.width = x }
                    try? await Task.sleep(nanoseconds: 80_000_000)
                }
            }
            tilt = 0
            lean = .zero
            try? await Task.sleep(nanoseconds: 300_000_000)
            guard !Task.isCancelled else { return }
            override = nil
        }
    }

    /// Squish down and bounce back up.
    private func squash(depth: CGFloat) {
        guard !reduceMotion else { return }
        spring(0.12, 0.7) { squish = CGSize(width: 1 + depth * 0.6, height: 1 - depth) }
        after(0.12) {
            spring(0.45, 0.45) { squish = CGSize(width: 1, height: 1); lift = -8 }
            after(0.15) { spring(0.35, 0.6) { lift = 0 } }
        }
    }

    /// Back to the real mood after a while (cancelled by the next poke).
    private func settle(after seconds: Double) {
        reset?.cancel()
        reset = Task {
            try? await Task.sleep(nanoseconds: UInt64(seconds * 1_000_000_000))
            guard !Task.isCancelled else { return }
            override = nil
            gaze = nil
        }
    }

    private func spring(_ response: Double, _ damping: Double, _ change: () -> Void) {
        withAnimation(reduceMotion ? .easeInOut(duration: 0.2) : .spring(response: response, dampingFraction: damping), change)
    }

    private func after(_ seconds: Double, _ work: @escaping () -> Void) {
        DispatchQueue.main.asyncAfter(deadline: .now() + seconds, execute: work)
    }
}
