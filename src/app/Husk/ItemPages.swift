// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

// MARK: - the top of every page

/// An item's page opens on its artwork: the icon over a blurred, darkened copy of itself, the name, a line under it, and
/// the one thing to do -- Play, Open, Start -- as a big button.
private struct PageHero<Action: View>: View {
    let item: LibraryItem
    let subtitle: String
    @ViewBuilder var action: Action

    var body: some View {
        VStack(spacing: 0) {
            ZStack(alignment: .bottomLeading) {
                ItemBackdrop(item: item, fallback: Color(uiColor: .tertiarySystemFill))
                LinearGradient(colors: [.clear, .black.opacity(0.7)], startPoint: .top, endPoint: .bottom)
                HStack(alignment: .bottom, spacing: 14) {
                    ItemIcon(item: item, size: 84)
                        .shadow(color: .black.opacity(0.35), radius: 12, y: 5)
                    VStack(alignment: .leading, spacing: 4) {
                        Text(item.title)
                            .font(.system(.title2, design: .rounded).weight(.heavy))
                            .lineLimit(2)
                            .minimumScaleFactor(0.8)
                        Text(subtitle)
                            .font(.subheadline)
                            .foregroundStyle(.white.opacity(0.75))
                            .lineLimit(2)
                    }
                    .foregroundStyle(.white)
                    Spacer(minLength: 0)
                }
                .padding(16)
            }
            .frame(height: 210)
            action
                .padding(14)
        }
        .background(Color(uiColor: .secondarySystemGroupedBackground))
        .clipShape(RoundedRectangle(cornerRadius: 24, style: .continuous))
    }
}

/// The page's main button.
private struct BigButton: View {
    let title: String
    let systemImage: String
    var prominent = true
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Label(title, systemImage: systemImage)
                .font(.system(.headline, design: .rounded))
                .frame(maxWidth: .infinity)
                .frame(height: 50)
                .foregroundStyle(prominent ? Color.white : Color.accentColor)
                .background(prominent ? Color.accentColor : Color.accentColor.opacity(0.14),
                            in: RoundedRectangle(cornerRadius: 15, style: .continuous))
        }
        .buttonStyle(CardButtonStyle())
    }
}

/// A hero that sits in a List as its first row, edge to edge within the list's margins.
private extension View {
    func heroRow() -> some View {
        listRowInsets(EdgeInsets()).listRowBackground(Color.clear)
    }
}

private func fileBytes(_ paths: [String]) -> String {
    let total = paths.reduce(Int64(0)) { sum, p in
        sum + (((try? FileManager.default.attributesOfItem(atPath: p)[.size]) as? NSNumber)?.int64Value ?? 0)
    }
    return ByteCountFormatter.string(fromByteCount: total, countStyle: .file)
}

@MainActor private func turnOnJIT() {
    let jit = JITCoordinator.shared
    if HuskBuiltInJIT.isAvailable { jit.method = .builtIn }
    jit.enable()
}

// MARK: - a game

/// A translation-layer game: Play, its settings, what it is, and removing it.
struct GamePage: View {
    let app: TLApp
    @ObservedObject private var showcase = ShowcaseStore.shared

    @ObservedObject private var store = TranslationLayerStore.shared
    @ObservedObject private var jit = JITCoordinator.shared
    @Environment(\.dismiss) private var dismiss
    @State private var playing = false
    @State private var confirmRemove = false
    @State private var backupFile: URL?
    @State private var savesBusy = false
    @State private var savesMessage: String?
    @State private var confirmRestore: URL?
    @State private var confirmQuit = false
    @State private var reporting: CompatSubject?

    private var runs: Bool { app.report?.canRun == true }

    private var compatSubject: CompatSubject? {
        app.packageName.map { CompatSubject(package: $0, name: app.label, isGame: LibraryItem.game(app).isGame, iconPath: app.iconPath) }
    }

    /// Another game already loaded in this run of Husk: engines cannot be unloaded, so this one cannot start until Husk is
    /// closed and opened again.
    private var blockedBy: String? {
        guard let loaded = husk_native_loaded_apk().map({ String(cString: $0) }), let apk = app.apks.first, loaded != apk
        else { return nil }
        return store.apps.first { $0.apks.first == loaded }?.label ?? "另一个游戏"
    }

