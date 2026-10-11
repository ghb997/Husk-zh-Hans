// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

/// What Husk asks on a first install, and remembers.
///
/// Deliberately versioned rather than a plain "seen it" flag: a later build that
/// adds a question needs to be able to ask it, and existing installs must not be
/// dragged back through the whole flow for one new answer. Bumping
/// `Onboarding.version` is what re-opens it.
enum Onboarding {
    /// Raise this when a question is added that existing installs must answer.
    static let version = 1

    private static let key = "husk.onboardingVersion"

    static var needed: Bool {
        UserDefaults.standard.integer(forKey: key) < version
    }

    static func complete() {
        UserDefaults.standard.set(version, forKey: key)
    }

    /// Start the guest as soon as the app opens, when JIT is available.
    static var autoStart: Bool {
        UserDefaults.standard.bool(forKey: "husk.autoStart")
    }
}

struct OnboardingView: View {
    let onDone: () -> Void

    @State private var page = 0
    @State private var autoStart = false
    @State private var landscape = UserDefaults.standard.bool(forKey: "husk.landscapeGuest")
    @State private var sound = UserDefaults.standard.bool(forKey: "husk.sound")
    @State private var autoSave =
        UserDefaults.standard.object(forKey: "husk.autoSave") as? Bool ?? true
    @Environment(\.colorScheme) private var scheme
    @ObservedObject private var jit = JITCoordinator.shared
    @State private var settingUpJIT = false

    private let pages = 4

    var body: some View {
        ZStack {
            Theme.backdrop

            VStack(spacing: 0) {
                TabView(selection: $page) {
                    welcome.tag(0)
                    choices.tag(1)
                    jitPage.tag(2)
                    ready.tag(3)
                }
                .tabViewStyle(.page(indexDisplayMode: .never))
                .sheet(isPresented: $settingUpJIT) { JITSetupFlow() }

                // One control, always in the same place. A flow that moves its
                // own button around is harder to get through than one that does
                // not, and this is the first thing anyone sees.
                VStack(spacing: 12) {
                    HStack(spacing: 6) {
                        ForEach(0..<pages, id: \.self) { i in
                            Capsule()
                                .fill(i == page ? Theme.accent : Color.secondary.opacity(0.3))
                                .frame(width: i == page ? 18 : 6, height: 6)
                                .animation(.snappy, value: page)
                        }
                    }
                    Button {
                        if page < pages - 1 {
                            withAnimation(.snappy) { page += 1 }
                        } else {
                            save()
                            onDone()
                        }
                    } label: {
                        Text(page < pages - 1 ? "继续" : "开始使用 Husk")
                    }
                    .buttonStyle(PrimaryButtonStyle())
                    .padding(.horizontal, 28)
                }
                .padding(.bottom, 28)
            }
        }
    }

    private func save() {
        let d = UserDefaults.standard
        d.set(autoStart, forKey: "husk.autoStart")
        d.set(landscape, forKey: "husk.landscapeGuest")
        d.set(sound, forKey: "husk.sound")
        d.set(autoSave, forKey: "husk.autoSave")
        Onboarding.complete()
        HuskLog.log("ui", "setup complete: autoStart=\(autoStart) landscape=\(landscape) "
                        + "sound=\(sound) autoSave=\(autoSave)")
    }

    // MARK: pages

