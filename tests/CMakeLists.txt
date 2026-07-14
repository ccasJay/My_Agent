include(CTest)
include(FetchContent)

find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4
    )
    FetchContent_MakeAvailable(Catch2)
endif()

add_executable(swe_agent_tests
    test_shell.cpp
    test_config.cpp
    test_agent_loop.cpp
    test_model.cpp
)

target_link_libraries(swe_agent_tests
    PRIVATE
        swe_agent_model
        Catch2::Catch2WithMain
)

target_compile_options(swe_agent_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_test(NAME swe_agent_tests COMMAND swe_agent_tests)
