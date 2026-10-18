// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

// MARK: - what's new

/// What this version brought, shown once after updating to it (not on a first install: onboarding is the welcome then).
enum WhatsNew {
    struct Item: Identifiable {
        let symbol: String
        let title: String
        let detail: String
        var id: String { title }
    }

    /// The version whose notes these are. A build of the same version shows nothing again.
    static let version = "3.0.0"
    static let items: [Item] = [
        Item(symbol: "gamecontroller.fill", title: "焕然一新的 Husk",
             detail: "主机风格首页让游戏整齐排列，选中游戏即可铺满封面，点一下就能开玩。Android 与 PS4 游戏并排展示，应用库也焕然一新，并遵循“设置 › 外观”中的深浅色偏好。"),
        Item(symbol: "playstation.logo", title: "PS4 游戏",
             detail: "点击“+”添加 PS4 游戏：支持 FPKG 安装包（.pkg）、包含安装包的文件夹或已解压的游戏目录。更新包会随游戏安装，着色器缓存会在多次启动间保留，需要输入名称时会弹出键盘。"),
        Item(symbol: "square.stack.3d.up.fill", title: "支持数百款新增 Android 游戏",
             detail: "兼容列表新增数百款可运行游戏，支持 Sonic Robo Blast 2、Ring Racers、NetHack、PokeWilds 等非官方移植版。修复 Flutter 游戏白屏，支持 Godot 4 游戏，并为 Python 游戏提供联网能力。"),
        Item(symbol: "arrow.down.circle.fill", title: "更完整的商店下载",
             detail: "下载游戏时会一并获取所需的扩展数据文件，拆分为多个安装包的游戏也能正常加载。"),
        Item(symbol: "keyboard.fill", title: "更广泛的文字输入支持",
             detail: "Unity、Java、Flutter、cocos2d-x 和 SDL 游戏中的文本输入框现在都可以唤起键盘。"),
        Item(symbol: "textformat", title: "中文、日文和韩文",
             detail: "本汉化版已内置中日韩字体，帮助游戏正常显示文字；也可在“设置 › 字体”中管理额外字体。"),
        Item(symbol: "bolt.fill", title: "自动开启 JIT",
             detail: "通过 TrollStore 安装的版本可在启动时开启 JIT。若因未开启 JIT 而无法运行，Husk 会在界面中给出提示。"),
        Item(symbol: "iphone", title: "iOS 15",
             detail: "Husk 现支持 iOS 15.5 及更高版本；PS4 游戏需要 iOS 17.4。"),
        Item(symbol: "wrench.and.screwdriver.fill", title: "问题修复",
             detail: "修复 Android 下载完成后崩溃的问题，将完整 Android 模拟器移至“设置”并补充实验性说明，不再把从多任务界面关闭 Husk 误报为崩溃，以及更多修复。"),
    ]

    private static let seenKey = "husk.whatsNew.seen"

    /// Whether to show the notes now: an update to this version, not a new install.
    static var due: Bool {
        let seen = UserDefaults.standard.string(forKey: seenKey)
        if seen == version { return false }
        if Onboarding.needed { markSeen(); return false }       // a first install: onboarding welcomes them
        return true
    }

    static func markSeen() { UserDefaults.standard.set(version, forKey: seenKey) }
}

struct WhatsNewSheet: View {
    var done: () -> Void

    var body: some View {
        VStack(spacing: 0) {
            ScrollView {
                VStack(alignment: .leading, spacing: 26) {
                    VStack(alignment: .leading, spacing: 6) {
                        Text("Husk \(WhatsNew.version) 更新内容").font(.largeTitle.weight(.bold))
                        Text("全新界面、PS4 游戏，以及数百款新增 Android 游戏。").foregroundStyle(.secondary)
                    }
                    .padding(.top, 36)
                    ForEach(WhatsNew.items) { item in
                        HStack(alignment: .top, spacing: 16) {
                            Image(systemName: item.symbol)
                                .font(.system(size: 26))
                                .foregroundStyle(Color.accentColor)
                                .frame(width: 36)
                            VStack(alignment: .leading, spacing: 3) {
                                Text(item.title).font(.headline)
                                Text(item.detail).font(.subheadline).foregroundStyle(.secondary)
                                    .fixedSize(horizontal: false, vertical: true)
                            }
                        }
                    }
                }
                .padding(.horizontal, 28)
                .padding(.bottom, 20)
            }
            Button(action: done) {
                Text("继续").font(.headline).frame(maxWidth: .infinity).padding(.vertical, 8)
            }
            .buttonStyle(.borderedProminent)
            .padding(.horizontal, 28)
            .padding(.vertical, 18)
        }
        .interactiveDismissDisabled()
    }
}

// MARK: - a newer Husk

/// A newer Husk on GitHub: the releases page is asked at launch (at most twice a day) and the person is told once per release.
@MainActor
final class AppUpdates: ObservableObject {
    static let shared = AppUpdates()

    struct Release: Equatable {
        let version: String
        let page: URL
    }

    @Published var available: Release?

    // Keep the Chinese build on its own release channel.
    private static let api = URL(string: "https://api.github.com/repos/ghb997/Husk-zh-Hans/releases/latest")!
    private static let checkedKey = "husk.updates.checked", dismissedKey = "husk.updates.dismissed"

    static var current: String { Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "0" }

    func check() async {
        let d = UserDefaults.standard
        if Date().timeIntervalSince1970 - d.double(forKey: Self.checkedKey) < 12 * 3600 { return }
        var req = URLRequest(url: Self.api)
        req.timeoutInterval = 20
        req.setValue("Husk", forHTTPHeaderField: "User-Agent")
        guard let (data, response) = try? await URLSession.shared.data(for: req),
              (response as? HTTPURLResponse)?.statusCode == 200,
              let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let tag = obj["tag_name"] as? String,
              let page = (obj["html_url"] as? String).flatMap(URL.init(string:)) else { return }
        d.set(Date().timeIntervalSince1970, forKey: Self.checkedKey)
        let version = tag.hasPrefix("v") ? String(tag.dropFirst()) : tag
        guard Self.newer(version, than: Self.current), d.string(forKey: Self.dismissedKey) != version else { return }
        HuskLog.log("update", "Husk \(version) is out (this is \(Self.current))")
        available = Release(version: version, page: page)
    }

    func dismiss() {
        if let v = available?.version { UserDefaults.standard.set(v, forKey: Self.dismissedKey) }
        available = nil
    }

    /// "1.0.10" is newer than "1.0.9": compared number by number.
    static func newer(_ a: String, than b: String) -> Bool {
        let x = a.split(separator: ".").map { Int($0) ?? 0 }, y = b.split(separator: ".").map { Int($0) ?? 0 }
        for i in 0..<max(x.count, y.count) {
            let p = i < x.count ? x[i] : 0, q = i < y.count ? y[i] : 0
            if p != q { return p > q }
        }
        return false
    }
}
