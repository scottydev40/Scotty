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
