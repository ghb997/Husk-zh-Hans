// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation
import GameController

/// Game controllers for the native runtime's games.
///
/// iOS's GameController framework does the hard part: a controller is paired in Settings › Bluetooth (or is wired, or MFi) and shows up here.
/// This reads each one and reports it to the runtime (husk_gamepad_*), which shows it to the running game as an Xbox-style Android controller:
/// buttons as key events, the sticks, triggers and D-pad as joystick motion. Only changes are sent on, so polling fast costs little.
@MainActor
final class HuskGamepads: ObservableObject {
    static let shared = HuskGamepads()

    /// The controllers connected now, by the name iOS gives them, for the screen to show.
    @Published private(set) var names: [String] = []

    /// The bits of the button mask (husk-tl-gamepad.h, TL_PAD_*).
    private enum Bit: UInt32 {
        case a = 0, b, x, y, l1, r1, thumbL, thumbR, start, select, mode, dpadUp, dpadDown, dpadLeft, dpadRight
        var mask: UInt32 { 1 << rawValue }
    }
    private static let slots = 4

    private var slotOf: [ObjectIdentifier: Int] = [:]
    private var controllerIn: [Int: GCController] = [:]
    private var timer: Timer?
    private var started = false

    private init() {}

    /// Begin watching. Safe to call again.
    func start() {
        guard !started else { return }
        started = true
        // Input while Husk is in the background is not wanted: the game is paused then too.
        GCController.shouldMonitorBackgroundEvents = false
        NotificationCenter.default.addObserver(forName: .GCControllerDidConnect, object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.sync() }
        }
        NotificationCenter.default.addObserver(forName: .GCControllerDidDisconnect, object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.sync() }
        }
        sync()
    }

    /// Match the runtime's controller slots to what iOS reports now.
    private func sync() {
        let present = GCController.controllers().filter { $0.extendedGamepad != nil }
        for (id, slot) in slotOf where !present.contains(where: { ObjectIdentifier($0) == id }) {
            husk_gamepad_disconnect(Int32(slot))
            controllerIn[slot] = nil
            slotOf[id] = nil
            HuskLog.log("pad", "controller \(slot) disconnected")
        }
        for controller in present where slotOf[ObjectIdentifier(controller)] == nil {
            guard let slot = (0..<Self.slots).first(where: { controllerIn[$0] == nil }) else { break }
            slotOf[ObjectIdentifier(controller)] = slot
            controllerIn[slot] = controller
            let name = controller.vendorName ?? controller.productCategory
            husk_gamepad_connect(Int32(slot), name)
            HuskLog.log("pad", "controller \(slot) connected: \(name) (\(controller.productCategory))")
        }
        names = (0..<Self.slots).compactMap { controllerIn[$0]?.vendorName ?? controllerIn[$0]?.productCategory }
        if controllerIn.isEmpty {
            timer?.invalidate(); timer = nil
        } else if timer == nil {
            // 120 times a second, on the main run loop in every mode so that scrolling elsewhere does not stop it.
            let t = Timer(timeInterval: 1.0 / 120.0, repeats: true) { [weak self] _ in
                MainActor.assumeIsolated { self?.poll() }
            }
            RunLoop.main.add(t, forMode: .common)
            timer = t
        }
    }

    private func poll() {
        for (slot, controller) in controllerIn {
            guard let pad = controller.extendedGamepad else { continue }
            var buttons: UInt32 = 0
            func set(_ bit: Bit, _ input: GCControllerButtonInput?) { if input?.isPressed == true { buttons |= bit.mask } }
            set(.a, pad.buttonA); set(.b, pad.buttonB); set(.x, pad.buttonX); set(.y, pad.buttonY)
            set(.l1, pad.leftShoulder); set(.r1, pad.rightShoulder)
            set(.thumbL, pad.leftThumbstickButton); set(.thumbR, pad.rightThumbstickButton)
            set(.start, pad.buttonMenu); set(.select, pad.buttonOptions); set(.mode, pad.buttonHome)
            set(.dpadUp, pad.dpad.up); set(.dpadDown, pad.dpad.down); set(.dpadLeft, pad.dpad.left); set(.dpadRight, pad.dpad.right)
            husk_gamepad_update(Int32(slot), buttons,
                                pad.leftThumbstick.xAxis.value, pad.leftThumbstick.yAxis.value,
                                pad.rightThumbstick.xAxis.value, pad.rightThumbstick.yAxis.value,
                                pad.leftTrigger.value, pad.rightTrigger.value)
        }
    }
}
