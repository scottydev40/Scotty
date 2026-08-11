import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

Item {
    Layout.fillWidth: true
    height: 80

    signal settingsRequested()

    readonly property color textPrimary: Theme.textPrimary
    readonly property color textMuted: Theme.textMuted
    readonly property color accent: Theme.accentColor

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 24

        ColumnLayout {
            spacing: 2
            Label {
                text: "Device name"
                font.pixelSize: 12
                color: textMuted
            }
            Label {
                text: fileShareController.deviceName
                font.pixelSize: 22
                font.weight: Font.Medium
                color: textPrimary
            }
        }

        Item { Layout.fillWidth: true }



        // My-Devices: "Sign in" pill when the opt-in plugin is present and
        // no account is signed in. Hidden entirely on the stock (no-plugin)
        // build, so the clean core shows no account UI.
        Rectangle {
            visible: fileShareController.mydevicesAvailable
                     && fileShareController.signedInEmail.length === 0
            Layout.rightMargin: 8
            Layout.preferredWidth: signInLabel.implicitWidth + 28
            Layout.preferredHeight: 36
            radius: 18
            color: signInArea.containsMouse ? Theme.rowFill : "transparent"
            border.color: Theme.accentColor
            border.width: 1

            Label {
                id: signInLabel
                anchors.centerIn: parent
                text: "Sign in"
                font.pixelSize: 14
                color: textPrimary
            }
            MouseArea {
                id: signInArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: fileShareController.requestMyDevicesSignIn()
            }
        }

        // Account avatar (first letter) once signed in.
        Rectangle {
            visible: fileShareController.signedInEmail.length > 0
            Layout.rightMargin: 8
            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            radius: 18
            color: accent
            clip: true

            // Profile photo when available, else the initial letter.
            Image {
                anchors.fill: parent
                visible: fileShareController.signedInPhotoPath.length > 0
                source: fileShareController.signedInPhotoPath.length > 0
                        ? "file://" + fileShareController.signedInPhotoPath : ""
                fillMode: Image.PreserveAspectCrop
                layer.enabled: true
            }
            Label {
                anchors.centerIn: parent
                visible: fileShareController.signedInPhotoPath.length === 0
                text: fileShareController.signedInEmail.length > 0
                      ? fileShareController.signedInEmail.charAt(0).toUpperCase() : ""
                font.pixelSize: 16
                font.weight: Font.Medium
                color: "white"
            }
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: accountMenu.open()
            }

            // Account dropdown — follows this app's existing, crash-free Menu
            // pattern (see SideBar.qml visibilityMenu). Menu handles its own
            // popup + positioning relative to the avatar.
            Menu {
                id: accountMenu
                y: parent.height + 8
                x: parent.width - width
                width: 280
                padding: 6

                background: Rectangle {
                    radius: 12
                    color: Theme.surface
                    border.color: Theme.accentColor
                    border.width: 1
                }

                // Account identity — a non-interactive header row.
                Item {
                    implicitWidth: 280
                    implicitHeight: 58
                    Row {
                        anchors.fill: parent
                        anchors.margins: 11
                        spacing: 12
                        Rectangle {
                            width: 36; height: 36; radius: 18; color: accent; clip: true
                            Image {
                                anchors.fill: parent
                                visible: fileShareController.signedInPhotoPath.length > 0
                                source: fileShareController.signedInPhotoPath.length > 0
                                        ? "file://" + fileShareController.signedInPhotoPath : ""
                                fillMode: Image.PreserveAspectCrop
                            }
                            Label {
                                anchors.centerIn: parent
                                visible: fileShareController.signedInPhotoPath.length === 0
                                text: fileShareController.signedInEmail.length > 0
                                      ? fileShareController.signedInEmail.charAt(0).toUpperCase() : ""
                                color: "white"; font.pixelSize: 16; font.weight: Font.Medium
                            }
                        }
                        Column {
                            width: parent.width - 48
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1
                            Label {
                                visible: fileShareController.signedInName.length > 0
                                width: parent.width
                                text: fileShareController.signedInName
                                color: textPrimary; font.pixelSize: 14; font.weight: Font.Medium
                                elide: Text.ElideRight
                            }
                            Label {
                                width: parent.width
                                text: fileShareController.signedInEmail
                                color: textMuted; font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                MenuSeparator { }

                MenuItem {
                    implicitHeight: 40
                    contentItem: Row {
                        spacing: 10
                        Item { width: 4; height: 1 }
                        Label { text: "⏻"; color: textPrimary; font.pixelSize: 15
                                anchors.verticalCenter: parent.verticalCenter }
                        Label { text: "Sign out"; color: textPrimary; font.pixelSize: 14
                                anchors.verticalCenter: parent.verticalCenter }
                    }
                    background: Rectangle {
                        radius: 8
                        color: parent.highlighted ? Theme.rowFill : "transparent"
                    }
                    onTriggered: fileShareController.signOutMyDevices()
                }
            }
        }

        Rectangle {
            width: 40
            height: 40
            radius: 12
            color: settingsBtn.containsMouse ? Theme.rowFill : "transparent"
            border.color: settingsBtn.containsMouse ? Theme.accentColor : "transparent"

            Item {
                anchors.centerIn: parent
                width: 22
                height: 22
                Image {
                    id: gearGlyph
                    anchors.fill: parent
                    visible: false
                    smooth: true
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: 44
                    sourceSize.height: 44
                    source: "qrc:/icons/gear.svg"
                }
                MultiEffect {
                    source: gearGlyph
                    anchors.fill: gearGlyph
                    colorization: 1.0
                    colorizationColor: textMuted
                }
            }

            MouseArea {
                id: settingsBtn
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: settingsRequested()
            }
        }
    }
}
