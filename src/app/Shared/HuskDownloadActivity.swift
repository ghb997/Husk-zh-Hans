// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation
#if canImport(ActivityKit)
import ActivityKit

/// The Live Activity a download shows on the Lock Screen and in the Dynamic Island. Compiled into the app, which starts and updates
/// it, and into the widget extension, which draws it.
@available(iOS 16.1, *)
struct HuskDownloadAttributes: ActivityAttributes {
    struct ContentState: Codable, Hashable {
        /// What is downloading: the item's name, or how many items.
        var title: String
        var received: Int64
        var total: Int64
        /// When it should be done at the current speed. Between the app's updates (iOS wakes it only now and then while a background
        /// download runs) the bar runs on this, so it keeps moving.
        var finishBy: Date?
        var started: Date
        var finished: Bool
        var failed: Bool
        /// Bytes per second when the app last saw it, and files done of all, for the stats line.
        var speed: Double = 0
        var filesDone: Int = 0
        var filesTotal: Int = 0
    }
}
#endif
