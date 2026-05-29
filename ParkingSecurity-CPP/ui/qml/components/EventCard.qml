import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    height: 60
    radius: 8

    // Color mapping matches the React dashboard:
    //   authorized  -> green
    //   wrong_time  -> orange
    //   wrong_day   -> dark orange
    //   unknown     -> red
    property int    eventType: model.eventType
    property string personName: model.personName
    property real   confidence: model.confidence

    color: {
        if (eventType === 0) return "#064E3B";  // authorized
        if (eventType === 1) return "#9A3412";  // wrong_time
        if (eventType === 2) return "#7F1D1D";  // wrong_day
        return "#7F1D1D";                        // unknown
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        Image {
            source: faceImage || ""
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
            fillMode: Image.PreserveAspectCrop
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            Text { text: personName; color: "white"; font.bold: true }
            Text {
                text: (confidence * 100).toFixed(1) + "%"
                color: "#D1D5DB"; font.pixelSize: 11
            }
        }

        Text {
            text: {
                if (eventType === 0) return "AUTHORIZED";
                if (eventType === 1) return "WRONG TIME";
                if (eventType === 2) return "WRONG DAY";
                return "UNKNOWN";
            }
            color: "white"
            font.pixelSize: 11
            font.bold: true
            Layout.rightMargin: 8
        }
    }
}
