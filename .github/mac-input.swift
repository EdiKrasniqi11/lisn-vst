// Synthetic mouse input and a brightness reading for the macos job's checks of the desktop app's window.
//   mac-input click X Y          a left click at screen point X, Y
//   mac-input drag X Y DX DY     press at X, Y, move by DX, DY in steps, release
//   mac-input lum FILE.png       the image's mean luminance, 0 to 255
import Cocoa

func post(_ type: CGEventType, _ x: Double, _ y: Double) {
    CGEvent(mouseEventSource: nil, mouseType: type, mouseCursorPosition: CGPoint(x: x, y: y), mouseButton: .left)?.post(tap: .cghidEventTap)
    usleep(20000)
}

let a = CommandLine.arguments
func n(_ i: Int) -> Double { Double(a[i]) ?? 0 }

switch a.count > 1 ? a[1] : "" {
case "click":
    post(.mouseMoved, n(2), n(3)); usleep(200000)
    post(.leftMouseDown, n(2), n(3)); usleep(80000)
    post(.leftMouseUp, n(2), n(3))
case "drag":
    post(.mouseMoved, n(2), n(3)); usleep(200000)
    post(.leftMouseDown, n(2), n(3)); usleep(100000)
    for i in 1...20 { post(.leftMouseDragged, n(2) + n(4) * Double(i) / 20, n(3) + n(5) * Double(i) / 20); usleep(15000) }
    usleep(100000)
    post(.leftMouseUp, n(2) + n(4), n(3) + n(5))
case "lum":
    guard let src = CGImageSourceCreateWithURL(URL(fileURLWithPath: a[2]) as CFURL, nil),
          let img = CGImageSourceCreateImageAtIndex(src, 0, nil) else { print("unreadable"); exit(1) }
    let w = img.width, h = img.height
    var buf = [UInt8](repeating: 0, count: w * h * 4)
    buf.withUnsafeMutableBytes { p in
        let ctx = CGContext(data: p.baseAddress, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                            space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        ctx.draw(img, in: CGRect(x: 0, y: 0, width: w, height: h))
    }
    var sum = 0.0
    for i in stride(from: 0, to: buf.count, by: 4) { sum += 0.299 * Double(buf[i]) + 0.587 * Double(buf[i + 1]) + 0.114 * Double(buf[i + 2]) }
    print(String(format: "%.1f", sum / Double(w * h)))
default:
    print("usage: click X Y | drag X Y DX DY | lum FILE"); exit(2)
}
