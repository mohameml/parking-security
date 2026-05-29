# Unit tests (GoogleTest). Opt-in via -DPARKING_BUILD_TESTS=ON.

find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(parking_tests
    test_config.cpp
    test_embedding_index.cpp
    test_event_bus.cpp
)

target_link_libraries(parking_tests
    PRIVATE parking_core gtest gtest_main
)

include(GoogleTest)
gtest_discover_tests(parking_tests)
