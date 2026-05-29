import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    header: ToolBar {
        Label {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 16
            text: "Analytics"
            font.pixelSize: 18
        }
    }

    // TODO: charts (daily bar chart, hourly heatmap, top people). Use QtCharts
    // module or QML Canvas for custom rendering.
    Rectangle {
        anchors.fill: parent
        anchors.margins: 16
        color: "#1F2937"
        radius: 12

        Text {
            anchors.centerIn: parent
            text: "Analytics — implementation pending"
            color: "#9CA3AF"
            font.pixelSize: 14
        }
    }
}
