// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

/// Help: common problems and what to do about them, so a user stuck on JIT or on an app that will not open has an answer
/// without leaving Husk. A page in Settings (it was a tab of its own; four tabs read simpler than five).
struct FAQTab: View {
    @State private var query = ""

    private var sections: [FAQ.Section] {
        let q = query.trimmingCharacters(in: .whitespaces)
        guard !q.isEmpty else { return FAQ.sections }
        return FAQ.sections.compactMap { s in
            let items = s.items.filter { $0.question.localizedCaseInsensitiveContains(q) || $0.answer.localizedCaseInsensitiveContains(q) }
            return items.isEmpty ? nil : FAQ.Section(title: s.title, symbol: s.symbol, items: items)
        }
    }

    var body: some View {
            List {
                ForEach(sections) { s in
                    Section {
                        ForEach(s.items) { item in FAQRow(item: item, expanded: !query.isEmpty) }
                    } header: {
                        Label(s.title, systemImage: s.symbol)
                    }
                }
                if sections.isEmpty {
                    Text("没有找到与“\(query)”匹配的问题。可前往“设置 › 关于 › 打开控制台”查看 Husk 的运行日志。")
                        .foregroundStyle(.secondary)
                }
            }
            .navigationTitle("帮助")
            .searchable(text: $query, prompt: "搜索问题")
    }
}

private struct FAQRow: View {
    let item: FAQ.Item
    var expanded: Bool
    @State private var open = false

    var body: some View {
        DisclosureGroup(isExpanded: Binding(get: { open || expanded }, set: { open = $0 })) {
            Text(.init(item.answer))
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
                .padding(.vertical, 4)
        } label: {
            Text(item.question).font(.body.weight(.medium))
        }
    }
}

enum FAQ {
    struct Item: Identifiable { let question: String; let answer: String; var id: String { question } }
    struct Section: Identifiable { let title: String; let symbol: String; let items: [Item]; var id: String { title } }

