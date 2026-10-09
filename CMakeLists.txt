cmake_minimum_required(VERSION 3.20)
project(trace_prism_sdk_cpp LANGUAGES CXX)

find_package(nlohmann_json 3.11 REQUIRED)

add_library(traceprism INTERFACE)
target_compile_features(traceprism INTERFACE cxx_std_23)
target_include_directories(traceprism INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(traceprism INTERFACE nlohmann_json::nlohmann_json)

add_executable(binary_search examples/binary_search.cpp)
target_link_libraries(binary_search PRIVATE traceprism)
target_compile_definitions(binary_search PRIVATE $<$<CONFIG:Debug>:DEBUG>)
