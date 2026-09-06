#include "hack_engine.hpp"

#include <string>

namespace hack {
namespace {

std::string format_reason(RuntimeErrorReason reason, std::optional<uint16_t> address) {
    switch (reason) {
        case RuntimeErrorReason::InvalidInstruction:
            return "Invalid instruction";
        case RuntimeErrorReason::InvalidReadAddress:
            return "Invalid RAM read address " + std::to_string(*address);
        case RuntimeErrorReason::InvalidWriteAddress:
            return "Invalid RAM write address " + std::to_string(*address);
        case RuntimeErrorReason::InvalidPC:
            return "Invalid instruction address " + std::to_string(*address);
    }
    return "Unknown error";
}

} // namespace

HackRuntimeError::HackRuntimeError(RuntimeErrorReason reason, uint16_t address)
    : std::runtime_error(format_reason(reason, address)), reason_(reason) {}

HackRuntimeError::HackRuntimeError(RuntimeErrorReason reason)
    : std::runtime_error(format_reason(reason, std::nullopt)), reason_(reason) {}

uint16_t HackEngine::alu(uint16_t x_in, uint16_t y_in, uint16_t c) {
    uint16_t zx = (c >> 5) & 0x1;
    uint16_t nx = (c >> 4) & 0x1;
    uint16_t zy = (c >> 3) & 0x1;
    uint16_t ny = (c >> 2) & 0x1;
    uint16_t f = (c >> 1) & 0x1;
    uint16_t no = c & 0x1;

    uint16_t x = zx ? 0 : x_in;
    x = nx ? static_cast<uint16_t>(~x) : x;

    uint16_t y = zy ? 0 : y_in;
    y = ny ? static_cast<uint16_t>(~y) : y;

    if (f) {
        uint16_t sum = static_cast<uint16_t>(x + y);
        return no ? static_cast<uint16_t>(~sum) : sum;
    }
    uint16_t bit_and = static_cast<uint16_t>(x & y);
    return no ? static_cast<uint16_t>(~bit_and) : bit_and;
}

void HackEngine::sync_screen_from_ram() {
    constexpr size_t SCREEN_WORDS = 0x2000;
    for (size_t offset = 0; offset < SCREEN_WORDS; ++offset) {
        screen.write_word(offset, ram[0x4000 + offset]);
    }
}

bool HackEngine::set_ram(uint16_t address, uint16_t value) {
    if (address >= 0x8000) {
        throw HackRuntimeError(RuntimeErrorReason::InvalidWriteAddress, address);
    }

    bool ui_stop = false;
    if (address <= 0x3fff) {
        ram[address] = value;
    } else if (address <= 0x5fff) {
        size_t offset = static_cast<size_t>(address) - 0x4000;
        ram[address] = value;
        screen.write_word(offset, value);
    } else if (address == 0x6000) {
        // keyboard - write ignored
    } else if (address == 0x7fff) {
        // output port: buffer the byte, never interrupt execution
        output_buffer_.push_back(static_cast<uint8_t>(value));
    } else {
        ram[address] = value;
    }

    if (auto wp = watch_points.find(address); wp != watch_points.end()) {
        if (wp->second.write && wp->second.enabled) {
            triggered_watchpoint = address;
        }
    }
    return ui_stop;
}

uint16_t HackEngine::get_ram(uint16_t address) {
    if (address >= 0x8000) {
        throw HackRuntimeError(RuntimeErrorReason::InvalidReadAddress, address);
    }
    if (address == 0x6000) {
        return keyboard;
    }
    if (auto wp = watch_points.find(address); wp != watch_points.end()) {
        if (wp->second.read && wp->second.enabled) {
            triggered_watchpoint = address;
        }
    }
    return ram[address];
}

std::string HackEngine::take_output() {
    std::string s(output_buffer_.begin(), output_buffer_.end());
    output_buffer_.clear();
    return s;
}

std::pair<std::optional<StopReason>, bool> HackEngine::step() {
    if (pc >= 0x8000) {
        throw HackRuntimeError(RuntimeErrorReason::InvalidPC, pc);
    }
    inst_count_++;

    uint16_t instruction = rom[pc];

    // did we hit a call to Sys.halt?
    if (halt_addr != 0 && pc == static_cast<uint16_t>(halt_addr + 1)) {
        return {StopReason::SysHalt, false};
    }

    uint16_t opcode = instruction >> 15;
    uint16_t old_pc = pc;
    bool ui_stop = false;
    pc = static_cast<uint16_t>(pc + 1);

    if (opcode == 0) {
        // A instruction
        a = instruction;
    } else {
        // C instruction
        uint16_t a_bit = (instruction >> 12) & 0x1;
        uint16_t c = (instruction >> 6) & 0x3F;
        uint16_t dest = (instruction >> 3) & 0x7;
        uint16_t j = instruction & 0x7;

        // cannot use A as a jump address and a ram read/write address in
        // the same instruction
        if (j != 0 && (dest & 0x1) != 0) {
            throw HackRuntimeError(RuntimeErrorReason::InvalidInstruction);
        }

        uint16_t y = (a_bit == 0) ? a : get_ram(a);
        uint16_t alu_out = alu(d, y, c);

        // M
        if (dest & 0x1) {
            ui_stop = set_ram(a, alu_out);
        }
        // D
        if (dest & 0x2) {
            d = alu_out;
        }
        // A (update is deferred til after jmp is tested)
        uint16_t new_a = (dest & 0x4) ? alu_out : a;

        uint16_t saved_pc = pc;

        if ((j & 0x1) && static_cast<int16_t>(alu_out) > 0) {
            pc = a;
        }
        if ((j & 0x2) && alu_out == 0) {
            pc = a;
        }
        if ((j & 0x4) && static_cast<int16_t>(alu_out) < 0) {
            pc = a;
        }

        a = new_a;

        // detects halt loop: (halt) @halt 0;JMP
        if (saved_pc > 2 && pc == static_cast<uint16_t>(saved_pc - 2)) {
            return {StopReason::HardLoop, ui_stop};
        }
    }

    if (auto bp = break_points.find(old_pc); bp != break_points.end()) {
        if (bp->second.enabled) {
            return {StopReason::BreakPoint, ui_stop};
        }
    }
    if (triggered_watchpoint.has_value()) {
        triggered_watchpoint.reset();
        return {StopReason::WatchPoint, ui_stop};
    }
    return {std::nullopt, ui_stop};
}

StopReason HackEngine::execute_instructions(std::chrono::duration<float> run_time) {
    using clock = std::chrono::steady_clock;
    auto start_time = clock::now();
    speed = 0.0f;
    int counter = 0;
    uint64_t inst_count_snap = inst_count_;

    while (true) {
        counter++;
        // every chunk of instructions check to see if we should refresh the
        // UI by returning to the caller
        if (counter > 1000) {
            auto elapsed = std::chrono::duration<float>(clock::now() - start_time);
            if (elapsed > run_time) {
                speed = static_cast<float>(inst_count_ - inst_count_snap) / elapsed.count() / 1'000'000.0f;
                return StopReason::RefreshUI;
            }
            counter = 0;
        }

        auto [stop, ui_stop] = step();
        if (stop.has_value()) {
            return *stop;
        }
        if (run_time.count() == 0.0f || ui_stop) {
            return StopReason::RefreshUI;
        }
    }
}

StopReason HackEngine::execute_count(uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        auto [stop, ui_stop] = step();
        (void)ui_stop;
        if (stop.has_value()) {
            return *stop;
        }
    }
    return StopReason::RefreshUI;
}

std::tuple<uint16_t, uint16_t, uint16_t> HackEngine::get_registers() const {
    return {pc, a, d};
}

void HackEngine::add_breakpoint(uint16_t address) {
    break_points[address] = BreakPoint{true};
}

void HackEngine::remove_breakpoint(uint16_t address) {
    break_points.erase(address);
}

void HackEngine::remove_all_breakpoints() {
    break_points.clear();
}

void HackEngine::add_watchpoint(uint16_t address, bool read, bool write) {
    watch_points[address] = WatchPoint{read, write, true};
}

void HackEngine::remove_watchpoint(uint16_t address) {
    watch_points.erase(address);
}

void HackEngine::remove_all_watchpoints() {
    watch_points.clear();
}

} // namespace hack