    private var welcome: some View {
        VStack(spacing: 20) {
            Spacer()
            if let art = HuskAppIcon.current.preview(dark: scheme == .dark) {
                Image(uiImage: art)
                    .resizable().scaledToFit()
                    .frame(width: 112, height: 112)
                    .clipShape(RoundedRectangle(cornerRadius: 25, style: .continuous))
                    .shadow(color: Theme.accent.opacity(0.35), radius: 22, y: 10)
            }
            Text("Husk").font(.system(size: 40, weight: .semibold, design: .rounded))
            Text("在 iPhone 上运行 Android 应用。")
                .font(.title3).foregroundStyle(.secondary)
            Text("Husk 可运行完整的 Android 系统，并在其中打开 APK。"
               + "先完成几项设置，之后随时都可以在“设置”中修改。")
                .font(.callout).foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 34).padding(.top, 4)
            Spacer()
        }
    }

    private var choices: some View {
        ScrollView {
            VStack(spacing: 14) {
                Text("选择 Husk 的运行方式")
                    .font(.title2.weight(.semibold))
                    .padding(.top, 34).padding(.bottom, 6)

                choice(icon: "bolt.fill", title: "启动时自动运行 Android",
                       detail: "JIT 可用时，打开 Husk 即自动"
                             + "启动 Android。关闭后需要手动启动。",
                       isOn: $autoStart)

                choice(icon: "rectangle.landscape.rotate", title: "横屏显示",
                       detail: "为 Android 创建横屏画面，让横屏游戏铺满屏幕。"
                             + "竖屏应用则会显示黑边。",
                       isOn: $landscape)

                choice(icon: "speaker.wave.2.fill", title: "声音",
                       detail: "添加音频设备。开启后无法保存 Android 状态，"
                             + "每次启动都需要完整冷启动。",
                       isOn: $sound)

                choice(icon: "externaldrive.badge.checkmark", title: "自动保存",
                       detail: "Android 稳定运行后保存系统状态，"
                             + "下次启动时数秒即可恢复，无需重新引导。",
                       isOn: $autoSave)
            }
            .padding(.horizontal, 20).padding(.bottom, 20)
        }
    }

    private func choice(icon: String, title: String, detail: String,
                        isOn: Binding<Bool>) -> some View {
        HStack(alignment: .top, spacing: 14) {
            Image(systemName: icon)
                .font(.system(size: 17, weight: .semibold))
                .foregroundStyle(Theme.accent)
                .frame(width: 30, height: 30)
                .background(Theme.accentSoft, in: RoundedRectangle(cornerRadius: 9,
                                                                   style: .continuous))
            VStack(alignment: .leading, spacing: 3) {
                Text(title).font(.body.weight(.medium))
                Text(detail).font(.caption).foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Spacer(minLength: 8)
            Toggle("", isOn: isOn).labelsHidden().tint(Theme.accent)
        }
        .padding(16)
        .huskCard()
    }

    /// What the JIT page says is already in place, if anything.
    private var jitState: String? {
        if JITBootstrap.ready { return "JIT 已开启。" }
        if jit.method == .stikDebug { return "Husk 将使用 StikDebug。" }
        if jit.method == .trollStore { return "Husk 将使用 TrollStore。" }
        switch jit.pairingSource {
        case .onDevice: return "已在此设备上配对。"
        case .imported: return "已导入配对文件。"
        case nil: return nil
        }
    }

    private var jitPage: some View {
        VStack(spacing: 18) {
            Spacer()
            Image(systemName: "bolt.fill")
                .font(.system(size: 54))
                .foregroundStyle(Theme.accent)
            Text("开启 JIT").font(.largeTitle.weight(.semibold))
            Text("Android 和转译层游戏都需要 JIT，在 iOS 上需要附加调试器才能获得。"
               + "推荐使用 Husk 内置的 StikJIT，可直接在应用内开启 JIT，"
               + "无需电脑或其他调试应用。也可以使用 StikDebug 或 TrollStore。")
                .font(.callout).foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 34)
            if let state = jitState {
                Label(state, systemImage: "checkmark.circle.fill")
                    .font(.callout.weight(.medium)).foregroundStyle(.green)
            }
            Button(jitState == nil ? "立即设置 StikJIT" : "更改 JIT 设置") { settingUpJIT = true }
                .font(.body.weight(.semibold))
                .foregroundStyle(Theme.accent)
                .padding(.top, 4)
            Spacer()
        }
    }

    private var ready: some View {
        VStack(spacing: 18) {
            Spacer()
            Image(systemName: "checkmark.seal.fill")
                .font(.system(size: 62))
                .foregroundStyle(Theme.accent)
            Text("准备就绪").font(.largeTitle.weight(.semibold))
            Text("启动 Android 时，如果 JIT 尚未开启，Husk 会通过"
               + "你选择的方式开启，或引导你完成设置。"
               + "之后可随时在“设置 › JIT 与侧载”中更改。")
                .font(.callout).foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 34)
            Spacer()
        }
    }
}
