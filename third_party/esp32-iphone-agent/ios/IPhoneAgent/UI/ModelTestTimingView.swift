import SwiftUI

struct ModelTestTimingView: View {
    @ObservedObject var app: AppModel

    var body: some View {
        VStack(alignment: .leading, spacing: Space.xs) {
            if let started = app.modelTestStarted {
                HStack {
                    ProgressView()
                    Text("Waiting for the model…")
                    Text(started, style: .timer).monospacedDigit()
                }
            } else if let total = app.modelTestSeconds {
                LabeledContent("Total request time", value: seconds(total))
                if let response = app.modelResponseSeconds {
                    LabeledContent("Server response", value: seconds(response))
                }
                if let first = app.modelFirstTokenSeconds {
                    LabeledContent("First answer token", value: seconds(first))
                }
                Text("Includes network and model processing time.")
            }
        }
        .font(.footnote).foregroundStyle(.secondary)
    }

    private func seconds(_ value: Double) -> String {
        value.formatted(.number.precision(.fractionLength(2))) + " s"
    }
}
