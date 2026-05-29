import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: true
    width: 1280
    height: 720
    title: "Parking Security"

    Material.theme: Material.Dark
    Material.accent: Material.Blue
    Material.primary: "#1E3A8A"

    // ─── Sidebar + content layout ────────────────────────────
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Sidebar
        Rectangle {
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            color: "#1F2937"

            ColumnLayout {
                anchors.fill: parent
                spacing: 4

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    color: "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 8

                        Image {
                            source: "qrc:/qt/qml/Parking/icons/shield.svg"
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                        }

                        ColumnLayout {
                            spacing: 0
                            Text {
                                text: "Parking Security"
                                color: "white"
                                font.pixelSize: 14
                                font.bold: true
                            }
                            Text {
                                text: "Face Recognition System"
                                color: "#9CA3AF"
                                font.pixelSize: 10
                            }
                        }
                    }
                }

                Repeater {
                    model: [
                        { name: "Dashboard",  page: 0 },
                        { name: "Events",     page: 1 },
                        { name: "People",     page: 2 },
                        { name: "Analytics",  page: 3 },
                    ]
                    delegate: Button {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        text: modelData.name
                        flat: true
                        onClicked: stackView.currentIndex = modelData.page

                        contentItem: Text {
                            text: parent.text
                            color: stackView.currentIndex === modelData.page ? "white" : "#D1D5DB"
                            font.pixelSize: 14
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: Text.AlignLeft
                            leftPadding: 12
                        }

                        background: Rectangle {
                            color: stackView.currentIndex === modelData.page ? "#2563EB" : "transparent"
                            radius: 8
                        }
                    }
                }

                Item { Layout.fillHeight: true }   // spacer

                // Footer
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 50
                    color: "#111827"

                    Text {
                        anchors.centerIn: parent
                        text: "v1.0.0 · native"
                        color: "#6B7280"
                        font.pixelSize: 10
                    }
                }
            }
        }

        // Main content
        StackLayout {
            id: stackView
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: 0

            LiveView       { }
            EventsPage     { }
            PeoplePage     { }
            AnalyticsPage  { }
        }
    }
}
