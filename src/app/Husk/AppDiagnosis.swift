// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

/// Why an app will not run, or why it stopped, in words: from what the APK is (before it starts) and from what its run logged.
///
/// Before a start it answers the questions the files can: is this an app at all, does it have code for this iPhone's CPU, is a
/// part of it missing, does it carry protection that refuses to run anywhere but a certified Android phone. After a run it reads
/// the log for the line that ended it: a crash in one of the app's libraries, the app closing itself, an exception from its Java
/// code, a feature of Android that Husk does not have yet, Google Play services it cannot do without.
struct AppDiagnosis: Identifiable, Equatable {
    enum Kind: String {
        case badAPK, unsupportedCPU, missingSplits, protected, needsGoogle, missingFeature, javaCrash, nativeCrash, closedItself,
             missingFunction, noProgress
    }
    let id = UUID()
    let kind: Kind
    let title: String
    let detail: String
    /// The log line or report field it came from, for the curious and for bug reports.
    let evidence: String?
    /// A warning before launch that still lets the app be tried.
    var blocksLaunch: Bool { kind == .badAPK || kind == .unsupportedCPU }

    static func == (a: AppDiagnosis, b: AppDiagnosis) -> Bool { a.kind == b.kind && a.title == b.title && a.detail == b.detail }

    var symbol: String {
        switch kind {
        case .badAPK: return "doc.badge.ellipsis"
        case .unsupportedCPU: return "cpu"
        case .missingSplits: return "square.stack.3d.up.slash"
        case .protected, .closedItself: return "lock.shield"
        case .needsGoogle: return "person.crop.circle.badge.exclamationmark"
        case .missingFeature, .missingFunction: return "puzzlepiece"
        case .javaCrash, .nativeCrash: return "exclamationmark.triangle.fill"
        case .noProgress: return "hourglass"
        }
    }

    // MARK: before a start

    /// Libraries of commercial app protectors: they check the device and the app's own files, and stop the app off a real phone.
    private static let protectorLibraries = ["libjiagu", "libDexHelper", "libsecexe", "libsecmain", "libprotectClass", "libdexprotector",
                                             "libapp_shield", "libNSaferOnly", "libshell-super", "libtup", "libAppGuard", "libxloader",
                                             "libarmour", "libnesec", "libSecShell", "libkwscmm", "libmobisec",
                                             // seen in Play apps: ByteDance's metasec (TikTok, CapCut), Netflix's, Akamai's bot manager
                                             // (eBay), and games whose own library refuses uncertified devices (Clash Royale, HCR2)
                                             "libmetasec", "libcf2a", "libakamaibap", "libakamaibmp", "libk30d", "libsupercell_", "libfingersoft_hcr2", "libfp"]

    static func preflight(_ app: TLApp) -> AppDiagnosis? {
        guard let r = app.report else { return nil }
        if !r.ok || r.verdict == "unreadable" {
            return AppDiagnosis(kind: .badAPK, title: "无效的 APK",
                                detail: "Husk 无法读取此应用文件" + (r.error.map { ": \($0)." } ?? ".")
                                    + " 文件可能已损坏、尚未下载完整，或并非 Android 应用。",
                                evidence: r.error ?? r.summary)
        }
        if r.verdict == "noArm64" {
            return AppDiagnosis(kind: .unsupportedCPU, title: "不支持此 CPU 架构",
                                detail: r.summary + " iPhone 只能运行 64 位 ARM（arm64）代码，请寻找此应用的 arm64 或通用版本。",
                                evidence: "ABIs: " + r.abis.joined(separator: ", "))
        }
        if app.apks.count == 1, let base = app.apks.first, husk_apk_split_required(base) != 0 {
            return AppDiagnosis(kind: .missingSplits, title: "应用安装包不完整",
                                detail: "此应用由多个拆分 APK 组成，但目前只导入了基础包，缺少所需的库或资源。"
                                    + "请导入完整安装包（.apks、.xapk 或 .apkm），或从 Husk 商店获取。",
                                evidence: "manifest: split APKs required")
        }
        if let lib = r.libraries.first(where: { l in protectorLibraries.contains(where: { l.name.hasPrefix($0) }) }) {
            return AppDiagnosis(kind: .protected, title: "应用受保护",
                                detail: "此应用包含防篡改保护（\(lib.name)），会检查是否运行在经过认证的"
                                    + "Android 手机上，否则会关闭应用，因此可能无法在 Husk 中启动。",
                                evidence: lib.name)
        }
        return nil
    }

