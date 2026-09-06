#include <catch2/catch_test_macros.hpp>

TEST_CASE("build infrastructure works", "[smoke]") {
    REQUIRE(1 + 1 == 2);
}

#include "screen.hpp"

TEST_CASE("screen pixels round-trip through set/get", "[screen]") {
    hack::HackScreen screen;
    REQUIRE_FALSE(screen.get_pixel(0, 0));

    screen.set_pixel(0, 0, true);
    screen.set_pixel(1, 0, true);
    REQUIRE(screen.get_pixel(0, 0));
    REQUIRE(screen.get_pixel(1, 0));
    REQUIRE_FALSE(screen.get_pixel(2, 0));

    screen.set_pixel(0, 0, false);
    REQUIRE_FALSE(screen.get_pixel(0, 0));
}

TEST_CASE("screen out-of-bounds pixels are always off and ignored on write", "[screen]") {
    hack::HackScreen screen;
    REQUIRE_FALSE(screen.get_pixel(-1, 0));
    REQUIRE_FALSE(screen.get_pixel(0, -1));
    REQUIRE_FALSE(screen.get_pixel(hack::SCREEN_WIDTH, 0));
    REQUIRE_FALSE(screen.get_pixel(0, hack::SCREEN_HEIGHT));

    screen.set_pixel(hack::SCREEN_WIDTH, 0, true); // must not crash or affect in-bounds state
    REQUIRE_FALSE(screen.get_pixel(0, 0));
}

TEST_CASE("write_word/read_word round-trip", "[screen]") {
    hack::HackScreen screen;
    screen.write_word(5, 0xBEEF);
    REQUIRE(screen.read_word(5) == 0xBEEF);
}

TEST_CASE("write_word sets the pixels get_pixel reports", "[screen]") {
    hack::HackScreen screen;
    screen.write_word(0, 0x0003); // bits 0 and 1 set
    REQUIRE(screen.get_pixel(0, 0));
    REQUIRE(screen.get_pixel(1, 0));
    REQUIRE_FALSE(screen.get_pixel(2, 0));

    constexpr int screen_words_per_row = hack::SCREEN_WIDTH / 16;
    screen.write_word(screen_words_per_row, 0x8000); // row 1, bit 15
    REQUIRE(screen.get_pixel(15, 1));
}

TEST_CASE("clear resets every pixel", "[screen]") {
    hack::HackScreen screen;
    screen.set_pixel(10, 10, true);
    screen.clear();
    REQUIRE_FALSE(screen.get_pixel(10, 10));
}
