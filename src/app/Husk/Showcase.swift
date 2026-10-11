// SPDX-License-Identifier: GPL-2.0-or-later
import CryptoKit
import SwiftUI

/// Gameplay pictures for apps that have been run on an iPhone, kept on GitHub rather than in the app.
///
/// The repository's `showcase/` folder holds `<package>_screenshot<N>.jpg` files and an `index.json` listing each with its
/// SHA-256 (`scripts/update_showcase_index.py` writes it). On every launch, and whenever an app is added, Husk reads the
/// index and fetches the pictures for the apps this person actually has -- nothing for anything else. A picture Husk has
/// never had is fetched straight away. One it already has that has changed upstream is not replaced behind anyone's back:
/// Husk asks first. Every download is checked against its checksum before it is kept, so a cut-off or wrong file is never
/// shown. New pictures go up by pushing to the repository, with no release of Husk.
@MainActor
final class ShowcaseStore: ObservableObject {
    static let shared = ShowcaseStore()

    static let base = URL(string: "https://raw.githubusercontent.com/Leviidev/Husk/main/showcase/")!

    /// Packages with pictures that changed upstream, waiting for a yes.
    @Published private(set) var updates: [String] = []
    /// Bumped whenever pictures arrive, so artwork redraws.
    @Published private(set) var generation = 0

    private struct Entry: Codable { let file: String; let sha256: String; let size: Int? }
    private struct Index: Codable { let format: Int; let apps: [String: [Entry]] }

    /// What Husk has on disk: file name -> checksum.
    private var have: [String: String] = [:]
    private var index: Index?
    private var checking = false
    private var askedThisLaunch = false

    private static var folder: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Showcase", isDirectory: true)
    }
    private static var haveFile: URL { folder.appendingPathComponent("have.json") }
    private static var indexFile: URL { folder.appendingPathComponent("index.json") }

    private init() {
        try? FileManager.default.createDirectory(at: Self.folder, withIntermediateDirectories: true)
        have = (try? Data(contentsOf: Self.haveFile)).flatMap { try? JSONDecoder().decode([String: String].self, from: $0) } ?? [:]
        index = (try? Data(contentsOf: Self.indexFile)).flatMap { try? JSONDecoder().decode(Index.self, from: $0) }
    }

    // MARK: what is shown

    /// The pictures on disk for a package, in order. The first is its artwork.
    func pictures(for package: String?) -> [String] {
        _ = generation
        guard let package else { return [] }
        let files = index?.apps[package]?.map(\.file) ?? have.keys.filter { $0.hasPrefix(package + "_screenshot") }.sorted()
        return files.filter { have[$0] != nil }.map { Self.folder.appendingPathComponent($0).path }
    }

    // MARK: keeping up to date

    /// Read the index and fetch what these packages are missing. Changed pictures are only counted, for the question.
    func refresh(for packages: Set<String>) {
        guard !checking, !packages.isEmpty else { return }
        checking = true
        Task {
            defer { checking = false }
            guard let fresh = await fetchIndex() else { return }
            index = fresh
            var missing: [Entry] = []
            var changed: Set<String> = []
            for package in packages {
                for entry in fresh.apps[package] ?? [] {
                    switch have[entry.file] {
                    case nil: missing.append(entry)
                    case let sum? where sum != entry.sha256: changed.insert(package)
                    default: break
                    }
                }
            }
            if !missing.isEmpty {
                HuskLog.log("showcase", "fetching \(missing.count) new picture(s)")
                await download(missing)
            }
            updates = changed.sorted()
            if !updates.isEmpty { HuskLog.log("showcase", "pictures changed upstream for \(updates.joined(separator: ", "))") }
        }
    }

    /// Whether to ask now: once per launch at most.
    var shouldAsk: Bool { !updates.isEmpty && !askedThisLaunch }
    func asked() { askedThisLaunch = true }

    /// The yes: replace the changed pictures.
    func downloadUpdates() {
        askedThisLaunch = true
        let packages = updates
        updates = []
        guard let index else { return }
        let entries = packages.flatMap { index.apps[$0] ?? [] }.filter { have[$0.file] != $0.sha256 }
        Task { await download(entries) }
    }

    private func fetchIndex() async -> Index? {
        var request = URLRequest(url: Self.base.appendingPathComponent("index.json"))
        request.cachePolicy = .reloadIgnoringLocalCacheData
        request.timeoutInterval = 20
        guard let (data, response) = try? await URLSession.shared.data(for: request),
              (response as? HTTPURLResponse)?.statusCode == 200,
              let parsed = try? JSONDecoder().decode(Index.self, from: data) else {
            HuskLog.log("showcase", "could not read the picture index (offline?)")
            return nil
        }
        try? data.write(to: Self.indexFile, options: .atomic)
        return parsed
    }

    private func download(_ entries: [Entry]) async {
        var got = 0
        for entry in entries {
            // Only plain file names: the index is data from the network, not paths to trust.
            guard !entry.file.contains("/"), !entry.file.hasPrefix(".") else { continue }
            var request = URLRequest(url: Self.base.appendingPathComponent(entry.file))
            request.cachePolicy = .reloadIgnoringLocalCacheData
            request.timeoutInterval = 60
            guard let (data, response) = try? await URLSession.shared.data(for: request),
                  (response as? HTTPURLResponse)?.statusCode == 200 else { continue }
            let sum = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
            guard sum == entry.sha256 else {
                HuskLog.log("showcase", "\(entry.file): checksum does not match the index; not kept")
                continue
            }
            do {
                try data.write(to: Self.folder.appendingPathComponent(entry.file), options: .atomic)
                have[entry.file] = sum
                got += 1
            } catch {
                HuskLog.log("showcase", "\(entry.file): \(error.localizedDescription)")
            }
        }
        if let data = try? JSONEncoder().encode(have) { try? data.write(to: Self.haveFile, options: .atomic) }
        if got > 0 { generation += 1 }
    }
}

