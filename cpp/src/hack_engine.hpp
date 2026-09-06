#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "screen.hpp"

namespace hack {

enum class RuntimeErrorReason {
    InvalidInstruction,
    InvalidReadAddress,
    InvalidWriteAddress,
    InvalidPC,
};

class HackRuntimeError : public std::runtime_error {
public:
    HackRuntimeError(RuntimeErrorReason reason, uint16_t address);
    explicit HackRuntimeError(RuntimeErrorReason reason);

    RuntimeErrorReason reason() const { return reason_; }

private:
    RuntimeErrorReason reason_;
};

struct BreakPoint {
    bool enabled = true;
};

struct WatchPoint {
    bool read = false;
    bool write = false;
    bool enabled = true;
};

enum class StopReason {
    RefreshUI,
    SysHalt,
    HardLoop,
    BreakPoint,
    WatchPoint,
};

class HackEngine {
public:
    HackEngine() = default;

    uint16_t pc = 0;
    uint16_t a = 0;
    uint16_t d = 0;
    std::array<uint16_t, 0x8000> ram{};
    std::array<uint16_t, 0x8000> rom{};
    uint16_t halt_addr = 0;
    float speed = 0.0f;
    size_t rom_words_loaded = 0;
    size_t ram_words_loaded = 0;

    std::map<uint16_t, BreakPoint> break_points;
    std::map<uint16_t, WatchPoint> watch_points;
    std::optional<uint16_t> triggered_watchpoint;

    HackScreen screen;
    uint16_t keyboard = 0;

    // Implemented in code_loader.cpp.
    void load_file(const std::string& bin);

    /// Rebuild `screen` from the raw RAM contents. Needed after bulk RAM
    /// writes (e.g. load_file) that bypass set_ram.
    void sync_screen_from_ram();

    bool set_ram(uint16_t address, uint16_t value); // returns ui_stop
    uint16_t get_ram(uint16_t address);

    /// Drain all bytes written to the output port (RAM[0x7FFF]) since the
    /// last call.
    std::string take_output();

    StopReason execute_instructions(std::chrono::duration<float> run_time);

    /// Execute up to `count` instructions with no wall-clock check at all —
    /// for callers implementing their own instructions-per-second throttle.
    StopReason execute_count(uint32_t count);

    std::tuple<uint16_t, uint16_t, uint16_t> get_registers() const;

    void add_breakpoint(uint16_t address);
    void remove_breakpoint(uint16_t address);
    void remove_all_breakpoints();

    void add_watchpoint(uint16_t address, bool read, bool write);
    void remove_watchpoint(uint16_t address);
    void remove_all_watchpoints();

    // Implemented later in this file.
    static std::string disassemble_one(uint16_t word);
    std::vector<std::tuple<uint16_t, uint16_t, std::string>> disassemble_range(
        uint16_t start, uint16_t count) const;

    /// Public (unlike the Rust original's module-private fn) so tests in a
    /// separate translation unit can exercise it directly.
    static uint16_t alu(uint16_t x_in, uint16_t y_in, uint16_t c);

private:
    /// Execute a single instruction. Returns the reason execution should
    /// stop (halt, hard loop, breakpoint, watchpoint), if any, plus whether
    /// this instruction wrote to the screen (ui_stop).
    std::pair<std::optional<StopReason>, bool> step();

    uint64_t inst_count_ = 0;
    std::vector<uint8_t> output_buffer_;
};

} // namespace hack
