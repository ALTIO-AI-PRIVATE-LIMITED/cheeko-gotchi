import Foundation
import NetworkExtension
import Security
import UIKit

/// LLM settings. The API key lives in the Keychain, everything else in UserDefaults.
@MainActor final class Settings: ObservableObject {
    static let defaultBaseURL = ""  // user enters their own OpenAI-compatible endpoint
    static let defaultModel = ""

    @Published private(set) var provider: ModelProvider = .others
    @Published private(set) var configurationRevision = 0
    private let defaults: UserDefaults
    @Published var baseURL: String { didSet {
        defaults.set(baseURL, forKey: "baseURL")
        if baseURL != oldValue {
            // Never carry an old provider's credential to a newly typed endpoint.
            if URL(string: oldValue)?.host != URL(string: baseURL)?.host { apiKey = "" }
            modelsChanged()
        }
    } }
    @Published var model: String { didSet { defaults.set(model, forKey: "model"); configurationRevision += 1 } }  // empty preserves a legacy saved selection
    /// The model picked from the endpoint's list when `model` is empty (forgotten when the endpoint or key changes).
    @Published private(set) var autoModel: String { didSet { defaults.set(autoModel, forKey: "autoModel") } }
    @Published var plannerModel: String { didSet { defaults.set(plannerModel, forKey: "plannerModel"); configurationRevision += 1 } }  // "" = model
    @Published var actorModel: String { didSet { defaults.set(actorModel, forKey: "actorModel"); configurationRevision += 1 } }        // "" = model
    @Published var tokenParam: String { didSet { defaults.set(tokenParam, forKey: "tokenParam"); configurationRevision += 1 } }        // auto | ...
    @Published var extraBody: String { didSet { defaults.set(extraBody, forKey: "extraBody"); configurationRevision += 1 } }          // JSON object
    @Published var apiKey: String { didSet { Keychain.set(apiKey, for: "llm-api-key"); DebugLog.redact([apiKey])
        if apiKey != oldValue { modelsChanged() } } }
    /// What the agent is called (Agent tab title, greeting, Live Activity); empty = the default.
    /// The endpoint's model list (GET /models), for the pickers; empty until loaded or when it can't be fetched.
    @Published private(set) var models: [String] = []
    @Published private(set) var loadingModels = false
    @Published private(set) var modelsError: String?
    private var modelsRequest = 0
    @Published var agentName: String {
        didSet {
            let trimmed = agentName.trimmingCharacters(in: .whitespacesAndNewlines)
            defaults.set(trimmed.isEmpty ? nil : trimmed, forKey: Self.agentNameKey)
            let shown = trimmed.isEmpty ? Self.defaultAgentName : trimmed
            if shown != agentName { agentName = shown }  // no observer re-entry inside didSet
        }
    }
    static let agentNameKey = "agentName"
    static var defaultAgentName: String { String(localized: "ESP32 Agent") }
    /// The name, for code without a Settings instance (Live Activity).
    static var storedAgentName: String { UserDefaults.standard.string(forKey: agentNameKey) ?? defaultAgentName }
    @Published var onboarded: Bool { didSet { defaults.set(onboarded, forKey: "onboarded") } }
    @Published var acceptedDisclaimer: Bool { didSet { defaults.set(acceptedDisclaimer, forKey: "acceptedDisclaimer") } }

