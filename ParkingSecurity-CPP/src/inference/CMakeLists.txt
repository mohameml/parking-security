target_sources(parking_core PRIVATE
    embedding_index.cpp
    scrfd_decoder.cpp
)

# Custom nvinfer bbox parser for SCRFD — built as a SHARED library because
# nvinfer dlopen's it at runtime (path referenced from face_detection.txt).
if(PARKING_USE_DEEPSTREAM)
    add_library(nvds_parsebbox_scrfd SHARED nvdsparsebbox_scrfd.cpp)
    target_link_libraries(nvds_parsebbox_scrfd PRIVATE
        DeepStream::DeepStream
        CUDA::cudart
        TensorRT::TensorRT
    )
    set_target_properties(nvds_parsebbox_scrfd PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib"
        PREFIX "lib"
    )
    install(TARGETS nvds_parsebbox_scrfd LIBRARY DESTINATION lib)
endif()
