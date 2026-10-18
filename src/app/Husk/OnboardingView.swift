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
    static let version = 2   // 3.0: everyone goes through the new setup once

    private static let key = "husk.onboardingVersion"

    static var needed: Bool {
        UserDefaults.standard.integer(forKey: key) < version
    }

    static func complete() {
        UserDefaults.standard.set(version, forKey: key)
    }

    /// What apps inside Husk are told about where you are, chosen in setup.
    // TODO: hand these to the guest (TL locale/time zone and Android's persist.sys.locale / TZ); stored only for now.
    static let languageKey = "husk.guest.language"
    static let regionKey = "husk.guest.region"
    static let timeZoneKey = "husk.guest.timeZone"

    /// Start the guest as soon as the app opens, when JIT is available.
    static var autoStart: Bool {
        UserDefaults.standard.bool(forKey: "husk.autoStart")
    }
}

/// First run: setup in the manner of the iPhone's own -- language, region, time zone, the
/// optional settings, JIT -- and an offer of a walkthrough. Shown once; people who set up an older Husk get What's New instead.
struct OnboardingView: View {
    let onDone: () -> Void

    private enum Step: Int, CaseIterable {
        case welcome, language, region, timeZone, options, jit, ask, tour
    }

    @State private var step: Step = .welcome
    @State private var forward = true
    @State private var tourPage = 0

    @State private var language = OnboardingView.defaultLanguage
    @State private var region = Locale.current.huskRegionCode ?? "US"
    @State private var timeZone = TimeZone.current.identifier
    @State private var query = ""

    @State private var autoStart = false
    @State private var landscape = UserDefaults.standard.bool(forKey: "husk.landscapeGuest")
    @State private var sound = UserDefaults.standard.bool(forKey: "husk.sound")
    @State private var autoSave = UserDefaults.standard.object(forKey: "husk.autoSave") as? Bool ?? true
    @AppStorage(Theme.Appearance.key) private var appearance = Theme.Appearance.system
    @ObservedObject private var theme = AppTheme.shared
    @ObservedObject private var jit = JITCoordinator.shared
    @State private var settingUpJIT = false

    var body: some View {
        ZStack {
            Theme.backdrop
            setup
        }
        .sheet(isPresented: $settingUpJIT) { JITSetupFlow() }
    }

    // MARK: frame

