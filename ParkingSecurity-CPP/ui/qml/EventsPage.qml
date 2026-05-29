import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    header: ToolBar {
        Label {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 16
            text: "Event Log"
            font.pixelSize: 18
        }
    }

    // TODO: full filterable event log with expandable rows + lightbox (mirroring the React Events page).
    Rectangle {
        anchors.fill: parent
        anchors.margins: 16
        color: "#1F2937"
        radius: 12

        Text {
            anchors.centerIn: parent
            text: "Events page — implementation pending"
            color: "#9CA3AF"
            font.pixelSize: 14
        }
    }
}
