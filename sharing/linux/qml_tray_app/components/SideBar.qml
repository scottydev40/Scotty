import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    Layout.preferredWidth: 280
    Layout.fillHeight: true

    readonly property color surface: Theme.surface
    readonly property color cardBorder: Theme.rowFillHover
    readonly property color textPrimary: Theme.textPrimary
    readonly property color textMuted: Theme.textMuted

    // Visibility label + description.
    // 0=Everyone, 1=Contacts, 2=No one, 3=Your devices, 4=Everyone (10 min).
    function visibilityLabel(v) {
        switch (v) {
        case 1: return "Contacts"
        case 2: return "No one"
        case 3: return "Your devices"
        case 4: return "Everyone (10 min)"
        default: return "Everyone"
        }
    }
    function visibilityDesc(v) {
        if (!fileShareController.running)
            return "The service is not running. Start it to discover or receive files."
        // Subtitles say WHO can find you; how transfers are accepted lives in
        // Settings (auto-accept).
        switch (v) {
        case 2:
            return "You won't appear to nearby devices. Active transfers still finish."
        case 3:
            return "Only your own devices signed in to the same account can find you — they share without a prompt."
        case 1:
            return "Your contacts can find you and share files with you."
        case 4:
            return "Visible to everyone for 10 minutes, then switches back automatically."
        default:
            return "Any nearby device can find you and share files with you."
        }
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

            // Visibility selector (No one / Your devices / Contacts / Everyone).
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                height: 52
                radius: 12
                color: visSelArea.containsMouse ? Theme.rowFillHover : Theme.rowFill
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
                        color: Theme.surface
                        border.color: cardBorder
                    }

                    component VisItem: MenuItem {
                        implicitHeight: 40
                        contentItem: Label {
                            text: parent.text
                            leftPadding: 8
                            font.pixelSize: 14
                            verticalAlignment: Text.AlignVCenter
                            color: Theme.textPrimary
                        }
                        background: Rectangle {
                            radius: 8
                            color: parent.highlighted ? Theme.rowFill : "transparent"
                        }
                    }

                    // "Your devices" and "Contacts" need a signed-in account
                    // (cert exchange), so
                    // they only appear when signed in — on the clean core the
                    // menu collapses to No one / Everyone / Everyone (10 min).
                    readonly property bool hasAccount: fileShareController.signedInEmail.length > 0

                    VisItem { text: "No one"; onTriggered: fileShareController.visibility = 2 }
                    VisItem {
                        text: "Your devices"
                        visible: visibilityMenu.hasAccount
                        height: visible ? implicitHeight : 0
                        onTriggered: fileShareController.visibility = 3
                    }
                    VisItem {
                        text: "Contacts"
                        visible: visibilityMenu.hasAccount
                        height: visible ? implicitHeight : 0
                        onTriggered: fileShareController.visibility = 1
                    }
                    VisItem { text: "Everyone"; onTriggered: fileShareController.visibility = 0 }
                    VisItem { text: "Everyone (10 min)"; onTriggered: fileShareController.visibility = 4 }
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
                       ? (sendBtnArea.pressed ? Theme.accentDeep : Theme.accentStrong) : Theme.accentColor

                Row {
                    anchors.centerIn: parent
                    spacing: 8
                    Label {
                        text: "+"
                        color: Theme.onAccent
                        font.pixelSize: 18
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Label {
                        text: "Send files"
                        color: Theme.onAccent
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

            // Send text / a link — Quick Share sends these as a text attachment;
            // the receiver gets it with a copy button. Collapsed to a button
            // until used so it doesn't crowd the send-files action.
            ColumnLayout {
                id: textSend
                property bool open: false
                function go() {
                    var t = textInput.text.trim()
                    if (t.length === 0) return
                    fileShareController.switchToSendModeWithText(t)
                    textInput.text = ""
                    textSend.open = false
                }
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                spacing: 8

                Rectangle {
                    visible: !textSend.open
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 12
                    color: textBtnArea.containsMouse ? Theme.rowFillHover : Theme.rowFill
                    border.color: cardBorder
                    border.width: 1
                    Label {
                        anchors.centerIn: parent
                        text: "Send text or link"
                        font.pixelSize: 14
                        color: textPrimary
                    }
                    MouseArea {
                        id: textBtnArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { textSend.open = true; textInput.forceActiveFocus() }
                    }
                }

                TextField {
                    id: textInput
                    visible: textSend.open
                    Layout.fillWidth: true
                    placeholderText: "Paste a link or type text"
                    onAccepted: textSend.go()
                }
                RowLayout {
                    visible: textSend.open
                    Layout.fillWidth: true
                    spacing: 16
                    Item { Layout.fillWidth: true }
                    Label {
                        text: "Cancel"
                        font.pixelSize: 13
                        color: textMuted
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -8
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { textInput.text = ""; textSend.open = false }
                        }
                    }
                    Label {
                        readonly property bool ready: textInput.text.trim().length > 0
                        text: "Send"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: ready ? Theme.accentColor : textMuted
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -8
                            enabled: parent.ready
                            cursorShape: Qt.PointingHandCursor
                            onClicked: textSend.go()
                        }
                    }
                }
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
                    color: Theme.accentColor
                    Label {
                        id: badge
                        anchors.centerIn: parent
                        text: "×" + fileShareController.pendingSendFileCount
                        color: Theme.onAccent
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

            // On-demand "Scan to connect" QR. The code is not shown by default:
            // scanning it auto-authorizes the peer to receive, so it is minted
            // fresh only when the user asks (showQrCode) and burned when hidden
            // or when leaving the send sheet — a photographed/old QR cannot be
            // replayed to pull files later.
            Rectangle {
                visible: !fileShareController.qrVisible
                Layout.fillWidth: true
                Layout.topMargin: 14
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.preferredHeight: 44
                radius: 12
                color: showQrArea.containsMouse ? Theme.rowFillHover : Theme.rowFill
                border.color: cardBorder
                border.width: 1

                Label {
                    anchors.centerIn: parent
                    text: "Show QR code"
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: textPrimary
                }
                MouseArea {
                    id: showQrArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: fileShareController.showQrCode()
                }
            }

            ColumnLayout {
                visible: fileShareController.qrVisible
                Layout.fillWidth: true
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.topMargin: 12
                    Layout.rightMargin: 12
                    text: "Scan this with your phone to connect. Works best with both devices on the same Wi-Fi. This code is single-use — it stops working once you hide it."
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: textMuted
                }

                // Rendered from fileShareController.qrCodeUrl (a fresh ephemeral
                // session key minted by showQrCode()).
                SendUrlPanel {
                    Layout.topMargin: 6
                    Layout.leftMargin: 12
                    Layout.alignment: Qt.AlignHCenter
                    qrFrameSize: 210
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.bottomMargin: 4
                    text: "Hide QR code"
                    font.pixelSize: 13
                    color: hideQrArea.containsMouse ? textPrimary : textMuted
                    MouseArea {
                        id: hideQrArea
                        anchors.fill: parent
                        anchors.margins: -8
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: fileShareController.hideQrCode()
                    }
                }
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
                color: sendMoreArea.containsMouse ? Theme.accentStrong : Theme.accentColor
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
