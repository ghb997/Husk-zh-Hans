// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

/// Settings, as the iOS Settings app lays them out: a card for the app up top, then groups of rows that each
/// push a page of their own. What belongs to the app, what belongs to the emulator, and what the thing is.
struct SettingsTab: View {
    @AppStorage(AppWindows.defaultKey) private var multitask = false
    @AppStorage(CompatibilityStore.askKey) private var askCompat = true

    var body: some View {
        NavigationStack {
            List {
                Section {
                    NavigationLink { AboutSettings() } label: { appCard }
                }

                Section {
                    row(JITSettings(), "bolt.fill", .yellow, "JIT 与侧载", "内置 StikJIT，推荐使用")
                } header: {
                    Text("JIT")
                } footer: {
                    Text("Android 和转译层游戏都需要 JIT。Husk 内置的 StikJIT 无需电脑即可开启 JIT。")
                }

                Section("通用") {
                    row(DiscoverView(), "sparkle.magnifyingglass", .mint, "发现", "从 F-Droid 等软件源查找应用")
                    row(LibrarySettings(), "square.grid.2x2.fill", .blue, "应用库", "应用和图标")
                    row(PerformanceSettings(), "speedometer", .orange, "性能", "渲染器、声音")
                    row(AppearanceSettings(), "paintbrush.fill", .pink, "外观", "深浅模式、强调色、应用图标")
                }

                Section {
                    Toggle(isOn: $multitask) {
                        Label {
                            VStack(alignment: .leading, spacing: 1) {
                                Text("在窗口中打开应用")
                                Text("应用运行时继续使用 Husk").font(.caption).foregroundStyle(.secondary).lineLimit(1)
                            }
                        } icon: {
                            RoundedRectangle(cornerRadius: 7, style: .continuous)
                                .fill(Color.purple.gradient)
                                .frame(width: 30, height: 30)
                                .overlay {
                                    Image(systemName: "macwindow.on.rectangle")
                                        .font(.system(size: 14, weight: .medium))
                                        .foregroundStyle(.white)
                                }
                        }
                    }
                } header: {
                    Text("多任务")
                } footer: {
                    Text("应用和游戏将在可移动、缩放及最小化到程序坞的窗口中打开，类似 LiveContainer 的多任务功能。"
                       + "也可在每个应用的设置中单独选择打开方式。")
                }

                Section {
                    Toggle(isOn: $askCompat) {
                        Label {
                            VStack(alignment: .leading, spacing: 1) {
                                Text("询问游戏运行情况")
                                Text("游戏结束后询问，每次更新后每个游戏仅询问一次").font(.caption).foregroundStyle(.secondary).lineLimit(1)
                            }
                        } icon: {
                            RoundedRectangle(cornerRadius: 7, style: .continuous)
                                .fill(Color.green.gradient)
                                .frame(width: 30, height: 30)
                                .overlay {
                                    Image(systemName: "list.bullet.clipboard")
                                        .font(.system(size: 14, weight: .medium))
                                        .foregroundStyle(.white)
                                }
                        }
                    }
                    Link(destination: CompatibilityStore.site) {
                        Label("兼容性列表", systemImage: "safari")
                    }
                } header: {
                    Text("兼容性")
                } footer: {
                    Text("游戏页面和商店会显示 antiweb.lol 上汇总的运行情况。你的反馈也会提交到该网站，"
                       + "并附带设备型号、iOS 版本、Husk 构建版本和 JIT 开启方式。")
                }

                Section("模拟器") {
                    row(InputSettings(), "hand.tap.fill", .indigo, "输入", "屏幕、触控、键盘")
                    row(NetworkSettings(), "globe", .green, "网络", "互联网与状态保存")
                    row(SavedMachineSettings(), "externaldrive.fill", .teal, "系统快照", "快照与自动保存")
                }
            }
            .listStyle(.insetGrouped)
            .navigationTitle("设置")
        }
    }

