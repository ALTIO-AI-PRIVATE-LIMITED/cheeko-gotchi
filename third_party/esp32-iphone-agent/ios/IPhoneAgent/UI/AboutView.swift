import SwiftUI

/// Settings › About: version, source code, acknowledgements, disclaimer and the text logs.
struct AboutView: View {
    @ObservedObject var app: AppModel
    static let repo = URL(string: "https://github.com/onehumanbeing/esp32-iphone-agent")!
    static let author = URL(string: "https://x.com/iamhenryyuan")!
    static let discord = URL(string: "https://discord.gg/BSJEnfdDX")!

    private var version: String {
        let info = Bundle.main.infoDictionary
        return "\(info?["CFBundleShortVersionString"] as? String ?? "1.0") (\(info?["CFBundleVersion"] as? String ?? "1"))"
    }

    /// A new GitHub issue, prefilled with what we always ask for.
    private var bugReport: URL {
        let device = UIDevice.current
        let body = """
        **What happened**


        **What you expected**


        **Steps**
        1.

        ESP32 Agent \(version) · \(device.systemName) \(device.systemVersion) · \(Self.model)
        (Attach the zip from Settings › About › Logs › Export all logs.)
        """
        var c = URLComponents(url: Self.repo.appendingPathComponent("issues/new"), resolvingAgainstBaseURL: false)!
        c.queryItems = [URLQueryItem(name: "labels", value: "bug"), URLQueryItem(name: "body", value: body)]
        return c.url ?? Self.repo
    }

    /// The hardware model, e.g. "iPhone17,2".
    private static let model: String = {
        var info = utsname()
        uname(&info)
        return withUnsafeBytes(of: &info.machine) { String(decoding: $0.prefix { $0 != 0 }, as: UTF8.self) }
    }()

    var body: some View {
        List {
            Section {
                VStack(spacing: Space.m) {
                    Image("Logo").resizable().scaledToFit().frame(width: 96, height: 96)
                        .accessibilityHidden(true)
                    Text(verbatim: "ESP32 Agent").font(.title2.bold())
                    Text("Version \(version)").font(.subheadline).foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity)
                .padding(.vertical, Space.s)
                .listRowBackground(Color.clear)
            }
            Section("Community") {
                CommunityRow(logo: "GitHubMark", title: "View source on GitHub", url: Self.repo)
                CommunityRow(logo: "XLogo", title: "Author on X", detail: "@iamhenryyuan", url: Self.author)
                CommunityRow(logo: "DiscordLogo", title: "Join the Discord", url: Self.discord)
            }
            Section {
                NavigationLink { AcknowledgementsView() } label: {
                    SettingsLabel(title: "Acknowledgements", symbol: "heart.fill", color: .pink)
                }
                NavigationLink { DisclaimerView() } label: {
                    SettingsLabel(title: "Disclaimer", symbol: "exclamationmark.shield.fill", color: .orange)
                }
            } footer: {
                Text("Open source. No data leaves this iPhone except the requests to the model endpoint you set up.")
            }
            Section {
                Link(destination: bugReport) {
                    HStack {
                        SettingsLabel(title: "Report a bug", symbol: "ladybug.fill", color: .red)
                        Spacer()
                        Image(systemName: "arrow.up.forward.app").foregroundStyle(.tertiary)
                    }
                }
                .tint(.primary)
                NavigationLink { LogsView(app: app) } label: {
                    SettingsLabel(title: "Logs", symbol: "text.alignleft", color: .gray)
                }
            } footer: {
                Text("Opens a GitHub issue with your version filled in. Attach the zip from Logs › Export all logs: it holds the debug log, the runs and any crash reports iOS gave the app.")
            }
        }
        .navigationTitle("About")
        .navigationBarTitleDisplayMode(.inline)
    }
}

/// A link row with a brand mark (template image: black or white with the appearance), opened with openURL.
struct CommunityRow: View {
    let logo: String
    let title: LocalizedStringKey
    var detail: String?
    let url: URL
    @Environment(\.openURL) private var openURL