/// A row of an app's pictures, on its page.
struct ShowcaseGallery: View {
    let package: String?
    @ObservedObject private var showcase = ShowcaseStore.shared
    @State private var shown: String?

    var body: some View {
        let pictures = showcase.pictures(for: package)
        if !pictures.isEmpty {
            ScrollView(.horizontal) {
                HStack(spacing: 10) {
                    ForEach(pictures, id: \.self) { path in
                        Button { shown = path } label: {
                            PictureView(path: path, pixels: 700)
                                .frame(width: 250, height: 116)
                                .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
                        }
                        .buttonStyle(CardButtonStyle())
                    }
                }
            }
            .scrollIndicators(.hidden)
            .fullScreenCover(item: Binding(get: { shown.map(ShownPicture.init) }, set: { shown = $0?.path })) { picture in
                ZStack(alignment: .topTrailing) {
                    Color.black.ignoresSafeArea()
                    PictureView(path: picture.path, pixels: 2400, fill: false)
                        .ignoresSafeArea()
                    Button { shown = nil } label: {
                        Image(systemName: "xmark")
                            .font(.system(size: 15, weight: .bold))
                            .foregroundStyle(.white)
                            .frame(width: 36, height: 36)
                            .background(Color.white.opacity(0.15), in: Circle())
                    }
                    .padding(16)
                }
            }
        }
    }

    private struct ShownPicture: Identifiable { let path: String; var id: String { path } }
}

/// A picture from disk, decoded off the main thread at the size it is drawn.
struct PictureView: View {
    let path: String
    let pixels: Int
    var fill = true
    @State private var image: UIImage?

    var body: some View {
        ZStack {
            Color(uiColor: .secondarySystemBackground)
            if let image {
                Image(uiImage: image).resizable().aspectRatio(contentMode: fill ? .fill : .fit).transition(.opacity)
            }
        }
        .task(id: path) {
            if let hit = IconCache.shared.cached(path, pixels: pixels) { image = hit; return }
            let loaded = await IconCache.shared.load(path, pixels: pixels)
            withAnimation(.easeOut(duration: 0.2)) { image = loaded }
        }
    }
}
