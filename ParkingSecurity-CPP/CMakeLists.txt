cmake_minimum_required(VERSION 3.20)
project(parking-security
    VERSION 1.0.0
    DESCRIPTION "Real-time face recognition access control (C++ native)"
    LANGUAGES CXX CUDA
)

# ─── Project-wide options ─────────────────────────────────────────

option(PARKING_BUILD_UI       "Build the Qt/QML dashboard UI"            ON)
option(PARKING_BUILD_TESTS    "Build unit tests"                         OFF)
option(PARKING_BUILD_TOOLS    "Build standalone CLI tools"               ON)
option(PARKING_USE_DEEPSTREAM "Use NVIDIA DeepStream SDK (vs raw GStr.)" ON)
option(PARKING_TARGET_JETSON  "Enable Jetson-specific optimizations"     ON)

# ─── Compiler settings ────────────────────────────────────────────

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# CUDA architectures for Jetson Orin (Ampere, compute 8.7)
if(PARKING_TARGET_JETSON AND NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 87)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(CompilerOptions)

# ─── Dependencies ─────────────────────────────────────────────────

find_package(CUDAToolkit REQUIRED)
find_package(TensorRT REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER  REQUIRED IMPORTED_TARGET  gstreamer-1.0)
pkg_check_modules(GSTAPP     REQUIRED IMPORTED_TARGET  gstreamer-app-1.0)
pkg_check_modules(GSTVIDEO   REQUIRED IMPORTED_TARGET  gstreamer-video-1.0)
pkg_check_modules(GLIB       REQUIRED IMPORTED_TARGET  glib-2.0)

if(PARKING_USE_DEEPSTREAM)
    find_package(DeepStream REQUIRED)
endif()

# spdlog for logging
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.14.1
    )
    FetchContent_MakeAvailable(spdlog)
endif()

# nlohmann/json for JSON handling
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_MakeAvailable(json)
endif()

# libpqxx for PostgreSQL. Ubuntu 22.04 packages it without a CMake config,
# so fall back to pkg-config.
find_package(libpqxx CONFIG QUIET)
if(NOT libpqxx_FOUND)
    pkg_check_modules(PQXX IMPORTED_TARGET libpqxx)
    if(PQXX_FOUND)
        add_library(libpqxx::pqxx ALIAS PkgConfig::PQXX)
    else()
        # Manual fallback for old systems without pkg-config info
        find_path(PQXX_INCLUDE_DIR pqxx/pqxx)
        find_library(PQXX_LIBRARY pqxx)
        find_library(PQ_LIBRARY pq)
        if(PQXX_INCLUDE_DIR AND PQXX_LIBRARY AND PQ_LIBRARY)
            add_library(libpqxx::pqxx INTERFACE IMPORTED)
            set_target_properties(libpqxx::pqxx PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${PQXX_INCLUDE_DIR}"
                INTERFACE_LINK_LIBRARIES      "${PQXX_LIBRARY};${PQ_LIBRARY}"
            )
        else()
            message(FATAL_ERROR "libpqxx not found. Install libpqxx-dev.")
        endif()
    endif()
endif()

# OpenCV (used minimally for CPU-side image utilities)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

# libcurl — used by EventPublisher to POST live events to the Python
# backend (which broadcasts them over WebSocket to the dashboard).
find_package(CURL REQUIRED)

# Qt 6 (for UI)
if(PARKING_BUILD_UI)
    find_package(Qt6 REQUIRED COMPONENTS Core Quick QuickControls2 Gui Widgets Network)
    qt_standard_project_setup()
endif()

# ─── Shared library: parking_core ─────────────────────────────────

add_library(parking_core STATIC)
target_include_directories(parking_core
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)
target_link_libraries(parking_core
    PUBLIC
        CUDA::cudart
        TensorRT::TensorRT
        PkgConfig::GSTREAMER
        PkgConfig::GSTAPP
        PkgConfig::GSTVIDEO
        PkgConfig::GLIB
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        libpqxx::pqxx
        opencv_core
        opencv_imgproc
        opencv_imgcodecs
        CURL::libcurl
)

if(PARKING_USE_DEEPSTREAM)
    target_link_libraries(parking_core PUBLIC DeepStream::DeepStream)
    target_compile_definitions(parking_core PUBLIC PARKING_USE_DEEPSTREAM=1)
endif()

# Module sources (each module adds its own files via add_subdirectory)
add_subdirectory(src/core)
add_subdirectory(src/camera)
add_subdirectory(src/inference)
add_subdirectory(src/tracking)
add_subdirectory(src/database)
add_subdirectory(src/alerts)
add_subdirectory(src/schedule)
add_subdirectory(src/api)

# ─── Main executable ──────────────────────────────────────────────

add_executable(parking-security src/main.cpp)
target_link_libraries(parking-security PRIVATE parking_core)

if(PARKING_BUILD_UI)
    add_subdirectory(ui)
    target_link_libraries(parking-security PRIVATE parking_ui)
endif()

# ─── Tools ────────────────────────────────────────────────────────

if(PARKING_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# ─── Tests ────────────────────────────────────────────────────────

if(PARKING_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ─── Install ──────────────────────────────────────────────────────

# ─── Install layout ───────────────────────────────────────────────
# When deployed to the Jetson we expect:
#   /opt/parking-security/bin/parking-security
#   /opt/parking-security/config/*
#   /opt/parking-security/models/*
#   /opt/parking-security/lib/libnvds_parsebbox_scrfd.so
#   /opt/parking-security/data/embeddings.json     (created by export script)
# The systemd unit is installed to /etc/systemd/system.

install(TARGETS parking-security RUNTIME DESTINATION bin)
install(DIRECTORY config/    DESTINATION config FILES_MATCHING
        PATTERN "*.txt" PATTERN "*.yml" PATTERN "*.ini"
        PATTERN "*.sql" PATTERN "*.yaml")
install(DIRECTORY models/    DESTINATION models OPTIONAL)
if(EXISTS "${CMAKE_SOURCE_DIR}/lib")
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/lib/ DESTINATION lib
            FILES_MATCHING PATTERN "*.so")
endif()

install(FILES scripts/parking-security.service
        DESTINATION share/parking-security
        RENAME parking-security.service)

# ─── Summary ──────────────────────────────────────────────────────

message(STATUS "")
message(STATUS "═══════════════════════════════════════════════════════")
message(STATUS "  Parking Security ${PROJECT_VERSION}")
message(STATUS "═══════════════════════════════════════════════════════")
message(STATUS "  Build type     : ${CMAKE_BUILD_TYPE}")
message(STATUS "  CUDA arch      : ${CMAKE_CUDA_ARCHITECTURES}")
message(STATUS "  UI (Qt)        : ${PARKING_BUILD_UI}")
message(STATUS "  DeepStream     : ${PARKING_USE_DEEPSTREAM}")
message(STATUS "  Jetson mode    : ${PARKING_TARGET_JETSON}")
message(STATUS "  Tools          : ${PARKING_BUILD_TOOLS}")
message(STATUS "  Tests          : ${PARKING_BUILD_TESTS}")
message(STATUS "═══════════════════════════════════════════════════════")
message(STATUS "")