    var body: some View {
        Button { openURL(url) } label: {
            HStack {
                Label {
                    Text(title)
                } icon: {
                    Image(logo).renderingMode(.template).resizable().scaledToFit()
                        .frame(width: 20, height: 20)
                        .frame(width: 29, height: 29)
                        .background(Color(.tertiarySystemFill), in: RoundedRectangle(cornerRadius: 7, style: .continuous))
                        .accessibilityHidden(true)
                }
                Spacer()
                if let detail { Text(verbatim: detail).foregroundStyle(.secondary) }
                Image(systemName: "arrow.up.forward").font(.footnote).foregroundStyle(.tertiary)
            }
        }
        .tint(.primary)
    }
}

struct AcknowledgementsView: View {
    private let items: [(name: String, license: String, note: LocalizedStringKey)] = [
        ("UxPlay", "GPL-3.0", "The board's AirPlay receiver is ported from UxPlay."),
        ("Arduino-ESP32 / ESP-IDF", "LGPL-2.1 / Apache-2.0", "Espressif's frameworks for the board firmware."),
        ("NimBLE", "Apache-2.0", "The board's Bluetooth LE stack."),
        ("TinyUSB", "MIT", "The board's USB mouse and keyboard."),
    ]

    var body: some View {
        List {
            ForEach(items, id: \.name) { item in
                VStack(alignment: .leading, spacing: Space.xs) {
                    HStack {
                        Text(verbatim: item.name).font(.headline)
                        Spacer()
                        Text(verbatim: item.license).font(.caption.weight(.semibold)).foregroundStyle(.secondary)
                    }
                    Text(item.note).font(.subheadline).foregroundStyle(.secondary)
                }
                .padding(.vertical, Space.xs)
            }
        }
        .navigationTitle("Acknowledgements")
        .navigationBarTitleDisplayMode(.inline)
    }
}

struct DisclaimerView: View {
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: Space.l) {
                PokeableFace(mood: .idle).frame(width: 64, height: 72)
                Text(Disclaimer.text).font(.body)
            }
            .padding(Space.xl)
        }
        .navigationTitle("Disclaimer")
        .navigationBarTitleDisplayMode(.inline)
    }
}

/// The app's text log, newest first; copyable.
struct LogsView: View {
    @ObservedObject var app: AppModel
    @State private var shareURL: URL?
    @State private var exporting = false

    var body: some View {
        List {
            Section {
                Button { exportAll() } label: {
                    HStack {
                        Label("Export all logs", systemImage: "archivebox")
                        Spacer()
                        if exporting { ProgressView() }
                    }
                }
                .disabled(exporting)
                if FileManager.default.fileExists(atPath: DebugLog.url.path) {
                    ShareLink(item: DebugLog.url) { Label("Share debug log", systemImage: "doc.text.magnifyingglass") }
                }
            } footer: {
                Text("A zip of debug.log, crash reports from iOS and every run (steps, model replies, screenshots, video). Your API key is never in it.")
            }
            if app.log.isEmpty {
                Text("Nothing logged yet.").foregroundStyle(.secondary)
            }
            ForEach(app.log.reversed()) { line in
                VStack(alignment: .leading, spacing: 2) {
                    Text(line.time, format: .dateTime.hour().minute().second()).font(.caption2).foregroundStyle(.secondary)
                    Text(verbatim: line.text).font(.caption.monospaced()).textSelection(.enabled)
                }
            }
        }
        .listStyle(.plain)
        .sheet(item: $shareURL) { ShareSheet(items: [$0]) }
        .navigationTitle("Logs")
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            ToolbarItem(placement: .topBarTrailing) {
                ShareLink(item: app.log.map { "\($0.time.formatted(.iso8601)) \($0.text)" }.joined(separator: "\n"))
            }
        }
    }

    /// Zip the whole Documents folder (debug.log, debug.1.log, traces/) off the main thread.
    private func exportAll() {
        exporting = true
        let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        DebugLog.write("export: all logs")
        Task {
            let result = await Task.detached { Result { try TraceStore.zip(docs, name: "esp32-agent-logs.zip") } }.value
            exporting = false
            switch result {
            case .success(let url): shareURL = url
            case .failure(let e): app.say("export: \(e.localizedDescription)")
            }
        }
    }
}
