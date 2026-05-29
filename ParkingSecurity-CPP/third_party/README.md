# Third-party dependencies

Most dependencies are resolved via `find_package()` against system-installed
libraries (GStreamer, CUDA, TensorRT, Qt, libpqxx, OpenCV). A few are
auto-fetched by CMake's `FetchContent` if not found:

- `spdlog`   — logging (v1.14.1)
- `nlohmann/json` — JSON (v3.11.3)
- `GoogleTest` — unit tests (v1.14.0, only when `PARKING_BUILD_TESTS=ON`)

This directory is reserved for any vendored third-party code that we want
under version control (e.g., a patched header we can't wait for upstream to fix).
Empty for now.