    /// info: where build-time presets are read from (the app's Info.plist; tests pass none).
    init(defaults: UserDefaults = .standard, info: [String: Any]? = Bundle.main.infoDictionary) {
        self.defaults = defaults
        let preset = { (key: String) in Self.stored((info?[key] as? String)?.trimmingCharacters(in: .whitespaces)) }
        baseURL = Self.stored(defaults.string(forKey: "baseURL")) ?? preset("IAPresetBaseURL") ?? Self.defaultBaseURL
        model = Self.stored(defaults.string(forKey: "model")) ?? preset("IAPresetModel") ?? Self.defaultModel
        autoModel = defaults.string(forKey: "autoModel") ?? ""
        plannerModel = defaults.string(forKey: "plannerModel") ?? ""
        actorModel = defaults.string(forKey: "actorModel") ?? ""
        tokenParam = defaults.string(forKey: "tokenParam") ?? "auto"
        extraBody = defaults.string(forKey: "extraBody") ?? "{}"
        apiKey = Self.stored(Keychain.get("llm-api-key")) ?? preset("IAPresetKey") ?? ""
        agentName = defaults.string(forKey: Self.agentNameKey) ?? Self.defaultAgentName
        onboarded = defaults.bool(forKey: "onboarded")
        acceptedDisclaimer = defaults.bool(forKey: "acceptedDisclaimer")
        provider = defaults.string(forKey: "modelProvider").flatMap(ModelProvider.init(rawValue:))
            ?? ModelProvider.infer(baseURL: baseURL, model: model)
        DebugLog.redact([apiKey])  // never in debug.log
    }

    private static func stored(_ v: String?) -> String? { v?.isEmpty == false ? v : nil }

    /// The endpoint is valid (local endpoints may not require a key).
    var isConfigured: Bool {
        guard let url = URL(string: baseURL), let host = url.host, !host.isEmpty else { return false }
        return ["http", "https"].contains(url.scheme?.lowercased() ?? "")
    }

    /// The manual override, else the model already picked from the endpoint, else nil.
    var currentModel: String? {
        let manual = model.trimmingCharacters(in: .whitespaces)
        if !manual.isEmpty { return manual }
        return autoModel.isEmpty ? nil : autoModel
    }

    /// Use an explicit model or a previously saved selection; never guess from API list order.
    func resolvedModel() async throws -> String {
        if let m = currentModel { return m }
        throw LLMError(message: "Choose a vision model or enter its model ID before testing.")
    }

    private func modelsChanged() {
        configurationRevision += 1
        modelsRequest += 1
        loadingModels = false
        autoModel = ""
        models = []
        modelsError = nil
    }

    var selectedProvider: ModelProvider {
        get { provider }
        set { selectProvider(newValue) }
    }

    func selectProvider(_ choice: ModelProvider) {
        guard choice != provider else { return }
        // Keep credentials scoped to an endpoint when switching providers.
        if !apiKey.isEmpty { Keychain.set(apiKey, for: "llm-endpoint-" + baseURL) }
        provider = choice
        defaults.set(choice.rawValue, forKey: "modelProvider")
        if choice == .others { configurationRevision += 1; return }
        let changedEndpoint = baseURL != choice.baseURL
        baseURL = choice.baseURL
        if changedEndpoint { apiKey = Keychain.get("llm-endpoint-" + baseURL) ?? "" }
        model = ""
        plannerModel = ""
        actorModel = ""
        tokenParam = "auto"
        extraBody = "{}"
        modelsChanged()
    }

    /// Fetch the model list; only a result for the current configuration is accepted.
    func loadModels() async {
        guard isConfigured else { return }
        modelsRequest += 1
        let request = modelsRequest
        loadingModels = true
        defer { if request == modelsRequest { loadingModels = false } }
        do {
            let list = try await LLMClient.models(baseURL: baseURL, apiKey: apiKey)
            guard request == modelsRequest else { return }
            models = list
            modelsError = list.isEmpty ? String(localized: "The endpoint lists no models.") : nil
            // Model order does not indicate image support; never silently select the first entry.
            DebugLog.write("models: \(list.count) listed, first \(list.first ?? "-")")
        } catch {
            guard request == modelsRequest else { return }
            models = []
            modelsError = error.localizedDescription
            DebugLog.write("models: \(error.localizedDescription)")
        }
    }

    var extra: [String: Any] {
        (try? JSONSerialization.jsonObject(with: Data(extraBody.utf8))) as? [String: Any] ?? [:]
    }

