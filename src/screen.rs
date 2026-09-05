//! Memory-mapped Hack screen device.
//!
//! Mirrors the real Hack machine's screen: 512x256 pixels, 1 bit per pixel,
//! packed 16 pixels per 16-bit word, LSB first. On the real machine this
//! lives at RAM[0x4000..0x6000); here `offset` is address minus 0x4000, so
//! the CPU (when wired in later — see hackem's `emulator::engine::HackEngine`)
//! can drive this with the exact same word offsets it already uses.

use olc_pixel_game_engine as olc;

pub const SCREEN_WIDTH: i32 = 512;
pub const SCREEN_HEIGHT: i32 = 256;
const WORD_PIXELS: usize = 16;
const WORDS_PER_ROW: usize = SCREEN_WIDTH as usize / WORD_PIXELS;
const WORD_COUNT: usize = (SCREEN_WIDTH as usize * SCREEN_HEIGHT as usize) / WORD_PIXELS; // 0x2000

pub struct HackScreen {
    words: [u16; WORD_COUNT],
}

impl Default for HackScreen {
    fn default() -> Self {
        Self {
            words: [0; WORD_COUNT],
        }
    }
}

// write_word/read_word/get_pixel aren't called yet — they're the API a CPU
// core wires up when it lands (mirroring HackEngine::set_ram/get_ram).
#[allow(dead_code)]
impl HackScreen {
    pub fn new() -> Self {
        Self::default()
    }

    /// Write a 16-bit word at screen-relative offset (0..0x2000), as the CPU
    /// would when it writes RAM[0x4000 + offset].
    pub fn write_word(&mut self, offset: usize, value: u16) {
        self.words[offset] = value;
    }

    pub fn read_word(&self, offset: usize) -> u16 {
        self.words[offset]
    }

    /// Set/clear a single pixel via read-modify-write of its containing word
    /// — the same operation a Hack program performs with `M=D`.
    pub fn set_pixel(&mut self, x: i32, y: i32, on: bool) {
        let Some((offset, mask)) = Self::locate(x, y) else {
            return;
        };
        if on {
            self.words[offset] |= mask;
        } else {
            self.words[offset] &= !mask;
        }
    }

    pub fn get_pixel(&self, x: i32, y: i32) -> bool {
        match Self::locate(x, y) {
            Some((offset, mask)) => (self.words[offset] & mask) != 0,
            None => false,
        }
    }

    fn locate(x: i32, y: i32) -> Option<(usize, u16)> {
        if x < 0 || y < 0 || x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT {
            return None;
        }
        let word_col = x as usize / WORD_PIXELS;
        let bit = x as usize % WORD_PIXELS;
        let offset = y as usize * WORDS_PER_ROW + word_col;
        // Bit i is the i-th pixel from the left of the word. Confirmed
        // against both hackem's own test and hack_cc's reference emulator
        // (hack_cc/src/bin/hack_emu.rs::pixel_set) — naive bit==col is
        // correct; an earlier "per-byte-reversed" theory here was wrong
        // (it broke tetris.hackem, which hackem renders correctly).
        Some((offset, 1u16 << bit))
    }

    pub fn clear(&mut self) {
        self.words = [0; WORD_COUNT];
    }

    /// Full redraw from the word buffer. Simple and correct; if this ever
    /// becomes a bottleneck once the CPU is driving it at full speed, copy
    /// hackem's dirty-word tracking (`ScreenUpdate::Partial` in
    /// `emulator/engine.rs`) so only changed words get re-blitted.
    pub fn draw(&self) {
        for y in 0..SCREEN_HEIGHT {
            for word_col in 0..WORDS_PER_ROW {
                let word = self.words[y as usize * WORDS_PER_ROW + word_col];
                if word == 0 {
                    continue;
                }
                let base_x = (word_col * WORD_PIXELS) as i32;
                for bit in 0..WORD_PIXELS {
                    if word & (1u16 << bit) != 0 {
                        olc::draw(base_x + bit as i32, y, olc::BLACK);
                    }
                }
            }
        }
    }
}
