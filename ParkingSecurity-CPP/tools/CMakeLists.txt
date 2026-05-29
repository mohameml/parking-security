# Small CLI utilities that share code with the main binary.

add_executable(test_pipeline test_pipeline.cpp)
target_link_libraries(test_pipeline PRIVATE parking_core)
