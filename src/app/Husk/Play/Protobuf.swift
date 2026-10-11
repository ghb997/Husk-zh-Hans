// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation

/// Just enough of the protocol-buffer wire format to talk to Google Play: messages are built field by field and read back
/// as a tree of fields, without generated code. Google Play's messages are proto2, so every field is optional and anything
/// a reply carries that is not asked for is simply ignored.
struct ProtoWriter {
    private(set) var data = Data()

    private mutating func varint(_ v: UInt64) {
        var v = v
        while v >= 0x80 { data.append(UInt8(v & 0x7F) | 0x80); v >>= 7 }
        data.append(UInt8(v))
    }
    private mutating func key(_ field: Int, _ wire: Int) { varint(UInt64(field << 3 | wire)) }

    mutating func int(_ field: Int, _ v: Int64) { key(field, 0); varint(UInt64(bitPattern: v)) }
    mutating func int(_ field: Int, _ v: Int) { int(field, Int64(v)) }
    mutating func bool(_ field: Int, _ v: Bool) { int(field, v ? 1 : 0) }
    mutating func fixed64(_ field: Int, _ v: UInt64) {
        key(field, 1)
        var le = v.littleEndian
        withUnsafeBytes(of: &le) { data.append(contentsOf: $0) }
    }
    mutating func bytes(_ field: Int, _ v: Data) { key(field, 2); varint(UInt64(v.count)); data.append(v) }
    mutating func string(_ field: Int, _ v: String) { bytes(field, Data(v.utf8)) }
    mutating func message(_ field: Int, _ build: (inout ProtoWriter) -> Void) {
        var inner = ProtoWriter()
        build(&inner)
        bytes(field, inner.data)
    }
}

/// A decoded message: each field number with the values it carried, in order. Length-delimited values stay as bytes and are
/// read as a string or a nested message by whoever knows which it is.
struct ProtoMessage {
    enum Value {
        case varint(UInt64)
        case fixed64(UInt64)
        case fixed32(UInt32)
        case bytes(Data)
    }

    private(set) var fields: [Int: [Value]] = [:]

    init(_ data: Data) {
        var i = data.startIndex
        func readVarint() -> UInt64? {
            var result: UInt64 = 0, shift: UInt64 = 0
            while i < data.endIndex, shift < 64 {
                let b = data[i]; i += 1
                result |= UInt64(b & 0x7F) << shift
                if b & 0x80 == 0 { return result }
                shift += 7
            }
            return nil
        }
        while i < data.endIndex {
            guard let key = readVarint() else { break }
            let field = Int(key >> 3), wire = Int(key & 7)
            switch wire {
            case 0:
                guard let v = readVarint() else { return }
                fields[field, default: []].append(.varint(v))
            case 1:
                guard data.endIndex - i >= 8 else { return }
                var v: UInt64 = 0
                for k in 0..<8 { v |= UInt64(data[i + k]) << (8 * UInt64(k)) }
                i += 8
                fields[field, default: []].append(.fixed64(v))
            case 2:
                guard let n = readVarint(), n <= UInt64(data.endIndex - i) else { return }
                let end = i + Int(n)
                fields[field, default: []].append(.bytes(data.subdata(in: i..<end)))
                i = end
            case 3:
                // A proto2 group: its fields follow until the matching end tag, and are kept as a message of their own.
                let start = i
                var depth = 1
                while i < data.endIndex, depth > 0 {
                    let before = i
                    guard let k = readVarint() else { return }
                    switch Int(k & 7) {
                    case 0: _ = readVarint()
                    case 1: i += 8
                    case 2: if let n = readVarint() { i += Int(n) } else { return }
                    case 3: depth += 1
                    case 4: depth -= 1; if depth == 0 { fields[field, default: []].append(.bytes(data.subdata(in: start..<before))) }
                    case 5: i += 4
                    default: return
                    }
                }
            case 5:
                guard data.endIndex - i >= 4 else { return }
                var v: UInt32 = 0
                for k in 0..<4 { v |= UInt32(data[i + k]) << (8 * UInt32(k)) }
                i += 4
                fields[field, default: []].append(.fixed32(v))
            default:
                return
            }
        }
    }

    func message(_ field: Int) -> ProtoMessage? {
        guard case .bytes(let d)? = fields[field]?.first else { return nil }
        return ProtoMessage(d)
    }
    func messages(_ field: Int) -> [ProtoMessage] {
        (fields[field] ?? []).compactMap { if case .bytes(let d) = $0 { return ProtoMessage(d) } else { return nil } }
    }
    func string(_ field: Int) -> String? {
        guard case .bytes(let d)? = fields[field]?.first else { return nil }
        return String(data: d, encoding: .utf8)
    }
    func strings(_ field: Int) -> [String] {
        (fields[field] ?? []).compactMap { if case .bytes(let d) = $0 { return String(data: d, encoding: .utf8) } else { return nil } }
    }
    func bytes(_ field: Int) -> Data? {
        guard case .bytes(let d)? = fields[field]?.first else { return nil }
        return d
    }
    func int(_ field: Int) -> Int64? {
        switch fields[field]?.first {
        case .varint(let v)?: return Int64(bitPattern: v)
        case .fixed64(let v)?: return Int64(bitPattern: v)
        case .fixed32(let v)?: return Int64(v)
        default: return nil
        }
    }
    func uint64(_ field: Int) -> UInt64? {
        switch fields[field]?.first {
        case .varint(let v)?, .fixed64(let v)?: return v
        case .fixed32(let v)?: return UInt64(v)
        default: return nil
        }
    }
    func float(_ field: Int) -> Float? {
        guard case .fixed32(let v)? = fields[field]?.first else { return nil }
        return Float(bitPattern: v)
    }
    func has(_ field: Int) -> Bool { fields[field] != nil }
}
