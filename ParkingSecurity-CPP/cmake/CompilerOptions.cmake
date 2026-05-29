# Cross-compiler options for the parking-security project.

add_library(parking_compiler_options INTERFACE)

# C++ warnings
target_compile_options(parking_compiler_options INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wmisleading-indentation
        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op
        -Wnull-dereference
        -Wuseless-cast
        -Wdouble-promotion
        -Wformat=2
    >
)

# Debug: enable sanitizers
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(parking_compiler_options INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -g
        >
    )
    target_link_options(parking_compiler_options INTERFACE
        -fsanitize=address,undefined
    )
endif()

# Release: aggressive optimizations for ARM Cortex-A78AE
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(parking_compiler_options INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:
            -O3
            -ffast-math
            -funroll-loops
            -DNDEBUG
        >
    )
    if(PARKING_TARGET_JETSON)
        target_compile_options(parking_compiler_options INTERFACE
            $<$<COMPILE_LANGUAGE:CXX>:
                -mcpu=cortex-a78
                -mtune=cortex-a78
            >
        )
    endif()
endif()

# CUDA optimizations
target_compile_options(parking_compiler_options INTERFACE
    $<$<COMPILE_LANGUAGE:CUDA>:
        --use_fast_math
        -O3
    >
)

link_libraries(parking_compiler_options)