    private var preflight: AppDiagnosis? { AppDiagnosis.preflight(app) }

    private var subtitle: String {
        if let used = LibraryItem.game(app).usedText { return used }
        return runs ? "可以开始游戏" : "可能无法在此 iPhone 上运行"
    }

    var body: some View {
        List {
            Section {
                PageHero(item: .game(app), subtitle: subtitle) { primary }
            } footer: {
                if isGeodeLauncher {
                    Text("这是 Geode 的 Android 启动器。在 Husk 中，Geode 直接运行在 Geometry Dash 内。请在此处启用"
                       + "（也可在 Geometry Dash 的“游戏设置 › 模组”中启用），然后启动 Geometry Dash，"
                       + "通过主菜单上的 Geode 按钮获取模组。此 APK 提供了 Geode 所需的库，请保留。")
                } else if let other = blockedBy {
                    Text("\(other) 仍处于加载状态，游戏启动后无法卸载出内存。请使用上方按钮关闭 Husk，"
                       + "然后重新打开，即可运行 \(app.label)。")
                } else if !runs {
                    Text("此 APK 不包含 Husk 能运行的 64 位代码，因此可能无法启动。")
                }
            }
            .heroRow()

            // what the files say before it starts: a damaged APK, the wrong CPU, a missing piece, protection that refuses Husk
            if let d = preflight {
                Section {
                    AppDiagnosisCard(diagnosis: d)
                        .listRowInsets(EdgeInsets(top: 4, leading: 4, bottom: 4, trailing: 4))
                }
            }
            // why it stopped last time, until it plays again
            if let d = GameStatusStore.shared.diagnosis(app.id), d != preflight {
                Section("上次运行") {
                    AppDiagnosisCard(diagnosis: d, showsEvidence: true)
                        .listRowInsets(EdgeInsets(top: 4, leading: 4, bottom: 4, trailing: 4))
                }
            }

            if let compatSubject, !isGeodeLauncher {
                CompatibilitySection(subject: compatSubject, reporting: $reporting)
            }

            if !ShowcaseStore.shared.pictures(for: app.packageName).isEmpty {
                Section("截图") {
                    ShowcaseGallery(package: app.packageName)
                        .listRowInsets(EdgeInsets(top: 10, leading: 12, bottom: 10, trailing: 12))
                }
            }

            Section {
                HuskLink(value: LibraryRoute.gameSettings(app.id)) {
                    Label("游戏设置", systemImage: "slider.horizontal.3")
                }
                HuskLink(value: LibraryRoute.gameReport(app.id)) {
                    Label("技术详情", systemImage: "cpu")
                }
            }

            if !isGeodeLauncher {
                Section {
                    Button { backUp() } label: { Label("备份存档", systemImage: "square.and.arrow.up") }
                        .disabled(savesBusy || !SaveBackup.hasData(app))
                    Button { pickRestore() } label: { Label("恢复存档", systemImage: "clock.arrow.circlepath") }
                        .disabled(savesBusy || loadedNow)
                } header: {
                    Text("存档")
                } footer: {
                    if let savesMessage { Text(savesMessage) }
                    else if loadedNow { Text("本次运行 Husk 时已启动 \(app.label)。请关闭并重新打开 Husk，再恢复存档。") }
                    else { Text("备份是包含 \(app.label) 在 Husk 中保存的全部数据的 ZIP 文件，可保存在“文件”中或转移到其他设备。") }
                }
            }

            Section("关于") {
                LabeledContent("运行引擎", value: app.report?.runnerName ?? "未知")
                if let st = GameStatusStore.shared.status(app.id) {
                    LabeledContent("上次运行结果") {
                        Label(st.result.label, systemImage: st.result.symbol).foregroundStyle(st.result.color)
                    }
                }
                LabeledContent("上次游玩", value: app.lastPlayed.map { $0.formatted(.dateTime.year().month().day().hour().minute().locale(Locale(identifier: "zh_Hans_CN"))) } ?? "从未")
                LabeledContent("大小", value: fileBytes(app.apks))
            }

            Section {
                Button(role: .destructive) { confirmRemove = true } label: {
                    Label("移除游戏", systemImage: "trash")
                }
            }
        }
        .listStyle(.insetGrouped)
        .navigationTitle(app.label)
        .navigationBarTitleDisplayMode(.inline)
        .fullScreenCover(isPresented: $playing, onDismiss: askHowItRan) { TLAttemptView(app: app) }
        .sheet(item: $reporting) { CompatReportSheet(subject: $0) }
        .sheet(item: Binding(get: { backupFile.map(IdentifiedURL.init) }, set: { backupFile = $0?.url })) { f in
            ShareSheet(items: [f.url])
        }
        .confirmationDialog("替换 \(app.label) 的存档？", isPresented: Binding(get: { confirmRestore != nil }, set: { if !$0 { confirmRestore = nil } }),
                            titleVisibility: .visible) {
            Button("替换", role: .destructive) { if let url = confirmRestore { restore(url) } }
        } message: {
            Text("\(app.label) 当前的存档将被备份中的数据替换。如需保留，请先备份。")
        }
        .confirmationDialog("关闭 Husk？", isPresented: $confirmQuit, titleVisibility: .visible) {
            Button("关闭 Husk") {
                // The game that is loaded is closed with Husk, which is the normal way out for it, not a crash; and this
                // one starts by itself when Husk is opened again.
                Router.shared.switchTo(app.id)
                CrashReport.gameEnded()
                HuskLog.flushNow()
                exit(0)
            }
        } message: {
            Text("\(blockedBy ?? "另一个游戏") 仍处于加载状态，每次启动 Husk 只能运行一个游戏。Husk 将关闭；重新打开后会自动启动 \(app.label)。")
        }
        .confirmationDialog("移除 \(app.label)？", isPresented: $confirmRemove, titleVisibility: .visible) {
            Button("移除", role: .destructive) {
                dismiss()
                store.remove(app)
            }
        } message: {
            Text("游戏及其在 Husk 中保存的全部数据都将被删除。")
        }
        .id(jit.attachGeneration)
        .onAppear {
            // Kept until JIT is on: this page is rebuilt when it attaches (.id above), and plays then.
            guard Router.shared.autoPlay == app.id, Launcher.jitOn, blockedBy == nil else { return }
            Router.shared.autoPlay = nil
            play()
        }
    }

