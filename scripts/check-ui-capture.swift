// Reject launch-screen captures; no image transformation or device/game access.
import AppKit
import Foundation
guard ProcessInfo.processInfo.environment["GITHUB_ACTIONS"] == "true",
      CommandLine.arguments.count == 2,
      let data = try? Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[1])),
      let bitmap = NSBitmapImageRep(data: data) else { exit(1) }
var dark = 0, tinted = 0, total = 0
for row in 2..<38 {
    for column in 1..<19 {
        let x = bitmap.pixelsWide * column / 20, y = bitmap.pixelsHigh * row / 40
        guard let color = bitmap.colorAt(x: x, y: y)?.usingColorSpace(NSColorSpace.deviceRGB) else { exit(1) }
        let r = color.redComponent, g = color.greenComponent, b = color.blueComponent
        if max(r, max(g, b)) < 0.45 { dark += 1 }
        if g - r > 0.08 && g > 0.12 && b > r { tinted += 1 }
        total += 1
    }
}
guard dark * 2 > total, tinted >= 3 else {
    print("Capture is not the expected rendered dark NSP UI: dark=\(dark)/\(total), tint=\(tinted)")
    exit(1)
}
print("PASS: rendered dark UI capture \(URL(fileURLWithPath: CommandLine.arguments[1]).lastPathComponent)")