    private var appCard: some View {
        HStack(spacing: 14) {
            HuskMark(size: 56)
            VStack(alignment: .leading, spacing: 2) {
                Text("Husk").font(.title3.weight(.semibold))
                Text("版本 \(Bundle.main.version) · \(Bundle.main.commit)")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
        }
        .padding(.vertical, 6)
    }

    private func row<D: View>(_ destination: D, _ icon: String, _ tint: Color,
                              _ title: String, _ subtitle: String) -> some View {
        NavigationLink { destination } label: {
            Label {
                VStack(alignment: .leading, spacing: 1) {
                    Text(title)
                    Text(subtitle).font(.caption).foregroundStyle(.secondary).lineLimit(1)
                }
            } icon: {
                RoundedRectangle(cornerRadius: 7, style: .continuous)
                    .fill(tint.gradient)
                    .frame(width: 30, height: 30)
                    .overlay {
                        Image(systemName: icon)
                            .font(.system(size: 15, weight: .medium))
                            .foregroundStyle(.white)
                    }
            }
        }
    }
}

/// A Form, as the system draws one.
private struct HuskForm: ViewModifier {
    func body(content: Content) -> some View {
        content.navigationBarTitleDisplayMode(.inline)
    }
}

extension View {
    func huskForm() -> some View { modifier(HuskForm()) }
}

// MARK: - Library

struct LibrarySettings: View {
    @ObservedObject private var host = AndroidHost.shared
    @State private var working = false

    var body: some View {
        Form {
            Section {
                DetailRow(label: "应用数量", value: "\(host.packages.count)", mono: false)
                DetailRow(label: "已有图标",
                          value: "\(host.packages.filter { $0.iconPath != nil }.count)",
                          mono: false)
            } footer: {
                Text("应用列表保存在本地，因此无需等待 Android"
                   + "启动完毕即可显示。")
            }

            Section {
                Button {
                    working = true
                    Task { await host.refreshPackages(); working = false }
                } label: {
                    Label(working ? "正在刷新…" : "从 Android 刷新",
                          systemImage: "arrow.clockwise")
                }
                .disabled(working || !host.isReady)

                Button {
                    AndroidHost.forgetIcons()
                    working = true
                    Task { await host.refreshPackages(); working = false }
                } label: {
                    Label("重新获取图标", systemImage: "photo.on.rectangle")
                }
                .disabled(working || !host.isReady)
            } footer: {
                Text("名称和图标来自 Android 自带的启动器，"
                   + "与其显示的版本一致。重新获取会删除 Husk 缓存的图标，"
                   + "并再次向 Android 请求。")
            }
        }
        .huskForm()
        .navigationTitle("应用库")
    }
}

// MARK: - Performance

struct PerformanceSettings: View {
    @ObservedObject private var runner = QemuRunner.shared
    @State private var gpuMode =
        UserDefaults.standard.object(forKey: "husk.gpuMode") as? Bool ?? true
    @State private var sound = UserDefaults.standard.bool(forKey: "husk.sound")
    @State private var soundDevice =
        UserDefaults.standard.object(forKey: "husk.soundDevice") as? Bool ?? true

