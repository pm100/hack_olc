//! Memory-mapped Hack keyboard device.
//!
//! Mirrors RAM[0x6000] on the real machine: a single register holding the
//! code of whichever key is currently held down, 0 when none is. Printable
//! keys report their ASCII code; special keys use the codes from the Hack
//! keyboard spec (also used by hackem's `ui::key_lookup`), so this register
//! is drop-in compatible with a CPU wired in later.
//!
//! Note: olc's `Key` enum has no dedicated punctuation keys (comma, slash,
//! brackets, etc.) beyond `PERIOD`, so those aren't mapped here.

use olc_pixel_game_engine::{get_key, Key};

pub const KEY_ENTER: u16 = 128;
pub const KEY_BACKSPACE: u16 = 129;
pub const KEY_LEFT: u16 = 130;
pub const KEY_UP: u16 = 131;
pub const KEY_RIGHT: u16 = 132;
pub const KEY_DOWN: u16 = 133;

const LETTERS: [(Key, u8); 26] = [
    (Key::A, b'a'),
    (Key::B, b'b'),
    (Key::C, b'c'),
    (Key::D, b'd'),
    (Key::E, b'e'),
    (Key::F, b'f'),
    (Key::G, b'g'),
    (Key::H, b'h'),
    (Key::I, b'i'),
    (Key::J, b'j'),
    (Key::K, b'k'),
    (Key::L, b'l'),
    (Key::M, b'm'),
    (Key::N, b'n'),
    (Key::O, b'o'),
    (Key::P, b'p'),
    (Key::Q, b'q'),
    (Key::R, b'r'),
    (Key::S, b's'),
    (Key::T, b't'),
    (Key::U, b'u'),
    (Key::V, b'v'),
    (Key::W, b'w'),
    (Key::X, b'x'),
    (Key::Y, b'y'),
    (Key::Z, b'z'),
];

const DIGITS: [(Key, u8, u8); 10] = [
    (Key::K0, b'0', b')'),
    (Key::K1, b'1', b'!'),
    (Key::K2, b'2', b'@'),
    (Key::K3, b'3', b'#'),
    (Key::K4, b'4', b'$'),
    (Key::K5, b'5', b'%'),
    (Key::K6, b'6', b'^'),
    (Key::K7, b'7', b'&'),
    (Key::K8, b'8', b'*'),
    (Key::K9, b'9', b'('),
];

const SPECIALS: &[(Key, u16)] = &[
    (Key::SPACE, b' ' as u16),
    (Key::RETURN, KEY_ENTER),
    (Key::ENTER, KEY_ENTER),
    (Key::BACK, KEY_BACKSPACE),
    (Key::LEFT, KEY_LEFT),
    (Key::UP, KEY_UP),
    (Key::RIGHT, KEY_RIGHT),
    (Key::DOWN, KEY_DOWN),
    (Key::HOME, 134),
    (Key::END, 135),
    (Key::PGUP, 136),
    (Key::PGDN, 137),
    (Key::INS, 138),
    (Key::DEL, 139),
    (Key::ESCAPE, 140),
    (Key::F1, 141),
    (Key::F2, 142),
    (Key::F3, 143),
    (Key::F4, 144),
    (Key::F5, 145),
    (Key::F6, 146),
    (Key::F7, 147),
    (Key::F8, 148),
    (Key::F9, 149),
    (Key::F10, 150),
    (Key::F11, 151),
    (Key::F12, 152),
    (Key::PERIOD, b'.' as u16),
];

pub struct HackKeyboard {
    current: u16,
}

impl Default for HackKeyboard {
    fn default() -> Self {
        Self { current: 0 }
    }
}

impl HackKeyboard {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn read(&self) -> u16 {
        self.current
    }

    /// Call once per frame to resync the register from the live key state.
    pub fn poll(&mut self) {
        let shift = get_key(Key::SHIFT).held;

        for &(key, lower) in &LETTERS {
            if get_key(key).held {
                self.current = if shift {
                    lower.to_ascii_uppercase() as u16
                } else {
                    lower as u16
                };
                return;
            }
        }

        for &(key, digit, symbol) in &DIGITS {
            if get_key(key).held {
                self.current = if shift { symbol as u16 } else { digit as u16 };
                return;
            }
        }

        for &(key, code) in SPECIALS {
            if get_key(key).held {
                self.current = code;
                return;
            }
        }

        self.current = 0;
    }
}