    /// role "planner" or "actor": its own model if set, else the shared one.
    func client(_ role: String) async throws -> LLMClient {
        let own = role == "planner" ? plannerModel : actorModel
        return LLMClient(baseURL: baseURL, model: own.isEmpty ? try await resolvedModel() : own, apiKey: apiKey,
                         extra: extra, tokenParam: tokenParam)
    }
}

enum Keychain {
    private static func query(_ account: String) -> [CFString: Any] {
        [kSecClass: kSecClassGenericPassword, kSecAttrService: Bundle.main.bundleIdentifier ?? "iphoneagent", kSecAttrAccount: account]
    }

    static func get(_ account: String) -> String? {
        var q = query(account)
        q[kSecReturnData] = true
        var out: CFTypeRef?
        guard SecItemCopyMatching(q as CFDictionary, &out) == errSecSuccess, let data = out as? Data else { return nil }
        return String(data: data, encoding: .utf8)
    }

    static func set(_ value: String, for account: String) {
        SecItemDelete(query(account) as CFDictionary)
        guard !value.isEmpty else { return }
        var q = query(account)
        q[kSecValueData] = Data(value.utf8)
        q[kSecAttrAccessible] = kSecAttrAccessibleAfterFirstUnlock  // the agent may run while the phone is locked
        SecItemAdd(q as CFDictionary, nil)
    }
}

/// Joins the board's SoftAP with the credentials read over BLE (never shown or logged).
enum Hotspot {
    static func join(_ net: NetConfig) async throws {
        guard let ssid = net.ssid, let pass = net.pass else { throw DeviceError(message: "board sent no hotspot credentials") }
        let config = NEHotspotConfiguration(ssid: ssid, passphrase: pass, isWEP: false)
        config.joinOnce = false
        do {
            try await NEHotspotConfigurationManager.shared.apply(config)
        } catch let e as NSError where e.domain == NEHotspotConfigurationErrorDomain
                    && e.code == NEHotspotConfigurationError.alreadyAssociated.rawValue {
            return
        }
    }

    /// For the debug log: domain, code, the NEHotspotConfigurationError name and the message.
    static func describe(_ e: NSError) -> String {
        guard e.domain == NEHotspotConfigurationErrorDomain else { return "\(e.domain) \(e.code): \(e.localizedDescription)" }
        let names = ["invalid", "invalidSSID", "invalidWPAPassphrase", "invalidWEPPassphrase", "invalidEAPSettings",
                     "invalidHS20Settings", "invalidHS20DomainName", "userDenied", "internal", "pending",
                     "systemConfiguration", "unknown", "joinOnceNotSupported", "alreadyAssociated",
                     "applicationIsNotInForeground", "invalidSSIDPrefix", "userUnauthorized", "systemDenied"]
        let name = names.indices.contains(e.code) ? names[e.code] : "?"
        return "\(e.domain) \(e.code) \(name): \(e.localizedDescription)"
    }

    static func userDenied(_ e: NSError) -> Bool {
        e.domain == NEHotspotConfigurationErrorDomain && e.code == NEHotspotConfigurationError.userDenied.rawValue
    }

    /// Settings › Wi-Fi when iOS allows the deep link, else this app's page in Settings.
    @MainActor static func openWiFiSettings() {
        let app = UIApplication.shared
        // iOS 18+ has no public or private link to the Wi-Fi page: page links like App-prefs:root=WIFI
        // "succeed" but land on this app's page. The bare scheme opens the top of Settings (Wi-Fi is
        // the second row). Last resort: this app's page.
        var urls = ["App-prefs:"].compactMap(URL.init(string:))
        urls.append(URL(string: UIApplication.openSettingsURLString)!)
        func next() {
            guard !urls.isEmpty else { return }
            app.open(urls.removeFirst()) { ok in if !ok { next() } }
        }
        next()
    }

    /// The SSID the phone is on, if it is one of ours (needs the hotspot entitlement).
    static func currentSSID() async -> String? {
        await NEHotspotNetwork.fetchCurrent()?.ssid
    }
}
