import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

// Idle centerpiece: a soft, gently-morphing accent blob shown when nothing is
// being sent and there is no incoming transfer. Incoming/outgoing transfers are
// rendered as rows (see FileShareTray.qml), so this component is purely idle art.
Item {
    id: blobRoot
    anchors.fill: parent

    readonly property color textPrimary: Theme.textPrimary
    readonly property color textMuted: Theme.textMuted
    readonly property bool isSendMode: fileShareController.pendingSendFilePath.length > 0

    // Only animate when the blob is actually on a visible window. The three
    // canvases repaint at ~60fps each; left running while hidden in the tray or
    // during a send they burn CPU for nothing (sluggish cursor). Gate on it.
    readonly property bool animating: blobRoot.visible
                                      && Window.visibility !== Window.Hidden
                                      && Window.visibility !== Window.Minimized

    // Calm idle animation (no receive-intensity ramp — the blob is never shown
    // during an active transfer).
    readonly property real blobChaos: 0

    // Blob fill derived from the accent: an accent tint over the panel surface,
    // so the blob follows both the accent choice and light/dark mode. The three
    // canvas layers stack lightest→deepest. Called from onPaint (which repaints
    // every frame), so theme changes apply on the next tick with no extra wiring.
    function blobHex(alpha) {
        var acc = Theme.accentColor
        var base = Theme.dark ? Theme.surface : Qt.rgba(1, 1, 1, 1)
        return Qt.tint(base, Qt.rgba(acc.r, acc.g, acc.b, alpha)).toString()
    }

    Label {
        x: 48; y: 48
        visible: fileShareController.running
                 && fileShareController.transfers.length === 0
        text: isSendMode
              ? "Ready to send"
              : "Ready to receive"
        font.pixelSize: 20
        font.weight: Font.Medium
        color: textPrimary
    }

    Canvas {
        id: blobCanvas3
        width: 380; height: 380
        anchors.centerIn: parent
        visible: !isSendMode

        property real t: 0
        property double lastMs: Date.now()

        Timer {
            interval: 16
            running: blobRoot.animating
            repeat: true
            onTriggered: {
                var now = Date.now()
                var dt = Math.min(0.05, Math.max(0.0, (now - blobCanvas3.lastMs) * 0.001))
                blobCanvas3.lastMs = now
                blobCanvas3.t += dt * (Math.PI * 2 / Math.max(4.6, 8.0 - blobChaos * 2.6))
                blobCanvas3.requestPaint()
            }
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            var chaos = blobChaos
            var cx = width / 2 + Math.sin(t * 0.33) * chaos * 10
            var cy = height / 2 + Math.cos(t * 0.27) * chaos * 8
            var n = 10
            var pts = []

            for (var i = 0; i < n; i++) {
                var a = (i / n) * Math.PI * 2 - Math.PI / 2
                var r = 150 + chaos * 22
                    + Math.sin(a * 2 + t)               * (11 + chaos * 14)
                    + Math.cos(a * 3 - t * 0.2)         * (8 + chaos * 10)
                    + Math.sin(a * 1.5 + t * 0.7)       * (6 + chaos * 8)
                    + Math.sin(a * 1.5 + t)             * (4 + chaos * 6)
                    + Math.cos(a * 5 - t * 1.2)         * (chaos * 9)
                    + Math.sin(a * 7 + t * 0.85)        * (chaos * 6)
                pts.push({ x: cx + Math.cos(a) * r, y: cy + Math.sin(a) * r })
            }

            var len = pts.length
            ctx.beginPath()
            for (var j = 0; j < len; j++) {
                var p0 = pts[(j - 1 + len) % len]
                var p1 = pts[j]
                var p2 = pts[(j + 1) % len]
                var p3 = pts[(j + 2) % len]
                var cp1x = p1.x + (p2.x - p0.x) / 6
                var cp1y = p1.y + (p2.y - p0.y) / 6
                var cp2x = p2.x - (p3.x - p1.x) / 6
                var cp2y = p2.y - (p3.y - p1.y) / 6
                if (j === 0) ctx.moveTo(p1.x, p1.y)
                ctx.bezierCurveTo(cp1x, cp1y, cp2x, cp2y, p2.x, p2.y)
            }
            ctx.closePath()

            var grad = ctx.createRadialGradient(cx - 40, cy - 40, 0, cx, cy, 150)
            grad.addColorStop(0, blobRoot.blobHex(Theme.dark ? 0.16 : 0.11))
            ctx.fillStyle = grad
            ctx.fill()
        }
    }

    Canvas {
        id: blobCanvas2
        width: 380; height: 380
        anchors.centerIn: parent
        visible: !isSendMode

        property real t: 0
        property double lastMs: Date.now()

        Timer {
            interval: 16
            running: blobRoot.animating
            repeat: true
            onTriggered: {
                var now = Date.now()
                var dt = Math.min(0.05, Math.max(0.0, (now - blobCanvas2.lastMs) * 0.001))
                blobCanvas2.lastMs = now
                blobCanvas2.t += dt * (Math.PI * 2 / Math.max(4.9, 8.0 - blobChaos * 2.2))
                blobCanvas2.requestPaint()
            }
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            var chaos = blobChaos
            var cx = width / 2 + Math.cos(t * 0.29) * chaos * 8
            var cy = height / 2 + Math.sin(t * 0.41) * chaos * 11
            var n = 10
            var pts = []

            for (var i = 0; i < n; i++) {
                var a = (i / n) * Math.PI * 2 - Math.PI / 2
                var r = 140 + chaos * 18
                    + Math.sin(a * 2 + t)               * (11 + chaos * 12)
                    + Math.cos(a * 3 - t * 0.8)         * (8 + chaos * 9)
                    + Math.sin(a * 1.5 + t * 0.23)      * (6 + chaos * 7)
                    + Math.sin(a * 1.5 + t * 0.85)      * (4 + chaos * 6)
                    + Math.cos(a * 4 + t * 1.1)         * (chaos * 7)
                    + Math.sin(a * 6 - t * 0.95)        * (chaos * 5)
                pts.push({ x: cx + Math.cos(a) * r, y: cy + Math.sin(a) * r })
            }

            var len = pts.length
            ctx.beginPath()
            for (var j = 0; j < len; j++) {
                var p0 = pts[(j - 1 + len) % len]
                var p1 = pts[j]
                var p2 = pts[(j + 1) % len]
                var p3 = pts[(j + 2) % len]
                var cp1x = p1.x + (p2.x - p0.x) / 6
                var cp1y = p1.y + (p2.y - p0.y) / 6
                var cp2x = p2.x - (p3.x - p1.x) / 6
                var cp2y = p2.y - (p3.y - p1.y) / 6
                if (j === 0) ctx.moveTo(p1.x, p1.y)
                ctx.bezierCurveTo(cp1x, cp1y, cp2x, cp2y, p2.x, p2.y)
            }
            ctx.closePath()

            var grad = ctx.createRadialGradient(cx - 40, cy - 40, 0, cx, cy, 150)
            grad.addColorStop(0, blobRoot.blobHex(Theme.dark ? 0.24 : 0.19))
            ctx.fillStyle = grad
            ctx.fill()
        }
    }

    Canvas {
        id: blobCanvas
        width: 380; height: 380
        anchors.centerIn: parent
        visible: !isSendMode

        property real t: 0
        property double lastMs: Date.now()

        Timer {
            interval: 16
            running: blobRoot.animating
            repeat: true
            onTriggered: {
                var now = Date.now()
                var dt = Math.min(0.05, Math.max(0.0, (now - blobCanvas.lastMs) * 0.001))
                blobCanvas.lastMs = now
                blobCanvas.t += dt * (Math.PI * 2 / Math.max(4.4, 8.0 - blobChaos * 3.0))
                blobCanvas.requestPaint()
            }
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            var chaos = blobChaos
            var cx = width / 2 + Math.sin(t * 0.55) * chaos * 12
            var cy = height / 2 + Math.cos(t * 0.47) * chaos * 10
            var n = 10
            var pts = []

            for (var i = 0; i < n; i++) {
                var a = (i / n) * Math.PI * 2 - Math.PI / 2
                var r = 130 + chaos * 15
                    + Math.sin(a * 2 + t)               * (11 + chaos * 15)
                    + Math.cos(a * 3 - t * 0.6)         * (8 + chaos * 12)
                    + Math.sin(a * 1.5 + t * 0.35)      * (6 + chaos * 9)
                    + Math.sin(a + t * 0.75)            * (3 + chaos * 6)
                    + Math.cos(a * 5 - t * 1.35)        * (chaos * 10)
                    + Math.sin(a * 8 + t * 0.92)        * (chaos * 6)
                pts.push({ x: cx + Math.cos(a) * r, y: cy + Math.sin(a) * r })
            }

            var len = pts.length
            ctx.beginPath()
            for (var j = 0; j < len; j++) {
                var p0 = pts[(j - 1 + len) % len]
                var p1 = pts[j]
                var p2 = pts[(j + 1) % len]
                var p3 = pts[(j + 2) % len]
                var cp1x = p1.x + (p2.x - p0.x) / 6
                var cp1y = p1.y + (p2.y - p0.y) / 6
                var cp2x = p2.x - (p3.x - p1.x) / 6
                var cp2y = p2.y - (p3.y - p1.y) / 6
                if (j === 0) ctx.moveTo(p1.x, p1.y)
                ctx.bezierCurveTo(cp1x, cp1y, cp2x, cp2y, p2.x, p2.y)
            }
            ctx.closePath()

            var grad = ctx.createRadialGradient(cx - 40, cy - 40, 0, cx, cy, 150)
            grad.addColorStop(0, blobRoot.blobHex(Theme.dark ? 0.34 : 0.30))
            ctx.fillStyle = grad
            ctx.fill()
        }
    }

    Label {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 48
        visible: !isSendMode
        text: fileShareController.statusMessage
        font.pixelSize: 13
        color: textMuted
    }
}
