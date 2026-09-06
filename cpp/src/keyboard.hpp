#pragma once

#include <cstdint>

namespace olc { class PixelGameEngine; }

namespace hack {

// Mirrors RAM[0x6000] on the real machine: the code of whichever key is
// currently held down, 0 when none is. Printable keys report their ASCII
// code; special keys use these Hack keyboard-spec codes.
constexpr uint16_t KEY_ENTER = 128;
constexpr uint16_t KEY_BACKSPACE = 129;
constexpr uint16_t KEY_LEFT = 130;
constexpr uint16_t KEY_UP = 131;
constexpr uint16_t KEY_RIGHT = 132;
constexpr uint16_t KEY_DOWN = 133;

class HackKeyboard {
public:
    HackKeyboard() = default;

    uint16_t read() const { return current_; }

    /// Call once per frame to resync the register from the live key state.
    void poll(olc::PixelGameEngine& pge);

private:
    uint16_t current_ = 0;
};

} // namespace hack