    private var setup: some View {
        VStack(spacing: 0) {
            HStack {
                if step != .welcome {
                    Button { go(back: true) } label: {
                        HStack(spacing: 4) {
                            Image(systemName: "chevron.left").font(.system(size: 16, weight: .semibold))
                            Text("返回")
                        }
                    }
                    .accessibilityLabel("返回")
                }
                Spacer()
            }
            .frame(height: 44)
            .padding(.horizontal, 16)

            ZStack {
                page(step)
                    .id(step)
                    .transition(.asymmetric(insertion: .move(edge: forward ? .trailing : .leading).combined(with: .opacity),
                                            removal: .move(edge: forward ? .leading : .trailing).combined(with: .opacity)))
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .clipped()

            if step != .ask {
                VStack(spacing: 14) {
                    dots
                    Button(step == .tour && tourPage == Self.tour.count - 1 ? "开始使用 Husk" : "继续") { next() }
                        .buttonStyle(PrimaryButtonStyle())
                }
                .padding(.horizontal, 28)
                .padding(.bottom, 24)
                .padding(.top, 8)
            }
        }
        .frame(maxWidth: 560)
    }

    private var dots: some View {
        let count = step == .tour ? Self.tour.count : Step.ask.rawValue + 1
        let current = step == .tour ? tourPage : step.rawValue
        return HStack(spacing: 6) {
            ForEach(0..<count, id: \.self) { i in
                Capsule()
                    .fill(i == current ? Theme.text : Color.secondary.opacity(0.3))
                    .frame(width: i == current ? 18 : 6, height: 6)
            }
        }
        .animation(.huskSnappy, value: current)
    }

    private func next() {
        if step == .tour {
            if tourPage < Self.tour.count - 1 { withAnimation(.huskSnappy) { tourPage += 1 } } else { finish() }
            return
        }
        query = ""
        forward = true
        withAnimation(.huskSnappy) { step = Step(rawValue: step.rawValue + 1) ?? .ask }
    }

    private func go(back: Bool) {
        query = ""
        forward = false
        withAnimation(.huskSnappy) {
            if step == .tour, tourPage > 0 { tourPage -= 1; return }
            step = Step(rawValue: step.rawValue - 1) ?? .welcome
        }
    }

    private func finish() {
        let d = UserDefaults.standard
        d.set(autoStart, forKey: "husk.autoStart")
        d.set(landscape, forKey: "husk.landscapeGuest")
        d.set(sound, forKey: "husk.sound")
        d.set(autoSave, forKey: "husk.autoSave")
        d.set(language, forKey: Onboarding.languageKey)
        d.set(region, forKey: Onboarding.regionKey)
        d.set(timeZone, forKey: Onboarding.timeZoneKey)
        Onboarding.complete()
        WhatsNew.markSeen()
        HuskLog.log("ui", "setup complete: language=\(language) region=\(region) timeZone=\(timeZone) autoStart=\(autoStart) "
                        + "landscape=\(landscape) sound=\(sound) autoSave=\(autoSave)")
        onDone()
    }

    // MARK: pages

    @ViewBuilder
    private func page(_ step: Step) -> some View {
        switch step {
        case .welcome: welcome
        case .language:
            picker(symbol: "globe", title: "语言",
                   subtitle: "Husk 内应用和游戏使用的语言。Husk 本身使用简体中文界面。",
                   rows: Self.languages, selection: $language)
        case .region:
            picker(symbol: "map.fill", title: "地区",
                   subtitle: "供需要获取国家或地区信息的应用使用。",
                   rows: Self.regions, selection: $region)
        case .timeZone:
            picker(symbol: "clock.fill", title: "时区",
                   subtitle: "供需要显示本地时间的应用使用。",
                   rows: Self.timeZones, selection: $timeZone)
        case .options: options
        case .jit: jitPage
        case .ask: ask
        case .tour: tourCard(Self.tour[tourPage]).id(tourPage)
        }
    }

    private func header(_ symbol: String, _ title: String, _ subtitle: String) -> some View {
        VStack(spacing: 12) {
            Image(systemName: symbol)
                .font(.system(size: 50, weight: .regular))
                .foregroundStyle(Theme.accent)
                .frame(height: 60)
            Text(title)
                .font(.system(size: 32, weight: .bold))
                .multilineTextAlignment(.center)
            Text(subtitle)
                .font(.system(size: 16))
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .fixedSize(horizontal: false, vertical: true)
                .padding(.horizontal, 12)
        }
        .padding(.horizontal, 24)
        .padding(.top, 8)
    }

    private var welcome: some View {
        VStack(spacing: 18) {
            Spacer()
            HuskMark(size: 110)
            Text("欢迎使用 Husk")
                .font(.system(size: 34, weight: .bold))
            Text("在 iPhone 上畅玩 Android 和 PS4 游戏。")
                .font(.title3).foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
            Text("先完成几项简单设置，之后随时都能在“设置”中修改。")
                .font(.callout).foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 36)
            Spacer()
        }
    }