    var body: some View {
        Form {
            Section {
                Picker("渲染器", selection: $gpuMode) {
                    Text("GPU").tag(true)
                    Text("CPU").tag(false)
                }
                .pickerStyle(.segmented)
                .onChange(of: gpuMode) { v in
                    UserDefaults.standard.set(v, forKey: "husk.gpuMode")
                    HuskLog.log("ui", v ? "GPU renderer selected" : "CPU renderer selected")
                }
            } header: {
                Text("渲染器")
            } footer: {
                Text(gpuMode
                     ? "Android 通过 Metal 使用实际 GPU 绘制画面，帧率约为软件渲染的四倍。"
                     + "这是默认设置。"
                     : "每个像素都由模拟的 CPU 绘制，速度明显更慢。"
                     + "仅建议在 GPU 渲染异常时使用。")
            }

            Section {
                DetailRow(label: "帧率",
                          value: runner.fps > 0
                                 ? String(format: "%.0f 帧/秒", runner.fps) : "—")
                DetailRow(label: "客户系统画面",
                          value: "\(QemuRunner.lastGuestRes.w)×\(QemuRunner.lastGuestRes.h)")
            } header: {
                Text("当前状态")
            }

            Section {
                Toggle("声音", isOn: $sound)
                    .onChange(of: sound) { v in
                        UserDefaults.standard.set(v, forKey: "husk.sound")
                        HuskLog.log("ui", v ? "sound on" : "sound off")
                    }
                if sound {
                    Toggle("连接音频设备", isOn: $soundDevice)
                        .onChange(of: soundDevice) { v in
                            UserDefaults.standard.set(v, forKey: "husk.soundDevice")
                        }
                }
            } header: {
                Text("声音")
            } footer: {
                Text("添加虚拟音频设备。连接后无法保存 Android 状态，"
                   + "因为 QEMU 不支持为带音频设备的虚拟机创建快照，"
                   + "所以每次启动都需要完整冷启动。"
                   + "无论开启还是关闭此项，都需要冷启动一次。")
            }
        }
        .huskForm()
        .navigationTitle("性能")
    }
}

// MARK: - Input

struct InputSettings: View {
    @State private var landscapeGuest =
        UserDefaults.standard.bool(forKey: "husk.landscapeGuest")
    @State private var customRes = UserDefaults.standard.bool(forKey: "husk.customRes")
    @State private var widthText = InputSettings.stored("husk.resWidth", 720)
    @State private var heightText = InputSettings.stored("husk.resHeight", 1280)

    /// Sizes worth offering without typing. Deliberately short: these are the
    /// shapes a phone guest is actually run at, not a catalogue of every panel
    /// ever made.
    private static let presets: [(name: String, w: Int, h: Int)] = [
        ("低分辨率 — 360 × 800", 360, 800),
        ("高清 — 720 × 1280", 720, 1280),
        ("全高清 — 1080 × 1920", 1080, 1920),
        ("横屏高清 — 1280 × 720", 1280, 720),
        ("平板 — 1280 × 800", 1280, 800),
    ]

    private static func stored(_ key: String, _ fallback: Int) -> String {
        let v = UserDefaults.standard.integer(forKey: key)
        return String(v > 0 ? v : fallback)
    }

    /// What the typed numbers actually come to, or nothing if they are not a
    /// size the guest can be given.
    private var effective: (w: Int, h: Int)? {
        QemuRunner.validResolution(w: Int(widthText) ?? 0, h: Int(heightText) ?? 0)
    }

