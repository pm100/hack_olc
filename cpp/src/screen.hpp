#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace olc { class PixelGameEngine; }

namespace hack {

constexpr int32_t SCREEN_WIDTH = 512;
constexpr int32_t SCREEN_HEIGHT = 256;

/// Memory-mapped Hack screen device: 512x256 pixels, 1 bit per pixel,
/// packed 16 pixels per 16-bit word, LSB first. On the real machine this
/// lives at RAM[0x4000..0x6000); `offset` here is address minus 0x4000, so
/// HackEngine can drive it with the exact same word offsets it already uses.
class HackScreen {
public:
    HackScreen() = default;

    /// Write a 16-bit word at screen-relative offset (0..0x2000), as the
    /// CPU would when it writes RAM[0x4000 + offset].
    void write_word(std::size_t offset, uint16_t value);
    uint16_t read_word(std::size_t offset) const;

    /// Set/clear a single pixel via read-modify-write of its containing
    /// word — the same operation a Hack program performs with `M=D`.
    void set_pixel(int32_t x, int32_t y, bool on);
    bool get_pixel(int32_t x, int32_t y) const;

    void clear();

    /// Full redraw from the word buffer, skipping all-zero words.
    void draw(olc::PixelGameEngine& pge) const;

private:
    static constexpr int WORD_PIXELS = 16;
    static constexpr std::size_t WORDS_PER_ROW =
        static_cast<std::size_t>(SCREEN_WIDTH) / WORD_PIXELS;
    static constexpr std::size_t WORD_COUNT =
        (static_cast<std::size_t>(SCREEN_WIDTH) * static_cast<std::size_t>(SCREEN_HEIGHT)) / WORD_PIXELS;

    static std::optional<std::pair<std::size_t, uint16_t>> locate(int32_t x, int32_t y);

    std::array<uint16_t, WORD_COUNT> words_{};
};

} // namespace hack