    /// A list to choose one from, searchable, with the choice ticked.
    private func picker(symbol: String, title: String, subtitle: String, rows: [SetupChoice],
                        selection: Binding<String>) -> some View {
        let shown = rows.filter { matches(query, $0.name, $0.id) }
        return VStack(spacing: 16) {
            header(symbol, title, subtitle)
            SearchField(text: $query, prompt: "搜索")
                .padding(.horizontal, 20)
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(spacing: 0) {
                        ForEach(shown) { row in
                            Button { selection.wrappedValue = row.id } label: {
                                HStack {
                                    Text(row.name).foregroundStyle(Theme.text)
                                    Spacer()
                                    if selection.wrappedValue == row.id {
                                        Image(systemName: "checkmark").font(.body.weight(.semibold)).foregroundStyle(Theme.accent)
                                    }
                                }
                                .padding(.horizontal, 16)
                                .frame(height: 46)
                                .contentShape(Rectangle())
                            }
                            .buttonStyle(.plain)
                            .id(row.id)
                            if row.id != shown.last?.id { RowDivider().padding(.leading, -46) }
                        }
                    }
                    .huskCard()
                    .padding(.horizontal, 20)
                    .padding(.bottom, 12)
                }
                .huskDismissKeyboardOnScroll()
                .onAppear { proxy.scrollTo(selection.wrappedValue, anchor: .center) }
            }
        }
    }

    private var options: some View {
        ScrollView {
            VStack(spacing: 16) {
                header("slider.horizontal.3", "个性化设置", "所有选项均可跳过，之后随时都能在“设置”中修改。")

                VStack(alignment: .leading, spacing: 12) {
                    Text("外观").font(.headline)
                    Picker("外观", selection: $appearance) {
                        ForEach(Theme.Appearance.allCases) { Text($0.title).tag($0) }
                    }
                    .pickerStyle(.segmented)
                    .onChange(of: appearance) { Theme.apply($0) }
                    Text("主题色").font(.headline).padding(.top, 4)
                    LazyVGrid(columns: Array(repeating: GridItem(.flexible()), count: 5), spacing: 12) {
                        ForEach(AppTheme.presets) { preset in
                            let selected = preset.color.themeHex == theme.accentColor.themeHex
                            Button { theme.accentColor = preset.color } label: {
                                Circle().fill(preset.color)
                                    .frame(width: 34, height: 34)
                                    .overlay(Circle().stroke(Theme.text, lineWidth: selected ? 2.5 : 0).padding(-4))
                            }
                            .buttonStyle(.plain)
                            .accessibilityLabel(preset.name)
                        }
                    }
                }
                .padding(16)
                .huskCard()

                choice(icon: "bolt.fill", title: "打开 Husk 时启动 Android",
                       detail: "默认关闭。完整 Android 模拟器仍处于实验阶段，随 Husk 启动可能引发"
                             + "异常、卡死或崩溃，也会拖慢每次启动。运行转译层游戏和应用无需开启此项。",
                       isOn: $autoStart, caution: true)
                choice(icon: "rectangle.landscape.rotate", title: "Android 使用横屏",
                       detail: "适用于 Android 模拟器：游戏铺满横屏，竖屏应用两侧保留黑边。",
                       isOn: $landscape)
                choice(icon: "speaker.wave.2.fill", title: "Android 声音",
                       detail: "为模拟器添加音频设备。开启时无法保存运行状态，每次都需完整启动。",
                       isOn: $sound)
                choice(icon: "externaldrive.badge.checkmark", title: "自动保存 Android 状态",
                       detail: "模拟器运行稳定后自动保存状态，方便之后快速启动。",
                       isOn: $autoSave)
            }
            .padding(.horizontal, 20)
            .padding(.bottom, 16)
        }
    }

    private func choice(icon: String, title: String, detail: String, isOn: Binding<Bool>, caution: Bool = false) -> some View {
        HStack(alignment: .top, spacing: 14) {
            Image(systemName: caution ? "exclamationmark.triangle.fill" : icon)
                .font(.system(size: 16, weight: .semibold))
                .foregroundStyle(caution ? Color.orange : Theme.accent)
                .frame(width: 30, height: 30)
            VStack(alignment: .leading, spacing: 3) {
                Text(title).font(.body.weight(.medium))
                Text(detail).font(.caption).foregroundStyle(caution ? Color.orange : Color.secondary)
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
            header("bolt.fill", "开启 JIT",
                   "游戏需要 JIT，而 iOS 通常仅在连接调试器后才会授予这一权限。Husk 内置 StikJIT，可直接在"
                 + "应用内开启，无需电脑。也支持 StikDebug 和 TrollStore。")
            if let state = jitState {
                Label(state, systemImage: "checkmark.circle.fill")
                    .font(.callout.weight(.medium)).foregroundStyle(.green)
            }
            Button(jitState == nil ? "立即设置 StikJIT" : "更改 JIT 设置") { settingUpJIT = true }
                .font(.body.weight(.semibold))
                .foregroundStyle(Theme.accent)
            Text("可以跳过：游戏需要 JIT 时，Husk 会再次提示。也可前往“设置 › JIT 与侧载”开启。")
                .font(.footnote).foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 36)
            Spacer()
        }
    }

    private var ask: some View {
        VStack(spacing: 18) {
            Spacer()
            header("map.circle.fill", "需要快速了解使用方法吗？", "快速了解游戏的位置、添加游戏的方法以及各项设置。")
            Spacer()
            VStack(spacing: 12) {
                Button("查看使用导览") {
                    forward = true
                    tourPage = 0
                    withAnimation(.huskSnappy) { step = .tour }
                }
                .buttonStyle(PrimaryButtonStyle())
                Button("暂不需要") { finish() }
                    .font(.system(size: 16, weight: .semibold))
                    .foregroundStyle(Theme.accent)
                    .frame(maxWidth: .infinity, minHeight: 44)
            }
            .padding(.horizontal, 28)
            .padding(.bottom, 24)
        }
    }

    private struct TourCard { let symbol: String; let title: String; let text: String }

    private static let tour: [TourCard] = [
        TourCard(symbol: "house.fill", title: "首页",
                 text: "游戏按最近游玩的顺序排列。点按游戏即可选中，并在背景中显示其封面；再次点按，"
                     + "或点击“开始游戏”即可运行。“⋯”按钮中包含游戏设置及更多选项。"),
        TourCard(symbol: "square.grid.2x2.fill", title: "应用库",
                 text: "按平台查看已添加的全部内容。可用“全部”“Android”和“PS4”筛选、按名称搜索，长按"
                     + "游戏可查看操作选项。圆点表示兼容状态：绿色为可游玩，橙色为可打开，红色为无法运行。"),
        TourCard(symbol: "plus.circle.fill", title: "添加游戏",
                 text: "点击“+”添加 Android APK 或应用包（.xapk、.apkm、.apks），或添加 PS4 游戏：FPKG 安装包（.pkg）、包含安装包的文件夹，"
                     + "或已解压的游戏文件夹。也可从“文件”或 Safari 将文件共享到 Husk。"),
        TourCard(symbol: "cart.fill", title: "商店",
                 text: "使用游客账号或自己的账号浏览并下载 Google Play 中的 Android 应用，还能查看它们在 Husk 中的兼容情况。"),
        TourCard(symbol: "bolt.fill", title: "设置与 JIT",
                 text: "游戏需要 JIT。可从首页或“设置 › JIT 与侧载”开启。“设置”中还有性能、外观、"
                     + "输入方式，以及可选的完整 Android 模拟器等选项。"),
        TourCard(symbol: "questionmark.circle.fill", title: "帮助",
                 text: "遇到问题时，可在“设置 › 帮助”中查找解答。若游戏导致 Husk 崩溃，还可以分享生成的报告。"),
    ]

    private func tourCard(_ card: TourCard) -> some View {
        VStack(spacing: 22) {
            Spacer()
            Image(systemName: card.symbol)
                .font(.system(size: 44, weight: .semibold))
                .foregroundStyle(Theme.accent)
                .frame(width: 96, height: 96)
                .background(Theme.surface, in: RoundedRectangle(cornerRadius: 26, style: .continuous))
            Text(card.title).font(.system(size: 32, weight: .bold))
            Text(card.text)
                .font(.system(size: 17))
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .fixedSize(horizontal: false, vertical: true)
                .padding(.horizontal, 32)
            Spacer()
        }
        .transition(.opacity)
    }

    // MARK: lists

    private static var defaultLanguage: String {
        let preferred = Locale.preferredLanguages.first ?? "en"
        if preferred.hasPrefix("zh-Hant") || preferred.hasPrefix("zh-TW") || preferred.hasPrefix("zh-HK") { return "zh-Hant" }
        if preferred.hasPrefix("zh") { return "zh-Hans" }
        if preferred.hasPrefix("pt-BR") { return "pt-BR" }
        let code = Locale.current.huskLanguageCode ?? "en"
        return languages.contains { $0.id == code } ? code : "en"
    }

    private static let languages: [SetupChoice] = {
        let codes = ["en", "es", "fr", "de", "it", "pt", "pt-BR", "ru", "uk", "pl", "nl", "sv", "da", "nb", "fi", "tr", "el",
                     "cs", "hu", "ro", "ar", "he", "hi", "th", "vi", "id", "ms", "ja", "ko", "zh-Hans", "zh-Hant"]
        return codes.map { code in
            let native = Locale(identifier: code).localizedString(forIdentifier: code) ?? code
            let here = Locale(identifier: "zh_Hans_CN").localizedString(forIdentifier: code) ?? code
            return SetupChoice(id: code, name: native == here ? native.capitalized : "\(native.capitalized) – \(here)")
        }
    }()

    private static let regions: [SetupChoice] = {
        Locale.isoRegionCodes
            .compactMap { code in Locale(identifier: "zh_Hans_CN").localizedString(forRegionCode: code).map { SetupChoice(id: code, name: $0) } }
            .sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
    }()

    private static let timeZones: [SetupChoice] = {
        TimeZone.knownTimeZoneIdentifiers.map { id in
            let tz = TimeZone(identifier: id)
            let city = tz?.localizedName(for: .generic, locale: Locale(identifier: "zh_Hans_CN")) ?? id
            let offset = tz.map { z -> String in
                let s = z.secondsFromGMT()
                return String(format: "GMT%@%d:%02d", s >= 0 ? "+" : "-", abs(s) / 3600, abs(s) % 3600 / 60)
            } ?? ""
            return SetupChoice(id: id, name: "\(city) (\(offset)) · \(id)")
        }
        .sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
    }()
}

/// One row of a setup list: what is stored, and what is shown.
struct SetupChoice: Identifiable { let id: String; let name: String }
