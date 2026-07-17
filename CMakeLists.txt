cmake_minimum_required(VERSION 3.20)

project(swe_agent_model LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)

FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(CLI11)

add_library(swe_agent_model
    src/agent_loader.cpp
    src/dotenv_loader.cpp
    src/http_client.cpp
    src/model_client.cpp
    src/process_result.cpp
    src/shell.cpp
)

target_include_directories(swe_agent_model
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(swe_agent_model
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE CURL::libcurl yaml-cpp::yaml-cpp
)

target_compile_options(swe_agent_model PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_executable(swe-agent
    src/main.cpp
    src/cli.cpp
)
set_target_properties(swe-agent PROPERTIES OUTPUT_NAME agent)
target_link_libraries(swe-agent PRIVATE swe_agent_model CLI11::CLI11)

install(TARGETS swe-agent
    RUNTIME DESTINATION bin
)

option(SWE_AGENT_BUILD_TESTS "Build unit tests" ON)
if(SWE_AGENT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
