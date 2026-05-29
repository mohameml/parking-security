# FindDeepStream.cmake
# Locates NVIDIA DeepStream SDK headers and libraries.
#
# Default install location on Jetson: /opt/nvidia/deepstream/deepstream/
# The `deepstream` dir is usually a symlink to a versioned dir like `deepstream-7.1`.

set(DeepStream_ROOT_HINTS
    /opt/nvidia/deepstream/deepstream
    /opt/nvidia/deepstream/deepstream-7.1
    /opt/nvidia/deepstream/deepstream-7.0
    /opt/nvidia/deepstream/deepstream-6.4
    /opt/nvidia/deepstream/deepstream-6.3
    $ENV{DEEPSTREAM_ROOT}
)

find_path(DeepStream_ROOT_DIR sources/includes/nvds_version.h
    HINTS ${DeepStream_ROOT_HINTS}
    DOC   "Root directory of DeepStream SDK"
)

if(DeepStream_ROOT_DIR)
    set(DeepStream_INCLUDE_DIR "${DeepStream_ROOT_DIR}/sources/includes")

    # Find the bufsurface and meta libraries (all DeepStream 7 flavors use these).
    find_library(DeepStream_NVBUFSURFACE_LIB   nvbufsurface           HINTS ${DeepStream_ROOT_DIR}/lib)
    find_library(DeepStream_NVBUFSURFTRANSFORM nvbufsurftransform     HINTS ${DeepStream_ROOT_DIR}/lib)
    find_library(DeepStream_NVDS_META_LIB      nvds_meta              HINTS ${DeepStream_ROOT_DIR}/lib)
    find_library(DeepStream_NVDSGST_META_LIB   nvdsgst_meta           HINTS ${DeepStream_ROOT_DIR}/lib)
    find_library(DeepStream_NVDS_UTILS_LIB     nvds_utils             HINTS ${DeepStream_ROOT_DIR}/lib)
    find_library(DeepStream_NVDS_INFER_LIB     nvds_infer             HINTS ${DeepStream_ROOT_DIR}/lib)

    # Extract version from nvds_version.h
    if(EXISTS "${DeepStream_INCLUDE_DIR}/nvds_version.h")
        file(READ "${DeepStream_INCLUDE_DIR}/nvds_version.h" _ds_header)
        string(REGEX MATCH "NVDS_VERSION_MAJOR[ \t]+([0-9]+)" _m "${_ds_header}")
        set(DeepStream_VERSION_MAJOR ${CMAKE_MATCH_1})
        string(REGEX MATCH "NVDS_VERSION_MINOR[ \t]+([0-9]+)" _m "${_ds_header}")
        set(DeepStream_VERSION_MINOR ${CMAKE_MATCH_1})
        if(DeepStream_VERSION_MAJOR AND DeepStream_VERSION_MINOR)
            set(DeepStream_VERSION "${DeepStream_VERSION_MAJOR}.${DeepStream_VERSION_MINOR}")
        endif()
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DeepStream
    REQUIRED_VARS
        DeepStream_ROOT_DIR
        DeepStream_INCLUDE_DIR
        DeepStream_NVDS_META_LIB
        DeepStream_NVBUFSURFACE_LIB
    VERSION_VAR DeepStream_VERSION
)

if(DeepStream_FOUND AND NOT TARGET DeepStream::DeepStream)
    add_library(DeepStream::DeepStream INTERFACE IMPORTED)

    set(_ds_libs ${DeepStream_NVDS_META_LIB} ${DeepStream_NVBUFSURFACE_LIB})
    if(DeepStream_NVBUFSURFTRANSFORM)
        list(APPEND _ds_libs ${DeepStream_NVBUFSURFTRANSFORM})
    endif()
    if(DeepStream_NVDSGST_META_LIB)
        list(APPEND _ds_libs ${DeepStream_NVDSGST_META_LIB})
    endif()
    if(DeepStream_NVDS_UTILS_LIB)
        list(APPEND _ds_libs ${DeepStream_NVDS_UTILS_LIB})
    endif()
    if(DeepStream_NVDS_INFER_LIB)
        list(APPEND _ds_libs ${DeepStream_NVDS_INFER_LIB})
    endif()

    set_target_properties(DeepStream::DeepStream PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${DeepStream_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES      "${_ds_libs}"
    )
endif()

mark_as_advanced(
    DeepStream_ROOT_DIR
    DeepStream_INCLUDE_DIR
    DeepStream_NVDS_META_LIB
    DeepStream_NVBUFSURFACE_LIB
    DeepStream_NVBUFSURFTRANSFORM
    DeepStream_NVDSGST_META_LIB
    DeepStream_NVDS_UTILS_LIB
    DeepStream_NVDS_INFER_LIB
)
