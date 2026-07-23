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

    // Visibility (0=Everyone, 1=Contacts, 2=Hidden) label + description.
    function visibilityLabel(v) {
        return v === 1 ? "Contacts" : v === 2 ? "Hidden" : "Everyone"
    }
    function visibilityDesc(v) {
        if (!fileShareController.running)
            return "The service is not running. Start it to discover or receive files."
        if (v === 2)
            return "Hidden — you won't appear to nearby devices. Active sessions still work."
        if (v === 1)
            return "Only your saved contacts can discover this device."
        return "Nearby devices can share files with you. You'll be notified and must approve each transfer."
    }

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

            // Visibility selector (Everyone / Contacts / Hidden).
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                height: 52
                radius: 12
                color: visSelArea.containsMouse ? "#ddf7e8" : "#e8faf0"
                border.color: cardBorder

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12

                    Label {
                        Layout.fillWidth: true
                        text: visibilityLabel(fileShareController.visibility)
                        font.weight: Font.Medium
                        font.pixelSize: 15
                        elide: Text.ElideRight
                        color: textPrimary
                    }
                    Label {
                        text: "›"
                        font.pixelSize: 20
                        color: textMuted
                    }
                }

                MouseArea {
                    id: visSelArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: visibilityMenu.open()
                }

                // Opens aligned directly below the selector, light-themed.
                Menu {
                    id: visibilityMenu
                    y: parent.height + 4
                    width: parent.width
                    padding: 5

                    background: Rectangle {
                        radius: 10
                        color: "#ffffff"
                        border.color: cardBorder
                    }

                    component VisItem: MenuItem {
                        implicitHeight: 40
                        contentItem: Label {
                            text: parent.text
                            leftPadding: 8
                            font.pixelSize: 14
                            verticalAlignment: Text.AlignVCenter
                            color: "#111827"
                        }
                        background: Rectangle {
                            radius: 8
                            color: parent.highlighted ? "#dcfce7" : "transparent"
                        }
                    }

                    VisItem { text: "Everyone"; onTriggered: fileShareController.visibility = 0 }
                    VisItem { text: "Contacts"; onTriggered: fileShareController.visibility = 1 }
                    VisItem { text: "Hidden"; onTriggered: fileShareController.visibility = 2 }
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.topMargin: 8
                Layout.rightMargin: 12
                text: visibilityDesc(fileShareController.visibility)
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: textMuted
            }

            // ── Send-file initiator (blob keeps the main area; send starts here) ──
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 18
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                height: 46
                radius: 12
                color: sendBtnArea.containsMouse
                       ? (sendBtnArea.pressed ? "#047857" : "#059669") : "#10b981"

                Row {
                    anchors.centerIn: parent
                    spacing: 8
                    Label {
                        text: "+"
                        color: "#ffffff"
                        font.pixelSize: 18
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Label {
                        text: "Send files"
                        color: "#ffffff"
                        font.pixelSize: 15
                        font.weight: Font.Medium
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                MouseArea {
                    id: sendBtnArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: sidebarSendDialog.open()
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.topMargin: 6
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                text: "…or drop files anywhere in the window."
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                color: textMuted
            }

            FileDialog {
                id: sidebarSendDialog
                title: "Select file(s) to send"
                fileMode: FileDialog.OpenFiles
                onAccepted: {
                    const paths = selectedFiles.map(
                        u => u.toString().replace(/^file:\/\//, ""))
                    fileShareController.switchToSendModeWithFiles(paths)
                }
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

                // Font-independent document glyph (the folder emoji rendered blank
                // for multi-file). A small "×N" badge marks a multi-file send.
                Canvas {
                    anchors.centerIn: parent
                    width: 34; height: 40
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.strokeStyle = "#2563eb"
                        ctx.fillStyle = "#eff6ff"
                        ctx.lineWidth = 2
                        ctx.lineJoin = "round"
                        var fold = 10
                        ctx.beginPath()
                        ctx.moveTo(3, 2)
                        ctx.lineTo(width - fold, 2)
                        ctx.lineTo(width - 3, fold + 2)
                        ctx.lineTo(width - 3, height - 2)
                        ctx.lineTo(3, height - 2)
                        ctx.closePath()
                        ctx.fill()
                        ctx.stroke()
                        ctx.beginPath()
                        ctx.moveTo(width - fold, 2)
                        ctx.lineTo(width - fold, fold + 2)
                        ctx.lineTo(width - 3, fold + 2)
                        ctx.stroke()
                    }
                }

                Rectangle {
                    visible: fileShareController.pendingSendFileCount > 1
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 4
                    width: badge.implicitWidth + 12
                    height: 20
                    radius: 10
                    color: "#10b981"
                    Label {
                        id: badge
                        anchors.centerIn: parent
                        text: "×" + fileShareController.pendingSendFileCount
                        color: "#ffffff"
                        font.pixelSize: 11
                        font.weight: Font.Bold
                    }
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

            // QR — sized so a phone camera can actually resolve it.
            SendUrlPanel {
                Layout.topMargin: 14
                Layout.leftMargin: 12
                Layout.alignment: Qt.AlignHCenter
                qrFrameSize: 210
            }
        }

        Item {
            Layout.fillHeight: true
        }

        // Send-mode action. While a send is active: "Cancel" (back to receive).
        // Once idle/sent: "Send more" (pick another file) + "Done" (back to receive).
        RowLayout {
            visible: fileShareController.pendingSendFilePath.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 16
            spacing: 16

            // "Send more" — only when no send is currently in flight.
            Label {
                visible: !sendActive
                text: "Send more"
                font.pixelSize: 16
                color: sendMoreArea.containsMouse ? "#059669" : "#10b981"
                font.weight: Font.Medium
                MouseArea {
                    id: sendMoreArea
                    anchors.fill: parent
                    anchors.margins: -8
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: sidebarSendDialog.open()
                }
            }

            Item { Layout.fillWidth: true }

            Label {
                text: sendActive ? "Cancel" : "Done"
                font.pixelSize: 16
                color: cancelArea.containsMouse ? textPrimary : textMuted
                MouseArea {
                    id: cancelArea
                    anchors.fill: parent
                    anchors.margins: -8
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (!sendActive)
                            fileShareController.clearTransfers()
                        fileShareController.switchToReceiveMode()
                    }
                }
            }
        }
    }
}
