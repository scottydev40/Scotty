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

                // ── Idle: animated blob ───────────────────────────────────
                AnimatedBlob { visible: !mainContent.isSendMode }

                FileDialog {
                    id: sendFileDialog
                    title: "Select a file to send"
                    onAccepted: {
                        const path = selectedFile.toString().replace(/^file:\/\//, "")
                        fileShareController.switchToSendModeWithFile(path)
                    }
                }

                ColumnLayout {
                    visible: !mainContent.isSendMode
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 56
                    spacing: 8
                    z: 1

                    Button {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Select a file to send"
                        font.pixelSize: 15
                        padding: 14
                        background: Rectangle {
                            radius: 22
                            color: parent.down ? "#15803d" : parent.hovered ? "#16a34a" : "#22c55e"
                        }
                        contentItem: Label {
                            text: parent.text
                            font: parent.font
                            color: "#ffffff"
                            horizontalAlignment: Text.AlignHCenter
                        }
                        onClicked: sendFileDialog.open()
                    }

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: "or drop a file anywhere in this window"
                        font.pixelSize: 12
                        color: "#6b7280"
                    }
                }

                // ── Non-idle: scrollable device + transfer cards ──────────
                Flickable {
                    id: mainFlickable
                    anchors.fill: parent
                    clip: true
                    visible: mainContent.isSendMode
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

                        SendUrlPanel {
                            Layout.alignment: Qt.AlignHCenter
                            width: Math.max(240, Math.min(mainCol.width, 420))
                        }


                        Label {
                            text: "Nearby devices"
                            font.pixelSize: 20
                            font.weight: Font.Medium
                            color: "#111827"
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: deviceFlow.childrenRect.height
                            visible: fileShareController.discoveredTargets.length > 0

                            Flow {
                                id: deviceFlow
                                width: parent.width
                                spacing: 20

                                Repeater {
                                    model: fileShareController.discoveredTargets
                                    delegate: DeviceCard {}
                                }
                            }
                        }

                        TransferList {
                            Layout.fillWidth: true
                            Layout.topMargin: 16
                        }

                    }
                }

                // ── Idle: recent/incoming transfers over the blob ─────────
                Flickable {
                    visible: !mainContent.isSendMode
                             && fileShareController.transfers.length > 0
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.topMargin: 48
                    anchors.leftMargin: 48
                    anchors.rightMargin: 48
                    height: Math.min(idleTransferList.implicitHeight,
                                     parent.height * 0.5)
                    contentWidth: width
                    contentHeight: idleTransferList.implicitHeight
                    clip: true
                    z: 1

                    TransferList {
                        id: idleTransferList
                        width: parent.width
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
                            const path = drop.urls[0].toString().replace(/^file:\/\//, "")
                            fileShareController.switchToSendModeWithFile(path)
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
