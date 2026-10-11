// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation

/// The private NSExtension API (ExtensionFoundation) that starts an app
/// extension of Husk's own: the same calls LiveContainer uses to start its
/// LiveProcess.
@objc private protocol NSExtensionClassShim {
    @objc(extensionWithIdentifier:error:)
    func `extension`(withIdentifier identifier: String) throws -> AnyObject
}

@objc private protocol NSExtensionShim {
    @objc(beginExtensionRequestWithInputItems:completion:)
    func beginExtensionRequest(withInputItems items: [Any], completion: @escaping (NSUUID?) -> Void)
    @objc(pidForRequestIdentifier:)
    func pid(forRequestIdentifier identifier: NSUUID) -> Int32
    @objc(setRequestCompletionBlock:)
    func setRequestCompletionBlock(_ block: @escaping (NSUUID?, [Any]?) -> Void)
    @objc(setRequestCancellationBlock:)
    func setRequestCancellationBlock(_ block: @escaping (NSUUID?, NSError?) -> Void)
    @objc(setRequestInterruptionBlock:)
    func setRequestInterruptionBlock(_ block: @escaping (NSUUID?) -> Void)
}

/// Starts the separate helper process (PlugIns/HuskJITHelper.appex) that debugs Husk.
///
/// The helper is found by the bundle identifier it has in this installation,
/// read from its own Info.plist. Sideloaders that sign with the user's own
/// Apple ID (SideStore, AltStore, Plume, Impactor) append the team ID to Husk's
/// identifier and the helper's alike, so that lookup survives them. An
/// ExtensionKit extension point, which the helper used before, is named in
/// files they do not rewrite, and iOS registered none for a renamed Husk
/// ("Failed to add observer").
///
/// One extension request per HuskJITRequest. For `enable` the request lasts
/// until Husk detaches. Log category "jit-helper".
@MainActor
enum HuskBuiltInJIT {
    static let helperFile = "HuskJITHelper.appex"

    /// Requests that have started and not finished, kept alive until they do.
    private static var running: [HelperRequest] = []

    static var unavailableReason: String? {
#if targetEnvironment(simulator)
        return "内置 JIT 仅适用于实体设备。"
#else
        guard #available(iOS 26.0, *) else {
            return "内置 JIT 需要 iOS 26 或更高版本，请改用 StikDebug。"
        }
        if getenv("LC_HOME_PATH") != nil {
            return "在 LiveContainer 内无法使用内置 JIT，请改用 StikDebug。"
        }
        return nil
#endif
    }

    static var isAvailable: Bool { unavailableReason == nil }

    /// The helper's bundle identifier in this installation; nil when a sideloader left it out.
    static var helperIdentifier: String? {
        guard let url = Bundle.main.builtInPlugInsURL?.appendingPathComponent(helperFile) else { return nil }
        return Bundle(url: url)?.bundleIdentifier
    }

    static func send(_ request: HuskJITRequest,
                     started: @escaping () -> Void = {},
                     completion: @escaping (Result<HuskJITRequest.Response, Error>) -> Void) {
        func fail(_ code: Int, _ message: String) {
            HuskLog.log("jit-helper", message)
            completion(.failure(HelperRequest.error(code, message)))
        }
        guard unavailableReason == nil else {
            fail(1, unavailableReason ?? "内置 JIT 不可用。")
            return
        }
        guard let identifier = helperIdentifier else {
            fail(2, "当前安装中缺少 Husk JIT 辅助程序。请重新安装 Husk，"
                  + "如果侧载工具询问，请保留应用扩展；也可以改用 StikDebug。")
            return
        }
        if NSClassFromString("NSExtension") == nil {
            dlopen("/System/Library/Frameworks/ExtensionFoundation.framework/ExtensionFoundation", RTLD_NOW)
        }
        guard let extensionClass = NSClassFromString("NSExtension") else {
            fail(3, "iOS 未提供 Husk JIT 辅助程序所需的扩展 API。")
            return
        }
        let factory = unsafeBitCast(extensionClass as AnyObject, to: NSExtensionClassShim.self)
        let found: AnyObject
        do { found = try factory.extension(withIdentifier: identifier) }
        catch {
            fail(2, "找不到 Husk JIT 辅助程序（\(identifier)）：\(error.localizedDescription)。"
                  + "请重新安装 Husk。")
            return
        }
        guard let data = try? JSONEncoder().encode(request) else {
            fail(4, "无法编码 Husk JIT 辅助程序的请求。")
            return
        }
        let helper = HelperRequest(extension: found, operation: request.operation.rawValue,
                                   completion: completion)
        running.append(helper)
        helper.begin(data: data, identifier: identifier, started: started)
    }

    fileprivate static func finished(_ helper: HelperRequest) {
        running.removeAll { $0 === helper }
    }
}

