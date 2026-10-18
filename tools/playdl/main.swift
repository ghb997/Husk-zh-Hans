// SPDX-License-Identifier: GPL-2.0-or-later
// playdl: Husk's own Play Store client (src/app/Husk/Play) on the Mac, for compatibility testing.
//   playdl get <out-dir> <package>...   base APK + splits into <out-dir>/<package>/
//   playdl search <query>                package names and titles
//   playdl browse                        the store's front page
//   playdl login                         sign in with a Google account (a window shows Google's page) instead of the anonymous dispenser
// An anonymous session from Aurora's token dispenser, cached in ~/.husk-playdl.json.
import Foundation

let sessionCache = URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent(".husk-playdl.json")

func save(_ s: PlaySession) {
    guard let d = try? JSONEncoder().encode(s) else { return }
    try? d.write(to: sessionCache, options: .atomic)
    chmod(sessionCache.path, 0o600)
}

/// A signed-in session (playdl login) lasts: only its Play token is renewed, hourly. Without one, Aurora's anonymous dispenser.
func session() async throws -> PlaySession {
    if let d = try? Data(contentsOf: sessionCache), var s = try? JSONDecoder().decode(PlaySession.self, from: d) {
        let age = (try? FileManager.default.attributesOfItem(atPath: sessionCache.path)[.modificationDate] as? Date).map { Date().timeIntervalSince($0) } ?? .infinity
        if age < 3600 { return s }
        if !s.aasToken.isEmpty {
            s.bearer = try await PlayAPI.playToken(s)
            save(s)
            return s
        }
    }
    let s = try await PlayAPI.fetchAuroraGuestSession()
    save(s)
    return s
}

/// playdl login: sign in on Google's page, then register this downloader as a device, as PlayStoreManager.signIn does.
func login(email: String?, token: String?) async -> Int32 {
    guard let token else { print("login: the window was closed before signing in"); return 1 }
    guard let email else { print("login: signed in, but the page did not show the account's email"); return 1 }
    do {
        var s = PlaySession(email: email, aasToken: try await PlayAPI.accountToken(email: email, oauthToken: token))
        (s.gsfID, s.consistencyToken) = try await PlayAPI.checkin()
        s.bearer = try await PlayAPI.playToken(s)
        s.deviceConfigToken = try await PlayAPI.uploadDeviceConfig(s)
        save(s)
        print("login: signed in as \(email); the session is in \(sessionCache.path)")
        return 0
    } catch { print("login: \(error.localizedDescription)"); return 1 }
}

func run() async -> Int32 {
    let args = Array(CommandLine.arguments.dropFirst())
    guard let cmd = args.first else { print("usage: playdl login | get <dir> <pkg>... | search <q> | browse"); return 2 }
    do {
        let s = try await session()
        switch cmd {
        case "search":
            for a in try await PlayAPI.search(query: args.dropFirst().joined(separator: " "), session: s) { print("\(a.id)\t\(a.title)") }
        case "purchase":
            for pkg in args.dropFirst() {
                guard let app = try await PlayAPI.details(packageName: pkg, session: s) else { print("\(pkg)\tno details"); continue }
                do { print("\(pkg)\tvc \(app.versionCode)\tdelivery token \(try await PlayAPI.purchase(packageName: pkg, versionCode: app.versionCode, session: s) != nil)") }
                catch { print("\(pkg)\tpurchase failed: \(error)") }
            }
        case "browse":
            for a in try await PlayAPI.browse(session: s) { print("\(a.id)\t\(a.title)") }
        case "get":
            guard args.count >= 3 else { print("usage: playdl get <dir> <pkg>..."); return 2 }
            let out = URL(fileURLWithPath: args[1])
            var rc: Int32 = 0
            for pkg in args.dropFirst(2) {
                do {
                    guard let app = try await PlayAPI.details(packageName: pkg, session: s) else { print("\(pkg)\tFAIL no details"); rc = 1; continue }
                    let files = try await PlayAPI.downloadApp(packageName: pkg, versionCode: app.versionCode, session: s, stagingDirectory: out)
                    let dir = out.appendingPathComponent(pkg)
                    try? FileManager.default.removeItem(at: dir)
                    try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
                    for f in files {
                        // expansion files go to obb/ beside the APKs, where the runtime looks for them
                        var to = dir
                        if f.pathExtension.lowercased() == "obb" {
                            to = dir.appendingPathComponent("obb", isDirectory: true)
                            try FileManager.default.createDirectory(at: to, withIntermediateDirectories: true)
                        }
                        try FileManager.default.moveItem(at: f, to: to.appendingPathComponent(f.lastPathComponent))
                    }
                    if let staging = files.first?.deletingLastPathComponent() { try? FileManager.default.removeItem(at: staging) }
                    print("\(pkg)\tOK vc \(app.versionCode)\t\(files.map { $0.lastPathComponent }.joined(separator: " "))")
                } catch { print("\(pkg)\tFAIL \(error.localizedDescription)"); rc = 1 }
            }
            return rc
        default: print("unknown command \(cmd)"); return 2
        }
    } catch { print("error: \(error.localizedDescription)"); return 1 }
    return 0
}

// login's window runs on the main thread outside any dispatch block: WebKit's callbacks come through the main queue, which a
// window run from inside a main-queue block would never drain (the page stayed grey)
if CommandLine.arguments.dropFirst().first == "login" {
    let (email, token) = runLoginWindow()
    Task { exit(await login(email: email, token: token)) }
} else {
    Task { exit(await run()) }
}
RunLoop.main.run()
