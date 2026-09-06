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

#include "keyboard.hpp"

TEST_CASE("keyboard starts with no key held", "[keyboard]") {
    hack::HackKeyboard kb;
    REQUIRE(kb.read() == 0);
}

#include "hack_engine.hpp"

namespace {

void test_alu_core(uint16_t x, uint16_t y) {
    using hack::HackEngine;
    REQUIRE(HackEngine::alu(x, y, 0x2a) == 0);
    REQUIRE(HackEngine::alu(x, y, 0x3f) == 1);
    REQUIRE(HackEngine::alu(x, y, 0x3a) == 0xffff);
    REQUIRE(HackEngine::alu(x, y, 0x0c) == x);
    REQUIRE(HackEngine::alu(x, y, 0x30) == y);
    REQUIRE(HackEngine::alu(x, y, 0x0d) == static_cast<uint16_t>(~x));
    REQUIRE(HackEngine::alu(x, y, 0x31) == static_cast<uint16_t>(~y));
    REQUIRE(HackEngine::alu(x, y, 0x0f) == static_cast<uint16_t>(0 - x));
    REQUIRE(HackEngine::alu(x, y, 0x33) == static_cast<uint16_t>(0 - y));
    REQUIRE(HackEngine::alu(x, y, 0x1f) == static_cast<uint16_t>(x + 1));
    REQUIRE(HackEngine::alu(x, y, 0x37) == static_cast<uint16_t>(y + 1));
    REQUIRE(HackEngine::alu(x, y, 0x0e) == static_cast<uint16_t>(x - 1));
    REQUIRE(HackEngine::alu(x, y, 0x32) == static_cast<uint16_t>(y - 1));
    REQUIRE(HackEngine::alu(x, y, 0x02) == static_cast<uint16_t>(x + y));
    REQUIRE(HackEngine::alu(x, y, 0x13) == static_cast<uint16_t>(x - y));
    REQUIRE(HackEngine::alu(x, y, 0x07) == static_cast<uint16_t>(y - x));
    REQUIRE(HackEngine::alu(x, y, 0x00) == static_cast<uint16_t>(x & y));
    REQUIRE(HackEngine::alu(x, y, 0x15) == static_cast<uint16_t>(x | y));
}

hack::StopReason run_to_hard_loop(hack::HackEngine& cpu) {
    using namespace std::chrono_literals;
    while (true) {
        auto reason = cpu.execute_instructions(0s);
        if (reason == hack::StopReason::HardLoop) return reason;
    }
}

} // namespace

TEST_CASE("alu matches the documented truth table exhaustively", "[engine][alu]") {
    for (uint32_t x = 0; x <= 0xffff; ++x) {
        for (uint32_t y = 0; y <= 0xff; ++y) {
            test_alu_core(static_cast<uint16_t>(x), static_cast<uint16_t>(y));
        }
    }
    for (uint32_t y = 0; y <= 0xffff; ++y) {
        for (uint32_t x = 0; x <= 0xff; ++x) {
            test_alu_core(static_cast<uint16_t>(x), static_cast<uint16_t>(y));
        }
    }
}

TEST_CASE("a small hand-assembled conditional program runs to completion", "[engine]") {
    hack::HackEngine cpu;
    // @2 / D=A / @17 / M=D / @10 / D;JLT / @3 / D=A / @16 / M=D / (POP) (HALT) / @HALT / D;JMP
    cpu.rom[0] = 0x0002;
    cpu.rom[1] = 0x8c10;
    cpu.rom[2] = 0x0011;
    cpu.rom[3] = 0x8308;
    cpu.rom[4] = 0x000a;
    cpu.rom[5] = 0x8304;
    cpu.rom[6] = 0x0003;
    cpu.rom[7] = 0x8c10;
    cpu.rom[8] = 0x0010;
    cpu.rom[9] = 0x8308;
    cpu.rom[10] = 0x000a;
    cpu.rom[11] = 0x8307;

    run_to_hard_loop(cpu);

    REQUIRE(cpu.ram[17] == 2);
    REQUIRE(cpu.ram[16] == 3);
}

