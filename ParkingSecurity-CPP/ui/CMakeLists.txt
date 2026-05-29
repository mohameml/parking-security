# Qt/QML dashboard UI library. Linked into the main parking-security executable.

qt_add_library(parking_ui STATIC
    src/app_window.cpp
    src/models/event_model.cpp
    src/models/people_model.cpp
    src/controllers/video_provider.cpp
)

target_include_directories(parking_ui
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ui/src>
)

qt_add_qml_module(parking_ui
    URI Parking
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/LiveView.qml
        qml/EventsPage.qml
        qml/PeoplePage.qml
        qml/AnalyticsPage.qml
        qml/components/EventCard.qml
        qml/components/CameraWidget.qml
        qml/components/StatCard.qml
    RESOURCES
        icons/shield.svg
)

target_link_libraries(parking_ui
    PUBLIC
        parking_core
        Qt6::Core
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
)

# Public header for main.cpp to call into.
target_sources(parking_ui PUBLIC
    FILE_SET ui_headers TYPE HEADERS
        BASE_DIRS ${CMAKE_SOURCE_DIR}/ui
        FILES     ${CMAKE_SOURCE_DIR}/ui/src/app_window.hpp
)
