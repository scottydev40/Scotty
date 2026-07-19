import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    Layout.preferredWidth: 280
    Layout.fillHeight: true

    readonly property color surface: "#ffffff"
    readonly property color cardBorder: "#bbf7d0"
    readonly property color textPrimary: "#111827"
    readonly property color textMuted: "#6b7280"

    // True while an outgoing transfer is still active (not yet sent/failed).
    readonly property bool sendActive: {
        const list = fileShareController.transfers
        for (var i = 0; i < list.length; ++i) {
            const t = list[i]
            if (String(t.direction) !== "outgoing")
                continue
            const s = String(t.status)
            if (s === "InProgress" || s === "Queued" || s === "Connecting"
                || s === "AwaitingLocalConfirmation"
                || s === "AwaitingRemoteAcceptance")
                return true
        }
        return false
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 0

        // Receive mode: visibility info
        ColumnLayout {
            visible: fileShareController.pendingSendFilePath.length === 0
            Layout.fillWidth: true
            spacing: 0

            Label {
                Layout.leftMargin: 12
                Layout.topMargin: 16
                Layout.bottomMargin: 8
                text: "Visibility state"
                color: textMuted
                font.pixelSize: 13
            }

            Rectangle {
                Layout.fillWidth: true
                height: 52
                radius: 12
                color: "#e8faf0"
                border.color: cardBorder

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12

                    Label {
                        Layout.fillWidth: true
                        text: !fileShareController.running
                              ? "Inactive"
                              : fileShareController.mode === "Send"
                                ? "Discovering"
                                : "Receiving as “" + fileShareController.deviceName + "”"
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                        color: textPrimary
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.topMargin: 8
                Layout.rightMargin: 12
                text: !fileShareController.running
                    ? "The service is not running. Start it to discover or receive files."
                    : fileShareController.mode === "Send"
                    ? "Discovering nearby devices. Select a device below to send your file."
                    : "Nearby devices can share files with you. You'll be notified and must approve each transfer."
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: textMuted
            }
        }

        // Send mode: outbound file info
        ColumnLayout {
            visible: fileShareController.pendingSendFilePath.length > 0
            Layout.fillWidth: true
            spacing: 0

            Label {
                Layout.leftMargin: 12
                Layout.topMargin: 16
                Layout.bottomMargin: 8
                text: {
                    const n = fileShareController.pendingSendFileCount
                    return n === 1 ? "Sharing 1 file"
                                   : "Sharing " + n + " files"
                }
                font.weight: Font.Medium
                color: textPrimary
            }

            Rectangle {
                Layout.leftMargin: 12
                width: 72
                height: 72
                radius: 12
                color: surface

                Label {
                    anchors.centerIn: parent
                    text: fileShareController.pendingSendFileCount > 1 ? "🗂" : "📄"
                    font.pixelSize: 28
                }
            }

            // File name(s). Single file shows inline; multiple files list in a
            // capped, scrollable column.
            Label {
                visible: fileShareController.pendingSendFileCount <= 1
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.topMargin: 8
                Layout.rightMargin: 12
                text: fileShareController.pendingSendFileName
                elide: Text.ElideRight
                font.pixelSize: 13
                color: textMuted
            }

            Flickable {
                visible: fileShareController.pendingSendFileCount > 1
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.topMargin: 8
                Layout.rightMargin: 12
                Layout.preferredHeight: Math.min(nameCol.implicitHeight, 140)
                contentWidth: width
                contentHeight: nameCol.implicitHeight
                clip: true
                ScrollBar.vertical: ScrollBar {}

                Column {
                    id: nameCol
                    width: parent.width
                    spacing: 2

                    Repeater {
                        model: fileShareController.pendingSendFileNames
                        delegate: Label {
                            width: nameCol.width
                            text: "• " + modelData
                            elide: Text.ElideRight
                            font.pixelSize: 13
                            color: textMuted
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.topMargin: 12
                Layout.rightMargin: 12
                text: "Make sure both devices are unlocked, close together, and have Bluetooth turned on."
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: textMuted
            }
        }

        // ── This-session transfer history (scrollable, fills the middle) ──
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 16
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            spacing: 8
            visible: fileShareController.transfers.length > 0

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: "This session"
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    color: textMuted
                }
                Label {
                    text: "Clear"
                    font.pixelSize: 12
                    color: textMuted
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -6
                        cursorShape: Qt.PointingHandCursor
                        onClicked: fileShareController.clearTransfers()
                    }
                }
            }

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: histCol.implicitHeight
                clip: true
                ScrollBar.vertical: ScrollBar {}

                Column {
                    id: histCol
                    width: parent.width
                    spacing: 8

                    Repeater {
                        model: fileShareController.transfers
                        delegate: Rectangle {
                            id: histCard
                            required property var modelData
                            width: histCol.width
                            height: histRow.implicitHeight + 20
                            radius: 10
                            color: surface
                            border.color: "#e5e7eb"

                            function isActive(s) {
                                return s === "InProgress" || s === "Queued"
                                    || s === "Connecting"
                                    || s === "AwaitingLocalConfirmation"
                                    || s === "AwaitingRemoteAcceptance"
                            }
                            function statusText(m) {
                                const s = String(m.status || "")
                                const inc = String(m.direction || "") === "incoming"
                                switch (s) {
                                case "Queued": return "Queued…"
                                case "Connecting": return "Connecting…"
                                case "AwaitingLocalConfirmation": return "Waiting to accept…"
                                case "AwaitingRemoteAcceptance": return "Waiting for them…"
                                case "InProgress": {
                                    const p = Math.round(Number(m.progress || 0) * 100)
                                    const v = inc ? "Receiving" : "Sending"
                                    return p > 0 ? v + "… " + p + "%" : v + "…"
                                }
                                case "Complete": return inc ? "Received" : "Sent"
                                default: return "Failed"
                                }
                            }

                            ColumnLayout {
                                id: histRow
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 4

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Label {
                                        text: String(modelData.direction) === "incoming" ? "↓" : "↑"
                                        font.pixelSize: 15
                                        font.weight: Font.Bold
                                        color: String(modelData.status) === "Complete" ? "#16a34a"
                                               : histCard.isActive(String(modelData.status)) ? textMuted : "#ef4444"
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: {
                                            const f = String(modelData.fileName || "")
                                            return f.length > 0 ? f : "File transfer"
                                        }
                                        font.pixelSize: 13
                                        font.weight: Font.Medium
                                        elide: Text.ElideMiddle
                                        color: textPrimary
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: {
                                        const who = String(modelData.targetName || "")
                                        const st = histCard.statusText(modelData)
                                        const dir = String(modelData.direction) === "incoming" ? "from " : "to "
                                        return who.length > 0 ? st + " · " + dir + who : st
                                    }
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                    color: String(modelData.status) === "Complete" ? "#16a34a"
                                           : histCard.isActive(String(modelData.status)) ? textMuted : "#ef4444"
                                }

                                Label {
                                    visible: String(modelData.status) === "Complete"
                                             && String(modelData.direction) === "incoming"
                                             && String(modelData.filePath || "").length > 0
                                    text: "Open folder"
                                    font.pixelSize: 11
                                    font.weight: Font.Medium
                                    color: "#16a34a"
                                    MouseArea {
                                        anchors.fill: parent
                                        anchors.margins: -6
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: fileShareController.openFileLocation(String(modelData.filePath))
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillHeight: true
            visible: fileShareController.transfers.length === 0
        }

        FileDialog {
            id: anotherFileDialog
            title: "Select file(s) to send"
            fileMode: FileDialog.OpenFiles
            onAccepted: {
                const paths = selectedFiles.map(
                    u => u.toString().replace(/^file:\/\//, ""))
                fileShareController.switchToSendModeWithFiles(paths)
            }
        }

        // Send-mode actions
        ColumnLayout {
            visible: fileShareController.pendingSendFilePath.length > 0
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 12
            spacing: 8

            // Primary: pick another file to send
            Rectangle {
                Layout.fillWidth: true
                height: 42
                radius: 12
                color: anotherArea.containsMouse
                       ? (anotherArea.pressed ? "#047857" : "#059669")
                       : "#10b981"

                Label {
                    anchors.centerIn: parent
                    text: "Send another file"
                    font.weight: Font.Medium
                    color: "#ffffff"
                }

                MouseArea {
                    id: anotherArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: anotherFileDialog.open()
                }
            }

            // Secondary: leave send mode (label reflects transfer state)
            Rectangle {
                Layout.fillWidth: true
                height: 40
                radius: 12
                color: "#f3f4f6"
                border.color: "#d1d5db"

                Label {
                    anchors.centerIn: parent
                    text: sendActive ? "Cancel" : "Done"
                    font.weight: Font.Medium
                    color: textPrimary
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        // "Done" ends the session view: leave send mode and wipe
                        // the session transfer history. "Cancel" (active send) just
                        // leaves send mode without clearing.
                        if (!sendActive)
                            fileShareController.clearTransfers()
                        fileShareController.switchToReceiveMode()
                    }
                }
            }
        }
    }
}
