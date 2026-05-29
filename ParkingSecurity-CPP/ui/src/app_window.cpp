#include "app_window.hpp"

#include "parking/camera/camera_pipeline.hpp"
#include "parking/core/event_bus.hpp"
#include "parking/core/logger.hpp"
#include "parking/database/db_connection.hpp"

#include "models/event_model.hpp"
#include "models/people_model.hpp"
#include "controllers/video_provider.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQuickControls2/QQuickStyle>

namespace parking::ui {

int run_app(int argc, char** argv,
             EventBus& bus,
             database::ConnectionPool& db_pool,
             camera::CameraPipeline& pipeline) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Parking Security");
    app.setApplicationName("Dashboard");
    app.setApplicationVersion("1.0.0");
    QQuickStyle::setStyle("Material");

    QQmlApplicationEngine engine;

    // Models exposed to QML
    auto event_model  = std::make_unique<EventModel>(bus);
    auto people_model = std::make_unique<PeopleModel>(db_pool);
    auto video_provider = new VideoProvider(pipeline);

    engine.rootContext()->setContextProperty("eventModel",  event_model.get());
    engine.rootContext()->setContextProperty("peopleModel", people_model.get());
    engine.addImageProvider("camera", video_provider);

    engine.load(QUrl("qrc:/qt/qml/Parking/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) {
        PLOG_ERROR(ui, "Failed to load QML");
        return 1;
    }

    return app.exec();
}

}  // namespace parking::ui
