#include "screen.hpp"

#include "olcPixelGameEngine.h"

namespace hack {

void HackScreen::write_word(std::size_t offset, uint16_t value) {
    words_[offset] = value;
}

uint16_t HackScreen::read_word(std::size_t offset) const {
    return words_[offset];
}

void HackScreen::set_pixel(int32_t x, int32_t y, bool on) {
    auto loc = locate(x, y);
    if (!loc) return;
    auto [offset, mask] = *loc;
    if (on) {
        words_[offset] |= mask;
    } else {
        words_[offset] &= static_cast<uint16_t>(~mask);
    }
}

bool HackScreen::get_pixel(int32_t x, int32_t y) const {
    auto loc = locate(x, y);
    if (!loc) return false;
    auto [offset, mask] = *loc;
    return (words_[offset] & mask) != 0;
}

std::optional<std::pair<std::size_t, uint16_t>> HackScreen::locate(int32_t x, int32_t y) {
    if (x < 0 || y < 0 || x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) {
        return std::nullopt;
    }
    std::size_t word_col = static_cast<std::size_t>(x) / WORD_PIXELS;
    std::size_t bit = static_cast<std::size_t>(x) % WORD_PIXELS;
    std::size_t offset = static_cast<std::size_t>(y) * WORDS_PER_ROW + word_col;
    // Bit i is the i-th pixel from the left of the word. Confirmed against
    // both hackem's own test and hack_cc's reference emulator in the
    // original Rust port — naive bit==col is correct.
    return std::make_pair(offset, static_cast<uint16_t>(1u << bit));
}

void HackScreen::clear() {
    words_.fill(0);
}

void HackScreen::draw(olc::PixelGameEngine& pge) const {
    for (int32_t y = 0; y < SCREEN_HEIGHT; ++y) {
        for (std::size_t word_col = 0; word_col < WORDS_PER_ROW; ++word_col) {
            uint16_t word = words_[static_cast<std::size_t>(y) * WORDS_PER_ROW + word_col];
            if (word == 0) continue;
            int32_t base_x = static_cast<int32_t>(word_col * WORD_PIXELS);
            for (int bit = 0; bit < WORD_PIXELS; ++bit) {
                if (word & (1u << bit)) {
                    pge.Draw(base_x + bit, y, olc::BLACK);
                }
            }
        }
    }
}

} // namespace hack