    // MARK: after a run

    /// The reason a run stopped, from its log; nil when nothing in it explains the end (the app closed the normal way).
    static func fromLog(_ text: String) -> AppDiagnosis? {
        let lines = text.split(separator: "\n", omittingEmptySubsequences: true).map(String.init)

        // A native crash: the runtime's crash handler names the library the bad instruction was in
        if let i = lines.lastIndex(where: { $0.contains("=== CRASH:") || $0.contains("*** FATAL SIGNAL") }) {
            let head = lines[i]
            let lib = lines[(i + 1)..<min(lines.count, i + 4)].compactMap { l -> String? in
                guard l.contains("pc ") else { return nil }
                return l.split(separator: " ").map(String.init).first { $0.hasSuffix(".so") || $0.contains(".so") }
            }.first
            let signal = head.range(of: "signal \\d+", options: .regularExpression).map { String(head[$0]) } ?? "致命信号"
            if let lib, !lib.hasPrefix("libhusk"), !lib.contains("Husk") {
                return AppDiagnosis(kind: .nativeCrash, title: "在 \(lib) 中崩溃",
                                    detail: "应用的原生代码（\(lib)）因 \(signal) 停止运行。可能需要 Husk 尚未提供的"
                                        + "Android 功能，或执行了 iOS 不允许的操作。",
                                    evidence: head)
            }
            return AppDiagnosis(kind: .nativeCrash, title: "应用已崩溃",
                                detail: "应用因 \(signal) 停止运行。报告包含此次运行的完整日志。", evidence: head)
        }

        // The app ended itself from native code: protection that found it is not on a certified phone, most of the time
        if let l = lines.last(where: { $0.contains("bionic: exit(") && $0.contains("called from") }),
           let code = l.range(of: "exit\\((\\d+)\\)", options: .regularExpression).map({ String(l[$0]) }), code != "exit(0)" {
            let lib = l.components(separatedBy: "called from ").last?.split(separator: " ").first.map(String.init) ?? "应用代码"
            return AppDiagnosis(kind: .closedItself, title: "应用拒绝在此环境中运行",
                                detail: "\(lib) 主动关闭了应用（\(code)）。带有防篡改或反作弊保护的应用"
                                    + "可能会在检测到非认证 Android 设备时退出，Husk 无法满足其认证要求。",
                                evidence: l)
        }

        // Java: the exception that ended the app's main thread
        if let l = lines.last(where: { $0.contains("main thread ended with ") }) {
            let what = l.components(separatedBy: "main thread ended with ").last ?? l
            let name = String(what.prefix { $0 != ":" && $0 != " " })
            let short = name.split(separator: ".").last.map(String.init) ?? name
            let message = what.dropFirst(name.count).drop { $0 == ":" || $0 == " " }
            let firstPart = message.components(separatedBy: " | caused by").first ?? String(message)
            if ["NoSuchMethodError", "NoClassDefFoundError", "UnsatisfiedLinkError", "AbstractMethodError", "NoSuchFieldError",
                "IncompatibleClassChangeError"].contains(short) {
                return AppDiagnosis(kind: .missingFeature, title: "使用了 Husk 尚未支持的功能",
                                    detail: "此应用需要 Husk 运行环境尚未提供的 Android 功能：\(apiName(firstPart))。",
                                    evidence: l)
            }
            if what.localizedCaseInsensitiveContains("google play services") || what.contains("GooglePlayServices") {
                return googleDiagnosis(l)
            }
            return AppDiagnosis(kind: .javaCrash, title: "应用已崩溃",
                                detail: "应用代码因 \(short) 停止运行" + (firstPart.isEmpty ? "." : ": \(firstPart.prefix(220))"),
                                evidence: l)
        }

        // Google Play services the app will not run without
        if let l = lines.last(where: { $0.contains("requires Google Play services") || $0.contains("Failed to fetch kids onboarding status")
                                        || $0.contains("GooglePlayServicesNotAvailableException") }),
           lines.contains(where: { $0.contains("the app finished") }) {
            return googleDiagnosis(l)
        }

        // An app whose work is done by Linux programs it starts as processes of their own (Winlator's and GameNative's Box64 and Wine,
        // a proot userland): its own screens run, the programs can not, as an iOS app may not start processes
        if let l = lines.first(where: { $0.contains("the app started a process:") }),
           ["box64", "box86", "wine", "proot", "/usr/bin/", "imagefs", "rootfs", "termux", "ld-linux"].contains(where: { l.localizedCaseInsensitiveContains($0) }) {
            let prog = l.components(separatedBy: "the app started a process: ").last?.components(separatedBy: " -- ").first ?? "某个程序"
            return AppDiagnosis(kind: .missingFeature, title: "需要启动独立的 Linux 进程",
                                detail: "此应用的主要功能依赖独立启动的 Linux 程序（此处为 \(prog.prefix(120))）。"
                                    + "应用界面可以在 Husk 中运行，但 iOS 不允许应用启动其他进程，因此这些程序"
                                    + "及其运行的游戏或工具暂时无法启动。",
                                evidence: l)
        }

        // An app that only works signed in to a Google account: it asked Android's account picker, got none, and closed
        if let l = lines.last(where: { $0.contains("ChooseTypeAndAccountActivity") || $0.contains("AccountManager.newChooseAccountIntent") }),
           lines.contains(where: { $0.contains("the app finished") }) {
            return AppDiagnosis(kind: .needsGoogle, title: "需要 Google 账户",
                                detail: "此应用必须登录 Google 账户才能使用。它请求 Android 选择账户，"
                                    + "但 Husk 无法提供 Google 账户，因此应用已关闭。",
                                evidence: l)
        }

        // A game whose data comes as an expansion file the store did not install
        if let l = lines.first(where: { $0.contains("expansion file (OBB) is missing") }) {
            return AppDiagnosis(kind: .missingSplits, title: "缺少游戏数据",
                                detail: "此游戏的大部分数据存储在 OBB 扩展文件中，由 Play 商店单独下载。"
                                    + "Husk 目前只安装了应用本身，缺少该文件，游戏无法加载。",
                                evidence: l)
        }

        // Video that only plays with DRM keys
        if let l = lines.first(where: { $0.contains("Husk cannot decrypt protected media") }) {
            return AppDiagnosis(kind: .protected, title: "不支持受保护的视频",
                                detail: "此视频受 Widevine DRM 保护，只有经过认证的 Android 设备才能解密。"
                                    + "无保护的视频可以播放，受保护的视频会保持黑屏。",
                                evidence: l)
        }

        // A function of Android's C library or system libraries that was never written for Husk
        if let l = lines.first(where: { $0.contains("CALLED an unresolved import: ") }), lines.contains(where: { $0.contains("the app finished") || $0.contains("CRASH") }) {
            let fn = l.components(separatedBy: "unresolved import: ").last?.split(separator: " ").first.map(String.init) ?? "?"
            return AppDiagnosis(kind: .missingFunction, title: "使用了 Husk 尚未支持的功能",
                                detail: "此应用调用了 Husk 尚未提供的系统函数 \(fn)。", evidence: l)
        }
        return nil
    }

