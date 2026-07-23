import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Full-width device / transfer row (RQuickShare-style). One component, state-driven.
// Renders a discovered send target and/or its active transfer. Incoming transfers
// are display-only (backend auto-accepts; no Accept/Decline invokable yet — see
// UI_OVERHAUL.md §7).
Item {
    id: root
    required property var modelData      // { id, name, isIncoming }

    // Width comes from the enclosing layout (Layout.fillWidth); height is fixed.
    implicitHeight: 84
    // Send mode: show every discovered target. Receive mode: only rows that have a
    // transfer (incoming sender), so stale send-targets don't clutter the receive view.
    // (Layouts skip items with visible:false.)
    visible: canSend || hasTransfer

    readonly property color rowFill: "#dcfce7"
    readonly property color rowFillHover: "#c9f4d9"
    readonly property color surface: "#ffffff"
    readonly property color textPrimary: "#111827"
    readonly property color textMuted: "#6b7280"
    readonly property color accentGreen: "#16a34a"
    readonly property color danger: "#ef4444"

    readonly property bool canSend: fileShareController.mode === "Send"
                                    && fileShareController.pendingSendFilePath.length > 0
    readonly property string targetName: (modelData && modelData.name && modelData.name.length > 0)
                                         ? modelData.name : "Unknown device"

    readonly property var transferData: transferForTarget()
    readonly property string transferStatus: transferData ? String(transferData.status || "") : ""
    readonly property bool incoming: transferData
                                     ? String(transferData.direction || "") === "incoming" : false
    readonly property bool hasTransfer: transferData !== null
    readonly property bool isActive: ["InProgress", "Queued", "Connecting",
                                      "AwaitingLocalConfirmation",
                                      "AwaitingRemoteAcceptance"].indexOf(transferStatus) !== -1
    readonly property bool isComplete: transferStatus === "Complete"
    readonly property bool isFailed: hasTransfer && !isActive && !isComplete
    readonly property string filePath: transferData ? String(transferData.filePath || "") : ""
    readonly property string fileName: transferData ? String(transferData.fileName || "") : ""

    readonly property real progress: {
        if (!hasTransfer) return 0
        if (isComplete || isFailed) return 1
        var n = Number(transferData.progress)
        if (!isFinite(n) || n < 0) n = 0
        return Math.max(0, Math.min(1, n))
    }
    readonly property string statusText: {
        if (!hasTransfer) return canSend ? "Tap to send" : ""
        switch (transferStatus) {
        case "Queued": return "Waiting…"
        case "Connecting": return "Connecting…"
        case "AwaitingLocalConfirmation": return incoming ? "Wants to share…" : "Confirm on this device…"
        case "AwaitingRemoteAcceptance": return "Waiting for them to accept…"
        case "InProgress": {
            var p = Math.round(progress * 100)
            var v = incoming ? "Receiving" : "Sending"
            return p > 0 ? v + "… " + p + "%" : v + "…"
        }
        case "Complete": return incoming ? "Received" : "Sent"
        default: return incoming ? "Unexpected disconnection" : "Couldn't send — tap to retry"
        }
    }
    readonly property color statusColor: isFailed ? danger
                                         : isComplete ? accentGreen : textMuted
    readonly property bool rowClickable: canSend && (!hasTransfer || isFailed || isComplete)
    readonly property bool awaitingLocal: transferStatus === "AwaitingLocalConfirmation"

    // ShareTargetType: 1=Phone, 2=Tablet, 3=Laptop, 5=Foldable, else generic.
    readonly property int deviceType: (modelData && modelData.deviceType !== undefined)
                                      ? Number(modelData.deviceType) : 0
    function deviceIcon() {
        switch (deviceType) {
        case 1: case 5: return "qrc:/icons/dev_phone.svg"
        case 2:         return "qrc:/icons/dev_tablet.svg"
        case 3:         return "qrc:/icons/dev_laptop.svg"
        default:        return "qrc:/icons/dev_generic.svg"
        }
    }

    function transferForTarget() {
        if (!modelData) return null
        var t = fileShareController.transfers
        for (var i = 0; i < t.length; ++i)
            if (t[i] && t[i].targetId === modelData.id) return t[i]
        return null
    }
    function initialLetter(s) { return (s && s.length) ? s.charAt(0).toUpperCase() : "?" }

    Rectangle {
        anchors.fill: parent
        radius: 18
        color: (rowArea.containsMouse && root.rowClickable) ? root.rowFillHover : root.rowFill

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 22
            spacing: 16

            // Avatar: white circle with device glyph, or a double-check when done.
            // (Per-type laptop/phone/tablet needs a deviceType on the model — see
            //  UI_OVERHAUL.md §7/§10; generic device icon for now.)
            Rectangle {
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56
                radius: 28
                color: root.surface
                border.color: root.isComplete ? root.accentGreen : "transparent"
                border.width: root.isComplete ? 2 : 0

                Image {
                    anchors.centerIn: parent
                    width: 26
                    height: 26
                    smooth: true
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: 52
                    sourceSize.height: 52
                    source: root.isComplete ? "qrc:/icons/check_double.svg"
                                            : root.deviceIcon()
                }
            }

            // Name + status (+ saved path for completed incoming).
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                Label {
                    Layout.fillWidth: true
                    text: root.targetName
                    font.pixelSize: 16
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                    color: root.textPrimary
                }
                Label {
                    Layout.fillWidth: true
                    visible: root.statusText.length > 0
                    text: root.isComplete && root.fileName.length > 0
                          ? root.statusText + " " + root.fileName
                          : root.statusText
                    font.pixelSize: 13
                    elide: Text.ElideRight
                    color: root.statusColor
                }
                Label {
                    Layout.fillWidth: true
                    visible: root.isComplete && root.incoming && root.filePath.length > 0
                    text: "Saved to " + root.filePath
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                    color: root.textMuted
                }
            }

            // Trailing actions.
            Row {
                Layout.alignment: Qt.AlignVCenter
                spacing: 20

                // Accept / Decline for an incoming request awaiting confirmation.
                Label {
                    visible: root.incoming && root.awaitingLocal
                    text: "Accept"
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: root.accentGreen
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -8
                        cursorShape: Qt.PointingHandCursor
                        onClicked: fileShareController.acceptTransfer(root.modelData.id)
                    }
                }
                Label {
                    visible: root.incoming && root.awaitingLocal
                    text: "Decline"
                    font.pixelSize: 14
                    color: root.danger
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -8
                        cursorShape: Qt.PointingHandCursor
                        onClicked: fileShareController.declineTransfer(root.modelData.id)
                    }
                }
                // Cancel an in-flight transfer (not the awaiting-confirmation state).
                Label {
                    visible: root.isActive && !root.awaitingLocal
                    text: "Cancel"
                    font.pixelSize: 14
                    color: root.textMuted
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -8
                        cursorShape: Qt.PointingHandCursor
                        onClicked: fileShareController.cancelTransfer(root.modelData.id)
                    }
                }
                Label {
                    visible: root.isComplete && root.incoming && root.filePath.length > 0
                    text: "Open"
                    font.pixelSize: 14
                    color: root.textPrimary
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -8
                        cursorShape: Qt.PointingHandCursor
                        onClicked: fileShareController.openFileLocation(root.filePath)
                    }
                }
                Label {
                    visible: root.isComplete || root.isFailed
                    text: "Done"
                    font.pixelSize: 14
                    color: root.textMuted
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -8
                        cursorShape: Qt.PointingHandCursor
                        // Dismiss just this transfer, not every device.
                        onClicked: fileShareController.clearTransfer(root.modelData.id)
                    }
                }
            }
        }

        MouseArea {
            id: rowArea
            anchors.fill: parent
            hoverEnabled: true
            enabled: root.rowClickable
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            // Sit behind the action labels' own MouseAreas.
            z: -1
            onClicked: {
                if (root.modelData)
                    fileShareController.sendPendingFileToTarget(root.modelData.id)
            }
        }
    }
}