namespace {

/// Builds a `D=<value>; @L1; D;<jump>; @1; M=1; (L1) @L1; D;JMP` program
/// (mirroring the Rust jump tests exactly) and returns ram[1]: 1 means the
/// branch was NOT taken, 0 means it was.
uint16_t run_jump_test(uint16_t d_setup, uint16_t jump_word) {
    hack::HackEngine cpu;
    cpu.rom[0] = d_setup;
    cpu.rom[1] = 0x0005;
    cpu.rom[2] = jump_word;
    cpu.rom[3] = 0x0001;
    cpu.rom[4] = 0xefc8;
    cpu.rom[5] = 0x0005;
    cpu.rom[6] = 0xe307;
    run_to_hard_loop(cpu);
    return cpu.ram[1];
}

} // namespace

TEST_CASE("conditional jumps take/skip the branch as expected", "[engine][jumps]") {
    // D=1
    CHECK(run_jump_test(0xefd0, 0xe301) == 0); // JGT taken
    CHECK(run_jump_test(0xefd0, 0xe302) == 1); // JEQ not taken
    CHECK(run_jump_test(0xefd0, 0xe303) == 0); // JGE taken
    CHECK(run_jump_test(0xefd0, 0xe305) == 0); // JNE taken
    CHECK(run_jump_test(0xefd0, 0xe306) == 1); // JLE not taken
    CHECK(run_jump_test(0xefd0, 0xe307) == 0); // JMP taken
    CHECK(run_jump_test(0xefd0, 0xe304) == 1); // JLT not taken

    // D=0
    CHECK(run_jump_test(0xea90, 0xe301) == 1); // JGT not taken
    CHECK(run_jump_test(0xea90, 0xe302) == 0); // JEQ taken
    CHECK(run_jump_test(0xea90, 0xe303) == 0); // JGE taken
    CHECK(run_jump_test(0xea90, 0xe304) == 1); // JLT not taken
    CHECK(run_jump_test(0xea90, 0xe305) == 1); // JNE not taken
    CHECK(run_jump_test(0xea90, 0xe306) == 0); // JLE taken
    CHECK(run_jump_test(0xea90, 0xe307) == 0); // JMP taken

    // D=-1
    CHECK(run_jump_test(0xee90, 0xe301) == 1); // JGT not taken
    CHECK(run_jump_test(0xee90, 0xe302) == 1); // JEQ not taken
    CHECK(run_jump_test(0xee90, 0xe303) == 1); // JGE not taken
    CHECK(run_jump_test(0xee90, 0xe304) == 0); // JLT taken
    CHECK(run_jump_test(0xee90, 0xe305) == 0); // JNE taken
    CHECK(run_jump_test(0xee90, 0xe306) == 0); // JLE taken
    CHECK(run_jump_test(0xee90, 0xe307) == 0); // JMP taken
}

TEST_CASE("set_ram/get_ram keep the screen in sync with RAM writes", "[engine]") {
    hack::HackEngine engine;
    engine.set_ram(0x4000, 0x0003);
    REQUIRE(engine.screen.get_pixel(0, 0));
    REQUIRE(engine.screen.get_pixel(1, 0));
    REQUIRE_FALSE(engine.screen.get_pixel(2, 0));

    constexpr int screen_words_per_row = hack::SCREEN_WIDTH / 16;
    uint16_t next_row = static_cast<uint16_t>(0x4000 + screen_words_per_row);
    engine.set_ram(next_row, 0x8000);
    REQUIRE(engine.screen.get_pixel(15, 1));
}

TEST_CASE("disassemble_one decodes A- and C-instructions", "[engine][disasm]") {
    using hack::HackEngine;
    REQUIRE(HackEngine::disassemble_one(0x0002) == "@2");
    REQUIRE(HackEngine::disassemble_one(0x8c10) == "D=A");
    REQUIRE(HackEngine::disassemble_one(0xe307) == "D;JMP");
}

TEST_CASE("disassemble_range walks consecutive ROM words", "[engine][disasm]") {
    hack::HackEngine cpu;
    cpu.rom[0] = 0x0002;
    cpu.rom[1] = 0x8c10;
    cpu.rom[2] = 0xe307;

    auto lines = cpu.disassemble_range(0, 3);
    REQUIRE(lines.size() == 3);
    REQUIRE(std::get<0>(lines[0]) == 0);
    REQUIRE(std::get<2>(lines[0]) == "@2");
    REQUIRE(std::get<2>(lines[1]) == "D=A");
    REQUIRE(std::get<2>(lines[2]) == "D;JMP");
}
