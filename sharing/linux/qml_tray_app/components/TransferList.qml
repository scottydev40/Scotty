import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Flat list of current/recent transfers, shown in both send and receive modes.
ColumnLayout {
    id: root
    spacing: 12
    visible: fileShareController.transfers.length > 0

    readonly property color textPrimary: "#111827"
    readonly property color textMuted: "#6b7280"
    readonly property color cardBorder: "#e5e7eb"
    readonly property color okColor: "#16a34a"
    readonly property color failColor: "#ef4444"

    function statusLabel(entry) {
        const status = String(entry.status || "")
        const incoming = String(entry.direction || "") === "incoming"
        switch (status) {
        case "Queued": return "Queued…"
        case "Connecting": return "Connecting…"
        case "AwaitingLocalConfirmation": return "Waiting for you to accept…"
        case "AwaitingRemoteAcceptance": return "Waiting for them to accept…"
        case "InProgress": {
            const pct = Math.round(Number(entry.progress || 0) * 100)
            const verb = incoming ? "Receiving" : "Sending"
            return pct > 0 ? verb + "… " + pct + "%" : verb + "…"
        }
        case "Complete": return incoming ? "Received" : "Sent"
        default: return "Failed"
        }
    }

    function isActive(entry) {
        const status = String(entry.status || "")
        return status === "InProgress" || status === "Queued"
                || status === "Connecting"
                || status === "AwaitingLocalConfirmation"
                || status === "AwaitingRemoteAcceptance"
    }

    RowLayout {
        Layout.fillWidth: true

        Label {
            text: "Transfers"
            font.pixelSize: 20
            font.weight: Font.Medium
            color: root.textPrimary
        }

        Item { Layout.fillWidth: true }

        Label {
            text: "Clear"
            font.pixelSize: 13
            color: root.textMuted

            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                cursorShape: Qt.PointingHandCursor
                onClicked: fileShareController.clearTransfers()
            }
        }
    }

    Repeater {
        model: fileShareController.transfers

        delegate: Rectangle {
            required property var modelData

            Layout.fillWidth: true
            implicitHeight: rowLayout.implicitHeight + 24
            radius: 12
            color: "#ffffff"
            border.color: root.cardBorder

            RowLayout {
                id: rowLayout
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12

                Label {
                    text: String(modelData.direction) === "incoming" ? "↓" : "↑"
                    font.pixelSize: 20
                    font.weight: Font.Bold
                    color: String(modelData.status) === "Complete" ? root.okColor
                           : root.isActive(modelData) ? root.textMuted : root.failColor
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Label {
                        Layout.fillWidth: true
                        text: {
                            const file = String(modelData.fileName || "")
                            return file.length > 0 ? file : "File transfer"
                        }
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        elide: Text.ElideMiddle
                        color: root.textPrimary
                    }

                    Label {
                        Layout.fillWidth: true
                        text: {
                            const who = String(modelData.targetName || "")
                            const state = root.statusLabel(modelData)
                            const from = String(modelData.direction) === "incoming" ? "from " : "to "
                            return who.length > 0 ? state + " · " + from + who : state
                        }
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        color: String(modelData.status) === "Complete" ? root.okColor
                               : root.isActive(modelData) ? root.textMuted : root.failColor
                    }

                    ProgressBar {
                        Layout.fillWidth: true
                        visible: root.isActive(modelData)
                        value: Math.max(0, Math.min(1, Number(modelData.progress || 0)))
                    }
                }

                Label {
                    visible: String(modelData.status) === "Complete"
                             && String(modelData.direction) === "incoming"
                             && String(modelData.filePath || "").length > 0
                    text: "Open folder"
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    color: root.okColor

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -8
                        cursorShape: Qt.PointingHandCursor
                        onClicked: fileShareController.openFileLocation(String(modelData.filePath))
                    }
                }
            }
        }
    }
}
