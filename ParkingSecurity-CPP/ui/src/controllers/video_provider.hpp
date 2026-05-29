#pragma once

#include <QImage>
#include <QQuickImageProvider>

namespace parking::camera { class CameraPipeline; }

namespace parking::ui {

/// Supplies the latest annotated camera frame to QML via `Image { source: "image://camera/latest" }`.
/// The VideoProvider pulls the newest frame from the pipeline's FrameBuffer
/// and returns it as a QImage. For smooth playback, the QML side should
/// refresh on a Timer or via a property binding.
///
/// For the final production version, switch to a Qt `QVideoSink` pipeline
/// that shares GPU memory with DeepStream (zero-copy).
class VideoProvider final : public QQuickImageProvider {
public:
    explicit VideoProvider(camera::CameraPipeline& pipeline);

    QImage requestImage(const QString& id,
                         QSize* size,
                         const QSize& requestedSize) override;

private:
    camera::CameraPipeline& pipeline_;
};

}  // namespace parking::ui
