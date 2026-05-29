# FindTensorRT.cmake
# Locates the NVIDIA TensorRT libraries on a system (typically the Jetson).
#
# Variables set:
#   TensorRT_FOUND         - TRUE if TensorRT was found
#   TensorRT_VERSION       - Detected version (e.g. 10.3.0)
#   TensorRT_INCLUDE_DIRS  - Include directories
#   TensorRT_LIBRARIES     - All TensorRT libraries
#
# Imported targets:
#   TensorRT::TensorRT    - Combined target with headers + libraries

set(TensorRT_SEARCH_PATHS
    /usr/include/aarch64-linux-gnu
    /usr/include/x86_64-linux-gnu
    /usr/local/tensorrt/include
    /opt/tensorrt/include
)

find_path(TensorRT_INCLUDE_DIR NvInfer.h
    HINTS ${TensorRT_SEARCH_PATHS}
    DOC   "TensorRT include directory"
)

find_library(TensorRT_NVINFER_LIB        nvinfer          PATHS /usr/lib/aarch64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/local/tensorrt/lib)
find_library(TensorRT_NVINFER_PLUGIN_LIB nvinfer_plugin   PATHS /usr/lib/aarch64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/local/tensorrt/lib)
find_library(TensorRT_NVONNX_PARSER_LIB  nvonnxparser     PATHS /usr/lib/aarch64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/local/tensorrt/lib)

# Extract version from NvInferVersion.h if present
if(TensorRT_INCLUDE_DIR AND EXISTS "${TensorRT_INCLUDE_DIR}/NvInferVersion.h")
    file(READ "${TensorRT_INCLUDE_DIR}/NvInferVersion.h" _trt_header)
    string(REGEX MATCH "NV_TENSORRT_MAJOR ([0-9]+)" _m "${_trt_header}")
    set(TensorRT_VERSION_MAJOR ${CMAKE_MATCH_1})
    string(REGEX MATCH "NV_TENSORRT_MINOR ([0-9]+)" _m "${_trt_header}")
    set(TensorRT_VERSION_MINOR ${CMAKE_MATCH_1})
    string(REGEX MATCH "NV_TENSORRT_PATCH ([0-9]+)" _m "${_trt_header}")
    set(TensorRT_VERSION_PATCH ${CMAKE_MATCH_1})
    set(TensorRT_VERSION "${TensorRT_VERSION_MAJOR}.${TensorRT_VERSION_MINOR}.${TensorRT_VERSION_PATCH}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(TensorRT
    REQUIRED_VARS TensorRT_INCLUDE_DIR TensorRT_NVINFER_LIB
    VERSION_VAR   TensorRT_VERSION
)

if(TensorRT_FOUND)
    set(TensorRT_INCLUDE_DIRS ${TensorRT_INCLUDE_DIR})
    set(TensorRT_LIBRARIES
        ${TensorRT_NVINFER_LIB}
        ${TensorRT_NVINFER_PLUGIN_LIB}
        ${TensorRT_NVONNX_PARSER_LIB}
    )

    if(NOT TARGET TensorRT::TensorRT)
        add_library(TensorRT::TensorRT INTERFACE IMPORTED)
        set_target_properties(TensorRT::TensorRT PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${TensorRT_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES      "${TensorRT_LIBRARIES}"
        )
    endif()
endif()

mark_as_advanced(
    TensorRT_INCLUDE_DIR
    TensorRT_NVINFER_LIB
    TensorRT_NVINFER_PLUGIN_LIB
    TensorRT_NVONNX_PARSER_LIB
)