    var body: some View {
        Form {
            Section {
                Picker("屏幕", selection: $landscapeGuest) {
                    Text("竖屏").tag(false)
                    Text("横屏").tag(true)
                }
                .pickerStyle(.segmented)
                .disabled(customRes)
                .onChange(of: landscapeGuest) { v in
                    UserDefaults.standard.set(v, forKey: "husk.landscapeGuest")
                    HuskLog.log("ui", v ? "guest panel will be landscape"
                                        : "guest panel will be portrait")
                }
            } header: {
                Text("屏幕")
            } footer: {
                Text(customRes
                     ? "自定义分辨率会决定屏幕形状，开启时此选项不生效。"
                     + "如需横屏，请将宽度设为大于高度。"
                     : "Android 运行后无法改变屏幕形状。"
                     + "横屏游戏在竖屏画面上会被压缩成一条带黑边的区域，"
                     + "看起来很小。只有以横屏创建屏幕才能铺满画面，"
                     + "但竖屏应用则会出现黑边。"
                     + "修改后需要冷启动一次。")
            }

            Section {
                Toggle("自定义分辨率", isOn: $customRes)
                    .onChange(of: customRes) { v in
                        UserDefaults.standard.set(v, forKey: "husk.customRes")
                        store()
                        HuskLog.log("ui", v ? "custom resolution on: "
                                            + "\(widthText)x\(heightText)"
                                            : "custom resolution off")
                    }

                if customRes {
                    Picker("预设", selection: Binding(
                        get: { presetIndex },
                        set: { i in
                            guard i >= 0, i < Self.presets.count else { return }
                            widthText = String(Self.presets[i].w)
                            heightText = String(Self.presets[i].h)
                            store()
                        })) {
                        ForEach(0..<Self.presets.count, id: \.self) { i in
                            Text(Self.presets[i].name).tag(i)
                        }
                        Text("自定义").tag(-1)
                    }

                    HStack {
                        Text("宽度")
                        Spacer()
                        TextField("720", text: $widthText)
                            .keyboardType(.numberPad)
                            .multilineTextAlignment(.trailing)
                            .font(.technical())
                            .frame(width: 90)
                            .onChange(of: widthText) { _ in store() }
                    }
                    HStack {
                        Text("高度")
                        Spacer()
                        TextField("1280", text: $heightText)
                            .keyboardType(.numberPad)
                            .multilineTextAlignment(.trailing)
                            .font(.technical())
                            .frame(width: 90)
                            .onChange(of: heightText) { _ in store() }
                    }

                    if let size = effective {
                        DetailRow(label: "Android 将使用",
                                  value: "\(size.w) × \(size.h)")
                    } else {
                        Text("宽度和高度都必须在 240 到 2560 之间。")
                            .font(.caption).foregroundStyle(.orange)
                    }
                }

                DetailRow(label: "当前分辨率", value: running)
            } header: {
                Text("分辨率")
            } footer: {
                Text("屏幕在虚拟机启动时创建，因此修改后需要冷启动一次，"
                   + "下一次保存也会替换原分辨率的系统快照。"
                   + "改回原来的尺寸同样需要冷启动。尺寸会取整为"
                   + "8 的倍数。分辨率越高，模拟绘制的像素越多，速度越慢。"
                   + "Android 的显示密度不会随分辨率变化，"
                   + "因此提高分辨率会显示更多内容，而不是放大内容。")
            }

            Section {
                Text("触控始终启用。键盘和旋转按钮位于"
                   + "客户系统屏幕底部的控制条中。"
                   + "手柄和鼠标尚未接入模拟系统。")
                    .font(.footnote).foregroundStyle(.secondary)
            } header: {
                Text("控制")
            }
        }
        .huskForm()
        .navigationTitle("输入")
    }

    /// The panel the guest actually has, which only means anything while there
    /// is a guest: the stored value is last launch's until one starts.
    private var running: String {
        guard QemuRunner.shared.isRunning else { return "尚未启动" }
        return "\(QemuRunner.lastGuestRes.w) × \(QemuRunner.lastGuestRes.h)"
    }

    /// Which preset the typed numbers are, if any.
    private var presetIndex: Int {
        guard let size = effective else { return -1 }
        return Self.presets.firstIndex { $0.w == size.w && $0.h == size.h } ?? -1
    }

    private func store() {
        UserDefaults.standard.set(Int(widthText) ?? 0, forKey: "husk.resWidth")
        UserDefaults.standard.set(Int(heightText) ?? 0, forKey: "husk.resHeight")
    }
}

// MARK: - Network

struct NetworkSettings: View {
    @State private var keepNetwork =
        UserDefaults.standard.object(forKey: "husk.keepNetwork") as? Bool ?? true

    var body: some View {
        Form {
            Section {
                Toggle("保存状态时保留网络", isOn: $keepNetwork)
                    .onChange(of: keepNetwork) { v in
                        UserDefaults.standard.set(v, forKey: "husk.keepNetwork")
                    }
            } footer: {
                Text(keepNetwork
                     ? "保存时关闭应用，但保持 Android 框架运行，"
                     + "使恢复后网络仍能正常工作。"
                     : "保存时也停止 Android 框架，清理全部 GPU 资源，"
                     + "运行更稳定，但网络可能需要"
                     + "冷启动后才能恢复。")
            }

            Section {
                Text("Android 通过 QEMU 内部网络上的"
                   + "虚拟以太网卡访问互联网。手机所在网络中的设备"
                   + "与客户系统彼此不可见。")
                    .font(.footnote).foregroundStyle(.secondary)
            } header: {
                Text("连接方式")
            }
        }
        .huskForm()
        .navigationTitle("网络")
    }
}

// MARK: - JIT and sideloading

struct JITSettings: View {
    @ObservedObject private var runner = QemuRunner.shared
    @ObservedObject private var jit = JITCoordinator.shared
    @State private var autoStart = Onboarding.autoStart
    @AppStorage(JITCoordinator.autoEnableKey) private var autoJIT = false
    @State private var keepAttached = JITBootstrap.keepDebuggerAttached

