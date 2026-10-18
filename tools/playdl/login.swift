// SPDX-License-Identifier: GPL-2.0-or-later
// playdl login: Google's own sign-in page in a window, as Husk's GoogleSignInSheet shows it. The page leaves an oauth_token cookie,
// which is traded for the account token; the password never passes through this tool.
import AppKit
import WebKit

final class LoginWindow: NSObject, WKNavigationDelegate, NSWindowDelegate {
    private let window: NSWindow
    private let web: WKWebView
    private var timer: Timer?
    private var captured = false
    private let done: (String?, String?) -> Void

    init(done: @escaping (_ email: String?, _ oauthToken: String?) -> Void) {
        self.done = done
        let config = WKWebViewConfiguration()
        config.websiteDataStore = .nonPersistent()
        web = WKWebView(frame: NSRect(x: 0, y: 0, width: 480, height: 720), configuration: config)
        web.customUserAgent = PlayDevice.authUserAgent
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 480, height: 720), styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
        super.init()
        window.title = "Sign in to Google Play (Husk downloader)"
        window.contentView = web
        window.delegate = self
        web.navigationDelegate = self
        window.center()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        web.load(URLRequest(url: URL(string: "https://accounts.google.com/EmbeddedSetup")!))
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in self?.check() }
    }

    func webView(_ webView: WKWebView, didStartProvisionalNavigation navigation: WKNavigation!) { log("loading \(webView.url?.absoluteString ?? "?")") }
    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) { log("loaded \(webView.url?.absoluteString ?? "?")"); check() }
    func webView(_ webView: WKWebView, didFail navigation: WKNavigation!, withError error: Error) { log("failed: \(error)") }
    func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) { log("failed to load: \(error)") }
    func webViewWebContentProcessDidTerminate(_ webView: WKWebView) { log("the web content process died; reloading"); webView.reload() }

    private func log(_ s: String) { FileHandle.standardError.write(Data("login: \(s)\n".utf8)) }

    func windowWillClose(_ notification: Notification) { if !captured { finish(nil, nil) } }

    private func check() {
        guard !captured else { return }
        web.configuration.websiteDataStore.httpCookieStore.getAllCookies { [weak self] cookies in
            guard let self, !self.captured, let token = cookies.first(where: { $0.name == "oauth_token" && !$0.value.isEmpty })?.value else { return }
            self.captured = true
            self.web.evaluateJavaScript("document.querySelector('[data-email]')?.getAttribute('data-email') || document.body.innerText") { result, _ in
                let text = result as? String ?? ""
                let email = text.range(of: "[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}", options: .regularExpression).map { String(text[$0]) }
                self.finish(email, token)
            }
        }
    }

    private func finish(_ email: String?, _ token: String?) {
        timer?.invalidate()
        window.orderOut(nil)
        done(email, token)
    }
}

/// Shows the window, waits for the sign-in, and returns the email (as the page shows it) and the oauth_token.
func runLoginWindow() -> (email: String?, token: String?) {
    let app = NSApplication.shared
    app.setActivationPolicy(.regular)
    var result: (String?, String?) = (nil, nil)
    var keep: LoginWindow?
    keep = LoginWindow { email, token in
        result = (email, token)
        keep = nil
        app.stop(nil)
        app.postEvent(NSEvent.otherEvent(with: .applicationDefined, location: .zero, modifierFlags: [], timestamp: 0, windowNumber: 0, context: nil, subtype: 0, data1: 0, data2: 0)!, atStart: true)
    }
    app.run()
    return result
}
