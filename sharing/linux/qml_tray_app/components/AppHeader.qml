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

            Label {
                anchors.centerIn: parent
                text: fileShareController.signedInEmail.length > 0
                      ? fileShareController.signedInEmail.charAt(0).toUpperCase()
                      : ""
                font.pixelSize: 16
                font.weight: Font.Medium
                color: "white"
            }
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: fileShareController.signOutMyDevices()
                ToolTip.visible: containsMouse
                ToolTip.text: "Signed in as " + fileShareController.signedInEmail
                              + " — click to sign out"
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
