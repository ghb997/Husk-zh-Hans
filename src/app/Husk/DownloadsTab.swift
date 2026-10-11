// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

/// Downloads from a link, and what has an update.
struct DownloadsTab: View {
    @ObservedObject private var downloads = Downloads.shared
    @ObservedObject private var updates = AppUpdates.shared
    @ObservedObject private var guest = GuestImage.shared
    @State private var adding = false
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            List {
                updatesSection
                Section {
                    if downloads.jobs.isEmpty {
                        VStack(alignment: .leading, spacing: 6) {
                            Text("暂无下载").font(.headline)
                            Text("添加 APK 或文件的下载链接，也可以输入电脑共享文件夹时提供的地址。")
                                .font(.subheadline).foregroundStyle(.secondary)
                        }
                        .padding(.vertical, 6)
                    }
                    ForEach(downloads.jobs) { job in
                        DownloadRow(job: job, speed: downloads.speed)
                            .swipeActions {
                                Button(role: .destructive) { downloads.remove(job.id) } label: { Label("移除", systemImage: "trash") }
                            }
                    }
                } header: {
                    HStack {
                        Text("下载")
                        Spacer()
                        if downloads.jobs.contains(where: \.complete) {
                            Button("清除已完成") { downloads.clearFinished() }.font(.caption).textCase(nil)
                        }
                    }
                } footer: {
                    VStack(alignment: .leading, spacing: 6) {
                        if downloads.liveActivitiesOff {
                            Text("Husk 的实时活动已关闭，下载进度不会显示在锁定屏幕或灵动岛上。请在“设置 › Husk › 实时活动”中开启。")
                                .foregroundStyle(.orange)
                        }
                        if let free = Downloads.freeSpace() { Text("此设备剩余 \(Downloads.bytes(free)) 可用空间。") }
                    }
                }
            }
            .navigationTitle("下载")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("完成") { dismiss() } }
                ToolbarItem(placement: .primaryAction) {
                    Button { adding = true } label: { Label("添加", systemImage: "plus") }
                }
            }
            .sheet(isPresented: $adding) { AddDownloadSheet() }
        }
    }

    @ViewBuilder private var updatesSection: some View {
        Section("更新") {
            if let release = updates.available {
                HStack {
                    Label("Husk \(release.version)", systemImage: "arrow.down.app.fill")
                    Spacer()
                    Button("查看") { UIApplication.shared.open(release.page) }.buttonStyle(.bordered)
                }
            }
            if guest.update.isSomething {
                HStack {
                    Label(guest.update.title, systemImage: "shippingbox.fill")
                    Spacer()
                    Button("下载") { guest.applyUpdate() }.buttonStyle(.bordered)
                }
            }
            if updates.available == nil && !guest.update.isSomething {
                Label("已是最新版本", systemImage: "checkmark.circle").foregroundStyle(.secondary)
            }
        }
    }
}

private struct DownloadRow: View {
    let job: Downloads.Job
    let speed: Double

    private var fraction: Double { job.total > 0 ? min(1, Double(job.received) / Double(job.total)) : 0 }

    private var status: String {
        if job.complete { return job.kind == .apk ? "已添加到应用库" : job.kind == .folder ? "已保存到共享存储" : "已保存到共享存储" }
        if job.paused { return "已暂停 · \(Downloads.bytes(job.received)) / \(Downloads.bytes(job.total))" }
        if let err = job.files.first(where: { $0.error != nil })?.error { return "已停止：\(err)" }
        var s = "\(Downloads.bytes(job.received)) / \(Downloads.bytes(job.total))"
        if job.files.count > 1 { s += " · \(job.filesDone)/\(job.files.count) 个文件" }
        if speed > 1 {
            s += " · \(Downloads.bytes(Int64(speed)))/s"
            let left = Double(job.total - job.received) / speed
            if left.isFinite, left > 0 {
                let f = DateComponentsFormatter(); var calendar = Calendar.current; calendar.locale = Locale(identifier: "zh_Hans_CN"); f.calendar = calendar; f.allowedUnits = left > 3600 ? [.hour, .minute] : [.minute, .second]; f.unitsStyle = .abbreviated
                if let t = f.string(from: left) { s += " · 还需 \(t)" }
            }
        }
        return s
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 10) {
                Image(systemName: job.kind == .folder ? "folder.fill" : job.kind == .apk ? "app.badge.fill" : "doc.fill")
                    .foregroundStyle(job.complete ? Color.green : Color.accentColor)
                Text(job.title).font(.headline).lineLimit(1)
                Spacer()
                if !job.complete {
                    Button {
                        if job.paused || job.failed { Downloads.shared.resume(job.id) } else { Downloads.shared.pause(job.id) }
                    } label: {
                        Image(systemName: job.paused || job.failed ? "arrow.clockwise.circle.fill" : "pause.circle.fill").font(.title2)
                    }
                    .buttonStyle(.borderless)
                }
            }
            if !job.complete { ProgressView(value: fraction) }
            Text(status).font(.caption).foregroundStyle(job.failed ? Color.orange : Color.secondary).lineLimit(2)
        }
        .padding(.vertical, 4)
    }
}

private struct AddDownloadSheet: View {
    @Environment(\.dismiss) private var dismiss
    @ObservedObject private var downloads = Downloads.shared
    @State private var link = ""
    @State private var working = false

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    TextField("https://… 或 http://192.168.1.20:8642/", text: $link)
                        .keyboardType(.URL)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                } footer: {
                    if let problem = downloads.problem { Text(problem).foregroundStyle(.orange) }
                }
                Section {
                    Text("APK 下载完成后会添加到应用库。其他文件会放入“共享存储”文件夹，游戏会将其识别为手机存储。")
                    Text("要从同一 Wi-Fi 下的电脑传输整个文件夹，请在电脑上运行以下命令，然后输入显示的地址：")
                    Text("python3 serve-folder.py <folder>")
                        .font(.system(.footnote, design: .monospaced))
                        .textSelection(.enabled)
                    Text("serve-folder.py 位于 Husk 仓库的 tools 目录中。文件夹中的内容会放到“共享存储”的根目录，请共享包含游戏所需文件的文件夹。")
                } header: {
                    Text("使用说明")
                }
                .font(.footnote)
                .foregroundStyle(.secondary)
            }
            .navigationTitle("添加下载")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("取消") { dismiss() } }
                ToolbarItem(placement: .confirmationAction) {
                    if working { ProgressView() } else {
                        Button("下载") {
                            working = true
                            Task {
                                await downloads.add(link: link)
                                working = false
                                if downloads.problem == nil { dismiss() }
                            }
                        }
                        .disabled(link.trimmingCharacters(in: .whitespaces).isEmpty)
                    }
                }
            }
            .onAppear { downloads.problem = nil }
        }
    }
}