    /// Play: with Geode on, whatever it is missing (a newer release, its resources) is fetched first.
    private func play() {
        guard TLAppSettings.load(app.id).geode, case .ready = GeodeSupport.shared.current(app) else {
            if TLAppSettings.load(app.id).geode {
                Task { await GeodeSupport.shared.prepare(app); start() }
            } else {
                start()
            }
            return
        }
        start()
    }

    /// After a session that got going: how did it run? Once per game per build, and only when it played (a crash has its own sheet).
    private func askHowItRan() {
        guard var subject = compatSubject, CompatibilityStore.shared.shouldAsk(subject.package),
              GameStatusStore.shared.status(app.id)?.result == .plays else { return }
        subject.afterPlaying = true
        Task { try? await Task.sleep(nanoseconds: 500_000_000); reporting = subject }
    }

    /// Full screen, or in a window over Husk when Multitasking says so for this app.
    private func start() {
        if app.report?.runsOnNativeRuntime == true || app.report?.webKind != nil, AppWindows.opensInWindow(app) {
            AppWindows.shared.open(app)
        } else {
            AppWindows.shared.close(app)
            playing = true
        }
    }

    /// This game's engine is loaded in this run of Husk, so its files may be open: no restoring under it.
    private var loadedNow: Bool {
        guard let loaded = husk_native_loaded_apk().map({ String(cString: $0) }) else { return false }
        return loaded == app.apks.first
    }

