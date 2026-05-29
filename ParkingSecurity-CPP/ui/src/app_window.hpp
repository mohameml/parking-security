#pragma once

namespace parking {
class EventBus;
namespace database { class ConnectionPool; }
namespace camera   { class CameraPipeline; }
}  // namespace parking

namespace parking::ui {

/// Runs the Qt application event loop. Blocks until the user closes the window
/// or a signal asks for shutdown. Returns the Qt exit code.
int run_app(int argc, char** argv,
             EventBus& bus,
             database::ConnectionPool& db_pool,
             camera::CameraPipeline& pipeline);

}  // namespace parking::ui
