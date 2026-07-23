import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Popup {
    id: root
    // Centered modal over the whole window (RQuickShare-style).
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    dim: true
    padding: 0
    width: 560
    height: Math.min(cardHeightHint, (parent ? parent.height : 800) - 80)

    readonly property color bg: Theme.surface
    readonly property color surface: Theme.surface
    readonly property color accent: Theme.accentColor
    readonly property color accentLight: Theme.rowFill
    readonly property color borderColor: Theme.border
    readonly property color textPrimary: Theme.textPrimary
    readonly property color textMuted: Theme.textMuted

    // Dimmed backdrop behind the card.
    Overlay.modal: Rectangle { color: "#66000000" }

    background: Rectangle {
        color: root.bg
        radius: 20
        border.color: Theme.border
    }

    // Card height driver: header (64) + content column + bottom padding.
    readonly property real cardHeightHint: 64 + settingsCol.height + 40

    contentItem: ColumnLayout {
        spacing: 0

        // Header
        Rectangle {
            Layout.fillWidth: true
            height: 64
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20

                Label {
                    text: "Settings"
                    font.pixelSize: 20
                    font.weight: Font.Bold
                    color: root.textPrimary
                }

                Item { Layout.fillWidth: true }

                Label {
                    text: "Close"
                    font.pixelSize: 15
                    color: closeArea.containsMouse ? root.textPrimary : root.textMuted

                    MouseArea {
                        id: closeArea
                        anchors.fill: parent
                        anchors.margins: -8
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.close()
                    }
                }
            }
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: settingsCol.height + 32
            ScrollBar.vertical: ScrollBar {}

            Column {
                id: settingsCol
                x: 20
                y: 20
                width: flick.width - 40
                spacing: 20

                SectionLabel { text: "DEVICE" }
                SectionCard {
                    width: settingsCol.width

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label {
                                text: "Device name"
                                font.pixelSize: 13
                                color: root.textMuted
                                Layout.preferredWidth: 110
                            }
                            ThemedField {
                                text: fileShareController.deviceName
                                onEditingFinished: fileShareController.deviceName = text
                            }
                        }
                    }
                }

                SectionLabel { text: "SHARING" }
                SectionCard {
                    width: settingsCol.width

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 12

                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                            color: root.textMuted
                            text: "Nearby Sharing uses built-in transport and discovery settings."
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label {
                                Layout.fillWidth: true
                                color: root.textPrimary
                                font.pixelSize: 13
                                text: "Auto-accept incoming"
                            }
                            ThemedToggle {
                                checked: fileShareController.autoAcceptIncoming
                                onToggled: fileShareController.autoAcceptIncoming = checked
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            ColumnLayout {
                                spacing: 1
                                Label {
                                    color: root.textPrimary
                                    font.pixelSize: 13
                                    text: "Run at startup"
                                }
                                Label {
                                    color: root.textMuted
                                    font.pixelSize: 11
                                    text: "Launch automatically when you log in."
                                }
                            }
                            Item { Layout.fillWidth: true }
                            ThemedToggle {
                                Layout.alignment: Qt.AlignVCenter
                                checked: fileShareController.runAtStartup
                                onToggled: fileShareController.runAtStartup = checked
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label {
                                Layout.fillWidth: true
                                color: root.textPrimary
                                font.pixelSize: 13
                                text: "Enable 5 GHz hotspot"
                            }
                            ThemedToggle {
                                checked: fileShareController.enable5GhzHotspot
                                onToggled: fileShareController.enable5GhzHotspot = checked
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label {
                                Layout.fillWidth: true
                                color: root.textPrimary
                                font.pixelSize: 13
                                text: "Developer mode"
                            }
                            ThemedToggle {
                                checked: fileShareController.developerMode
                                onToggled: fileShareController.developerMode = checked
                            }
                        }
                    }
                }

                SectionLabel { text: "APPEARANCE" }
                SectionCard {
                    width: settingsCol.width

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label {
                                Layout.fillWidth: true
                                color: root.textPrimary
                                font.pixelSize: 13
                                text: "Dark mode"
                            }
                            ThemedToggle {
                                checked: Theme.dark
                                onToggled: Theme.dark = checked
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            ColumnLayout {
                                spacing: 1
                                Label {
                                    color: root.textPrimary
                                    font.pixelSize: 13
                                    text: "Show tray icon"
                                }
                                Label {
                                    color: root.textMuted
                                    font.pixelSize: 11
                                    text: "Off if you drive it from the GNOME Quick Settings tile."
                                }
                            }
                            Item { Layout.fillWidth: true }
                            ThemedToggle {
                                Layout.alignment: Qt.AlignVCenter
                                checked: fileShareController.showTrayIcon
                                onToggled: fileShareController.showTrayIcon = checked
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label {
                                Layout.fillWidth: true
                                color: root.textPrimary
                                font.pixelSize: 13
                                text: "Accent color"
                            }
                            Row {
                                spacing: 12
                                Repeater {
                                    // value maps to ThemeController::Accent (0/1/2).
                                    model: [
                                        { swatch: "#16a34a", value: 0 },
                                        { swatch: "#2563eb", value: 1 },
                                        { swatch: "#e95420", value: 2 }
                                    ]
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: 28
                                        height: 28
                                        radius: 14
                                        color: modelData.swatch
                                        border.color: root.textPrimary
                                        border.width: Theme.accent === modelData.value ? 3 : 0
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: Theme.accent = modelData.value
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ── Developer-only settings (experimental) ────────────────
                SectionLabel {
                    text: "DEVELOPER"
                    visible: fileShareController.developerMode
                }
                SectionCard {
                    width: settingsCol.width
                    visible: fileShareController.developerMode

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 12

                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                            color: root.textMuted
                            text: "Experimental. Save folder for received files (blank = default). QR/WebRTC sharing is not supported on Linux."
                        }

                        FolderDialog {
                            id: saveFolderDialog
                            title: "Choose save folder for received files"
                            onAccepted: {
                                fileShareController.savePath =
                                    selectedFolder.toString().replace(/^file:\/\//, "")
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label {
                                text: "Save folder"
                                font.pixelSize: 13
                                color: root.textMuted
                                Layout.preferredWidth: 110
                            }
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideMiddle
                                font.pixelSize: 11
                                color: root.textPrimary
                                text: fileShareController.savePath.length > 0
                                      ? fileShareController.savePath
                                      : "Default (~/Downloads)"
                            }
                            BrowseButton {
                                onClicked: saveFolderDialog.open()
                            }
                        }

                        FileDialog {
                            id: logFileDialog
                            title: "Choose log file"
                            fileMode: FileDialog.SaveFile
                            onAccepted: {
                                fileShareController.logPath =
                                    selectedFile.toString().replace(/^file:\/\//, "")
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label {
                                text: "Log path"
                                font.pixelSize: 13
                                color: root.textMuted
                                Layout.preferredWidth: 110
                            }
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideMiddle
                                font.pixelSize: 11
                                color: root.textPrimary
                                text: fileShareController.logPath.length > 0
                                      ? fileShareController.logPath
                                      : "Default"
                            }
                            BrowseButton {
                                onClicked: logFileDialog.open()
                            }
                        }
                    }
                }

                // Quit — the only in-window way to fully exit when the tray icon
                // is hidden (closing the window just hides it to the background).
                Rectangle {
                    width: settingsCol.width
                    height: 44
                    radius: 12
                    color: quitArea.containsMouse ? Theme.dangerSoft : "transparent"
                    border.color: Theme.danger
                    border.width: 1
                    Label {
                        anchors.centerIn: parent
                        text: "Quit Quick Share"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: Theme.danger
                    }
                    MouseArea {
                        id: quitArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: fileShareController.quitApplication()
                    }
                }
            }
        }
    }

    component BrowseButton: Button {
        text: "Browse…"
        font.pixelSize: 11
        padding: 8
        background: Rectangle {
            radius: 8
            color: parent.down ? Theme.accentDeep
                   : parent.hovered ? Theme.accentStrong : Theme.accentColor
        }
        contentItem: Label {
            text: parent.text
            font: parent.font
            color: Theme.onAccent
        }
    }

    component SectionLabel: Label {
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.letterSpacing: 0.8
        color: root.accent
    }

    component SectionCard: Rectangle {
        radius: 12
        color: root.surface
        border.color: root.borderColor
        height: (children.length > 0 ? children[0].implicitHeight : 0) + 24
    }

    component ThemedField: TextField {
        Layout.fillWidth: true
        implicitHeight: 38
        font.pixelSize: 13
        leftPadding: 12
        rightPadding: 12
        topPadding: 0
        bottomPadding: 0
        verticalAlignment: TextInput.AlignVCenter
        color: root.textPrimary
        background: Rectangle {
            radius: 8
            color: Theme.surfaceAlt
            border.color: parent.activeFocus ? root.accent : root.borderColor
            border.width: parent.activeFocus ? 2 : 1
        }
    }

    component ThemedToggle: Switch {
        palette.highlight: root.accent
    }
}
