// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation

/// Codable messages shared by Husk and its JIT helper extension. Compiled into
/// both targets. Each travels as JSON in an extension request's item
/// (userInfo[itemKey]), to the helper and back.
struct HuskJITRequest: Codable, Sendable {
    static let itemKey = "husk-jit-request"

    enum Operation: String, Codable, Sendable {
        /// Check LocalDevVPN and download, mount and verify the Developer Disk Image.
        case prepare
        /// Attach to `targetPID` and run the JIT script until it detaches.
        case enable
        case resetDDI
    }

    let operation: Operation
    let targetPID: Int32?
    let pairingData: Data?
    let scriptBase64: String?

    static func prepare(pairingData: Data) -> HuskJITRequest {
        HuskJITRequest(operation: .prepare, targetPID: nil,
                       pairingData: pairingData, scriptBase64: nil)
    }

    static func enable(targetPID: Int32, pairingData: Data,
                       scriptBase64: String) -> HuskJITRequest {
        HuskJITRequest(operation: .enable, targetPID: targetPID,
                       pairingData: pairingData, scriptBase64: scriptBase64)
    }

    static var resetDDI: HuskJITRequest {
        HuskJITRequest(operation: .resetDDI, targetPID: nil,
                       pairingData: nil, scriptBase64: nil)
    }

    struct Response: Codable, Sendable {
        static let itemKey = "husk-jit-response"
        let success: Bool
        let message: String
        let txmPresent: Bool?
    }
}