    private static func googleDiagnosis(_ line: String) -> AppDiagnosis {
        AppDiagnosis(kind: .needsGoogle, title: "需要 Google Play 服务",
                     detail: "此应用对 Google Play 服务（Google 登录、账户和设备检查）的依赖超出了 Husk 的支持范围。"
                        + "只使用 Google 库的应用可以运行，而此应用缺少真实的 Play 服务就会停止。",
                     evidence: line)
    }

    /// "android/os/StrictMode$ThreadPolicy$Builder.penaltyListener(...)" as "StrictMode.ThreadPolicy.Builder.penaltyListener".
    private static func apiName(_ s: String) -> String {
        var t = s.components(separatedBy: "(").first ?? s
        t = t.replacingOccurrences(of: "No implementation found for ", with: "")
        t = t.replacingOccurrences(of: "$", with: ".")
        let parts = t.split(separator: "/").map(String.init)
        return (parts.count > 1 ? parts.suffix(2).joined(separator: ".") : t).trimmingCharacters(in: .whitespaces)
    }
}

/// A diagnosis, as a card: on an app's page before it starts, and over an app's screen when it stops.
struct AppDiagnosisCard: View {
    let diagnosis: AppDiagnosis
    var showsEvidence = false

    var body: some View {
        HStack(alignment: .top, spacing: 12) {
            Image(systemName: diagnosis.symbol)
                .font(.system(size: 20, weight: .semibold))
                .foregroundStyle(diagnosis.blocksLaunch ? .red : .orange)
                .frame(width: 26)
            VStack(alignment: .leading, spacing: 4) {
                Text(diagnosis.title).font(.subheadline.weight(.semibold))
                Text(diagnosis.detail).font(.footnote).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                if showsEvidence, let e = diagnosis.evidence {
                    Text(e).font(.system(size: 10.5, design: .monospaced)).foregroundStyle(.secondary)
                        .lineLimit(4).textSelection(.enabled).padding(.top, 2)
                }
            }
            Spacer(minLength: 0)
        }
        .padding(14)
        .background(Color(.secondarySystemBackground), in: RoundedRectangle(cornerRadius: 14, style: .continuous))
    }
}