    private func backUp() {
        savesBusy = true; savesMessage = nil
        let app = self.app
        Task.detached {
            let result = Result { try SaveBackup.export(app) }
            await MainActor.run {
                savesBusy = false
                switch result {
                case .success(let url): backupFile = url
                case .failure(let e): savesMessage = e.localizedDescription
                }
            }
        }
    }

    private func pickRestore() {
        HuskFilePicker.present(types: [.zip], multiple: false) { urls in
            if let url = urls.first { confirmRestore = url }
        } onFail: { savesMessage = $0 }
    }

    private func restore(_ url: URL) {
        savesBusy = true; savesMessage = nil
        let app = self.app
        Task.detached {
            let result = Result { try SaveBackup.restore(app, from: url) }
            await MainActor.run {
                savesBusy = false
                switch result {
                case .success(let n): savesMessage = "已从 \(url.lastPathComponent) 恢复 \(n) 个文件。"
                case .failure(let e): savesMessage = e.localizedDescription
                }
            }
        }
    }

    /// Geode's Android launcher, added as if it were a game: it is a whole Android app, which Husk does not run. On Husk Geode
    /// is turned on from Geometry Dash's own settings, and this APK only lends it a library.
    private var isGeodeLauncher: Bool { app.packageName == GeodeSupport.launcherPackage }
    private var geometryDash: TLApp? { store.apps.first { $0.packageName == GeodeSupport.gamePackage } }

    @ViewBuilder
    private var primary: some View {
        if isGeodeLauncher {
            if let gd = geometryDash {
                BigButton(title: TLAppSettings.load(gd.id).geode ? "Geometry Dash 已启用 Geode" : "为 Geometry Dash 启用 Geode",
                          systemImage: "puzzlepiece.extension.fill", prominent: !TLAppSettings.load(gd.id).geode) {
                    var s = TLAppSettings.load(gd.id)
                    s.geode = true
                    s.save(gd.id)
                    Task { await GeodeSupport.shared.prepare(gd) }
                }
            } else {
                BigButton(title: "请先添加 Geometry Dash", systemImage: "plus", prominent: false) { }.disabled(true)
            }
        } else if blockedBy != nil {
            BigButton(title: "关闭 Husk 以切换游戏", systemImage: "xmark.circle", prominent: false) { confirmQuit = true }
        } else if !Launcher.jitOn {
            if jit.busy {
                HStack(spacing: 10) { ProgressView(); Text(jit.status ?? "正在开启 JIT…").foregroundStyle(.secondary) }
                    .frame(maxWidth: .infinity).frame(height: 50)
            } else {
                BigButton(title: "开启 JIT 以开始游戏", systemImage: "bolt.fill", action: turnOnJIT)
            }
        } else {
            BigButton(title: runs ? "开始游戏" : "尝试运行", systemImage: "play.fill", prominent: runs) { play() }
        }
    }
}

// MARK: - a PS4 game

/// A PS4 game from Documents/PS4 Games: Play, and what it is. Run by the PS4 layer (libhuskps4.dylib) on Husk's JIT.
struct PS4GamePage: View {
    let game: PS4Game
    @ObservedObject private var jit = JITCoordinator.shared
    @State private var playing = false
    @State private var confirmQuit = false

    private var item: LibraryItem { .ps4(game) }

    /// Another game already loaded in this run of Husk (an Android game's engine, or another PS4 game).
    private var blockedBy: String? {
        if Router.shared.androidStarted { return "Android" }   // QEMU holds the JIT region the PS4 layer needs
        guard let loaded = husk_native_loaded_apk().map({ String(cString: $0) }), loaded != game.libraryID else { return nil }
        if loaded.hasPrefix("ps4:") { return PS4Store.shared.game(String(loaded.dropFirst(4)))?.title ?? "另一个游戏" }
        return TranslationLayerStore.shared.apps.first { $0.apks.first == loaded }?.label ?? "另一个游戏"
    }

    private var subtitle: String {
        if let used = item.usedText { return used }
        return "PlayStation 4" + (game.version.map { " · v\($0)" } ?? "")
    }

