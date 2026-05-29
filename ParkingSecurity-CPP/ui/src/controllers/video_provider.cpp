#include "video_provider.hpp"

#include "parking/camera/camera_pipeline.hpp"

namespace parking::ui {

VideoProvider::VideoProvider(camera::CameraPipeline& pipeline)
    : QQuickImageProvider(QQuickImageProvider::Image), pipeline_(pipeline) {}

QImage VideoProvider::requestImage(const QString& /*id*/,
                                     QSize* /*size*/,
                                     const QSize& /*requestedSize*/) {
    // TODO: decode the latest FrameBuffer entry into a QImage.
    // For now, return a blank placeholder to keep the QML valid.
    return QImage(1280, 720, QImage::Format_RGB888);
}

}  // namespace parking::ui
