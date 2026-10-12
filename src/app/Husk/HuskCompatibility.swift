// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

extension Animation {
    /// The iOS 17 animation, with a spring fallback for Husk's iOS 16 target.
    static var huskSnappy: Animation { huskSnappy(duration: 0.5) }

    static func huskSnappy(duration: Double) -> Animation {
        if #available(iOS 17.0, *) {
            return .snappy(duration: duration)
        }
        return .spring(response: duration, dampingFraction: 0.85)
    }
}
