import SwiftUI

/// A small board placed immediately alongside the phone in the setup illustration.
struct BoardProximityIllustration: View {
    var body: some View {
        VStack(spacing: Space.xs) {
            Text("Board").font(.footnote).foregroundStyle(.secondary)
            Image(systemName: "cpu")
                .font(.title2)
                .foregroundStyle(.primary)
                .frame(width: 36, height: 44)
                .background(.background, in: .rect(cornerRadius: 6))
                .overlay(RoundedRectangle(cornerRadius: 6).stroke(.secondary, lineWidth: 1))
                .overlay(alignment: .bottom) {
                    RoundedRectangle(cornerRadius: 2)
                        .fill(.secondary).frame(width: 14, height: 5).offset(y: 3)
                }
        }
        .accessibilityHidden(true) // The demo's caption describes the position.
    }
}
