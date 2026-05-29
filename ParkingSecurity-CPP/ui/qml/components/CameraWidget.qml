import QtQuick
import QtQuick.Controls

// Reusable wrapper around an Image bound to the camera image provider.
// For the production build, swap for a QVideoSink with GPU-shared NV12 buffers.
Item {
    id: root
    property int refreshInterval: 33   // ~30 FPS

    Image {
        id: feed
        anchors.fill: parent
        source: "image://camera/latest"
        fillMode: Image.PreserveAspectFit
        cache: false
    }

    Timer {
        interval: refreshInterval
        running: visible
        repeat: true
        onTriggered: feed.source = "image://camera/latest?" + Date.now()
    }
}
