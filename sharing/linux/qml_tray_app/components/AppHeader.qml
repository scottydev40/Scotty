import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    Layout.fillWidth: true
    height: 80

    signal settingsRequested()

    readonly property color textPrimary: "#111827"
    readonly property color textMuted: "#6b7280"
    readonly property color accent: "#16a34a"

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
            color: settingsBtn.containsMouse ? "#dcfce7" : "transparent"
            border.color: settingsBtn.containsMouse ? "#86efac" : "transparent"

            Image {
                anchors.centerIn: parent
                width: 22
                height: 22
                smooth: true
                fillMode: Image.PreserveAspectFit
                sourceSize.width: 44
                sourceSize.height: 44
                source: "qrc:/icons/gear.svg"
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