    static let sections: [Section] = [
        Section(title: "JIT", symbol: "bolt.fill", items: [
            Item(question: "什么是 JIT？Husk 为什么需要它？",
                 answer: "Android 应用和游戏需要在运行时编译代码，因此需要能够先写入、再执行的内存。"
                       + "iOS 通常只允许已附加调试器的应用使用这种内存。开启 JIT 时会短暂附加调试器。"
                       + "没有 JIT 时，你可以浏览应用库，但无法启动 Android 或游戏。"),
            Item(question: "应该选择哪种 JIT 开启方式？",
                 answer: "• **iOS 27 及更新版本：**前往“设置 › JIT › 在此 iPhone 上配对”，无需电脑。\n"
                       + "• **iOS 26：**使用内置 StikJIT，导入在电脑上生成的配对文件，并连接 LocalDevVPN。\n"
                       + "• **较早的 iOS，或在 LiveContainer 中运行 Husk：**使用 StikDebug 应用。\n"
                       + "• **通过 TrollStore 安装：**使用 TrollStore，并开启其 URL Scheme 设置。\n"
                       + "• **使用 Dopamine 越狱：**在越狱工具设置中为应用开启 JIT。"),
            Item(question: "StikJIT 提示无法连接，或开启 JIT 失败",
                 answer: "1. 确认已安装并连接 **LocalDevVPN**，状态栏应显示 VPN 图标。\n"
                       + "2. 必须开启 Wi-Fi，配对通信仅在本机进行。\n"
                       + "3. 开启**开发者模式**（系统设置 › 隐私与安全性 › 开发者模式）。\n"
                       + "4. 首次使用时，Husk 会下载 Apple 开发者磁盘映像，可能需要一分钟，请等待完成。\n"
                       + "5. 如果仍然失败，请断开并重新连接 LocalDevVPN，然后重新配对。"),
            Item(question: "开发者模式中没有显示“与 Husk 配对”",
                 answer: "先在 Husk 中点按**开始配对**，并在提示时允许访问**本地网络**。如果之前拒绝了此权限，"
                       + "请前往“系统设置 › 隐私与安全性 › 本地网络 › Husk”开启。Wi-Fi 也必须开启。"
                       + "需要提前开启开发者模式，首次开启时 iPhone 会重新启动。"),
            Item(question: "配对文件失效了",
                 answer: "重置 iPhone 的信任设置后，配对文件会失效；部分 iOS 更新也可能导致失效。"
                       + "请重新生成并在“设置 › JIT”中导入配对文件；iOS 27 用户也可直接在 iPhone 上重新配对。"),
            Item(question: "StikDebug 已打开，但 JIT 仍未开启",
                 answer: "单独打开 StikDebug，确认已导入配对文件，并且 LocalDevVPN 已连接。"
                       + "请将 StikDebug 更新至最新版。Husk 会自动将脚本发送给 StikDebug，无需在其中为 Husk 额外配置。"),
            Item(question: "为什么需要再次开启 JIT？",
                 answer: "JIT 仅在 Husk 当前运行期间有效。iOS 在后台关闭 Husk，或你手动将它划掉后，"
                       + "下次启动时需要重新开启 JIT。完成本机配对或导入配对文件后，只需点按一次即可开启。"),
            Item(question: "不开启 JIT 可以使用 Husk 吗？",
                 answer: "可以浏览和导入应用，但无法运行 Android 应用。运行游戏和应用都需要 JIT。"),
        ]),
        Section(title: "Husk 本身", symbol: "app.badge", items: [
            Item(question: "Husk 完全无法打开",
                 answer: "• **打开后立即退出：**签名证书可能已过期，免费 Apple 账户的签名有效期为 7 天。"
                       + "请使用侧载工具重新签名或覆盖安装 Husk，以保留应用和存档。\n"
                       + "• **提示“不受信任的开发者”：**前往“系统设置 › 通用 › VPN 与设备管理”，信任对应证书。\n"
                       + "• **要求开启开发者模式：**前往“系统设置 › 隐私与安全性”开启。"),
            Item(question: "游玩过程中 Husk 退出了",
                 answer: "重新打开 Husk 后会显示出错原因，并提供可分享的报告。通常是游戏内存不足，"
                       + "或使用了 Husk 尚不支持的功能。请关闭其他应用；如果问题持续出现，可分享报告以协助排查。"),
            Item(question: "如何获取日志以反馈问题？",
                 answer: "在应用库中打开对应应用的页面，点按**分享报告**。如需 Husk 自身的日志，请前往"
                       + "“设置 › 关于 › **打开控制台**”。开启**开发者信息**后可查看更详细的内容。"),
            Item(question: "如何更新 Husk？",
                 answer: "有新版本时 Husk 会提示。使用原来的侧载工具将新版 IPA 覆盖安装到旧版上，"
                       + "即可保留应用库、应用和存档。"),
        ]),
        Section(title: "应用和游戏", symbol: "gamecontroller.fill", items: [
            Item(question: "如何添加 PS4 游戏？",
                 answer: "点击“+”，选择 FPKG 安装包（.pkg）、包含安装包的文件夹，或已解压的游戏文件夹（包含 eboot.bin 和 sce_sys）。"
                       + "Husk 会将其安装到“文件”中的“PS4 Games”目录，更新包会安装在对应游戏旁的文件夹中。"
                       + "零售版安装包与 PS4 设备绑定，无法安装。PS4 游戏需要 iOS 17.4 或更高版本。"),
            Item(question: "应用无法打开",
                 answer: "在应用库中打开该应用的页面，Husk 会说明停止运行的原因：\n"
                       + "• **受保护的应用：**应用会检测真实 Android 设备，或采用反作弊、DRM 保护。Husk 无法绕过这些限制。\n"
                       + "• **CPU 架构不匹配：**APK 仅包含 32 位代码。请寻找 arm64（arm64-v8a）版本。\n"
                       + "• **应用不完整或缺少游戏数据：**APK 还需要拆分文件或 OBB 数据包。请导入完整的 .apks、.xapk 或 .apkm 文件。\n"
                       + "• **需要 Google：**应用依赖 Google 账户或 Husk 无法提供的 Play 服务。\n"
                       + "• **使用了 Husk 尚未支持的功能：**请分享报告，帮助开发者补充支持。"),
            Item(question: "屏幕一直是黑的",
                 answer: "大型游戏首次启动时需要准备代码和数据，可能需要一分钟。"
                       + "如果之后仍然黑屏，请关闭并重新打开一次；若问题依旧，可分享运行报告。"),
            Item(question: "修改版或重新打包的 APK 发生崩溃",
                 answer: "APKVISION 等网站重新打包的应用可能将原代码封装到自定义库中，或加入防篡改检查，"
                       + "因此更容易出现兼容性问题。请尝试开发者或 APKMirror 提供的原始 APK。"),
            Item(question: "可以导入哪些文件？",
                 answer: "支持 .apk，以及 .apks、.xapk、.apkm 格式的拆分安装包。可将文件分享到 Husk、从“文件”中打开，或点按应用库中的“+”。"),
            Item(question: "游戏运行缓慢或手机发热",
                 answer: "请关闭其他应用并让手机降温。虽然游戏采用原生方式运行，但某些原本由 Android 硬件处理的任务需要由 CPU 完成。"
                       + "低电量模式也会降低 Husk 的运行速度。"),
            Item(question: "可以使用手柄吗？",
                 answer: "可以。在 iPhone 的蓝牙设置中连接手柄，支持手柄的游戏即可识别。"),
            Item(question: "游戏运行时还能使用 Husk 吗？",
                 answer: "在设置中开启**在窗口中打开应用**。之后应用会在可移动、调整大小和最小化的窗口中打开。"),
            Item(question: "无法使用 Google 登录",
                 answer: "Husk 不提供完整的 Google Play 服务，因此“使用 Google 登录”可能失败。如果应用支持，请尝试邮箱或其他登录方式。"),
        ]),
        Section(title: "存档和存储空间", symbol: "externaldrive.fill", items: [
            Item(question: "存档保存在哪里？",
                 answer: "每个应用的数据都保存在 Husk 内。在应用页面中，**备份存档**可导出数据，**恢复存档**可将其导入，"
                       + "也可以导入到另一台 iPhone。"),
            Item(question: "Husk 占用了大量存储空间",
                 answer: "导入的应用及其数据都保存在 Husk 文件夹中。可在游戏页面删除不再游玩的游戏。"
                       + "完整 Android 系统还需要下载约 2 GB 数据，只有运行 Android 系统时才需要。"),
        ]),
        Section(title: "Android", symbol: "cpu", items: [
            Item(question: "直接运行应用与启动 Android 有什么区别？",
                 answer: "大部分应用和游戏可以像原生应用一样直接在 iPhone 上运行。完整 Android 系统是独立的运行方式，"
                       + "速度较慢，用于那些只能在完整 Android 环境中运行的应用。"),
            Item(question: "Android 启动很慢",
                 answer: "首次启动需要下载 Android 镜像并进行冷启动，可能需要几分钟。"
                       + "之后 Husk 会从系统快照恢复，启动速度会快得多。"),
        ]),
    ]
}
