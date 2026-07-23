import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import "components"

ApplicationWindow {
    id: root
    width: 980
    height: 760
    minimumWidth: 820
    minimumHeight: 620
    visible: true
    title: "Quick Share"

    background: Rectangle { color: "#f0fdf4" }

    onClosing: function(close) {
        close.accepted = false
        root.hide()
        fileShareController.hideToTray()
    }

    SettingsPanel {
        id: settingsPanel
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        AppHeader {
            onSettingsRequested: settingsPanel.open()
        }

        // ── Body ─────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            SideBar {}

            // ── Main content (white panel) ────────────────────────────────
            Rectangle {
                id: mainContent
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#ffffff"
                topLeftRadius: 48
                clip: true

                readonly property bool isSendMode: fileShareController.pendingSendFilePath.length > 0

                // Count of incoming transfers. Receive-side rows are driven off the
                // transfers list directly (not discoveredTargets): the sender never
                // reliably lands in the discovery list, so keying receive display off
                // discovery left received files invisible.
                readonly property int incomingCount: {
                    var n = 0
                    var t = fileShareController.transfers
                    for (var i = 0; i < t.length; ++i)
                        if (t[i] && String(t[i].direction || "") === "incoming") n++
                    return n
                }

                // ── Idle: animated blob (only when nothing is going on) ───
                AnimatedBlob {
                    visible: !mainContent.isSendMode
                             && mainContent.incomingCount === 0
                }

                FileDialog {
                    id: sendFileDialog
                    title: "Select file(s) to send"
                    fileMode: FileDialog.OpenFiles
                    onAccepted: {
                        const paths = selectedFiles.map(
                            u => u.toString().replace(/^file:\/\//, ""))
                        fileShareController.switchToSendModeWithFiles(paths)
                    }
                }

                // Idle: the AnimatedBlob (above) is the centerpiece. The send-file
                // initiator lives in the sidebar; whole-window drag-drop (below)
                // still starts a send.

                // ── Send mode OR an incoming transfer: full-width device rows ──
                Flickable {
                    id: mainFlickable
                    anchors.fill: parent
                    clip: true
                    visible: mainContent.isSendMode
                             || mainContent.incomingCount > 0
                    contentWidth: width
                    contentHeight: mainCol.implicitHeight + 96
                    ScrollBar.vertical: ScrollBar {}
                    z: 1

                    ColumnLayout {
                        id: mainCol
                        x: 48
                        y: 48
                        width: mainFlickable.width - 96
                        spacing: 16

                        // ── Send: discovered nearby devices ──────────────
                        Label {
                            visible: mainContent.isSendMode
                            text: "Nearby devices"
                            font.pixelSize: 20
                            font.weight: Font.Medium
                            color: "#111827"
                        }

                        Repeater {
                            // Only in send mode; receive rows come from the
                            // incoming Repeater below so senders never double-render.
                            model: mainContent.isSendMode
                                   ? fileShareController.discoveredTargets : []
                            delegate: DeviceRow {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 84
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: mainContent.isSendMode
                                     && fileShareController.discoveredTargets.length === 0
                            text: "Looking for nearby devices…"
                            font.pixelSize: 13
                            color: "#6b7280"
                        }

                        // ── Receive: incoming transfers ──────────────────
                        Label {
                            visible: mainContent.incomingCount > 0
                            text: "Incoming"
                            font.pixelSize: 20
                            font.weight: Font.Medium
                            color: "#111827"
                        }

                        Repeater {
                            model: fileShareController.transfers
                            delegate: Item {
                                id: inWrap
                                required property var modelData   // transfer entry
                                readonly property bool isIncoming:
                                    modelData && String(modelData.direction || "") === "incoming"
                                Layout.fillWidth: true
                                Layout.preferredHeight: isIncoming ? 84 : 0
                                visible: isIncoming

                                // Adapt a transfer entry {targetId,targetName,…} to the
                                // {id,name,deviceType} shape DeviceRow expects; DeviceRow
                                // then matches the live transfer back by id.
                                DeviceRow {
                                    anchors.fill: parent
                                    modelData: ({
                                        id: inWrap.modelData ? inWrap.modelData.targetId : 0,
                                        name: inWrap.modelData ? inWrap.modelData.targetName : "",
                                        deviceType: 0
                                    })
                                }
                            }
                        }
                    }
                }

                // ── Drop a file anywhere → switch to send mode ────────────
                DropArea {
                    id: dropArea
                    anchors.fill: parent
                    z: 2
                    keys: ["text/uri-list"]

                    onDropped: function(drop) {
                        if (drop.hasUrls && drop.urls.length > 0) {
                            const paths = drop.urls.map(
                                u => u.toString().replace(/^file:\/\//, ""))
                            fileShareController.switchToSendModeWithFiles(paths)
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        visible: dropArea.containsDrag
                        color: "#16a34a"
                        opacity: 0.12
                        radius: mainContent.topLeftRadius

                        Label {
                            anchors.centerIn: parent
                            text: "Drop to send"
                            font.pixelSize: 22
                            font.weight: Font.DemiBold
                            color: "#16a34a"
                        }
                    }
                }
            }
        }
    }
}