    var body: some View {
        List {
            PageHero(item: item, subtitle: subtitle) { primary }
                .heroRow()
            if !PS4Store.available {
                Section {
                    Text(PS4Store.moduleURL == nil
                         ? "此版本的 Husk 未包含 PS4 运行模块。"
                         : "PS4 游戏需要 iOS 17.4 或更高版本。")
                        .foregroundStyle(.secondary)
                }
            }
            Section("关于") {
                LabeledContent("运行引擎", value: "Husk PS4 运行模块")
                LabeledContent("游戏编号", value: game.titleID)
                if let v = game.version { LabeledContent("版本", value: v) }
                LabeledContent("上次游玩", value: game.lastPlayed.map { $0.formatted(date: .abbreviated, time: .shortened) } ?? "从未")
                LabeledContent("文件夹", value: "PS4 Games/" + game.folder.lastPathComponent)
            }
        }
        .listStyle(.insetGrouped)
        .navigationTitle(game.title)
        .navigationBarTitleDisplayMode(.inline)
        .fullScreenCover(isPresented: $playing) { PS4PlayView(game: game) }
        .confirmationDialog("关闭 Husk？", isPresented: $confirmQuit, titleVisibility: .visible) {
            Button("关闭 Husk") {
                Router.shared.switchTo(game.libraryID)
                CrashReport.gameEnded()
                HuskLog.flushNow()
                exit(0)
            }
        } message: {
            Text("\(blockedBy ?? "另一款游戏") 仍处于已加载状态，每次启动 Husk 只能运行一款游戏。Husk 将关闭；再次打开后，\(game.title) 会自动启动。")
        }
        .id(jit.attachGeneration)
        .onAppear {
            guard Router.shared.autoPlay == game.libraryID, Launcher.jitOn, blockedBy == nil, PS4Store.available else { return }
            Router.shared.autoPlay = nil
            playing = true
        }
    }

    @ViewBuilder
    private var primary: some View {
        if !PS4Store.available {
            BigButton(title: "无法在此运行", systemImage: "xmark.octagon", prominent: false) { }.disabled(true)
        } else if blockedBy != nil {
            BigButton(title: "关闭 Husk 以切换游戏", systemImage: "xmark.circle", prominent: false) { confirmQuit = true }
        } else if !Launcher.jitOn {
            if jit.busy {
                HStack(spacing: 10) { ProgressView(); Text(jit.status ?? "正在开启 JIT…").foregroundStyle(.secondary) }
                    .frame(maxWidth: .infinity).frame(height: 50)
            } else {
                BigButton(title: "开启 JIT 以开始游戏", systemImage: "bolt.fill", action: turnOnJIT)
            }
        } else {
            BigButton(title: "开始游戏", systemImage: "play.fill") { playing = true }
        }
    }
}

// MARK: - an app inside Android

/// An app installed in Android: Open (starting Android first when it is not running), what it is, and its files.
struct AndroidAppPage: View {
    let app: AndroidHost.Package
    @ObservedObject private var showcase = ShowcaseStore.shared

    @ObservedObject private var host = AndroidHost.shared
    @ObservedObject private var guest = GuestImage.shared
    @ObservedObject private var runner = QemuRunner.shared
    @ObservedObject private var router = Router.shared
    @Environment(\.dismiss) private var dismiss
    @State private var confirmUninstall = false

    private var live: AndroidHost.Package { host.packages.first { $0.name == app.name } ?? app }
    private var canOpen: Bool { host.isReady && host.busy == nil }
    private var item: LibraryItem { .app(live) }

