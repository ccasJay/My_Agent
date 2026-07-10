cmake_minimum_required(VERSION 3.20)

project(swe_agent_model LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(swe_agent_model
    src/openai_compatible_client.cpp
)

target_include_directories(swe_agent_model
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(swe_agent_model PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_executable(swe-agent src/main.cpp)
target_link_libraries(swe-agent PRIVATE swe_agent_model)