/// Over an app's screen when its run ends with a reason: what happened, and the way out.
struct AppStoppedOverlay: View {
    let app: TLApp
    let diagnosis: AppDiagnosis
    let close: () -> Void
    @State private var report: URL?

    var body: some View {
        ZStack {
            Color.black.opacity(0.55).ignoresSafeArea()
            VStack(alignment: .leading, spacing: 14) {
                Text(app.label).font(.footnote.weight(.semibold)).foregroundStyle(.secondary)
                AppDiagnosisCard(diagnosis: diagnosis, showsEvidence: true)
                HStack(spacing: 10) {
                    if let report {
                        ShareLink(item: report) { Label("分享报告", systemImage: "square.and.arrow.up").frame(maxWidth: .infinity) }
                            .buttonStyle(.bordered)
                    }
                    Button(action: close) { Text("关闭").frame(maxWidth: .infinity) }
                        .buttonStyle(.borderedProminent)
                }
            }
            .padding(18)
            .frame(maxWidth: 440)
            .background(Color(.systemBackground), in: RoundedRectangle(cornerRadius: 20, style: .continuous))
            .padding(24)
        }
        .task { report = AppDiagnosis.writeReport(app: app, diagnosis: diagnosis) }
    }
}

extension AppDiagnosis {
    /// The run that just ended, in one text file to share: the reason, the app, this device, and the runtime's log of the run.
    @MainActor static func writeReport(app: TLApp, diagnosis d: AppDiagnosis) -> URL? {
        let info = Bundle.main.infoDictionary ?? [:]
        var text = """
        Husk 应用报告
        ===============
        应用     : \(app.label) (\(app.packageName ?? "?"))
        问题     : \(d.title)
        原因     : \(d.detail)
        证据     : \(d.evidence ?? "-")
        Husk     : \(info["CFBundleShortVersionString"] as? String ?? "?") (\(info["CFBundleVersion"] as? String ?? "?"))
        设备     : \(HuskLog.deviceModel), \(UIDevice.current.systemName) \(UIDevice.current.systemVersion)
        APKs     : \(app.apks.map { ($0 as NSString).lastPathComponent }.joined(separator: ", "))
        引擎     : \(app.report?.runnerName ?? "?")

        ---- 此次运行日志 ----

        """
        if let c = husk_tl_attempt_log() { text += String(cString: c); free(c) }
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("Husk report - \(app.label.replacingOccurrences(of: "/", with: "-")).txt")
        do { try text.write(to: url, atomically: true, encoding: .utf8) } catch { return nil }
        return url
    }

    /// When a run has ended: why, from its log (nil when it closed the normal way).
    static func ofCurrentRun() -> AppDiagnosis? {
        guard let c = husk_tl_attempt_log() else { return nil }
        defer { free(c) }
        return fromLog(String(cString: c))
    }
}