    var body: some View {
        List {
            Section {
                PageHero(item: item, subtitle: item.usedText ?? "在 Android 系统中运行") { primary }
            } footer: {
                if router.pendingAndroidLaunch == app.name, !host.isReady {
                    Text("Android 启动后会自动打开 \(live.label)。")
                }
            }
            .heroRow()

            if !ShowcaseStore.shared.pictures(for: app.name).isEmpty {
                Section("截图") {
                    ShowcaseGallery(package: app.name)
                        .listRowInsets(EdgeInsets(top: 10, leading: 12, bottom: 10, trailing: 12))
                }
            }

            Section("关于") {
                LabeledContent("版本", value: live.version ?? "—")
                LabeledContent("大小", value: live.sizeBytes.map { ByteCountFormatter.string(fromByteCount: $0, countStyle: .file) } ?? "—")
                LabeledContent("包名", value: live.name)
                if let b = live.bitness { LabeledContent("架构", value: b) }
            }

            Section {
                Button { router.openFiles(at: "/sdcard/Android/data/\(app.name)") } label: {
                    Label("在文件中打开", systemImage: "folder")
                }
                .disabled(!canOpen)
                Button { appInfo() } label: {
                    Label("Android 应用信息", systemImage: "info.circle")
                }
                .disabled(!canOpen)
                Button { UIPasteboard.general.string = app.name } label: {
                    Label("复制包名", systemImage: "doc.on.doc")
                }
            }

            Section {
                Button(role: .destructive) { confirmUninstall = true } label: {
                    Label("卸载", systemImage: "trash")
                }
                .disabled(!canOpen)
            } footer: {
                if !canOpen { Text("请先启动 Android，再卸载或查看应用内容。") }
            }
        }
        .listStyle(.insetGrouped)
        .navigationTitle(live.label)
        .navigationBarTitleDisplayMode(.inline)
        .confirmationDialog("卸载 \(live.label)？", isPresented: $confirmUninstall, titleVisibility: .visible) {
            Button("卸载", role: .destructive) {
                host.uninstall(app.name)
                dismiss()
            }
        } message: {
            Text("应用数据也会一并删除。")
        }
    }

    @ViewBuilder
    private var primary: some View {
        if canOpen {
            BigButton(title: "打开", systemImage: "play.fill") {
                host.launch(app.name) { router.openGuest() }
            }
        } else if router.androidStarted {
            VStack(spacing: 10) {
                BigButton(title: "正在启动 Android…", systemImage: "hourglass", prominent: false) { }
                    .disabled(true)
                if runner.bootProgress > 0 { ProgressView(value: Double(runner.bootProgress), total: 100) }
            }
        } else if guest.state == .ready {
            BigButton(title: Launcher.jitOn ? "启动 Android 并打开" : "开启 JIT", systemImage: Launcher.jitOn ? "play.fill" : "bolt.fill") {
                if Launcher.jitOn { router.pendingAndroidLaunch = app.name }
                router.startAndroid()
            }
        } else {
            HuskLink(value: LibraryRoute.androidSystem) {
                Label("请先下载 Android", systemImage: "arrow.down.circle")
                    .font(.system(.headline, design: .rounded))
                    .frame(maxWidth: .infinity).frame(height: 50)
                    .foregroundStyle(.white)
                    .background(Color.accentColor, in: RoundedRectangle(cornerRadius: 15, style: .continuous))
            }
            .buttonStyle(CardButtonStyle())
        }
    }

    /// Android's own page for the app: permissions, storage, force stop.
    private func appInfo() {
        let pkg = app.name
        DispatchQueue.global(qos: .userInitiated).async {
            _ = try? GuestBridge.shared.shell("am start -a android.settings.APPLICATION_DETAILS_SETTINGS -d package:\(pkg)", timeout: 30)
        }
        router.openGuest()
    }
}

// MARK: - Android itself

/// The whole Android system, as an app of its own: get it, start it, open it, and its settings.
struct AndroidSystemPage: View {
    @ObservedObject private var guest = GuestImage.shared
    @ObservedObject private var runner = QemuRunner.shared
    @ObservedObject private var host = AndroidHost.shared
    @ObservedObject private var router = Router.shared
    @ObservedObject private var jit = JITCoordinator.shared

    private var running: Bool { router.androidStarted }