    private var pairingLabel: String {
        switch jit.pairingSource {
        case .onDevice: return "已在此设备上配对"
        case .imported: return "已导入配对文件"
        case nil: return "尚未设置"
        }
    }

    var body: some View {
        Form {
            Section {
                JITCard()
            }
            .listRowInsets(EdgeInsets())
            .listRowBackground(Color.clear)

            Section {
                Picker("方式", selection: $jit.method) {
                    ForEach(JITMethod.allCases) { Text($0.title).tag($0) }
                }
                DetailRow(label: "StikDebug",
                          value: JITBootstrap.isStikDebugInstalled ? "已安装" : "未检测到", mono: false)
                DetailRow(label: "TrollStore",
                          value: JITBootstrap.isTrollStoreInstalled ? "已安装" : "未检测到", mono: false)
                DetailRow(label: "通过 TrollStore 安装",
                          value: JITBootstrap.isInstalledWithTrollStore ? "是" : "否", mono: false)
                DetailRow(label: "越狱状态",
                          value: JITBootstrap.debuggedAtLaunch ? "已允许应用使用 JIT"
                               : JITBootstrap.isJailbroken ? "已检测到" : "未检测到", mono: false)
                DetailRow(label: "内置配对", value: pairingLabel, mono: false)
                Button {
                    jit.showSetup = true
                } label: {
                    Label("设置 JIT", systemImage: "wand.and.stars")
                }
            } header: {
                Text("其他 JIT 开启方式")
            } footer: {
                Text(jit.method == .automatic
                     ? jit.automaticDescription + " 内置 StikJIT 需要 iOS 26、LocalDevVPN 和"
                       + "配对文件；在 iOS 27 上，Husk 可以自行生成配对文件。"
                     : HuskBuiltInJIT.unavailableReason ?? "内置 StikJIT 需要 LocalDevVPN 和配对文件；"
                       + "在 iOS 27 上，Husk 可以自行生成。")
            }

            Section {
                DetailRow(label: "调试器",
                          value: JITBootstrap.isDebuggerAttached ? "已附加" : "未附加",
                          mono: false)
                DetailRow(label: "可执行内存",
                          value: JITBootstrap.isLive ? "已获得" : "尚未获取", mono: false)
                // The two routes, named separately. Either one is enough, and
                // when someone reports "JIT does not work" these two rows are
                // the whole diagnosis.
                DetailRow(label: "陷阱请求处理器",
                          value: husk_ios_jit_self_route() != nil ? "无需使用"
                               : JITBootstrap.prewarmed ? "有响应" : "无响应",
                          mono: false)
                if let route = husk_ios_jit_self_route() {
                    DetailRow(label: "Husk 创建的内存",
                              value: String(cString: route) == "MAP_JIT" ? "MAP_JIT（TrollStore 权限）" : "普通内存（调试或越狱）",
                              mono: false)
                }
                // Cached answer only: running the probe from a view body
                // could freeze the app (see JITBootstrap.mapJITWorks).
                DetailRow(label: "MAP_JIT",
                          value: JITBootstrap.deviceEnforcesTXM ? "未使用（TXM）"
                               : JITBootstrap.mapJITResult.map { $0 ? "可执行" : "被拒绝" }
                                 ?? "尚未测试",
                          mono: false)
                DetailRow(label: "设置后的调试器状态",
                          value: JITBootstrap.detached ? "已分离" : "已附加",
                          mono: false)
                if let why = JITBootstrap.lastFailure {
                    Text(why).font(.caption).foregroundStyle(.orange)
                }
                if !JITBootstrap.ready {
                    Button {
                        jit.enable()
                    } label: {
                        Label("通过 \(jit.resolvedMethod.title) 开启 JIT", systemImage: "bolt.fill")
                    }
                    .disabled(jit.busy)
                    Button {
                        _ = JITBootstrap.requestTrollStoreAttach()
                    } label: {
                        Label("通过 TrollStore 开启 JIT", systemImage: "sparkles")
                    }
                }
            } header: {
                Text("JIT")
            } footer: {
                Text("Husk 需要可以先写入再执行的内存，"
                   + "在 iOS 上需要附加调试器才能获得。可通过"
                   + "处理陷阱请求的调试器，或内核允许被调试进程使用的"
                   + "MAP_JIT 映射来实现。任一种方式可用即可；"
                   + "具体取决于设备和 iOS 版本，"
                   + "因此 Husk 会分别测试两种方式。")
            }

            Section {
                Toggle("启动时开启 JIT", isOn: $autoJIT)
                    .disabled(!HuskBuiltInJIT.isAvailable)
            } footer: {
                Text(HuskBuiltInJIT.isAvailable
                     ? "每次打开 Husk 时，如果 JIT 尚未开启，就会自动请求内置 StikJIT 开启，"
                       + "无需手动操作即可开始游戏。请先完成 StikJIT 配对设置。"
                     : "需要内置 StikJIT，适用于 iOS 26 及更高版本。")
            }

            Section {
                Toggle("启动时自动运行 Android", isOn: $autoStart)
                    .onChange(of: autoStart) { v in
                        UserDefaults.standard.set(v, forKey: "husk.autoStart")
                    }
            } footer: {
                Text("JIT 可用时，在打开 Husk 后立即启动 Android 系统。")
            }

            Section {
                Toggle("保持调试器附加", isOn: $keepAttached)
                    .onChange(of: keepAttached) { v in JITBootstrap.keepDebuggerAttached = v }
            } footer: {
                Text("默认关闭。Husk 获得 JIT 内存区域后会立即分离 StikDebug，"
                   + "因为被 iOS 挂起的调试器"
                   + "会在下次被调用时导致整个应用停止运行。"
                   + "仅在需要收集 StikDebug 日志时开启。")
            }

            Section {
                Text("可以通过应用库中的 + 按钮或“文件”页面安装 APK。"
                   + "拆分安装包（基础 APK 及其配置包）必须一起选择，"
                   + "只安装基础 APK 会因缺少本地库"
                   + "而失败。")
                    .font(.footnote).foregroundStyle(.secondary)
            } header: {
                Text("侧载")
            }
        }
        .huskForm()
        .navigationTitle("JIT 与侧载")
    }
}

// MARK: - Saved machine

struct SavedMachineSettings: View {
    @ObservedObject private var runner = QemuRunner.shared
    @State private var autoSave =
        UserDefaults.standard.object(forKey: "husk.autoSave") as? Bool ?? true
    @State private var useSnapshot =
        UserDefaults.standard.object(forKey: "husk.downloadSnapshot") as? Bool ?? true
    @State private var askWhichToDelete = false
    @State private var deleteResult: String?
    @AppStorage(GuestImage.askUpdatesKey) private var askUpdates = true

