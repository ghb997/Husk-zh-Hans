// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation

/// A PS4 game Husk can run: an extracted game folder (eboot.bin and sce_sys/) under Documents/PS4 Games, used where it is.
/// Nothing is copied; a folder named like CUSA07023-UPDATE beside it is the game's patch, which the PS4 layer picks up itself.
struct PS4Game: Identifiable, Hashable {
    let titleID: String          // CUSA07023
    let title: String
    let version: String?
    let folder: URL

    var id: String { titleID }
    /// The library and Close Husk to Play key: one namespace with the Android games' ids.
    var libraryID: String { "ps4:" + titleID }
    var eboot: URL { folder.appendingPathComponent("eboot.bin") }
    var iconPath: String? { existing("sce_sys/icon0.png") }
    var artPath: String? { existing("sce_sys/pic1.png") ?? existing("sce_sys/pic0.png") }

    /// Husk's own files for the game: its log, when it was last played.
    var dataDir: URL { TranslationLayer.root.appendingPathComponent("ps4-" + titleID) }

    var lastPlayed: Date? {
        (try? String(contentsOf: dataDir.appendingPathComponent("last-played.txt"), encoding: .utf8))
            .flatMap { Double($0.trimmingCharacters(in: .whitespacesAndNewlines)) }
            .map { Date(timeIntervalSince1970: $0) }
    }

    func markPlayed() {
        try? FileManager.default.createDirectory(at: dataDir, withIntermediateDirectories: true)
        try? String(Date().timeIntervalSince1970).write(to: dataDir.appendingPathComponent("last-played.txt"), atomically: true, encoding: .utf8)
    }

    private func existing(_ rel: String) -> String? {
        let p = folder.appendingPathComponent(rel).path
        return FileManager.default.fileExists(atPath: p) ? p : nil
    }
}

/// The PS4 games in Documents/PS4 Games.
@MainActor final class PS4Store: ObservableObject {
    static let shared = PS4Store()

    /// Visible in Files as "PS4 Games": drop an extracted game folder in.
    nonisolated static var root: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("PS4 Games", isDirectory: true)
    }
    /// The PS4 layer's own data, shared by every game: its settings, saves, system modules and shader cache.
    static var userDir: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("PS4", isDirectory: true)
    }

    /// The PS4 layer is in this build and can run on this iOS (it needs 17.4, for FEXCore's address waits).
    static var available: Bool {
        guard #available(iOS 17.4, *) else { return false }
        return moduleURL != nil
    }
    static var moduleURL: URL? {
        guard let fw = Bundle.main.privateFrameworksPath else { return nil }
        let url = URL(fileURLWithPath: fw).appendingPathComponent("libhuskps4.dylib")
        return FileManager.default.fileExists(atPath: url.path) ? url : nil
    }

    @Published private(set) var games: [PS4Game] = []

    private init() { reload() }

    func reload() {
        let fm = FileManager.default
        try? fm.createDirectory(at: Self.root, withIntermediateDirectories: true)
        func list(_ dir: URL) -> [URL] {
            (try? fm.contentsOfDirectory(at: dir, includingPropertiesForKeys: [.isDirectoryKey], options: [.skipsHiddenFiles])) ?? []
        }
        // Game folders, and one level down: Files unzips an archive of game folders into a folder named after it.
        var folders: [URL] = []
        for top in list(Self.root) {
            if fm.fileExists(atPath: top.appendingPathComponent("eboot.bin").path) { folders.append(top) }
            else { folders += list(top) }
        }
        var found: [PS4Game] = []
        for folder in folders where !folder.lastPathComponent.uppercased().hasSuffix("-UPDATE") {
            guard fm.fileExists(atPath: folder.appendingPathComponent("eboot.bin").path),
                  let sfo = ParamSfo.parse(from: folder.appendingPathComponent("sce_sys/param.sfo")) else { continue }
            let id = sfo.titleID ?? folder.lastPathComponent
            found.append(PS4Game(titleID: id, title: sfo.title ?? id, version: sfo.appVersion, folder: folder))
        }
        found.sort { $0.title.localizedStandardCompare($1.title) == .orderedAscending }
        if found != games { games = found }
    }

    func game(_ titleID: String) -> PS4Game? { games.first { $0.titleID == titleID } }

    /// The folder in PS4 Games holding the game with this title ID, looked up on disk (any thread).
    nonisolated static func folder(of titleID: String) -> URL? {
        let fm = FileManager.default
        func list(_ dir: URL) -> [URL] {
            (try? fm.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil, options: [.skipsHiddenFiles])) ?? []
        }
        for top in list(root) {
            for folder in [top] + list(top) where !folder.lastPathComponent.uppercased().hasSuffix("-UPDATE") {
                guard fm.fileExists(atPath: folder.appendingPathComponent("eboot.bin").path) else { continue }
                if ParamSfo.parse(from: folder.appendingPathComponent("sce_sys/param.sfo"))?.titleID == titleID { return folder }
            }
        }
        return nil
    }
}

/// A PS4 param.sfo (PSF): the title, its ID and version.
struct ParamSfo {
    var title: String?
    var titleID: String?
    var appVersion: String?
    /// "gd" for a game, "gp" for an update.
    var category: String?

    static func parse(from url: URL) -> ParamSfo? {
        guard let data = try? Data(contentsOf: url), data.count >= 0x14 else { return nil }
        func u32(_ o: Int) -> Int { o + 4 <= data.count ? Int(data.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: o, as: UInt32.self) }) : 0 }
        func u16(_ o: Int) -> Int { o + 2 <= data.count ? Int(data.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: o, as: UInt16.self) }) : 0 }
        guard u32(0) == 0x4653_5000 else { return nil }   // "\0PSF"
        let keys = u32(0x08), values = u32(0x0C), count = u32(0x10)
        var sfo = ParamSfo()
        for i in 0..<min(count, 512) {
            let e = 0x14 + i * 16
            guard e + 16 <= data.count else { break }
            let keyStart = keys + u16(e), format = u16(e + 2), length = u32(e + 4), valueStart = values + u32(e + 12)
            guard keyStart < data.count, format == 0x0204, valueStart + length <= data.count else { continue }   // UTF-8 strings only
            let keyEnd = data[keyStart...].firstIndex(of: 0) ?? data.count
            guard let key = String(data: data[keyStart..<keyEnd], encoding: .utf8),
                  let value = String(data: data[valueStart..<valueStart + length], encoding: .utf8)?.trimmingCharacters(in: CharacterSet(charactersIn: "\0"))
            else { continue }
            switch key {
            case "TITLE": sfo.title = value
            case "TITLE_ID": sfo.titleID = value
            case "APP_VER": sfo.appVersion = value
            case "CATEGORY": sfo.category = value
            default: break
            }
        }
        return sfo
    }
}
