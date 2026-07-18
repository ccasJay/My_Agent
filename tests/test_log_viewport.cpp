#include <catch2/catch_test_macros.hpp>

#include "tui/log_viewport.hpp"

TEST_CASE("log viewport follows appended lines", "[tui][viewport]") {
    swe_agent::tui::LogViewport viewport;

    REQUIRE(viewport.sync(3));
    REQUIRE(viewport.current_line() == 2);
    REQUIRE(viewport.following_tail());

    REQUIRE(viewport.sync(5));
    REQUIRE(viewport.current_line() == 4);
}

TEST_CASE("log viewport animates scrolling and resumes tail following", "[tui][viewport]") {
    swe_agent::tui::LogViewport viewport;
    (void)viewport.sync(10);

    REQUIRE(viewport.scroll_up(3));
    REQUIRE_FALSE(viewport.following_tail());
    REQUIRE(viewport.animation_pending());
    REQUIRE(viewport.tick());
    REQUIRE(viewport.current_line() == 8);
    REQUIRE(viewport.tick());
    REQUIRE(viewport.tick());
    REQUIRE(viewport.current_line() == 6);
    REQUIRE_FALSE(viewport.animation_pending());

    REQUIRE(viewport.scroll_down(20));
    while (viewport.animation_pending()) {
        REQUIRE(viewport.tick());
    }
    REQUIRE(viewport.current_line() == 9);
    REQUIRE(viewport.following_tail());
}

TEST_CASE("log viewport supports immediate home and end", "[tui][viewport]") {
    swe_agent::tui::LogViewport viewport;
    (void)viewport.sync(4);

    REQUIRE(viewport.home());
    REQUIRE(viewport.current_line() == 0);
    REQUIRE_FALSE(viewport.following_tail());

    REQUIRE(viewport.end());
    REQUIRE(viewport.current_line() == 3);
    REQUIRE(viewport.following_tail());
}

TEST_CASE("log viewport jumps to a selected logical line", "[tui][viewport]") {
    swe_agent::tui::LogViewport viewport;
    (void)viewport.sync(5);

    REQUIRE(viewport.jump_to(2));
    REQUIRE(viewport.current_line() == 2);
    REQUIRE_FALSE(viewport.following_tail());
    REQUIRE_FALSE(viewport.animation_pending());
    REQUIRE(viewport.jump_to(99));
    REQUIRE(viewport.current_line() == 4);
}

TEST_CASE("log viewport limits rendering around the current line", "[tui][viewport]") {
    swe_agent::tui::LogViewport viewport;
    (void)viewport.sync(1000);

    auto window = viewport.render_window(100);
    REQUIRE(window.begin == 900);
    REQUIRE(window.end == 1000);

    REQUIRE(viewport.jump_to(500));
    window = viewport.render_window(100);
    REQUIRE(window.begin == 450);
    REQUIRE(window.end == 550);

    REQUIRE(viewport.jump_to(10));
    window = viewport.render_window(100);
    REQUIRE(window.begin == 0);
    REQUIRE(window.end == 100);
}

TEST_CASE("log viewport accelerates long scroll distances", "[tui][viewport]") {
    swe_agent::tui::LogViewport viewport;
    (void)viewport.sync(1000);

    REQUIRE(viewport.scroll_up(900));
    REQUIRE(viewport.tick());
    REQUIRE(viewport.current_line() < 998);
    REQUIRE(viewport.current_line() > 99);

    std::size_t ticks = 1;
    while (viewport.animation_pending() && ticks < 80) {
        (void)viewport.tick();
        ++ticks;
    }
    REQUIRE_FALSE(viewport.animation_pending());
    REQUIRE(viewport.current_line() == 99);
    REQUIRE(ticks < 80);
}
