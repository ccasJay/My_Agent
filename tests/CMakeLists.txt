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
    ../src/cli.cpp
    test_shell.cpp
    test_config.cpp
    test_agent_loop.cpp
    test_cli.cpp
    test_log_block.cpp
    test_log_viewport.cpp
    test_model.cpp
    test_prompt_history.cpp
    test_run_status.cpp
    test_tui_session.cpp
    test_tui_state.cpp
)

target_link_libraries(swe_agent_tests
    PRIVATE
        swe_agent_core
        swe_agent_tui_support
        Catch2::Catch2WithMain
        CLI11::CLI11
)

target_compile_options(swe_agent_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_test(NAME swe_agent_tests COMMAND swe_agent_tests)