    var body: some View {
        Form {
            Section {
                Toggle("自动保存", isOn: $autoSave)
                    .onChange(of: autoSave) { v in
                        UserDefaults.standard.set(v, forKey: "husk.autoSave")
                        HuskLog.log("ui", v ? "automatic saving on" : "automatic saving off")
                    }
                Button {
                    QemuRunner.shared.saveState(reason: "asked from settings")
                } label: {
                    Label(runner.isSavingState ? "正在保存…" : "立即保存",
                          systemImage: "externaldrive.badge.checkmark")
                }
                .disabled(runner.isSavingState)
            } footer: {
                Text("Husk 可直接恢复系统快照，省去完整启动过程，"
                   + "通常只需数秒。写入快照期间画面会暂时停住。"
                   + "关闭后不会自动保存状态，"
                   + "包括安装应用之后。")
            }

            Section {
                Button(role: .destructive) { askWhichToDelete = true } label: {
                    Label("删除系统快照", systemImage: "trash")
                }
                .disabled(!QemuRunner.shared.hasSnapshot)
                if let deleteResult {
                    Text(deleteResult).font(.caption).foregroundStyle(.secondary)
                }
            } footer: {
                Text(QemuRunner.shared.hasSnapshot
                     ? "当前已保存："
                     + ((QemuRunner.shared.snapshotDisplay ?? "sw").contains("gl")
                        ? "GPU" : "软件渲染") + "。"
                     : "没有可用快照，Android 将完整冷启动。")
            }

            Section {
                Toggle("下载已启动的系统快照", isOn: $useSnapshot)
                    .onChange(of: useSnapshot) { v in
                        UserDefaults.standard.set(v, forKey: "husk.downloadSnapshot")
                    }
            } footer: {
                Text("首次下载将增加约 2 GB。这份快照通过"
                   + "软件渲染器创建，不适用于 GPU 模式。"
                   + "GPU 模式会先冷启动一次，再保存自己的快照。")
            }

            Section {
                Toggle("提示 Android 更新", isOn: $askUpdates)
            } footer: {
                Text("有新的 Android 镜像或快照发布时，Husk 会询问是否下载。"
                   + "下载过 Android 后才会显示此提示。")
            }
        }
        .huskForm()
        .navigationTitle("系统快照")
        .confirmationDialog("删除哪个系统快照？", isPresented: $askWhichToDelete,
                            titleVisibility: .visible) {
            Button("GPU 模式快照", role: .destructive) { forget("gl", "GPU") }
            Button("软件渲染快照", role: .destructive) { forget("sw", "软件渲染") }
            Button("取消", role: .cancel) { }
        } message: {
            Text("Android 将冷启动一次，然后保存新的快照。")
        }
    }

