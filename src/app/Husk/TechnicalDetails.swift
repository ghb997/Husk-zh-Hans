// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

extension TLReport {
    /// Whether Husk will try to run it at all. A game with an engine Husk has a driver for runs on the native runtime; one
    /// without (Flappy Bird is plain Java with a couple of AndroidX helper libraries) runs on Husk's own Java interpreter.
    /// Only an APK with no 64-bit code, or one Husk could not read, is out of reach.
    var canRun: Bool {
        runsOnNativeRuntime || webKind != nil || ["java", "native", "nativeWithWork"].contains(verdict)
    }

    /// What runs it, in words.
    var runnerName: String {
        if runsOnNativeRuntime { return nativeEngineName }
        if let web = webKind { return "\(web.name)（网页应用）" }
        return canRun ? "Java（Husk 解释器）" : "无"
    }
}

extension TLApp {
    /// The folder the game keeps what it saves in, beside its APKs.
    var dataDirectory: URL {
        let name: String
        switch report?.nativeEngine {
        case .minecraft?: name = "minecraft-data"
        case .sdl?: name = "sdl-data"
        case .ue4?: name = "ue4-data"
        case .gta?: name = "gta-data"
        case .godot?: name = "godot-data"
        case .nativeactivity?: name = "na-data"
        case .flutter?: name = "flutter-data"
        case .gamemaker?: name = "gm-data"
        case .java?: name = "java-data"
        case .cocos?: name = "cocos-data"
        case nil: name = report?.webKind != nil ? "web" : "classic-data"
        default: name = "unity-data"
        }
        return TranslationLayer.root.appendingPathComponent(id, isDirectory: true).appendingPathComponent(name, isDirectory: true)
    }
}

/// What a game is, technically: how Husk runs it, what is in the APK, how its native libraries load, and its last run's log.
struct TLTechnicalView: View {
    let app: TLApp

    @State private var log: String?
    @State private var dataSize = "…"
    @State private var sharing = false

    private var report: TLReport? { app.report }
    private var logURL: URL { app.dataDirectory.appendingPathComponent("native-run.log") }
    private var arm64: [TLLibrary] { (report?.libraries ?? []).filter { $0.abi == "arm64-v8a" } }

    var body: some View {
        List {
            Section("运行环境") {
                row("运行引擎", report?.runnerName ?? "尚未扫描")
                row("包名", app.packageName ?? "—", mono: true)
                row("架构", report.map { $0.abis.isEmpty ? "无（仅 Java）" : $0.abis.joined(separator: ", ") } ?? "—")
                if let summary = report?.displaySummary {
                    Text(summary).font(.footnote).foregroundStyle(.secondary)
                }
            }

            Section("安装包") {
                ForEach(app.apks, id: \.self) { apk in
                    row((apk as NSString).lastPathComponent, size(of: apk), mono: true)
                }
                if let r = report {
                    row("Java 代码", r.dexCount == 0 ? "无" : "\(r.dexCount) 个 DEX 文件，\(bytes(r.dexBytes))")
                    row("使用的 Android 库", "\(r.systemLibraries.count)")
                }
                row("已保存的数据", dataSize)
            }

            if !arm64.isEmpty {
                Section {
                    ForEach(arm64) { lib in
                        VStack(alignment: .leading, spacing: 4) {
                            HStack {
                                Text(lib.name).font(.system(.subheadline, design: .monospaced)).lineLimit(1).truncationMode(.middle)
                                Spacer(minLength: 8)
                                Text(bytes(lib.bytes)).font(.caption).foregroundStyle(.secondary)
                            }
                            Text(libraryDetail(lib)).font(.caption).foregroundStyle(lib.status == "ok" ? Color.secondary : Color.orange)
                        }
                        .padding(.vertical, 2)
                    }
                } header: {
                    Text("原生库（\(arm64.count)）")
                } footer: {
                    Text("各个 64 位库在 iPhone 上的加载情况：重定位、Husk 改写的指令，以及加载器处理的兼容性问题。")
                }
            }

            Section {
                if let log, !log.isEmpty {
                    ScrollView(.horizontal) {
                        Text(log)
                            .font(.system(size: 10, design: .monospaced))
                            .textSelection(.enabled)
                            .fixedSize(horizontal: true, vertical: false)
                            .padding(.vertical, 4)
                    }
                    .frame(maxHeight: 320)
                    Button { sharing = true } label: { Label("分享日志", systemImage: "square.and.arrow.up") }
                } else {
                    Text(log == nil ? "正在读取…" : "暂无日志，首次运行游戏后会生成。")
                        .font(.footnote).foregroundStyle(.secondary)
                }
            } header: {
                Text("上次运行")
            } footer: {
                Text("上次启动游戏时的日志末尾内容。反馈问题时可分享此日志。")
            }
        }
        .listStyle(.insetGrouped)
        .navigationTitle("技术详情")
        .navigationBarTitleDisplayMode(.inline)
        .task {
            let url = logURL, dir = app.dataDirectory
            let (text, total) = await Task.detached(priority: .utility) { () -> (String, Int64) in
                let text = (try? String(contentsOf: url, encoding: .utf8)).map { full -> String in
                    let lines = full.split(separator: "\n", omittingEmptySubsequences: false)
                    return lines.suffix(200).joined(separator: "\n")
                } ?? ""
                var total: Int64 = 0
                let walker = FileManager.default.enumerator(at: dir, includingPropertiesForKeys: [.fileSizeKey])
                while let file = walker?.nextObject() as? URL {
                    total += Int64((try? file.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0)
                }
                return (text, total)
            }.value
            log = text
            dataSize = total == 0 ? "暂无数据" : bytes(total)
        }
        .sheet(isPresented: $sharing) { ShareSheet(items: [logURL]) }
    }

    private func row(_ label: String, _ value: String, mono: Bool = false) -> some View {
        LabeledContent {
            Text(value)
                .font(mono ? .system(.footnote, design: .monospaced) : .body)
                .multilineTextAlignment(.trailing)
                .textSelection(.enabled)
        } label: {
            Text(label).lineLimit(2)
        }
    }

    private func libraryDetail(_ lib: TLLibrary) -> String {
        var parts: [String] = []
        if let r = lib.relocations { parts.append("\(r) 次重定位") }
        if let t = lib.tpidrReads, t > 0 { parts.append("\(t) 次线程寄存器读取") }
        if let s = lib.svc, s > 0 { parts.append("\(s) 次系统调用") }
        if let c = lib.conflictPages, c > 0 { parts.append("\(c) 个代码与数据共享页面") }
        if lib.tls == true { parts.append("线程本地存储") }
        if !lib.notes.isEmpty { parts.append(contentsOf: lib.notes) }
        return parts.isEmpty ? (lib.status == "ok" ? "可直接加载" : lib.status) : parts.joined(separator: " · ")
    }

    private func size(of path: String) -> String {
        bytes(((try? FileManager.default.attributesOfItem(atPath: path)[.size]) as? NSNumber)?.int64Value ?? 0)
    }

    private func bytes(_ n: Int64) -> String { ByteCountFormatter.string(fromByteCount: n, countStyle: .file) }
}
