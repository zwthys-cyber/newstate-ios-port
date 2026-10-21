// Render original vector branding on the GitHub macOS runner using AppKit.
import AppKit
import Foundation

guard ProcessInfo.processInfo.environment["GITHUB_ACTIONS"] == "true",
      CommandLine.arguments.count == 2 else { exit(1) }
let destination = URL(fileURLWithPath: CommandLine.arguments[1])
try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
let slots: [(Int, String, String)] = [(60,"2x","iphone"),(60,"3x","iphone"),
    (76,"1x","ipad"),(76,"2x","ipad"),(83,"2x","ipad"),(1024,"1x","ios-marketing")]
var images: [[String: String]] = []
for (points, scale, idiom) in slots {
    // iPad Pro uses 83.5pt @2x.
    let dimension = points == 83 ? 167 : points * (scale == "2x" ? 2 : scale == "3x" ? 3 : 1)
    let filename = "Icon-\(dimension).png"
    let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: dimension, pixelsHigh: dimension,
        bitsPerSample: 8, samplesPerPixel: 3, hasAlpha: false, isPlanar: false,
        colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
    let factor = CGFloat(dimension) / 1024
    let transform = NSAffineTransform(); transform.scale(by: factor); transform.concat()
    NSColor(srgbRed: 0.035, green: 0.055, blue: 0.085, alpha: 1).setFill()
    NSBezierPath(rect: NSRect(x: 0, y: 0, width: 1024, height: 1024)).fill()
    let mint = NSColor(srgbRed: 0.25, green: 0.88, blue: 0.79, alpha: 1)
    mint.withAlphaComponent(0.12).setStroke()
    let ring = NSBezierPath(ovalIn: NSRect(x: 128, y: 128, width: 768, height: 768)); ring.lineWidth = 2; ring.stroke()
    mint.setStroke()
    for (x, y, dx, dy) in [(190.0, 190.0, 1.0, 1.0),(834,190,-1,1),(190,834,1,-1),(834,834,-1,-1)] {
        let corner = NSBezierPath(); corner.move(to: NSPoint(x: x, y: y + dy * 110))
        corner.line(to: NSPoint(x: x, y: y)); corner.line(to: NSPoint(x: x + dx * 110, y: y))
        corner.lineWidth = 20; corner.lineCapStyle = .round; corner.lineJoinStyle = .round; corner.stroke()
    }
    let paragraph = NSMutableParagraphStyle(); paragraph.alignment = .center
    let word = "NSP" as NSString
    word.draw(in: NSRect(x: 160, y: 350, width: 704, height: 280), withAttributes: [
        .font: NSFont.systemFont(ofSize: 244, weight: .heavy), .foregroundColor: NSColor.white, .paragraphStyle: paragraph])
    ("P R O" as NSString).draw(in: NSRect(x: 200, y: 275, width: 624, height: 70), withAttributes: [
        .font: NSFont.systemFont(ofSize: 42, weight: .semibold), .foregroundColor: mint, .paragraphStyle: paragraph])
    NSGraphicsContext.restoreGraphicsState()
    try bitmap.representation(using: .png, properties: [:])!.write(to: destination.appendingPathComponent(filename))
    images.append(["idiom": idiom, "size": points == 83 ? "83.5x83.5" : "\(points)x\(points)", "scale": scale, "filename": filename])
}
let contents: [String: Any] = ["images": images, "info": ["version": 1, "author": "NSP"]]
try JSONSerialization.data(withJSONObject: contents, options: [.prettyPrinted, .sortedKeys])
    .write(to: destination.appendingPathComponent("Contents.json"))
