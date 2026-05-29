import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    width: 160
    height: 72
    radius: 12
    color: "#1F2937"

    property string label: ""
    property string value: ""
    property color  accent: "#3B82F6"

    Column {
        anchors.centerIn: parent
        spacing: 2
        Text {
            text: root.value
            color: root.accent
            font.pixelSize: 22
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            text: root.label
            color: "#9CA3AF"
            font.pixelSize: 11
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