/// One request to the helper, from its start to its one result.
@MainActor
private final class HelperRequest {
    private let shim: NSExtensionShim
    private let `extension`: AnyObject   // the NSExtension, kept alive for the request
    private let operation: String
    private var completion: ((Result<HuskJITRequest.Response, Error>) -> Void)?

    init(extension found: AnyObject, operation: String,
         completion: @escaping (Result<HuskJITRequest.Response, Error>) -> Void) {
        self.extension = found
        self.shim = unsafeBitCast(found, to: NSExtensionShim.self)
        self.operation = operation
        self.completion = completion
    }

    func begin(data: Data, identifier: String, started: @escaping () -> Void) {
        // The blocks can arrive on any queue; everything below runs on the main thread.
        shim.setRequestCompletionBlock { [weak self] _, items in
            let info = (items?.first as? NSExtensionItem)?.userInfo
            let payload = info?[HuskJITRequest.Response.itemKey] as? Data
            DispatchQueue.main.async { MainActor.assumeIsolated { self?.returned(payload) } }
        }
        shim.setRequestCancellationBlock { [weak self] _, error in
            let message = error?.localizedDescription ?? "请求已取消"
            DispatchQueue.main.async { MainActor.assumeIsolated {
                self?.end(.failure(Self.error(5, "Husk JIT 辅助程序已停止：\(message)")))
            } }
        }
        shim.setRequestInterruptionBlock { [weak self] _ in
            DispatchQueue.main.async { MainActor.assumeIsolated {
                self?.end(.failure(Self.error(6, "Husk JIT 辅助程序意外停止，请重试。")))
            } }
        }
        let item = NSExtensionItem()
        item.userInfo = [HuskJITRequest.itemKey: data]
        shim.beginExtensionRequest(withInputItems: [item]) { [weak self] uuid in
            DispatchQueue.main.async { MainActor.assumeIsolated {
                guard let self else { return }
                guard let uuid else {
                    self.end(.failure(Self.error(7, "Husk JIT 辅助程序未启动。请重新安装 Husk，"
                                                  + "如果侧载工具询问，请保留应用扩展。")))
                    return
                }
                HuskLog.log("jit-helper", "\(self.operation): started \(identifier) as pid "
                                        + "\(self.shim.pid(forRequestIdentifier: uuid))")
                started()
            } }
        }
    }

    private func returned(_ payload: Data?) {
        guard let payload,
              let response = try? JSONDecoder().decode(HuskJITRequest.Response.self, from: payload) else {
            end(.failure(Self.error(8, "Husk JIT 辅助程序已结束，但未返回结果。")))
            return
        }
        HuskLog.log("jit-helper", "\(operation): finished success=\(response.success ? 1 : 0)")
        end(.success(response))
    }

    private func end(_ result: Result<HuskJITRequest.Response, Error>) {
        guard let completion else { return }   // the first result counts
        self.completion = nil
        if case .failure(let error) = result {
            HuskLog.log("jit-helper", "\(operation): \(error.localizedDescription)")
        }
        HuskBuiltInJIT.finished(self)
        completion(result)
    }

    nonisolated static func error(_ code: Int, _ message: String) -> NSError {
        NSError(domain: "HuskBuiltInJIT", code: code, userInfo: [NSLocalizedDescriptionKey: message])
    }
}
