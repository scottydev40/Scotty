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
                Label {
                    width: settingsCol.width
                    wrapMode: Text.WordWrap
                    color: root.textMuted
                    font.pixelSize: 12
                    text: "Off-network transfers use Wi-Fi for speed. If this device cannot keep both connections active, Scotty will notify you and temporarily disconnect your current Wi-Fi, even with Boost off."
                }
                SectionCard {
                    width: settingsCol.width

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 12

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
                            ColumnLayout {
                                spacing: 1
                                Label {
                                    color: root.textPrimary
                                    font.pixelSize: 13
                                    text: "Follow system theme"
                                }
                                Label {
                                    color: root.textMuted
                                    font.pixelSize: 11
                                    text: Theme.systemAccentAvailable
                                          ? "Match the desktop's light/dark mode and accent colour."
                                          : "Match the desktop's light/dark mode."
                                }
                            }
                            Item { Layout.fillWidth: true }
                            ThemedToggle {
                                Layout.alignment: Qt.AlignVCenter
                                checked: Theme.followSystem
                                onToggled: Theme.followSystem = checked
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label {
                                Layout.fillWidth: true
                                color: root.textPrimary
                                font.pixelSize: 13
                                text: "Dark mode"
                                opacity: Theme.followSystem ? 0.5 : 1.0
                            }
                            ThemedToggle {
                                checked: Theme.dark
                                onToggled: Theme.dark = checked
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
                                opacity: Theme.followSystem
                                         && Theme.systemAccentAvailable ? 0.5 : 1.0
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
                                        // No ring while the desktop's own
                                        // accent is the one in use.
                                        border.width:
                                            Theme.accent === modelData.value
                                            && !(Theme.followSystem
                                                 && Theme.systemAccentAvailable)
                                            ? 3 : 0
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

                // ── About & software updates ──────────────────────────────
                SectionLabel { text: "ABOUT" }
                SectionCard {
                    width: settingsCol.width

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 12

                        // Current version.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label {
                                Layout.fillWidth: true
                                color: root.textPrimary
                                font.pixelSize: 13
                                text: "Version"
                            }
                            Label {
                                color: root.textMuted
                                font.pixelSize: 13
                                text: updateChecker.currentVersion
                            }
                        }

                        // Beta channel opt-in (persisted).
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Label {
                                    color: root.textPrimary
                                    font.pixelSize: 13
                                    text: "Beta updates"
                                }
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    color: root.textMuted
                                    font.pixelSize: 11
                                    text: "Include pre-release (beta) builds when checking."
                                }
                            }
                            ThemedToggle {
                                Layout.alignment: Qt.AlignVCenter
                                checked: updateChecker.betaChannel
                                onToggled: updateChecker.betaChannel = checked
                            }
                        }

                        // Status line (checking / up to date / available / error).
                        Label {
                            Layout.fillWidth: true
                            visible: updateChecker.statusText.length > 0
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                            color: root.textMuted
                            text: updateChecker.statusText
                        }

                        // Download progress.
                        ProgressBar {
                            Layout.fillWidth: true
                            visible: updateChecker.downloadProgress >= 0
                            from: 0
                            to: 100
                            value: updateChecker.downloadProgress
                        }

                        // Action buttons.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            // Check for updates.
                            Rectangle {
                                id: checkBtn
                                Layout.preferredHeight: 34
                                Layout.preferredWidth: checkLabel.implicitWidth + 28
                                radius: 8
                                color: checkArea.containsMouse ? root.accentLight
                                                               : "transparent"
                                border.color: root.borderColor
                                border.width: 1
                                Label {
                                    id: checkLabel
                                    anchors.centerIn: parent
                                    text: "Check for updates"
                                    font.pixelSize: 13
                                    color: root.textPrimary
                                }
                                MouseArea {
                                    id: checkArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: updateChecker.checkForUpdates()
                                }
                            }

                            Item { Layout.fillWidth: true }

                            // Install / open release — only when an update was found.
                            Rectangle {
                                id: installBtn
                                visible: updateChecker.availableVersion.length > 0
                                Layout.preferredHeight: 34
                                Layout.preferredWidth: installLabel.implicitWidth + 28
                                radius: 8
                                color: installArea.containsMouse
                                       ? Qt.darker(root.accent, 1.1) : root.accent
                                Label {
                                    id: installLabel
                                    anchors.centerIn: parent
                                    text: updateChecker.canSelfUpdate
                                          ? "Download & install" : "Open release page"
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    color: "#ffffff"
                                }
                                MouseArea {
                                    id: installArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: updateChecker.canSelfUpdate
                                               ? updateChecker.downloadAndInstall()
                                               : updateChecker.openReleasePage()
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
                            text: "Advanced options for tuning transport and diagnostics. Change these only if you know what they do."
                        }

                        // Boost: full-bandwidth hotspot at the cost of Wi-Fi.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    Layout.fillWidth: true
                                    color: root.textPrimary
                                    font.pixelSize: 13
                                    text: "Boost transfer speed"
                                }
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 11
                                    color: root.textMuted
                                    text: "Gives the hotspot the full radio for faster transfers, but disconnects this device from Wi-Fi for the duration. Trades connectivity for speed."
                                }
                            }
                            ThemedToggle {
                                checked: fileShareController.hotspotBoost
                                onToggled: fileShareController.hotspotBoost = checked
                            }
                        }

                        // Force the hotspot onto 2.4 GHz. Off by default: the
                        // engine already auto-picks the best band (5/6 GHz when
                        // the card supports it). This is only a compatibility
                        // fallback for peers or regulatory setups that can't join
                        // a 5/6 GHz AP.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    Layout.fillWidth: true
                                    color: root.textPrimary
                                    font.pixelSize: 13
                                    text: "Allow 5/6 GHz hotspot"
                                }
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 11
                                    color: root.textMuted
                                    text: "On: auto-use the fastest band the Wi-Fi card supports. Off: force 2.4 GHz for older/incompatible peers."
                                }
                            }
                            ThemedToggle {
                                checked: fileShareController.enable5GhzHotspot
                                onToggled: fileShareController.enable5GhzHotspot = checked
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

                        // Global summon hotkey (experimental). Click the box and
                        // press a combo; it registers as a GNOME custom keybinding
                        // whose command re-activates Scotty (raising the window).
                        ColumnLayout {
                            id: shortcutRow
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            spacing: 6
                            property string captureError: ""

                            function pretty(s) {
                                if (s.length === 0) return "Not set — click to record"
                                return s.replace(/<Control>/gi, "Ctrl+")
                                        .replace(/<Primary>/gi, "Ctrl+")
                                        .replace(/<Alt>/gi, "Alt+")
                                        .replace(/<Shift>/gi, "Shift+")
                                        .replace(/<Super>/gi, "Super+")
                                        .toUpperCase()
                            }

                            Label {
                                Layout.fillWidth: true
                                color: root.textPrimary
                                font.pixelSize: 13
                                text: "Global shortcut to open Scotty"
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font.pixelSize: 11
                                color: root.textMuted
                                text: "A system-wide hotkey that brings Scotty to the front. Click the box, then press your combo (needs at least one modifier). GNOME only."
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Rectangle {
                                    id: captureBox
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 38
                                    radius: 8
                                    color: Theme.surfaceAlt
                                    border.color: captureBox.recording ? root.accent
                                                                       : root.borderColor
                                    border.width: captureBox.recording ? 2 : 1
                                    property bool recording: false

                                    Label {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.leftMargin: 12
                                        anchors.rightMargin: 12
                                        elide: Text.ElideRight
                                        font.pixelSize: 13
                                        color: captureBox.recording ? root.accent
                                               : (fileShareController.globalShortcut.length > 0
                                                  ? root.textPrimary : root.textMuted)
                                        text: captureBox.recording
                                              ? "Press a combo… (Esc to cancel)"
                                              : shortcutRow.pretty(fileShareController.globalShortcut)
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            shortcutRow.captureError = ""
                                            captureBox.recording = true
                                            keyCatcher.forceActiveFocus()
                                        }
                                    }

                                    Item {
                                        id: keyCatcher
                                        anchors.fill: parent
                                        Keys.onPressed: function(event) {
                                            if (!captureBox.recording) return
                                            const k = event.key
                                            if (k === Qt.Key_Control || k === Qt.Key_Alt
                                                || k === Qt.Key_Shift || k === Qt.Key_Meta
                                                || k === Qt.Key_Super_L || k === Qt.Key_Super_R) {
                                                event.accepted = true; return
                                            }
                                            if (k === Qt.Key_Escape) {
                                                captureBox.recording = false
                                                event.accepted = true; return
                                            }
                                            var mods = ""
                                            if (event.modifiers & Qt.ControlModifier) mods += "<Control>"
                                            if (event.modifiers & Qt.AltModifier) mods += "<Alt>"
                                            if (event.modifiers & Qt.ShiftModifier) mods += "<Shift>"
                                            if (event.modifiers & Qt.MetaModifier) mods += "<Super>"
                                            if (mods === "") {
                                                shortcutRow.captureError = "Include a modifier (Ctrl / Alt / Super)."
                                                event.accepted = true; return
                                            }
                                            var keyName = ""
                                            if (k >= Qt.Key_A && k <= Qt.Key_Z)
                                                keyName = String.fromCharCode(k).toLowerCase()
                                            else if (k >= Qt.Key_0 && k <= Qt.Key_9)
                                                keyName = String.fromCharCode(k)
                                            if (keyName === "") {
                                                shortcutRow.captureError = "Use a letter or number."
                                                event.accepted = true; return
                                            }
                                            captureBox.recording = false
                                            shortcutRow.captureError =
                                                fileShareController.setGlobalShortcut(mods + keyName)
                                            event.accepted = true
                                        }
                                    }
                                }

                                Rectangle {
                                    Layout.preferredWidth: 64
                                    Layout.preferredHeight: 38
                                    radius: 8
                                    visible: fileShareController.globalShortcut.length > 0
                                             || captureBox.recording
                                    color: clearShortcutArea.containsMouse ? Theme.rowFillHover
                                                                           : "transparent"
                                    border.color: root.borderColor
                                    border.width: 1
                                    Label {
                                        anchors.centerIn: parent
                                        text: "Clear"
                                        font.pixelSize: 13
                                        color: root.textMuted
                                    }
                                    MouseArea {
                                        id: clearShortcutArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            captureBox.recording = false
                                            shortcutRow.captureError = ""
                                            fileShareController.clearGlobalShortcut()
                                        }
                                    }
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: shortcutRow.captureError.length > 0
                                wrapMode: Text.WordWrap
                                font.pixelSize: 11
                                color: Theme.danger
                                text: shortcutRow.captureError
                            }
                        }

                        // Hard reset — the "unstick" button.
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            spacing: 6
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font.pixelSize: 11
                                color: root.textMuted
                                text: "Kills all active connections and transfers, then rebuilds the radios and re-advertises. Use only if something is stuck."
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 38
                                radius: 10
                                color: hardResetArea.containsMouse ? Theme.dangerSoft
                                                                   : "transparent"
                                border.color: Theme.danger
                                border.width: 1
                                Label {
                                    anchors.centerIn: parent
                                    text: "Hard reset connection"
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    color: Theme.danger
                                }
                                MouseArea {
                                    id: hardResetArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        // hardReset() is synchronous; when it
                                        // returns the rebuild is done. Drop back
                                        // to the home page, where the status blob
                                        // shows "Connection reset — ready to
                                        // receive." as confirmation.
                                        fileShareController.hardReset()
                                        root.close()
                                    }
                                }
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
                        text: "Quit Scotty"
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
