// SPDX-License-Identifier: GPL-2.0-or-later
import ImageIO
import SwiftUI
import UIKit

/// Decoded app icons, kept at the size they are drawn.
///
/// An icon used to be decoded from its file inside `body`, on the main thread, every time the view redrew -- a grid
/// of thirty icons decoded thirty PNGs each time anything on the screen changed, at whatever size the PNG happened to
/// be (512 px for an xxxhdpi launcher icon, for a 64 pt tile). Here each file is decoded once, off the main thread,
/// straight to the pixels the tile shows (ImageIO's thumbnailing does the scaling while decoding), and kept in a
/// cache keyed by the file and when it last changed, so a new icon replaces an old one without a stale copy lingering.
final class IconCache: @unchecked Sendable {
    static let shared = IconCache()

    private let cache = NSCache<NSString, UIImage>()
    private let queue = DispatchQueue(label: "husk.icons", qos: .userInitiated, attributes: .concurrent)

    private init() { cache.countLimit = 400 }

    /// When the file last changed: part of the key, so an icon drawn again replaces the old one.
    static func stamp(_ path: String?) -> TimeInterval {
        guard let path else { return 0 }
        return (try? FileManager.default.attributesOfItem(atPath: path)[.modificationDate] as? Date)?
            .timeIntervalSince1970 ?? 0
    }

    private func key(_ path: String, _ pixels: Int) -> NSString {
        "\(path)|\(pixels)|\(Self.stamp(path))" as NSString
    }

    /// Already decoded at this size: drawn at once, with no fade.
    func cached(_ path: String, pixels: Int) -> UIImage? { cache.object(forKey: key(path, pixels)) }

    func load(_ path: String, pixels: Int) async -> UIImage? {
        let k = key(path, pixels)
        if let hit = cache.object(forKey: k) { return hit }
        return await withCheckedContinuation { done in
            queue.async {
                let image = Self.decode(path, pixels: pixels)
                if let image { self.cache.setObject(image, forKey: k) }
                done.resume(returning: image)
            }
        }
    }

    private static func decode(_ path: String, pixels: Int) -> UIImage? {
        let url = URL(fileURLWithPath: path) as CFURL
        guard let source = CGImageSourceCreateWithURL(url, [kCGImageSourceShouldCache: false] as CFDictionary) else { return nil }
        let options: [CFString: Any] = [
            kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceCreateThumbnailWithTransform: true,
            kCGImageSourceShouldCacheImmediately: true,
            kCGImageSourceThumbnailMaxPixelSize: max(pixels, 32),
        ]
        guard let cg = CGImageSourceCreateThumbnailAtIndex(source, 0, options as CFDictionary) else { return nil }
        return UIImage(cgImage: cg)
    }
}

/// An app's icon, or a placeholder while there is none.
///
/// The size belongs to whoever places it; the corners follow it, close to the ratio iOS uses for a home-screen icon,
/// so a large icon is not rounded like a small one. Android icons are square PNGs, and the rounded square is what
/// makes them look like apps here.
struct AppIcon: View {
    let path: String?
    var size: CGFloat = 40

    @Environment(\.displayScale) private var scale
    @State private var image: UIImage?

    private var corner: CGFloat { size * 0.225 }
    private var pixels: Int { Int((size * scale).rounded(.up)) }

    var body: some View {
        ZStack {
            placeholder
            if let image {
                Image(uiImage: image)
                    .resizable()
                    .interpolation(.high)
                    .aspectRatio(contentMode: .fill)
                    .transition(.opacity)
            }
        }
        .frame(width: size, height: size)
        .clipShape(RoundedRectangle(cornerRadius: corner, style: .continuous))
        .task(id: "\(path ?? "")|\(pixels)|\(IconCache.stamp(path))") { await load() }
    }

    /// Quiet, so a grid still arriving does not read as broken: the surface an icon sits on, and a faint glyph.
    private var placeholder: some View {
        ZStack {
            Color(uiColor: .tertiarySystemFill)
            if image == nil {
                Image(systemName: "app.dashed")
                    .font(.system(size: size * 0.38, weight: .light))
                    .foregroundStyle(.tertiary)
            }
        }
    }

    private func load() async {
        guard let path, FileManager.default.fileExists(atPath: path) else {
            image = nil
            return
        }
        if let hit = IconCache.shared.cached(path, pixels: pixels) {
            image = hit
            return
        }
        let decoded = await IconCache.shared.load(path, pixels: pixels)
        guard !Task.isCancelled else { return }
        // A fade only for an icon that arrives after the tile is on screen; a cached one is simply there.
        withAnimation(.easeOut(duration: 0.2)) { image = decoded }
    }
}
