// Minimal logging shim for the ported FLUX.2 Klein pipeline.
//
// The upstream flux-2-swift-mlx code logs through a `Flux2Debug` helper; we keep
// the call sites intact but route them to stderr, gated by an env var so the
// benchmark stays quiet by default. Enable with FLUX2_DEBUG=1 (or =verbose).

import Foundation

enum Flux2Debug {
    private static let level: Int = {
        switch ProcessInfo.processInfo.environment["FLUX2_DEBUG"]?.lowercased() {
        case "verbose", "2": return 2
        case nil, "", "0", "false": return 0
        default: return 1
        }
    }()

    static func log(_ message: @autoclosure () -> String) {
        if level >= 1 { FileHandle.standardError.write(Data(("[flux2] " + message() + "\n").utf8)) }
    }

    static func warning(_ message: @autoclosure () -> String) {
        if level >= 1 { FileHandle.standardError.write(Data(("[flux2][warn] " + message() + "\n").utf8)) }
    }

    static func verbose(_ message: @autoclosure () -> String) {
        if level >= 2 { FileHandle.standardError.write(Data(("[flux2] " + message() + "\n").utf8)) }
    }
}
