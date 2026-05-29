import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Label {
                text: "People"
                font.pixelSize: 18
                Layout.leftMargin: 16
            }
            Item { Layout.fillWidth: true }
            Text {
                text: peopleModel.totalCount + " enrolled"
                color: "#9CA3AF"
                Layout.rightMargin: 16
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TextField {
                placeholderText: "Search by ID or name..."
                Layout.fillWidth: true
                onTextChanged: peopleModel.search = text
            }

            Repeater {
                model: ["all", "employee", "student"]
                delegate: Button {
                    text: modelData
                    flat: peopleModel.typeFilter !== modelData
                    highlighted: peopleModel.typeFilter === modelData
                    onClicked: peopleModel.typeFilter = modelData
                }
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: peopleModel
            clip: true
            spacing: 4

            delegate: Rectangle {
                width: ListView.view.width
                height: 56
                color: "#1F2937"
                radius: 8

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    Image {
                        source: photoPath ? ("file://" + photoPath) : ""
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        fillMode: Image.PreserveAspectCrop
                    }
                    ColumnLayout {
                        spacing: 0
                        Text { text: personName; color: "white" }
                        Text { text: personType; color: "#9CA3AF"; font.pixelSize: 11 }
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: embeddingCount + " embeddings"
                        color: embeddingCount > 0 ? "#10B981" : "#EF4444"
                        font.pixelSize: 11
                    }
                }
            }
        }

        // Pagination controls
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Button { text: "Prev"; onClicked: peopleModel.page = Math.max(1, peopleModel.page - 1) }
            Label  { text: "Page " + peopleModel.page; color: "white" }
            Button { text: "Next"; onClicked: peopleModel.page++ }
        }
    }
}