    private func forget(_ mode: String, _ name: String) {
        if QemuRunner.shared.forgetSnapshot(mode: mode) {
            deleteResult = "已删除 \(name) 模式的快照。下次启动将完整冷启动。"
        } else {
            deleteResult = "没有保存 \(name) 模式的快照，未删除任何内容。"
        }
    }
}

// MARK: - Appearance

struct AppearanceSettings: View {
    @State private var appIcon = HuskAppIcon.current
    @AppStorage(Theme.Appearance.key) private var appearance = Theme.Appearance.system
    @AppStorage(Holiday.key) private var holiday = true
    @ObservedObject private var theme = AppTheme.shared
    @Environment(\.colorScheme) private var scheme
    @State private var custom: Color = AppTheme.shared.accentColor

    private let iconColumns = [GridItem(.adaptive(minimum: 92), spacing: 14)]

    var body: some View {
        Form {
            Section {
                Picker("外观", selection: $appearance) {
                    ForEach(Theme.Appearance.allCases) { Text($0.title).tag($0) }
                }
                .pickerStyle(.segmented)
                .onChange(of: appearance) { v in
                    HuskLog.log("ui", "appearance: \(v.rawValue)")
                }
                Toggle("节日装饰", isOn: $holiday)
            } header: {
                Text("主题")
            } footer: {
                Text("“跟随系统”会使用手机当前的浅色或深色模式。"
                   + "客户系统画面不受此设置影响，它显示的是另一套系统的画面。")
            }

            Section {
                LazyVGrid(columns: Array(repeating: GridItem(.flexible()), count: 5), spacing: 14) {
                    ForEach(AppTheme.presets) { preset in
                        Button {
                            theme.accentColor = preset.color
                            custom = preset.color
                        } label: {
                            swatch(preset.color, selected: preset.color.themeHex == theme.accentColor.themeHex)
                        }
                        .buttonStyle(.plain)
                        .accessibilityLabel(preset.name)
                    }
                }
                .padding(.vertical, 6)

                ColorPicker("自定义颜色", selection: $custom, supportsOpacity: false)
                    .onChange(of: custom) { theme.accentColor = $0 }
            } header: {
                Text("强调色")
            } footer: {
                Text("设置整个应用中按钮、开关和选中状态的颜色。")
            }

            Section {
                LazyVGrid(columns: iconColumns, spacing: 14) {
                    ForEach(HuskAppIcon.allCases) { icon in
                        Button {
                            appIcon = icon
                            HuskAppIcon.apply(icon)
                        } label: {
                            VStack(spacing: 8) {
                                if let art = icon.preview(dark: scheme == .dark) {
                                    Image(uiImage: art)
                                        .resizable().scaledToFit()
                                        .frame(width: 60, height: 60)
                                        .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
                                        .overlay(RoundedRectangle(cornerRadius: 14, style: .continuous)
                                            .strokeBorder(appIcon == icon ? Color.accentColor : .clear, lineWidth: 3))
                                }
                                Text(icon.title)
                                    .font(.caption)
                                    .foregroundStyle(appIcon == icon ? Color.accentColor : .primary)
                                    .lineLimit(1)
                            }
                            .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.plain)
                    }
                }
                .padding(.vertical, 6)
            } header: {
                Text("应用图标")
            } footer: {
                Text("“自动”跟随系统的浅色、深色及着色外观，其他选项固定使用一种外观。"
                   + "更改后 iOS 会显示系统确认提示，此提示无法关闭。")
            }
        }
        .navigationTitle("外观")
        .navigationBarTitleDisplayMode(.inline)
    }

