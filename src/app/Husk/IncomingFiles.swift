// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI
import UniformTypeIdentifiers

/// APKs handed to Husk from another app: the share sheet's "Husk" (or "Copy to Husk"), or "Open in Husk" from Files.
///
/// Each arrives as a URL that may only be readable for a moment (a security-scoped file in another app's container, or a copy
/// in Documents/Inbox that iOS made), so it is copied straight away to a folder of Husk's own, and then the person is asked
/// where it goes: the Translation Layer, which runs a game on the iPhone itself, or Android. Several shared at once -- a base
/// APK and its splits -- arrive one URL at a time and are gathered into one question.
@MainActor
final class IncomingFiles: ObservableObject {
    static let shared = IncomingFiles()

    /// Files waiting for the person to say where they go.
    @Published var waiting: [URL] = []
    /// Files meant for Android, held until Android is up to take them.
    @Published private(set) var forAndroid: [URL] = []

    private var gathering: [URL] = []
    private var gatherTask: Task<Void, Never>?

    /// Where copies wait. Emptied when Husk starts, so nothing left over takes up space.
    nonisolated static var folder: URL {
        FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0].appendingPathComponent("Incoming", isDirectory: true)
    }

    nonisolated static func clearLeftovers() {
        try? FileManager.default.removeItem(at: folder)
    }

    static let extensions: Set<String> = ["apk", "xapk", "apkm", "apks"]

    /// Copies under way, said in one line while they last.
    @Published private(set) var preparing: String?

    /// A URL iOS opened Husk with. Anything that is not an APK or a bundle of them is left alone (husk:// links are not files).
    func receive(_ url: URL) {
        guard url.isFileURL, Self.extensions.contains(url.pathExtension.lowercased()) else { return }
        take([url]) { copies in
            self.gathering += copies
            // The pieces of a split set arrive one after another: ask once they have all come.
            self.gatherTask?.cancel()
            self.gatherTask = Task {
                try? await Task.sleep(nanoseconds: 600_000_000)
                guard !Task.isCancelled else { return }
                self.waiting += self.gathering
                self.gathering = []
            }
        }
    }

    /// Files picked inside Husk: all at once, so they are one question.
    func receive(_ urls: [URL]) {
        let wanted = urls.filter { Self.extensions.contains($0.pathExtension.lowercased()) }
        guard !wanted.isEmpty else { return }
        take(wanted) { self.waiting += $0 }
    }

    /// Copy files to Husk's own folder, off the main thread -- a game can be gigabytes -- then hand back the copies.
    private func take(_ urls: [URL], then: @escaping @MainActor ([URL]) -> Void) {
        preparing = urls.count == 1 ? "Preparing \(urls[0].lastPathComponent)…" : "Preparing \(urls.count) files…"
        Task.detached(priority: .userInitiated) {
            var copies: [URL] = []
            for url in urls {
                let scoped = url.startAccessingSecurityScopedResource()
                defer { if scoped { url.stopAccessingSecurityScopedResource() } }
                let dir = Self.folder.appendingPathComponent(UUID().uuidString, isDirectory: true)
                let copy = dir.appendingPathComponent(url.lastPathComponent)
                do {
                    try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
                    try FileManager.default.copyItem(at: url, to: copy)
                    copies.append(copy)
                    // iOS's own copy (Documents/Inbox) is no use once Husk has one.
                    if url.path.contains("/Documents/Inbox/") { try? FileManager.default.removeItem(at: url) }
                    HuskLog.log("ui", "took in \(url.lastPathComponent)")
                } catch {
                    HuskLog.log("ui", "could not take in \(url.lastPathComponent): \(error.localizedDescription)")
                }
            }
            await MainActor.run {
                self.preparing = nil
                if !copies.isEmpty { then(copies) }
            }
        }
    }

    /// Run on the iPhone itself.
    func sendToTranslationLayer() {
        let urls = waiting
        waiting = []
        guard !urls.isEmpty else { return }
        // Moved, not copied: the copy here is Husk's own, and a game can be gigabytes.
        TranslationLayerStore.shared.add(urls, move: true)
    }

    /// Installed into Android -- now if it is up, otherwise as soon as it is.
    func sendToAndroid() {
        let urls = waiting
        waiting = []
        guard !urls.isEmpty else { return }
        forAndroid += urls
        installForAndroidIfReady()
    }

    func installForAndroidIfReady() {
        guard AndroidHost.shared.isReady, AndroidHost.shared.busy == nil, !forAndroid.isEmpty else { return }
        let urls = forAndroid
        forAndroid = []
        AndroidHost.shared.install(urls)
    }

    func discard() {
        for url in waiting { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        waiting = []
    }
}

/// Where a shared APK goes. Two choices, each saying what it means, and a way out.
struct IncomingChooser: View {
    @ObservedObject private var incoming = IncomingFiles.shared
    @ObservedObject private var host = AndroidHost.shared
    @ObservedObject private var guest = GuestImage.shared

    private var title: String {
        guard let first = incoming.waiting.first else { return "" }
        return incoming.waiting.count == 1 ? first.lastPathComponent : "\(incoming.waiting.count) files"
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            VStack(alignment: .leading, spacing: 4) {
                Text("Add to Husk").font(.title2.weight(.bold))
                Text(title).font(.subheadline).foregroundStyle(.secondary).lineLimit(2)
            }

            choice(icon: "bolt.fill", title: "Translation Layer",
                   detail: "Runs the game directly on your iPhone, without starting Android. Best for games.") {
                incoming.sendToTranslationLayer()
            }
            choice(icon: "apps.iphone", title: "Android",
                   detail: androidDetail) {
                incoming.sendToAndroid()
            }

            Button("Cancel", role: .cancel) { incoming.discard() }
                .frame(maxWidth: .infinity)
                .padding(.top, 2)
        }
        .padding(22)
    }

    private var androidDetail: String {
        if host.isReady { return "Installs it into Android, where any app can run." }
        if guest.state == .ready { return "Installs it into Android the next time Android starts." }
        return "Installs it into Android once Android is downloaded and started."
    }

    private func choice(icon: String, title: String, detail: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 14) {
                Image(systemName: icon)
                    .font(.system(size: 18, weight: .semibold))
                    .foregroundStyle(Color.accentColor)
                    .frame(width: 44, height: 44)
                    .background(Color.accentColor.opacity(0.14), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
                VStack(alignment: .leading, spacing: 2) {
                    Text(title).font(.headline).foregroundStyle(.primary)
                    Text(detail).font(.subheadline).foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
                Spacer(minLength: 0)
                Image(systemName: "chevron.right").font(.footnote.weight(.semibold)).foregroundStyle(.tertiary)
            }
            .padding(14)
            .background(Color(uiColor: .secondarySystemBackground), in: RoundedRectangle(cornerRadius: 16, style: .continuous))
        }
        .buttonStyle(CardButtonStyle())
    }
}
