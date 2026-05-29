import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Label {
                text: "Live Dashboard"
                font.pixelSize: 18
                Layout.leftMargin: 16
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                width: 90; height: 24
                radius: 12
                color: "#064E3B"
                Layout.rightMargin: 12
                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: "#10B981"
                        anchors.verticalCenter: parent.verticalCenter
                        SequentialAnimation on opacity {
                            running: true; loops: Animation.Infinite
                            NumberAnimation { from: 1.0; to: 0.3; duration: 800 }
                            NumberAnimation { from: 0.3; to: 1.0; duration: 800 }
                        }
                    }
                    Text { text: "LIVE"; color: "#6EE7B7"; font.pixelSize: 11; font.bold: true }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        // Camera feed
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: parent.height * 0.6
            color: "#111827"
            radius: 12

            Image {
                id: cameraFeed
                anchors.fill: parent
                anchors.margins: 2
                source: "image://camera/latest"
                fillMode: Image.PreserveAspectFit
                cache: false

                // Refresh ~30 times/second
                Timer {
                    interval: 33
                    running: cameraFeed.visible
                    repeat: true
                    onTriggered: cameraFeed.source = "image://camera/latest?" + Date.now()
                }
            }
        }

        // Recent events
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#1F2937"
            radius: 12

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                Text {
                    text: "Recent Events"
                    color: "white"
                    font.pixelSize: 16
                    font.bold: true
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: eventModel
                    clip: true
                    spacing: 6

                    delegate: EventCard {
                        width: ListView.view.width
                    }
                }
            }
        }
    }
}