    var body: some View {
        List {
            Section {
                PageHero(item: .android, subtitle: status) {
                    VStack(spacing: 10) {
                        primary
                        if let progress { ProgressView(value: progress) }
                    }
                }
            } footer: {
                if let note { Text(note) }
            }
            .heroRow()

            if host.isReady {
                Section {
                    Button { router.openFiles(at: FilesTab.root) } label: { Label("文件", systemImage: "folder") }
                    Button { runner.saveState(reason: "asked from the Android page") } label: {
                        Label(runner.isSavingState ? "正在保存…" : "立即保存 Android 状态", systemImage: "externaldrive.badge.checkmark")
                    }
                    .disabled(runner.isSavingState)
                } footer: {
                    Text("保存 Android 的完整运行状态，下次启动时即可直接恢复。")
                }
            }

            Section("设置") {
                NavigationLink { PerformanceSettings() } label: { Label("性能", systemImage: "speedometer") }
                NavigationLink { InputSettings() } label: { Label("输入", systemImage: "hand.tap") }
                NavigationLink { NetworkSettings() } label: { Label("网络", systemImage: "globe") }
                NavigationLink { SavedMachineSettings() } label: { Label("系统快照", systemImage: "externaldrive") }
            }

            Section {
                Text("Android 通过完整模拟运行，因此比转译层中的游戏更慢。适合运行 Husk 无法直接支持的应用，"
                   + "例如 Termux 或应用商店。")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
        }
        .listStyle(.insetGrouped)
        .navigationTitle("Android")
        .navigationBarTitleDisplayMode(.inline)
        .animation(.easeInOut(duration: 0.2), value: status)
    }

    private var status: String {
        if running {
            if host.isReady { return "运行中" }
            return runner.setupMessage ?? (runner.bootProgress > 0 ? "正在启动 · \(runner.bootProgress)%" : "正在启动…")
        }
        switch guest.state {
        case .missing: return "尚未下载"
        case .downloading(_, let received, let total):
            return "正在下载 · \(bytes(received)) / \(total > 0 ? bytes(total) : "…")"
        case .installing: return "正在安装…"
        case .failed: return "出现错误"
        case .ready: return Launcher.jitOn ? "可以启动" : "需要开启 JIT 才能启动"
        }
    }

    private var progress: Double? {
        if running, !host.isReady, runner.bootProgress > 0 { return Double(runner.bootProgress) / 100 }
        if !running, case .downloading(let p, _, _) = guest.state { return p }
        return nil
    }

    private var note: String? {
        guard !running else { return nil }
        switch guest.state {
        case .missing: return "Husk 的 Android 运行环境约为 760 MB。安装完成后会继续下载 Android 系统。"
        case .failed(let message): return message
        default: return nil
        }
    }

    @ViewBuilder
    private var primary: some View {
        if running {
            if host.isReady {
                BigButton(title: "打开 Android", systemImage: "play.fill") { open() }
            } else {
                BigButton(title: "查看启动画面", systemImage: "eye", prominent: false) { open() }
            }
        } else {
            switch guest.state {
            case .missing:
                BigButton(title: "下载 Android", systemImage: "arrow.down.circle") {
                    // Claim the JIT region before the download: it takes a while, and StikDebug lets go by the end of it.
                    JITBootstrap.prewarm()
                    guest.download()
                }
            case .downloading:
                BigButton(title: "取消下载", systemImage: "xmark", prominent: false) { guest.cancel() }
            case .installing:
                ProgressView().frame(height: 50)
            case .failed:
                BigButton(title: "重试", systemImage: "arrow.clockwise") { JITBootstrap.prewarm(); guest.download() }
            case .ready:
                if jit.busy {
                    HStack(spacing: 10) { ProgressView(); Text(jit.status ?? "正在开启 JIT…").foregroundStyle(.secondary) }
                        .frame(maxWidth: .infinity).frame(height: 50)
                } else {
                    BigButton(title: Launcher.jitOn ? "启动 Android" : "开启 JIT", systemImage: Launcher.jitOn ? "power" : "bolt.fill") {
                        router.startAndroid()
                    }
                }
            }
        }
    }

    private func open() {
        UserDefaults.standard.set(Date(), forKey: "husk.android.lastOpened")
        router.openGuest()
    }

    private func bytes(_ n: Int64) -> String { ByteCountFormatter.string(fromByteCount: n, countStyle: .file) }
}

/// A URL that a sheet can be presented for.
struct IdentifiedURL: Identifiable {
    let url: URL
    var id: String { url.path }
}
