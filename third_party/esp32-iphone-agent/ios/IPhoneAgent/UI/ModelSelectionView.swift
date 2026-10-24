import SwiftUI

struct ModelSelectionView: View {
    @ObservedObject var settings: Settings
    @Binding var selection: String
    let allowsDefault: Bool
    @Environment(\.dismiss) private var dismiss
    @State private var search = ""
    @State private var customID = ""

    static let routers: Set<String> = ["openrouter/free", "openrouter/auto"]

    static func displayName(_ id: String) -> String {
        switch id {
        case "openrouter/free": String(localized: "Automatic · Free models")
        case "openrouter/auto": String(localized: "Automatic · OpenRouter")
        default: id
        }
    }

    private var matches: [String] {
        let ids = Set(settings.models + (selection.isEmpty ? [] : [selection]))
        return ids.filter { search.isEmpty || $0.localizedStandardContains(search)
            || Self.displayName($0).localizedStandardContains(search) }.sorted()
    }

    var body: some View {
        let visible = matches
        NavigationStack {
            List {
                if allowsDefault && search.isEmpty {
                    Button("Use Default") { choose("") }
                }
                DisclosureGroup("Custom model") {
                    TextField("Model ID", text: $customID)
                        .textInputAutocapitalization(.never).autocorrectionDisabled()
                    Button("Use Model ID") { choose(customID.trimmingCharacters(in: .whitespacesAndNewlines)) }
                        .disabled(customID.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                }
                if !visible.filter({ Self.routers.contains($0) }).isEmpty {
                    Section {
                        ForEach(visible.filter { Self.routers.contains($0) }, id: \.self) { id in row(id) }
                    } header: { Text("Automatic routing") } footer: {
                        Text("OpenRouter chooses a model for each request. Free model availability and responses can vary.")
                    }
                }
                Section("Models") {
                    ForEach(visible.filter { !Self.routers.contains($0) }, id: \.self) { id in row(id) }
                }
                if settings.loadingModels {
                    ProgressView("Loading models…")
                } else if visible.isEmpty {
                    if search.isEmpty {
                        ContentUnavailableView("No models loaded", systemImage: "list.bullet",
                                               description: Text("Refresh the list or enter a model ID below."))
                    } else { ContentUnavailableView.search(text: search) }
                }
                if let error = settings.modelsError {
                    Text(verbatim: error).font(.footnote).foregroundStyle(.secondary)
                }

            }
            .searchable(text: $search, prompt: "Search models")
            .navigationTitle("Choose Model")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } }
                ToolbarItem(placement: .primaryAction) {
                    Button("Refresh Models", systemImage: "arrow.clockwise") {
                        Task { await settings.loadModels() }
                    }
                    .disabled(settings.loadingModels || !settings.isConfigured)
                }
            }
            .task {
                if settings.models.isEmpty && !settings.loadingModels { await settings.loadModels() }
            }
        }
    }

    private func row(_ id: String) -> some View {
        Button { choose(id) } label: {
            HStack {
                VStack(alignment: .leading) {
                    Text(Self.displayName(id)).foregroundStyle(.primary)
                    if Self.routers.contains(id) {
                        Text(verbatim: id).font(.footnote).foregroundStyle(.secondary)
                    }
                }
                Spacer()
                if selection == id {
                    Image(systemName: "checkmark").accessibilityLabel("Selected")
                }
            }
            .frame(minHeight: 44)
            .contentShape(.rect)
        }
        .buttonStyle(.plain)
        .accessibilityAddTraits(selection == id ? .isSelected : [])
    }

    private func choose(_ id: String) {
        selection = id
        dismiss()
    }
}