    private func swatch(_ color: Color, selected: Bool) -> some View {
        Circle()
            .fill(color)
            .frame(width: 34, height: 34)
            .overlay(Circle().strokeBorder(Color.primary.opacity(0.9), lineWidth: selected ? 2 : 0).padding(-4))
            .overlay {
                if selected {
                    Image(systemName: "checkmark")
                        .font(.caption.bold())
                        .foregroundStyle(color.isLight ? .black : .white)
                }
            }
            .frame(maxWidth: .infinity)
    }
}

// MARK: - About

struct AboutSettings: View {
    @ObservedObject private var runner = QemuRunner.shared
    @State private var showLogs = false
    @AppStorage(TranslationLayer.devInfoKey) private var devInfo = false

    var body: some View {
        List {
            Section {
                VStack(spacing: 8) {
                    HuskMark(size: 84)
                    Text("Husk").font(.title2.weight(.semibold))
                    Text("版本 \(Bundle.main.version)")
                        .font(.subheadline).foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity)
                .padding(.vertical, 10)
                .listRowBackground(Color.clear)
            }

            Section {
                LabeledContent("构建版本", value: Bundle.main.commit)
                LabeledContent("客户系统镜像", value: GuestImage.imageVersion)
                LabeledContent("渲染器",
                               value: runner.displayKind == .gl ? "GPU"
                                    : runner.displayKind == .software ? "CPU"
                                    : "尚未启动")
            }

            Section {
                Toggle(isOn: $devInfo) {
                    VStack(alignment: .leading, spacing: 3) {
                        Text("开发者信息")
                        Text("在 Android 转译层界面中显示技术详情："
                           + "本地库报告、设备检查和运行日志。")
                            .font(.caption).foregroundStyle(.secondary)
                    }
                }
                if devInfo {
                    NavigationLink { TLChecksView() } label: {
                        Label("设备检查", systemImage: "stethoscope")
                    }
                    NavigationLink { MassTestView() } label: {
                        Label("批量测试", systemImage: "square.stack.3d.down.forward")
                    }
                }
                Button { showLogs = true } label: {
                    Label("打开控制台", systemImage: "terminal")
                }
            } footer: {
                Text("Husk 可在 iPhone 上的完整 Android 系统中运行原版 APK。"
                   + "控制台显示 Husk 的实时日志、客户系统串口输出和 QEMU 输出，"
                   + "这些信息可用于诊断运行问题。")
            }
        }
        .navigationTitle("关于")
        .navigationBarTitleDisplayMode(.inline)
        .sheet(isPresented: $showLogs) { LogView() }
    }
}

extension Bundle {
    var version: String {
        (infoDictionary?["CFBundleShortVersionString"] as? String) ?? "?"
    }
    var commit: String {
        (infoDictionary?["HuskBuildCommit"] as? String) ?? "?"
    }
}
